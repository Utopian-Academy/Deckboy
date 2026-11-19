package io.github.utopian_academy.deckboy.pocket;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.pdf.PdfRenderer;
import android.net.Uri;
import android.os.BatteryManager;
import android.os.PowerManager;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;

/**
 * What Pocket's C++ needs from Android's Java side: the name and type of a file
 * the system picker handed over, keeping access to it across launches, and PDF
 * pages rendered by Android's own PDF engine (the same "the platform's engine,
 * nothing bundled" rule the desk follows for slide decks).
 */
public final class PocketBridge {
    private static final String TAG = "DeckboyPocket";

    private PocketBridge() {}

    /** The file's own name ("Keynote.pdf"), or "" when the provider will not say. */
    public static String displayName(Context context, String uriText) {
        try (Cursor c = context.getContentResolver().query(Uri.parse(uriText),
                new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                String name = c.getString(0);
                return name != null ? name : "";
            }
        } catch (Exception e) {
            Log.w(TAG, "no name for " + uriText + ": " + e);
        }
        return "";
    }

    /** The provider's MIME type ("video/mp4", "application/pdf"), or "". */
    public static String mimeType(Context context, String uriText) {
        try {
            String type = context.getContentResolver().getType(Uri.parse(uriText));
            return type != null ? type : "";
        } catch (Exception e) {
            return "";
        }
    }

    /** Keep read access after Pocket closes, so a show can find the file again. */
    public static boolean keepAccess(Context context, String uriText) {
        try {
            context.getContentResolver().takePersistableUriPermission(Uri.parse(uriText),
                    Intent.FLAG_GRANT_READ_URI_PERMISSION);
            return true;
        } catch (Exception e) {
            // Not every provider offers lasting access; the file still plays now.
            return false;
        }
    }

    /** Battery percent, or -1; while charging, 1000 + percent. */
    public static int battery(Context context) {
        try {
            BatteryManager bm = (BatteryManager) context.getSystemService(Context.BATTERY_SERVICE);
            int pct = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY);
            return bm.isCharging() ? 1000 + pct : pct;
        } catch (Exception e) {
            return -1;
        }
    }

    /**
     * Android's own heat level: 0 none, 1 light, 2 moderate, 3 severe (the
     * phone is slowing itself down), 4 critical, 5 emergency, 6 shutdown.
     */
    public static int thermal(Context context) {
        try {
            PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
            return pm.getCurrentThermalStatus();
        } catch (Exception e) {
            return 0;
        }
    }

    /**
     * Render every page of a PDF to PNG files page-001.png ... in outDir, each
     * targetWidth pixels wide on white (a slide's paper). Returns the page
     * count, or -1 when the PDF cannot be opened (encrypted, damaged).
     */
    public static int renderPdf(Context context, String uriText, String outDir, int targetWidth) {
        File dir = new File(outDir);
        if (!dir.isDirectory() && !dir.mkdirs()) return -1;
        ContentResolver resolver = context.getContentResolver();
        try (ParcelFileDescriptor fd = uriText.startsWith("content:")
                     ? resolver.openFileDescriptor(Uri.parse(uriText), "r")
                     : ParcelFileDescriptor.open(new File(uriText), ParcelFileDescriptor.MODE_READ_ONLY);
             PdfRenderer pdf = new PdfRenderer(fd)) {
            int pages = pdf.getPageCount();
            for (int i = 0; i < pages; ++i) {
                try (PdfRenderer.Page page = pdf.openPage(i)) {
                    int w = Math.max(16, targetWidth);
                    int h = Math.max(16, Math.round((float) w * page.getHeight() / Math.max(1, page.getWidth())));
                    Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
                    bmp.eraseColor(Color.WHITE);
                    page.render(bmp, null, null, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY);
                    File out = new File(dir, String.format("page-%03d.png", i + 1));
                    try (FileOutputStream os = new FileOutputStream(out)) {
                        bmp.compress(Bitmap.CompressFormat.PNG, 100, os);
                    }
                    bmp.recycle();
                }
            }
            return pages;
        } catch (Exception e) {
            Log.w(TAG, "cannot render " + uriText + ": " + e);
            return -1;
        }
    }
}
