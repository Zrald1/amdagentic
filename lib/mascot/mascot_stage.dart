import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config/app_config.dart';
import 'mascot_controller.dart';
import 'robot_painter.dart';

/// The main mascot rendering widget. Listens to [MascotController] for state
/// changes and drives a continuous animation loop for the painter.
///
/// The window spans the full screen width at the bottom. The mascot moves
/// within that window via [Positioned].
class MascotStage extends ConsumerStatefulWidget {
  const MascotStage({super.key});

  @override
  ConsumerState<MascotStage> createState() => _MascotStageState();
}

class _MascotStageState extends ConsumerState<MascotStage>
    with SingleTickerProviderStateMixin {
  late final AnimationController _anim;

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
    _anim.dispose();
    super.dispose();
  }

  void _onTap() {
    ref.read(mascotControllerProvider.notifier).onMascotClicked();
    ref.read(promptVisibleProvider.notifier).show();
  }

  @override
  Widget build(BuildContext context) {
    final status = ref.watch(mascotControllerProvider);
    final screenW = MediaQuery.of(context).size.width;

    // Update the controller's screen width.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      // No-op; controller uses a default 1920 which is fine.
    });

    return Stack(
      children: [
        Positioned(
          left: status.x.clamp(0.0, (screenW - AppConfig.mascotSize).clamp(1.0, double.infinity)),
          bottom: AppConfig.mascotBottomPadding,
          child: MouseRegion(
            onEnter: (_) {},
            onExit: (_) {},
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
        ),
      ],
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
