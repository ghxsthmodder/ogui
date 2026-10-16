// androidjni.cpp -- the input connection, seen from C++.
//
// An IME talks to a Java object: `InputConnection` methods are called on the
// activity's UI thread, and no NDK header can make one. So the Java half lives
// in android/src/dev/ogui/android, and this file is the other side of that
// boundary: it binds the class's `static native` declarations, answers the
// questions the connection is allowed to ask, and hands the app's draw thread a
// way to say which keyboard it wants.
//
// The widget tree is never touched from here. What crosses is the bridge's
// snapshot and the bridge's queue, both behind AndroidInputBridge's mutex, so
// the UI thread and the thread that draws can neither see half an edit nor block
// each other on a frame.
//
// Off Android this file is empty: nothing declares the symbols, and nothing
// calls them either -- see src/androidime.hpp.
#include "androidime.hpp"
#include "ogui/androidhost.hpp"
#include "ogui/font.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#ifdef __ANDROID__

#include <android/log.h>
#include <jni.h>

namespace ogui {
namespace {

std::mutex g_stateMutex;
AndroidInputBridge* g_bridge = nullptr;
JavaVM* g_vm = nullptr;
// Global references to the Java class and to the one static method native calls.
// Both are made in JNI_OnLoad and never released: the process outlives them.
jclass g_jniClass = nullptr;
jmethodID g_requestKeyboard = nullptr;

AndroidInputBridge* bridge() {
    std::lock_guard<std::mutex> lk(g_stateMutex);
    return g_bridge;
}

bool javaReady() {
    std::lock_guard<std::mutex> lk(g_stateMutex);
    return g_jniClass && g_requestKeyboard && g_vm;
}

// ---- UTF-16, the only encoding either side of this boundary speaks ----------
// jstring is UTF-16 and TextInput's text is UTF-8, and the platform's own
// "UTF" for JNI purposes is *modified* UTF-8, which spells an emoji as two
// surrogate halves in six bytes. So neither GetStringUTFChars nor
// NewStringUTF is ever called: what is typed and what is reported goes through
// these two functions, and a code point crosses as one code point.

void appendUtf8(std::string& out, uint32_t cp) {
    // A lone half means nothing on its own; it becomes the replacement rune.
    if (cp >= 0xD800u && cp <= 0xDFFFu) cp = 0xFFFDu;
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

std::string fromJava(JNIEnv* env, jstring text) {
    if (!text) return std::string();
    const jsize len = env->GetStringLength(text);
    const jchar* chars = env->GetStringChars(text, nullptr);
    if (!chars) return std::string();
    std::string out;
    out.reserve(static_cast<size_t>(len));
    for (jsize i = 0; i < len; ++i) {
        uint32_t cp = chars[i];
        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < len) {
            const uint32_t low = chars[i + 1];
            if (low >= 0xDC00u && low <= 0xDFFFu) {
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                ++i;
            }
        }
        appendUtf8(out, cp);
    }
    env->ReleaseStringChars(text, chars);
    return out;
}

jstring toJava(JNIEnv* env, const std::string& utf8) {
    std::vector<jchar> units;
    units.reserve(utf8.size());
    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8.data());
    const unsigned char* end = p + utf8.size();
    while (p < end) {
        const uint32_t cp = utf8Next(p);   // utf8.data() is NUL-terminated
        if (cp > 0xFFFFu) {
            const uint32_t v = cp - 0x10000u;
            units.push_back(static_cast<jchar>(0xD800u + (v >> 10)));
            units.push_back(static_cast<jchar>(0xDC00u + (v & 0x3FFu)));
        } else {
            units.push_back(static_cast<jchar>(cp));
        }
    }
    return env->NewString(units.empty() ? nullptr : units.data(),
                          static_cast<jsize>(units.size()));
}

// ---- the queue: what the IME asked for -------------------------------------

void queue(ImeEdit::Kind kind, JNIEnv* env, jstring text, jint cursor) {
    ImeEdit edit;
    edit.kind = kind;
    edit.text = text ? fromJava(env, text) : std::string();
    edit.cursor = cursor;
    if (auto* b = bridge()) b->queueEdit(std::move(edit));
}

void queue(ImeEdit::Kind kind) {
    ImeEdit edit;
    edit.kind = kind;
    if (auto* b = bridge()) b->queueEdit(std::move(edit));
}

void nativeCommitText(JNIEnv* env, jclass, jstring text,
                    jint newCursorPosition) {
    queue(ImeEdit::Kind::Commit, env, text, newCursorPosition);
}

void nativeSetComposingText(JNIEnv* env, jclass, jstring text,
                           jint newCursorPosition) {
    queue(ImeEdit::Kind::Composing, env, text, newCursorPosition);
}

void nativeFinishComposingText(JNIEnv*, jclass) {
    queue(ImeEdit::Kind::Finish);
}

void nativeSetComposingRegion(JNIEnv*, jclass, jint start, jint end) {
    ImeEdit edit;
    edit.kind = ImeEdit::Kind::ComposingRegion;
    edit.start = start;
    edit.end = end;
    if (auto* b = bridge()) b->queueEdit(std::move(edit));
}

void nativeDeleteSurroundingText(JNIEnv*, jclass, jint beforeLength,
                                 jint afterLength) {
    ImeEdit edit;
    edit.kind = ImeEdit::Kind::Delete;
    edit.before = beforeLength;
    edit.after = afterLength;
    if (auto* b = bridge()) b->queueEdit(std::move(edit));
}

void nativeSetSelection(JNIEnv*, jclass, jint start, jint end) {
    ImeEdit edit;
    edit.kind = ImeEdit::Kind::Select;
    edit.start = start;
    edit.end = end;
    if (auto* b = bridge()) b->queueEdit(std::move(edit));
}

void nativeSendKeyEvent(JNIEnv*, jclass, jint keyCode, jboolean down) {
    ImeEdit edit;
    edit.kind = ImeEdit::Kind::Key;
    edit.key = keyCode;
    edit.down = down == JNI_TRUE;
    if (auto* b = bridge()) b->queueEdit(std::move(edit));
}

void nativePerformEditorAction(JNIEnv*, jclass, jint actionCode) {
    ImeEdit edit;
    edit.kind = ImeEdit::Kind::Action;
    edit.action = actionCode;
    if (auto* b = bridge()) b->queueEdit(std::move(edit));
}

// ---- the snapshot: what the field says back --------------------------------
// Every position is answered in UTF-16 code units, because that is what the
// caller counts in, and the snapshot keeps byte offsets, because that is what
// the widget keeps. imeCountUnits is the whole of the translation, and -1 is the
// answer for "there is no composition" -- a span of nothing and no span are not
// the same thing to an IME.

jstring nativeGetText(JNIEnv* env, jclass) {
    auto* b = bridge();
    return toJava(env, b ? b->editor().text : std::string());
}

jint nativeGetSelectionStart(JNIEnv*, jclass) {
    auto* b = bridge();
    if (!b) return 0;
    const ImeEditor e = b->editor();
    return static_cast<jint>(imeCountUnits(e.text, 0, e.selStart));
}

jint nativeGetSelectionEnd(JNIEnv*, jclass) {
    auto* b = bridge();
    if (!b) return 0;
    const ImeEditor e = b->editor();
    return static_cast<jint>(imeCountUnits(e.text, 0, e.selEnd));
}

jint nativeGetComposingStart(JNIEnv*, jclass) {
    auto* b = bridge();
    if (!b) return -1;
    const ImeEditor e = b->editor();
    if (!e.composing) return -1;
    return static_cast<jint>(imeCountUnits(e.text, 0, e.compStart));
}

jint nativeGetComposingEnd(JNIEnv*, jclass) {
    auto* b = bridge();
    if (!b) return -1;
    const ImeEditor e = b->editor();
    if (!e.composing) return -1;
    return static_cast<jint>(imeCountUnits(e.text, 0, e.compEnd));
}

jboolean nativeHasEditor(JNIEnv*, jclass) {
    auto* b = bridge();
    return b && b->editor().field ? JNI_TRUE : JNI_FALSE;
}

// One entry per `static native` in OguiJni.java, with the signature that
// declaration has. RegisterNatives refuses the whole table if one name or one
// signature does not match the class, and the alternative to finding that out
// here is an UnsatisfiedLinkError on a phone -- which is why scripts/build.sh
// diffs this table against the Java file before it packs the APK.
const JNINativeMethod kMethods[] = {
    {"nativeCommitText", "(Ljava/lang/String;I)V",
     reinterpret_cast<void*>(nativeCommitText)},
    {"nativeSetComposingText", "(Ljava/lang/String;I)V",
     reinterpret_cast<void*>(nativeSetComposingText)},
    {"nativeFinishComposingText", "()V",
     reinterpret_cast<void*>(nativeFinishComposingText)},
    {"nativeSetComposingRegion", "(II)V",
     reinterpret_cast<void*>(nativeSetComposingRegion)},
    {"nativeDeleteSurroundingText", "(II)V",
     reinterpret_cast<void*>(nativeDeleteSurroundingText)},
    {"nativeSetSelection", "(II)V",
     reinterpret_cast<void*>(nativeSetSelection)},
    {"nativeSendKeyEvent", "(IZ)V",
     reinterpret_cast<void*>(nativeSendKeyEvent)},
    {"nativePerformEditorAction", "(I)V",
     reinterpret_cast<void*>(nativePerformEditorAction)},
    {"nativeGetText", "()Ljava/lang/String;",
     reinterpret_cast<void*>(nativeGetText)},
    {"nativeGetSelectionStart", "()I",
     reinterpret_cast<void*>(nativeGetSelectionStart)},
    {"nativeGetSelectionEnd", "()I",
     reinterpret_cast<void*>(nativeGetSelectionEnd)},
    {"nativeGetComposingStart", "()I",
     reinterpret_cast<void*>(nativeGetComposingStart)},
    {"nativeGetComposingEnd", "()I",
     reinterpret_cast<void*>(nativeGetComposingEnd)},
    {"nativeHasEditor", "()Z",
     reinterpret_cast<void*>(nativeHasEditor)},
};

const char* const kClassName = "dev/ogui/android/OguiJni";

} // namespace

