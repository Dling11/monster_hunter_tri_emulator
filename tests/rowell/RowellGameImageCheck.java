package com.oceantri.mh3;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

public final class RowellGameImageCheck {
    private static int checks;
    private static void check(boolean condition, String message) {
        ++checks;
        if (!condition) throw new AssertionError(message);
    }
    private static byte[] image(String format, int size) {
        byte[] bytes = new byte[size];
        if (format.equals("iso")) {
            System.arraycopy("RMHP08".getBytes(StandardCharsets.US_ASCII), 0, bytes, 0, 6);
            bytes[0x18] = 0x5d; bytes[0x19] = 0x1c; bytes[0x1a] = (byte) 0x9e; bytes[0x1b] = (byte) 0xa3;
        } else {
            bytes[0] = (byte) format.charAt(0); bytes[1] = (byte) format.charAt(1);
            bytes[2] = (byte) format.charAt(2); bytes[3] = 1;
        }
        for (int i = RowellGameImage.HEADER_BYTES; i < bytes.length; ++i) bytes[i] = (byte) (i % 251);
        return bytes;
    }
    private static String problem(byte[] bytes, String name) throws IOException {
        return RowellGameImage.problem(new ByteArrayInputStream(bytes), name);
    }
    private static void noPartialFiles(Path root) throws IOException {
        try (var files = Files.list(root)) {
            check(files.noneMatch(path -> path.getFileName().toString().endsWith(".part")), "temporary copy was left behind");
        }
    }
    private static void expectCopyFailure(InputStream source, String name, File target, long expected) throws IOException {
        byte[] original = Files.exists(target.toPath()) ? Files.readAllBytes(target.toPath()) : null;
        boolean rejected = false;
        try { RowellGameImage.copy(source, name, target, expected, null); }
        catch (IOException error) { rejected = true; }
        check(rejected, "bad copy was accepted: " + name);
        check(original == null ? !target.exists() : Arrays.equals(original, Files.readAllBytes(target.toPath())),
            "failed copy changed an existing destination: " + name);
        noPartialFiles(target.toPath().getParent());
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        Files.createDirectories(root.resolve("nand"));
        Path save = root.resolve("nand/character.bin");
        Path controls = root.resolve("touch_layout.cfg");
        Files.writeString(save, "existing character progress");
        Files.writeString(controls, "custom buttons and controller mapping");
        byte[] originalSave = Files.readAllBytes(save), originalControls = Files.readAllBytes(controls);

        for (String format : new String[] { "RVZ", "WIA", "iso" }) {
            byte[] bytes = image(format, 700);
            check(problem(bytes, "Monster Hunter Tri." + format) == null, "valid disc format rejected: " + format);
            for (int length : new int[] { 4, 32, RowellGameImage.HEADER_BYTES - 1 }) {
                check(RowellGameImage.INCOMPLETE.equals(problem(Arrays.copyOf(bytes, length), "game.rvz")),
                    "short disc header did not explain incomplete file");
            }
        }
        int[][] signatures = {
            {0x50,0x4b,3,4}, {0x50,0x4b,5,6}, {0x50,0x4b,7,8},
            {0x52,0x61,0x72,0x21,0x1a,7,0}, {0x52,0x61,0x72,0x21,0x1a,7,1,0},
            {0x37,0x7a,0xbc,0xaf,0x27,0x1c}, {0x1f,0x8b}, {0x42,0x5a,0x68},
            {0xfd,0x37,0x7a,0x58,0x5a,0}, {0x28,0xb5,0x2f,0xfd}
        };
        for (int[] signature : signatures) {
            byte[] archive = new byte[700];
            for (int i = 0; i < signature.length; ++i) archive[i] = (byte) signature[i];
            check(RowellGameImage.EXTRACT_FIRST.equals(problem(archive, "renamed-game.rvz")), "renamed archive accepted");
            expectCopyFailure(new ByteArrayInputStream(archive), "renamed-game.rvz", root.resolve("game.rvz").toFile(), archive.length);
        }
        for (String suffix : new String[] { ".ZIP", ".rar", ".7z", ".tar.gz", ".bz2", ".xz", ".tar", ".tgz", ".zst" }) {
            check(RowellGameImage.EXTRACT_FIRST.equals(problem(new byte[2], "game.rvz" + suffix)), "archive extension missed: " + suffix);
        }
        byte[] tar = new byte[700];
        System.arraycopy("ustar".getBytes(StandardCharsets.US_ASCII), 0, tar, 257, 5);
        check(RowellGameImage.EXTRACT_FIRST.equals(problem(tar, "renamed-game.iso")), "tar signature missed");
        byte[] apk = new byte[700]; apk[0] = 'P'; apk[1] = 'K'; apk[2] = 3; apk[3] = 4;
        check(problem(apk, "MH3_Rowell_Dev-arm64.apk").contains("Android app installer"), "APK confused with game archive");
        check(RowellGameImage.INCOMPLETE.equals(problem(new byte[0], "game.rvz")), "empty game image accepted");
        check(problem(new byte[700], "game.rvz").contains("not a supported game image"), "unknown header accepted");
        check(problem("<html>download page</html>".repeat(30).getBytes(StandardCharsets.UTF_8), "game.rvz") != null,
            "download page accepted as disc");

        byte[] valid = image("RVZ", 1024 * 1024 + 53);
        long[] lastProgress = {0};
        File target = RowellGameImage.destination(root.toFile(), "Monster Hunter Tri.rvz");
        long copied = RowellGameImage.copy(new ByteArrayInputStream(valid), target.getName(), target, valid.length,
            count -> { check(count >= lastProgress[0], "copy progress moved backwards"); lastProgress[0] = count; });
        check(copied == valid.length && lastProgress[0] == valid.length, "copy length or progress wrong");
        check(Arrays.equals(valid, Files.readAllBytes(target.toPath())), "valid copy changed game bytes or lost header");
        check(RowellGameImage.problem(target) == null, "copied file failed automatic detection");
        noPartialFiles(root);
        for (long unknownSize : new long[] {-1, 0}) {
            File unknownTarget = root.resolve("unknown-size-" + unknownSize + ".rvz").toFile();
            RowellGameImage.copy(new ByteArrayInputStream(valid), "game.rvz", unknownTarget, unknownSize, null);
            check(Arrays.equals(valid, Files.readAllBytes(unknownTarget.toPath())), "unknown provider size rejected a valid copy");
        }
        check(RowellGameImage.EXTRACT_FIRST.equals(problem(new byte[] {'P','K',3,4}, "game.rvz")),
            "small renamed ZIP did not explain extraction");
        boolean[] streamClosed = {false};
        ByteArrayInputStream rejectedStream = new ByteArrayInputStream(apk) {
            @Override public void close() throws IOException { streamClosed[0] = true; super.close(); }
        };
        expectCopyFailure(rejectedStream, "wrong.rvz", target, apk.length);
        check(streamClosed[0], "rejected content-provider stream leaked");

        expectCopyFailure(new ByteArrayInputStream(apk), "renamed.rvz", target, apk.length);
        expectCopyFailure(new ByteArrayInputStream(valid), "game.rvz", target, valid.length + 1L);
        expectCopyFailure(new ByteArrayInputStream(valid), "game.rvz", target, 2472166476L);
        expectCopyFailure(new ByteArrayInputStream(valid), "game.rvz", target, valid.length - 1L);
        InputStream interrupted = new InputStream() {
            int offset;
            @Override public int read() throws IOException {
                if (offset >= 512) throw new IOException("transfer disconnected");
                return valid[offset++] & 255;
            }
            @Override public int read(byte[] buffer, int at, int length) throws IOException {
                if (offset >= 512) throw new IOException("transfer disconnected");
                int count = Math.min(length, 512 - offset);
                System.arraycopy(valid, offset, buffer, at, count); offset += count; return count;
            }
        };
        expectCopyFailure(interrupted, "game.rvz", target, valid.length);
        expectCopyFailure(new ByteArrayInputStream(valid), "touch_layout.cfg", controls.toFile(), valid.length);

        InputStream shortReads = new FilterInputStream(new ByteArrayInputStream(valid)) {
            @Override public int read(byte[] bytes, int offset, int count) throws IOException {
                return super.read(bytes, offset, Math.min(3, count));
            }
        };
        check(RowellGameImage.problem(shortReads, "game.rvz") == null, "short stream reads broke header validation");
        ByteArrayInputStream bounded = new ByteArrayInputStream(valid);
        check(RowellGameImage.problem(bounded, "game.rvz") == null, "valid bounded read rejected");
        check(bounded.available() == valid.length - RowellGameImage.HEADER_BYTES, "validator read beyond header");
        for (String name : new String[] { "../nand/character.bin", "..\\touch_layout.cfg", "", ".", "..", "/outside.iso" }) {
            boolean rejected = false;
            try { RowellGameImage.destination(root.toFile(), name); } catch (IOException expected) { rejected = true; }
            check(rejected, "unsafe destination accepted: " + name);
        }
        check(RowellGameImage.problem(root.resolve("missing.rvz").toFile()) != null, "missing automatic candidate accepted");
        check(RowellGameImage.hasImageExtension("GAME.RVZ") && !RowellGameImage.hasImageExtension("game.rvz.zip"), "auto-detection extension filter");
        check(Arrays.equals(originalSave, Files.readAllBytes(save)), "character progress changed");
        check(Arrays.equals(originalControls, Files.readAllBytes(controls)), "controls changed");
        if (args.length > 1) check(RowellGameImage.problem(new File(args[1])) == null, "actual working RVZ rejected");
        System.out.println("GAME_IMAGE_CHECK: " + checks + " checks passed");
    }
}
