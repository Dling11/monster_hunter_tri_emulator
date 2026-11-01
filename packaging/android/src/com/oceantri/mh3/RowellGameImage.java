package com.oceantri.mh3;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Locale;

/** Bounded setup checks, before a selected file reaches the native disc decoder. */
final class RowellGameImage {
    static final int HEADER_BYTES = 0x124;
    static final String EXTRACT_FIRST = "This file is still compressed. Extract this archive first, "
        + "then select the .rvz, .iso or .wia game file inside.";
    static final String INCOMPLETE = "This file is incomplete or unreadable. Finish downloading or "
        + "transferring it, then choose the game image again.";
    private static final String CHOOSE_IMAGE = "Choose your European Monster Hunter Tri .rvz, .iso or .wia file.";

    interface Progress { void update(long copied); }

    static final class RejectedImage extends IOException {
        RejectedImage(String message) { super(message); }
    }

    private RowellGameImage() {}

    private static boolean starts(byte[] bytes, int size, int... prefix) {
        if (size < prefix.length) return false;
        for (int i = 0; i < prefix.length; ++i) {
            if ((bytes[i] & 255) != prefix[i]) return false;
        }
        return true;
    }

    static boolean hasImageExtension(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        return lower.endsWith(".rvz") || lower.endsWith(".iso") || lower.endsWith(".wia");
    }

    /** Reads at most one disc header. The caller owns the stream and any reset. */
    static String problem(InputStream source, String name) throws IOException {
        if (source == null) throw new IOException("The selected file could not be opened. " + CHOOSE_IMAGE);
        byte[] header = new byte[HEADER_BYTES];
        int count = 0;
        while (count < header.length) {
            int read = source.read(header, count, header.length - count);
            if (read < 0) break;
            if (read == 0) {
                int single = source.read();
                if (single < 0) break;
                header[count++] = (byte) single;
            } else {
                count += read;
            }
        }
        boolean container = starts(header, count, 'R', 'V', 'Z', 1)
            || starts(header, count, 'W', 'I', 'A', 1);
        boolean wii = count >= 0x1c && (header[0x18] & 255) == 0x5d
            && (header[0x19] & 255) == 0x1c && (header[0x1a] & 255) == 0x9e
            && (header[0x1b] & 255) == 0xa3;
        if (container || wii) return count == HEADER_BYTES ? null : INCOMPLETE;

        String lower = name == null ? "" : name.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".apk")) {
            return "This is the Android app installer. Install the APK first, then choose "
                + "the separate .rvz, .iso or .wia game image.";
        }
        boolean archive = starts(header, count, 'P', 'K', 3, 4)
            || starts(header, count, 'P', 'K', 5, 6) || starts(header, count, 'P', 'K', 7, 8)
            || starts(header, count, 'R', 'a', 'r', '!', 0x1a, 7)
            || starts(header, count, 0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c)
            || starts(header, count, 0x1f, 0x8b) || starts(header, count, 'B', 'Z', 'h')
            || starts(header, count, 0xfd, 0x37, 0x7a, 0x58, 0x5a, 0)
            || starts(header, count, 0x28, 0xb5, 0x2f, 0xfd)
            || (count >= 262 && header[257] == 'u' && header[258] == 's'
                && header[259] == 't' && header[260] == 'a' && header[261] == 'r');
        for (String suffix : new String[] { ".zip", ".rar", ".7z", ".gz", ".bz2", ".xz", ".tar", ".tgz", ".zst" }) {
            archive |= lower.endsWith(suffix);
        }
        if (archive) return EXTRACT_FIRST;
        if (count < HEADER_BYTES) return INCOMPLETE;
        return "This file is not a supported game image. " + CHOOSE_IMAGE
            + " If it is inside a ZIP, RAR or 7z archive, extract it first.";
    }

    static String problem(File file) {
        try (InputStream source = new FileInputStream(file)) {
            return problem(source, file.getName());
        } catch (IOException error) {
            return "Could not read this game image. Check that it is stored on this phone and choose it again.";
        }
    }

    static File destination(File directory, String name) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")
            || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf('\0') >= 0) {
            throw new IOException("The selected file has an invalid filename. " + CHOOSE_IMAGE);
        }
        File target = new File(directory, name);
        if (!directory.getCanonicalFile().equals(target.getCanonicalFile().getParentFile())) {
            throw new IOException("The game image must stay in the app's game folder.");
        }
        return target;
    }

    /** Owns source. Rejected or failed copies never replace an existing destination. */
    static long copy(InputStream source, String name, File target, long expected, Progress progress) throws IOException {
        if (source == null) throw new IOException("The selected file could not be opened. " + CHOOSE_IMAGE);
        File partial = null;
        try (BufferedInputStream image = new BufferedInputStream(source)) {
            image.mark(HEADER_BYTES);
            String error = problem(image, name);
            if (error != null) throw new RejectedImage(error);
            image.reset();
            if (!hasImageExtension(target.getName())) {
                throw new RejectedImage("The game filename must end in .rvz, .iso or .wia. " + CHOOSE_IMAGE);
            }
            File directory = target.getParentFile();
            if (!directory.isDirectory() && !directory.mkdirs()) {
                throw new IOException("Could not create the game folder. Check storage access and free space.");
            }
            partial = File.createTempFile(".mh3-image-", ".part", directory);
            long copied = 0;
            try (FileOutputStream output = new FileOutputStream(partial)) {
                byte[] buffer = new byte[4 * 1024 * 1024];
                int read;
                while ((read = image.read(buffer)) != -1) {
                    if (read == 0) continue;
                    output.write(buffer, 0, read);
                    copied += read;
                    if (progress != null) progress.update(copied);
                }
                if (expected > 0 && copied != expected) throw new IOException(INCOMPLETE);
                output.flush();
                output.getFD().sync();
            }
            // Close the input before replacing a file selected from the game folder itself.
            image.close();
            Files.move(partial.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
            partial = null;
            return copied;
        } finally {
            if (partial != null) Files.deleteIfExists(partial.toPath());
        }
    }
}
