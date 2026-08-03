import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'app.dart';
import 'system/window_setup.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Desktop: configure transparent, always-on-top overlay window.
  await WindowSetup.initialize();

  runApp(
    const ProviderScope(
      child: AriaApp(),
    ),
  );
}
