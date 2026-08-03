import 'dart:async';
import 'dart:math';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config/app_config.dart';
import 'mascot_state.dart';

/// Controller that drives the mascot's behavior FSM.
///
/// State transitions:
///   idle --(random timer)--> walking --> idle
///   idle --(rarely)--> greeting --> idle
///   idle --(very long)--> sleeping --> (mouse activity) --> idle
///   click --> greeting --> (prompt submitted) --> working --> celebrating --> idle
class MascotController extends StateNotifier<MascotStatus> {
  MascotController(this._screenWidth) : super(MascotStatus(x: 100.0));

  final double _screenWidth;
  final Random _rng = Random();
  Timer? _behaviorTimer;
  Timer? _walkTimer;
  Timer? _greetTimer;
  Timer? _sleepTimer;
  double _targetX = 100.0;

  static const _mascotW = AppConfig.mascotSize;

  void start() {
    _scheduleNextBehavior();
  }

  @override
  void dispose() {
    _behaviorTimer?.cancel();
    _walkTimer?.cancel();
    _greetTimer?.cancel();
    _sleepTimer?.cancel();
    super.dispose();
  }

  // ── Public API for UI interactions ──────────────────────────────────

  /// Called when the user clicks/taps the mascot.
  void onMascotClicked() {
    _cancelTimers();
    state = state.copyWith(state: MascotState.greeting, thoughtText: '');
    _greetTimer = Timer(const Duration(milliseconds: 1800), () {
      if (state.state == MascotState.greeting) {
        state = state.copyWith(state: MascotState.idle);
        _scheduleNextBehavior();
      }
    });
  }

  /// Called when the user submits a task in the prompt panel.
  void onTaskStarted() {
    _cancelTimers();
    state = state.copyWith(
      state: MascotState.working,
      workProgress: 0.0,
      thoughtText: '',
    );
  }

  /// Update progress + streamed thought text during work.
  void onWorkProgress(double progress, String partialText) {
    state = state.copyWith(
      state: MascotState.working,
      workProgress: progress,
      thoughtText: partialText,
    );
  }

  /// Called when the agent finishes.
  void onTaskCompleted(String finalText) {
    state = state.copyWith(
      state: MascotState.celebrating,
      workProgress: 1.0,
      thoughtText: finalText,
    );
    _greetTimer = Timer(const Duration(milliseconds: 2500), () {
      state = state.copyWith(state: MascotState.idle, workProgress: 0.0);
      _scheduleNextBehavior();
    });
  }

  /// Called when the agent errors out.
  void onTaskError(String error) {
    state = state.copyWith(
      state: MascotState.idle,
      thoughtText: '',
      workProgress: 0.0,
    );
    _scheduleNextBehavior();
  }

  // ── Internal FSM ────────────────────────────────────────────────────

  void _cancelTimers() {
    _behaviorTimer?.cancel();
    _walkTimer?.cancel();
    _greetTimer?.cancel();
    _sleepTimer?.cancel();
  }

  void _scheduleNextBehavior() {
    _behaviorTimer?.cancel();
    final delay = Duration(
      milliseconds: AppConfig.idleMin.inMilliseconds +
          _rng.nextInt(AppConfig.idleMax.inMilliseconds -
              AppConfig.idleMin.inMilliseconds),
    );
    _behaviorTimer = Timer(delay, _pickBehavior);
  }

  void _pickBehavior() {
    if (state.state == MascotState.working ||
        state.state == MascotState.greeting) {
      return;
    }

    final roll = _rng.nextDouble();
    if (roll < 0.15) {
      _startGreeting();
    } else if (roll < 0.85) {
      _startWalking();
    } else {
      _startSleeping();
    }
  }

  void _startGreeting() {
    state = state.copyWith(state: MascotState.greeting);
    _greetTimer = Timer(const Duration(milliseconds: 1500), () {
      if (state.state == MascotState.greeting) {
        state = state.copyWith(state: MascotState.idle);
        _scheduleNextBehavior();
      }
    });
  }

  void _startWalking() {
    _targetX = _rng.nextDouble() * (_screenWidth - _mascotW);
    final currentX = state.x;
    final distance = (_targetX - currentX).abs();
    final durationMs = (distance / AppConfig.walkSpeed * 1000).round();

    state = state.copyWith(
      state: MascotState.walking,
      facingRight: _targetX > currentX,
    );

    _walkTimer?.cancel();
    _walkTimer = Timer(Duration(milliseconds: durationMs), () {
      state = state.copyWith(state: MascotState.idle, x: _targetX);
      _scheduleNextBehavior();
    });

    // Animate X position smoothly.
    _animateWalk(currentX, _targetX, durationMs);
  }

  void _animateWalk(double fromX, double toX, int durationMs) {
    final steps = (durationMs / 16).ceil().clamp(1, 200);
    final stepDelay = Duration(milliseconds: (durationMs / steps).round());
    var step = 0;
    _walkTimer?.cancel();
    Timer.periodic(stepDelay, (t) {
      step++;
      if (step >= steps || state.state != MascotState.walking) {
        state = state.copyWith(x: toX);
        t.cancel();
        return;
      }
      final progress = step / steps;
      final x = fromX + (toX - fromX) * progress;
      state = state.copyWith(x: x);
    });
  }

  void _startSleeping() {
    state = state.copyWith(state: MascotState.sleeping);
    _sleepTimer = Timer(const Duration(seconds: 20), () {
      if (state.state == MascotState.sleeping) {
        state = state.copyWith(state: MascotState.idle);
        _scheduleNextBehavior();
      }
    });
  }
}

final mascotControllerProvider =
    StateNotifierProvider<MascotController, MascotStatus>(
  (ref) => MascotController(1920.0), // default; updated at runtime
);
