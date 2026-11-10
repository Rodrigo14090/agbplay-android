package org.agbplay.android;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import java.lang.ref.WeakReference;

/** Only presentation orientation; never participates in audio or services. */
public final class MilkDropOrientation {
    private static native void nativeExit();
    private static WeakReference<Activity> owner = new WeakReference<>(null);
    private static int previous = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
    private static int previousVisibility;
    private static int previousBehavior;
    private static int previousVisibleTypes;
    private MilkDropOrientation() { }
    /** Called by Activity on Android's main thread, including system back gestures. */
    public static boolean consumeBack() {
        if (owner.get() == null) return false;
        try { nativeExit(); return true; }
        catch (UnsatisfiedLinkError error) { return false; }
    }
    public static void setLandscape(Activity activity, boolean enabled) {
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            if (enabled) {
                if (owner.get() == activity) return;
                previous = activity.getRequestedOrientation();
                owner = new WeakReference<>(activity);
                View decor = activity.getWindow().getDecorView();
                previousVisibility = decor.getSystemUiVisibility();
                if (Build.VERSION.SDK_INT >= 30) {
                    WindowInsetsController controller = activity.getWindow().getInsetsController();
                    WindowInsets insets = decor.getRootWindowInsets();
                    previousVisibleTypes = insets == null ? WindowInsets.Type.systemBars()
                            : ((insets.isVisible(WindowInsets.Type.statusBars()) ? WindowInsets.Type.statusBars() : 0)
                            | (insets.isVisible(WindowInsets.Type.navigationBars()) ? WindowInsets.Type.navigationBars() : 0));
                    if (controller != null) {
                        previousBehavior = controller.getSystemBarsBehavior();
                        controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                        controller.hide(WindowInsets.Type.systemBars());
                    }
                } else {
                    decor.setSystemUiVisibility(previousVisibility | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
                }
                activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
            } else {
                Activity original = owner.get();
                owner.clear();
                if (original != null && !original.isFinishing()) {
                    View decor = original.getWindow().getDecorView();
                    decor.setSystemUiVisibility(previousVisibility);
                    if (Build.VERSION.SDK_INT >= 30) {
                        WindowInsetsController controller = original.getWindow().getInsetsController();
                        if (controller != null) {
                            controller.setSystemBarsBehavior(previousBehavior);
                            controller.show(previousVisibleTypes);
                            controller.hide(WindowInsets.Type.systemBars() & ~previousVisibleTypes);
                        }
                    }
                    original.setRequestedOrientation(previous);
                }
            }
        });
    }
}
