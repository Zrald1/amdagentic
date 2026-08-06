package com.argos.companion;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.AccessibilityServiceInfo;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;
import android.util.Log;

import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;

public class ArgosAccessibilityService extends AccessibilityService {

    private static final String TAG = "Argos";
    private static ArgosAccessibilityService instance;
    private static String currentApp = "";
    private static String currentAppLabel = "";
    // Track recently used apps (most recent first, max 10)
    private static final LinkedList<String> appHistory = new LinkedList<>();
    private static final int MAX_HISTORY = 10;

    // Packages to ignore (keyboards, IMEs, system UI, launchers that aren't real apps)
    private static boolean isIgnoredPackage(String pkg) {
        if (pkg == null || pkg.isEmpty()) return true;
        if (pkg.equals("com.argos.companion")) return true;
        // System UI
        if (pkg.startsWith("com.android.systemui")) return true;
        if (pkg.startsWith("android")) return true;
        // Keyboards / IMEs
        if (pkg.contains("inputmethod")) return true;
        if (pkg.contains("ime")) return true;
        if (pkg.equals("com.google.android.inputmethod.latin")) return true;
        if (pkg.equals("com.android.inputmethod.latin")) return true;
        if (pkg.startsWith("com.samsung.android.inputmethod")) return true;
        if (pkg.startsWith("com.swiftkey")) return true;
        // Common launcher packages
        if (pkg.equals("com.android.launcher")) return true;
        if (pkg.equals("com.android.launcher3")) return true;
        if (pkg.contains("launcher")) return true;
        // Notification shade / recents
        if (pkg.contains("notification")) return true;
        if (pkg.contains("recents")) return true;
        return false;
    }

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
        int type = event.getEventType();
        // Detect app switches via TYPE_WINDOW_STATE_CHANGED
        if (type == AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) {
            String pkg = event.getPackageName() != null ? event.getPackageName().toString() : "";
            if (isIgnoredPackage(pkg)) return;

            if (!pkg.equals(currentApp)) {
                currentApp = pkg;
                currentAppLabel = getAppLabel(pkg);
                Log.i(TAG, "App switched to: " + pkg + " (" + currentAppLabel + ")");

                // Add to history (avoid consecutive duplicates)
                synchronized (appHistory) {
                    if (appHistory.isEmpty() || !appHistory.getFirst().equals(currentAppLabel)) {
                        appHistory.addFirst(currentAppLabel);
                        if (appHistory.size() > MAX_HISTORY) appHistory.removeLast();
                    }
                }

                // Notify FloatingRobotService
                FloatingRobotService svc = FloatingRobotService.getInstance();
                if (svc != null) {
                    svc.onAppChanged(currentAppLabel);
                }
            }
        }
    }

    // Get the root node of the actual app window (not keyboard/IME)
    private AccessibilityNodeInfo getRealAppRoot() {
        // Try getWindows() first — allows us to filter out IME
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            List<AccessibilityWindowInfo> windows = getWindows();
            if (windows != null && !windows.isEmpty()) {
                // Find an application window (not IME, not system)
                for (AccessibilityWindowInfo win : windows) {
                    if (win.getType() == AccessibilityWindowInfo.TYPE_APPLICATION) {
                        AccessibilityNodeInfo root = win.getRoot();
                        if (root != null) {
                            String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
                            if (!isIgnoredPackage(pkg)) {
                                return root;
                            }
                        }
                    }
                }
                // Fallback: try any non-IME window
                for (AccessibilityWindowInfo win : windows) {
                    if (win.getType() != AccessibilityWindowInfo.TYPE_INPUT_METHOD) {
                        AccessibilityNodeInfo root = win.getRoot();
                        if (root != null) {
                            String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
                            if (!isIgnoredPackage(pkg)) {
                                return root;
                            }
                        }
                    }
                }
            }
        }
        // Final fallback: getRootInActiveWindow (may return IME)
        AccessibilityNodeInfo root = getRootInActiveWindow();
        if (root != null) {
            String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
            if (!isIgnoredPackage(pkg)) {
                return root;
            }
        }
        return null;
    }

    // Get friendly app name from package — never returns raw package name
    private String getAppLabel(String pkg) {
        // First try PackageManager
        try {
            android.content.pm.PackageManager pm = getPackageManager();
            android.content.pm.ApplicationInfo ai = pm.getApplicationInfo(pkg, 0);
            String label = (String) pm.getApplicationLabel(ai);
            if (label != null && !label.isEmpty() && !label.equals(pkg)) {
                return label;
            }
        } catch (Exception e) {
            // Fall through to extraction
        }

        // Fallback: extract a clean name from the package name
        // e.g. "com.whatsapp" -> "WhatsApp", "com.instagram.android" -> "Instagram"
        return cleanPackageName(pkg);
    }

    // Extract a human-readable name from a package name
    private static String cleanPackageName(String pkg) {
        if (pkg == null || pkg.isEmpty()) return "Unknown";

        // Split by dot and take the most meaningful part
        String[] parts = pkg.split("\\.");
        // Skip common prefixes: com, org, net, io, app, android, google, samsung, miui, etc.
        java.util.Set<String> skip = new java.util.HashSet<>(java.util.Arrays.asList(
            "com", "org", "net", "io", "app", "android", "google", "samsung",
            "miui", "huawei", "xiaomi", "oppo", "vivo", "realme", "lge", "motorola",
            "amazon", "facebook", "microsoft", "adobe", "intellij", "jetbrains",
            "whatsapp", "llc", "inc", "co", "uk", "cn", "de", "fr", "jp", "kr",
            "the", "my", "mobile", "app", "application", "client", "lite", "web"
        ));

        String bestPart = "";
        for (int i = parts.length - 1; i >= 0; i--) {
            String part = parts[i].toLowerCase();
            if (!skip.contains(part) && part.length() >= 2) {
                bestPart = parts[i];
                break;
            }
        }

        if (bestPart.isEmpty() && parts.length > 0) {
            bestPart = parts[parts.length - 1];
        }

        // Capitalize first letter
        if (bestPart.length() > 0) {
            bestPart = bestPart.substring(0, 1).toUpperCase() + bestPart.substring(1);
        }

        // Fix common known apps
        String lower = pkg.toLowerCase();
        if (lower.contains("whatsapp")) return "WhatsApp";
        if (lower.contains("instagram")) return "Instagram";
        if (lower.contains("facebook") || lower.contains("fbandroid")) return "Facebook";
        if (lower.contains("messenger") || lower.contains("orca")) return "Messenger";
        if (lower.contains("twitter") || lower.contains("com.twitter.android")) return "Twitter";
        if (lower.contains("tiktok") || lower.contains("musical")) return "TikTok";
        if (lower.contains("snapchat")) return "Snapchat";
        if (lower.contains("telegram")) return "Telegram";
        if (lower.contains("chrome")) return "Chrome";
        if (lower.contains("firefox")) return "Firefox";
        if (lower.contains("youtube")) return "YouTube";
        if (lower.contains("gmail") || lower.contains("google.android.gm")) return "Gmail";
        if (lower.contains("spotify")) return "Spotify";
        if (lower.contains("netflix")) return "Netflix";
        if (lower.contains("discord")) return "Discord";
        if (lower.contains("reddit")) return "Reddit";
        if (lower.contains("amazon.mShop")) return "Amazon";
        if (lower.contains("paypal")) return "PayPal";
        if (lower.contains("linkedin")) return "LinkedIn";
        if (lower.contains("zoom")) return "Zoom";
        if (lower.contains("teams")) return "Teams";
        if (lower.contains("slack")) return "Slack";
        if (lower.contains("chrome")) return "Chrome";
        if (lower.contains("browser") || lower.contains("org.mozilla.firefox")) return "Browser";
        if (lower.contains("settings")) return "Settings";
        if (lower.contains("calculator")) return "Calculator";
        if (lower.contains("calendar")) return "Calendar";
        if (lower.contains("camera")) return "Camera";
        if (lower.contains("gallery") || lower.contains("photos")) return "Gallery";
        if (lower.contains("music")) return "Music";
        if (lower.contains("clock")) return "Clock";
        if (lower.contains("weather")) return "Weather";
        if (lower.contains("maps")) return "Maps";
        if (lower.contains("play.store") || lower.contains("com.android.vending")) return "Play Store";
        if (lower.contains("phone") || lower.contains("dialer")) return "Phone";
        if (lower.contains("messages") || lower.contains("messaging")) return "Messages";
        if (lower.contains("contacts")) return "Contacts";
        if (lower.contains("email") || lower.contains("mail")) return "Email";
        if (lower.contains("files") || lower.contains("filemanager")) return "Files";
        if (lower.contains("notes")) return "Notes";

        return bestPart.isEmpty() ? "Unknown App" : bestPart;
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
            AccessibilityNodeInfo root = getRealAppRoot();
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

    // Get the package name of the currently focused app (skips keyboard/IME)
    public String getActiveApp() {
        try {
            AccessibilityNodeInfo root = getRealAppRoot();
            if (root != null && root.getPackageName() != null) {
                String pkg = root.getPackageName().toString();
                String className = root.getClassName() != null ? root.getClassName().toString() : "";
                String label = getAppLabel(pkg);
                // Build app history string
                String historyStr = "";
                synchronized (appHistory) {
                    StringBuilder hs = new StringBuilder();
                    for (int i = 0; i < appHistory.size(); i++) {
                        if (i > 0) hs.append(", ");
                        hs.append(appHistory.get(i));
                    }
                    historyStr = hs.toString();
                }
                return "{\"package\":\"" + escapeJson(pkg) + "\",\"name\":\"" + escapeJson(label) + "\",\"class\":\"" + escapeJson(className) + "\",\"recent_apps\":\"" + escapeJson(historyStr) + "\"}";
            }
            // Fallback: use tracked currentApp
            if (!currentApp.isEmpty()) {
                return "{\"package\":\"" + escapeJson(currentApp) + "\",\"name\":\"" + escapeJson(currentAppLabel) + "\"}";
            }
            return "{\"error\":\"No active window\"}";
        } catch (Exception e) {
            return "{\"error\":\"Failed to get active app: " + e.getMessage() + "\"}";
        }
    }

    // Get app history as comma-separated string
    public static String getAppHistory() {
        synchronized (appHistory) {
            if (appHistory.isEmpty()) return "";
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < appHistory.size(); i++) {
                if (i > 0) sb.append(", ");
                sb.append(appHistory.get(i));
            }
            return sb.toString();
        }
    }

    // Click on the first element containing the given text
    public String clickText(String text) {
        try {
            AccessibilityNodeInfo root = getRealAppRoot();
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
            AccessibilityNodeInfo root = getRealAppRoot();
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
            AccessibilityNodeInfo root = getRealAppRoot();
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

    // ── UI Tree Inspection ──
    // Node ID counter for each inspection session
    private int nodeIdCounter = 0;
    private java.util.Map<Integer, AccessibilityNodeInfo> nodeMap = new java.util.HashMap<>();

    // Build a JSON tree of all UI elements. Called from C++ via FloatingRobotService.
    public String getUITree(int maxDepth) {
        // Reset node map for this session
        synchronized (nodeMap) {
            nodeMap.clear();
            nodeIdCounter = 0;
        }

        AccessibilityNodeInfo root = getRealAppRoot();
        if (root == null) {
            return "{\"error\":\"No active window content available\"}";
        }

        String appName = currentAppLabel != null ? currentAppLabel : "";
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";

        StringBuilder sb = new StringBuilder();
        sb.append("{\"app\":\"").append(escapeJson(appName)).append("\"");
        sb.append(",\"package\":\"").append(escapeJson(pkg)).append("\"");
        sb.append(",\"elements\":[");

        buildTreeJson(root, sb, 0, maxDepth);

        sb.append("]}");
        return sb.toString();
    }

    private void buildTreeJson(AccessibilityNodeInfo node, StringBuilder sb, int depth, int maxDepth) {
        if (node == null) return;
        if (maxDepth >= 0 && depth > maxDepth) return;

        // Skip nodes with no text, no desc, no children, and not clickable (noise reduction)
        // But always include root and first-level children
        if (depth > 1) {
            CharSequence text = node.getText();
            CharSequence desc = node.getContentDescription();
            boolean hasContent = (text != null && text.length() > 0) || (desc != null && desc.length() > 0);
            boolean isInteractive = node.isClickable() || node.isLongClickable() || node.isEditable() || node.isScrollable();
            if (!hasContent && !isInteractive && node.getChildCount() == 0) return;
        }

        int id;
        synchronized (nodeMap) {
            id = nodeIdCounter++;
            nodeMap.put(id, node);
        }

        if (id > 0) sb.append(",");

        sb.append("{\"id\":").append(id);

        CharSequence text = node.getText();
        if (text != null && text.length() > 0) {
            sb.append(",\"text\":\"").append(escapeJson(text.toString().trim())).append("\"");
        }

        CharSequence desc = node.getContentDescription();
        if (desc != null && desc.length() > 0) {
            sb.append(",\"desc\":\"").append(escapeJson(desc.toString().trim())).append("\"");
        }

        String role = inferRole(node);
        if (!role.isEmpty()) {
            sb.append(",\"role\":\"").append(escapeJson(role)).append("\"");
        }

        String resId = node.getViewIdResourceName();
        if (resId != null && !resId.isEmpty()) {
            sb.append(",\"resId\":\"").append(escapeJson(resId)).append("\"");
        }

        sb.append(",\"clickable\":").append(node.isClickable());
        sb.append(",\"longClickable\":").append(node.isLongClickable());
        sb.append(",\"editable\":").append(node.isEditable());
        sb.append(",\"focused\":").append(node.isFocused());
        sb.append(",\"scrollable\":").append(node.isScrollable());

        if (node.isChecked()) sb.append(",\"checked\":true");
        if (!node.isEnabled()) sb.append(",\"enabled\":false");

        // Bounds
        android.graphics.Rect bounds = new android.graphics.Rect();
        node.getBoundsInScreen(bounds);
        sb.append(",\"bounds\":{\"x\":").append(bounds.left)
          .append(",\"y\":").append(bounds.top)
          .append(",\"w\":").append(bounds.width())
          .append(",\"h\":").append(bounds.height()).append("}");

        // Children
        if (node.getChildCount() > 0) {
            sb.append(",\"children\":[");
            boolean firstChild = true;
            for (int i = 0; i < node.getChildCount(); i++) {
                AccessibilityNodeInfo child = node.getChild(i);
                if (child == null) continue;
                // Build child into a temp buffer to check if it produced output
                int sbLen = sb.length();
                buildTreeJson(child, sb, depth + 1, maxDepth);
                // If nothing was added, undo the potential comma
                if (sb.length() == sbLen && !firstChild) {
                    // Remove trailing comma if child produced nothing
                    // This is handled by the id>0 check in recursive call
                }
                firstChild = false;
            }
            sb.append("]");
        }

        sb.append("}");
    }

    // Infer a simplified role from the node's class name
    private String inferRole(AccessibilityNodeInfo node) {
        String cls = node.getClassName() != null ? node.getClassName().toString() : "";
        String lower = cls.toLowerCase();

        if (lower.contains("button")) return "button";
        if (lower.contains("imagebutton")) return "button";
        if (lower.contains("checkbox")) return "checkbox";
        if (lower.contains("radiobutton")) return "radio";
        if (lower.contains("switch")) return "switch";
        if (lower.contains("toggle")) return "toggle";
        if (lower.contains("edittext") || lower.contains("textfield")) return "input";
        if (lower.contains("textview") || lower.contains("text")) return "text";
        if (lower.contains("imageview") || lower.contains("image")) return "image";
        if (lower.contains("recyclerview") || lower.contains("listview") || lower.contains("scrollview")) return "list";
        if (lower.contains("webview")) return "webview";
        if (lower.contains("progressbar")) return "progress";
        if (lower.contains("spinner")) return "dropdown";
        if (lower.contains("toolbar") || lower.contains("actionbar")) return "toolbar";
        if (lower.contains("menu")) return "menu";
        if (lower.contains("tab")) return "tab";
        if (lower.contains("card")) return "card";
        if (lower.contains("container") || lower.contains("layout") || lower.contains("group")) return "container";
        if (lower.contains("framelayout")) return "container";
        if (lower.contains("linearlayout")) return "container";
        if (lower.contains("relativelayout")) return "container";
        if (lower.contains("constraintlayout")) return "container";
        return "";
    }

    // Perform an accessibility action on a node by its session ID
    public String performUIAction(int elementId, String action, String extra) {
        AccessibilityNodeInfo node;
        synchronized (nodeMap) {
            node = nodeMap.get(elementId);
        }
        if (node == null) {
            return "{\"error\":\"Element not found (id=" + elementId + "). Run ui_inspect first.\"}";
        }

        // Find clickable parent if the node itself isn't clickable
        AccessibilityNodeInfo target = node;
        if (!node.isClickable() && ("click".equals(action) || "long_click".equals(action))) {
            AccessibilityNodeInfo clickable = findClickableParent(node);
            if (clickable != null) target = clickable;
        }

        boolean success = false;
        String actionName = action;

        switch (action) {
            case "click":
                success = target.performAction(AccessibilityNodeInfo.ACTION_CLICK);
                break;
            case "long_click":
                success = target.performAction(AccessibilityNodeInfo.ACTION_LONG_CLICK);
                break;
            case "focus":
                success = target.performAction(AccessibilityNodeInfo.ACTION_FOCUS);
                break;
            case "select":
                success = target.performAction(AccessibilityNodeInfo.ACTION_SELECT);
                break;
            case "clear_selection":
                success = target.performAction(AccessibilityNodeInfo.ACTION_CLEAR_SELECTION);
                break;
            case "scroll_forward":
                success = target.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);
                break;
            case "scroll_backward":
                success = target.performAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD);
                break;
            case "expand":
                success = target.performAction(AccessibilityNodeInfo.ACTION_EXPAND);
                break;
            case "collapse":
                success = target.performAction(AccessibilityNodeInfo.ACTION_COLLAPSE);
                break;
            case "set_text":
                Bundle args = new Bundle();
                args.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, extra);
                success = target.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, args);
                actionName = "set_text: " + extra;
                break;
            case "set_selection":
                // extra format: "start,end"
                try {
                    String[] parts = extra.split(",");
                    int start = Integer.parseInt(parts[0].trim());
                    int end = parts.length > 1 ? Integer.parseInt(parts[1].trim()) : start;
                    Bundle selArgs = new Bundle();
                    selArgs.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT, start);
                    selArgs.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT, end);
                    success = target.performAction(AccessibilityNodeInfo.ACTION_SET_SELECTION, selArgs);
                } catch (Exception e) {
                    return "{\"error\":\"Invalid selection args: " + escapeJson(extra) + "\"}";
                }
                break;
            default:
                return "{\"error\":\"Unknown action: " + escapeJson(action) + "\"}";
        }

        if (success) {
            return "{\"status\":\"success\",\"action\":\"" + escapeJson(actionName) + "\",\"elementId\":" + elementId + "}";
        } else {
            return "{\"status\":\"failed\",\"action\":\"" + escapeJson(actionName) + "\",\"elementId\":" + elementId + "}";
        }
    }

    // ── Screenshot ──
    // Takes a screenshot using PixelCopy API (requires API 24+)
    public String takeScreenshot(String savePath) {
        try {
            // Use the root view of the top window
            AccessibilityNodeInfo root = getRealAppRoot();
            if (root == null) {
                return "{\"error\":\"No active window for screenshot\"}";
            }

            android.graphics.Rect bounds = new android.graphics.Rect();
            root.getBoundsInScreen(bounds);

            if (bounds.width() <= 0 || bounds.height() <= 0) {
                return "{\"error\":\"Invalid bounds for screenshot\"}";
            }

            // Create a bitmap
            android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(
                bounds.width(), bounds.height(), android.graphics.Bitmap.Config.ARGB_8888);

            // Try PixelCopy (API 24+)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                // We need a Surface — use the accessibility windows
                java.util.List<AccessibilityWindowInfo> windows = getWindows();
                if (windows == null || windows.isEmpty()) {
                    return "{\"error\":\"No windows available for screenshot\"}";
                }

                // Find the application window
                android.view.Surface surface = null;
                for (AccessibilityWindowInfo win : windows) {
                    if (win.getType() == AccessibilityWindowInfo.TYPE_APPLICATION) {
                        // PixelCopy needs a Surface, but Accessibility doesn't expose it
                        // Fall back to using MediaProjection (requires activity result)
                        break;
                    }
                }
            }

            // Fallback: save the bounds info and note that screenshot requires MediaProjection
            // For now, return a descriptive JSON of what's on screen
            String screenText = getScreenText();
            String appName = currentAppLabel != null ? currentAppLabel : "unknown";

            // Save screen text as a "screenshot" file for OCR-like analysis
            if (savePath == null || savePath.isEmpty()) {
                savePath = getCacheDir().getAbsolutePath() + "/argos_screenshot.txt";
            }
            java.io.FileWriter fw = new java.io.FileWriter(savePath);
            fw.write("App: " + appName + "\n");
            fw.write("Bounds: " + bounds.toString() + "\n");
            fw.write("Timestamp: " + System.currentTimeMillis() + "\n");
            fw.write("--- Screen Content ---\n");
            fw.write(screenText);
            fw.close();

            return "{\"status\":\"saved\",\"path\":\"" + escapeJson(savePath) + "\",\"app\":\"" + escapeJson(appName) + "\",\"bounds\":\"" + bounds.toString() + "\",\"note\":\"Text-based screenshot saved. For image screenshot, MediaProjection permission is required.\"}";
        } catch (Exception e) {
            return "{\"error\":\"Screenshot failed: " + escapeJson(e.getMessage()) + "\"}";
        }
    }

    // ── Notifications ──
    // Get active notifications by opening the notification shade and reading it
    public String getNotifications() {
        try {
            // Open notification shade
            boolean opened = performGlobalAction(GLOBAL_ACTION_NOTIFICATIONS);

            // Wait for shade to open
            try { Thread.sleep(1500); } catch (Exception e) {}

            // Read the notification shade content
            String notifText = getScreenText();

            // Also get the UI tree for structured data
            String treeJson = getUITree(10);

            // Close the shade
            performGlobalAction(GLOBAL_ACTION_NOTIFICATIONS);

            return "{\"source\":\"notification_shade\",\"opened\":" + opened
                + ",\"text\":\"" + escapeJson(notifText) + "\""
                + ",\"tree\":" + treeJson + "}";
        } catch (Exception e) {
            try { performGlobalAction(GLOBAL_ACTION_NOTIFICATIONS); } catch (Exception ex) {}
            return "{\"error\":\"Failed to get notifications: " + escapeJson(e.getMessage()) + "\"}";
        }
    }

    // Reply to a notification by reading the shade, finding the reply field, and typing
    public String replyToNotification(int index, String message) {
        try {
            // Open notification shade
            performGlobalAction(GLOBAL_ACTION_NOTIFICATIONS);
            try { Thread.sleep(1500); } catch (Exception e) {}

            // Get the UI tree to find reply buttons and input fields
            String treeJson = getUITree(10);

            // Find "Reply" button by text
            AccessibilityNodeInfo root = getRealAppRoot();
            if (root == null) {
                performGlobalAction(GLOBAL_ACTION_NOTIFICATIONS);
                return "{\"error\":\"Could not read notification shade\"}";
            }

            // Find all "Reply" buttons
            java.util.List<AccessibilityNodeInfo> replyButtons = root.findAccessibilityNodeInfosByText("Reply");
            if (replyButtons == null || replyButtons.isEmpty()) {
                // Try "reply" lowercase
                replyButtons = root.findAccessibilityNodeInfosByText("reply");
            }

            if (replyButtons == null || replyButtons.isEmpty()) {
                performGlobalAction(GLOBAL_ACTION_NOTIFICATIONS);
                return "{\"error\":\"No reply buttons found in notification shade\"}";
            }

            // Select the notification by index (0-based)
            if (index >= replyButtons.size()) {
                performGlobalAction(GLOBAL_ACTION_NOTIFICATIONS);
                return "{\"error\":\"Notification index " + index + " out of range (found " + replyButtons.size() + " reply buttons)\"}";
            }

            AccessibilityNodeInfo replyBtn = replyButtons.get(index);

            // Click the reply button to expand the input field
            AccessibilityNodeInfo clickable = findClickableParent(replyBtn);
            if (clickable != null) {
                clickable.performAction(AccessibilityNodeInfo.ACTION_CLICK);
            } else {
                replyBtn.performAction(AccessibilityNodeInfo.ACTION_CLICK);
            }

            // Wait for input field to appear
            try { Thread.sleep(1000); } catch (Exception e) {}

            // Re-read the tree to find the now-visible input field
            root = getRealAppRoot();
            if (root != null) {
                AccessibilityNodeInfo editField = findFocusedEditable(root);
                if (editField == null) {
                    // Try to find any editable field
                    editField = findAnyEditable(root);
                }

                if (editField != null) {
                    // Type the message
                    Bundle args = new Bundle();
                    args.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, message);
                    editField.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, args);

                    // Wait briefly then find and click Send button
                    try { Thread.sleep(500); } catch (Exception e) {}

                    root = getRealAppRoot();
                    if (root != null) {
                        java.util.List<AccessibilityNodeInfo> sendButtons = root.findAccessibilityNodeInfosByText("Send");
                        if (sendButtons != null && !sendButtons.isEmpty()) {
                            AccessibilityNodeInfo sendBtn = sendButtons.get(0);
                            AccessibilityNodeInfo sendClickable = findClickableParent(sendBtn);
                            if (sendClickable != null) {
                                sendClickable.performAction(AccessibilityNodeInfo.ACTION_CLICK);
                            } else {
                                sendBtn.performAction(AccessibilityNodeInfo.ACTION_CLICK);
                            }
                        }
                    }

                    // Close notification shade
                    performGlobalAction(GLOBAL_ACTION_NOTIFICATIONS);
                    return "{\"status\":\"success\",\"message\":\"Replied to notification " + index + " with: " + escapeJson(message) + "\"}";
                }
            }

            // Close notification shade
            performGlobalAction(GLOBAL_ACTION_NOTIFICATIONS);
            return "{\"error\":\"Could not find input field after clicking reply\"}";
        } catch (Exception e) {
            try { performGlobalAction(GLOBAL_ACTION_NOTIFICATIONS); } catch (Exception ex) {}
            return "{\"error\":\"Reply failed: " + escapeJson(e.getMessage()) + "\"}";
        }
    }

    // Find any editable field in the tree
    private AccessibilityNodeInfo findAnyEditable(AccessibilityNodeInfo node) {
        if (node == null) return null;
        if (node.isEditable()) return node;
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo child = node.getChild(i);
            if (child != null) {
                AccessibilityNodeInfo found = findAnyEditable(child);
                if (found != null) return found;
            }
        }
        return null;
    }
}
