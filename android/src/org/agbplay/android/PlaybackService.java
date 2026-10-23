package org.agbplay.android;

import android.app.*;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.media.*;
import android.media.session.*;
import android.os.*;

/** Keeps the existing native player alive; never generates or converts PCM. */
public final class PlaybackService extends Service {
    private static final String CHANNEL = "agbplay.playback";
    private static final int NOTIFICATION = 42;
    private static final String PLAY = "org.agbplay.PLAY", PAUSE = "org.agbplay.PAUSE", STOP = "org.agbplay.STOP";
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static volatile boolean nativeReady;
    private static volatile long latestGeneration;
    private long generation;
    private static PlaybackService instance; // Android main thread only
    private static native void nativeCommand(int command, long generation);
    public static void setNativeReady(boolean ready) { nativeReady = ready; }
    private void command(int value) {
        if (nativeReady && generation == latestGeneration) nativeCommand(value, generation);
    }

    public static void play(Context context, String title, long generation) {
        latestGeneration = generation;
        context.getApplicationContext().startForegroundService(new Intent(context, PlaybackService.class)
                .setAction(PLAY).putExtra("title", title).putExtra("generation", generation));
    }
    public static void pause(long generation) {
        latestGeneration = generation;
        MAIN.post(() -> {
            if (generation != latestGeneration) return;
            if (instance != null) { instance.generation = generation; instance.pausePlayback(false); }
            else if (nativeReady) nativeCommand(2, generation);
        });
    }
    public static void stop(Context context, long generation) {
        latestGeneration = generation;
        context.getApplicationContext().stopService(new Intent(context, PlaybackService.class));
    }
    public static void title(String title) {
        MAIN.post(() -> { if (instance != null) { instance.title = title; instance.publish(); } });
    }

