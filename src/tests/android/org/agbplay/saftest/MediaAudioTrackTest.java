package org.agbplay.saftest;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.test.InstrumentationTestCase;
import org.agbplay.android.MediaAudioTrack;

public final class MediaAudioTrackTest extends InstrumentationTestCase {
    public void testMediaAttributesSessionAndNonblockingTransport() throws Exception {
        for (int cycle = 0; cycle < 6; ++cycle) {
            MediaAudioTrack output = new MediaAudioTrack(48000);
            try {
                java.lang.reflect.Field member = MediaAudioTrack.class.getDeclaredField("track");
                member.setAccessible(true);
                AudioTrack track = (AudioTrack) member.get(output);
                assertEquals(48000, track.getSampleRate());
                assertEquals(2, track.getChannelCount());
                assertEquals(AudioFormat.ENCODING_PCM_FLOAT, track.getAudioFormat());
                assertEquals(AudioAttributes.USAGE_MEDIA, track.getAudioAttributes().getUsage());
                assertEquals(AudioAttributes.CONTENT_TYPE_MUSIC, track.getAudioAttributes().getContentType());
                assertEquals(AudioTrack.PERFORMANCE_MODE_NONE, track.getPerformanceMode());
                assertTrue(track.getAudioSessionId() > 0);
                assertTrue(output.bufferFrames() > 0);
                float[] pcm = new float[output.bufferFrames() * 4];
                int written = output.write(pcm, 0, pcm.length);
                assertTrue(written > 0 && written <= pcm.length && written % 2 == 0);
                // A full, unstarted native buffer must return promptly rather
                // than strand the worker when another thread requests pause.
                long start = android.os.SystemClock.elapsedRealtime();
                for (int attempt = 0; attempt < 20; ++attempt) {
                    int accepted = output.write(pcm, 0, pcm.length);
                    assertTrue(accepted >= 0 && accepted <= pcm.length && accepted % 2 == 0);
                }
                assertTrue(android.os.SystemClock.elapsedRealtime() - start < 1000);
                output.play();
                Thread.sleep(60);
                output.pause();
                assertTrue(track.getPlaybackHeadPosition() > 0);
                output.flush();
                output.play();
                output.write(pcm, 0, Math.min(480, pcm.length));
                output.pause();
                assertTrue(output.diagnostics().contains("proprietary DSP status unknown"));
            } finally { output.close(); }
        }
    }
}
