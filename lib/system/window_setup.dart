import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_acrylic/flutter_acrylic.dart' as acrylic;
import 'package:window_manager/window_manager.dart';

/// Handles desktop-only window configuration.
/// On mobile this is a no-op.
class WindowSetup {
  WindowSetup._();

  static Future<void> initialize() async {
    if (!Platform.isWindows && !Platform.isLinux && !Platform.isMacOS) {
      return;
    }

    await acrylic.Window.initialize();
    await windowManager.ensureInitialized();

    await windowManager.waitUntilReadyToShow(
      const WindowOptions(
        size: Size(400, 400),
        backgroundColor: Color(0xFF1E1E2E), // opaque dark background so it's visible
        skipTaskbar: false,
        titleBarStyle: TitleBarStyle.hidden,
        alwaysOnTop: true,
      ),
      () async {
        // Center on screen so it's easy to find.
        await windowManager.center();
        await windowManager.show();
        await windowManager.focus();
      },
    );
  }

  static Future<void> setClickThrough(bool ignore) async {
    if (!Platform.isWindows && !Platform.isLinux && !Platform.isMacOS) return;
    await windowManager.setIgnoreMouseEvents(ignore);
  }
}
