package org.agbplay.android;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.Log;

/** Keep Qt's SAF picker; repair only its optional initial-location hint. */
public final class DocumentPickerIntent {
    private DocumentPickerIntent() {}
    public static void normalize(Intent intent) {
        String action = intent.getAction();
        if (!(Intent.ACTION_OPEN_DOCUMENT.equals(action) || Intent.ACTION_OPEN_DOCUMENT_TREE.equals(action)
                || Intent.ACTION_CREATE_DOCUMENT.equals(action))) return;
        Bundle extras = intent.getExtras();
        Object initial = extras == null ? null : extras.get(DocumentsContract.EXTRA_INITIAL_URI);
        if (initial == null) return;
        // Qt 6.11.2 puts this extra as String. Android specifies a Parcelable
        // Uri. Local import/cache paths are not provider document locations.
        Uri uri = initial instanceof Uri ? (Uri)initial : initial instanceof String ? Uri.parse((String)initial) : null;
        if (uri != null && "content".equals(uri.getScheme())) {
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, uri);
        } else {
            intent.removeExtra(DocumentsContract.EXTRA_INITIAL_URI);
        }
        Log.i("agbplay.lifecycle", "SAF initial-location hint checked (" + initial.getClass().getSimpleName() + ")");
    }
}
