package io.github.utopian_academy.deckboy.pocket;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.os.IBinder;

/**
 * "Show running": a foreground service while a cue is live, so Android keeps
 * Pocket playing behind other apps, in picture-in-picture and with the screen
 * off, instead of freezing or killing it to save battery. Its notification
 * carries the show's name and BACK / GO / BLACK / PAUSE, which reach the
 * native side the same way the picture-in-picture buttons do.
 */
public class ShowService extends Service {
    static final String CHANNEL = "show";
    static final String EXTRA_TITLE = "title";
    private static final int ID = 7;

    /** Start, or update the text of, the running notification. */
    static void show(Context context, String title) {
        Intent i = new Intent(context, ShowService.class).putExtra(EXTRA_TITLE, title);
        try {
            if (Build.VERSION.SDK_INT >= 26) context.startForegroundService(i);
            else context.startService(i);
        } catch (Exception ignored) {
            // Android refuses a foreground start from the background on some
            // versions; the next update from the front tries again.
        }
    }

    static void hide(Context context) {
        context.stopService(new Intent(context, ShowService.class));
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String title = intent != null ? intent.getStringExtra(EXTRA_TITLE) : null;
        Notification n = build(title != null ? title : "Show running");
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        } else {
            startForeground(ID, n);
        }
        return START_NOT_STICKY;
    }

    private Notification build(String title) {
        NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26 && nm.getNotificationChannel(CHANNEL) == null) {
            NotificationChannel ch = new NotificationChannel(CHANNEL, "Show running", NotificationManager.IMPORTANCE_LOW);
            ch.setDescription("Shown while a cue is live, so the show keeps playing behind other apps.");
            ch.setShowBadge(false);
            nm.createNotificationChannel(ch);
        }
        Intent open = new Intent(this, PocketActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent openPi = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        b.setSmallIcon(android.R.drawable.ic_media_play)
         .setContentTitle("Deckboy Pocket")
         .setContentText(title)
         .setContentIntent(openPi)
         .setOngoing(true)
         .setCategory(Notification.CATEGORY_TRANSPORT)
         .setVisibility(Notification.VISIBILITY_PUBLIC)
         .addAction(action(PocketActivity.PIP_BACK, android.R.drawable.ic_media_previous, "BACK"))
         .addAction(action(PocketActivity.PIP_GO, android.R.drawable.ic_media_play, "GO"))
         .addAction(action(PocketActivity.PIP_BLACK, android.R.drawable.ic_media_pause, "BLACK"))
         .addAction(action(PocketActivity.PIP_PAUSE, android.R.drawable.ic_media_pause, "PAUSE"));
        if (Build.VERSION.SDK_INT >= 31) b.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        return b.build();
    }

    private Notification.Action action(int code, int icon, String title) {
        Intent intent = new Intent(PocketActivity.ACTION_PIP).setPackage(getPackageName()).putExtra("action", code);
        PendingIntent pi = PendingIntent.getBroadcast(this, 100 + code, intent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Action.Builder(Icon.createWithResource(this, icon), title, pi).build();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
