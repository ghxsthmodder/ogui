package dev.ogui.android;

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.ViewGroup;

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
 */
public class OguiActivity extends NativeActivity {
    private OguiEditorView mEditor;

    @Override protected void onCreate(Bundle savedState) {
        // Loads liboggui_app.so -- which binds OguiJni's natives on the way
        // through JNI_OnLoad -- and starts the thread that runs android_main.
        super.onCreate(savedState);
        mEditor = new OguiEditorView(this);
        // One pixel square in the corner. It has to be visible to hold focus, and
        // it has no background, so the only thing it covers is a pixel that was
        // never drawn to anyway; touches and keys reach the native queue either
        // way, since that queue is the window's and not the view's.
        addContentView(mEditor, new ViewGroup.LayoutParams(1, 1));
    }

    @Override protected void onDestroy() {
        // After this the app thread has no view to post a keyboard request to, and
        // native finds that out by being told, not by an NPE on the UI thread.
        OguiEditorView.clear(mEditor);
        mEditor = null;
        super.onDestroy();
    }
}
