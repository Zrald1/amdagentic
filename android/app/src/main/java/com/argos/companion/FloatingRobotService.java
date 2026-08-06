package com.argos.companion;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.res.Resources;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.IBinder;
import android.text.TextUtils;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

public class FloatingRobotService extends Service implements SurfaceHolder.Callback {

    private WindowManager windowManager;
    private SurfaceView surfaceView;
    private LinearLayout bubbleOverlay;
    private ScrollView convoScroll;
    private LinearLayout convoLayout;
    private EditText inputEdit;
    private ImageButton sendBtn;
    private boolean bubbleVisible = false;
    private boolean chatInProgress = false;

    private WindowManager.LayoutParams robotParams;
    private WindowManager.LayoutParams bubbleParams;
    private int layoutType;

    // Drag state
    private boolean isDragging = false;
    private float dragStartRawX = 0;
    private float dragStartRawY = 0;
    private int dragStartWindowX = 0;
    private int dragStartWindowY = 0;

    // Robot position tracking
    private float robotScreenX = 0;
    private float robotScreenY = 0;
    private float robotSize = 100;
    private int screenWidth = 1080;
    private int screenHeight = 1920;

    private static FloatingRobotService instance;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        instance = this;
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);

        createFloatingWindow();
    }

    private void createFloatingWindow() {
        DisplayMetrics metrics = Resources.getSystem().getDisplayMetrics();
        screenWidth = metrics.widthPixels;
        screenHeight = metrics.heightPixels;

        // Robot size — 12% of screen width (must match C++ robot_gles.cpp)
        robotSize = screenWidth * 0.12f;
        // Window size — tight around robot: ~1.6x robot size wide, ~2.2x tall
        int robotWindowWidth = (int) (robotSize * 1.6f);
        int robotWindowHeight = (int) (robotSize * 2.2f);

        // SurfaceView for robot rendering — small tight window
        surfaceView = new SurfaceView(this);
        surfaceView.setZOrderOnTop(true);
        surfaceView.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        surfaceView.getHolder().addCallback(this);

        layoutType = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O ?
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY :
            WindowManager.LayoutParams.TYPE_PHONE;

        // Small overlay window — tight around robot
        robotParams = new WindowManager.LayoutParams(
            robotWindowWidth,
            robotWindowHeight,
            layoutType,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT
        );
        robotParams.gravity = Gravity.TOP | Gravity.START;
        // Center initially
        robotParams.x = (screenWidth - robotWindowWidth) / 2;
        robotParams.y = (int) (screenHeight * 0.35f - robotWindowHeight / 2.0f);

        windowManager.addView(surfaceView, robotParams);

        // Touch listener — handle drag + forward taps to native
        surfaceView.setOnTouchListener((v, event) -> {
            float x = event.getX();
            float y = event.getY();
            int action = event.getActionMasked();
            float rawX = event.getRawX();
            float rawY = event.getRawY();

            if (action == MotionEvent.ACTION_DOWN) {
                dragStartRawX = rawX;
                dragStartRawY = rawY;
                dragStartWindowX = robotParams.x;
                dragStartWindowY = robotParams.y;
                isDragging = false;
                nativeOnTouch(x, y, 0);
            } else if (action == MotionEvent.ACTION_MOVE) {
                float dx = rawX - dragStartRawX;
                float dy = rawY - dragStartRawY;
                if (Math.abs(dx) > 15 || Math.abs(dy) > 15) {
                    isDragging = true;
                    // Move window to follow finger
                    robotParams.x = dragStartWindowX + (int) dx;
                    robotParams.y = dragStartWindowY + (int) dy;
                    try {
                        windowManager.updateViewLayout(surfaceView, robotParams);
                    } catch (Exception e) {}
                    // Update robot screen position in native
                    float newScreenX = robotParams.x + robotParams.width / 2.0f;
                    float newScreenY = robotParams.y + robotParams.height / 2.0f;
                    nativeSetPosition(newScreenX, newScreenY);
                    robotScreenX = newScreenX;
                    robotScreenY = newScreenY;
                }
                nativeOnTouch(x, y, 2);
            } else if (action == MotionEvent.ACTION_UP) {
                if (isDragging) {
                    // Tell native to resume from new position
                    float newScreenX = robotParams.x + robotParams.width / 2.0f;
                    float newScreenY = robotParams.y + robotParams.height / 2.0f;
                    nativeSetPosition(newScreenX, newScreenY);
                    robotScreenX = newScreenX;
                    robotScreenY = newScreenY;
                    isDragging = false;
                } else {
                    // Forward tap to native for head/body detection
                    nativeOnTouch(x, y, 1);
                }
            }
            return true;
        });

        // Speech bubble overlay — hidden by default
        bubbleOverlay = createSpeechBubble();

        bubbleParams = new WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            layoutType,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_WATCH_OUTSIDE_TOUCH,
            PixelFormat.TRANSLUCENT
        );
        bubbleParams.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        bubbleParams.x = 0;
        bubbleParams.y = 120;

        bubbleOverlay.setVisibility(View.GONE);
        windowManager.addView(bubbleOverlay, bubbleParams);

        // Touch listener on bubble — dismiss on outside touch
        bubbleOverlay.setOnTouchListener((v, event) -> {
            if (event.getActionMasked() == MotionEvent.ACTION_OUTSIDE) {
                hideBubble();
                return true;
            }
            return false;
        });
    }

    // Called from C++ via JNI with robot position "x,y,size"
    public void onRobotPosition(String posStr) {
        try {
            String[] parts = posStr.split(",");
            if (parts.length < 3) return;
            final float x = Float.parseFloat(parts[0]);
            final float y = Float.parseFloat(parts[1]);
            final float size = Float.parseFloat(parts[2]);

            robotScreenX = x;
            robotScreenY = y;
            robotSize = size;

            // Skip if dragging — Java controls position during drag
            if (isDragging) return;

            // Update window size to match robot
            final int winW = (int) (size * 1.6f);
            final int winH = (int) (size * 2.2f);

            android.os.Handler handler = new android.os.Handler(getMainLooper());
            handler.post(() -> {
                if (surfaceView == null || robotParams == null) return;
                robotParams.width = winW;
                robotParams.height = winH;
                // Position window so robot center maps to (x, y)
                robotParams.x = (int) (x - winW / 2.0f);
                robotParams.y = (int) (y - winH / 2.0f);
                try {
                    windowManager.updateViewLayout(surfaceView, robotParams);
                } catch (Exception e) {}
            });
        } catch (Exception e) {}
    }

    private LinearLayout createSpeechBubble() {
        LinearLayout bubble = new LinearLayout(this);
        bubble.setOrientation(LinearLayout.VERTICAL);
        bubble.setBackgroundColor(Color.rgb(25, 25, 30));
        bubble.setPadding(28, 24, 28, 24);
        bubble.setVisibility(View.GONE);

        // Set a max width
        float density = getResources().getDisplayMetrics().density;
        int maxWidth = (int) (320 * density);
        LinearLayout.LayoutParams bParams = new LinearLayout.LayoutParams(
            maxWidth, LinearLayout.LayoutParams.WRAP_CONTENT);
        bubble.setLayoutParams(bParams);

        // Title bar with close button
        LinearLayout titleBar = new LinearLayout(this);
        titleBar.setOrientation(LinearLayout.HORIZONTAL);
        titleBar.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams titleBarParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        titleBarParams.bottomMargin = 16;

        TextView title = new TextView(this);
        title.setText("Ask Argos");
        title.setTextColor(Color.rgb(0, 200, 255));
        title.setTextSize(20f);
        title.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams titleParams = new LinearLayout.LayoutParams(
            0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        titleBar.addView(title, titleParams);

        ImageButton closeBtn = new ImageButton(this);
        closeBtn.setImageResource(android.R.drawable.ic_menu_close_clear_cancel);
        closeBtn.setBackgroundColor(Color.TRANSPARENT);
        closeBtn.setPadding(8, 8, 8, 8);
        LinearLayout.LayoutParams closeParams = new LinearLayout.LayoutParams(
            (int) (40 * density), (int) (40 * density));
        closeBtn.setOnClickListener(v -> hideBubble());
        titleBar.addView(closeBtn, closeParams);

        bubble.addView(titleBar, titleBarParams);

        // Neon blue divider
        View divider = new View(this);
        divider.setBackgroundColor(Color.rgb(0, 180, 255));
        LinearLayout.LayoutParams divParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 3);
        divParams.bottomMargin = 16;
        bubble.addView(divider, divParams);

        // Conversation scroll area
        convoScroll = new ScrollView(this);
        convoScroll.setBackgroundColor(Color.rgb(20, 20, 25));
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, (int) (200 * density));
        convoLayout = new LinearLayout(this);
        convoLayout.setOrientation(LinearLayout.VERTICAL);
        convoLayout.setPadding(20, 20, 20, 20);
        convoScroll.addView(convoLayout);
        bubble.addView(convoScroll, scrollParams);

        // Input bar
        LinearLayout inputBar = new LinearLayout(this);
        inputBar.setOrientation(LinearLayout.HORIZONTAL);
        inputBar.setBackgroundColor(Color.rgb(30, 30, 35));
        inputBar.setPadding(12, 12, 12, 12);
        LinearLayout.LayoutParams inputBarParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        inputBarParams.topMargin = 12;
        bubble.addView(inputBar, inputBarParams);

        inputEdit = new EditText(this);
        inputEdit.setHint("Message Argos...");
        inputEdit.setHintTextColor(Color.rgb(100, 100, 110));
        inputEdit.setTextColor(Color.WHITE);
        inputEdit.setBackgroundColor(Color.rgb(40, 40, 48));
        inputEdit.setPadding(24, 16, 24, 16);
        LinearLayout.LayoutParams editParams = new LinearLayout.LayoutParams(
            0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        inputBar.addView(inputEdit, editParams);

        sendBtn = new ImageButton(this);
        sendBtn.setImageResource(android.R.drawable.ic_menu_send);
        sendBtn.setBackgroundColor(Color.rgb(0, 120, 215));
        sendBtn.setPadding(24, 24, 24, 24);
        LinearLayout.LayoutParams btnParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.MATCH_PARENT);
        btnParams.setMargins(12, 0, 0, 0);
        inputBar.addView(sendBtn, btnParams);

        sendBtn.setOnClickListener(v -> sendMessage());
        inputEdit.setOnEditorActionListener((v, actionId, event) -> {
            sendMessage();
            return true;
        });

        return bubble;
    }

    // Called from C++ via JNI when robot head is tapped
    public void onHeadTap() {
        android.os.Handler handler = new android.os.Handler(getMainLooper());
        handler.post(() -> {
            if (bubbleVisible) {
                hideBubble();
            } else {
                showBubble();
            }
        });
    }

    // Called from C++ via JNI when robot body is tapped (walk)
    public void onBodyTap() {
        android.os.Handler handler = new android.os.Handler(getMainLooper());
        handler.post(() -> {
            if (bubbleVisible) {
                hideBubble();
            }
        });
    }

    private void showBubble() {
        // Position bubble near robot
        bubbleParams.gravity = Gravity.TOP | Gravity.START;
        bubbleParams.x = (int) Math.max(0, robotScreenX - 160);
        bubbleParams.y = (int) Math.max(0, robotScreenY - robotSize * 2.5f);

        bubbleOverlay.setVisibility(View.VISIBLE);
        bubbleVisible = true;
        // Remove FLAG_NOT_FOCUSABLE so EditText can receive focus and keyboard
        // Keep FLAG_WATCH_OUTSIDE_TOUCH so clicking outside dismisses
        bubbleParams.flags &= ~WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        bubbleParams.flags |= WindowManager.LayoutParams.FLAG_WATCH_OUTSIDE_TOUCH;
        try {
            windowManager.updateViewLayout(bubbleOverlay, bubbleParams);
        } catch (Exception e) {}
        // Focus the input field to bring up keyboard
        inputEdit.requestFocus();
        android.os.Handler h = new android.os.Handler(getMainLooper());
        h.postDelayed(() -> {
            android.view.inputmethod.InputMethodManager imm =
                (android.view.inputmethod.InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.showSoftInput(inputEdit, android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT);
            }
        }, 100);
    }

    private void hideBubble() {
        // Hide keyboard first
        android.view.inputmethod.InputMethodManager imm =
            (android.view.inputmethod.InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (imm != null && inputEdit != null) {
            imm.hideSoftInputFromWindow(inputEdit.getWindowToken(), 0);
        }
        bubbleOverlay.setVisibility(View.GONE);
        bubbleVisible = false;
        // Add FLAG_NOT_FOCUSABLE back so touches pass through
        bubbleParams.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        try {
            windowManager.updateViewLayout(bubbleOverlay, bubbleParams);
        } catch (Exception e) {}
    }

    private void sendMessage() {
        if (chatInProgress) return;
        String text = inputEdit.getText().toString().trim();
        if (TextUtils.isEmpty(text)) return;

        chatInProgress = true;
        inputEdit.setText("");
        sendBtn.setEnabled(false);

        addMessage("You: " + text, Color.rgb(200, 200, 210));
        nativeSendChat(text);
    }

    private void addMessage(String text, int color) {
        android.os.Handler handler = new android.os.Handler(getMainLooper());
        handler.post(() -> {
            TextView tv = new TextView(this);
            tv.setText(text);
            tv.setTextColor(color);
            tv.setTextSize(15f);
            tv.setPadding(0, 12, 0, 12);
            convoLayout.addView(tv);
            convoScroll.post(() -> convoScroll.fullScroll(ScrollView.FOCUS_DOWN));
        });
    }

    // Called from C++ via JNI — shows AI reasoning/thoughts in bubble
    public void onChatThoughts(final String thoughts) {
        android.os.Handler handler = new android.os.Handler(getMainLooper());
        handler.post(() -> {
            int count = convoLayout.getChildCount();
            // Find existing thoughts view or create new one
            for (int i = count - 1; i >= 0; i--) {
                View v = convoLayout.getChildAt(i);
                if (v instanceof TextView) {
                    TextView tv = (TextView) v;
                    if (tv.getTag() != null && tv.getTag().equals("thoughts")) {
                        tv.setText("💭 " + thoughts);
                        convoScroll.post(() -> convoScroll.fullScroll(ScrollView.FOCUS_DOWN));
                        return;
                    }
                }
            }
            // Create new thoughts view
            TextView tv = new TextView(this);
            tv.setText("💭 " + thoughts);
            tv.setTextColor(Color.rgb(180, 180, 200));
            tv.setTextSize(13f);
            tv.setTypeface(tv.getTypeface(), android.graphics.Typeface.ITALIC);
            tv.setPadding(0, 8, 0, 8);
            tv.setTag("thoughts");
            convoLayout.addView(tv);
            convoScroll.post(() -> convoScroll.fullScroll(ScrollView.FOCUS_DOWN));
        });
    }

    // Called from C++ via JNI
    public void onChatResponse(final String response) {
        android.os.Handler handler = new android.os.Handler(getMainLooper());
        handler.post(() -> {
            chatInProgress = false;
            sendBtn.setEnabled(true);
            addMessage("Argos: " + response, Color.rgb(100, 200, 255));
        });
    }

    // Called from C++ via JNI
    public void onChatError(final String error) {
        android.os.Handler handler = new android.os.Handler(getMainLooper());
        handler.post(() -> {
            chatInProgress = false;
            sendBtn.setEnabled(true);
            addMessage("Error: " + error, Color.rgb(255, 100, 100));
        });
    }

    // Called from C++ via JNI
    public void onChatStream(final String delta) {
        android.os.Handler handler = new android.os.Handler(getMainLooper());
        handler.post(() -> {
            int count = convoLayout.getChildCount();
            for (int i = count - 1; i >= 0; i--) {
                View v = convoLayout.getChildAt(i);
                if (v instanceof TextView) {
                    TextView tv = (TextView) v;
                    if (tv.getTag() != null && tv.getTag().equals("thoughts")) {
                        // Thoughts view found — add response after it
                        break;
                    }
                    String current = tv.getText().toString();
                    if (current.startsWith("Argos: ")) {
                        tv.setText("Argos: " + delta);
                        convoScroll.post(() -> convoScroll.fullScroll(ScrollView.FOCUS_DOWN));
                        return;
                    }
                }
            }
            addMessage("Argos: " + delta, Color.rgb(100, 200, 255));
        });
    }

    // SurfaceHolder.Callback
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        nativeInit(surfaceView, (float) screenWidth, (float) screenHeight);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeDestroy();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // Start as foreground service to keep alive
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                "argos_robot", "Argos Robot", NotificationManager.IMPORTANCE_LOW);
            NotificationManager nm = getSystemService(NotificationManager.class);
            if (nm != null) nm.createNotificationChannel(channel);
        }

        Notification notification = null;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            notification = new Notification.Builder(this, "argos_robot")
                .setContentTitle("Argos is running")
                .setContentText("Robot is floating on screen")
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setOngoing(true)
                .build();
        } else {
            notification = new Notification.Builder(this)
                .setContentTitle("Argos is running")
                .setContentText("Robot is floating on screen")
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setOngoing(true)
                .build();
        }

        startForeground(1, notification);

        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        nativeDestroy();
        if (surfaceView != null) {
            windowManager.removeView(surfaceView);
            surfaceView = null;
        }
        if (bubbleOverlay != null) {
            windowManager.removeView(bubbleOverlay);
            bubbleOverlay = null;
        }
        instance = null;
    }

    public static FloatingRobotService getInstance() {
        return instance;
    }

    // Called from C++ via JNI to perform HTTP POST (handles HTTPS automatically)
    public String httpPostJava(String url, String headers, String body, boolean stream) {
        try {
            java.net.URL urlObj = new java.net.URL(url);
            javax.net.ssl.HttpsURLConnection.setDefaultHostnameVerifier((hostname, session) -> true);
            java.net.HttpURLConnection conn = (java.net.HttpURLConnection) urlObj.openConnection();
            conn.setRequestMethod("POST");
            conn.setRequestProperty("Content-Type", "application/json");
            if (stream) {
                conn.setRequestProperty("Accept", "text/event-stream");
            }
            // Parse custom headers
            for (String line : headers.split("\r\n")) {
                int colon = line.indexOf(':');
                if (colon > 0) {
                    String key = line.substring(0, colon).trim();
                    String val = line.substring(colon + 1).trim();
                    if (!key.isEmpty()) {
                        conn.setRequestProperty(key, val);
                    }
                }
            }
            conn.setDoOutput(true);
            conn.setConnectTimeout(30000);
            conn.setReadTimeout(30000);
            conn.setInstanceFollowRedirects(true);

            // Send body
            java.io.OutputStream os = conn.getOutputStream();
            os.write(body.getBytes("UTF-8"));
            os.flush();
            os.close();

            int status = conn.getResponseCode();
            android.util.Log.i("Argos", "Java HTTP POST " + url + " -> " + status);

            // Read response
            java.io.InputStream is;
            if (status >= 200 && status < 300) {
                is = conn.getInputStream();
            } else {
                is = conn.getErrorStream();
                if (is == null) is = conn.getInputStream();
            }

            java.io.ByteArrayOutputStream baos = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = is.read(buf)) > 0) {
                baos.write(buf, 0, n);
            }
            is.close();
            conn.disconnect();

            String response = new String(baos.toByteArray(), "UTF-8");
            if (status < 200 || status >= 300) {
                return "[Error: HTTP " + status + ": " + response.substring(0, Math.min(200, response.length())) + "]";
            }
            return response;
        } catch (Exception e) {
            android.util.Log.e("Argos", "Java HTTP error: " + e.getMessage());
            return "[Error: " + e.getClass().getSimpleName() + ": " + e.getMessage() + "]";
        }
    }

    // Native methods
    private native void nativeInit(SurfaceView surfaceView, float screenWidth, float screenHeight);
    private native void nativeSendChat(String message);
    private native void nativeResume();
    private native void nativePause();
    private native void nativeDestroy();
    private native void nativeOnTouch(float x, float y, int action);
    private native void nativeSetPosition(float x, float y);
}
