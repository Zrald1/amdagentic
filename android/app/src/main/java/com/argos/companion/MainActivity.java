package com.argos.companion;

import android.app.Activity;
import android.os.Bundle;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.graphics.Color;
import android.text.TextUtils;

public class MainActivity extends Activity implements SurfaceHolder.Callback {

    private SurfaceView surfaceView;
    private EditText inputEdit;
    private ImageButton sendBtn;
    private LinearLayout convoLayout;
    private ScrollView convoScroll;
    private LinearLayout bubbleOverlay;
    private boolean chatInProgress = false;
    private boolean nativeReady = false;
    private boolean bubbleVisible = false;

    static {
        System.loadLibrary("argos");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Full-screen SurfaceView — robot walks here
        surfaceView = new SurfaceView(this);

        // Speech bubble overlay — hidden by default, shown when head is tapped
        bubbleOverlay = createSpeechBubble();

        // Root layout containing robot + bubble overlay
        android.widget.FrameLayout root = new android.widget.FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        root.addView(surfaceView, new android.widget.FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // Position bubble in upper portion of screen
        android.widget.FrameLayout.LayoutParams bubbleParams = new android.widget.FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        bubbleParams.gravity = Gravity.TOP;
        bubbleParams.setMargins(40, 60, 40, 0);
        root.addView(bubbleOverlay, bubbleParams);

        setContentView(root);

        // Register for surface callbacks — nativeInit will be called
        // when the surface is actually created
        surfaceView.getHolder().addCallback(this);

        // Forward touch events to native renderer
        surfaceView.setOnTouchListener((v, event) -> {
            float x = event.getX();
            float y = event.getY();
            int action = event.getActionMasked();
            if (action == android.view.MotionEvent.ACTION_DOWN) {
                nativeOnTouch(x, y, 0);
            } else if (action == android.view.MotionEvent.ACTION_UP) {
                nativeOnTouch(x, y, 1);
            } else if (action == android.view.MotionEvent.ACTION_MOVE) {
                nativeOnTouch(x, y, 2);
            }
            return true;
        });
    }

    private LinearLayout createSpeechBubble() {
        LinearLayout bubble = new LinearLayout(this);
        bubble.setOrientation(LinearLayout.VERTICAL);
        bubble.setBackgroundColor(Color.rgb(25, 25, 30));
        bubble.setPadding(28, 24, 28, 24);
        bubble.setVisibility(View.GONE);

        // Title bar — "Ask Argos" in neon blue
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
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
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
        runOnUiThread(() -> {
            if (bubbleVisible) {
                bubbleOverlay.setVisibility(View.GONE);
                bubbleVisible = false;
            } else {
                bubbleOverlay.setVisibility(View.VISIBLE);
                bubbleVisible = true;
                if (inputEdit != null) inputEdit.requestFocus();
            }
        });
    }

    // Called from C++ via JNI when robot body is tapped (walk)
    public void onBodyTap() {
        runOnUiThread(() -> {
            if (bubbleVisible) {
                bubbleOverlay.setVisibility(View.GONE);
                bubbleVisible = false;
            }
        });
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
        nativeInit(surfaceView);
        nativeReady = true;
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeReady = false;
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
