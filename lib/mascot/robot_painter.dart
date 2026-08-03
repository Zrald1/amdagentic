import 'dart:math';
import 'package:flutter/material.dart';

import 'mascot_state.dart';

/// Custom-painted 2.5D robot mascot. Renders a cute robot with state-driven
/// animations: idle breathing, walking legs, waving arm, thinking gears,
/// celebrating sparkles, sleeping Zzz.
///
/// This is a fallback/stand-in for a Rive .riv asset. The painting API mirrors
/// what Rive inputs would be (state, progress, facing) so swapping to Rive
/// later only changes this widget.
class RobotPainter extends CustomPainter {
  RobotPainter({
    required this.state,
    required this.facingRight,
    required this.animValue,
    this.workProgress = 0.0,
  });

  final MascotState state;
  final bool facingRight;
  final double animValue; // 0..1 loop from AnimationController
  final double workProgress;

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2;

    canvas.save();
    // Flip horizontally if facing left.
    if (!facingRight) {
      canvas.translate(size.width, 0);
      canvas.scale(-1, 1);
    }

    switch (state) {
      case MascotState.idle:
        _paintIdle(canvas, size, cx, cy, animValue);
      case MascotState.walking:
        _paintWalking(canvas, size, cx, cy, animValue);
      case MascotState.greeting:
        _paintGreeting(canvas, size, cx, cy, animValue);
      case MascotState.working:
        _paintWorking(canvas, size, cx, cy, animValue, workProgress);
      case MascotState.celebrating:
        _paintCelebrating(canvas, size, cx, cy, animValue);
      case MascotState.sleeping:
        _paintSleeping(canvas, size, cx, cy, animValue);
    }

