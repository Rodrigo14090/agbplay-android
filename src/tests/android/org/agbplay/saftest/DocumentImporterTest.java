package org.agbplay.saftest;

import android.content.Context;
import android.content.ContentValues;
import android.net.Uri;
import android.os.Environment;
import android.provider.MediaStore;
import java.io.OutputStream;
import android.test.InstrumentationTestCase;
import org.agbplay.android.DocumentImporter;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.Arrays;

public final class DocumentImporterTest extends InstrumentationTestCase {
    private static final String URI = "content://org.agbplay.saftest.documents/document/primary%3AMusic%2FPok%C3%A9mon%20%231%20%25.GBA";
    private Context context;
    private File directory;
    protected void setUp() throws Exception {
        super.setUp();
        context = getInstrumentation().getTargetContext();
        directory = new File(context.getCacheDir(), "document-test");
        directory.mkdirs();
        TestProvider.fail = false;
    }
    protected void tearDown() throws Exception {
        for (File file : directory.listFiles()) assertTrue(file.delete());
        assertTrue(directory.delete());
        super.tearDown();
    }
    public void testEncodedUriPipeAndDisplayName() throws Exception {
        String path = DocumentImporter.importDocument(context, URI, directory.getPath());
        assertEquals(URI, TestProvider.lastUri);
        assertTrue(path.endsWith(".gba"));
        assertTrue(Arrays.equals(TestProvider.DATA, Files.readAllBytes(new File(path).toPath())));
        // A second import of the same document replaces the cache, not a new file.
        assertEquals(path, DocumentImporter.importDocument(context, URI, directory.getPath()));
        assertEquals(1, directory.list().length);
    }
    public void testMetadataOptionalAndOpaqueId() throws Exception {
        String uri = "content://org.agbplay.saftest.documents/metadata-denied/opaque123";
        // Metadata failure must not block a successful openInputStream.
        // This provider's read rejection applies only to the separate /denied URI.
        String path = DocumentImporter.importDocument(context, uri, directory.getPath());
        assertTrue(path.endsWith(".bin"));
        assertTrue(Arrays.equals(TestProvider.DATA, Files.readAllBytes(new File(path).toPath())));
    }
    public void testReadFailureKeepsPreviousCopy() throws Exception {
        String path = DocumentImporter.importDocument(context, URI, directory.getPath());
        TestProvider.fail = true;
        try {
            DocumentImporter.importDocument(context, URI, directory.getPath());
            fail("Expected provider failure");
        } catch (IOException expected) { assertNotNull(expected.getMessage()); }
        assertTrue(Arrays.equals(TestProvider.DATA, Files.readAllBytes(new File(path).toPath())));
        assertEquals(1, directory.list().length);
    }
    public void testInternalDownloadsProvider() throws Exception {
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, "agbplay synthetic # %.gba");
        values.put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream");
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri uri = context.getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        assertNotNull(uri);
        try {
            try (OutputStream output = context.getContentResolver().openOutputStream(uri)) {
                assertNotNull(output);
                output.write(TestProvider.DATA);
            }
            values.clear();
            values.put(MediaStore.MediaColumns.IS_PENDING, 0);
            context.getContentResolver().update(uri, values, null, null);
            String path = DocumentImporter.importDocument(context, uri.toString(), directory.getPath());
            assertTrue(path.endsWith(".gba"));
            assertTrue(Arrays.equals(TestProvider.DATA, Files.readAllBytes(new File(path).toPath())));
        } finally {
            context.getContentResolver().delete(uri, null, null);
        }
    }
    public void testDeniedGrantHasDetailsAndNoPartialFile() throws Exception {
        try {
            DocumentImporter.importDocument(context, "content://org.agbplay.saftest.documents/denied", directory.getPath());
            fail("Expected denied grant");
        } catch (SecurityException expected) { assertTrue(expected.getMessage().contains("Read permission denied")); }
        assertEquals(0, directory.list().length);
    }
}
