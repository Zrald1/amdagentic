package com.argos.companion;

import android.app.Activity;
import android.os.Bundle;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.view.ViewGroup;
import android.graphics.Color;
import android.text.TextUtils;
import android.widget.Toast;

public class MainActivity extends Activity implements SurfaceHolder.Callback {

    private SurfaceView surfaceView;
    private EditText inputEdit;
    private ImageButton sendBtn;
    private LinearLayout convoLayout;
    private ScrollView convoScroll;
    private boolean chatInProgress = false;
    private boolean nativeReady = false;

    static {
        System.loadLibrary("argos");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);

        // SurfaceView for the robot rendering (takes ~60% of screen)
        surfaceView = new SurfaceView(this);
        LinearLayout.LayoutParams surfaceParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 6f);
        root.addView(surfaceView, surfaceParams);

        // Conversation scroll area
        convoScroll = new ScrollView(this);
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 3f);
        convoScroll.setBackgroundColor(Color.rgb(20, 20, 25));
        convoLayout = new LinearLayout(this);
        convoLayout.setOrientation(LinearLayout.VERTICAL);
        convoLayout.setPadding(24, 24, 24, 24);
        convoScroll.addView(convoLayout);
        root.addView(convoScroll, scrollParams);

        // Input bar
        LinearLayout inputBar = new LinearLayout(this);
        inputBar.setOrientation(LinearLayout.HORIZONTAL);
        inputBar.setBackgroundColor(Color.rgb(30, 30, 35));
        inputBar.setPadding(16, 12, 16, 12);

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

        LinearLayout.LayoutParams inputParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        root.addView(inputBar, inputParams);

        setContentView(root);

        // Register for surface callbacks — nativeInit will be called
        // when the surface is actually created
        surfaceView.getHolder().addCallback(this);

        // Forward touch events to native renderer
        surfaceView.setOnTouchListener((v, event) -> {
            float x = event.getX();
            float y = event.getY();
            int action = event.getActionMasked();
            // Map Android action constants: DOWN=0, UP=1, MOVE=2
            if (action == android.view.MotionEvent.ACTION_DOWN) {
                nativeOnTouch(x, y, 0);
            } else if (action == android.view.MotionEvent.ACTION_UP) {
                nativeOnTouch(x, y, 1);
            } else if (action == android.view.MotionEvent.ACTION_MOVE) {
                nativeOnTouch(x, y, 2);
            }
            return true;
        });

        sendBtn.setOnClickListener(v -> sendMessage());
        inputEdit.setOnEditorActionListener((v, actionId, event) -> {
            sendMessage();
            return true;
        });
    }

    private void sendMessage() {
        if (chatInProgress) return;
        String text = inputEdit.getText().toString().trim();
        if (TextUtils.isEmpty(text)) return;

        chatInProgress = true;
        inputEdit.setText("");
        sendBtn.setEnabled(false);

        // Add user message to conversation
        addMessage("You: " + text, Color.rgb(200, 200, 210));

        // Send to AI via native code
        nativeSendChat(text);
    }

    private void addMessage(String text, int color) {
        runOnUiThread(() -> {
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
        runOnUiThread(() -> {
            chatInProgress = false;
            sendBtn.setEnabled(true);
            addMessage("Argos: " + response, Color.rgb(100, 200, 255));
        });
    }

    // Called from C++ via JNI
    public void onChatError(final String error) {
        runOnUiThread(() -> {
            chatInProgress = false;
            sendBtn.setEnabled(true);
            addMessage("Error: " + error, Color.rgb(255, 100, 100));
        });
    }

    // Called from C++ via JNI
    public void onChatStream(final String delta) {
        runOnUiThread(() -> {
            // Update last Argos message or create new one
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

    @Override
    protected void onResume() {
        super.onResume();
        nativeResume();
    }

    @Override
    protected void onPause() {
        super.onPause();
        nativePause();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        nativeDestroy();
    }

    // SurfaceHolder.Callback
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // Surface is now valid — safe to initialize native renderer
        nativeInit(surfaceView);
        nativeReady = true;
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        // Pass through to native if needed
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeReady = false;
        // Stop render thread and release EGL resources
        // nativeInit will be called again when surface is recreated
        nativeDestroy();
    }

    // Native methods
    private native void nativeInit(SurfaceView surfaceView);
    private native void nativeSendChat(String message);
    private native void nativeResume();
    private native void nativePause();
    private native void nativeDestroy();
    private native void nativeOnTouch(float x, float y, int action);
}
