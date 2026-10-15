#!/usr/bin/env bash
# Build script for ogui.
#
#   ./scripts/build.sh              # host build (Linux/macOS) + tests
#   ./scripts/build.sh win          # cross-compile static Windows demo.exe
#   ./scripts/build.sh clean        # remove build trees
#
# Host builds use CMake. The Windows target uses mingw directly so the
# resulting demo.exe is fully static (no libstdc++-6.dll / libgcc_s_seh-1.dll).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

WIN_CXX="${WIN_CXX:-x86_64-w64-mingw32-g++}"
WIN_CC="${WIN_CC:-x86_64-w64-mingw32-gcc}"
WIN_AR="${WIN_AR:-x86_64-w64-mingw32-ar}"

LIB_SRCS=(
    src/api.cpp
    src/assets.cpp
    src/style.cpp
    src/drawlist.cpp
    src/rasterizer.cpp
    src/font.cpp
    src/imagemanager.cpp
    src/widget.cpp
    src/properties.cpp
    src/widgets.cpp
    src/widgets_text.cpp
    src/widgets_container.cpp
    src/widgets_input.cpp
    src/widgets_list.cpp
    src/widgets_editor.cpp
    src/clipboard.cpp
    src/software.cpp
    src/kernel.cpp
    src/xmlparser.cpp
    src/glbackend.cpp
    src/androidinput.cpp
    src/androidhost.cpp
    src/tween.cpp
    src/host.cpp
    src/script.cpp
)

build_host() {
    cmake -S . -B build -DOGUI_BUILD_EXAMPLES=ON -DOGUI_BUILD_TESTS=ON
    cmake --build build -j"$(nproc 2>/dev/null || echo 4)"
    echo
    echo "== running tests =="
    (cd build && ctest --output-on-failure)
    echo
    echo "== headless demo =="
    ./build/ogui_demo
}

