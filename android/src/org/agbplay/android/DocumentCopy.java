package org.agbplay.android;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** Bounded sequential copy. Never depends on size(), available(), seeking or metadata. */
public final class DocumentCopy {
    public static final long LIMIT = 64L * 1024 * 1024;
    private DocumentCopy() {}

    public static long copy(InputStream input, OutputStream output, long limit) throws IOException {
        if (limit < 0) throw new IllegalArgumentException("Negative import limit");
        byte[] buffer = new byte[64 * 1024];
        long total = 0;
        while (true) {
            int count = input.read(buffer);
            if (count == -1) break;
            if (count == 0) {
                // A broken/slow stream must not leave us in a zero-progress loop.
                int value = input.read();
                if (value == -1) break;
                buffer[0] = (byte) value;
                count = 1;
            }
            if (count > limit - total)
                throw new IOException("Document exceeds " + limit + " byte import limit");
            output.write(buffer, 0, count);
            total += count;
        }
        if (total == 0) throw new IOException("Selected document is empty");
        return total;
    }
}
