package com.oceantri.mh3;

import android.Manifest;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;
import android.view.Display;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.LinkedList;
import java.util.List;
import java.util.Locale;

public class OceanTriActivity extends SDLActivity {
    private static final String TAG = "MH3";
    private static final int REQUEST_STORAGE_PERMS = 1001;
    private static final int REQUEST_PICK_ROM = 1002;

    private static final LinkedList<String> sLogBuffer = new LinkedList<>();
    private static String sRomPath = null;
    private volatile boolean mRomReady = false;
    private boolean mRomCheckStarted = false;

    public static synchronized void log(String msg) {
        String timestamp = new SimpleDateFormat("HH:mm:ss.SSS", Locale.US).format(new Date());
        String line = "[" + timestamp + "] " + msg;
        Log.i(TAG, line);

        sLogBuffer.add(line);
        if (sLogBuffer.size() > 500) {
            sLogBuffer.removeFirst();
        }

        // Try writing to MH3/logs file
        try {
            File logDir = new File(LowSpecStorage.prepare(new File("/storage/emulated/0")), "logs");
            if (!logDir.exists()) {
                logDir.mkdirs();
            }
            File logFile = new File(logDir, "oceantri_log.txt");
            RowellLogStore.append(logFile, line + "\n", RowellLogStore.ACTIVITY_BYTES);
        } catch (Exception ignored) {}
    }

