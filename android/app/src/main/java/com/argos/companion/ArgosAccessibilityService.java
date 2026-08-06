package com.argos.companion;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.AccessibilityServiceInfo;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;

public class ArgosAccessibilityService extends AccessibilityService {

    private static final String TAG = "Argos";
    private static ArgosAccessibilityService instance;
    private static String currentApp = "";
    private static String currentAppLabel = "";

    @Override
    public void onServiceConnected() {
        super.onServiceConnected();
        instance = this;
        Log.i(TAG, "ArgosAccessibilityService connected");

        AccessibilityServiceInfo info = getServiceInfo();
        if (info == null) info = new AccessibilityServiceInfo();
        info.eventTypes = AccessibilityEvent.TYPES_ALL_MASK;
        info.feedbackType = AccessibilityServiceInfo.FEEDBACK_GENERIC;
        info.flags |= AccessibilityServiceInfo.FLAG_REPORT_VIEW_IDS;
        info.flags |= AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS;
        info.flags |= AccessibilityServiceInfo.FLAG_REQUEST_TOUCH_EXPLORATION_MODE;
        info.notificationTimeout = 100;
        setServiceInfo(info);
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        // Detect app switches via TYPE_WINDOW_STATE_CHANGED
        if (event.getEventType() == AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) {
            String pkg = event.getPackageName() != null ? event.getPackageName().toString() : "";
            if (pkg.isEmpty() || pkg.equals("com.argos.companion")) return;
            // Ignore system UI packages
            if (pkg.startsWith("com.android.systemui") || pkg.startsWith("android")) return;

            if (!pkg.equals(currentApp)) {
                currentApp = pkg;
                currentAppLabel = getAppLabel(pkg);
                Log.i(TAG, "App switched to: " + pkg + " (" + currentAppLabel + ")");

                // Notify FloatingRobotService
                FloatingRobotService svc = FloatingRobotService.getInstance();
                if (svc != null) {
                    svc.onAppChanged(currentAppLabel);
                }
            }
        }
    }

    // Get friendly app name from package
    private String getAppLabel(String pkg) {
        try {
            android.content.pm.PackageManager pm = getPackageManager();
            android.content.pm.ApplicationInfo ai = pm.getApplicationInfo(pkg, 0);
            return (String) pm.getApplicationLabel(ai);
        } catch (Exception e) {
            return pkg;
        }
    }

    public static String getCurrentApp() {
        return currentApp;
    }

    public static String getCurrentAppLabel() {
        return currentAppLabel;
    }

    @Override
    public void onInterrupt() {
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        instance = null;
    }

    public static ArgosAccessibilityService getInstance() {
        return instance;
    }

    // ── Browser / Screen interaction methods (called from C++ via FloatingRobotService) ──

    // Open a URL in the default browser
    public String openUrl(String url) {
        try {
            if (!url.startsWith("http://") && !url.startsWith("https://")) {
                url = "https://" + url;
            }
            Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(intent);
            return "{\"status\":\"Opening URL: " + url + "\"}";
        } catch (Exception e) {
            return "{\"error\":\"Failed to open URL: " + e.getMessage() + "\"}";
        }
    }

    // Get all text content currently visible on screen
    public String getScreenText() {
        try {
            AccessibilityNodeInfo root = getRootInActiveWindow();
            if (root == null) {
                return "{\"error\":\"No active window content available\"}";
            }
            StringBuilder sb = new StringBuilder();
            List<AccessibilityNodeInfo> visited = new ArrayList<>();
            collectText(root, sb, visited, 0);
            String text = sb.toString().trim();
            if (text.isEmpty()) {
                return "{\"text\":\"\",\"error\":\"No text content found on screen\"}";
            }
            // Truncate to reasonable size for AI consumption
            if (text.length() > 8000) {
                text = text.substring(0, 8000) + "\n...(truncated)";
            }
            return "{\"text\":\"" + escapeJson(text) + "\"}";
        } catch (Exception e) {
            return "{\"error\":\"Failed to get screen text: " + e.getMessage() + "\"}";
        }
    }

    // Get the package name of the currently focused app
    public String getActiveApp() {
        try {
            AccessibilityNodeInfo root = getRootInActiveWindow();
            if (root != null && root.getPackageName() != null) {
                String pkg = root.getPackageName().toString();
                String className = root.getClassName() != null ? root.getClassName().toString() : "";
                return "{\"package\":\"" + escapeJson(pkg) + "\",\"class\":\"" + escapeJson(className) + "\"}";
            }
            return "{\"error\":\"No active window\"}";
        } catch (Exception e) {
            return "{\"error\":\"Failed to get active app: " + e.getMessage() + "\"}";
        }
    }

