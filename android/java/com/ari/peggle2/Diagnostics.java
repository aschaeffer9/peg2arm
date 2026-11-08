package com.ari.peggle2;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Date;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * peggle2-android (2026-11): the logs in a place the player can reach.
 *
 * The app's folder (Android/data/com.ari.peggle2/files) has been invisible to file managers and to USB transfers
 * since Android 11, so a player reporting a crash had no way to send the log: the only routes were adb or the app
 * itself. This copies the text logs, plus a device report, into Downloads - which every file manager can open and
 * every share sheet can send - when the previous run did not finish cleanly.
 *
 * The marker: onCreate writes logs/session.lock, and the native side removes it once the run has presented about
 * 10 seconds of frames (gpu/backend/present.cpp), or on the game's own Exit (windowed_app_main_sdl.cpp). A launch that
 * finds it knows the last start-up never got that far - including a GPU fault, which kills the process without
 * running any of our code - and exports before the new run truncates the log (Video::CreateHostDevice reopens
 * logs/plume_stderr.log with "w"). A run killed later (Recents, the system) has no marker left and reports nothing.
 * The check is made once per process: an activity recreated inside a running process finds this run's own marker.
 *
 * Everything here is best effort: a failure is logged and never affects the game.
 */
final class Diagnostics {
    private static final String TAG = "peggle2";
    private static final String MARKER = "session.lock";
    private static final String STARTUP_ERROR = "startup_error.txt";
    // Text only: the pipeline cache and the like would make the zip tens of megabytes.
    private static final String[] KINDS = { ".log", ".txt" };
    private static final long MAX_FILE = 4L << 20;
    private static final int MAX_FILES = 40;
    static final String ISSUES = "https://github.com/aschaeffer9/peg2arm/issues";

    /** The previous run's renderer failure, for the activity to show once it is up (null if there is none). */
    static String startupError;

    /** Set once beforeStart has run in this process. */
    private static boolean started;

    /** Called from onCreate before super.onCreate: reads the previous run's state and marks this one. */
    static void beforeStart(Activity activity) {
        if (started) {
            return;
        }
        started = true;
        try {
            File files = activity.getExternalFilesDir(null);
            if (files == null) {
                return;
            }
            File logs = new File(files, "logs");
            File marker = new File(logs, MARKER);
            File error = new File(logs, STARTUP_ERROR);
            if (marker.exists() || error.exists()) {
                String name = export(activity, files, logs);
                if (name != null) {
                    toast(activity, "Peggle 2: logs saved to Downloads/" + name);
                }
            }
            if (error.exists()) {
                startupError = read(error);
                error.delete();
            }
            logs.mkdirs();
            new FileOutputStream(marker).close();
        } catch (Throwable t) {
            Log.w(TAG, "diagnostics: before start failed", t);
        }
    }

    /** The zip's name in Downloads, or null. */
    private static String export(Activity activity, File files, File logs) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            return null;  // no MediaStore.Downloads: this build's minSdk is well above it
        }
        final String model = Build.MODEL == null ? "device" : Build.MODEL.replaceAll("[^A-Za-z0-9._-]", "_");
        final String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
        final String name = "Peggle2-logs-" + model + "-" + stamp + ".zip";
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
        values.put(MediaStore.MediaColumns.MIME_TYPE, "application/zip");
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        ContentResolver resolver = activity.getContentResolver();
        Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (uri == null) {
            return null;
        }
        try (OutputStream out = resolver.openOutputStream(uri);
             ZipOutputStream zip = new ZipOutputStream(out)) {
            add(zip, new File(logs, STARTUP_ERROR), "startup_error.txt");
            final File[] logs_ = logs.listFiles();
            if (logs_ != null) {
                // Newest first. (No lambdas or method references: the APK's javac has no LambdaMetafactory.)
                Arrays.sort(logs_, new Comparator<File>() {
                    @Override
                    public int compare(File a, File b) {
                        return Long.compare(b.lastModified(), a.lastModified());
                    }
                });
                int added = 0;
                for (File file : logs_) {
                    if (added >= MAX_FILES) {
                        break;
                    }
                    if (file.isFile() && wanted(file.getName()) && file.length() <= MAX_FILE) {
                        add(zip, file, "logs/" + file.getName());
                        ++added;
                    }
                }
            }
            add(zip, report(activity, files), "device.txt");
        } catch (Throwable t) {
            Log.w(TAG, "diagnostics: export failed", t);
            resolver.delete(uri, null, null);
            return null;
        }
        values.clear();
        values.put(MediaStore.MediaColumns.IS_PENDING, 0);
        resolver.update(uri, values, null, null);
        return name;
    }

    private static boolean wanted(String fileName) {
        for (String kind : KINDS) {
            if (fileName.endsWith(kind)) {
                return true;
            }
        }
        return false;
    }

    private static void add(ZipOutputStream zip, File file, String entry) throws Exception {
        if (file == null || !file.isFile()) {
            return;
        }
        zip.putNextEntry(new ZipEntry(entry));
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0) {
                zip.write(buffer, 0, n);
            }
        }
        zip.closeEntry();
    }

    /** What device this is; the GPU's own limits are in logs/plume_stderr.log's "[android]" block. */
    private static File report(Activity activity, File files) throws Exception {
        File out = new File(activity.getCacheDir(), "device.txt");
        StringBuilder text = new StringBuilder();
        text.append("Peggle 2 (peg2arm) diagnostics\n");
        text.append("generated  ").append(new Date().toString()).append('\n');
        text.append("model      ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append('\n');
        text.append("device     ").append(Build.DEVICE).append(" (").append(Build.HARDWARE).append(")\n");
        text.append("android    ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT)
            .append(")\n");
        text.append("abis       ").append(Arrays.toString(Build.SUPPORTED_ABIS)).append('\n');
        text.append("fingerprint ").append(Build.FINGERPRINT).append('\n');
        text.append("app        ").append(version(activity)).append('\n');
        text.append("app folder ").append(files.getAbsolutePath()).append('\n');
        text.append('\n').append("The GPU (deviceName, driver, limits, descriptor indexing) is the [android] block at");
        text.append('\n').append("the top of logs/plume_stderr.log. Builds write it before anything else.\n");
        try (OutputStream os = new FileOutputStream(out)) {
            os.write(text.toString().getBytes(StandardCharsets.UTF_8));
        }
        return out;
    }

    /** "0.2 (2)", so a report says which build made it. */
    private static String version(Activity activity) {
        try {
            android.content.pm.PackageInfo info = activity.getPackageManager()
                .getPackageInfo(activity.getPackageName(), 0);
            return info.versionName + " (" + info.getLongVersionCode() + ")";
        } catch (Throwable t) {
            return "unknown";
        }
    }

    private static String read(File file) {
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] bytes = new byte[(int) Math.min(file.length(), 4096)];
            int n = in.read(bytes);
            return n <= 0 ? "" : new String(bytes, 0, n, StandardCharsets.UTF_8).trim();
        } catch (Throwable t) {
            return "";
        }
    }

    private static void toast(Activity activity, String message) {
        try {
            Toast.makeText(activity, message, Toast.LENGTH_LONG).show();
        } catch (Throwable t) {
            Log.w(TAG, "diagnostics: toast failed", t);
        }
    }

    private Diagnostics() {
    }
}
