package com.argos.companion;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.graphics.Color;
import android.view.Gravity;
import android.view.ViewGroup;

public class MainActivity extends Activity {

    private static final int OVERLAY_PERMISSION_REQUEST_CODE = 1001;

    static {
        System.loadLibrary("argos");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (hasOverlayPermission()) {
            startFloatingService();
            finish();
        } else {
            requestOverlayPermission();
        }
    }

    private boolean hasOverlayPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            return Settings.canDrawOverlays(this);
        }
        return true;
    }

    private void requestOverlayPermission() {
        // Show a simple screen explaining what's needed
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(15, 15, 20));
        root.setGravity(Gravity.CENTER);
        root.setPadding(48, 48, 48, 48);

        TextView title = new TextView(this);
        title.setText("Argos needs overlay permission");
        title.setTextColor(Color.rgb(0, 200, 255));
        title.setTextSize(22f);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 0, 0, 24);
        root.addView(title);

        TextView desc = new TextView(this);
        desc.setText("Argos floats on top of your screen while you use other apps. Please grant the 'Display over other apps' permission to continue.");
        desc.setTextColor(Color.rgb(200, 200, 210));
        desc.setTextSize(16f);
        desc.setGravity(Gravity.CENTER);
        desc.setPadding(0, 0, 0, 32);
        root.addView(desc);

        TextView btn = new TextView(this);
        btn.setText("Grant Permission");
        btn.setTextColor(Color.WHITE);
        btn.setTextSize(18f);
        btn.setGravity(Gravity.CENTER);
        btn.setBackgroundColor(Color.rgb(0, 120, 215));
        btn.setPadding(48, 32, 48, 32);
        LinearLayout.LayoutParams btnParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        root.addView(btn, btnParams);

        btn.setOnClickListener(v -> {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                Intent intent = new Intent(
                    Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
                startActivityForResult(intent, OVERLAY_PERMISSION_REQUEST_CODE);
            }
        });

        setContentView(root);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == OVERLAY_PERMISSION_REQUEST_CODE) {
            if (hasOverlayPermission()) {
                startFloatingService();
                finish();
            } else {
                // Permission not granted — show again
                requestOverlayPermission();
            }
        }
    }

    private void startFloatingService() {
        Intent intent = new Intent(this, FloatingRobotService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
    }
}

