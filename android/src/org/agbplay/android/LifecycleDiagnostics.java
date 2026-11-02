package org.agbplay.android;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;
import android.os.Process;
import android.util.AtomicFile;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

/** Control-plane diagnostics only; never called from PCM or synthesis threads. */
final class LifecycleDiagnostics {
    private static final String TAG = "agbplay.lifecycle";
    private static boolean installed;
    static synchronized void install(Context context) {
        if (installed) return;
        installed = true;
        String version = "unknown";
        try { version = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName; }
        catch (Exception ignored) { }
        final String header = "agbplay " + version + ", " + Build.MANUFACTURER + " " + Build.MODEL
                + ", API " + Build.VERSION.SDK_INT + ", pid " + Process.myPid();
        Log.i(TAG, header);
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        if (previous != null) Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try {
                Log.e(TAG, "Uncaught Java exception in " + thread.getName(), error);
                String report = header + "\nThread: " + thread.getName() + "\n" + Log.getStackTraceString(error);
                if (report.length() > 65536) report = report.substring(0, 65536);
                AtomicFile file = new AtomicFile(new File(context.getFilesDir(), "last-java-crash.txt"));
                FileOutputStream output = null;
                try {
                    output = file.startWrite();
                    output.write(report.getBytes(StandardCharsets.UTF_8));
                    file.finishWrite(output);
                } catch (Exception ignored) { if (output != null) file.failWrite(output); }
            } finally { previous.uncaughtException(thread, error); }
        });
        // Native crashes/ANRs still belong to Android/debuggerd; do not install
        // signal handlers or claim a Java handler captures a native backtrace.
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                ActivityManager manager = context.getSystemService(ActivityManager.class);
                for (ApplicationExitInfo exit : manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 3))
                    Log.i(TAG, "previous exit: timestamp=" + exit.getTimestamp() + ", reason=" + exit.getReason()
                            + ", status=" + exit.getStatus() + ", description=" + exit.getDescription());
            } catch (RuntimeException error) { Log.w(TAG, "Exit history unavailable", error); }
        }
    }
}
