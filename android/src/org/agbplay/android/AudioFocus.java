package org.agbplay.android;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;

/** Android focus policy only. Synthesis and audio callbacks remain entirely C++. */
public final class AudioFocus {
    private final Context context;
    private final AudioManager manager;
    private final AudioFocusRequest request;
    private volatile boolean focused;
    private boolean registered;
    private final BroadcastReceiver noisy = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(intent.getAction()))
                abandon();
        }
    };

    public AudioFocus(Context context) {
        this.context = context;
        manager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        request = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setWillPauseWhenDucked(true)
                .setOnAudioFocusChangeListener(change -> focused = change == AudioManager.AUDIOFOCUS_GAIN)
                .build();
        IntentFilter filter = new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
        if (Build.VERSION.SDK_INT >= 33)
            context.registerReceiver(noisy, filter, Context.RECEIVER_NOT_EXPORTED);
        else
            context.registerReceiver(noisy, filter);
        registered = true;
    }
    public boolean request() {
        focused = manager.requestAudioFocus(request) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        return focused;
    }
    public boolean hasFocus() { return focused; }
    public void abandon() {
        focused = false;
        manager.abandonAudioFocusRequest(request);
    }
    public void close() {
        abandon();
        if (registered) { context.unregisterReceiver(noisy); registered = false; }
    }
}