    canvas.restore();
  }

  // ── Color palette ───────────────────────────────────────────────────
  static const _bodyColor = Color(0xFF6C5CE7);
  static const _bodyDark = Color(0xFF5447CB);
  static const _headColor = Color(0xFFA29BFE);
  static const _accent = Color(0xFF00CEC9);
  static const _eyeColor = Color(0xFF2D3436);
  static const _metalLight = Color(0xFFDFE6E9);
  static const _metalDark = Color(0xFFB2BEC3);

  // ── Helpers ─────────────────────────────────────────────────────────

  void _paintShadow(Canvas canvas, double cx, double baseY, double w) {
    final paint = Paint()..color = Colors.black.withValues(alpha: 0.15);
    canvas.drawOval(
      Rect.fromCenter(center: Offset(cx, baseY), width: w, height: w * 0.25),
      paint,
    );
  }

  void _paintBody(Canvas canvas, double cx, double cy, double bobY) {
    // Body — rounded rectangle torso
    final bodyRect = RRect.fromRectAndRadius(
      Rect.fromCenter(center: Offset(cx, cy + 18 + bobY), width: 52, height: 48),
      const Radius.circular(14),
    );
    canvas.drawRRect(
      bodyRect,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [_bodyColor, _bodyDark],
        ).createShader(bodyRect.outerRect),
    );

    // Chest light/indicator
    canvas.drawCircle(
      Offset(cx, cy + 22 + bobY),
      6,
      Paint()..color = _accent.withValues(alpha: 0.9),
    );
    canvas.drawCircle(
      Offset(cx, cy + 22 + bobY),
      3,
      Paint()
        ..color = Colors.white
        ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 2),
    );
  }

  void _paintHead(Canvas canvas, double cx, double cy, double bobY,
      {bool happy = false, bool sleeping = false}) {
    final headY = cy - 22 + bobY;

    // Head — rounded square
    final headRect = RRect.fromRectAndRadius(
      Rect.fromCenter(center: Offset(cx, headY), width: 56, height: 46),
      const Radius.circular(16),
    );
    canvas.drawRRect(
      headRect,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [_headColor, _bodyColor],
        ).createShader(headRect.outerRect),
    );

    // Antenna
    final antennaX = cx + 14;
    canvas.drawLine(
      Offset(antennaX, headY - 18),
      Offset(antennaX, headY - 30),
      Paint()
        ..color = _metalDark
        ..strokeWidth = 2.5
        ..strokeCap = StrokeCap.round,
    );
    // Antenna bulb
    canvas.drawCircle(
      Offset(antennaX, headY - 32),
      4,
      Paint()
        ..color = _accent
        ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 1),
    );

    // Eyes
    if (sleeping) {
      // Closed eyes — curved lines
      final eyePaint = Paint()
        ..color = _eyeColor
        ..strokeWidth = 2.5
        ..strokeCap = StrokeCap.round;
      canvas.drawArc(
        Rect.fromCenter(center: Offset(cx - 10, headY - 2), width: 10, height: 6),
        0, 3.14, false, eyePaint,
      );
      canvas.drawArc(
        Rect.fromCenter(center: Offset(cx + 10, headY - 2), width: 10, height: 6),
        0, 3.14, false, eyePaint,
      );
    } else {
      // Eye whites
      for (final ex in [-10.0, 10.0]) {
        canvas.drawCircle(
          Offset(cx + ex, headY - 2),
          7,
          Paint()..color = Colors.white,
        );
        // Pupils with glow
        canvas.drawCircle(
          Offset(cx + ex, headY - 2),
          4,
          Paint()..color = _eyeColor,
        );
        canvas.drawCircle(
          Offset(cx + ex - 1, headY - 3),
          1.5,
          Paint()..color = Colors.white,
        );
      }
    }

    // Mouth
    if (happy || sleeping) {
      // Smile
      final mouthPaint = Paint()
        ..color = _eyeColor
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2.5
        ..strokeCap = StrokeCap.round;
      canvas.drawArc(
        Rect.fromCenter(center: Offset(cx, headY + 10), width: 16, height: 10),
        0.2, 2.74, false, mouthPaint,
      );
    } else {
      // Neutral line
      canvas.drawLine(
        Offset(cx - 6, headY + 10),
        Offset(cx + 6, headY + 10),
        Paint()
          ..color = _eyeColor
          ..strokeWidth = 2.5
          ..strokeCap = StrokeCap.round,
      );
    }
  }

  void _paintArmsIdle(Canvas canvas, double cx, double cy, double bobY) {
    final armPaint = Paint()
      ..color = _metalLight
      ..strokeWidth = 6
      ..strokeCap = StrokeCap.round;
    // Left arm
    canvas.drawLine(
      Offset(cx - 26, cy + 18 + bobY),
      Offset(cx - 34, cy + 38 + bobY),
      armPaint,
    );
    // Right arm
    canvas.drawLine(
      Offset(cx + 26, cy + 18 + bobY),
      Offset(cx + 34, cy + 38 + bobY),
      armPaint,
    );
  }

  void _paintArmWaving(Canvas canvas, double cx, double cy, double bobY,
      double waveAngle) {
    final armPaint = Paint()
      ..color = _metalLight
      ..strokeWidth = 6
      ..strokeCap = StrokeCap.round;
    // Left arm — idle
    canvas.drawLine(
      Offset(cx - 26, cy + 18 + bobY),
      Offset(cx - 34, cy + 38 + bobY),
      armPaint,
    );
    // Right arm — waving up
    final shoulder = Offset(cx + 26, cy + 18 + bobY);
    final elbow = Offset(cx + 38, cy + 2 + bobY);
    final hand = Offset(
      cx + 42 + 8 * cos(waveAngle),
      cy - 14 + bobY + 6 * sin(waveAngle),
    );
    canvas.drawLine(shoulder, elbow, armPaint);
    canvas.drawLine(elbow, hand, armPaint);
    // Hand circle
    canvas.drawCircle(hand, 5, Paint()..color = _metalLight);
  }

  void _paintLegs(Canvas canvas, double cx, double cy, double bobY,
      {double walkPhase = 0}) {
    final legPaint = Paint()
      ..color = _metalDark
      ..strokeWidth = 7
      ..strokeCap = StrokeCap.round;
    final baseY = cy + 42 + bobY;
    if (walkPhase == 0) {
      // Standing
      canvas.drawLine(Offset(cx - 10, baseY), Offset(cx - 10, baseY + 16), legPaint);
      canvas.drawLine(Offset(cx + 10, baseY), Offset(cx + 10, baseY + 16), legPaint);
    } else {
      // Walking — alternate leg swing
      final l1 = 10 * sin(walkPhase);
      final l2 = 10 * sin(walkPhase + pi);
      canvas.drawLine(
        Offset(cx - 10, baseY),
        Offset(cx - 10 + l1, baseY + 16),
        legPaint,
      );
      canvas.drawLine(
        Offset(cx + 10, baseY),
        Offset(cx + 10 + l2, baseY + 16),
        legPaint,
      );
    }
  }

  // ── State-specific painting ─────────────────────────────────────────

  void _paintIdle(Canvas canvas, Size size, double cx, double cy, double t) {
    final bob = 3 * sin(t * 2 * pi);
    _paintShadow(canvas, cx, cy + 62, 50);
    _paintLegs(canvas, cx, cy, bob);
    _paintBody(canvas, cx, cy, bob);
    _paintArmsIdle(canvas, cx, cy, bob);
    _paintHead(canvas, cx, cy, bob);
  }

  void _paintWalking(Canvas canvas, Size size, double cx, double cy, double t) {
    final bob = 2 * sin(t * 4 * pi);
    final walkPhase = t * 4 * pi;
    _paintShadow(canvas, cx, cy + 62, 46);
    _paintLegs(canvas, cx, cy, bob, walkPhase: walkPhase);
    _paintBody(canvas, cx, cy, bob);
    _paintArmsIdle(canvas, cx, cy, bob);
    _paintHead(canvas, cx, cy, bob, happy: true);
  }

  void _paintGreeting(Canvas canvas, Size size, double cx, double cy, double t) {
    final bob = 3 * sin(t * 2 * pi);
    final waveAngle = t * 6 * pi;
    _paintShadow(canvas, cx, cy + 62, 50);
    _paintLegs(canvas, cx, cy, bob);
    _paintBody(canvas, cx, cy, bob);
    _paintArmWaving(canvas, cx, cy, bob, waveAngle);
    _paintHead(canvas, cx, cy, bob, happy: true);
  }

  void _paintWorking(Canvas canvas, Size size, double cx, double cy, double t,
      double progress) {
    final bob = 2 * sin(t * 3 * pi);
    _paintShadow(canvas, cx, cy + 62, 48);
    _paintLegs(canvas, cx, cy, bob);
    _paintBody(canvas, cx, cy, bob);

    // Both arms up — "thinking" pose
    final armPaint = Paint()
      ..color = _metalLight
      ..strokeWidth = 6
      ..strokeCap = StrokeCap.round;
    canvas.drawLine(
      Offset(cx - 26, cy + 18 + bob),
      Offset(cx - 36, cy + 2 + bob),
      armPaint,
    );
    canvas.drawLine(
      Offset(cx + 26, cy + 18 + bob),
      Offset(cx + 36, cy + 2 + bob),
      armPaint,
    );

    _paintHead(canvas, cx, cy, bob);

    // Progress ring around the body
    final ringPaint = Paint()
      ..color = _accent
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3
      ..strokeCap = StrokeCap.round;
    canvas.drawArc(
      Rect.fromCenter(center: Offset(cx, cy + 20 + bob), width: 64, height: 64),
      -pi / 2,
      2 * pi * progress,
      false,
      ringPaint,
    );

    // Floating gears above head
    _paintGear(canvas, cx - 14, cy - 44 + bob + 3 * sin(t * 4 * pi), 8, t * 2);
    _paintGear(canvas, cx + 14, cy - 48 + bob + 3 * sin(t * 4 * pi + pi), 6, -t * 2);
  }

  void _paintGear(Canvas canvas, double cx, double cy, double r, double rot) {
    final paint = Paint()
      ..color = _metalLight.withValues(alpha: 0.8)
      ..style = PaintingStyle.fill;
    final path = Path();
    const teeth = 8;
    for (var i = 0; i < teeth * 2; i++) {
      final angle = rot + (i / (teeth * 2)) * 2 * pi;
      final radius = i.isEven ? r * 1.3 : r;
      final x = cx + radius * cos(angle);
      final y = cy + radius * sin(angle);
      if (i == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    path.close();
    canvas.drawPath(path, paint);
    canvas.drawCircle(Offset(cx, cy), r * 0.4, Paint()..color = _metalDark);
  }

  void _paintCelebrating(Canvas canvas, Size size, double cx, double cy, double t) {
    final bob = 4 * sin(t * 4 * pi);
    _paintShadow(canvas, cx, cy + 62, 52);
    _paintLegs(canvas, cx, cy, bob, walkPhase: 0);
    _paintBody(canvas, cx, cy, bob);
    // Both arms up in celebration
    final armPaint = Paint()
      ..color = _metalLight
      ..strokeWidth = 6
      ..strokeCap = StrokeCap.round;
    canvas.drawLine(
      Offset(cx - 26, cy + 18 + bob),
      Offset(cx - 36, cy - 10 + bob),
      armPaint,
    );
    canvas.drawLine(
      Offset(cx + 26, cy + 18 + bob),
      Offset(cx + 36, cy - 10 + bob),
      armPaint,
    );
    _paintHead(canvas, cx, cy, bob, happy: true);

    // Sparkles
    final sparklePaint = Paint()
      ..color = _accent
      ..style = PaintingStyle.fill;
    for (var i = 0; i < 6; i++) {
      final angle = (i / 6) * 2 * pi + t * 2;
      final dist = 35 + 10 * sin(t * 6 * pi + i);
      final sx = cx + dist * cos(angle);
      final sy = cy + dist * sin(angle);
      canvas.drawCircle(Offset(sx, sy), 3, sparklePaint);
    }
  }

  void _paintSleeping(Canvas canvas, Size size, double cx, double cy, double t) {
    final bob = 1 * sin(t * pi);
    _paintShadow(canvas, cx, cy + 62, 48);
    _paintLegs(canvas, cx, cy, bob);
    _paintBody(canvas, cx, cy, bob);
    _paintArmsIdle(canvas, cx, cy, bob);
    _paintHead(canvas, cx, cy, bob, sleeping: true);

    // Zzz particles
    for (var i = 0; i < 3; i++) {
      final phase = (t * 0.5 + i * 0.33) % 1.0;
      final zx = cx + 30 + i * 10;
      final zy = cy - 30 - phase * 30;
      final opacity = (1 - phase).clamp(0.0, 1.0);
      final tp = TextPainter(
        text: TextSpan(
          text: i == 2 ? 'Z' : 'z',
          style: TextStyle(
            color: _accent.withValues(alpha: opacity * 0.7),
            fontSize: 14 + i * 4,
            fontWeight: FontWeight.bold,
          ),
        ),
      )..layout();
      tp.paint(canvas, Offset(zx, zy));
    }
  }

  @override
  bool shouldRepaint(covariant RobotPainter old) =>
      state != old.state ||
      facingRight != old.facingRight ||
      animValue != old.animValue ||
      workProgress != old.workProgress;
}
