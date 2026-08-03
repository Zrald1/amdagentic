import 'dart:io';
import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

/// Handles desktop-only window configuration: transparent, always-on-top,
/// borderless, click-through overlay. On mobile this is a no-op.
class WindowSetup {
  WindowSetup._();

  static Size? _cachedScreenSize;

  static Future<void> initialize() async {
    if (!Platform.isWindows && !Platform.isLinux && !Platform.isMacOS) {
      return; // Mobile — no window management.
    }

    await windowManager.ensureInitialized();

    await windowManager.waitUntilReadyToShow(
      WindowOptions(
        size: const Size(400, 200),
        backgroundColor: Colors.transparent,
        skipTaskbar: true,
        titleBarStyle: TitleBarStyle.hidden,
        alwaysOnTop: true,
      ),
      () async {
        await windowManager.setPosition(const Offset(50, 50));
        await windowManager.show();
        // Make the whole window click-through by default. The mascot widget
        // re-enables mouse events on its own bounding box when hovered.
        await windowManager.setIgnoreMouseEvents(true);
      },
    );

    _cachedScreenSize = const Size(1920, 1080);
  }

  /// Toggle click-through. When true, mouse events pass through the window.
  static Future<void> setClickThrough(bool ignore) async {
    if (!Platform.isWindows && !Platform.isLinux && !Platform.isMacOS) return;
    await windowManager.setIgnoreMouseEvents(ignore);
  }

  /// Move the overlay window so the mascot appears at [logicalX] along the
  /// bottom of the screen.
  static Future<void> positionMascot(
      double logicalX, double screenWidth, double mascotHeight) async {
    if (!Platform.isWindows && !Platform.isLinux && !Platform.isMacOS) return;
    final screenH = _cachedScreenSize?.height ?? 1080.0;
    final maxX = (screenWidth - mascotHeight).clamp(1.0, double.infinity);
    final x = logicalX.clamp(0.0, maxX);
    final y = screenH - mascotHeight - 48;
    await windowManager.setPosition(Offset(x, y));
  }
}
