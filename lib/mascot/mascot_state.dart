/// Behavioral states for the Aria mascot.
enum MascotState {
  /// Standing still, occasional blink/breathe.
  idle,

  /// Moving horizontally to a target X.
  walking,

  /// Waving hello — triggered on launch, on click, or randomly.
  greeting,

  /// Agent is executing a task; show thinking animation + progress.
  working,

  /// Task completed successfully — brief celebration.
  celebrating,

  /// Long idle — Zzz particles.
  sleeping,
}

/// Extended state payload for the mascot controller.
class MascotStatus {
  MascotStatus({
    this.state = MascotState.idle,
    this.x = 100.0,
    this.facingRight = true,
    this.workProgress = 0.0,
    this.thoughtText = '',
  });

  final MascotState state;
  final double x; // logical X position
  final bool facingRight;
  final double workProgress; // 0..1 during working
  final String thoughtText; // streamed tokens during working

  MascotStatus copyWith({
    MascotState? state,
    double? x,
    bool? facingRight,
    double? workProgress,
    String? thoughtText,
  }) {
    return MascotStatus(
      state: state ?? this.state,
      x: x ?? this.x,
      facingRight: facingRight ?? this.facingRight,
      workProgress: workProgress ?? this.workProgress,
      thoughtText: thoughtText ?? this.thoughtText,
    );
  }
}
