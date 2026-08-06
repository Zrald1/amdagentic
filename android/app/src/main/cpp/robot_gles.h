#pragma once
#include "egl_renderer.h"
#include <string>

enum class AndroidRobotState {
    Idle,
    Thinking,
    Talking,
    Sleeping
};

class RobotGles {
public:
    RobotGles();
    ~RobotGles();

    void init(EglRenderer* renderer);
    void update(float dt);
    void render();

    void setThinking(bool thinking) { m_thinking = thinking; }
    void setTalking(bool talking) { m_talking = talking; }
    void onTouch(float x, float y, int action);

private:
    EglRenderer* m_renderer = nullptr;
    float m_animTime = 0.0f;
    float m_bob = 0.0f;
    bool m_thinking = false;
    bool m_talking = false;
    AndroidRobotState m_state = AndroidRobotState::Idle;
    float m_stateTimer = 0.0f;
    float m_eyeBlinkTimer = 3.0f;
    bool m_blinking = false;
    float m_blinkDuration = 0.0f;

    // Robot position (center of screen, hovering)
    float m_cx = 0.0f;
    float m_cy = 0.0f;
    float m_robotSize = 100.0f;

    void drawEggBody(float cx, float cy, float bob);
    void drawHead(float cx, float headY, float bob);
    void drawEye(float ex, float ey, float w, float h, bool thinking);
    void drawHoverGlow(float cx, float baseY);
    void drawArm(float cx, float cy, float bob, bool left, float waveAngle);
    void drawGroundShadow(float cx, float baseY, float w);
    void drawSpartanCrest(float cx, float headY);
    void drawSpeechBubble(float cx, float cy, const std::string& text);
    void drawThinkingDots(float cx, float cy);
};
