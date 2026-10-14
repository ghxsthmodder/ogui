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
    src/style.cpp
    src/drawlist.cpp
    src/rasterizer.cpp
    src/font.cpp
    src/imagemanager.cpp
    src/widget.cpp
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

case "${1:-host}" in
    host) build_host ;;
    win)  build_win ;;
    clean)
        rm -rf build build-win debug/build-win-objs build-msvc debug/build-msvc
        find src debug/tests examples -name '*.o' -delete 2>/dev/null || true
        rm -f libogui.a demo.exe debug/demo.exe parser_test.exe
        echo "cleaned (debug/screenshots and debug/*.xml kept)"
        ;;
    *)
        echo "usage: $0 [host|win|clean]" >&2
        exit 2
        ;;
esac
