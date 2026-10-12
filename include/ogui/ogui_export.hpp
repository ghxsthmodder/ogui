// ogui_export.hpp : import/export macros for shared/static builds.
#pragma once

#if defined(OGUI_STATIC)
    // built as a static library -> no import/export
    #define OGUI_EXPORT
    #define OGUI_API
#elif defined(OGUI_BUILD_SHARED)
    #ifdef OGUI_EXPORT
        // already defined by the importer (dllimport)
    #elif defined(_WIN32)
        #define OGUI_EXPORT __declspec(dllimport)
        #define OGUI_API __declspec(dllexport)
    #else
        #define OGUI_EXPORT __attribute__((visibility("default")))
        #define OGUI_API __attribute__((visibility("default")))
    #endif
#else
    #define OGUI_EXPORT
    #define OGUI_API
#endif
