package com.oceantri.mh3;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public final class LowSpecStorageCheck {
    private static int checks;
    private static void check(boolean result,String message) {
        ++checks;if(!result)throw new AssertionError(message);
    }
    private static void write(Path root,String path,String contents) throws IOException {
        Path file=root.resolve(path);Files.createDirectories(file.getParent());Files.writeString(file,contents);
    }
    private static void unchanged(Path file,String contents) throws IOException {
        check(Arrays.equals(Files.readAllBytes(file),contents.getBytes(StandardCharsets.UTF_8)),"Changed "+file);
    }
    private static void rejected(Path root) throws IOException {
        boolean rejected=false;
        try {LowSpecStorage.prepare(root.toFile());}catch(IOException expected){rejected=true;}
        check(rejected,"Unsafe duplicate/path accepted");
    }
    public static void main(String[] args) throws Exception {
        Path root=Path.of(args[0]);Files.createDirectories(root);
        Path fresh=root.resolve("fresh");
        File current=LowSpecStorage.prepare(fresh.toFile());
        check(current.isDirectory() && current.getName().equals("MH3_Low_Spec"),"Wrong fresh folder");
        check(!fresh.resolve("MH3_Rowell_Lite").toFile().exists(),"Fresh install created old folder");
        check(LowSpecStorage.prepare(fresh.toFile()).equals(current),"Fresh setup not idempotent");

        Path upgrade=root.resolve("upgrade"),old=upgrade.resolve("MH3_Rowell_Lite"),next=upgrade.resolve("MH3_Low_Spec");
        String[] names={"nand/title/00010000/524d4850/data/nocopy/data00","touch_layout.cfg","controller_bindings.cfg",
            "MH3UI.graphics","saves/backup/data/nocopy/data00","imports/export/data/nocopy/data00","game.rvz"};
        for(String name:names)write(old,name,"preserve "+name);
        write(upgrade,"MH3_Rowell_Dev/nand/normal-save","normal app progress");
        write(upgrade,"MH3/legacy-save","Spanish app progress");
        check(LowSpecStorage.prepare(upgrade.toFile()).toPath().equals(next),"Wrong migration destination");
        check(!Files.exists(old),"Previous Lite folder was not renamed");
        for(String name:names)unchanged(next.resolve(name),"preserve "+name);
        unchanged(upgrade.resolve("MH3_Rowell_Dev/nand/normal-save"),"normal app progress");
        unchanged(upgrade.resolve("MH3/legacy-save"),"Spanish app progress");
        check(LowSpecStorage.prepare(upgrade.toFile()).toPath().equals(next),"Upgrade not idempotent");

        Path duplicate=root.resolve("duplicate");
        write(duplicate,"MH3_Rowell_Lite/nand/progress","older progress");
        write(duplicate,"MH3_Low_Spec/nand/progress","newer progress");
        rejected(duplicate);
        unchanged(duplicate.resolve("MH3_Rowell_Lite/nand/progress"),"older progress");
        unchanged(duplicate.resolve("MH3_Low_Spec/nand/progress"),"newer progress");

        Path blocked=root.resolve("blocked");
        write(blocked,"MH3_Low_Spec","not a directory");rejected(blocked);
        unchanged(blocked.resolve("MH3_Low_Spec"),"not a directory");
        Path badLegacy=root.resolve("bad-legacy");
        write(badLegacy,"MH3_Rowell_Lite","previous path is a file");rejected(badLegacy);
        check(!Files.exists(badLegacy.resolve("MH3_Low_Spec")),"Created fresh folder after failed migration");
        unchanged(badLegacy.resolve("MH3_Rowell_Lite"),"previous path is a file");
        System.out.println("Low Spec storage migration: "+checks+" checks passed");
    }
}
