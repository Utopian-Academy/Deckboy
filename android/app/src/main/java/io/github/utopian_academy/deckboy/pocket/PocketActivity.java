package io.github.utopian_academy.deckboy.pocket;

import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * SDL's activity, plus the show output: when a second screen is connected
 * (USB-C to HDMI, a USB-C monitor, wireless display), Pocket opens a full-screen
 * Presentation on it and hands its surface to the native side, which draws the
 * program picture there. The phone keeps the controls.
 */
public class PocketActivity extends SDLActivity {
    private DisplayManager displays;
    private OutputPresentation output;
    private final Handler main = new Handler(Looper.getMainLooper());

    /** The output screen's surface (null when it goes), its size and refresh. */
    static native void nativeSetOutputSurface(Object surface, int width, int height, float refreshHz, String name);

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // A show must not be cut by the screen going to sleep.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        displays = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        displays.registerDisplayListener(listener, main);
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateOutput();
    }

    @Override
    protected void onDestroy() {
        if (displays != null) displays.unregisterDisplayListener(listener);
        closeOutput();
        super.onDestroy();
    }

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
