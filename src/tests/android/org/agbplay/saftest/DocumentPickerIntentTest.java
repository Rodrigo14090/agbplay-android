package org.agbplay.saftest;

import android.content.Intent;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.test.InstrumentationTestCase;
import org.agbplay.android.DocumentPickerIntent;

public final class DocumentPickerIntentTest extends InstrumentationTestCase {
    public void testQtStringHintBecomesParcelableContentUri() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, "content://example.documents/document/folder");
        DocumentPickerIntent.normalize(intent);
        assertEquals(Uri.parse("content://example.documents/document/folder"),
                intent.getParcelableExtra(DocumentsContract.EXTRA_INITIAL_URI));
    }
    public void testPrivateCachePathIsNotPassedToProviderAndGrantsStayIntact() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, "file:///data/user/0/org.agbplay.android/cache/imports");
        int flags = intent.getFlags();
        DocumentPickerIntent.normalize(intent);
        assertFalse(intent.hasExtra(DocumentsContract.EXTRA_INITIAL_URI));
        assertEquals("*/*", intent.getType());
        assertEquals(flags, intent.getFlags());
        assertTrue(intent.getBooleanExtra(Intent.EXTRA_ALLOW_MULTIPLE, false));
    }
    public void testParcelableAndNonDocumentIntentsRemainValid() {
        Uri uri = Uri.parse("content://example.documents/tree/music");
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        picker.putExtra(DocumentsContract.EXTRA_INITIAL_URI, uri);
        DocumentPickerIntent.normalize(picker);
        assertEquals(uri, picker.getParcelableExtra(DocumentsContract.EXTRA_INITIAL_URI));
        Intent other = new Intent(Intent.ACTION_VIEW);
        other.putExtra(DocumentsContract.EXTRA_INITIAL_URI, "unrelated data");
        DocumentPickerIntent.normalize(other);
        assertEquals("unrelated data", other.getStringExtra(DocumentsContract.EXTRA_INITIAL_URI));
    }
}
