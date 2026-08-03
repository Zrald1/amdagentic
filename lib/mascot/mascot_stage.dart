import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config/app_config.dart';
import '../system/window_setup.dart';
import 'mascot_controller.dart';
import 'robot_painter.dart';

/// The main mascot rendering widget. Listens to [MascotController] for state
/// changes and drives a continuous animation loop for the painter.
class MascotStage extends ConsumerStatefulWidget {
  const MascotStage({super.key});

  @override
  ConsumerState<MascotStage> createState() => _MascotStageState();
}

class _MascotStageState extends ConsumerState<MascotStage>
    with SingleTickerProviderStateMixin {
  late final AnimationController _anim;
  Timer? _positionTimer;
  bool _isHovering = false;

  @override
  void initState() {
    super.initState();
    _anim = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 1),
    )..repeat();
  }

  @override
  void dispose() {
    _positionTimer?.cancel();
    _anim.dispose();
    super.dispose();
  }

  void _onHover(bool hovering) {
    if (hovering == _isHovering) return;
    _isHovering = hovering;
    WindowSetup.setClickThrough(!hovering);
  }

  void _onTap() {
    ref.read(mascotControllerProvider.notifier).onMascotClicked();
    // Also open the prompt panel via the prompt overlay provider.
    ref.read(promptVisibleProvider.notifier).show();
  }

  @override
  Widget build(BuildContext context) {
    final status = ref.watch(mascotControllerProvider);
    final screenW = MediaQuery.of(context).size.width;

    // Update the controller's screen width on first build.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      // Reposition the desktop window to follow the mascot.
      _positionTimer?.cancel();
      _positionTimer = Timer(const Duration(milliseconds: 16), () {
        WindowSetup.positionMascot(
          status.x,
          screenW,
          AppConfig.mascotSize,
        );
      });
    });

    return Positioned(
      left: status.x,
      bottom: AppConfig.mascotBottomPadding,
      child: MouseRegion(
        onEnter: (_) => _onHover(true),
        onExit: (_) => _onHover(false),
        child: GestureDetector(
          onTap: _onTap,
          child: SizedBox(
            width: AppConfig.mascotSize,
            height: AppConfig.mascotSize,
            child: AnimatedBuilder(
              animation: _anim,
              builder: (context, _) {
                return CustomPaint(
                  painter: RobotPainter(
                    state: status.state,
                    facingRight: status.facingRight,
                    animValue: _anim.value,
                    workProgress: status.workProgress,
                  ),
                  size: Size.infinite,
                );
              },
            ),
          ),
        ),
      ),
    );
  }
}

/// Provider to control prompt panel visibility.
class PromptVisibleNotifier extends StateNotifier<bool> {
  PromptVisibleNotifier() : super(false);
  void show() => state = true;
  void hide() => state = false;
  void toggle() => state = !state;
}

final promptVisibleProvider =
    StateNotifierProvider<PromptVisibleNotifier, bool>(
  (ref) => PromptVisibleNotifier(),
);
