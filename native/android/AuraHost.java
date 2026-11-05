package com.noxlang.aura;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Matrix;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Bundle;
import android.os.Build;
import android.util.Log;
import android.text.InputType;
import android.text.Editable;
import android.text.Selection;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.view.Choreographer;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.content.res.Configuration;
import android.graphics.Insets;
import android.provider.Settings;
import android.view.Display;
import android.view.DisplayCutout;
import android.view.Surface;
import android.view.WindowInsets;
import android.view.WindowInsetsAnimation;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.CursorAnchorInfo;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.window.BackEvent;
import android.window.OnBackAnimationCallback;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;

public final class AuraHost extends Activity implements Choreographer.FrameCallback {
    private static final class PendingTextEdit {
        final int operation;
        final String text;
        final int first;
        final int second;
        final long timestampNs;

        PendingTextEdit(int operation, String text, int first, int second, long timestampNs) {
            this.operation = operation;
            this.text = text;
            this.first = first;
            this.second = second;
            this.timestampNs = timestampNs;
        }
    }

    private static long monotonicNowNs() {
        return android.os.SystemClock.uptimeNanos();
    }

    private static final int LIFECYCLE_DETACHED = 0;
    private static final int LIFECYCLE_INACTIVE = 1;
    private static final int LIFECYCLE_RESUMED = 2;
    private static final int LIFECYCLE_PAUSED = 3;
    private static final int LIFECYCLE_HIDDEN = 4;
    private static final int LIFECYCLE_REASON_SYSTEM = 1;

    static {
        System.loadLibrary("noxapp");
    }

    private AuraView view;
    private float safeLeft;
    private float safeTop;
    private float safeRight;
    private float safeBottom;
    private float gestureLeft;
    private float gestureTop;
    private float gestureRight;
    private float gestureBottom;
    private float keyboard;
    private float keyboardDuration;
    private int keyboardCurve;
    private float[] cutouts = new float[0];
    private float loggedW;
    private float loggedH;
    private boolean logFrame = true;
    private boolean frameCallbacksActive;
    private boolean backPending;
    private Object backCallback;
    private String environmentSignature = "";

    public native void nativeBind(AuraHost host);
    public native void nativePointer(int phase, float x, float y, float scale, long timestampNs, int deviceId, int pointerId, int pointerKind, int buttons, float pressure, float tiltX, float tiltY);
    public native void nativeKey(boolean down, int logicalKey, int modifiers, long timestampNs, int deviceId);
    public native void nativeResize(float width, float height, float scale, float safeLeft, float safeTop, float safeRight, float safeBottom, float keyboard, float gestureLeft, float gestureTop, float gestureRight, float gestureBottom, float refreshRate, float fontScale, int rotationDegrees, float keyboardDuration, int keyboardCurve, float[] cutouts, long timestampNs);
    public native boolean nativeEnvironment(int brightness, int highContrast, int reduceMotion, int accessibleNavigation, int textDirection, int boldText, float fontScale, String locales, long timestampNs);
    public native void nativeMemoryPressure(int level, long timestampNs);
    public native void nativeVsync();
    public native void nativeLifecycle(int state, int reason, long timestampNs);
    public native void nativeSystemBack(int phase, float progress, long timestampNs);
    public native void nativePlatformRoute(int source, String location, long timestampNs);
    public native boolean nativeSurfaceAttach(int width, int height, long timestampNs);
    public native boolean nativeSurfaceResize(int width, int height, long timestampNs);
    public native boolean nativeSurfaceDetach(long timestampNs);
    public native void nativeShutdown(boolean stopRuntime);
    public native void nativeTextEdit(int operation, String text, int first, int second, long timestampNs);
    public native void nativeBlit(Bitmap bitmap);
    public native int nativeAxCount();
    public native String nativeAxLabel(int index);
    public native String nativeAxValue(int index);
    public native String nativeAxRole(int index);
    public native String nativeAxClassName(int index);
    public native int nativeAxChecked(int index);
    public native long nativeAxPress(int index);
    public native int nativeAxActivate(int index);
    public native float[] nativeAxFrame(int index);
    public native boolean nativeAssetStampCurrent(String path);
    public native String nativeAssetVersion();
    public native void nativeStart(String documents, String support, String cache, String tempDir, String assets);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        File documents = new File(getFilesDir(), "documents");
        File support = new File(getFilesDir(), "support");
        File cache = getCacheDir();
        File temp = new File(cache, "tmp");
        File assets = new File(getFilesDir(), "aura");
        File staging = new File(getFilesDir(), "aura-staging");
        documents.mkdirs();
        support.mkdirs();
        temp.mkdirs();
        if (!nativeAssetStampCurrent(new File(assets, ".aura-stamp").getAbsolutePath())) {
            deleteTree(staging);
            extractAssets("", staging);
            writeStamp(new File(staging, ".aura-stamp"));
            deleteTree(assets);
            if (!staging.renameTo(assets)) {
                throw new RuntimeException("asset extract rename failed");
            }
        }
        view = new AuraView(this);
        setContentView(view);
        nativeBind(this);
        nativeStart(documents.getAbsolutePath(), support.getAbsolutePath(), cache.getAbsolutePath(), temp.getAbsolutePath(), assets.getAbsolutePath());
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            view.setWindowInsetsAnimationCallback(new WindowInsetsAnimation.Callback(WindowInsetsAnimation.Callback.DISPATCH_MODE_CONTINUE_ON_SUBTREE) {
                @Override
                public void onPrepare(WindowInsetsAnimation animation) {
                    keyboardDuration = animation.getDurationMillis() / 1000.0f;
                    keyboardCurve = 0;
                }

                @Override
                public WindowInsets onProgress(WindowInsets insets, java.util.List<WindowInsetsAnimation> runningAnimations) {
                    view.onApplyWindowInsets(insets);
                    return insets;
                }
            });
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
        publishEnvironment();
        publishIntent(getIntent());
        if (Build.VERSION.SDK_INT >= 34) {
            backCallback = BackApi34.register(this);
        } else if (Build.VERSION.SDK_INT >= 33) {
            backCallback = BackApi33.register(this);
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        publishIntent(intent);
    }

