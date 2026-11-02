package org.agbplay.android;

import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRouting;
import android.media.AudioTrack;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;

/** PCM transport only. The native worker owns the one synthesizer and gain. */
public final class MediaAudioTrack {
    private final AudioTrack track;
    private final AudioRouting.OnRoutingChangedListener routeListener;
    private volatile String routeEvent = "initial";
    public MediaAudioTrack(int rate) {
        int minimum = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_FLOAT);
        if (minimum <= 0) throw new IllegalArgumentException("AudioTrack float stereo unsupported: " + minimum);
        AudioTrack created = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(rate)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setEncoding(AudioFormat.ENCODING_PCM_FLOAT).build())
                .setTransferMode(AudioTrack.MODE_STREAM).setPerformanceMode(AudioTrack.PERFORMANCE_MODE_NONE)
                .setSessionId(AudioManager.AUDIO_SESSION_ID_GENERATE)
                .setBufferSizeInBytes(Math.max(minimum, (rate / 50) * 8)).build();
        track = created;
        routeListener = routing -> routeEvent = "changed";
        try {
            if (track.getState() != AudioTrack.STATE_INITIALIZED || track.getSampleRate() != rate)
                throw new IllegalStateException("AudioTrack initialization/format mismatch");
            track.addOnRoutingChangedListener(routeListener, new Handler(Looper.getMainLooper()));
        } catch (RuntimeException error) { track.release(); throw error; }
    }
    public int bufferFrames() { return track.getBufferSizeInFrames(); }
    public void writerPriority() { Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO); }
    public void play() { track.play(); }
    public void pause() { track.pause(); }
    public void flush() { track.flush(); }
    public int write(float[] pcm, int offset, int count) {
        return track.write(pcm, offset, count, AudioTrack.WRITE_NON_BLOCKING);
    }
    public String diagnostics() {
        AudioDeviceInfo device = track.getRoutedDevice();
        String route = device == null ? "not routed" : "id=" + device.getId() + ", type=" + device.getType() + ", name=" + device.getProductName();
        String event = routeEvent;
        routeEvent = "steady";
        return "AudioTrack native: rate=" + track.getSampleRate() + ", channels=" + track.getChannelCount()
                + ", encoding=" + track.getAudioFormat() + ", attributes=" + track.getAudioAttributes()
                + ", session=" + track.getAudioSessionId() + ", performance=" + track.getPerformanceMode()
                + ", buffer/capacity=" + track.getBufferSizeInFrames() + "/" + track.getBufferCapacityInFrames()
                + ", underrunCount=" + track.getUnderrunCount() + ", playbackHead=" + Integer.toUnsignedLong(track.getPlaybackHeadPosition())
                + ", route=" + route + ", event=" + event + "; proprietary DSP status unknown";
    }
    public void close() {
        track.removeOnRoutingChangedListener(routeListener);
        track.release();
    }
}
