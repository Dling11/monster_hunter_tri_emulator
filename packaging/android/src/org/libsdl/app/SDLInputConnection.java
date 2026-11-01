package org.libsdl.app;

import android.content.*;
import android.os.Build;
import android.text.Editable;
import android.view.*;
import android.view.inputmethod.BaseInputConnection;
import android.widget.EditText;

class SDLInputConnection extends BaseInputConnection
{
    protected EditText mEditText;
    protected String mCommittedText = "";

    SDLInputConnection(View targetView, boolean fullEditor) {
        super(targetView, fullEditor);
        mEditText = new EditText(SDL.getContext());
    }

    @Override
    public Editable getEditable() {
        return mEditText.getEditableText();
    }

    @Override
    public boolean sendKeyEvent(KeyEvent event) {
        /*
         * This used to handle the keycodes from soft keyboard (and IME-translated input from hardkeyboard)
         * However, as of Ice Cream Sandwich and later, almost all soft keyboard doesn't generate key presses
         * and so we need to generate them ourselves in commitText.  To avoid duplicates on the handful of keys
         * that still do, we empty this out.
         */

        /*
         * Return DOES still generate a key event, however.  So rather than using it as the 'click a button' key
         * as we do with physical keyboards, let's just use it to hide the keyboard.
         */

        if (event.getKeyCode() == KeyEvent.KEYCODE_ENTER) {
            if (SDLActivity.onNativeSoftReturnKey()) {
                return true;
            }
        }

        return super.sendKeyEvent(event);
    }

    @Override
    public boolean commitText(CharSequence text, int newCursorPosition) {
        if (!super.commitText(text, newCursorPosition)) {
            return false;
        }
        updateText();
        return true;
    }

    @Override
    public boolean setComposingText(CharSequence text, int newCursorPosition) {
        if (!super.setComposingText(text, newCursorPosition)) {
            return false;
        }
        updateText();
        return true;
    }

    @Override
    public boolean deleteSurroundingText(int beforeLength, int afterLength) {
        // Keep the editable/composition buffer and mCommittedText synchronized.
        // Emitting backspaces without editing this buffer breaks later suggestions.
        if (beforeLength > 0 && afterLength == 0 && getEditable().length() == 0) {
            for (int i=0;i<Math.min(beforeLength,256);++i) nativeGenerateScancodeForUnichar('\b');
            return true;
        }

        if (!super.deleteSurroundingText(beforeLength, afterLength)) {
            return false;
        }
        updateText();
        return true;
    }

    @Override
    public boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength) {
        if (beforeLength > 0 && afterLength == 0 && getEditable().length() == 0) {
            for (int i=0;i<Math.min(beforeLength,256);++i) nativeGenerateScancodeForUnichar('\b');
            return true;
        }
        if (!super.deleteSurroundingTextInCodePoints(beforeLength, afterLength)) return false;
        updateText();
        return true;
    }

    protected void updateText() {
        final Editable content = getEditable();
        if (content == null) {
            return;
        }

        String text = RowellImeText.sanitize(content);
        RowellImeText.Delta delta = RowellImeText.between(mCommittedText, text);
        for (int i=0;i<delta.backspaces;++i) nativeGenerateScancodeForUnichar('\b');
        if (!delta.insert.isEmpty()) {
            if (!SDLActivity.dispatchingKeyEvent()) {
                for (int offset=0;offset<delta.insert.length();) {
                    int codePoint=delta.insert.codePointAt(offset);
                    if(codePoint=='\n' && SDLActivity.onNativeSoftReturnKey()) {
                        mCommittedText=text;
                        return;
                    }
                    if(codePoint>0 && codePoint<128) nativeGenerateScancodeForUnichar((char)codePoint);
                    offset+=Character.charCount(codePoint);
                }
            }
            nativeCommitText(delta.insert,0);
        }
        mCommittedText=text;
    }

    public static native void nativeCommitText(String text, int newCursorPosition);

    public static native void nativeGenerateScancodeForUnichar(char c);
}