build_win() {
    command -v "$WIN_CXX" >/dev/null 2>&1 || {
        echo "error: $WIN_CXX not found (install mingw-w64)" >&2
        exit 1
    }

    # Everything test-related lives in debug/ so the folder can be downloaded
    # whole (SFTP) and run on Windows: demo.exe + its XML screens.
    mkdir -p debug debug/build-win-objs
    local flags=(-std=c++20 -O2 -static -static-libgcc -static-libstdc++
                 -Iinclude -Ithirdparty/stb -Ithirdparty/lua/src
                 -DOGUI_USE_STB=1 -DOGUI_HAS_LUA=1)

    local objs=()
    # Vendored Lua 5.4 interpreter (plain C, warnings off).
    for src in thirdparty/lua/src/*.c; do
        local obj="debug/build-win-objs/lua_$(basename "${src%.c}").o"
        echo "  CC  $src"
        "$WIN_CC" -O2 -w -Ithirdparty/lua/src -c "$src" -o "$obj"
        objs+=("$obj")
    done
    for src in "${LIB_SRCS[@]}"; do
        local obj="debug/build-win-objs/$(basename "${src%.cpp}").o"
        echo "  CXX $src"
        "$WIN_CXX" "${flags[@]}" -c "$src" -o "$obj"
        objs+=("$obj")
    done

    echo "  AR  libogui.a"
    "$WIN_AR" rcs debug/build-win-objs/libogui.a "${objs[@]}"

    echo "  CXX examples/demo_win32.cpp -> debug/demo.exe"
    # -mwindows: GUI subsystem (no stray console window), like the MSVC project.
    # -lshell32: DragAcceptFiles/DragQueryFile for the drop-white-files demo.
    "$WIN_CXX" "${flags[@]}" -mwindows examples/demo_win32.cpp debug/build-win-objs/libogui.a \
        -o debug/demo.exe -lgdi32 -lopengl32 -lshell32

    # Layout checker — console subsystem, so it can run from a shell or an
    # editor save hook.
    echo "  CXX examples/validate.cpp -> debug/validate.exe"
    "$WIN_CXX" "${flags[@]}" examples/validate.cpp debug/build-win-objs/libogui.a \
        -o debug/validate.exe

    # Sync the screens the demo loads at runtime into debug/ (the source of
    # truth stays at the project root; the copy makes debug/ self-contained).
    cp -f manifest.xml layout_demo.xml debug/
    rm -rf debug/layouts && cp -r layouts debug/layouts
    rm -rf debug/assets && cp -r assets debug/assets
    rm -rf debug/themes && cp -r themes debug/themes

    echo
    echo "== demo.exe imports =="
    if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
        x86_64-w64-mingw32-objdump -p debug/demo.exe | grep -i "DLL Name" || true
    fi
    echo "built: $ROOT/debug/demo.exe"
}

build_android() {
    local NDK="${ANDROID_NDK:-/opt/android-ndk-r30}"
    local SDK="${ANDROID_SDK:-/opt/android-sdk}"
    local ABIS="${ANDROID_ABIS:-arm64-v8a}"
    local API="${ANDROID_API:-24}"
    local BT PLATFORM
    BT="$(ls -d "$SDK"/build-tools/*/ 2>/dev/null | sort -V | tail -1)"
    PLATFORM="$(ls -d "$SDK"/platforms/android-*/ 2>/dev/null | sort -V | tail -1)android.jar"

    [ -f "$NDK/build/cmake/android.toolchain.cmake" ] || {
        echo "error: no NDK at $NDK (set ANDROID_NDK)" >&2; exit 1; }
    [ -n "$BT" ] && [ -x "${BT}aapt2" ] || {
        echo "error: no build-tools in $SDK (set ANDROID_SDK)" >&2; exit 1; }
    [ -f "$PLATFORM" ] || {
        echo "error: no android.jar in $SDK/platforms" >&2; exit 1; }
    command -v python3 >/dev/null || {
        echo "error: python3 needed to add the libraries to the archive" >&2; exit 1; }

    local STRIP="${NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
    command -v "$STRIP" >/dev/null 2>&1 || STRIP="$(command -v llvm-strip || true)"
    [ -n "$STRIP" ] || {
        echo "error: no llvm-strip found (set ANDROID_NDK)" >&2; exit 1; }

    local stage="$ROOT/build-apk"
    rm -rf "$stage"; mkdir -p "$stage/lib" "$stage/assets"

    # ---- 1. the native library, one build per ABI --------------------------
    # Examples stay off: the app target is built by its own if(ANDROID) block,
    # and the other five are console programs with no console on a phone.
    for abi in $ABIS; do
        echo "== $abi =="
        cmake -S "$ROOT" -B "$ROOT/build-android-$abi" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$API" \
            -DANDROID_STL=c++_static \
            -DOGUI_BUILD_EXAMPLES=OFF -DOGUI_BUILD_TESTS=OFF >/dev/null
        cmake --build "$ROOT/build-android-$abi" --target oggui_app \
            -j"$(nproc 2>/dev/null || echo 4)"
        mkdir -p "$stage/lib/$abi"
        cp "$ROOT/build-android-$abi/liboggui_app.so" "$stage/lib/$abi/"
        # Debug info and the internals the dynamic linker never ask for are ten
        # times the library; the ELF that gets mmap'd out of the APK is the one
        # that has to keep its exported symbols, so --strip-unneeded and no more.
        "$STRIP" --strip-unneeded "$stage/lib/$abi/liboggui_app.so"
        ls -lh "$stage/lib/$abi/liboggui_app.so" | awk '{print "  stripped: "$5}'
    done

    # ---- 2. the screens, inside the archive --------------------------------
    # The asset reader installed by the host is what makes these names work;
    # nothing on the device has a filesystem the app may read.
    cp -f "$ROOT/manifest.xml" "$ROOT/layout_demo.xml" "$stage/assets/"
    cp -r "$ROOT/layouts" "$stage/assets/layouts"
    [ -d "$ROOT/themes" ] && cp -r "$ROOT/themes" "$stage/assets/themes"

    # ---- 3. manifest + assets into an archive ------------------------------
    local out="$stage/ogui-demo-debug.apk"
    "${BT}aapt2" link -I "$PLATFORM" \
        --manifest "$ROOT/android/AndroidManifest.xml" \
        --min-sdk-version "$API" \
        --target-sdk-version 36 \
        -A "$stage/assets" \
        -o "$stage/unaligned.apk"

    # The native libraries are not aapt2's business: stored (extractNativeLibs
    # is false, so the loader mmaps them straight out of the archive) and then
    # page-aligned by zipalign below.
    python3 - "$stage/unaligned.apk" "$stage/lib" <<'PY'
import os, sys, zipfile
apk, libroot = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(apk, 'a', zipfile.ZIP_STORED) as z:
    for abi in sorted(os.listdir(libroot)):
        so = os.path.join(libroot, abi, 'liboggui_app.so')
        z.write(so, f'lib/{abi}/liboggui_app.so')
        print(f'  + lib/{abi}/liboggui_app.so')
PY

    # ---- 4. align, sign with the debug key, verify -------------------------
    local KS="${ANDROID_DEBUG_KEYSTORE:-$HOME/.android/debug.keystore}"
    if [ ! -f "$KS" ]; then
        mkdir -p "$(dirname "$KS")"
        keytool -genkeypair -v -keystore "$KS" -alias androiddebugkey \
            -keyalg RSA -keysize 2048 -validity 10000 \
            -storepass android -keypass android \
            -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
    fi
    "${BT}zipalign" -f -p 4 "$stage/unaligned.apk" "$stage/aligned.apk"
    "${BT}apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
        --out "$out" "$stage/aligned.apk"
    "${BT}apksigner" verify --print-certs "$out" | head -3
    echo "built: $out"
}

case "${1:-host}" in
    host) build_host ;;
    win)  build_win ;;
    android) build_android ;;
    clean)
        rm -rf build build-win debug/build-win-objs build-msvc debug/build-msvc
        rm -rf build-android build-apk
        find src debug/tests examples -name '*.o' -delete 2>/dev/null || true
        rm -f libogui.a demo.exe debug/demo.exe parser_test.exe
        echo "cleaned (debug/screenshots and debug/*.xml kept)"
        ;;
    *)
        echo "usage: $0 [host|win|android|clean]" >&2
        exit 2
        ;;
esac
