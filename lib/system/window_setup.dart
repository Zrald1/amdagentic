import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_acrylic/flutter_acrylic.dart' as acrylic;
import 'package:window_manager/window_manager.dart';

/// Handles desktop-only window configuration: transparent, always-on-top,
/// borderless overlay using flutter_acrylic for native transparency.
/// On mobile this is a no-op.
class WindowSetup {
  WindowSetup._();

  static Future<void> initialize() async {
    if (!Platform.isWindows && !Platform.isLinux && !Platform.isMacOS) {
      return; // Mobile — no window management.
    }

    // flutter_acrylic must be initialized before window_manager.
    await acrylic.Window.initialize();

    await windowManager.ensureInitialized();

    await windowManager.waitUntilReadyToShow(
      const WindowOptions(
        size: Size(360, 500),
        backgroundColor: Colors.transparent,
        skipTaskbar: true,
        titleBarStyle: TitleBarStyle.hidden,
        alwaysOnTop: true,
      ),
      () async {
        // Apply transparent window effect via flutter_acrylic.
        // This is the key step that makes the window actually transparent
        // on Windows — without it, the window background is opaque.
        await acrylic.Window.setEffect(
          effect: acrylic.WindowEffect.transparent,
          color: Colors.transparent,
        );

        // Position at bottom-left initially.
        await windowManager.setPosition(const Offset(50, 50));
        await windowManager.show();
        await windowManager.focus();
      },
    );
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
    // Window is small and follows the mascot.
    final screenH = await _getScreenHeight();
    final maxX = (screenWidth - mascotHeight).clamp(1.0, double.infinity);
    final x = logicalX.clamp(0.0, maxX);
    final y = screenH - mascotHeight - 48;
    await windowManager.setPosition(Offset(x, y));
  }

  static Future<double> _getScreenHeight() async {
    try {
      final size = await windowManager.getSize();
      return size.height;
    } catch (_) {
      return 1080.0;
    }
  }
}
