package org.agbplay.saftest;

import android.content.Context;
import android.content.ContentValues;
import android.net.Uri;
import android.os.Environment;
import android.provider.MediaStore;
import android.provider.DocumentsContract;
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
        TestProvider.name = "Pokémon #1 %.GBA";
    }
    protected void tearDown() throws Exception {
        deleteChildren(directory);
        assertTrue(directory.delete());
        super.tearDown();
    }
    private void deleteChildren(File parent) {
        for (File file : parent.listFiles()) {
            if (file.isDirectory()) deleteChildren(file);
            assertTrue(file.delete());
        }
    }
    public void testEncodedUriPipeAndDisplayName() throws Exception {
        String path = DocumentImporter.importDocument(context, URI, directory.getPath());
        assertEquals(URI, TestProvider.lastUri);
        assertEquals("Pokémon #1 %.GBA", new File(path).getName());
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
    public void testGsfNamesAndUriIsolation() throws Exception {
        TestProvider.name = "fixture.gsflib";
        String library = DocumentImporter.importDocument(context, URI + "1", directory.getPath());
        TestProvider.name = "01 canción.minigsf";
        String first = DocumentImporter.importDocument(context, URI + "2", directory.getPath());
        String other = DocumentImporter.importDocument(context, URI + "3", directory.getPath());
        assertEquals("fixture.gsflib", new File(library).getName());
        assertEquals("01 canción.minigsf", new File(first).getName());
        assertFalse(first.equals(other));
        assertTrue(Arrays.equals(TestProvider.DATA, Files.readAllBytes(new File(first).toPath())));
    }
    public void testUnsafeDisplayNameCannotEscape() throws Exception {
        TestProvider.name = "../../escape\\outside.gsflib";
        String imported = DocumentImporter.importDocument(context, URI, directory.getPath());
        assertTrue(new File(imported).getCanonicalPath().startsWith(directory.getCanonicalPath() + File.separator));
        assertFalse(new File(imported).getName().contains("/"));
        assertFalse(new File(imported).getName().contains("\\"));
    }
    public void testFolderDiscoveryAndImport() throws Exception {
        Uri tree = DocumentsContract.buildTreeDocumentUri("org.agbplay.saftest.documents", "root/with spaces %");
        String[] files = DocumentImporter.listGsfDocuments(context, tree.toString());
        assertEquals(6, files.length); // only the library and two distinct MINIGSFs
        assertEquals("Pokémon.GSFLIB", files[1]);
        assertEquals("01 canción.minigsf", files[3]);
        assertEquals("opaque:lib# %", DocumentsContract.getDocumentId(Uri.parse(files[0])));
        assertEquals("mini:one/encoded%", DocumentsContract.getDocumentId(Uri.parse(files[2])));
        for (int i = 0; i < files.length; i += 2) {
            String imported = DocumentImporter.importDocument(context, files[i], directory.getPath());
            assertEquals(files[i + 1], new File(imported).getName());
            assertTrue(Arrays.equals(TestProvider.DATA, Files.readAllBytes(new File(imported).toPath())));
        }
    }
    public void testFolderEmptyDeniedAndInvalidUri() throws Exception {
        String authority = "org.agbplay.saftest.documents";
        assertEquals(0, DocumentImporter.listGsfDocuments(context,
                DocumentsContract.buildTreeDocumentUri(authority, "empty").toString()).length);
        try {
            DocumentImporter.listGsfDocuments(context, URI);
            fail("An individual document is not a folder grant");
        } catch (IOException expected) { assertTrue(expected.getMessage().contains("folder")); }
        try {
            DocumentImporter.listGsfDocuments(context, DocumentsContract.buildTreeDocumentUri(authority, "denied").toString());
            fail("Expected denied tree grant");
        } catch (SecurityException expected) { assertTrue(expected.getMessage().contains("permission denied")); }
        assertEquals(0, directory.list().length);
    }
    public void testFolderEntryLimit() throws Exception {
        try {
            DocumentImporter.listGsfDocuments(context,
                    DocumentsContract.buildTreeDocumentUri("org.agbplay.saftest.documents", "large").toString());
            fail("Expected folder entry limit");
        } catch (IOException expected) { assertTrue(expected.getMessage().contains("Too many entries")); }
    }
}
