package com.ari.peggle.launcher;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.OverScroller;
import android.widget.Toast;

import java.io.File;

/**
 * The collection's launcher: a carousel of covers, one per game, with the title and year under the one in front.
 * Tapping the front cover starts that game and closes the launcher; tapping a side cover brings it to the front.
 *
 * Covers are the player's own images in the app's folder, Android/data/<package>/files/covers/<id>.png (or .jpg,
 * .jpeg, .webp), any size (shown centre-cropped to 3:4). A game without one gets a plain title card.
 */
public class LauncherActivity extends Activity {

    /** One game of the carousel: where it can be found, and (once found) how to start it. */
    static final class Game {
        final String id, title, year;
        final int color;  // the title card's colour
        final String peggleTarget;  // where Peggle Enhanced should open (sent as an extra; a build that knows it uses it)
        final String activity;  // an activity of this APK that runs the game (the all-in-one build), or null
        final String[] packages;  // installed apps that run the game, in order of preference
        Intent intent;  // how to start it here; null: not installed
        Bitmap cover;

        Game(String id, String title, String year, int color, String peggleTarget, String activity, String... packages) {
            this.id = id;
            this.title = title;
            this.year = year;
            this.color = color;
            this.peggleTarget = peggleTarget;
            this.activity = activity;
            this.packages = packages;
        }
    }

    // Peggle Enhanced (github.com/aidrum/peggle-enhanced): its HD and iPhone editions; peg2arm (this repository's port);
    // Peggle Blast (EA's app).
    private static final String PEGGLE_ENHANCED = "peggle.android.GameActivity";
    private static final String[] PEGGLE_ENHANCED_APPS = { "com.aidrum.peggle.hd", "com.aidrum.peggle" };
    private static final String PEGGLE_2 = "com.ari.peggle2.Peggle2Activity";
    private static final String PEG2ARM_APP = "com.ari.peggle2";
    private static final String PEGGLE_BLAST_APP = "com.ea.game.pegglex_row";

    static final Game[] ALL_GAMES = {
        new Game("peggle", "Peggle", "2007", 0xFF2F7DE1, "peggle", PEGGLE_ENHANCED, PEGGLE_ENHANCED_APPS),
        new Game("extreme", "Peggle Extreme", "2007", 0xFFE0662A, "extreme", PEGGLE_ENHANCED, PEGGLE_ENHANCED_APPS),
        new Game("nights", "Peggle Nights", "2008", 0xFF4B3A9E, "nights", PEGGLE_ENHANCED, PEGGLE_ENHANCED_APPS),
        new Game("dualshot", "Peggle Dual Shot", "2009", 0xFF5BA82E, "dualshot", PEGGLE_ENHANCED, PEGGLE_ENHANCED_APPS),
        new Game("wow", "Peggle: World of Warcraft Edition", "2010", 0xFFB8862B, "wow", PEGGLE_ENHANCED, PEGGLE_ENHANCED_APPS),
        new Game("peggle2", "Peggle 2", "2013", 0xFFD94A8C, null, PEGGLE_2, PEG2ARM_APP),
        new Game("blast", "Peggle Blast", "2014", 0xFF1FA4B8, null, null, PEGGLE_BLAST_APP),
    };

    /** The games found on this device, in carousel order. */
    Game[] GAMES = new Game[0];

    // How each game starts here: an activity of this APK if it has the game, else the first installed app.
    private void findGames() {
        java.util.ArrayList<Game> found = new java.util.ArrayList<Game>();
        PackageManager pm = getPackageManager();
        for (Game game : ALL_GAMES) {
            Intent intent = null;
            if (game.activity != null) {
                Intent own = new Intent().setClassName(this, game.activity);
                if (pm.resolveActivity(own, 0) != null) {
                    intent = own;
                }
            }
            for (int i = 0; intent == null && i < game.packages.length; i++) {
                intent = pm.getLaunchIntentForPackage(game.packages[i]);
            }
            if (intent == null) {
                continue;
            }
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            if (game.peggleTarget != null) {
                intent.putExtra("peggle_target", game.peggleTarget);
            }
            game.intent = intent;
            found.add(game);
        }
        GAMES = found.toArray(new Game[0]);
    }

