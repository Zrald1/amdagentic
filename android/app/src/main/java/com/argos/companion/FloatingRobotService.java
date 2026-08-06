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
        int screenWidth = metrics.widthPixels;
        int screenHeight = metrics.heightPixels;

        // Robot window size — just big enough for the robot, not full screen
        // This allows touches to pass through to apps outside the robot area
        float density = metrics.density;
        int robotWindowWidth = (int) (250 * density);
        int robotWindowHeight = (int) (320 * density);

        // SurfaceView for robot rendering — small window, transparent
        surfaceView = new SurfaceView(this);
        surfaceView.setZOrderOnTop(true);
        surfaceView.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        surfaceView.getHolder().addCallback(this);

        layoutType = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O ?
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY :
            WindowManager.LayoutParams.TYPE_PHONE;

        // Small overlay window — only covers robot area, touches outside pass through
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
        robotParams.y = (screenHeight - robotWindowHeight) / 2;

        windowManager.addView(surfaceView, robotParams);

        // Touch listener on the surface — forward to native in window-local coords
        surfaceView.setOnTouchListener((v, event) -> {
            float x = event.getX();
            float y = event.getY();
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                nativeOnTouch(x, y, 0);
            } else if (action == MotionEvent.ACTION_UP) {
                nativeOnTouch(x, y, 1);
            } else if (action == MotionEvent.ACTION_MOVE) {
                nativeOnTouch(x, y, 2);
            }
            return true;
        });

        // Speech bubble overlay — hidden by default
        bubbleOverlay = createSpeechBubble();

        bubbleParams = new WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            layoutType,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        );
        bubbleParams.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        bubbleParams.x = 0;
        bubbleParams.y = 120;

        bubbleOverlay.setVisibility(View.GONE);
        windowManager.addView(bubbleOverlay, bubbleParams);
    }

    // Called from C++ via JNI with robot position "x,y,size"
    // Robot walks within the fixed window — no window movement needed
    public void onRobotPosition(String posStr) {
        // No-op: robot renders within the small fixed window
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

        // Title bar
        TextView title = new TextView(this);
        title.setText("Ask Argos");
        title.setTextColor(Color.rgb(0, 200, 255));
        title.setTextSize(20f);
        title.setPadding(0, 0, 0, 16);
        title.setGravity(Gravity.CENTER);
        bubble.addView(title);

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
        bubbleOverlay.setVisibility(View.VISIBLE);
        bubbleVisible = true;
        // Remove FLAG_NOT_FOCUSABLE so EditText can receive focus and keyboard
        bubbleParams.flags &= ~WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
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
            if (count > 0) {
                View last = convoLayout.getChildAt(count - 1);
                if (last instanceof TextView) {
                    TextView tv = (TextView) last;
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
        nativeInit(surfaceView);
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

    // Native methods
    private native void nativeInit(SurfaceView surfaceView);
    private native void nativeSendChat(String message);
    private native void nativeResume();
    private native void nativePause();
    private native void nativeDestroy();
    private native void nativeOnTouch(float x, float y, int action);
}
