package dev.ogui.android;

import android.content.Context;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

/**
 * The field the input method is aimed at.
 *
 * It is not a text widget. ogui draws its own caret, edits its own text and
 * keeps its own selection, all on the thread that draws; what a real
 * {@code EditText} would add here is a second copy of the text, on the UI
 * thread, that nothing in the tree ever reads. So this is a 1x1 view whose only
 * jobs are to be focusable, to say whether there is a field to write into, and
 * to hand out an {@link OguiInputConnection} that answers to native.
 *
 * It is added with {@code addContentView}, never {@code setContentView}: a
 * {@code NativeActivity} draws through the view its own {@code onCreate}
 * installed, and replacing the content view replaces the surface with it -- the
 * app would keep running, drawing into a window nobody shows.
 */
public final class OguiEditorView extends View {
    private static volatile OguiEditorView sCurrent;

    // Written on the thread that draws, read on the UI thread, which is why they
    // are volatile: a torn int is not the failure, an out-of-date keyboard is.
    private volatile int mInputType;
    private volatile int mImeOptions;
    private OguiInputConnection mConnection;

    public OguiEditorView(Context context) {
        super(context);
        setFocusable(true);
        setFocusableInTouchMode(true);
        sCurrent = this;
    }

    public static OguiEditorView current() {
        return sCurrent;
    }

    /** Only the view that is still the current one may clear it. */
    public static void clear(OguiEditorView view) {
        if (sCurrent == view) sCurrent = null;
    }

    /**
     * What {@code InputMethodManager} asks before it will bring a keyboard up
     * for this view: is there a field for it to write into. Answered from the
     * tree, so a screen that moved focus away between frames does not get a
     * keyboard aimed at nothing.
     */
    @Override public boolean onCheckIsTextEditor() {
        return OguiJni.nativeHasEditor();
    }

    @Override public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        if (mConnection == null) mConnection = new OguiInputConnection(this);
        outAttrs.inputType = mInputType;
        outAttrs.imeOptions = mImeOptions;
        // Where the caret is, so a keyboard that shows candidates, or a completion
        // strip, starts from the right place instead of the end of the field.
        outAttrs.initialSelStart = OguiJni.nativeGetSelectionStart();
        outAttrs.initialSelEnd = OguiJni.nativeGetSelectionEnd();
        outAttrs.fieldName = "ogui";
        return mConnection;
    }

    /**
     * Show or hide the keyboard. Safe from any thread: the request is recorded
     * here and the work is posted, because {@code InputMethodManager} takes its
     * calls on the UI thread and the activity's window token is not something the
     * drawing thread may read.
     */
    void applyRequest(final int inputType, final int imeOptions, final boolean visible) {
        mInputType = inputType;
        mImeOptions = imeOptions;
        post(new Runnable() {
            @Override public void run() { applyOnUiThread(visible); }
        });
    }

    private void applyOnUiThread(boolean visible) {
        final InputMethodManager imm =
                (InputMethodManager) getContext().getSystemService(
                        Context.INPUT_METHOD_SERVICE);
        if (imm == null) return;
        if (visible) {
            // The keyboard is aimed at the focused view, so this view has to hold
            // the focus for the connection it creates to be the live one. No touch
            // is lost by taking it: a NativeActivity window hands its input
            // straight to the native queue, which is why the tree can be drawn by
            // one thread and edited through another.
            requestFocus();
            imm.showSoftInput(this, InputMethodManager.SHOW_IMPLICIT);
        } else {
            imm.hideSoftInputFromWindow(getWindowToken(),
                                        InputMethodManager.HIDE_NOT_ALWAYS);
        }
        // And the IME re-reads EditorInfo, which is how a search field that took
        // the focus from a done field gets its magnifier instead of a check mark
        // while the keyboard never left the screen.
        imm.restartInput(this);
    }
}