    public static synchronized String getLogs() {
        StringBuilder sb = new StringBuilder();
        for (String s : sLogBuffer) {
            sb.append(s).append("\n");
        }
        return sb.toString();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "png16",
            "SDL3",
            "main"
        };
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }

    @Override
    protected String[] getArguments() {
        if (sRomPath != null && !sRomPath.isEmpty()) {
            log("Passing --rom argument to native main(): " + sRomPath);
            return new String[] { "--rom", sRomPath };
        }
        return new String[0];
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        log("OceanTriActivity.onCreate() called (pid=" + android.os.Process.myPid() + ")");

        // Request the low-power 60 Hz display preference
        setHighRefreshRate();

        // Crash handler to log Java crashes
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread t, Throwable e) {
                StringWriter sw = new StringWriter();
                e.printStackTrace(new PrintWriter(sw));
                String stackTrace = sw.toString();
                log("FATAL JAVA UNCAUGHT EXCEPTION: " + stackTrace);
                try {
                    File crashDir = new File(LowSpecStorage.prepare(new File("/storage/emulated/0")), "logs");
                    if (!crashDir.exists()) {
                        crashDir.mkdirs();
                    }
                    File crashFile = new File(crashDir, "oceantri_crash.txt");
                    RowellLogStore.append(crashFile,
                        "UNCAUGHT EXCEPTION in thread " + t.getName() + ":\n" + stackTrace + "\n",
                        RowellLogStore.CRASH_BYTES);
                } catch (Exception ignored) {}
                System.exit(1);
            }
        });

        if (hasStoragePermission()) {
            log("Storage permission granted at startup. Verifying ROM...");
            checkRomAndPrepare();
        } else {
            log("Storage permissions not yet granted. Requesting...");
            requestStoragePermission();
        }

        // Call super.onCreate synchronously (required by Android)
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onResume() {
        super.onResume();
        log("Game activity resumed (pid=" + android.os.Process.myPid() + ")");
        setHighRefreshRate();
        if (hasStoragePermission() && !mRomReady && !mRomCheckStarted) {
            checkRomAndPrepare();
        }
    }

    @Override
    protected void onPause() {
        log("Game activity paused (pid=" + android.os.Process.myPid() + ")");
        super.onPause();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        log("Returning to the existing game activity (pid=" + android.os.Process.myPid() + ")");
    }

    @Override
    protected void main() {
        log("OceanTriActivity.main() thread waiting for ROM ready...");
        while (!mRomReady && !isFinishing()) {
            try {
                Thread.sleep(100);
            } catch (InterruptedException ignored) {}
        }
        if (isFinishing()) {
            log("OceanTriActivity.main() aborting because activity is finishing");
            return;
        }
        log("OceanTriActivity.main() ROM ready! Launching native main() with: " + sRomPath);
        super.main();
    }

    private void setHighRefreshRate() {
        // Prefer 60 Hz at the current display dimensions. A 30 FPS game does
        // not benefit from forcing 120 Hz, which increases power/thermal load.
        try {
            Display display = getWindowManager().getDefaultDisplay();
            if (display == null) return;
            Display.Mode current = display.getMode();
            Display.Mode best = null;
            for (Display.Mode mode : display.getSupportedModes()) {
                if (mode.getPhysicalWidth() != current.getPhysicalWidth() ||
                    mode.getPhysicalHeight() != current.getPhysicalHeight()) continue;
                if (best == null || Math.abs(mode.getRefreshRate() - 60.0f) <
                        Math.abs(best.getRefreshRate() - 60.0f)) best = mode;
            }
            WindowManager.LayoutParams params = getWindow().getAttributes();
            if (best != null) params.preferredDisplayModeId = best.getModeId();
            params.preferredRefreshRate = 60.0f;
            getWindow().setAttributes(params);
        } catch (Exception e) {
            log("Unable to request Lite refresh rate: " + e.getMessage());
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            setHighRefreshRate();
        }
    }

    private synchronized void checkRomAndPrepare() {
        if (mRomReady) {
            return;
        }
        mRomCheckStarted = true;

        // 1. Check if ROM already exists in the official game directory (/storage/emulated/0/MH3_Low_Spec/)
        final File mh3Dir;
        try {
            mh3Dir = LowSpecStorage.prepare(new File("/storage/emulated/0"));
        } catch (final IOException error) {
            // Native main must not start with a fresh empty NAND if an earlier
            // Lite folder could not be carried across safely.
            log("Data folder preparation failed: " + error.getMessage());
            runOnUiThread(new Runnable() {
                @Override public void run() {
                    new AlertDialog.Builder(OceanTriActivity.this)
                        .setTitle("Save folder needs attention")
                        .setMessage(error.getMessage())
                        .setCancelable(false)
                        .setPositiveButton("Exit", new DialogInterface.OnClickListener() {
                            @Override public void onClick(DialogInterface dialog, int which) { finish(); }
                        }).show();
                }
            });
            return;
        }
        // Make transfer destinations visible even before a game image is selected.
        // mkdirs preserves any existing saves and user-created folders.
        for (String name : new String[] { "imports", "exports", "saves" }) {
            File folder = new File(mh3Dir, name);
            if (!folder.isDirectory() && !folder.mkdirs()) {
                log("Unable to create save-transfer folder: " + folder.getAbsolutePath());
            }
        }

        File existingMh3Rom = findRomInDir(mh3Dir);
        if (existingMh3Rom != null) {
            sRomPath = existingMh3Rom.getAbsolutePath();
            mRomReady = true;
            log("ROM already present in target directory /storage/emulated/0/MH3_Low_Spec: " + sRomPath);
            return;
        }

        // 2. Search for ROM in other common directories (e.g. Download)
        final String foundOtherRom = findGameRom();
        if (foundOtherRom != null) {
            sRomPath = foundOtherRom;
            mRomReady = true;
            log("Found ROM automatically, launching: " + sRomPath);
            return;
        }

        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                showRomSelectPrompt();
            }
        });
    }

    private void showRomFoundPrompt(final String detectedPath) {
        final File file = new File(detectedPath);
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("Monster Hunter Tri");
        builder.setMessage("Found a game image at:\n" + detectedPath + "\n\n" +
            "Copy it into MH3_Low_Spec and start?");
        builder.setPositiveButton("Copy to Low Spec and start", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                File target = new File("/storage/emulated/0/MH3_Low_Spec", file.getName());
                copyFileWithProgress(file, target);
            }
        });
        builder.setNeutralButton("Choose another image", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                launchRomPicker();
            }
        });
        builder.setNegativeButton("Use this image directly", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                useGameImageIfValid(file);
            }
        });
        builder.setCancelable(false);
        builder.show();
    }

    private void showRomSelectPrompt() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("Monster Hunter Tri");
        builder.setMessage("Select your European Monster Hunter Tri game image (.iso, .rvz or .wia).\n\n" +
            "ZIP, RAR or 7z? Extract it first, then choose the game file inside.\n\n" +
            "This Low Spec app uses MH3_Low_Spec. Your existing app and saves stay separate.");
        builder.setPositiveButton("Choose game image", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                launchRomPicker();
            }
        });
        builder.setNegativeButton("Exit", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                finish();
            }
        });
        builder.setCancelable(false);
        builder.show();
    }

    private void launchRomPicker() {
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/x-iso9660-image",
                "application/octet-stream",
                "application/*",
                "*/*"
            });
            startActivityForResult(intent, REQUEST_PICK_ROM);
        } catch (Exception e) {
            log("Error launching ACTION_OPEN_DOCUMENT: " + e.getMessage());
            try {
                Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
                intent.setType("*/*");
                startActivityForResult(intent, REQUEST_PICK_ROM);
            } catch (Exception ex) {
                log("Error launching ACTION_GET_CONTENT: " + ex.getMessage());
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_PICK_ROM) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                Uri uri = data.getData();
                copyUriWithProgress(uri);
            } else {
                // User cancelled file picker, ask again
                showRomSelectPrompt();
            }
        }
    }

    private void copyUriWithProgress(final Uri uri) {
        String displayName = "Monster Hunter Tri (Europe).rvz";
        long fileSize = -1;
        try (Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int nameIdx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                int sizeIdx = cursor.getColumnIndex(OpenableColumns.SIZE);
                if (nameIdx != -1) {
                    String name = cursor.getString(nameIdx);
                    if (name != null && !name.isEmpty()) displayName = name;
                }
                if (sizeIdx != -1 && !cursor.isNull(sizeIdx)) {
                    fileSize = cursor.getLong(sizeIdx);
                }
            }
        } catch (Exception e) {
            log("Querying URI info: " + e.getMessage());
        }

        try {
            File target = RowellGameImage.destination(new File("/storage/emulated/0/MH3_Low_Spec"), displayName);
            copyGameImageWithProgress(displayName, target, fileSize, new GameImageSource() {
                @Override
                public InputStream open() throws IOException {
                    return getContentResolver().openInputStream(uri);
                }
            });
        } catch (IOException error) {
            showGameImageProblem("Choose a game image", error.getMessage());
        }
    }

    private void copyFileWithProgress(final File sourceFile, final File targetFile) {
        if (sourceFile.getAbsolutePath().equals(targetFile.getAbsolutePath())) {
            useGameImageIfValid(sourceFile);
            return;
        }
        copyGameImageWithProgress(sourceFile.getName(), targetFile, sourceFile.length(), new GameImageSource() {
            @Override
            public InputStream open() throws IOException {
                return new FileInputStream(sourceFile);
            }
        });
    }

    private interface GameImageSource {
        InputStream open() throws IOException;
    }

    private void copyGameImageWithProgress(final String name, final File targetFile,
                                           final long expected, final GameImageSource source) {
        final ProgressDialog progressDialog = new ProgressDialog(this);
        progressDialog.setTitle("Copying game image");
        progressDialog.setMessage("Checking and copying " + name + ".\nPlease keep the app open.");
        progressDialog.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
        progressDialog.setCancelable(false);
        progressDialog.setCanceledOnTouchOutside(false);
        progressDialog.setMax(100);
        progressDialog.setIndeterminate(expected <= 0);
        progressDialog.show();

        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    long copied = RowellGameImage.copy(source.open(), name, targetFile, expected,
                        new RowellGameImage.Progress() {
                            private long lastUpdate;
                            @Override
                            public void update(long bytes) {
                                long now = System.currentTimeMillis();
                                if (expected > 0 && now - lastUpdate >= 250) {
                                    lastUpdate = now;
                                    final int percent = (int) Math.min(99, bytes * 100 / expected);
                                    runOnUiThread(new Runnable() {
                                        @Override
                                        public void run() {
                                            progressDialog.setProgress(percent);
                                        }
                                    });
                                }
                            }
                        });
                    log("Validated game image copied: " + targetFile.getAbsolutePath() + " (" + copied + " bytes)");
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            progressDialog.dismiss();
                            if (isFinishing()) return;
                            sRomPath = targetFile.getAbsolutePath();
                            mRomReady = true;
                        }
                    });
                } catch (final Exception e) {
                    log("Game image selection failed: " + e.getMessage());
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            progressDialog.dismiss();
                            showGameImageProblem(e instanceof RowellGameImage.RejectedImage
                                ? "Choose a game image" : "Could not copy the game image", e.getMessage());
                        }
                    });
                }
            }
        }, "RomCopyThread").start();
    }

    private void showGameImageProblem(String title, String message) {
        if (isFinishing() || isDestroyed()) return;
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle(title);
        builder.setMessage(message);
        builder.setPositiveButton("Choose another file", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                launchRomPicker();
            }
        });
        builder.setNegativeButton("Exit", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                finish();
            }
        });
        builder.setCancelable(false);
        builder.show();
    }

    private void useGameImageIfValid(File file) {
        String error = RowellGameImage.problem(file);
        if (error != null) {
            showGameImageProblem("Choose a game image", error);
            return;
        }
        sRomPath = file.getAbsolutePath();
        mRomReady = true;
    }

    private boolean isUsableRom(File file) {
        if (!file.isFile() || !isRomFile(file.getName())) return false;
        String error = RowellGameImage.problem(file);
        if (error == null) return true;
        log("Skipped invalid game image: " + file.getAbsolutePath() + " (" + error + ")");
        return false;
    }

    private File findRomInDir(File dir) {
        if (dir == null || !dir.exists() || !dir.isDirectory()) return null;
        try {
            File[] files = dir.listFiles();
            if (files != null) {
                for (File f : files) {
                    if (isUsableRom(f)) {
                        return f;
                    }
                }
            }
        } catch (Exception ignored) {}
        return null;
    }

    private boolean hasStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        }
        return true;
    }

    private void requestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.addCategory("android.intent.category.DEFAULT");
                intent.setData(Uri.parse(String.format("package:%s", getPackageName())));
                startActivity(intent);
            } catch (Exception e) {
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivity(intent);
                } catch (Exception ex) {
                    log("Error opening MANAGE_ALL_FILES settings: " + ex.getMessage());
                }
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            requestPermissions(new String[]{
                Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE
            }, REQUEST_STORAGE_PERMS);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE_PERMS) {
            if (hasStoragePermission()) {
                checkRomAndPrepare();
            } else {
                log("Storage permission denied by user.");
            }
        }
    }

    @Override
    protected void onDestroy() {
        log("OceanTriActivity.onDestroy() called - terminating process cleanly");
        super.onDestroy();
        android.os.Process.killProcess(android.os.Process.myPid());
        System.exit(0);
    }

    // A separate process relaunches SDL after the current game process exits.
    public void restartGame() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                log("Restarting the development game with saved settings");
                Intent restart = new Intent(OceanTriActivity.this, RowellRestartActivity.class);
                restart.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                restart.putExtra("game_pid", android.os.Process.myPid());
                startActivity(restart);
            }
        });
    }

    private String findGameRom() {
        List<File> searchDirs = new ArrayList<>();
        searchDirs.add(new File("/storage/emulated/0/Download"));
        searchDirs.add(new File("/storage/emulated/0/MH3_Low_Spec"));
        searchDirs.add(new File("/storage/emulated/0/OceanTri"));
        searchDirs.add(new File("/storage/emulated/0/Wii"));
        searchDirs.add(new File("/storage/emulated/0/ROMs"));
        searchDirs.add(new File("/storage/emulated/0/Games"));
        searchDirs.add(new File("/storage/emulated/0"));
        searchDirs.add(new File("/sdcard/Download"));
        searchDirs.add(new File("/sdcard/MH3_Low_Spec"));
        searchDirs.add(new File("/sdcard/OceanTri"));
        searchDirs.add(new File("/sdcard/Wii"));
        searchDirs.add(new File("/sdcard"));
        File extFiles = getExternalFilesDir(null);
        if (extFiles != null) searchDirs.add(extFiles);

        for (File dir : searchDirs) {
            if (!dir.exists() || !dir.isDirectory()) continue;
            try {
                File[] files = dir.listFiles();
                if (files == null) continue;
                for (File f : files) {
                    if (isUsableRom(f)) {
                        return f.getAbsolutePath();
                    } else if (f.isDirectory()) {
                        File[] subFiles = f.listFiles();
                        if (subFiles != null) {
                            for (File sf : subFiles) {
                                if (isUsableRom(sf)) {
                                    return sf.getAbsolutePath();
                                }
                            }
                        }
                    }
                }
            } catch (Exception e) {
                log("Error reading directory " + dir.getAbsolutePath() + ": " + e.getMessage());
            }
        }
        return null;
    }

    private boolean isRomFile(String name) {
        return RowellGameImage.hasImageExtension(name);
    }
}
