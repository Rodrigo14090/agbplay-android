package org.agbplay.saftest;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.provider.DocumentsContract;
import java.io.*;

/** Exercises the actual ContentResolver with non-seekable pipes and opaque IDs. */
public final class TestProvider extends ContentProvider {
    public static final byte[] DATA = new byte[150003];
    public static String lastUri;
    public static boolean fail;
    public static String name = "Pokémon #1 %.GBA";
    static { for (int i = 0; i < DATA.length; ++i) DATA[i] = (byte)(i * 31); }
    public boolean onCreate() { return true; }
    public String getType(Uri uri) { return "application/octet-stream"; }
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        if (DocumentsContract.isTreeUri(uri)) {
            String root = DocumentsContract.getTreeDocumentId(uri);
            if (root.equals("denied")) throw new SecurityException("Tree read permission denied");
            if (uri.getLastPathSegment().equals("children")) {
                MatrixCursor children = new MatrixCursor(new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE});
                if (root.equals("empty")) return children;
                if (root.equals("large")) {
                    for (int i = 0; i < 100001; i++) children.addRow(new Object[]{"irrelevant" + i, "cover.jpg", "image/jpeg"});
                    return children;
                }
                children.addRow(new Object[]{"opaque:lib# %", "Pokémon.GSFLIB", "application/octet-stream"});
                children.addRow(new Object[]{"mini:one/encoded%", "01 canción.minigsf", "application/octet-stream"});
                children.addRow(new Object[]{"mini:two", "02.minigsf", "application/octet-stream"});
                children.addRow(new Object[]{"mini:two", "02.minigsf", "application/octet-stream"}); // duplicate ID
                children.addRow(new Object[]{"cover", "cover.jpg", "image/jpeg"});
                children.addRow(new Object[]{"subfolder", "other.gsflib", DocumentsContract.Document.MIME_TYPE_DIR});
                return children;
            }
            String id = DocumentsContract.getDocumentId(uri);
            MatrixCursor metadata = new MatrixCursor(new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
            metadata.addRow(new Object[]{id.equals("opaque:lib# %") ? "Pokémon.GSFLIB" :
                    id.equals("mini:one/encoded%") ? "01 canción.minigsf" : "02.minigsf", null});
            return metadata;
        }
        if (uri.getPath().contains("metadata-denied")) throw new SecurityException("Metadata unavailable");
        MatrixCursor cursor = new MatrixCursor(new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
        cursor.addRow(new Object[]{name, null}); // deliberately unknown size
        return cursor;
    }
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        lastUri = uri.toString();
        if (uri.getPath().equals("/denied")) throw new SecurityException("Read permission denied by test provider");
        try {
            ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createReliablePipe();
            final boolean failRead = fail;
            new Thread(() -> {
                try {
                    if (failRead) { pipe[1].closeWithError("Simulated provider read failure"); return; }
                    try (OutputStream output = new ParcelFileDescriptor.AutoCloseOutputStream(pipe[1])) {
                        // Short writes; no regular file, length or seek support.
                        for (int offset = 0; offset < DATA.length; offset += 137)
                            output.write(DATA, offset, Math.min(137, DATA.length - offset));
                    }
                } catch (IOException error) { throw new RuntimeException(error); }
            }, "synthetic-document-writer").start();
            return pipe[0];
        } catch (IOException error) { throw new FileNotFoundException(error.toString()); }
    }
    public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException(); }
    public int delete(Uri uri, String selection, String[] args) { throw new UnsupportedOperationException(); }
    public int update(Uri uri, ContentValues values, String selection, String[] args) { throw new UnsupportedOperationException(); }
}