    // Click on the first element containing the given text
    public String clickText(String text) {
        try {
            AccessibilityNodeInfo root = getRootInActiveWindow();
            if (root == null) {
                return "{\"error\":\"No active window content available\"}";
            }
            AccessibilityNodeInfo target = findNodeByText(root, text);
            if (target == null) {
                return "{\"error\":\"Text not found on screen: " + escapeJson(text) + "\"}";
            }
            // Try to click — find clickable parent or the node itself
            AccessibilityNodeInfo clickable = findClickableParent(target);
            if (clickable != null && clickable.performAction(AccessibilityNodeInfo.ACTION_CLICK)) {
                return "{\"status\":\"Clicked on text: " + escapeJson(text) + "\"}";
            }
            // Fallback: try to click the node itself
            if (target.performAction(AccessibilityNodeInfo.ACTION_CLICK)) {
                return "{\"status\":\"Clicked on text: " + escapeJson(text) + "\"}";
            }
            return "{\"error\":\"Found text but could not click: " + escapeJson(text) + "\"}";
        } catch (Exception e) {
            return "{\"error\":\"Failed to click: " + e.getMessage() + "\"}";
        }
    }

    // Type text into the currently focused input field
    public String typeText(String text) {
        try {
            AccessibilityNodeInfo root = getRootInActiveWindow();
            if (root == null) {
                return "{\"error\":\"No active window content available\"}";
            }
            // Find a focused editable field
            AccessibilityNodeInfo focusable = findFocusedEditable(root);
            if (focusable == null) {
                return "{\"error\":\"No focused input field found on screen\"}";
            }
            Bundle args = new Bundle();
            args.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, text);
            if (focusable.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, args)) {
                return "{\"status\":\"Typed text into focused field\"}";
            }
            return "{\"error\":\"Could not type text into focused field\"}";
        } catch (Exception e) {
            return "{\"error\":\"Failed to type: " + e.getMessage() + "\"}";
        }
    }

    // Scroll the current screen
    public String scrollScreen(int direction) {
        try {
            AccessibilityNodeInfo root = getRootInActiveWindow();
            if (root == null) {
                return "{\"error\":\"No active window content available\"}";
            }
            AccessibilityNodeInfo scrollable = findScrollable(root);
            if (scrollable == null) {
                return "{\"error\":\"No scrollable container found on screen\"}";
            }
            int action = (direction == 0)
                ? AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD
                : AccessibilityNodeInfo.ACTION_SCROLL_FORWARD;
            if (scrollable.performAction(action)) {
                return "{\"status\":\"Scrolled " + (direction == 0 ? "up" : "down") + "\"}";
            }
            return "{\"error\":\"Scroll action failed\"}";
        } catch (Exception e) {
            return "{\"error\":\"Failed to scroll: " + e.getMessage() + "\"}";
        }
    }

    // ── Helper methods ──

    private void collectText(AccessibilityNodeInfo node, StringBuilder sb,
                             List<AccessibilityNodeInfo> visited, int depth) {
        if (node == null || visited.contains(node)) return;
        visited.add(node);

        CharSequence text = node.getText();
        if (text != null && text.length() > 0) {
            String t = text.toString().trim();
            if (!t.isEmpty()) {
                sb.append(t).append("\n");
            }
        }
        CharSequence desc = node.getContentDescription();
        if (desc != null && desc.length() > 0) {
            String d = desc.toString().trim();
            if (!d.isEmpty() && (text == null || !d.equals(text.toString().trim()))) {
                sb.append("[desc: ").append(d).append("]\n");
            }
        }

        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo child = node.getChild(i);
            if (child != null) {
                collectText(child, sb, visited, depth + 1);
            }
        }
    }

    private AccessibilityNodeInfo findNodeByText(AccessibilityNodeInfo node, String text) {
        if (node == null) return null;
        CharSequence nodeText = node.getText();
        if (nodeText != null && nodeText.toString().toLowerCase().contains(text.toLowerCase())) {
            return node;
        }
        // Also search content description
        CharSequence desc = node.getContentDescription();
        if (desc != null && desc.toString().toLowerCase().contains(text.toLowerCase())) {
            return node;
        }
        // Use built-in text search
        List<AccessibilityNodeInfo> matches = node.findAccessibilityNodeInfosByText(text);
        if (matches != null && !matches.isEmpty()) {
            return matches.get(0);
        }
        // Recurse children
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo child = node.getChild(i);
            if (child != null) {
                AccessibilityNodeInfo found = findNodeByText(child, text);
                if (found != null) return found;
            }
        }
        return null;
    }

    private AccessibilityNodeInfo findClickableParent(AccessibilityNodeInfo node) {
        AccessibilityNodeInfo current = node;
        while (current != null) {
            if (current.isClickable()) return current;
            current = current.getParent();
        }
        return null;
    }

    private AccessibilityNodeInfo findFocusedEditable(AccessibilityNodeInfo node) {
        if (node == null) return null;
        if (node.isEditable() && node.isFocused()) {
            return node;
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo child = node.getChild(i);
            if (child != null) {
                AccessibilityNodeInfo found = findFocusedEditable(child);
                if (found != null) return found;
            }
        }
        return null;
    }

    private AccessibilityNodeInfo findScrollable(AccessibilityNodeInfo node) {
        if (node == null) return null;
        if (node.isScrollable()) return node;
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo child = node.getChild(i);
            if (child != null) {
                AccessibilityNodeInfo found = findScrollable(child);
                if (found != null) return found;
            }
        }
        return null;
    }

    private String escapeJson(String s) {
        StringBuilder sb = new StringBuilder();
        for (char c : s.toCharArray()) {
            switch (c) {
                case '"':  sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:   sb.append(c);
            }
        }
        return sb.toString();
    }
}
