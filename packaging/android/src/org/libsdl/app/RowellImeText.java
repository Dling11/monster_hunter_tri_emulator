package org.libsdl.app;

// Shared by the Android input connection and its host-side regression checks.
// Only characters the Wii UCS-2 text field can represent reach the game.
public final class RowellImeText {
    private RowellImeText() {}
    public static String sanitize(CharSequence input) {
        StringBuilder result = new StringBuilder();
        for (int i=0;i<input.length() && result.length()<256;) {
            int codePoint=Character.codePointAt(input,i);
            i+=Character.charCount(codePoint);
            if ((codePoint>=32 || codePoint=='\n') && codePoint!=127 && codePoint<=0xFFFF &&
                !(codePoint>=0xD800 && codePoint<=0xDFFF)) result.append((char)codePoint);
        }
        return result.toString();
    }
    public static final class Delta {
        public final int backspaces;
        public final String insert;
        private Delta(int backspaces, String insert) { this.backspaces=backspaces; this.insert=insert; }
    }
    public static Delta between(String previous, String current) {
        previous=sanitize(previous);
        current=sanitize(current);
        int prefix=0;
        while(prefix<previous.length() && prefix<current.length() && previous.charAt(prefix)==current.charAt(prefix)) ++prefix;
        return new Delta(previous.length()-prefix,current.substring(prefix));
    }
}
