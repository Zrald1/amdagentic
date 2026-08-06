#include "robot_gles.h"
#include <cmath>
#include <android/log.h>

#define TAG "Argos"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

RobotGles::RobotGles() {}
RobotGles::~RobotGles() {}

void RobotGles::init(EglRenderer* renderer) {
    m_renderer = renderer;
    if (renderer) {
        m_cx = renderer->getWidth() / 2.0f;
        m_cy = renderer->getHeight() / 2.0f;
        m_robotSize = (float)(renderer->getHeight() < renderer->getWidth() ?
                     renderer->getHeight() : renderer->getWidth()) * 0.18f;
    }
}

void RobotGles::update(float dt) {
    m_animTime += dt;
    m_bob = sinf(m_animTime * 2.0f) * 6.0f;

    // Eye blink logic
    m_eyeBlinkTimer -= dt;
    if (m_eyeBlinkTimer <= 0 && !m_blinking) {
        m_blinking = true;
        m_blinkDuration = 0.15f;
        m_eyeBlinkTimer = 3.0f + (float)(rand() % 40) / 10.0f;
    }
    if (m_blinking) {
        m_blinkDuration -= dt;
        if (m_blinkDuration <= 0) m_blinking = false;
    }

    m_stateTimer += dt;
}

void RobotGles::onTouch(float x, float y, int action) {
    // Tap on robot head could trigger chat input focus
    (void)x; (void)y; (void)action;
}

void RobotGles::render() {
    if (!m_renderer) return;

    float cx = m_cx;
    float cy = m_cy + m_bob;
    float s = m_robotSize;

    // Ground shadow
    drawGroundShadow(cx, cy + s * 0.9f, s * 0.7f);

    // Hover glow
    drawHoverGlow(cx, cy + s * 0.8f);

    // Egg body (white, glossy)
    drawEggBody(cx, cy, m_bob);

    // Arms
    float waveAngle = sinf(m_animTime * 3.0f) * 0.3f;
    drawArm(cx, cy, m_bob, true, waveAngle);
    drawArm(cx, cy, m_bob, false, -waveAngle);

    // Head with Spartan helmet
    float headY = cy - s * 0.55f;
    drawHead(cx, headY, m_bob);

    // Spartan crest on top
    drawSpartanCrest(cx, headY - s * 0.15f);

    // Thinking dots if thinking
    if (m_thinking) {
        drawThinkingDots(cx, headY - s * 0.5f);
    }
}

void RobotGles::drawEggBody(float cx, float cy, float bob) {
    if (!m_renderer) return;
    float s = m_robotSize;
    // Main body — white egg shape (approximate with circles)
    m_renderer->drawCircle(cx, cy, s * 0.5f, 0.92f, 0.92f, 0.95f, 1.0f, 48);
    // Highlight (lighter, upper-left)
    m_renderer->drawCircle(cx - s * 0.15f, cy - s * 0.15f, s * 0.25f, 1.0f, 1.0f, 1.0f, 0.4f, 32);
    // Shadow (lower-right)
    m_renderer->drawCircle(cx + s * 0.15f, cy + s * 0.2f, s * 0.3f, 0.75f, 0.75f, 0.78f, 0.3f, 32);
}

void RobotGles::drawHead(float cx, float headY, float bob) {
    if (!m_renderer) return;
    float s = m_robotSize;

    // Head — slightly smaller egg, dark face screen
    m_renderer->drawCircle(cx, headY, s * 0.35f, 0.92f, 0.92f, 0.95f, 1.0f, 48);

    // Face screen (black, rounded)
    m_renderer->drawRoundRect(cx - s * 0.22f, headY - s * 0.15f,
                              s * 0.44f, s * 0.3f, s * 0.08f,
                              0.05f, 0.05f, 0.08f, 1.0f);

    // Eyes — neon red (Spartan helmet red eyes)
    float eyeY = headY - s * 0.02f;
    float eyeSpacing = s * 0.1f;
    float eyeW = s * 0.06f;
    float eyeH = m_blinking ? s * 0.01f : s * 0.08f;

    drawEye(cx - eyeSpacing, eyeY, eyeW, eyeH, m_thinking);
    drawEye(cx + eyeSpacing, eyeY, eyeW, eyeH, m_thinking);
}

void RobotGles::drawEye(float ex, float ey, float w, float h, bool thinking) {
    if (!m_renderer) return;

    // Red neon eye (Spartan red)
    float r = 1.0f, g = 0.15f, b = 0.15f;
    if (thinking) { r = 0.2f; g = 0.6f; b = 1.0f; } // Blue when thinking

    // Glow
    m_renderer->drawCircle(ex, ey, w * 1.8f, r * 0.3f, g * 0.3f, b * 0.3f, 0.4f, 24);
    // Eye
    m_renderer->drawCircle(ex, ey, w, r, g, b, 1.0f, 24);
    // Spark (white center)
    if (!m_blinking) {
        m_renderer->drawCircle(ex - w * 0.2f, ey - h * 0.2f, w * 0.3f, 1.0f, 1.0f, 1.0f, 0.8f, 16);
    }
}

void RobotGles::drawHoverGlow(float cx, float baseY) {
    if (!m_renderer) return;
    float s = m_robotSize;
    // Blue glow under robot
    for (int i = 0; i < 3; i++) {
        float alpha = 0.15f - i * 0.04f;
        m_renderer->drawCircle(cx, baseY, s * (0.4f + i * 0.15f), 0.0f, 0.5f, 1.0f, alpha, 32);
    }
}

void RobotGles::drawArm(float cx, float cy, float bob, bool left, float waveAngle) {
    if (!m_renderer) return;
    float s = m_robotSize;
    float armX = left ? cx - s * 0.45f : cx + s * 0.45f;
    float armY = cy + sinf(m_animTime * 2.0f + (left ? 0 : 3.14f)) * 4.0f + waveAngle * 10.0f;
    // Arm — small white circle
    m_renderer->drawCircle(armX, armY, s * 0.12f, 0.92f, 0.92f, 0.95f, 1.0f, 24);
}

void RobotGles::drawGroundShadow(float cx, float baseY, float w) {
    if (!m_renderer) return;
    m_renderer->drawCircle(cx, baseY, w * 0.5f, 0.0f, 0.0f, 0.0f, 0.2f, 32);
}

void RobotGles::drawSpartanCrest(float cx, float headY) {
    if (!m_renderer) return;
    float s = m_robotSize;
    // Golden Spartan helmet crest on top of head
    // Center ridge
    m_renderer->drawRect(cx - s * 0.03f, headY - s * 0.2f, s * 0.06f, s * 0.2f,
                         0.85f, 0.7f, 0.2f, 1.0f);
    // Crest feathers (red)
    for (int i = 0; i < 5; i++) {
        float fx = cx - s * 0.08f + i * s * 0.04f;
        float fy = headY - s * 0.25f - sinf(m_animTime * 2.0f + i * 0.5f) * 2.0f;
        m_renderer->drawCircle(fx, fy, s * 0.04f, 0.8f, 0.15f, 0.15f, 0.9f, 12);
    }
}

void RobotGles::drawThinkingDots(float cx, float cy) {
    if (!m_renderer) return;
    float s = m_robotSize;
    int phase = (int)(m_animTime * 3.0f) % 3;
    for (int i = 0; i < 3; i++) {
        float alpha = (i == phase) ? 1.0f : 0.3f;
        float dx = cx - s * 0.1f + i * s * 0.1f;
        m_renderer->drawCircle(dx, cy, s * 0.04f, 0.2f, 0.6f, 1.0f, alpha, 16);
    }
}