    private CarouselView carousel;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        findGames();
        File covers = new File(getExternalFilesDir(null), "covers");
        covers.mkdirs();
        loadCovers(covers);
        if (GAMES.length == 0) {
            Toast.makeText(this, "No Peggle games are installed (peg2arm, Peggle Enhanced, Peggle Blast)",
                    Toast.LENGTH_LONG).show();
        }
        if (skipped.length() > 0) {
            Toast.makeText(this, "Images not used:\n" + skipped, Toast.LENGTH_LONG).show();
        }
        carousel = new CarouselView(this);
        setContentView(carousel);
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    Bitmap background;

    // The player's cover images, decoded no larger than needed for the screen, with the black canvas around the box
    // made transparent and trimmed off (CutOutBox). The result is cached per image (by size and date).
    private void loadCovers(File dir) {
        final int maxHeight = Math.max(getResources().getDisplayMetrics().heightPixels, 720);
        File cache = new File(getCacheDir(), "covers");
        cache.mkdirs();
        for (Game game : GAMES) {
            File file = find(dir, game.id);
            if (file == null) {
                continue;
            }
            File cached = new File(cache, game.id + "_" + file.length() + "_" + file.lastModified() + ".png");
            if (cached.isFile()) {
                game.cover = BitmapFactory.decodeFile(cached.getAbsolutePath());
            }
            if (game.cover == null) {
                Bitmap raw = decode(file, maxHeight);
                if (raw != null) {
                    game.cover = cutOutBox(raw);
                    try (java.io.FileOutputStream out = new java.io.FileOutputStream(cached)) {
                        game.cover.compress(Bitmap.CompressFormat.PNG, 100, out);
                    } catch (java.io.IOException e) {
                        // Shown anyway; cut out again next time.
                    }
                }
            }
        }
        File back = find(dir, "background");
        if (back != null) {
            background = decode(back, maxHeight);
        }
    }

    private static File find(File dir, String name) {
        for (String ext : new String[] { ".png", ".jpg", ".jpeg", ".webp" }) {
            File file = new File(dir, name + ext);
            if (file.isFile()) {
                return file;
            }
        }
        return null;
    }

    // The limits on the player's images (launcher/README.md): larger or odder ones are skipped, and listed.
    static final long MAX_FILE_BYTES = 25L << 20;
    static final int MIN_SIDE = 256, MAX_SIDE = 8192;
    static final float MAX_ASPECT = 3f;  // long side / short side
    final StringBuilder skipped = new StringBuilder();

    private Bitmap decode(File file, int maxHeight) {
        if (file.length() > MAX_FILE_BYTES) {
            skipped.append(file.getName()).append(": larger than 25 MB\n");
            return null;
        }
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(file.getAbsolutePath(), bounds);
        int w = bounds.outWidth, h = bounds.outHeight;
        if (w <= 0 || h <= 0) {
            skipped.append(file.getName()).append(": not a PNG, JPEG or WebP image\n");
            return null;
        }
        if (Math.min(w, h) < MIN_SIDE || Math.max(w, h) > MAX_SIDE) {
            skipped.append(file.getName()).append(": ").append(w).append("x").append(h)
                    .append(" (each side 256 to 8192 pixels)\n");
            return null;
        }
        if ((float) Math.max(w, h) / Math.min(w, h) > MAX_ASPECT) {
            skipped.append(file.getName()).append(": ").append(w).append("x").append(h)
                    .append(" (no more than 3 times as long as wide)\n");
            return null;
        }
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = 1;
        while (bounds.outHeight / (options.inSampleSize * 2) >= maxHeight) {
            options.inSampleSize *= 2;
        }
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        options.inMutable = true;
        return BitmapFactory.decodeFile(file.getAbsolutePath(), options);
    }

