import 'dart:io';
import 'package:tray_manager/tray_manager.dart';

/// Manages the system tray icon for hide/show/quit on desktop.
class TrayController with TrayListener {
  TrayController._(this._onShow, this._onHide, this._onQuit);

  static TrayController? _instance;

  static Future<TrayController> create({
    required void Function() onShow,
    required void Function() onHide,
    required void Function() onQuit,
  }) async {
    final tray = TrayController._(onShow, onHide, onQuit);

    if (!Platform.isWindows && !Platform.isLinux && !Platform.isMacOS) {
      return tray;
    }

    _instance = tray;
    trayManager.addListener(tray);

    try {
      await trayManager.setIcon(
        Platform.isWindows ? 'assets/icons/tray.ico' : 'assets/icons/tray.png',
      );
    } catch (_) {
      // Icon file may not exist yet — tray still works without it on some platforms.
    }
    await trayManager.setToolTip('Aria — AMD Radeon AI Companion');
    await trayManager.setContextMenu(Menu(items: [
      MenuItem(label: 'Show Aria', onClick: (item) => onShow()),
      MenuItem(label: 'Hide Aria', onClick: (item) => onHide()),
      MenuItem.separator(),
      MenuItem(label: 'Quit', onClick: (item) => onQuit()),
    ]));

    return tray;
  }

  final void Function() _onShow;
  final void Function() _onHide;
  final void Function() _onQuit;

  @override
  void onTrayMenuItemClick(MenuItem menuItem) {
    switch (menuItem.label) {
      case 'Show Aria':
        _onShow();
        break;
      case 'Hide Aria':
        _onHide();
        break;
      case 'Quit':
        _onQuit();
        break;
    }
  }

  void dispose() {
    if (_instance != null) {
      trayManager.removeListener(this);
      _instance = null;
    }
  }
}