// The VM runs this when a System.loadLibrary brings the file in -- and the only
// loadLibrary here is OguiJni's own static block, not the activity: NativeActivity
// dlopen()s the library to find android_main, and a dlopen from native code never
// calls this. Binding by name happens here rather than by the VM's search of the
// Java_* symbols, which would fail one method at a time, in a phone, at runtime.
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    g_vm = vm;

    jclass local = env->FindClass(kClassName);
    if (!local) {
        // No classes.dex in the archive (a build that skipped the Java half):
        // the host still has its NDK fallback, so say what is missing and let
        // the exception go rather than carrying a pending one into Java.
        if (env->ExceptionCheck()) env->ExceptionClear();
        __android_log_print(ANDROID_LOG_WARN, "ogui",
                            "no %s: the input connection is not available",
                            kClassName);
        return JNI_VERSION_1_6;
    }
    if (env->RegisterNatives(local, kMethods,
                             static_cast<jint>(sizeof(kMethods) /
                                               sizeof(kMethods[0]))) != JNI_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "ogui", "RegisterNatives failed");
        env->DeleteLocalRef(local);
        return JNI_ERR;
    }
    g_jniClass = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);

    // (inputType, imeOptions, visible) -> shown or hidden; false when the
    // activity has no editor view yet, which is the cue to ask again next frame.
    g_requestKeyboard = env->GetStaticMethodID(
        g_jniClass, "requestKeyboard", "(IIZ)Z");
    if (!g_requestKeyboard) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, "ogui",
                            "OguiJni.requestKeyboard(int, int, boolean) is missing");
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

