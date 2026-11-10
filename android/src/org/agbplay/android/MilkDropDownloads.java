package org.agbplay.android;

import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.URL;
import javax.net.ssl.HttpsURLConnection;

/** Platform TLS on a background native worker. No storage permissions required. */
public final class MilkDropDownloads {
    private static volatile boolean cancelled;
    private MilkDropDownloads() { }
    public static void cancel() { cancelled = true; }
    public static void prepare() { cancelled = false; }
    public static String download(String address, String destination, long limit) {
        HttpsURLConnection connection = null;
        try {
            URL url = new URL(address);
            if (!"https".equals(url.getProtocol()) || !"codeload.github.com".equals(url.getHost()))
                return "URL de descarga no autorizada";
            connection = (HttpsURLConnection) url.openConnection();
            connection.setConnectTimeout(10000);
            connection.setReadTimeout(5000);
            connection.setInstanceFollowRedirects(false);
            if (connection.getResponseCode() != 200) return "Respuesta HTTPS inesperada";
            long total = 0;
            byte[] buffer = new byte[65536];
            try (InputStream input = connection.getInputStream();
                 FileOutputStream output = new FileOutputStream(destination)) {
                int count;
                while ((count = input.read(buffer)) != -1) {
                    if (cancelled) return "Cancelado";
                    total += count;
                    if (total > limit) return "Descarga demasiado grande";
                    output.write(buffer, 0, count);
                }
            }
            return cancelled ? "Cancelado" : "";
        } catch (Exception error) {
            return "Error HTTPS: " + error.getClass().getSimpleName();
        } finally {
            if (connection != null) connection.disconnect();
        }
    }
}
