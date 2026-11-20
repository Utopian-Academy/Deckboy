package io.github.utopian_academy.deckboy.pocket;

import android.app.PendingIntent;
import android.app.PictureInPictureParams;
import android.app.Presentation;
import android.app.RemoteAction;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.Configuration;
import android.graphics.drawable.Icon;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Rational;
import android.view.Display;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.util.ArrayList;

/**
 * SDL's activity, plus what a show needs from Android:
 *  - the show output: a full-screen Presentation on a second screen (USB-C to
 *    HDMI, a USB-C monitor, wireless display) whose surface the native side
 *    draws the program into, while the phone keeps the controls;
 *  - picture-in-picture: leaving Pocket with a cue live shrinks it to a
 *    floating program window with GO, BACK and BLACK on it, instead of going
 *    to the background, where Android would throttle it.
 */
public class PocketActivity extends SDLActivity {
    static final String ACTION_PIP = "io.github.utopian_academy.deckboy.pocket.PIP";
    static final int PIP_GO = 1, PIP_BACK = 2, PIP_BLACK = 3, PIP_PAUSE = 4;

    private DisplayManager displays;
    private OutputPresentation output;
    private final Handler main = new Handler(Looper.getMainLooper());

    /** The output screen's surface (null when it goes), its size, refresh and name. */
    static native void nativeSetOutputSurface(Object surface, int width, int height, float refreshHz, String name);
    /** Whether a cue is live, so leaving Pocket should keep it up in picture-in-picture. */
    static native boolean nativeWantsPip();
    /** Picture-in-picture began or ended. */
    static native void nativeSetPip(boolean on);
    /** A button in the picture-in-picture window or the notification: 1 GO, 2 BACK, 3 BLACK, 4 PAUSE. */
    static native void nativePipAction(int action);

    private static PocketActivity current;
    private static String showTitle = "";

    /**
     * From the native side, on any change: the live cue's name, or "" when
     * nothing is live. Starts, updates or stops the "show running" service.
     */
    static void setShowRunning(final String title) {
        final PocketActivity a = current;
        if (a == null) return;
        a.main.post(() -> {
            if (title.equals(showTitle)) return;
            showTitle = title;
            if (title.isEmpty()) ShowService.hide(a);
            else ShowService.show(a, title);
        });
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // A show must not be cut by the screen going to sleep.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // Steady clocks rather than bursts and throttling, where the phone
        // offers it: a show wants the same speed in the second hour as the first.
        try {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(Context.POWER_SERVICE);
            if (Build.VERSION.SDK_INT >= 24 && pm.isSustainedPerformanceModeSupported()) {
                getWindow().setSustainedPerformanceMode(true);
            }
        } catch (Exception ignored) {
        }
        current = this;
        // The "show running" notification needs this once, on Android 13 and later.
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {android.Manifest.permission.POST_NOTIFICATIONS}, 7);
        }
        displays = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        displays.registerDisplayListener(listener, main);
        IntentFilter filter = new IntentFilter(ACTION_PIP);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(pipReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(pipReceiver, filter);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateOutput();
    }

    @Override
    protected void onDestroy() {
        if (displays != null) displays.unregisterDisplayListener(listener);
        try { unregisterReceiver(pipReceiver); } catch (Exception ignored) {}
        ShowService.hide(this);
        showTitle = "";
        if (current == this) current = null;
        closeOutput();
        super.onDestroy();
    }

    // ── Picture-in-picture ──

    @Override
    protected void onUserLeaveHint() {
        super.onUserLeaveHint();
        if (mBrokenLibraries) return;
        if (nativeWantsPip()) {
            try {
                enterPictureInPictureMode(pipParams());
            } catch (Exception ignored) {
                // A phone or a setting that refuses picture-in-picture: Pocket
                // goes to the background as any app would.
            }
        }
    }

    @Override
    public void onPictureInPictureModeChanged(boolean inPip, Configuration newConfig) {
        super.onPictureInPictureModeChanged(inPip, newConfig);
        if (!mBrokenLibraries) nativeSetPip(inPip);
    }

    private PictureInPictureParams pipParams() {
        ArrayList<RemoteAction> actions = new ArrayList<>();
        actions.add(pipAction(PIP_BACK, android.R.drawable.ic_media_previous, "BACK"));
        actions.add(pipAction(PIP_GO, android.R.drawable.ic_media_play, "GO"));
        actions.add(pipAction(PIP_BLACK, android.R.drawable.ic_media_pause, "BLACK"));
        PictureInPictureParams.Builder b = new PictureInPictureParams.Builder()
                .setAspectRatio(new Rational(16, 9))
                .setActions(actions);
        if (Build.VERSION.SDK_INT >= 31) b.setSeamlessResizeEnabled(false);
        return b.build();
    }

    private RemoteAction pipAction(int code, int icon, String title) {
        Intent intent = new Intent(ACTION_PIP).setPackage(getPackageName()).putExtra("action", code);
        PendingIntent pi = PendingIntent.getBroadcast(this, code, intent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        return new RemoteAction(Icon.createWithResource(this, icon), title, title, pi);
    }

    private final BroadcastReceiver pipReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!mBrokenLibraries) nativePipAction(intent.getIntExtra("action", 0));
        }
    };

    // ── The show output ──

    private final DisplayManager.DisplayListener listener = new DisplayManager.DisplayListener() {
        @Override public void onDisplayAdded(int id) { updateOutput(); }
        @Override public void onDisplayRemoved(int id) { updateOutput(); }
        @Override public void onDisplayChanged(int id) { updateOutput(); }
    };

    /** Open the Presentation on the first presentation display, or close it when there is none. */
    private void updateOutput() {
        Display[] list = displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        Display want = list.length > 0 ? list[0] : null;
        if (output != null && (want == null || output.getDisplay().getDisplayId() != want.getDisplayId())) {
            closeOutput();
        }
        if (want != null && output == null && !isFinishing()) {
            output = new OutputPresentation(this, want);
            try {
                output.show();
            } catch (WindowManager.InvalidDisplayException e) {
                output = null;
            }
        }
    }

    private void closeOutput() {
        if (output != null) {
            output.dismiss();
            output = null;
        }
        if (mBrokenLibraries) return;
        nativeSetOutputSurface(null, 0, 0, 0f, "");
    }

    /** A plain full-screen surface on the output screen; the native side draws into it. */
    private static final class OutputPresentation extends Presentation implements SurfaceHolder.Callback {
        OutputPresentation(Context context, Display display) {
            super(context, display);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            SurfaceView view = new SurfaceView(getContext());
            view.getHolder().addCallback(this);
            setContentView(view);
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }

        @Override public void surfaceCreated(SurfaceHolder holder) {}

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            Display d = getDisplay();
            nativeSetOutputSurface(holder.getSurface(), width, height, d.getRefreshRate(), d.getName());
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            nativeSetOutputSurface(null, 0, 0, 0f, "");
        }
    }
}
