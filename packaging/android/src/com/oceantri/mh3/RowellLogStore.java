package com.oceantri.mh3;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/** Small local log history; never scans or removes saves or other app data. */
final class RowellLogStore {
    static final int ACTIVITY_BYTES = 2 * 1024 * 1024;
    static final int CRASH_BYTES = 256 * 1024;

    private RowellLogStore() {}

    private static void trim(File file, int limit) throws IOException {
        if (!file.exists()) return;
        if (!file.isFile()) throw new IOException("Log path is not a file");
        try (RandomAccessFile stream = new RandomAccessFile(file, "rw")) {
            long start = stream.length() - limit;
            if (start <= 0) return;
            byte[] buffer = new byte[4096];
            int copied = 0;
            while (copied < limit) {
                int count = Math.min(buffer.length, limit - copied);
                stream.seek(start + copied);
                stream.readFully(buffer, 0, count);
                stream.seek(copied);
                stream.write(buffer, 0, count);
                copied += count;
            }
            stream.setLength(limit);
        }
    }

    static synchronized void append(File file, String text, int limit) throws IOException {
        if (limit <= 0) throw new IllegalArgumentException("Log limit must be positive");
        File previous = new File(file.getPath() + ".previous");
        trim(previous, limit);
        trim(file, limit);
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        int start = Math.max(0, bytes.length - limit);
        int count = bytes.length - start;
        if (count == 0) return;
        if (file.length() > limit - count) {
            Files.move(file.toPath(), previous.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
        try (RandomAccessFile stream = new RandomAccessFile(file, "rw")) {
            stream.seek(stream.length());
            stream.write(bytes, start, count);
        }
    }
}
