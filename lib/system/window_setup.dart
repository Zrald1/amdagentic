import 'dart:io';
import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

/// Handles desktop-only window configuration: transparent, always-on-top,
/// borderless overlay. On mobile this is a no-op.
class WindowSetup {
  WindowSetup._();

  static Size? _cachedScreenSize;

  static Future<void> initialize() async {
    if (!Platform.isWindows && !Platform.isLinux && !Platform.isMacOS) {
      return; // Mobile — no window management.
    }

    await windowManager.ensureInitialized();

    // Get actual screen size
    final size = await windowManager.getSize();
    _cachedScreenSize = size;

    await windowManager.waitUntilReadyToShow(
      WindowOptions(
        // Wide window spanning the bottom of the screen.
        size: const Size(1920, 200),
        // Use a near-transparent black so the window is actually visible
        // on Windows. Full Colors.transparent can make the whole window
        // disappear on Windows layered windows.
        backgroundColor: const Color(0x01000000),
        skipTaskbar: true,
        titleBarStyle: TitleBarStyle.hidden,
        alwaysOnTop: true,
      ),
      () async {
        // Position at the bottom-left of a 1080p screen initially.
        // The mascot logic will reposition as it walks.
        await windowManager.setSize(const Size(1920, 200));
        await windowManager.setPosition(const Offset(0, 880));
        await windowManager.show();
        await windowManager.focus();
        // Don't set click-through initially — let the user see and click it.
      },
    );

    _cachedScreenSize = const Size(1920, 1080);
  }

  /// Toggle click-through.
  static Future<void> setClickThrough(bool ignore) async {
    if (!Platform.isWindows && !Platform.isLinux && !Platform.isMacOS) return;
    await windowManager.setIgnoreMouseEvents(ignore);
  }

  /// Move the overlay window so the mascot appears at [logicalX] along the
  /// bottom of the screen.
  static Future<void> positionMascot(
      double logicalX, double screenWidth, double mascotHeight) async {
    if (!Platform.isWindows && !Platform.isLinux && !Platform.isMacOS) return;
    // Window spans the full screen width at the bottom; we don't need to
    // move it — the mascot moves within the window via Positioned.
    // Just ensure the window is at the bottom of the screen.
  }
}
