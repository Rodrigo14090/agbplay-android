package org.agbplay.android;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import java.lang.ref.WeakReference;
import org.qtproject.qt.android.QtNative;

/** Only presentation orientation; never participates in audio or services. */
public final class MilkDropOrientation {
    private static WeakReference<Activity> owner = new WeakReference<>(null);
    private static int previous = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
    private MilkDropOrientation() { }
    public static void setLandscape(boolean enabled) {
        final Activity activity = QtNative.activity();
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            if (enabled) {
                if (owner.get() == activity) return;
                previous = activity.getRequestedOrientation();
                owner = new WeakReference<>(activity);
                activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
            } else {
                Activity original = owner.get();
                owner.clear();
                if (original != null && !original.isFinishing()) original.setRequestedOrientation(previous);
            }
        });
    }
}
