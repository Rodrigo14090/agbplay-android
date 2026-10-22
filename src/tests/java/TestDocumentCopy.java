import org.agbplay.android.DocumentCopy;
import java.io.*;
import java.util.Arrays;

public final class TestDocumentCopy {
    private static int tests;
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    private static void fails(String message, InputStream input, OutputStream output, long limit) throws Exception {
        try { DocumentCopy.copy(input, output, limit); throw new AssertionError("Missing failure"); }
        catch (IOException error) { check(error.getMessage().contains(message)); }
        tests++;
    }
    public static void main(String[] args) throws Exception {
        byte[] expected = new byte[1024 * 1024 + 53];
        for (int i = 0; i < expected.length; ++i) expected[i] = (byte)(i * 31);
        InputStream pipe = new ByteArrayInputStream(expected) {
            @Override public synchronized int read(byte[] bytes, int offset, int count) {
                return super.read(bytes, offset, Math.min(37, count));
            }
            @Override public synchronized int available() { throw new AssertionError("size assumption"); }
        };
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        check(DocumentCopy.copy(pipe, output, expected.length) == expected.length);
        check(Arrays.equals(expected, output.toByteArray()));
        tests++;
        fails("empty", new ByteArrayInputStream(new byte[0]), OutputStream.nullOutputStream(), 100);
        fails("limit", new ByteArrayInputStream(new byte[101]), OutputStream.nullOutputStream(), 100);
        fails("provider read failure", new InputStream() {
            public int read() throws IOException { throw new IOException("provider read failure"); }
        }, OutputStream.nullOutputStream(), 100);
        fails("storage full", new ByteArrayInputStream(new byte[]{1}), new OutputStream() {
            public void write(int value) throws IOException { throw new IOException("storage full"); }
        }, 100);
        InputStream zeroRead = new InputStream() {
            int value;
            public int read(byte[] bytes, int offset, int count) { return value == 3 ? -1 : 0; }
            public int read() { return value == 3 ? -1 : ++value; }
        };
        output.reset();
        check(DocumentCopy.copy(zeroRead, output, 3) == 3);
        check(Arrays.equals(output.toByteArray(), new byte[]{1, 2, 3}));
        tests++;
        fails("limit", new InputStream() {
            long remaining = DocumentCopy.LIMIT + 1;
            public int read() { if (remaining == 0) return -1; --remaining; return 0; }
            public int read(byte[] bytes, int offset, int count) {
                if (remaining == 0) return -1;
                int n = (int)Math.min(count, remaining);
                remaining -= n;
                return n;
            }
        }, OutputStream.nullOutputStream(), DocumentCopy.LIMIT);
        System.out.println("DocumentCopy: " + tests + " tests passed");
    }
}