    private void publishIntent(Intent intent) {
        long timestampNs = monotonicNowNs();
        if (intent == null || intent.getDataString() == null || intent.getDataString().isEmpty()) {
            return;
        }
        String location = intent.getDataString();
        int source = location.startsWith("https://") || location.startsWith("http://") ? 1 : 3;
        nativePlatformRoute(source, location, timestampNs);
    }

    @Override
    public void onTrimMemory(int level) {
        long timestampNs = monotonicNowNs();
        super.onTrimMemory(level);
        nativeMemoryPressure(memoryLevel(level), timestampNs);
        Log.i("aura", "aura-memory " + level);
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        publishEnvironment();
        publishSize();
    }

    private static int memoryLevel(int level) {
        if (level >= android.content.ComponentCallbacks2.TRIM_MEMORY_COMPLETE || level == android.content.ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL) {
            return 3;
        }
        if (level >= android.content.ComponentCallbacks2.TRIM_MEMORY_MODERATE || level == android.content.ComponentCallbacks2.TRIM_MEMORY_RUNNING_LOW) {
            return 2;
        }
        return 1;
    }

    @Override
    protected void onStart() {
        long timestampNs = monotonicNowNs();
        super.onStart();
        nativeLifecycle(LIFECYCLE_INACTIVE, LIFECYCLE_REASON_SYSTEM, timestampNs);
    }

    @Override
    protected void onPause() {
        long timestampNs = monotonicNowNs();
        frameCallbacksActive = false;
        Choreographer.getInstance().removeFrameCallback(this);
        nativeLifecycle(LIFECYCLE_PAUSED, LIFECYCLE_REASON_SYSTEM, timestampNs);
        Log.i("aura", "aura-background");
        super.onPause();
    }

    @Override
    protected void onResume() {
        long timestampNs = monotonicNowNs();
        super.onResume();
        publishEnvironment();
        nativeLifecycle(LIFECYCLE_RESUMED, LIFECYCLE_REASON_SYSTEM, timestampNs);
        if (!frameCallbacksActive) {
            frameCallbacksActive = true;
            Choreographer.getInstance().postFrameCallback(this);
        }
        logFrame = true;
        Log.i("aura", "aura-foreground");
    }

    @Override
    protected void onStop() {
        long timestampNs = monotonicNowNs();
        nativeLifecycle(LIFECYCLE_HIDDEN, LIFECYCLE_REASON_SYSTEM, timestampNs);
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        long timestampNs = monotonicNowNs();
        frameCallbacksActive = false;
        Choreographer.getInstance().removeFrameCallback(this);
        nativeLifecycle(LIFECYCLE_DETACHED, LIFECYCLE_REASON_SYSTEM, timestampNs);
        if (Build.VERSION.SDK_INT >= 33 && backCallback != null) {
            BackApi33.unregister(this, backCallback);
            backCallback = null;
        }
        nativeShutdown(!isChangingConfigurations());
        super.onDestroy();
    }

    @SuppressWarnings("deprecation")
    @Override
    public void onBackPressed() {
        if (Build.VERSION.SDK_INT >= 33) {
            requestBackCommit();
            return;
        }
        requestBackCommit();
    }

    private void postBackPhase(int phase, float progress) {
        if (phase == 4) {
            if (backPending) {
                return;
            }
            backPending = true;
        } else if (backPending) {
            return;
        }
        nativeSystemBack(phase, progress, System.nanoTime());
    }

    private void requestBackCommit() {
        postBackPhase(4, 1.0f);
    }

