package dev.ogui.android;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * The activity an ogui app names in its manifest.
 *
 * It is a {@code NativeActivity} with one thing added, because a
 * {@code NativeActivity} has no input connection at all: it puts an ordinary
 * {@code View} on screen, gives it focus and asks the input method to show
 * itself, and the framework then builds the connection that view would have
 * built -- an empty one, from a widget with no text. That is why a keyboard
 * raised over a native-only app types into nowhere, and why {@code commitText}
 * has never had a caller it could reach.
 *
 * Everything else is left alone. The library is still loaded by the framework,
 * {@code android_main} is still the entry point, and the surface still belongs
 * to the view {@code NativeActivity.onCreate} installed -- which is the reason
 * the editor below is *added* to the content rather than set as it.
 *
 * The one other thing this class owns is the window's system bars: hiding them
 * is a window operation, and the window is the activity's. Native asks through
 * {@link #setSystemBarsHidden}, stays out of the API-level split, and leaves the
 * insets to do the talking once the bars are gone.
 */
public class OguiActivity extends NativeActivity {
    private OguiEditorView mEditor;

    /** The activity native may reach; null between onDestroy and the next create. */
    private static volatile OguiActivity sCurrent;

    @Override protected void onCreate(Bundle savedState) {
        // Loads liboggui_app.so -- which binds OguiJni's natives on the way
        // through JNI_OnLoad -- and starts the thread that runs android_main.
        super.onCreate(savedState);
        claimWholeWindow();
        sCurrent = this;
        mEditor = new OguiEditorView(this);
        // One pixel square in the corner. It has to be visible to hold focus, and
        // it has no background, so the only thing it covers is a pixel that was
        // never drawn to anyway; touches and keys reach the native queue either
        // way, since that queue is the window's and not the view's.
        addContentView(mEditor, new ViewGroup.LayoutParams(1, 1));
    }

    /**
     * Draw on the whole display, and be told how much of it is covered.
     *
     * A {@code NativeActivity} surface is the window, so it already reaches the
     * edges of the screen -- but the framework, unless asked not to, keeps the
     * content view inside the system bars and hands out what is left of the
     * insets, which for a window already under the status bar means the app
     * draws behind a bar it was never told about. That is the exact bug: an
     * ogui layout authored from the top of its canvas puts its first row under
     * the clock. Asking not to fit gives the surface the same pixels it was
     * drawing on anyway and the insets their real numbers, which is what
     * {@link OguiEditorView} forwards to the tree.
     */
    private void claimWholeWindow() {
        final Window window = getWindow();
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
            return;
        }
        // The same request in the vocabulary a 2016 device has. LAYOUT_STABLE is
        // what makes the inset numbers the bars rather than the animation
        // halfway through one of them disappearing.
        final View decor = window.getDecorView();
        decor.setSystemUiVisibility(decor.getSystemUiVisibility()
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    @Override protected void onDestroy() {
        // After this the app thread has no view to post a keyboard request to, and
        // native finds that out by being told, not by an NPE on the UI thread.
        OguiEditorView.clear(mEditor);
        mEditor = null;
        if (sCurrent == this) sCurrent = null;
        super.onDestroy();
    }

    /**
     * Hide or show the bars, from native. False when there is no activity left
     * to ask, which is the answer that keeps native asking after a recreation.
     */
    static boolean setSystemBarsHidden(final boolean hidden) {
        final OguiActivity activity = sCurrent;
        if (activity == null) return false;
        // The window is the UI thread's; the caller is the thread that draws.
        activity.runOnUiThread(new Runnable() {
            @Override public void run() { activity.applySystemBarsHidden(hidden); }
        });
        return true;
    }

    private void applySystemBarsHidden(boolean hidden) {
        if (Build.VERSION.SDK_INT >= 30) {
            applySystemBarsHiddenModern(hidden);
        } else {
            applySystemBarsHiddenLegacy(hidden);
        }
    }

    /**
     * The API 30 vocabulary. Hiding takes a behaviour as well as a flag: with
     * {@code BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE} a swipe from an edge brings
     * the bars back for a moment and they leave again, so reaching for the
     * gesture bar does not permanently shrink a layout that was laid out for
     * the space. {@code hide()} only does anything because
     * {@link #claimWholeWindow} already told the decor not to fit the bars.
     */
    private void applySystemBarsHiddenModern(boolean hidden) {
        final WindowInsetsController controller = getWindow().getInsetsController();
        if (controller == null) return;
        if (hidden) {
            controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            controller.hide(WindowInsets.Type.systemBars());
        } else {
            controller.show(WindowInsets.Type.systemBars());
        }
    }

    /**
     * The same request on a device too old for the controller: the layout flags
     * stay (they are what keeps the surface at the full display), and the three
     * that make the bars go are added or removed. IMMERSIVE_STICKY is what makes
     * the swipe transient on these versions too.
     */
    private void applySystemBarsHiddenLegacy(boolean hidden) {
        final View decor = getWindow().getDecorView();
        final int layout = decor.getSystemUiVisibility()
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION;
        final int gone = View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
        decor.setSystemUiVisibility(hidden ? (layout | gone) : (layout & ~gone));
    }
}
