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

    /**
     * The phone's own name (Settings > About phone > Device name, "Galaxy S24
     * FE"), else its model: what a desk shows in its list of followers.
     */
    public static String deviceName(Context context, String unused) {
        try {
            String name = android.provider.Settings.Global.getString(context.getContentResolver(), "device_name");
            if (name != null && !name.isEmpty()) return name;
        } catch (Exception e) {
            // fall through to the model
        }
        return android.os.Build.MODEL != null ? android.os.Build.MODEL : "";
    }

    /**
     * A feel for each control, built from Android's haptic primitives where the
     * phone has them (Android 11+, most recent flagships) and from its preset
     * clicks where it does not:
     *   go     a rising swoop into a thud ("whoomp")
     *   back   a falling flick
     *   pad    a light tick
     *   black  two heavy clunks, like a shutter
     *   menu   a low tick
     *   spin   a spin (reverse, bounce, shuffle)
     *   lock   a click as a follower locks to its desk
     *   end    three quick ticks at the end of the list
     * Returns "" always (the C++ side calls this through its String bridge).
     */
    public static String haptic(Context context, String kind) {
        try {
            android.os.Vibrator v;
            if (android.os.Build.VERSION.SDK_INT >= 31) {
                v = ((android.os.VibratorManager) context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE)).getDefaultVibrator();
            } else {
                v = (android.os.Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
            }
            if (v == null || !v.hasVibrator()) return "";
            android.os.VibrationEffect effect = null;
            if (android.os.Build.VERSION.SDK_INT >= 30) effect = composed(v, kind);
            if (effect == null) effect = preset(kind);
            if (effect != null) v.vibrate(effect);
        } catch (Exception e) {
            // a phone with no motor, or one that refuses: no feel, no fault
        }
        return "";
    }

    @android.annotation.TargetApi(30)
    private static android.os.VibrationEffect composed(android.os.Vibrator v, String kind) {
        final int TICK = android.os.VibrationEffect.Composition.PRIMITIVE_TICK;
        final int CLICK = android.os.VibrationEffect.Composition.PRIMITIVE_CLICK;
        final int THUD = android.os.VibrationEffect.Composition.PRIMITIVE_THUD;
        final int SPIN = android.os.VibrationEffect.Composition.PRIMITIVE_SPIN;
        final int RISE = android.os.VibrationEffect.Composition.PRIMITIVE_QUICK_RISE;
        final int FALL = android.os.VibrationEffect.Composition.PRIMITIVE_QUICK_FALL;
        final int LOW = android.os.Build.VERSION.SDK_INT >= 31
                ? android.os.VibrationEffect.Composition.PRIMITIVE_LOW_TICK : TICK;
        android.os.VibrationEffect.Composition c = android.os.VibrationEffect.startComposition();
        int[] used;
        switch (kind) {
            case "go":
                used = new int[] {RISE, THUD};
                c.addPrimitive(RISE, 0.5f).addPrimitive(THUD, 1.0f, 15);
                break;
            case "back":
                used = new int[] {FALL};
                c.addPrimitive(FALL, 0.8f);
                break;
            case "pad":
                used = new int[] {TICK};
                c.addPrimitive(TICK, 0.7f);
                break;
            case "black":
                used = new int[] {CLICK};
                c.addPrimitive(CLICK, 1.0f).addPrimitive(CLICK, 1.0f, 70);
                break;
            case "menu":
                used = new int[] {LOW};
                c.addPrimitive(LOW, 0.9f);
                break;
            case "spin":
                used = new int[] {SPIN};
                c.addPrimitive(SPIN, 0.9f);
                break;
            case "lock":
                used = new int[] {RISE, CLICK};
                c.addPrimitive(RISE, 0.4f).addPrimitive(CLICK, 0.9f, 10);
                break;
            case "end":
                used = new int[] {TICK};
                c.addPrimitive(TICK, 1.0f).addPrimitive(TICK, 1.0f, 40).addPrimitive(TICK, 1.0f, 40);
                break;
            default:
                return null;
        }
        for (boolean ok : v.arePrimitivesSupported(used)) {
            if (!ok) return null;   // one missing primitive: use the preset instead
        }
        return c.compose();
    }

    private static android.os.VibrationEffect preset(String kind) {
        if (android.os.Build.VERSION.SDK_INT < 29) return null;
        switch (kind) {
            case "go": return android.os.VibrationEffect.createPredefined(android.os.VibrationEffect.EFFECT_HEAVY_CLICK);
            case "black": case "end": return android.os.VibrationEffect.createPredefined(android.os.VibrationEffect.EFFECT_DOUBLE_CLICK);
            case "pad": case "menu": return android.os.VibrationEffect.createPredefined(android.os.VibrationEffect.EFFECT_TICK);
            default: return android.os.VibrationEffect.createPredefined(android.os.VibrationEffect.EFFECT_CLICK);
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
