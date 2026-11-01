package com.oceantri.mh3;

import java.io.File;
import java.io.IOException;

/** Keeps the low-spec application's saves/settings together across its rename. */
public final class LowSpecStorage {
    public static final String DIRECTORY = "MH3_Low_Spec";
    private static final String LEGACY_DIRECTORY = "MH3_Rowell_Lite";
    private LowSpecStorage() {}

    public static synchronized File prepare(File sharedStorage) throws IOException {
        File current = new File(sharedStorage, DIRECTORY);
        File previous = new File(sharedStorage, LEGACY_DIRECTORY);
        if (previous.exists()) {
            if (!previous.isDirectory()) {
                throw new IOException("The previous Low Spec data path is not a folder: " + previous);
            }
            if (current.exists()) {
                throw new IOException("Both " + LEGACY_DIRECTORY + " and " + DIRECTORY +
                    " already exist. Neither folder was changed. Keep a copy of both and resolve the duplicate before opening the app.");
            }
            // Same storage/parent: rename the whole directory; do not merge,
            // delete, or partially copy saved progress, layouts or backups.
            if (!previous.renameTo(current)) {
                throw new IOException("Could not rename the previous Low Spec data folder to " + DIRECTORY +
                    ". Your existing files remain at " + previous + ".");
            }
        }
        if (!current.isDirectory() && !current.mkdirs()) {
            throw new IOException("Could not open the Low Spec data folder: " + current);
        }
        return current;
    }
}
