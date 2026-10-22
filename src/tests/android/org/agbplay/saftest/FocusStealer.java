package org.agbplay.saftest;
import android.app.Activity;
import android.media.*;
import android.os.Bundle;
import android.widget.TextView;
/** Emulator test companion: a real top Activity requesting competing audio focus. */
public final class FocusStealer extends Activity {
    private AudioManager manager;
    private AudioFocusRequest request;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView text = new TextView(this);
        text.setText("Synthetic audio focus test"); setContentView(text);
        manager = (AudioManager)getSystemService(AUDIO_SERVICE);
        request = new AudioFocusRequest.Builder(getIntent().getBooleanExtra("transient", false)
                ? AudioManager.AUDIOFOCUS_GAIN_TRANSIENT : AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setOnAudioFocusChangeListener(change -> {}).build();
    }
    @Override public void onPostResume() {
        super.onPostResume();
        if (manager.requestAudioFocus(request) != AudioManager.AUDIOFOCUS_REQUEST_GRANTED)
            throw new IllegalStateException("Test Activity could not acquire audio focus");
    }
    @Override public void onDestroy() { manager.abandonAudioFocusRequest(request); super.onDestroy(); }
}
