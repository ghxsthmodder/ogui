package dev.ogui.android;

/**
 * The declarations {@code src/androidjni.cpp} binds, and the one method it
 * calls.
 *
 * Nothing here is optional on either side: {@code RegisterNatives} refuses the
 * whole table if a name or a signature does not match the class, and the
 * alternative to finding that out while building is an
 * {@code UnsatisfiedLinkError} in the middle of a phone call. So
 * {@code scripts/build.sh} diffs these declarations against the C++ table
 * before it packs the archive.
 *
 * Every position that crosses is a UTF-16 code unit, because that is what an
 * {@code InputConnection} counts in, and every string is UTF-8 on the way back
 * to the widget tree, because that is what {@code TextInput} keeps. One emoji is
 * two of the first and four of the second, which is the whole reason the
 * conversions are a boundary of their own rather than an argument.
 */
public final class OguiJni {
    private OguiJni() {}

    // NativeActivity does not load the library the way a normal app does: it
    // resolves the path and hands it to its own native loader, which dlopen()s
    // it and looks up ANativeActivity_onCreate. A dlopen never calls JNI_OnLoad,
    // so on the framework's own path the natives would stay unbound forever and
    // the first question an IME asks would be an UnsatisfiedLinkError. Loading
    // it here -- through the VM, on the class that declares the methods -- is
    // what makes JNI_OnLoad run and RegisterNatives happen. The second load of
    // the same file is a refcount, not a problem: the code is already mapped and
    // android_main starts exactly as it did.
    static {
        System.loadLibrary("oggui_app");
    }

    // ---- the IME's questions and requests, answered in native ---------------
    public static native void nativeCommitText(String text, int newCursorPosition);
    public static native void nativeSetComposingText(String text, int newCursorPosition);
    public static native void nativeFinishComposingText();
    public static native void nativeSetComposingRegion(int start, int end);
    public static native void nativeDeleteSurroundingText(int beforeLength,
                                                          int afterLength);
    public static native void nativeSetSelection(int start, int end);
    public static native void nativeSendKeyEvent(int keyCode, boolean down);
    public static native void nativePerformEditorAction(int actionCode);
    public static native String nativeGetText();
    public static native int nativeGetSelectionStart();
    public static native int nativeGetSelectionEnd();
    public static native int nativeGetComposingStart();
    public static native int nativeGetComposingEnd();
    public static native boolean nativeHasEditor();

    /**
     * Ask for the keyboard a field wants: {@code android.text.InputType} and
     * {@code EditorInfo.imeOptions} as ogui decided them, and whether the
     * keyboard should be on screen at all.
     *
     * Called from the thread that draws, so it only ever posts work. The answer
     * says whether the request was taken: false is what an activity that has not
     * created its editor view yet has to say, and native asks again on the next
     * frame rather than deciding it already told the keyboard the truth.
     */
    public static boolean requestKeyboard(final int inputType, final int imeOptions,
                                         final boolean visible) {
        final OguiEditorView view = OguiEditorView.current();
        if (view == null) return false;
        view.applyRequest(inputType, imeOptions, visible);
        return true;
    }
}
