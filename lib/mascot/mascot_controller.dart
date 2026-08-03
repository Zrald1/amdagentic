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
  MascotController() : super(MascotStatus(x: 100.0));

  final Random _rng = Random();
  Timer? _behaviorTimer;
  Timer? _walkTimer;
  Timer? _greetTimer;
  Timer? _sleepTimer;

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
    // The mascot walks in place (facing alternates) since the window is
    // fixed-size. The walking animation is visual (legs moving) without
    // changing X position. This keeps the transparent overlay reliable.
    state = state.copyWith(
      state: MascotState.walking,
      facingRight: _rng.nextBool(),
    );

    _walkTimer?.cancel();
    _walkTimer = Timer(const Duration(seconds: 3), () {
      state = state.copyWith(state: MascotState.idle);
      _scheduleNextBehavior();
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
  (ref) => MascotController(),
);
