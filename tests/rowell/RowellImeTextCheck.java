package org.libsdl.app;

public final class RowellImeTextCheck {
    private static int checks=0;
    private static void check(boolean value,String message) {
        ++checks;
        if(!value)throw new AssertionError(message);
    }
    private static String apply(String game,String before,String after) {
        RowellImeText.Delta delta=RowellImeText.between(before,after);
        check(delta.backspaces<=game.length(),"IME requested deletion beyond its committed text");
        return game.substring(0,game.length()-delta.backspaces)+delta.insert;
    }
    public static void main(String[] args) {
        String game="";
        String[] edits={"h","he","hel","hello","help","hel","he","hey","hey hunter","hey Hunter","hey Hunt","hey Hu","hey","","Röwell"};
        String previous="";
        for(String edit:edits) {
            game=apply(game,previous,edit);
            check(game.equals(edit),"Composition/suggestion/deletion did not match the game: "+edit);
            previous=edit;
        }
        check(RowellImeText.sanitize("A\uD83D\uDE00B").equals("AB"),"Unsupported emoji reached the Wii field");
        check(RowellImeText.sanitize("A\u0000\u0001\u007fB").equals("AB"),"Control characters reached the Wii field");
        check(RowellImeText.sanitize("\uD800X\uDC00").equals("X"),"Unpaired UTF-16 surrogate reached the Wii field");
        check(RowellImeText.sanitize("X".repeat(1024)).length()==256,"IME payload was not bounded");
        check(apply("oldNameH","H","Hello").equals("oldNameHello"),"Existing game text prefix was overwritten");
        check(apply("Name","Name","Name\uD83D\uDE00").equals("Name"),"Emoji changed the game field");
        check(apply("Name","Name\uD83D\uDE00","Name").equals("Name"),"Deleting rejected emoji removed a real character");
        System.out.println("Android IME text checks passed: "+checks);
    }
}