    public void resolveSystemBack(final boolean handled) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                backPending = false;
                if (!handled && !isFinishing()) {
                    finish();
                }
            }
        });
    }

    private static final class BackApi33 {
        static Object register(final AuraHost host) {
            OnBackInvokedCallback callback = host::requestBackCommit;
            host.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT,
                callback
            );
            return callback;
        }

        static void unregister(AuraHost host, Object callback) {
            host.getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback((OnBackInvokedCallback) callback);
        }
    }

    private static final class BackApi34 {
        static Object register(final AuraHost host) {
            OnBackAnimationCallback callback = new OnBackAnimationCallback() {
                @Override
                public void onBackStarted(BackEvent event) {
                    host.postBackPhase(1, 0.0f);
                }

                @Override
                public void onBackProgressed(BackEvent event) {
                    host.postBackPhase(2, event.getProgress());
                }

                @Override
                public void onBackCancelled() {
                    host.postBackPhase(3, 0.0f);
                }

                @Override
                public void onBackInvoked() {
                    host.requestBackCommit();
                }
            };
            host.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT,
                callback
            );
            return callback;
        }
    }

    @Override
    public void doFrame(long frameTimeNanos) {
        if (!frameCallbacksActive) {
            return;
        }
        if (view != null && view.surfaceAttached) {
            nativeVsync();
        }
        Choreographer.getInstance().postFrameCallback(this);
    }

    public void requestDraw() {
        if (view != null) {
            view.post(view::invalidate);
        }
    }

    public void setTextFocus(final boolean enabled) {
        if (view == null) {
            return;
        }
        view.post(new Runnable() {
            @Override
            public void run() {
                if (enabled) {
                    view.requestFocus();
                    InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
                    if (imm != null) {
                        imm.showSoftInput(view, InputMethodManager.SHOW_IMPLICIT);
                    }
                } else {
                    InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
                    if (imm != null) {
                        imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
                    }
                }
            }
        });
    }

    public void setTextState(final String text, final long generation, final long revision, final int selectionStart, final int selectionEnd, final int composingStart, final int composingEnd) {
        if (view == null) {
            return;
        }
        view.post(new Runnable() {
            @Override
            public void run() {
                view.setTextState(text, generation, revision, selectionStart, selectionEnd, composingStart, composingEnd);
            }
        });
    }

    public void setTextGeometry(final float caretX, final float caretY, final float caretWidth, final float caretHeight, final float editableX, final float editableY, final float editableWidth, final float editableHeight) {
        if (view == null) {
            return;
        }
        view.post(new Runnable() {
            @Override
            public void run() {
                view.caretRect.set(caretX, caretY, caretX + caretWidth, caretY + caretHeight);
                view.editableRect.set(editableX, editableY, editableX + editableWidth, editableY + editableHeight);
                view.publishCursorAnchorInfo();
            }
        });
    }

    public void setTextBoundaries(final long revision, final int[] offsets, final float[] xs, final float[] ys, final float[] heights) {
        if (view == null) {
            return;
        }
        view.post(new Runnable() {
            @Override
            public void run() {
                view.boundaryRevision = revision;
                view.boundaryOffsets = offsets != null ? offsets : new int[0];
                view.boundaryXs = xs != null ? xs : new float[0];
                view.boundaryYs = ys != null ? ys : new float[0];
                view.boundaryHeights = heights != null ? heights : new float[0];
                view.publishCursorAnchorInfo();
            }
        });
    }

    public void setTextConfiguration(final int inputType, final int action, final boolean multiline, final boolean secure, final boolean autocorrect, final int capitalization, final String autofillHints) {
        if (view == null) {
            return;
        }
        view.post(new Runnable() {
            @Override
            public void run() {
                view.inputTypeConfig = inputType;
                view.actionConfig = action;
                view.multilineConfig = multiline;
                view.secureConfig = secure;
                view.autocorrectConfig = autocorrect;
                view.capitalizationConfig = capitalization;
                if (Build.VERSION.SDK_INT >= 26) {
                    view.setAutofillHints(autofillHints == null || autofillHints.isEmpty() ? null : autofillHints.split("\\n"));
                }
                InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
                if (imm != null && view.hasFocus()) {
                    imm.restartInput(view);
                }
            }
        });
    }

    public void clipboardSet(String text) {
        ClipboardManager manager = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        if (manager != null) {
            manager.setPrimaryClip(ClipData.newPlainText("aura", text != null ? text : ""));
        }
    }

    public String clipboardGet() {
        ClipboardManager manager = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        if (manager == null || !manager.hasPrimaryClip() || manager.getPrimaryClip().getItemCount() == 0) {
            return "";
        }
        CharSequence text = manager.getPrimaryClip().getItemAt(0).getText();
        return text != null ? text.toString() : "";
    }

    void noteInsets(float left, float top, float right, float bottom, float ime, float systemLeft, float systemTop, float systemRight, float systemBottom, float[] nextCutouts) {
        safeLeft = left;
        safeTop = top;
        safeRight = right;
        safeBottom = bottom;
        keyboard = ime;
        gestureLeft = systemLeft;
        gestureTop = systemTop;
        gestureRight = systemRight;
        gestureBottom = systemBottom;
        cutouts = nextCutouts != null ? nextCutouts : new float[0];
        publishSize();
    }

    private void publishEnvironment() {
        long timestampNs = monotonicNowNs();
        Configuration config = getResources().getConfiguration();
        int night = config.uiMode & Configuration.UI_MODE_NIGHT_MASK;
        int brightness = night == Configuration.UI_MODE_NIGHT_YES ? 1 : 0;
        int direction = config.getLayoutDirection() == View.LAYOUT_DIRECTION_RTL ? 1 : 0;
        int contrast = 0;
        int navigation = 0;
        int reduce = 0;
        int bold = 0;
        float animator = 1.0f;
        StringBuilder locales = new StringBuilder();
        android.os.LocaleList list = config.getLocales();
        AccessibilityManager accessibility = (AccessibilityManager) getSystemService(ACCESSIBILITY_SERVICE);
        int i = 0;
        try {
            contrast = Settings.Secure.getInt(getContentResolver(), "high_text_contrast_enabled", 0) == 1 ? 1 : 0;
            animator = Settings.Global.getFloat(getContentResolver(), Settings.Global.ANIMATOR_DURATION_SCALE, 1.0f);
        } catch (Exception ignored) {
            contrast = 0;
            animator = 1.0f;
        }
        reduce = animator == 0.0f ? 1 : 0;
        if (accessibility != null && accessibility.isTouchExplorationEnabled()) {
            navigation = 1;
        }
        if (Build.VERSION.SDK_INT >= 31 && config.fontWeightAdjustment > 0) {
            bold = 1;
        }
        while (i < list.size()) {
            if (i > 0) {
                locales.append('\n');
            }
            locales.append(list.get(i).toLanguageTag());
            i += 1;
        }
        String signature = brightness + "|" + contrast + "|" + reduce + "|" + navigation + "|" + direction + "|" + bold + "|" + config.fontScale + "|" + locales;
        if (signature.equals(environmentSignature)) {
            return;
        }
        if (nativeEnvironment(brightness, contrast, reduce, navigation, direction, bold, config.fontScale, locales.toString(), timestampNs)) {
            environmentSignature = signature;
        }
    }

    void publishSize() {
        long timestampNs = monotonicNowNs();
        if (view == null) {
            return;
        }
        float scale = getResources().getDisplayMetrics().density;
        float width = view.getWidth() / scale;
        float height = view.getHeight() / scale;
        if (width < 1.0f || height < 1.0f) {
            return;
        }
        if (width != loggedW || height != loggedH) {
            loggedW = width;
            loggedH = height;
            Log.i("aura", "aura-resize " + width + " " + height);
        }
        float refreshRate = view.getDisplay() != null ? view.getDisplay().getRefreshRate() : 60.0f;
        float fontScale = getResources().getConfiguration().fontScale;
        int rotation = Surface.ROTATION_0;
        Display display = view.getDisplay();
        if (display != null) {
            rotation = display.getRotation();
        }
        nativeResize(width, height, scale, safeLeft, safeTop, safeRight, safeBottom, keyboard, gestureLeft, gestureTop, gestureRight, gestureBottom, refreshRate, fontScale, rotation * 90, keyboardDuration, keyboardCurve, cutouts, timestampNs);
    }

    private void deleteTree(File file) {
        if (file == null || !file.exists()) {
            return;
        }
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        if (!file.delete()) {
            throw new RuntimeException("asset cleanup failed: " + file.getAbsolutePath());
        }
    }

    private void writeStamp(File stamp) {
        try {
            String version = nativeAssetVersion();
            java.io.FileOutputStream out = new java.io.FileOutputStream(stamp);
            out.write(version.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            out.close();
        } catch (Exception ignored) {
            throw new RuntimeException("asset stamp failed: " + stamp.getAbsolutePath(), ignored);
        }
    }

    private void extractAssets(String path, File dest) {
        try {
            String[] names = getAssets().list(path);
            if (names == null) {
                return;
            }
            if (names.length == 0 && path.length() > 0) {
                File parent = dest.getParentFile();
                if (parent != null) {
                    parent.mkdirs();
                }
                InputStream in = getAssets().open(path);
                OutputStream out = new FileOutputStream(dest);
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
                in.close();
                out.close();
                return;
            }
            dest.mkdirs();
            for (String name : names) {
                String child = path.length() == 0 ? name : path + "/" + name;
                extractAssets(child, new File(dest, name));
            }
        } catch (Exception ignored) {
            throw new RuntimeException("asset extract failed: " + path, ignored);
        }
    }

    private final class AuraView extends View {
        private Bitmap bitmap;
        private boolean surfaceAttached;
        private final SpannableStringBuilder editable = new SpannableStringBuilder();
        private final Object composingSpan = new Object();
        private long textGeneration;
        private long textRevision;
        private final RectF caretRect = new RectF();
        private final RectF editableRect = new RectF();
        private long boundaryRevision;
        private int[] boundaryOffsets = new int[0];
        private float[] boundaryXs = new float[0];
        private float[] boundaryYs = new float[0];
        private float[] boundaryHeights = new float[0];
        private int inputTypeConfig = 1;
        private int actionConfig;
        private boolean multilineConfig;
        private boolean secureConfig;
        private boolean autocorrectConfig = true;
        private int capitalizationConfig;
        private boolean cursorAnchorMonitor;
        private boolean extractedTextMonitor;
        private int extractedTextToken;

        AuraView(Activity activity) {
            super(activity);
            setFocusable(true);
            setFocusableInTouchMode(true);
            setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
        }

        void setTextState(String text, long generation, long revision, int selectionStart, int selectionEnd, int composingStart, int composingEnd) {
            if (generation < textGeneration || (generation == textGeneration && revision < textRevision)) {
                return;
            }
            textGeneration = generation;
            textRevision = revision;
            String value = text != null ? text : "";
            editable.replace(0, editable.length(), value);
            int start = utf16OffsetForCodePoint(value, selectionStart);
            int end = utf16OffsetForCodePoint(value, selectionEnd);
            int composingStartUtf16 = composingStart >= 0 ? utf16OffsetForCodePoint(value, composingStart) : -1;
            int composingEndUtf16 = composingEnd >= 0 ? utf16OffsetForCodePoint(value, composingEnd) : -1;
            Selection.setSelection(editable, start, end);
            BaseInputConnection.removeComposingSpans(editable);
            if (composingStartUtf16 >= 0 && composingEndUtf16 >= composingStartUtf16 && composingEndUtf16 <= editable.length()) {
                editable.setSpan(composingSpan, composingStartUtf16, composingEndUtf16, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE | Spanned.SPAN_COMPOSING);
            }
            InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.updateSelection(this, start, end, composingStartUtf16, composingEndUtf16);
            }
            publishExtractedText();
            publishCursorAnchorInfo();
        }

        private ExtractedText extractedText() {
            ExtractedText result = new ExtractedText();
            result.text = editable.toString();
            result.startOffset = 0;
            result.partialStartOffset = -1;
            result.partialEndOffset = -1;
            result.selectionStart = Math.max(0, Selection.getSelectionStart(editable));
            result.selectionEnd = Math.max(0, Selection.getSelectionEnd(editable));
            result.flags = multilineConfig ? 0 : ExtractedText.FLAG_SINGLE_LINE;
            return result;
        }

        private void publishExtractedText() {
            if (!extractedTextMonitor) return;
            InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) imm.updateExtractedText(this, extractedTextToken, extractedText());
        }

        private CursorAnchorInfo cursorAnchorInfo() {
            float scale = getResources().getDisplayMetrics().density;
            int selectionStart = Math.max(0, Selection.getSelectionStart(editable));
            int selectionEnd = Math.max(0, Selection.getSelectionEnd(editable));
            CursorAnchorInfo.Builder builder = new CursorAnchorInfo.Builder();
            builder.setSelectionRange(selectionStart, selectionEnd);
            int composingStart = BaseInputConnection.getComposingSpanStart(editable);
            int composingEnd = BaseInputConnection.getComposingSpanEnd(editable);
            if (composingStart >= 0 && composingEnd >= composingStart && composingEnd <= editable.length()) {
                builder.setComposingText(composingStart, editable.subSequence(composingStart, composingEnd));
            }
            builder.setInsertionMarkerLocation(
                caretRect.left * scale,
                caretRect.top * scale,
                caretRect.bottom * scale,
                caretRect.bottom * scale,
                CursorAnchorInfo.FLAG_HAS_VISIBLE_REGION
            );
            int boundary = 0;
            while (boundary < boundaryOffsets.length && boundary < boundaryXs.length && boundary < boundaryYs.length && boundary < boundaryHeights.length) {
                int utf16 = utf16OffsetForCodePoint(editable.toString(), boundaryOffsets[boundary]);
                float width = Math.max(1.0f, caretRect.width()) * scale;
                float left = (editableRect.left + boundaryXs[boundary]) * scale;
                float top = (editableRect.top + boundaryYs[boundary]) * scale;
                if (boundary + 1 < boundaryYs.length && Math.abs(boundaryYs[boundary + 1] - boundaryYs[boundary]) < 0.5f) {
                    width = Math.max(1.0f, boundaryXs[boundary + 1] - boundaryXs[boundary]) * scale;
                }
                builder.addCharacterBounds(utf16, left, top, left + width, top + boundaryHeights[boundary] * scale, CursorAnchorInfo.FLAG_HAS_VISIBLE_REGION);
                boundary += 1;
            }
            int[] screen = new int[2];
            getLocationOnScreen(screen);
            Matrix matrix = new Matrix();
            matrix.setTranslate(screen[0], screen[1]);
            builder.setMatrix(matrix);
            return builder.build();
        }

        private void publishCursorAnchorInfo() {
            if (!cursorAnchorMonitor) return;
            InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) imm.updateCursorAnchorInfo(this, cursorAnchorInfo());
        }

        private int utf16OffsetForCodePoint(String value, int codePointOffset) {
            int count = value.codePointCount(0, value.length());
            int clamped = Math.max(0, Math.min(codePointOffset, count));
            return value.offsetByCodePoints(0, clamped);
        }

        private int codePointOffsetForUtf16(int utf16Offset) {
            int clamped = Math.max(0, Math.min(utf16Offset, editable.length()));
            if (clamped > 0 && clamped < editable.length() && Character.isLowSurrogate(editable.charAt(clamped)) && Character.isHighSurrogate(editable.charAt(clamped - 1))) {
                clamped -= 1;
            }
            return Character.codePointCount(editable, 0, clamped);
        }

        private int codePointCountForUtf16Range(int start, int end) {
            int left = Math.max(0, Math.min(start, editable.length()));
            int right = Math.max(left, Math.min(end, editable.length()));
            if (left > 0 && left < editable.length() && Character.isLowSurrogate(editable.charAt(left)) && Character.isHighSurrogate(editable.charAt(left - 1))) {
                left -= 1;
            }
            if (right > 0 && right < editable.length() && Character.isLowSurrogate(editable.charAt(right)) && Character.isHighSurrogate(editable.charAt(right - 1))) {
                right += 1;
            }
            return Character.codePointCount(editable, left, right);
        }

        private int logicalKey(int keyCode) {
            if (keyCode >= KeyEvent.KEYCODE_A && keyCode <= KeyEvent.KEYCODE_Z) {
                return 4 + keyCode - KeyEvent.KEYCODE_A;
            }
            switch (keyCode) {
                case KeyEvent.KEYCODE_ENTER:
                case KeyEvent.KEYCODE_NUMPAD_ENTER:
                    return 40;
                case KeyEvent.KEYCODE_ESCAPE:
                    return 41;
                case KeyEvent.KEYCODE_DEL:
                    return 42;
                case KeyEvent.KEYCODE_TAB:
                    return 43;
                case KeyEvent.KEYCODE_SPACE:
                    return 44;
                case KeyEvent.KEYCODE_MOVE_HOME:
                    return 74;
                case KeyEvent.KEYCODE_FORWARD_DEL:
                    return 76;
                case KeyEvent.KEYCODE_MOVE_END:
                    return 77;
                case KeyEvent.KEYCODE_DPAD_RIGHT:
                    return 79;
                case KeyEvent.KEYCODE_DPAD_LEFT:
                    return 80;
                case KeyEvent.KEYCODE_DPAD_DOWN:
                    return 81;
                case KeyEvent.KEYCODE_DPAD_UP:
                    return 82;
                default:
                    return 0;
            }
        }

        private int keyModifiers(KeyEvent event) {
            int modifiers = 0;
            if (event.isMetaPressed()) modifiers |= 1;
            if (event.isShiftPressed()) modifiers |= 2;
            if (event.isCtrlPressed()) modifiers |= 4;
            if (event.isAltPressed()) modifiers |= 8;
            return modifiers;
        }

        @Override
        public boolean onKeyDown(int keyCode, KeyEvent event) {
            int logical = logicalKey(keyCode);
            if (logical == 0) {
                return super.onKeyDown(keyCode, event);
            }
            nativeKey(true, logical, keyModifiers(event), event.getEventTime() * 1000000L, event.getDeviceId());
            return true;
        }

        @Override
        public boolean onKeyUp(int keyCode, KeyEvent event) {
            int logical = logicalKey(keyCode);
            if (logical == 0) {
                return super.onKeyUp(keyCode, event);
            }
            nativeKey(false, logical, keyModifiers(event), event.getEventTime() * 1000000L, event.getDeviceId());
            return true;
        }

        @Override
        protected void onAttachedToWindow() {
            long timestampNs = monotonicNowNs();
            super.onAttachedToWindow();
            surfaceAttached = nativeSurfaceAttach(getWidth(), getHeight(), timestampNs);
        }

        @Override
        protected void onDetachedFromWindow() {
            long timestampNs = monotonicNowNs();
            surfaceAttached = nativeSurfaceDetach(timestampNs);
            if (bitmap != null) {
                bitmap.recycle();
                bitmap = null;
            }
            super.onDetachedFromWindow();
        }

        @Override
        public WindowInsets onApplyWindowInsets(WindowInsets insets) {
            float scale = getResources().getDisplayMetrics().density;
            float[] nextCutouts = cutoutsFrom(insets, scale);
            if (Build.VERSION.SDK_INT >= 30) {
                Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
                Insets ime = insets.getInsets(WindowInsets.Type.ime());
                Insets gestures = insets.getInsets(WindowInsets.Type.systemGestures());
                noteInsets(bars.left / scale, bars.top / scale, bars.right / scale, bars.bottom / scale, ime.bottom / scale, gestures.left / scale, gestures.top / scale, gestures.right / scale, gestures.bottom / scale, nextCutouts);
            } else {
                noteInsets(insets.getSystemWindowInsetLeft() / scale, insets.getSystemWindowInsetTop() / scale, insets.getSystemWindowInsetRight() / scale, insets.getSystemWindowInsetBottom() / scale, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, nextCutouts);
            }
            return insets;
        }

        private float[] cutoutsFrom(WindowInsets insets, float scale) {
            if (Build.VERSION.SDK_INT < 28 || insets.getDisplayCutout() == null || scale <= 0.0f) {
                return new float[0];
            }
            DisplayCutout cutout = insets.getDisplayCutout();
            java.util.List<Rect> rects = cutout.getBoundingRects();
            int limit = Math.min(rects.size(), 8);
            float[] values = new float[limit * 4];
            int i = 0;
            while (i < limit) {
                Rect rect = rects.get(i);
                values[i * 4] = rect.left / scale;
                values[i * 4 + 1] = rect.top / scale;
                values[i * 4 + 2] = rect.width() / scale;
                values[i * 4 + 3] = rect.height() / scale;
                i += 1;
            }
            return values;
        }

        @Override
        protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            long timestampNs = monotonicNowNs();
            super.onSizeChanged(w, h, oldw, oldh);
            nativeSurfaceResize(w, h, timestampNs);
            publishSize();
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            int action = event.getActionMasked();
            int actionIndex = event.getActionIndex();
            int i;
            int h;
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                emitPointer(event, actionIndex, 0, -1);
                return true;
            }
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
                emitPointer(event, actionIndex, 2, -1);
                return true;
            }
            if (action == MotionEvent.ACTION_MOVE) {
                h = 0;
                while (h < event.getHistorySize()) {
                    i = 0;
                    while (i < event.getPointerCount()) {
                        emitPointer(event, i, 1, h);
                        i += 1;
                    }
                    h += 1;
                }
                i = 0;
                while (i < event.getPointerCount()) {
                    emitPointer(event, i, 1, -1);
                    i += 1;
                }
                return true;
            }
            if (action == MotionEvent.ACTION_CANCEL) {
                i = 0;
                while (i < event.getPointerCount()) {
                    emitPointer(event, i, 3, -1);
                    i += 1;
                }
                return true;
            }
            return false;
        }

        private int pointerKind(int toolType) {
            switch (toolType) {
                case MotionEvent.TOOL_TYPE_FINGER:
                    return 1;
                case MotionEvent.TOOL_TYPE_MOUSE:
                    return 2;
                case MotionEvent.TOOL_TYPE_STYLUS:
                case MotionEvent.TOOL_TYPE_ERASER:
                    return 3;
                default:
                    return 0;
            }
        }

        private void emitPointer(MotionEvent event, int index, int phase, int historyIndex) {
            float x;
            float y;
            float pressure;
            float tilt;
            long timestampNs;
            if (historyIndex >= 0) {
                x = event.getHistoricalX(index, historyIndex);
                y = event.getHistoricalY(index, historyIndex);
                pressure = event.getHistoricalPressure(index, historyIndex);
                tilt = event.getHistoricalAxisValue(MotionEvent.AXIS_TILT, index, historyIndex);
                timestampNs = event.getHistoricalEventTimeNanos(historyIndex);
            } else {
                x = event.getX(index);
                y = event.getY(index);
                pressure = event.getPressure(index);
                tilt = event.getAxisValue(MotionEvent.AXIS_TILT, index);
                timestampNs = event.getEventTimeNanos();
            }
            int buttons = event.getButtonState();
            if (buttons == 0 && event.getToolType(index) == MotionEvent.TOOL_TYPE_FINGER) {
                buttons = 1;
            }
            nativePointer(phase, x, y, getResources().getDisplayMetrics().density, timestampNs, event.getDeviceId(), event.getPointerId(index), pointerKind(event.getToolType(index)), buttons, pressure, tilt, 0.0f);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            if (!surfaceAttached) {
                return;
            }
            if (bitmap == null || bitmap.getWidth() != getWidth() || bitmap.getHeight() != getHeight()) {
                if (getWidth() < 1 || getHeight() < 1) {
                    return;
                }
                bitmap = Bitmap.createBitmap(getWidth(), getHeight(), Bitmap.Config.ARGB_8888);
            }
            nativeBlit(bitmap);
            canvas.drawBitmap(bitmap, 0, 0, null);
            if (logFrame) {
                Log.i("aura", "aura-frame");
                logFrame = false;
            }
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return true;
        }

        private int configuredInputType() {
            int type;
            switch (inputTypeConfig) {
                case 2: type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS; break;
                case 3: type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI; break;
                case 4: type = InputType.TYPE_CLASS_PHONE; break;
                case 5: type = InputType.TYPE_CLASS_NUMBER; break;
                default: type = InputType.TYPE_CLASS_TEXT; break;
            }
            if (multilineConfig) type |= InputType.TYPE_TEXT_FLAG_MULTI_LINE;
            if (!autocorrectConfig && inputTypeConfig != 4 && inputTypeConfig != 5) type |= InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
            if (autocorrectConfig && inputTypeConfig == 1) type |= InputType.TYPE_TEXT_FLAG_AUTO_CORRECT;
            if (capitalizationConfig == 1) type |= InputType.TYPE_TEXT_FLAG_CAP_SENTENCES;
            if (capitalizationConfig == 2) type |= InputType.TYPE_TEXT_FLAG_CAP_WORDS;
            if (capitalizationConfig == 3) type |= InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS;
            if (secureConfig) {
                if (inputTypeConfig == 5) type |= InputType.TYPE_NUMBER_VARIATION_PASSWORD;
                else type = (type & ~InputType.TYPE_MASK_VARIATION) | InputType.TYPE_TEXT_VARIATION_PASSWORD;
            }
            return type;
        }

        private int configuredImeOptions() {
            int options = EditorInfo.IME_FLAG_NO_EXTRACT_UI;
            switch (actionConfig) {
                case 1: return options | EditorInfo.IME_ACTION_DONE;
                case 2: return options | EditorInfo.IME_ACTION_GO;
                case 3: return options | EditorInfo.IME_ACTION_NEXT;
                case 4: return options | EditorInfo.IME_ACTION_SEARCH;
                case 5: return options | EditorInfo.IME_ACTION_SEND;
                case 6: return options | EditorInfo.IME_ACTION_NONE;
                default: return options | EditorInfo.IME_ACTION_UNSPECIFIED;
            }
        }

        private int normalizedEditorAction(int actionId) {
            if (actionConfig >= 1 && actionConfig <= 6) return actionConfig;
            switch (actionId) {
                case EditorInfo.IME_ACTION_DONE: return 1;
                case EditorInfo.IME_ACTION_GO: return 2;
                case EditorInfo.IME_ACTION_NEXT: return 3;
                case EditorInfo.IME_ACTION_SEARCH: return 4;
                case EditorInfo.IME_ACTION_SEND: return 5;
                case EditorInfo.IME_ACTION_NONE: return 6;
                default: return 0;
            }
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
            outAttrs.inputType = configuredInputType();
            outAttrs.imeOptions = configuredImeOptions();
            return new BaseInputConnection(this, false) {
                private int batchDepth;
                private final ArrayList<PendingTextEdit> pendingEdits = new ArrayList<>();

                private void dispatchTextEdit(int operation, String text, int first, int second) {
                    PendingTextEdit edit = new PendingTextEdit(operation, text != null ? text : "", first, second, monotonicNowNs());
                    if (batchDepth > 0) {
                        pendingEdits.add(edit);
                    } else {
                        nativeTextEdit(edit.operation, edit.text, edit.first, edit.second, edit.timestampNs);
                    }
                }

                private void flushPendingEdits() {
                    for (PendingTextEdit edit : pendingEdits) {
                        nativeTextEdit(edit.operation, edit.text, edit.first, edit.second, edit.timestampNs);
                    }
                    pendingEdits.clear();
                    publishExtractedText();
                    publishCursorAnchorInfo();
                }

                @Override
                public Editable getEditable() {
                    return editable;
                }

                @Override
                public boolean beginBatchEdit() {
                    batchDepth += 1;
                    super.beginBatchEdit();
                    return true;
                }

                @Override
                public boolean endBatchEdit() {
                    if (batchDepth <= 0) return false;
                    super.endBatchEdit();
                    batchDepth -= 1;
                    if (batchDepth == 0) flushPendingEdits();
                    return true;
                }

                @Override
                public void closeConnection() {
                    batchDepth = 0;
                    flushPendingEdits();
                    super.closeConnection();
                }

                @Override
                public boolean commitText(CharSequence text, int newCursorPosition) {
                    if (text != null && (text.toString().equals("\n") || text.toString().equals("\r")) && !multilineConfig && actionConfig != 6) {
                        dispatchTextEdit(8, "", actionConfig, 0);
                        return true;
                    }
                    super.commitText(text, newCursorPosition);
                    dispatchTextEdit(1, text != null ? text.toString() : "", newCursorPosition, 0);
                    return true;
                }

                @Override
                public boolean performEditorAction(int actionCode) {
                    dispatchTextEdit(8, "", normalizedEditorAction(actionCode), 0);
                    return true;
                }

                @Override
                public boolean setComposingText(CharSequence text, int newCursorPosition) {
                    super.setComposingText(text, newCursorPosition);
                    dispatchTextEdit(2, text != null ? text.toString() : "", newCursorPosition, 0);
                    return true;
                }

                @Override
                public boolean setComposingRegion(int start, int end) {
                    int codePointStart = codePointOffsetForUtf16(start);
                    int codePointEnd = codePointOffsetForUtf16(end);
                    super.setComposingRegion(start, end);
                    dispatchTextEdit(6, "", codePointStart, codePointEnd);
                    return true;
                }

                @Override
                public boolean finishComposingText() {
                    super.finishComposingText();
                    dispatchTextEdit(3, "", 0, 0);
                    return true;
                }

                @Override
                public boolean setSelection(int start, int end) {
                    int codePointStart = codePointOffsetForUtf16(start);
                    int codePointEnd = codePointOffsetForUtf16(end);
                    super.setSelection(start, end);
                    dispatchTextEdit(4, "", codePointStart, codePointEnd);
                    return true;
                }

                @Override
                public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                    int selectionStart = Selection.getSelectionStart(editable);
                    int selectionEnd = Selection.getSelectionEnd(editable);
                    if (selectionStart < 0) selectionStart = 0;
                    if (selectionEnd < 0) selectionEnd = selectionStart;
                    int left = Math.min(selectionStart, selectionEnd);
                    int right = Math.max(selectionStart, selectionEnd);
                    int before = codePointCountForUtf16Range(Math.max(0, left - Math.max(0, beforeLength)), left);
                    int after = codePointCountForUtf16Range(right, Math.min(editable.length(), right + Math.max(0, afterLength)));
                    super.deleteSurroundingText(beforeLength, afterLength);
                    dispatchTextEdit(5, "", before, after);
                    return true;
                }

                @Override
                public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
                    if ((flags & InputConnection.GET_EXTRACTED_TEXT_MONITOR) != 0 && request != null) {
                        extractedTextMonitor = true;
                        extractedTextToken = request.token;
                    }
                    return extractedText();
                }

                @Override
                public boolean requestCursorUpdates(int cursorUpdateMode) {
                    cursorAnchorMonitor = (cursorUpdateMode & InputConnection.CURSOR_UPDATE_MONITOR) != 0;
                    if ((cursorUpdateMode & InputConnection.CURSOR_UPDATE_IMMEDIATE) != 0) {
                        InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
                        if (imm != null) imm.updateCursorAnchorInfo(AuraView.this, cursorAnchorInfo());
                    }
                    return true;
                }
            };
        }

        @Override
        public AccessibilityNodeProvider getAccessibilityNodeProvider() {
            return new AccessibilityNodeProvider() {
                private Rect screenBounds(float x, float y, float w, float h) {
                    int[] origin = new int[2];
                    getLocationOnScreen(origin);
                    float scale = getResources().getDisplayMetrics().density;
                    int left = origin[0] + (int) (x * scale);
                    int top = origin[1] + (int) (y * scale);
                    return new Rect(left, top, left + Math.max(1, (int) (w * scale)), top + Math.max(1, (int) (h * scale)));
                }

                @Override
                public AccessibilityNodeInfo createAccessibilityNodeInfo(int virtualViewId) {
                    if (virtualViewId == HOST_VIEW_ID) {
                        AccessibilityNodeInfo host = AccessibilityNodeInfo.obtain();
                        host.setSource(AuraView.this);
                        host.setClassName(View.class.getName());
                        host.setPackageName(getContext().getPackageName());
                        host.setVisibleToUser(true);
                        host.setEnabled(true);
                        host.setBoundsInScreen(screenBounds(0, 0, getWidth() / getResources().getDisplayMetrics().density, getHeight() / getResources().getDisplayMetrics().density));
                        int count = nativeAxCount();
                        int i = 0;
                        while (i < count) {
                            host.addChild(AuraView.this, i);
                            i = i + 1;
                        }
                        return host;
                    }
                    float[] frame = nativeAxFrame(virtualViewId);
                    if (frame == null || frame.length < 4) {
                        return null;
                    }
                    String label = nativeAxLabel(virtualViewId);
                    String value = nativeAxValue(virtualViewId);
                    String role = nativeAxRole(virtualViewId);
                    String className = nativeAxClassName(virtualViewId);
                    long press = nativeAxPress(virtualViewId);
                    AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain();
                    info.setSource(AuraView.this, virtualViewId);
                    info.setParent(AuraView.this);
                    info.setPackageName(getContext().getPackageName());
                    info.setClassName(className != null ? className : View.class.getName());
                    info.setContentDescription(label != null ? label : "");
                    if (value != null && value.length() > 0) {
                        info.setText(value);
                    }
                    info.setBoundsInScreen(screenBounds(frame[0], frame[1], frame[2], frame[3]));
                    info.setVisibleToUser(true);
                    info.setEnabled(true);
                    info.setFocusable(true);
                    if ("toggle".equals(role)) {
                        info.setCheckable(true);
                        info.setChecked(nativeAxChecked(virtualViewId) == 1);
                    }
                    info.setClickable(press >= 0);
                    if (press >= 0) {
                        info.addAction(AccessibilityNodeInfo.ACTION_CLICK);
                    }
                    return info;
                }

                @Override
                public boolean performAction(int virtualViewId, int action, Bundle arguments) {
                    if (virtualViewId == HOST_VIEW_ID || action != AccessibilityNodeInfo.ACTION_CLICK) {
                        return false;
                    }
                    return nativeAxActivate(virtualViewId) == 1;
                }
            };
        }
    }
}
