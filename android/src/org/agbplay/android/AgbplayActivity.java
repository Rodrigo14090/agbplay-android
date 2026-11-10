package org.agbplay.android;

import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import org.qtproject.qt.android.bindings.QtActivity;

/** Qt retains ownership of its views, document picker and configuration changes. */
public final class AgbplayActivity extends QtActivity {
    private void record(String event) {
        Log.i("agbplay.lifecycle", "Activity " + Integer.toHexString(System.identityHashCode(this))
                + " " + event + ", finishing=" + isFinishing() + ", changingConfig=" + isChangingConfigurations());
    }
    @Override public void onCreate(Bundle state) {
        LifecycleDiagnostics.install(getApplicationContext());
        record("create");
        super.onCreate(state);
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        // Qt handles KEYCODE_BACK before Activity.onBackPressed and may close
        // the hidden main window. Consume both halves at the Android boundary.
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK && MilkDropOrientation.isPresenting()) {
            if (event.getAction() == KeyEvent.ACTION_UP) MilkDropOrientation.consumeBack();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
    @Override public void onBackPressed() {
        if (MilkDropOrientation.consumeBack()) return;
        super.onBackPressed();
    }
    @Override public void onResume() { super.onResume(); record("resume"); }
    @Override public void onPause() { record("pause"); super.onPause(); }
    @Override public void onStop() { record("stop"); super.onStop(); }
    @Override public void onDestroy() {
        record("destroy");
        if (!isChangingConfigurations())
            PlaybackService.requestAppShutdown("Activity destroyed");
        // Qt's teardown can wait for the native main() to return. The request
        // above exits its event loop without an invisible profile-save dialog.
        super.onDestroy();
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        Log.i("agbplay.lifecycle", "document/activity result: request=" + request + ", result=" + result);
        super.onActivityResult(request, result, data);
    }
    @Override public void startActivityForResult(Intent intent, int request, Bundle options) {
        DocumentPickerIntent.normalize(intent);
        super.startActivityForResult(intent, request, options);
    }
}
