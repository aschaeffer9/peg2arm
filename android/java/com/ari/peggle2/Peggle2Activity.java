package com.ari.peggle2;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.Bundle;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.WindowManager;
import android.widget.FrameLayout;

import org.libsdl.app.SDLActivity;

/**
 * Peggle 2's activity: SDL3's, with SDL linked statically into libmain.so (so only that library is loaded), kept in
 * landscape and awake while the game runs.
 *
 * Two small buttons sit in the black bar left of the picture (the game is shown whole, 16:9, on a wider screen): pause
 * (the game's own pause screen, which leads to its main menu and its Exit) and sound on / off (remembered). The back
 * button / gesture pauses in a level and goes back one screen in menus. The game side of each is in
 * app/src/peggle2_touch.cpp (nativeMenu).
 */
public class Peggle2Activity extends SDLActivity {
    // nativeMenu commands (peggle2_touch.cpp).
    private static final int MENU_LEFT_APP = 0, MENU_MUTE = 4, MENU_UNMUTE = 5, MENU_PAUSE = 6, MENU_BACK = 7;

    private static native void nativeMenu(int command);

    private boolean muted;
    private IconButton pauseButton, soundButton;

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        muted = prefs().getBoolean("muted", false);
        sendMenu(muted ? MENU_MUTE : MENU_UNMUTE);
        addButtons();
    }

    private SharedPreferences prefs() {
        return getSharedPreferences("peggle2", Context.MODE_PRIVATE);
    }

    private static void sendMenu(int command) {
        try {
            nativeMenu(command);
        } catch (UnsatisfiedLinkError e) {
            // The library did not load (SDL shows its own error then).
        }
    }

    // Back (button or gesture): the game's pause screen in a level, one screen back in menus. Never closes the app.
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP && !event.isCanceled()) {
                sendMenu(MENU_BACK);
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public void onBackPressed() {
        sendMenu(MENU_BACK);
    }

    // Leaving the app pauses a level (the game's own pause screen), so nothing plays on unseen.
    @Override
    protected void onPause() {
        sendMenu(MENU_LEFT_APP);
        super.onPause();
    }

    private void addButtons() {
        pauseButton = new IconButton(this, IconButton.PAUSE);
        soundButton = new IconButton(this, muted ? IconButton.SOUND_OFF : IconButton.SOUND_ON);
        pauseButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                sendMenu(MENU_PAUSE);
            }
        });
        soundButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                muted = !muted;
                prefs().edit().putBoolean("muted", muted).apply();
                sendMenu(muted ? MENU_MUTE : MENU_UNMUTE);
                soundButton.setIcon(muted ? IconButton.SOUND_OFF : IconButton.SOUND_ON);
            }
        });
        addContentView(pauseButton, new FrameLayout.LayoutParams(1, 1, Gravity.TOP | Gravity.LEFT));
        addContentView(soundButton, new FrameLayout.LayoutParams(1, 1, Gravity.TOP | Gravity.LEFT));
        final View root = getWindow().getDecorView();
        root.getViewTreeObserver().addOnGlobalLayoutListener(new ViewTreeObserver.OnGlobalLayoutListener() {
            @Override
            public void onGlobalLayout() {
                placeButtons(root.getWidth(), root.getHeight());
            }
        });
    }

    // Centered in the left bar (the screen's width beyond the 16:9 picture, halved), one above the other at the top;
    // on a 16:9 or narrower screen, over the picture's top-left corner.
    private int placedWidth = -1, placedHeight = -1;

    private void placeButtons(int width, int height) {
        if (width <= 0 || height <= 0 || (width == placedWidth && height == placedHeight)) {
            return;
        }
        placedWidth = width;
        placedHeight = height;
        final float density = getResources().getDisplayMetrics().density;
        final int bar = Math.max(0, (width - height * 16 / 9) / 2);
        final int size = Math.round(Math.min(56 * density, Math.max(40 * density, bar * 0.6f)));
        final int left = bar >= size ? (bar - size) / 2 : Math.round(8 * density);
        final int top = Math.round(16 * density);
        final int gap = Math.round(12 * density);
        place(pauseButton, left, top, size);
        place(soundButton, left, top + size + gap, size);
    }

    private static void place(View view, int left, int top, int size) {
        FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) view.getLayoutParams();
        params.width = size;
        params.height = size;
        params.leftMargin = left;
        params.topMargin = top;
        view.setLayoutParams(params);
    }

    /** A round, translucent button with a drawn icon (no image assets): pause bars, or a speaker on / off. */
    private static final class IconButton extends View {
        static final int PAUSE = 0, SOUND_ON = 1, SOUND_OFF = 2;

        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint ink = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();
        private final RectF rect = new RectF();
        private int icon;
        private boolean pressed;

        IconButton(Context context, int icon) {
            super(context);
            this.icon = icon;
            fill.setStyle(Paint.Style.FILL);
            ink.setStyle(Paint.Style.FILL);
            ink.setColor(0xFFFFFFFF);
            stroke.setStyle(Paint.Style.STROKE);
            stroke.setColor(0xFFFFFFFF);
            stroke.setStrokeCap(Paint.Cap.ROUND);
            setClickable(true);
            setContentDescription(icon == PAUSE ? "Pause" : "Sound");
        }

        void setIcon(int icon) {
            this.icon = icon;
            invalidate();
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    pressed = true;
                    invalidate();
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    pressed = false;
                    invalidate();
                    break;
            }
            return super.onTouchEvent(event);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            final float w = getWidth(), h = getHeight(), r = Math.min(w, h) / 2;
            final float cx = w / 2, cy = h / 2;
            fill.setColor(pressed ? 0xB0FFFFFF : 0x59FFFFFF);
            canvas.drawCircle(cx, cy, r, fill);
            fill.setColor(pressed ? 0xCC1E2A4A : 0xB3141A2E);
            canvas.drawCircle(cx, cy, r - Math.max(2f, r * 0.07f), fill);
            final float u = r * 0.18f;  // icon unit
            if (icon == PAUSE) {
                rect.set(cx - 1.9f * u, cy - 2.2f * u, cx - 0.6f * u, cy + 2.2f * u);
                canvas.drawRoundRect(rect, u * 0.3f, u * 0.3f, ink);
                rect.set(cx + 0.6f * u, cy - 2.2f * u, cx + 1.9f * u, cy + 2.2f * u);
                canvas.drawRoundRect(rect, u * 0.3f, u * 0.3f, ink);
                return;
            }
            // Speaker: a box and a cone, a little left of center.
            final float sx = cx - 1.2f * u;
            path.reset();
            path.moveTo(sx - 1.6f * u, cy - 0.8f * u);
            path.lineTo(sx - 0.5f * u, cy - 0.8f * u);
            path.lineTo(sx + 0.9f * u, cy - 2.1f * u);
            path.lineTo(sx + 0.9f * u, cy + 2.1f * u);
            path.lineTo(sx - 0.5f * u, cy + 0.8f * u);
            path.lineTo(sx - 1.6f * u, cy + 0.8f * u);
            path.close();
            canvas.drawPath(path, ink);
            stroke.setStrokeWidth(Math.max(2f, 0.42f * u));
            if (icon == SOUND_ON) {
                rect.set(sx + 0.2f * u, cy - 1.5f * u, sx + 2.6f * u, cy + 1.5f * u);
                canvas.drawArc(rect, -50, 100, false, stroke);
                rect.set(sx - 0.4f * u, cy - 2.6f * u, sx + 3.8f * u, cy + 2.6f * u);
                canvas.drawArc(rect, -50, 100, false, stroke);
            } else {
                final float x0 = sx + 1.8f * u, x1 = sx + 3.6f * u;
                canvas.drawLine(x0, cy - 0.9f * u, x1, cy + 0.9f * u, stroke);
                canvas.drawLine(x0, cy + 0.9f * u, x1, cy - 0.9f * u, stroke);
            }
        }
    }
}
