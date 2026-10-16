package dev.ogui.android;

import android.content.Context;
import android.graphics.Insets;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;
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

    /**
     * Forward the window's bars to the tree, in device pixels, without keeping
     * any of them.
     *
     * This is the only place in an ogui app that learns where the status bar,
     * the navigation bar, the cutout around the camera and the keyboard are:
     * they are rectangles the system keeps for itself and paints over the
     * surface, and a native window is told about them through a view. The view
     * is one pixel wide and draws nothing, so the numbers go straight on to
     * native, which moves the layout inside them.
     *
     * The keyboard is the bottom bar while it is up, and it *replaces* the
     * navigation bar rather than adding to it: the two sit on the same strip of
     * pixels, and an inset that counted both would lift the layout twice.
     */
    @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        // The window's own numbers, not the ones this view was handed.
        //
        // A view added to the content receives what its parents decided to pass
        // down, and MIUI's decor consumes the status bar on the way: on the
        // M2101K7AI this was found on, the decor is dispatched top=93 and every
        // child under it top=0, because the decor fitted *its* content view below
        // the bar. A NativeActivity's surface is not that content view -- the
        // window stays the whole display, 1080x2400 to native while the view tree
        // reports a 1080x2246 box at y=154 -- so the app draws under the clock
        // while being told there is nothing to avoid. getRootWindowInsets() is
        // what the view root holds before anybody consumed it, which is right on
        // that build and on the Samsung, where the two agree anyway; it is null
        // only while detached, and a detached view is never dispatched to.
        final WindowInsets root = getRootWindowInsets();
        final WindowInsets source = root != null ? root : insets;
        if (Build.VERSION.SDK_INT >= 30) {
            reportModernInsets(source);
        } else {
            OguiJni.nativeSetWindowInsets(source.getSystemWindowInsetLeft(),
                                          source.getSystemWindowInsetTop(),
                                          source.getSystemWindowInsetRight(),
                                          source.getSystemWindowInsetBottom());
        }
        // Returned untouched: this view is not the one that has to live inside
        // the bars, and consuming them here would hide them from the window.
        return super.onApplyWindowInsets(insets);
    }

    /**
     * The API 30 vocabulary, in a method of its own so a device older than that
     * never verifies a reference to a type it does not have.
     */
    private static void reportModernInsets(WindowInsets insets) {
        final Insets bars = insets.getInsets(WindowInsets.Type.systemBars()
                                             | WindowInsets.Type.displayCutout());
        final Insets ime = insets.getInsets(WindowInsets.Type.ime());
        OguiJni.nativeSetWindowInsets(bars.left, bars.top, bars.right,
                                      Math.max(bars.bottom, ime.bottom));
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
