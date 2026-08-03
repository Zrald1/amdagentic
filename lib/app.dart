import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'mascot/mascot_controller.dart';
import 'mascot/mascot_stage.dart';
import 'ui/prompt_overlay.dart';

class AriaApp extends StatelessWidget {
  const AriaApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Aria',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF6C5CE7),
          brightness: Brightness.dark,
        ),
        fontFamily: 'Segoe UI',
      ),
      home: const AriaHome(),
    );
  }
}

class AriaHome extends ConsumerStatefulWidget {
  const AriaHome({super.key});

  @override
  ConsumerState<AriaHome> createState() => _AriaHomeState();
}

class _AriaHomeState extends ConsumerState<AriaHome> {
  @override
  void initState() {
    super.initState();
    // Start the behavior FSM after first frame.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      ref.read(mascotControllerProvider.notifier).start();
    });
  }

  @override
  Widget build(BuildContext context) {
    return const Scaffold(
      backgroundColor: Colors.transparent,
      body: Stack(
        children: [
          MascotStage(),
          PromptOverlay(),
        ],
      ),
    );
  }
}
