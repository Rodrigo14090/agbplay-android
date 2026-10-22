package org.agbplay.android;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;

/** Only the SAF grant is used. No real-path lookup or storage permission is needed. */
public final class DocumentImporter {
    private DocumentImporter() {}

    public static String importDocument(Context context, String document, String directory) throws IOException {
        Uri uri = Uri.parse(document); // Preserve escaped document IDs; never rebuild the path.
        if (!"content".equals(uri.getScheme())) throw new IOException("Expected a content URI");
        ContentResolver resolver = context.getContentResolver();
        File root = new File(directory);
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Cannot create private import directory");
        // Keep the actual filename for GSF _lib associations and playlist order.
        // URI-specific directories prevent equally named documents colliding.
        File documentRoot = new File(root, digest(document));
        if (!documentRoot.isDirectory() && !documentRoot.mkdirs())
            throw new IOException("Cannot create document import directory");
        String name = displayName(resolver, uri).replace('\\', '_').replace('/', '_')
                .replaceAll("[\\p{Cntrl}]", "_");
        if (name.isEmpty() || name.equals(".") || name.equals("..") || name.length() > 180)
            name = "document" + suffix(name);
        if (suffix(name).equals(".bin") && !name.toLowerCase(Locale.ROOT).endsWith(".bin")) name += ".bin";
        File destination = new File(documentRoot, name);
        File temporary = File.createTempFile("pending-", ".tmp", documentRoot);
        try {
            // Providers may return a pipe with unknown size. Read the stream to EOF.
            // Keep SecurityException / FileNotFoundException details for the UI.
            try (InputStream input = resolver.openInputStream(uri)) {
                if (input == null) throw new IOException("Document provider returned no input stream");
                try (FileOutputStream output = new FileOutputStream(temporary)) {
                    DocumentCopy.copy(input, output, DocumentCopy.LIMIT);
                    output.getFD().sync();
                }
            }
            // Same app-private filesystem; publish only a complete successful copy.
            Files.move(temporary.toPath(), destination.toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            return destination.getAbsolutePath();
        } finally {
            // Failed reads never replace an existing import or leave partial files.
            Files.deleteIfExists(temporary.toPath());
            String[] remaining = documentRoot.list();
            if (remaining != null && remaining.length == 0) documentRoot.delete();
        }
    }

    private static String displayName(ContentResolver resolver, Uri uri) {
        try (Cursor cursor = resolver.query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0 && !cursor.isNull(column)) return cursor.getString(column);
            }
        } catch (RuntimeException ignored) {
            // Metadata is optional. A failed query must not prevent reading the grant.
        }
        String last = uri.getLastPathSegment();
        return last == null ? "" : last;
    }

    private static String suffix(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        for (String ext : new String[]{".gba", ".gsflib", ".gsf", ".minigsf", ".zip"})
            if (lower.endsWith(ext)) return ext;
        return ".bin"; // The original ROM reader also identifies ZIP/GSF/raw by content.
    }

    private static String digest(String document) throws IOException {
        try {
            byte[] hash = MessageDigest.getInstance("SHA-256").digest(document.getBytes(StandardCharsets.UTF_8));
            StringBuilder result = new StringBuilder(64);
            for (byte value : hash) result.append(String.format(Locale.ROOT, "%02x", value & 255));
            return result.toString();
        } catch (NoSuchAlgorithmException error) {
            throw new IOException("SHA-256 unavailable", error);
        }
    }
}
