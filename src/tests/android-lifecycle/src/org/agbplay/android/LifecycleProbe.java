package org.agbplay.android;

import android.app.Activity;
import android.app.ActivityManager;
import android.os.Handler;
import android.os.Looper;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

/** Packaged only in the regression APK, never in agbplay-gui. */
public final class LifecycleProbe {
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static Object retiredService;
    private static Runnable retiredExpiry;

    private static Object field(Object object, String name) throws ReflectiveOperationException {
        Field field = PlaybackService.class.getDeclaredField(name);
        field.setAccessible(true);
        return field.get(object);
    }
    public static void expirePausedService() {
        MAIN.post(() -> {
            try {
                Object service = field(null, "instance");
                if (service == null || (Boolean)field(service, "playing"))
                    throw new IllegalStateException("Expected a paused live service");
                retiredService = service;
                retiredExpiry = (Runnable)field(service, "expirePause");
                retiredExpiry.run(); // same callback as the 5-minute deadline
            } catch (ReflectiveOperationException error) { throw new IllegalStateException(error); }
        });
    }
    public static void runRetiredCallbacks() {
        MAIN.post(() -> {
            try {
                if (retiredService == null || !(Boolean)field(retiredService, "destroyed"))
                    throw new IllegalStateException("Expected a destroyed old service");
                for (String name : new String[]{"startPlayback", "resumeWithFocus", "publish"}) {
                    Method method = PlaybackService.class.getDeclaredMethod(name);
                    method.setAccessible(true);
                    method.invoke(retiredService);
                }
                Method pause = PlaybackService.class.getDeclaredMethod("pausePlayback", boolean.class);
                pause.setAccessible(true);
                pause.invoke(retiredService, false);
                retiredExpiry.run();
            } catch (ReflectiveOperationException error) { throw new IllegalStateException(error); }
        });
    }
    public static void removeTask(Activity activity) {
        MAIN.post(() -> {
            ActivityManager manager = activity.getSystemService(ActivityManager.class);
            for (ActivityManager.AppTask task : manager.getAppTasks()) {
                if (task.getTaskInfo().taskId == activity.getTaskId()) {
                    task.finishAndRemoveTask();
                    return;
                }
            }
            throw new IllegalStateException("No app task to remove");
        });
    }
}