    // The near-black canvas a box scan sits on (reached from the image's edges, so black inside the art stays),
    // made transparent, with a soft edge, and the image trimmed to the box.
    private static Bitmap cutOutBox(Bitmap src) {
        final int w = src.getWidth(), h = src.getHeight();
        int[] px = new int[w * h];
        src.getPixels(px, 0, w, 0, 0, w, h);
        final int dark = 30;
        boolean[] cut = new boolean[w * h];
        int[] queue = new int[w * h];
        int head = 0, tail = 0;
        for (int x = 0; x < w; x++) {
            tail = seed(px, cut, queue, tail, x, dark);
            tail = seed(px, cut, queue, tail, (h - 1) * w + x, dark);
        }
        for (int y = 0; y < h; y++) {
            tail = seed(px, cut, queue, tail, y * w, dark);
            tail = seed(px, cut, queue, tail, y * w + w - 1, dark);
        }
        while (head < tail) {
            int i = queue[head++];
            int x = i % w, y = i / w;
            if (x > 0) tail = seed(px, cut, queue, tail, i - 1, dark);
            if (x < w - 1) tail = seed(px, cut, queue, tail, i + 1, dark);
            if (y > 0) tail = seed(px, cut, queue, tail, i - w, dark);
            if (y < h - 1) tail = seed(px, cut, queue, tail, i + w, dark);
        }
        int left = w, right = -1, top = h, bottom = -1;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int i = y * w + x;
                if (cut[i]) {
                    px[i] = 0;
                    continue;
                }
                // A box pixel next to the cut-away canvas: half transparent, so the edge is not jagged.
                boolean edge = (x > 0 && cut[i - 1]) || (x < w - 1 && cut[i + 1]) || (y > 0 && cut[i - w])
                        || (y < h - 1 && cut[i + w]);
                if (edge) {
                    px[i] = (px[i] & 0x00FFFFFF) | 0x80000000;
                }
                if (x < left) left = x;
                if (x > right) right = x;
                if (y < top) top = y;
                if (y > bottom) bottom = y;
            }
        }
        if (right < left || bottom < top) {
            return src;  // all dark: leave it as it is
        }
        Bitmap out = Bitmap.createBitmap(right - left + 1, bottom - top + 1, Bitmap.Config.ARGB_8888);
        out.setPixels(px, top * w + left, w, 0, 0, out.getWidth(), out.getHeight());
        return out;
    }

    private static int seed(int[] px, boolean[] cut, int[] queue, int tail, int i, int dark) {
        if (cut[i]) {
            return tail;
        }
        int c = px[i];
        int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        if (Math.max(r, Math.max(g, b)) > dark) {
            return tail;
        }
        cut[i] = true;
        queue[tail] = i;
        return tail + 1;
    }

    void launch(Game game) {
        try {
            startActivity(game.intent);
        } catch (ActivityNotFoundException e) {
            Toast.makeText(this, "Cannot start " + game.title, Toast.LENGTH_LONG).show();
            return;
        }
        finishAndRemoveTask();
    }

    /** The covers in a row: the front one large, its neighbours smaller and dimmer; swipe or fling to move. */
    final class CarouselView extends View {
        private final OverScroller scroller;
        private final GestureDetector gestures;
        private final Paint coverPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        private final Paint cardPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint shadePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint backPaint = new Paint();
        private final Paint picturePaint = new Paint(Paint.FILTER_BITMAP_FLAG);
        private final Rect pictureSrc = new Rect();
        private final RectF pictureDst = new RectF();
        private final TextPaint titlePaint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final TextPaint yearPaint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final TextPaint cardTitlePaint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final TextPaint cardYearPaint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final RectF rect = new RectF();
        private final Rect src = new Rect();
        private float position;  // the carousel's position, in covers (0 = the first in front)
        private float spacing = 1;  // pixels between cover centres

        CarouselView(Context context) {
            super(context);
            scroller = new OverScroller(context);
            final float density = getResources().getDisplayMetrics().density;
            titlePaint.setColor(Color.WHITE);
            titlePaint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
            titlePaint.setTextAlign(Paint.Align.CENTER);
            titlePaint.setTextSize(26 * density);
            yearPaint.setColor(0xB3FFFFFF);
            yearPaint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL));
            yearPaint.setTextAlign(Paint.Align.CENTER);
            yearPaint.setTextSize(18 * density);
            cardTitlePaint.setColor(Color.WHITE);
            cardTitlePaint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
            cardYearPaint.setColor(0xCCFFFFFF);
            cardYearPaint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL));
            cardYearPaint.setTextAlign(Paint.Align.CENTER);
            gestures = new GestureDetector(context, new GestureDetector.SimpleOnGestureListener() {
                @Override
                public boolean onDown(MotionEvent e) {
                    scroller.forceFinished(true);
                    return true;
                }

                @Override
                public boolean onScroll(MotionEvent e1, MotionEvent e2, float dx, float dy) {
                    position = clamp(position + dx / spacing, -0.4f, GAMES.length - 0.6f);
                    invalidate();
                    return true;
                }

                @Override
                public boolean onFling(MotionEvent e1, MotionEvent e2, float vx, float vy) {
                    // Land on a whole cover: where the fling would stop, rounded.
                    float travel = -vx / spacing * 0.35f;
                    int target = Math.round(clamp(position + travel, 0, GAMES.length - 1));
                    snapTo(target, 420);
                    return true;
                }

                @Override
                public boolean onSingleTapUp(MotionEvent e) {
                    int front = Math.round(position);
                    int tapped = coverAt(e.getX(), e.getY());
                    if (tapped < 0) {
                        return true;
                    }
                    if (tapped == front && Math.abs(position - front) < 0.05f) {
                        launch(GAMES[front]);
                    } else {
                        snapTo(tapped, 300);
                    }
                    return true;
                }
            });
            setFocusable(true);
            // The background picture: 25 % less colour, 35 % see-through over the dark gradient, so the covers
            // stand out.
            android.graphics.ColorMatrix desaturate = new android.graphics.ColorMatrix();
            desaturate.setSaturation(0.75f);
            picturePaint.setColorFilter(new android.graphics.ColorMatrixColorFilter(desaturate));
            picturePaint.setAlpha(Math.round(255 * 0.65f));
        }

        private float clamp(float v, float lo, float hi) {
            return Math.max(lo, Math.min(hi, v));
        }

        private void snapTo(int index, int ms) {
            int from = Math.round(position * 1000);
            scroller.startScroll(from, 0, index * 1000 - from, 0, ms);
            postInvalidateOnAnimation();
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            boolean handled = gestures.onTouchEvent(event);
            if (event.getActionMasked() == MotionEvent.ACTION_UP && scroller.isFinished()) {
                int target = Math.round(clamp(position, 0, GAMES.length - 1));
                if (Math.abs(target - position) > 0.001f) {
                    snapTo(target, 220);
                }
            }
            return handled || super.onTouchEvent(event);
        }

        @Override
        public void computeScroll() {
            if (scroller.computeScrollOffset()) {
                position = scroller.getCurrX() / 1000f;
                postInvalidateOnAnimation();
            }
        }

        // Cover size and placement for the current view size.
        private float coverHeight() {
            return getHeight() * 0.62f;
        }

        private float coverCenterY() {
            return getHeight() * 0.40f;
        }

        // The cover at index i: its rectangle for the current position (scale falls off to the sides).
        private float layout(int i, RectF out) {
            float offset = i - position;
            float scale = 1f - Math.min(Math.abs(offset), 2.5f) * 0.16f;
            float h = coverHeight() * scale, w = h * 0.75f;
            float cx = getWidth() / 2f + offset * spacing;
            float cy = coverCenterY();
            out.set(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
            return scale;
        }

        private int coverAt(float x, float y) {
            // Front-most first: nearest the centre.
            int best = -1;
            float bestDistance = Float.MAX_VALUE;
            for (int i = 0; i < GAMES.length; i++) {
                layout(i, rect);
                if (rect.contains(x, y)) {
                    float d = Math.abs(i - position);
                    if (d < bestDistance) {
                        bestDistance = d;
                        best = i;
                    }
                }
            }
            return best;
        }

        @Override
        protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            spacing = h * 0.62f * 0.75f * 0.95f;
            backPaint.setShader(new LinearGradient(0, 0, 0, h, 0xFF101828, 0xFF05070D, Shader.TileMode.CLAMP));
        }

        @Override
        protected void onDraw(Canvas canvas) {
            canvas.drawRect(0, 0, getWidth(), getHeight(), backPaint);
            if (background != null) {
                // Centre-cropped to fill the screen.
                int bw = background.getWidth(), bh = background.getHeight();
                float target = (float) getWidth() / getHeight();
                if ((float) bw / bh > target) {
                    int cw = Math.round(bh * target);
                    pictureSrc.set((bw - cw) / 2, 0, (bw - cw) / 2 + cw, bh);
                } else {
                    int ch = Math.round(bw / target);
                    pictureSrc.set(0, (bh - ch) / 2, bw, (bh - ch) / 2 + ch);
                }
                pictureDst.set(0, 0, getWidth(), getHeight());
                canvas.drawBitmap(background, pictureSrc, pictureDst, picturePaint);
            }
            // Back to front: the farthest covers first, the front one last.
            Integer[] order = new Integer[GAMES.length];
            for (int i = 0; i < order.length; i++) {
                order[i] = i;
            }
            java.util.Arrays.sort(order, new java.util.Comparator<Integer>() {
                @Override
                public int compare(Integer a, Integer b) {
                    return Float.compare(Math.abs(b - position), Math.abs(a - position));
                }
            });
            final float radius = getResources().getDisplayMetrics().density * 10;
            for (int i : order) {
                float scale = layout(i, rect);
                if (rect.right < 0 || rect.left > getWidth()) {
                    continue;
                }
                float dim = Math.min(Math.abs(i - position), 1.5f) / 1.5f;
                drawCover(canvas, GAMES[i], rect, radius * scale, dim);
                if (dim > 0.01f && GAMES[i].cover == null) {
                    shadePaint.setColor(Color.argb(Math.round(dim * 150), 0, 0, 0));
                    canvas.drawRoundRect(rect, radius * scale, radius * scale, shadePaint);
                }
            }
            // Title and year of the cover in front.
            int front = Math.round(clamp(position, 0, GAMES.length - 1));
            float alpha = 1f - Math.min(Math.abs(position - front) * 2.5f, 1f);
            titlePaint.setAlpha(Math.round(255 * alpha));
            yearPaint.setAlpha(Math.round(0xB3 * alpha));
            float textTop = coverCenterY() + coverHeight() / 2 + titlePaint.getTextSize() * 1.5f;
            canvas.drawText(GAMES[front].title, getWidth() / 2f, textTop, titlePaint);
            canvas.drawText(GAMES[front].year, getWidth() / 2f, textTop + yearPaint.getTextSize() * 1.4f, yearPaint);
        }

        private final RectF fitted = new RectF();

        private void drawCover(Canvas canvas, Game game, RectF r, float radius, float dim) {
            if (game.cover != null) {
                // The whole box, fitted in its place and standing on its bottom edge; the canvas around it is
                // transparent (cutOutBox), so no fill shows. Side covers are darkened through a colour filter,
                // which leaves the transparent parts alone.
                Bitmap b = game.cover;
                float scale = Math.min(r.width() / b.getWidth(), r.height() / b.getHeight());
                float w = b.getWidth() * scale, h = b.getHeight() * scale;
                fitted.set(r.centerX() - w / 2, r.bottom - h, r.centerX() + w / 2, r.bottom);
                int m = Math.round(255 * (1f - 0.6f * dim));
                coverPaint.setColorFilter(dim > 0.01f
                        ? new android.graphics.LightingColorFilter(Color.rgb(m, m, m), 0) : null);
                canvas.drawBitmap(b, null, fitted, coverPaint);
                return;
            }
            // Title card: the game's colour, its name and year.
            int top = game.color, bottom = blend(game.color, 0xFF000000, 0.45f);
            cardPaint.setShader(new LinearGradient(0, r.top, 0, r.bottom, top, bottom, Shader.TileMode.CLAMP));
            canvas.drawRoundRect(r, radius, radius, cardPaint);
            cardPaint.setShader(null);
            float pad = r.width() * 0.1f;
            cardTitlePaint.setTextSize(r.width() * 0.13f);
            cardYearPaint.setTextSize(r.width() * 0.08f);
            StaticLayout layout = StaticLayout.Builder.obtain(game.title, 0, game.title.length(), cardTitlePaint,
                    Math.max(1, Math.round(r.width() - 2 * pad))).setAlignment(Layout.Alignment.ALIGN_CENTER).build();
            canvas.save();
            canvas.translate(r.left + pad, r.centerY() - layout.getHeight() / 2f - cardYearPaint.getTextSize() * 0.6f);
            layout.draw(canvas);
            canvas.restore();
            canvas.drawText(game.year, r.centerX(), r.centerY() + layout.getHeight() / 2f + cardYearPaint.getTextSize(),
                    cardYearPaint);
        }

        private int blend(int a, int b, float t) {
            int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
            int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
            return 0xFF000000 | (Math.round(ar + (br - ar) * t) << 16) | (Math.round(ag + (bg - ag) * t) << 8)
                    | Math.round(ab + (bb - ab) * t);
        }
    }
}
