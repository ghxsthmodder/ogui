package dev.ogui.android;

import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.CompletionInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;

/**
 * The input connection ogui answers through.
 *
 * {@code BaseInputConnection} is built around an {@code Editable} it owns. Here
 * there is nothing to own: the text lives in a widget on the drawing thread, and
 * the base class's own copy would be a second buffer that edits itself into
 * silence. So every method a keyboard can reach either turns into an edit for
 * native, or is answered from the snapshot native published -- and the positions
 * both sides read are taken from the same snapshot, so the text and the caret
 * cannot disagree mid-answer.
 *
 * What is left to the base class is what cannot hurt: the batch-edit counter, the
 * meta key state, {@code reportFullscreenMode}.
 */
public final class OguiInputConnection extends BaseInputConnection {
    OguiInputConnection(View targetView) {
        // fullEditor true: an IME that believes it is editing a whole field sends
        // composing text, candidate picks and surrounding-text queries. Every one
        // of those is answered below, which is the point of saying so.
        super(targetView, true);
    }

    // ---- the field, as the snapshot has it ---------------------------------
    // Positions are already UTF-16 code units, which is what String indexes are,
    // so no conversion is needed on this side of the boundary -- an emoji is one
    // substring call away from being two characters, on both sides.

    private static String text() {
        final String t = OguiJni.nativeGetText();
        return t == null ? "" : t;
    }

    private static int start() {
        return Math.max(OguiJni.nativeGetSelectionStart(), 0);
    }

    private static int end() {
        return Math.max(OguiJni.nativeGetSelectionEnd(), 0);
    }

    private static int unitsForCodePointsBack(int from, int codePoints) {
        final String t = text();
        int i = Math.min(from, t.length());
        for (int n = 0; n < codePoints && i > 0; n++) {
            i--;
            // The low half of a pair is not a character of its own; stepping onto
            // it and past it is what makes one emoji one deletion.
            if (i > 0 && Character.isLowSurrogate(t.charAt(i))) i--;
        }
        return from - i;
    }

    private static int unitsForCodePointsForward(int from, int codePoints) {
        final String t = text();
        int i = Math.min(from, t.length());
        for (int n = 0; n < codePoints && i < t.length(); n++) {
            i++;
            if (i < t.length() && Character.isHighSurrogate(t.charAt(i))) i++;
        }
        return i - from;
    }

    // ---- what the keyboard asks for ----------------------------------------

    @Override public boolean commitText(CharSequence text, int newCursorPosition) {
        OguiJni.nativeCommitText(text == null ? "" : text.toString(), newCursorPosition);
        return true;
    }

    @Override public boolean setComposingText(CharSequence text, int newCursorPosition) {
        OguiJni.nativeSetComposingText(text == null ? "" : text.toString(),
                                       newCursorPosition);
        return true;
    }

    @Override public boolean finishComposingText() {
        OguiJni.nativeFinishComposingText();
        return true;
    }

    @Override public boolean setComposingRegion(int a, int b) {
        OguiJni.nativeSetComposingRegion(a, b);
        return true;
    }

    @Override public boolean deleteSurroundingText(int beforeLength, int afterLength) {
        OguiJni.nativeDeleteSurroundingText(beforeLength, afterLength);
        return true;
    }

    @Override public boolean deleteSurroundingTextInCodePoints(int beforeLength,
                                                               int afterLength) {
        // The base class counts code points in its own Editable, which here is an
        // empty one. Count them against the field instead and ask for the same
        // deletion in code units -- the two differ only where an emoji is.
        final int from = start();
        final int to = end();
        OguiJni.nativeDeleteSurroundingText(
                unitsForCodePointsBack(from, Math.max(beforeLength, 0)),
                unitsForCodePointsForward(to, Math.max(afterLength, 0)));
        return true;
    }

    @Override public boolean setSelection(int a, int b) {
        OguiJni.nativeSetSelection(a, b);
        return true;
    }

    @Override public boolean sendKeyEvent(KeyEvent event) {
        // The base class hands the event to the view hierarchy, and a
        // NativeActivity window does not consult the hierarchy for input: the tree
        // would never hear about it. Forwarded as the key event it is, so an
        // arrow, a hardware Enter or a letter from a keyboard that does not
        // compose arrives the same way one from a real keyboard does.
        if (event != null) {
            OguiJni.nativeSendKeyEvent(event.getKeyCode(),
                                       event.getAction() == KeyEvent.ACTION_DOWN);
        }
        return true;
    }

    @Override public boolean performEditorAction(int actionCode) {
        OguiJni.nativePerformEditorAction(actionCode);
        return true;
    }

    @Override public boolean commitCompletion(CompletionInfo text) {
        // A candidate picked from the old full-screen extract UI: what the user
        // chose is the text, and there is nothing else to report.
        if (text != null && text.getText() != null) {
            OguiJni.nativeCommitText(text.getText().toString(), 1);
        }
        return true;
    }

    // ---- what the keyboard asks about --------------------------------------

    @Override public CharSequence getSelectedText(int flags) {
        final String t = text();
        final int a = Math.min(start(), t.length());
        final int b = Math.min(end(), t.length());
        return a < b ? t.substring(a, b) : "";
    }

    @Override public CharSequence getTextBeforeCursor(int length, int flags) {
        if (length <= 0) return "";
        final String t = text();
        final int a = Math.min(start(), t.length());
        return t.substring(Math.max(0, a - length), a);
    }

    @Override public CharSequence getTextAfterCursor(int length, int flags) {
        if (length <= 0) return "";
        final String t = text();
        final int b = Math.min(end(), t.length());
        return t.substring(b, Math.min(t.length(), b + length));
    }

    @Override public ExtractedText getExtractedText(ExtractedTextRequest request,
                                                   int flags) {
        // The base implementation answers null, which to an IME means a field with
        // no text at all -- enough to make an older keyboard stop offering
        // anything. There is a field, and this is it, in one snapshot: text and
        // caret from the same publish, so a cursor can never be reported past the
        // end of a string it is not a cursor in.
        if (request == null || !OguiJni.nativeHasEditor()) return null;
        final String t = text();
        final ExtractedText out = new ExtractedText();
        out.flags = request.flags;
        out.text = t;
        out.startOffset = 0;
        // -1 on both is how the reply says "there is no partial window": the
        // whole field is here, so a keyboard that reads text reads all of it.
        out.partialStartOffset = -1;
        out.partialEndOffset = -1;
        out.selectionStart = Math.min(start(), t.length());
        out.selectionEnd = Math.min(end(), t.length());
        return out;
    }

    @Override public boolean performContextMenuAction(int id) {
        // Cut, copy, paste and select-all are widget work on a text widget, and
        // there is no widget here: say it was not done, which is the truth, rather
        // than editing the base class's empty buffer and reporting success.
        return false;
    }
}