namespace {

// The thread that draws is native_app_glue's, not the VM's, so it has to ask
// for a JNIEnv of its own. Attached once and left attached: the thread outlives
// every call made on it, and detaching on the way out would only turn a late
// keyboard request into a crash.
JNIEnv* appEnv() {
    JavaVM* vm = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_stateMutex);
        vm = g_vm;
    }
    if (!vm) return nullptr;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    return env;
}

} // namespace

void androidImeAttach(AndroidInputBridge* target) {
    std::lock_guard<std::mutex> lk(g_stateMutex);
    g_bridge = target;
}

void androidImeDetach() {
    std::lock_guard<std::mutex> lk(g_stateMutex);
    g_bridge = nullptr;
}

bool androidImeAvailable() { return javaReady(); }

bool androidImeSendRequest(const ImeRequest& request) {
    jclass clazz = nullptr;
    jmethodID method = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_stateMutex);
        clazz = g_jniClass;
        method = g_requestKeyboard;
    }
    JNIEnv* env = appEnv();
    if (!env || !clazz || !method) return false;
    const jboolean delivered = env->CallStaticBooleanMethod(
        clazz, method, request.inputType, request.imeOptions,
        request.visible ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck()) {
        // A Java-side failure is not the app's to act on; the pending exception
        // would be, because the next JNI call would refuse to run with it set.
        env->ExceptionClear();
        return false;
    }
    return delivered == JNI_TRUE;
}

} // namespace ogui

#endif  // __ANDROID__
