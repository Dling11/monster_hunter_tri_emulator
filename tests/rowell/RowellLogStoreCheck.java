package com.oceantri.mh3;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicReference;

public final class RowellLogStoreCheck {
    private static int checks;
    private static void check(boolean ok, String message) {
        ++checks;
        if (!ok) throw new AssertionError(message);
    }
    private static byte[] tail(byte[] bytes, int cap) {
        return Arrays.copyOfRange(bytes, Math.max(0, bytes.length - cap), bytes.length);
    }
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        Files.createDirectories(root.resolve("nand"));
        Path save = root.resolve("nand/save.bin");
        Files.writeString(save, "character progress");
        byte[] saved = Files.readAllBytes(save);
        File large = root.resolve("large-legacy.log").toFile();
        byte[] blocks = new byte[8192 + 17];
        for (int i = 0; i < blocks.length; ++i) blocks[i] = (byte)(i % 251);
        Files.write(large.toPath(), blocks);
        RowellLogStore.append(large, "", 8192);
        check(Arrays.equals(Files.readAllBytes(large.toPath()), tail(blocks, 8192)), "overlapping multi-buffer trim");
        File file = root.resolve("oceantri_log.txt").toFile();
        Path previous = Path.of(file + ".previous");
        byte[] legacy = ("old".repeat(100) + "latest legacy message").getBytes(StandardCharsets.UTF_8);
        Files.write(file.toPath(), legacy);
        Files.write(previous, legacy);
        RowellLogStore.append(file, "", 64);
        check(Arrays.equals(Files.readAllBytes(file.toPath()), tail(legacy, 64)), "legacy current tail");
        check(Arrays.equals(Files.readAllBytes(previous), tail(legacy, 64)), "legacy previous tail");
        RowellLogStore.append(file, "new report\n", 64);
        check(Files.readString(file.toPath()).equals("new report\n"), "new report missing");
        check(Arrays.equals(Files.readAllBytes(previous), tail(legacy, 64)), "previous report missing");
        for (int i = 0; i < 200; ++i) {
            RowellLogStore.append(file, "row " + i + "\n", 64);
            check(file.length() <= 64 && Files.size(previous) <= 64, "log exceeded cap");
        }
        String unicode = "日本語".repeat(100) + "newest";
        RowellLogStore.append(file, unicode, 64);
        check(Arrays.equals(Files.readAllBytes(file.toPath()), tail(unicode.getBytes(StandardCharsets.UTF_8), 64)), "UTF-8 byte limit");
        File blocked = root.resolve("blocked.txt").toFile();
        Files.writeString(blocked.toPath(), "a".repeat(64));
        Files.createDirectory(Path.of(blocked + ".previous"));
        boolean failed = false;
        try { RowellLogStore.append(blocked, "overflow", 64); } catch (IOException expected) { failed = true; }
        check(failed && blocked.length() == 64, "failed logging must not grow log");
        check(Files.isDirectory(Path.of(blocked + ".previous")), "logger changed unrelated directory");
        File concurrent = root.resolve("threads.txt").toFile();
        Thread[] threads = new Thread[4];
        AtomicReference<Throwable> threadFailure = new AtomicReference<>();
        for (int n = 0; n < threads.length; ++n) {
            threads[n] = new Thread(() -> {
                try {
                    for (int i = 0; i < 100; ++i) RowellLogStore.append(concurrent, "concurrent message\n", 256);
                } catch (Throwable e) { threadFailure.compareAndSet(null, e); }
            });
            threads[n].start();
        }
        for (Thread thread : threads) thread.join();
        check(threadFailure.get() == null, "concurrent writer failed");
        check(concurrent.length() <= 256 && Files.size(Path.of(concurrent + ".previous")) <= 256, "concurrent cap");
        check(Files.readString(concurrent.toPath()).replace("concurrent message\n", "").isEmpty(), "concurrent records interleaved");
        check(Arrays.equals(saved, Files.readAllBytes(save)), "saved progress changed");
        check(RowellLogStore.ACTIVITY_BYTES == 2097152 && RowellLogStore.CRASH_BYTES == 262144, "production caps");
        System.out.println("Bounded Java logs passed: " + checks + " checks.");
    }
}
