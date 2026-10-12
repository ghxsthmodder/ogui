cmake_minimum_required(VERSION 3.16)
project(ogui LANGUAGES C CXX)

set(OGUI_VERSION_MAJOR 0)
set(OGUI_VERSION_MINOR 1)
set(OGUI_VERSION_PATCH 0)
set(OGUI_VERSION "${OGUI_VERSION_MAJOR}.${OGUI_VERSION_MINOR}.${OGUI_VERSION_PATCH}")

# ---- Options -------------------------------------------------------------
option(OGUI_BUILD_EXAMPLES "Build example apps" ON)
option(OGUI_BUILD_TESTS "Build unit tests" ON)
option(OGUI_USE_STATIC_STB "Statically link stb_truetype + stb_image (Windows-safe)" ON)

# ---- Compiler / flags ----------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(OGUI_ARCH "x86_64" CACHE STRING "Target architecture")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(OGUI_ARCH "x86" CACHE STRING "Target architecture")
endif()

# ---- MSVC tweaks ---------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /WX- /Zc:__cplusplus)
    add_link_options(/ignore:4099)
endif()

# ---- stb_truetype + stb_image (static, no system deps) --------------------
if(OGUI_USE_STATIC_STB)
    # CURRENT_SOURCE_DIR (not CMAKE_SOURCE_DIR) so add_subdirectory() from a
    # consuming project resolves these against this file's directory.
    set(STB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb")
    if(NOT EXISTS "${STB_DIR}/stb_truetype.h")
        find_package(Git)
        if(GIT_FOUND)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} clone --depth 1 https://github.com/nothings/stb.git ${STB_DIR}
                OUTPUT_QUIET
            )
        endif()
    endif()
    if(NOT EXISTS "${STB_DIR}/stb_truetype.h")
        message(FATAL_ERROR "stb not found (see thirdparty/README.md)")
    endif()
    add_compile_definitions(OGUI_USE_STB=1)
endif()

# ---- Include dirs --------------------------------------------------------
# target_include_directories is used for both the INTERFACE and the target,
# so it is placed right after add_library().

# ---- Library -------------------------------------------------------------
add_library(ogui STATIC
    src/api.cpp
    src/style.cpp
    src/drawlist.cpp
    src/rasterizer.cpp
    src/font.cpp
    src/imagemanager.cpp
    src/widget.cpp
    src/widgets.cpp
    src/clipboard.cpp
    src/software.cpp
    src/kernel.cpp
    src/xmlparser.cpp
    src/glbackend.cpp
    src/tween.cpp
    src/host.cpp
)

# The GL backend resolves libGL at runtime (dlopen/GetProcAddress); no
# link-time dependency is added on purpose.
if(UNIX AND NOT APPLE)
    target_link_libraries(ogui PRIVATE ${CMAKE_DL_LIBS})
endif()

target_include_directories(ogui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(OGUI_USE_STATIC_STB)
    target_include_directories(ogui PRIVATE ${STB_DIR})
endif()

target_compile_definitions(ogui PRIVATE
    OGUI_VERSION_STRING="${OGUI_VERSION}"
)

# ---- Examples ------------------------------------------------------------
if(OGUI_BUILD_EXAMPLES)
    add_executable(ogui_demo examples/demo.cpp)
    target_link_libraries(ogui_demo PRIVATE ogui)
    target_compile_definitions(ogui_demo PRIVATE OGUI_DEMO_EXECUTABLE)

    # Headless PNG dump of the DrawList (debug aid; needs stb_image_write).
    if(OGUI_USE_STATIC_STB)
        add_executable(ogui_screenshot examples/screenshot.cpp)
        target_link_libraries(ogui_screenshot PRIVATE ogui)
        target_include_directories(ogui_screenshot PRIVATE ${STB_DIR})
    endif()

    # Win32 + WGL reference host. Only meaningful on Windows (or when
    # cross-compiling with mingw).
    if(WIN32 OR MINGW)
        add_executable(ogui_demo_win32 examples/demo_win32.cpp)
        target_link_libraries(ogui_demo_win32 PRIVATE ogui)
        if(MINGW)
            # Static runtime: avoids the libstdc++-6.dll / libgcc_s_seh-1.dll
            # errors a bare `demo.exe` used to hit on target machines.
            target_link_options(ogui_demo_win32 PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
        target_link_libraries(ogui_demo_win32 PRIVATE gdi32 opengl32)
    endif()
endif()

# ---- Tests ---------------------------------------------------------------
if(OGUI_BUILD_TESTS)
    add_executable(ogui_parser_test tests/parser_test.cpp)
    target_link_libraries(ogui_parser_test PRIVATE ogui)

    add_executable(ogui_drawlist_test tests/drawlist_test.cpp)
    target_link_libraries(ogui_drawlist_test PRIVATE ogui)

    add_executable(ogui_kernel_test tests/kernel_test.cpp)
    target_link_libraries(ogui_kernel_test PRIVATE ogui)
    target_compile_definitions(ogui_kernel_test PRIVATE
        OGUI_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

    # Golden-image test: renders a fixed layout and diffs it against
    # tests/golden/basic.png (regenerate with OGUI_UPDATE_GOLDEN=1).
    if(OGUI_USE_STATIC_STB)
        add_executable(ogui_golden_test tests/golden_test.cpp)
        target_link_libraries(ogui_golden_test PRIVATE ogui)
        target_include_directories(ogui_golden_test PRIVATE ${STB_DIR})
        target_compile_definitions(ogui_golden_test PRIVATE
            OGUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
    endif()

    enable_testing()
    add_test(NAME parser COMMAND ogui_parser_test)
    add_test(NAME drawlist COMMAND ogui_drawlist_test)
    add_test(NAME kernel COMMAND ogui_kernel_test)
    if(TARGET ogui_golden_test)
        add_test(NAME golden COMMAND ogui_golden_test)
    endif()
endif()

# ---- Install -------------------------------------------------------------
# Layout follows the GNU directories so a plain `cmake --install` lands in a
# prefix that find_package() can search.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS ogui
    EXPORT ogui-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Examples and tests are only installed when explicitly built, and the
# targets may not exist in a partial build configuration.
foreach(t ogui_demo ogui_demo_win32 ogui_screenshot ogui_parser_test ogui_drawlist_test ogui_kernel_test ogui_golden_test)
    if(TARGET ${t})
        install(TARGETS ${t} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endforeach()

install(FILES
    include/ogui/api.hpp
    include/ogui/clipboard.hpp
    include/ogui/drawlist.hpp
    include/ogui/font.hpp
    include/ogui/imagemanager.hpp
    include/ogui/glbackend.hpp
    include/ogui/kernel.hpp
    include/ogui/rasterizer.hpp
    include/ogui/software.hpp
    include/ogui/style.hpp
    include/ogui/widget.hpp
    include/ogui/widgets.hpp
    include/ogui/host.hpp
    include/ogui/tween.hpp
    include/ogui/xmlparser.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ogui
)

# ---- CMake package (find_package(ogui CONFIG)) ---------------------------
# Imported target: ogui::ogui
install(EXPORT ogui-targets
    FILE ogui-targets.cmake
    NAMESPACE ogui::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ogui
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ogui-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ogui-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ogui
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ogui-config-version.cmake
    VERSION ${OGUI_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ogui-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ogui-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ogui
)