    private AudioManager audio;
    private AudioFocusRequest focus;
    private MediaSession session;
    private PowerManager.WakeLock wake;
    private String title = "agbplay";
    private boolean playing, wanted, foreground, receiverRegistered, explicitStop;
    private final Runnable expirePause = () -> stopSelf();
    private final BroadcastReceiver noisy = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(intent.getAction())) pausePlayback(false);
        }
    };

    @Override public void onCreate() {
        super.onCreate();
        instance = this;
        NotificationManager notifications = getSystemService(NotificationManager.class);
        notifications.createNotificationChannel(new NotificationChannel(CHANNEL, "Reproducción de música", NotificationManager.IMPORTANCE_LOW));
        wake = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "agbplay:playback");
        wake.setReferenceCounted(false);
        audio = getSystemService(AudioManager.class);
        focus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setWillPauseWhenDucked(true)
                .setOnAudioFocusChangeListener(change -> {
                    if (change == AudioManager.AUDIOFOCUS_GAIN) {
                        if (wanted) resumeWithFocus();
                    } else if (change == AudioManager.AUDIOFOCUS_LOSS) {
                        pausePlayback(false);
                    } else if (change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT || change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) {
                        pausePlayback(true);
                    }
                }, MAIN).build();
        session = new MediaSession(this, "agbplay");
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() { startPlayback(); }
            @Override public void onPause() { pausePlayback(false); }
            @Override public void onStop() { explicitStop = true; stopSelf(); }
            @Override public void onSkipToNext() { command(4); }
            @Override public void onSkipToPrevious() { command(5); }
        }, MAIN);
        session.setFlags(MediaSession.FLAG_HANDLES_MEDIA_BUTTONS | MediaSession.FLAG_HANDLES_TRANSPORT_CONTROLS);
        session.setActive(true);
        IntentFilter filter = new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(noisy, filter, Context.RECEIVER_NOT_EXPORTED);
        else registerReceiver(noisy, filter);
        receiverRegistered = true;
    }

    @Override public int onStartCommand(Intent intent, int flags, int id) {
        if (!nativeReady || intent == null) { stopSelf(); return START_NOT_STICKY; }
        String action = intent.getAction();
        // UI requests may overtake service creation/destruction. A late PLAY
        // must not acquire focus or restart after a newer Pause/Stop.
        if (intent.hasExtra("generation")) {
            long requested = intent.getLongExtra("generation", 0);
            if (requested != latestGeneration) {
                if (!foreground) stopSelf();
                return START_NOT_STICKY;
            }
            generation = requested;
        }
        if (generation != latestGeneration) { if (!foreground) stopSelf(); return START_NOT_STICKY; }
        if (intent.hasExtra("title")) title = intent.getStringExtra("title");
        if (PLAY.equals(action)) startPlayback();
        else if (PAUSE.equals(action)) pausePlayback(false);
        else if (STOP.equals(action)) { explicitStop = true; stopSelf(); }
        return START_NOT_STICKY; // a restarted Java service cannot restore a missing C++ engine
    }

    private void startPlayback() {
        if (generation != latestGeneration) return;
        if (!nativeReady) { stopSelf(); return; }
        try {
        // Android 15 requires an active foreground service for background focus requests.
        foreground = true;
        if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIFICATION, notification(), ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        else startForeground(NOTIFICATION, notification());
        wanted = true;
        if (audio.requestAudioFocus(focus) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) resumeWithFocus();
        else { wanted = false; pausePlayback(false); command(6); }
        } catch (RuntimeException error) {
            android.util.Log.e("agbplay", "Cannot start foreground media playback", error);
            command(6);
            stopSelf();
        }
    }
    private void resumeWithFocus() {
        if (generation != latestGeneration) return;
        MAIN.removeCallbacks(expirePause);
        if (playing) { publish(); return; }
        if (!wake.isHeld()) wake.acquire(); // scoped to actual playback; never keeps the display on
        playing = true;
        command(1);
        publish();
    }
    private void pausePlayback(boolean transientLoss) {
        if (!transientLoss) { wanted = false; audio.abandonAudioFocusRequest(focus); }
        playing = false;
        if (wake.isHeld()) wake.release();
        command(2);
        publish();
        MAIN.removeCallbacks(expirePause);
        // Keep lock-screen resume available briefly, without holding the CPU awake.
        MAIN.postDelayed(expirePause, 5 * 60 * 1000);
    }
    private PendingIntent action(String name, int id) {
        Intent intent = new Intent(this, PlaybackService.class).setAction(name).putExtra("generation", generation);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE;
        return PLAY.equals(name) ? PendingIntent.getForegroundService(this, id, intent, flags)
                                : PendingIntent.getService(this, id, intent, flags);
    }
    private Notification notification() {
        Intent launcher = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent open = launcher == null ? null : PendingIntent.getActivity(this, 0, launcher, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.ic_media_play).setContentTitle(title).setContentText("agbplay")
                .setContentIntent(open).setOnlyAlertOnce(true).setVisibility(Notification.VISIBILITY_PUBLIC)
                .setCategory(Notification.CATEGORY_TRANSPORT).setOngoing(playing)
                .addAction(new Notification.Action.Builder(playing ? android.R.drawable.ic_media_pause : android.R.drawable.ic_media_play,
                        playing ? "Pausa" : "Reproducir", action(playing ? PAUSE : PLAY, 1)).build())
                .addAction(new Notification.Action.Builder(android.R.drawable.ic_menu_close_clear_cancel, "Detener", action(STOP, 2)).build())
                .setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken()).setShowActionsInCompactView(0, 1))
                .build();
    }
    private void publish() {
        session.setMetadata(new MediaMetadata.Builder().putString(MediaMetadata.METADATA_KEY_TITLE, title)
                .putString(MediaMetadata.METADATA_KEY_ARTIST, "agbplay").build());
        session.setPlaybackState(new PlaybackState.Builder()
                .setActions(PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE |
                        PlaybackState.ACTION_STOP | PlaybackState.ACTION_SKIP_TO_NEXT | PlaybackState.ACTION_SKIP_TO_PREVIOUS)
                .setState(playing ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED, PlaybackState.PLAYBACK_POSITION_UNKNOWN, playing ? 1 : 0).build());
        if (foreground) getSystemService(NotificationManager.class).notify(NOTIFICATION, notification());
    }
    @Override public void onDestroy() {
        if (instance == this) instance = null;
        MAIN.removeCallbacks(expirePause);
        // Expiring an idle service must not turn Pause into Stop or lose the
        // C++ song position. Explicit Stop still resets a paused player.
        boolean stopNative = playing || explicitStop;
        wanted = playing = false;
        if (wake != null && wake.isHeld()) wake.release();
        if (audio != null) audio.abandonAudioFocusRequest(focus);
        if (receiverRegistered) unregisterReceiver(noisy);
        if (session != null) { session.setActive(false); session.release(); }
        stopForeground(STOP_FOREGROUND_REMOVE);
        if (stopNative) command(3);
        super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent) { return null; }
}
