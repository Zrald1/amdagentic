#include "robot_renderer.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <windows.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// EVE palette
static const D2D1::ColorF C_WHITE       (0xFFFCFCFC);
static const D2D1::ColorF C_WHITE_LIGHT (0xFFFFFFFF);
static const D2D1::ColorF C_WHITE_SHADE (0xFFE4E4E4);
static const D2D1::ColorF C_WHITE_MID   (0xFFF1F1F1);
static const D2D1::ColorF C_BLACK       (0xFF141414);
static const D2D1::ColorF C_NEON        (0xFF00CFFF); // sharp neon blue (no soft glow)
static const D2D1::ColorF C_NEON_BRIGHT (0xFF7CF0FF);
static const D2D1::ColorF C_NEON_DIM    (0xFF006E8C);
static const D2D1::ColorF C_PLANT       (0xFF00FF66);
static const D2D1::ColorF C_SHADOW      (0xFFEFEFEF);
static const D2D1::ColorF C_GLOW        (0xFFCFF0FF);

static float GetIdleDuration() {
    return 25.0f + (float)(rand() % 10);
}

static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float EaseOutQuad(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
static inline float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

// Builds EVE's teardrop body path: wide rounded top, tapering to a smooth
// rounded point at the bottom.
static void FillEveBodyPath(ID2D1Factory* factory, ID2D1RenderTarget* target,
                             float cx, float centerY, float scale,
                             float offsetX, float offsetY, ID2D1Brush* brush) {
    ComPtr<ID2D1PathGeometry> geo;
    factory->CreatePathGeometry(geo.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink;
    geo->Open(sink.GetAddressOf());

    auto pt = [&](float x, float y) {
        return D2D1::Point2F(cx + offsetX + x * scale, centerY + offsetY + y * scale);
    };

    sink->BeginFigure(pt(0, -44), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment(pt(20, -44), pt(32, -34), pt(32, -16)));
    sink->AddBezier(D2D1::BezierSegment(pt(32, 10), pt(20, 45), pt(0, 54)));
    sink->AddBezier(D2D1::BezierSegment(pt(-20, 45), pt(-32, 10), pt(-32, -16)));
    sink->AddBezier(D2D1::BezierSegment(pt(-32, -34), pt(-20, -44), pt(0, -44)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    target->FillGeometry(geo.Get(), brush);
}

RobotRenderer::RobotRenderer() {
    srand((unsigned)time(nullptr));
    memset(m_lastKeyState, 0, sizeof(m_lastKeyState));
}

RobotRenderer::~RobotRenderer() {
    if (m_target) m_target->Release();
}

bool RobotRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_factory.GetAddressOf());
    if (FAILED(hr)) return false;

    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    hr = m_factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        m_target.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    // Color-key transparency needs hard (non-anti-aliased) edges, otherwise
    // blended pixels against magenta show up as a pink/purple fringe.
    m_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

    m_target->CreateSolidColorBrush(C_WHITE,       m_brushWhite.GetAddressOf());
    m_target->CreateSolidColorBrush(C_WHITE_LIGHT, m_brushWhiteLight.GetAddressOf());
    m_target->CreateSolidColorBrush(C_WHITE_SHADE, m_brushWhiteShade.GetAddressOf());
    m_target->CreateSolidColorBrush(C_WHITE_MID,   m_brushWhiteMid.GetAddressOf());
    m_target->CreateSolidColorBrush(C_BLACK,       m_brushBlack.GetAddressOf());
    m_target->CreateSolidColorBrush(C_NEON,        m_brushNeon.GetAddressOf());
    m_target->CreateSolidColorBrush(C_NEON_BRIGHT, m_brushNeonBright.GetAddressOf());
    m_target->CreateSolidColorBrush(C_NEON_DIM,    m_brushNeonDim.GetAddressOf());
    m_target->CreateSolidColorBrush(C_PLANT,       m_brushPlant.GetAddressOf());
    m_target->CreateSolidColorBrush(C_SHADOW,      m_brushShadow.GetAddressOf());
    m_target->CreateSolidColorBrush(C_GLOW,        m_brushGlow.GetAddressOf());

    return true;
}

void RobotRenderer::OnResize(int width, int height) {
    if (m_target) m_target->Resize(D2D1::SizeU(width, height));
}

// ── Screen edges ─────────────────────────────────────────────────────

float RobotRenderer::GetScreenLeftEdge() { return 10.0f; }

float RobotRenderer::GetScreenRightEdge() {
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    return (float)(workArea.right - 230);
}

void RobotRenderer::WalkToEdge(bool left) {
    m_targetX = left ? GetScreenLeftEdge() : GetScreenRightEdge();
    m_facingRight = !left;
    m_userWalking = true;
    SetState(RobotState::Walking, 999.0f);
}

// ── Keyboard detection ───────────────────────────────────────────────

void RobotRenderer::CheckKeyboardActivity() {
    m_keyboardCheckTimer += 0.016f;
    if (m_keyboardCheckTimer < 0.3f) return;
    m_keyboardCheckTimer = 0.0f;

    HWND fgWnd = GetForegroundWindow();
    if (fgWnd != m_lastForegroundWnd && m_lastForegroundWnd != nullptr) {
        float le = GetScreenLeftEdge(), re = GetScreenRightEdge();
        float ms = le + (re - le) / 3.0f, me = le + 2.0f * (re - le) / 3.0f;
        if (m_screenX > ms && m_screenX < me) {
            WalkToEdge(m_screenX < (le + re) / 2.0f);
            m_typingCooldown = 30.0f;
        }
    }
    m_lastForegroundWnd = fgWnd;

    bool anyKey = false;
    for (int vk = 0x08; vk <= 0x5A; vk++) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down && !m_lastKeyState[vk]) anyKey = true;
        m_lastKeyState[vk] = down;
    }
    for (int vk : {0x20, 0x0D}) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down && !m_lastKeyState[vk]) anyKey = true;
        m_lastKeyState[vk] = down;
    }

    if (anyKey && m_typingCooldown <= 0) {
        float le = GetScreenLeftEdge(), re = GetScreenRightEdge();
        float ms = le + (re - le) / 3.0f, me = le + 2.0f * (re - le) / 3.0f;
        if (m_screenX > ms && m_screenX < me) {
            WalkToEdge(m_screenX < (le + re) / 2.0f);
            m_typingCooldown = 30.0f;
        }
    }
}

// ── Mouse interaction ────────────────────────────────────────────────

void RobotRenderer::OnMouseDown(int mouseX, int mouseY) {
    m_mouseDown = true;
    m_dragging = false;
    m_holdTimer = 0.0f;
    m_downX = mouseX;
    m_downY = mouseY;
    m_dragStartX = m_screenX;
    m_dragStartY = m_screenY;
}

void RobotRenderer::OnMouseMove(int mouseX, int mouseY) {
    if (!m_mouseDown) return;
    float dx = (float)(mouseX - m_downX);
    float dy = (float)(mouseY - m_downY);
    m_holdTimer += 0.016f;
    if (!m_dragging && (m_holdTimer > 0.25f || dx * dx + dy * dy > 81.0f)) {
        m_dragging = true;
        SetState(RobotState::Idle, 999.0f);
    }
    if (m_dragging) {
        m_screenX = m_dragStartX + dx;
        m_screenY = m_dragStartY - dy;
        MoveWindowToRobot();
    }
}

void RobotRenderer::OnMouseUp(int mouseX, int mouseY) {
    if (m_dragging) {
        m_dragging = false;
        m_mouseDown = false;
        SetState(RobotState::Idle, GetIdleDuration());
        return;
    }
    if (m_mouseDown) {
        m_mouseDown = false;
        ClickRegion region = HitTest(mouseX, mouseY);
        switch (region) {
            case ClickRegion::Head:
                m_wantsInputDialog = true;
                SetState(RobotState::Working, 999.0f);
                break;
            case ClickRegion::Arms:
                SetState(RobotState::Greeting, 2.0f);
                break;
            case ClickRegion::Bottom: {
                float center = (GetScreenLeftEdge() + GetScreenRightEdge()) / 2.0f;
                WalkToEdge(m_screenX >= center);
                break;
            }
            default:
                SetState(RobotState::Greeting, 1.8f);
                break;
        }
    }
}

ClickRegion RobotRenderer::HitTest(int mouseX, int mouseY) {
    if (!m_target) return ClickRegion::None;
    D2D1_SIZE_F size = m_target->GetSize();
    float cx = size.width / 2;
    float cy = size.height / 2;
    float fx = (float)mouseX;
    float fy = (float)mouseY;

    if (fy < cy - 46) return ClickRegion::Head;
    if (fy > cy + 30) return ClickRegion::Bottom;
    if (fx < cx - 33 || fx > cx + 33) return ClickRegion::Arms;
    return ClickRegion::None;
}

// ── State machine ────────────────────────────────────────────────────

void RobotRenderer::SetState(RobotState state, float duration) {
    m_state = state;
    m_stateTimer = 0.0f;
    m_nextBehaviorTime = duration;
}

void RobotRenderer::PickNextBehavior() {
    float roll = (float)rand() / RAND_MAX;
    if (roll < 0.15f) {
        SetState(RobotState::Greeting, 1.5f);
    } else if (roll < 0.85f) {
        m_userWalking = false;
        WalkToEdge(rand() % 2 == 0);
    } else {
        SetState(RobotState::Sleeping, 15.0f);
    }
}

void RobotRenderer::MoveWindowToRobot() {
    if (!m_hwnd) return;
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    int winW = 220, winH = 220;
    int x = (int)m_screenX;
    int y = workArea.bottom - winH - 10 - (int)m_screenY;
    if (x < 0) x = 0;
    if (x + winW > workArea.right) x = workArea.right - winW;
    if (y < workArea.top) y = workArea.top;
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, winW, winH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// Dynamic lean: a quick "launch" dash up toward a steep angle, settling to
// a lower cruising lean, then easing back upright as EVE nears her target.
float RobotRenderer::ComputeWalkLeanDeg() const {
    float distToTarget = fabsf(m_targetX - m_screenX);
    const float peakLean = 75.0f;
    const float cruiseLean = 26.0f;
    const float rampDur = 0.18f;
    const float settleDur = 0.28f;

    float lean;
    if (m_stateTimer < rampDur) {
        lean = peakLean * EaseOutQuad(Clamp01(m_stateTimer / rampDur));
    } else {
        float t = Clamp01((m_stateTimer - rampDur) / settleDur);
        lean = peakLean + (cruiseLean - peakLean) * SmoothStep(t);
    }

    const float arrivalDist = 55.0f;
    float arrivalFactor = Clamp01(distToTarget / arrivalDist);
    lean *= arrivalFactor;

    return lean * (m_facingRight ? 1.0f : -1.0f);
}

void RobotRenderer::Update() {
    float dt = 0.016f;
    m_animTime += dt;
    m_stateTimer += dt;
    if (m_typingCooldown > 0) m_typingCooldown -= dt;

    // Cycle idle eye expressions so EVE feels alive, not a static stare.
    if (m_state == RobotState::Idle && !m_dragging) {
        m_eyeExprTimer -= dt;
        if (m_eyeExprTimer <= 0.0f) {
            static const EyeExpression pool[] = {
                EyeExpression::Neutral, EyeExpression::Happy, EyeExpression::Curious,
                EyeExpression::Surprised, EyeExpression::Wink, EyeExpression::Suspicious
            };
            m_eyeExpression = pool[rand() % 6];
            m_eyeExprTimer = 6.0f + (float)(rand() % 7);
        }
    }

    if (m_state != RobotState::Working && m_state != RobotState::Walking && !m_dragging)
        CheckKeyboardActivity();
    if (m_state == RobotState::Walking && !m_dragging) {
        float dx = m_targetX - m_screenX;
        if (fabs(dx) < 3.0f) {
            m_screenX = m_targetX;
            m_userWalking = false;
            SetState(RobotState::Idle, GetIdleDuration());
        } else {
            float step = m_hoverSpeed * dt;
            if (fabs(dx) < step) step = fabs(dx);
            if (dx > 0) { m_screenX += step; m_facingRight = true; }
            else { m_screenX -= step; m_facingRight = false; }
        }
        MoveWindowToRobot();
    }
    if (!m_dragging && m_stateTimer >= m_nextBehaviorTime) {
        if (m_state == RobotState::Working || m_state == RobotState::Greeting)
            SetState(RobotState::Idle, GetIdleDuration());
        else if (m_state == RobotState::Walking) {
            if (fabs(m_targetX - m_screenX) < 3.0f)
                SetState(RobotState::Idle, GetIdleDuration());
        } else
            PickNextBehavior();
    }
}

void RobotRenderer::Render() {
    if (!m_target) return;
    m_target->BeginDraw();
    m_target->Clear(D2D1::ColorF(D2D1::ColorF::Magenta, 1.0f));
    DrawRobot();
    m_target->EndDraw();
}

// ── Drawing ───────────────────────────────────────────────────────────

void RobotRenderer::DrawGroundShadow(float cx, float baseY, float w) {
    D2D1_ELLIPSE ell = D2D1::Ellipse(D2D1::Point2F(cx, baseY), w / 2, w / 10);
    m_target->FillEllipse(ell, m_brushShadow.Get());
}

void RobotRenderer::DrawHoverGlow(float cx, float baseY) {
    float pulse = 0.85f + 0.15f * sinf(m_animTime * 3.0f * (float)M_PI);
    float r = 20 * pulse;
    D2D1_ELLIPSE glow = D2D1::Ellipse(D2D1::Point2F(cx, baseY), r, r * 0.32f);
    m_target->FillEllipse(glow, m_brushGlow.Get());
}

void RobotRenderer::DrawEggBody(float cx, float cy, float bob) {
    float centerY = cy + bob;

    FillEveBodyPath(m_factory.Get(), m_target.Get(), cx, centerY, 1.03f, 2.0f, 2.0f, m_brushWhiteShade.Get());
    FillEveBodyPath(m_factory.Get(), m_target.Get(), cx, centerY, 1.0f, 0.0f, 0.0f, m_brushWhite.Get());
    FillEveBodyPath(m_factory.Get(), m_target.Get(), cx, centerY, 0.90f, 9.0f, 0.0f, m_brushWhiteMid.Get());
    FillEveBodyPath(m_factory.Get(), m_target.Get(), cx, centerY, 0.76f, 13.0f, 0.0f, m_brushWhiteShade.Get());
    FillEveBodyPath(m_factory.Get(), m_target.Get(), cx, centerY, 0.92f, -4.0f, 0.0f, m_brushWhite.Get());

    D2D1_ELLIPSE hl = D2D1::Ellipse(D2D1::Point2F(cx - 13, centerY - 16), 7, 18);
    m_target->FillEllipse(hl, m_brushWhiteLight.Get());
    D2D1_ELLIPSE hl2 = D2D1::Ellipse(D2D1::Point2F(cx - 16, centerY - 22), 3.5f, 9);
    m_target->FillEllipse(hl2, m_brushWhiteLight.Get());
}

// A single crisp neon eye — fill + brighter inner + white spark. No soft
// outer glow ring, so the color reads as sharp neon rather than a blur.
void RobotRenderer::DrawSingleEye(float ex, float ey, float halfW, float halfH, float rotationDeg) {
    bool rotated = fabsf(rotationDeg) > 0.01f;
    if (rotated) {
        m_target->SetTransform(D2D1::Matrix3x2F::Rotation(rotationDeg, D2D1::Point2F(ex, ey)));
    }

    D2D1_ELLIPSE eye = D2D1::Ellipse(D2D1::Point2F(ex, ey), halfW, halfH);
    m_target->FillEllipse(eye, m_brushNeon.Get());

    D2D1_ELLIPSE inner = D2D1::Ellipse(D2D1::Point2F(ex, ey - halfH * 0.15f), halfW * 0.6f, halfH * 0.65f);
    m_target->FillEllipse(inner, m_brushNeonBright.Get());

    D2D1_ELLIPSE core = D2D1::Ellipse(D2D1::Point2F(ex, ey - halfH * 0.25f), halfW * 0.22f, halfH * 0.3f);
    m_target->FillEllipse(core, m_brushWhiteLight.Get());

    if (rotated) {
        m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    }
}

// EVE's head with expressive eyes. `sleeping` overrides expr with closed eyes.
// `turnOffsetX` shifts just the visor + eyes sideways within the head shell,
// so she visually turns to look toward the direction of travel — like
// checking the road ahead — instead of always facing the viewer.
void RobotRenderer::DrawHead(float cx, float headY, EyeExpression expr, bool sleeping, float turnOffsetX) {
    float headW = 27.0f;
    float headH = 22.0f;

    D2D1_ELLIPSE headShadow = D2D1::Ellipse(D2D1::Point2F(cx + 1.5f, headY + 1.5f), headW + 1, headH + 1);
    m_target->FillEllipse(headShadow, m_brushWhiteShade.Get());

    D2D1_ELLIPSE head = D2D1::Ellipse(D2D1::Point2F(cx, headY), headW, headH);
    m_target->FillEllipse(head, m_brushWhite.Get());

    D2D1_ELLIPSE shade1 = D2D1::Ellipse(D2D1::Point2F(cx + 6, headY), headW - 6, headH - 4);
    m_target->FillEllipse(shade1, m_brushWhiteMid.Get());
    D2D1_ELLIPSE shade2 = D2D1::Ellipse(D2D1::Point2F(cx + 9, headY), headW - 11, headH - 7);
    m_target->FillEllipse(shade2, m_brushWhiteShade.Get());

    D2D1_ELLIPSE blend = D2D1::Ellipse(D2D1::Point2F(cx - 2, headY), headW - 4, headH - 2);
    m_target->FillEllipse(blend, m_brushWhite.Get());

    D2D1_ELLIPSE hl = D2D1::Ellipse(D2D1::Point2F(cx - 9, headY - 7), 5, 10);
    m_target->FillEllipse(hl, m_brushWhiteLight.Get());

    // Face visor — shifted toward the direction of travel to "look ahead"
    float fx = cx + turnOffsetX;
    float visorW = 42.0f;
    float visorH = 25.0f;
    D2D1_ELLIPSE visor = D2D1::Ellipse(D2D1::Point2F(fx, headY - 1), visorW / 2, visorH / 2);
    m_target->FillEllipse(visor, m_brushBlack.Get());

    float eyeY = headY - 3;

    if (sleeping) {
        m_target->DrawLine(D2D1::Point2F(fx - 11, eyeY), D2D1::Point2F(fx - 3, eyeY), m_brushNeonDim.Get(), 2.0f);
        m_target->DrawLine(D2D1::Point2F(fx + 3, eyeY), D2D1::Point2F(fx + 11, eyeY), m_brushNeonDim.Get(), 2.0f);
        return;
    }

    switch (expr) {
        case EyeExpression::Happy: {
            // Squinted, corners turned outward/upward — a joyful look
            DrawSingleEye(fx - 9, eyeY + 1, 8, 4, -16.0f);
            DrawSingleEye(fx + 9, eyeY + 1, 8, 4, 16.0f);
            break;
        }
        case EyeExpression::Curious: {
            // One eye bigger + raised — quizzical, head-tilt energy
            DrawSingleEye(fx - 9, eyeY - 2, 9.5f, 7, -8.0f);
            DrawSingleEye(fx + 9, eyeY, 6.5f, 5, 0.0f);
            break;
        }
        case EyeExpression::Surprised: {
            DrawSingleEye(fx - 9, eyeY, 9, 8.5f, 0.0f);
            DrawSingleEye(fx + 9, eyeY, 9, 8.5f, 0.0f);
            break;
        }
        case EyeExpression::Suspicious: {
            // Narrow, slanted inward — skeptical squint
            DrawSingleEye(fx - 9, eyeY, 8, 3, 14.0f);
            DrawSingleEye(fx + 9, eyeY, 8, 3, -14.0f);
            break;
        }
        case EyeExpression::Wink: {
            DrawSingleEye(fx - 9, eyeY, 8, 5.5f, 0.0f);
            m_target->DrawLine(D2D1::Point2F(fx + 3, eyeY), D2D1::Point2F(fx + 15, eyeY), m_brushNeon.Get(), 2.2f);
            break;
        }
        case EyeExpression::Neutral:
        default: {
            DrawSingleEye(fx - 9, eyeY, 8, 5.5f, 0.0f);
            DrawSingleEye(fx + 9, eyeY, 8, 5.5f, 0.0f);
            break;
        }
    }
}

// EVE's arms: slim leaf/fin shapes that float beside the body with a
// constant gap. `leanDeg` sweeps both arms backward together with the
// body while moving. `dislocate` pops the arm further out of its resting
// spot — used for the "say hi" wave, since her parts are magnetically
// detached and can move freely rather than just rotate in place.
void RobotRenderer::DrawArm(float cx, float cy, float bob, bool left, bool waving, float waveAngle,
                             float leanDeg, float dislocate, float squashX) {
    float armHalfW = 6.5f;
    float armHalfH = 32.0f;
    float gap = 9.0f;
    float bodyHalfW = 32.0f;

    float restX = cx + (left ? -1.0f : 1.0f) * (bodyHalfW + gap + armHalfW);
    float restY = cy + 6.0f + bob;

    // Pop the arm outward and upward when dislocated (mid-wave)
    float baseX = restX + (left ? -1.0f : 1.0f) * 16.0f * dislocate;
    float baseY = restY - 12.0f * dislocate;

    float angleDeg = 0.0f;
    if (waving && !left) angleDeg += 34.0f * sinf(waveAngle);
    angleDeg += leanDeg;

    bool transformed = fabsf(angleDeg) > 0.01f || fabsf(squashX - 1.0f) > 0.001f;
    D2D1_POINT_2F pivot = D2D1::Point2F(cx + (left ? -1.0f : 1.0f) * bodyHalfW * 0.5f, cy - 18 + bob);
    if (transformed) {
        // Squash first (turning the fin edge-on), then rotate/sweep it
        // backward with the body's lean — a real twist, not just a tilt.
        D2D1::Matrix3x2F squash = D2D1::Matrix3x2F::Scale(D2D1::SizeF(squashX, 1.0f), pivot);
        D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(angleDeg, pivot);
        m_target->SetTransform(squash * rot);
    }

    D2D1_ELLIPSE arm = D2D1::Ellipse(D2D1::Point2F(baseX, baseY), armHalfW, armHalfH);
    m_target->FillEllipse(arm, m_brushWhite.Get());

    float shadeX = baseX + (left ? -2.0f : 2.0f);
    D2D1_ELLIPSE shade = D2D1::Ellipse(D2D1::Point2F(shadeX, baseY), armHalfW - 2.0f, armHalfH - 5.0f);
    m_target->FillEllipse(shade, m_brushWhiteMid.Get());

    float hlX = baseX + (left ? 2.0f : -2.0f);
    D2D1_ELLIPSE hl = D2D1::Ellipse(D2D1::Point2F(hlX, baseY - 6.0f), armHalfW - 3.5f, armHalfH - 12.0f);
    m_target->FillEllipse(hl, m_brushWhiteLight.Get());

    if (transformed) {
        m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    }
}

void RobotRenderer::DrawPlantSymbol(float cx, float cy) {
    float symY = cy - 6;
    D2D1_ELLIPSE glow = D2D1::Ellipse(D2D1::Point2F(cx, symY), 6.5f, 6.5f);
    m_target->FillEllipse(glow, m_brushPlant.Get());
    D2D1_ELLIPSE center = D2D1::Ellipse(D2D1::Point2F(cx, symY), 4.5f, 4.5f);
    m_target->FillEllipse(center, m_brushBlack.Get());
    D2D1_ELLIPSE leaf = D2D1::Ellipse(D2D1::Point2F(cx, symY - 0.5f), 2.5f, 3.5f);
    m_target->FillEllipse(leaf, m_brushPlant.Get());
    D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(cx, symY), 1.0f, 1.0f);
    m_target->FillEllipse(dot, m_brushWhiteLight.Get());
}

void RobotRenderer::DrawGear(float cx, float cy, float r) {
    D2D1_ELLIPSE gear = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);
    m_target->FillEllipse(gear, m_brushWhiteMid.Get());
    D2D1_ELLIPSE inner = D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 0.5f, r * 0.5f);
    m_target->FillEllipse(inner, m_brushBlack.Get());
    D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 0.2f, r * 0.2f);
    m_target->FillEllipse(dot, m_brushNeon.Get());
}

void RobotRenderer::DrawRobot() {
    D2D1_SIZE_F size = m_target->GetSize();
    float cx = size.width / 2;
    float cy = size.height / 2;
    float t = m_animTime;

    float bob = 4.0f * sinf(t * 1.5f * (float)M_PI);
    float headY = cy + bob - 74.0f;

    if (m_dragging) {
        float dBob = bob + 6.0f;
        float dHeadY = cy + dBob - 74.0f;
        DrawGroundShadow(cx, cy + dBob + 66, 36);
        DrawEggBody(cx, cy, dBob);
        DrawHead(cx, dHeadY, EyeExpression::Surprised, false);
        DrawArm(cx, cy, dBob, true, false, 0);
        DrawArm(cx, cy, dBob, false, false, 0);
        return;
    }

    switch (m_state) {
        case RobotState::Idle: {
            DrawGroundShadow(cx, cy + bob + 66, 44);
            DrawHoverGlow(cx, cy + bob + 66);
            DrawEggBody(cx, cy, bob);
            DrawHead(cx, headY, m_eyeExpression, false);
            DrawArm(cx, cy, bob, true, false, 0);
            DrawArm(cx, cy, bob, false, false, 0);
            DrawPlantSymbol(cx, cy + bob);
            break;
        }
        case RobotState::Walking: {
            float gBob = 6.0f * sinf(t * 2.0f * (float)M_PI);
            float gHeadY = cy + gBob - 74.0f;

            DrawGroundShadow(cx, cy + gBob + 66, 40);
            DrawHoverGlow(cx, cy + gBob + 66);

            // Lean the body + head into the direction of travel, like a
            // fast forward dash, then level out as EVE nears the target.
            float leanDeg = ComputeWalkLeanDeg();

            // Turn amount tracks the lean strength: strongest right as she
            // launches, easing back to a full frontal view as she arrives.
            float turnAmount = Clamp01(fabsf(leanDeg) / 75.0f);
            float bodyScaleX = 1.0f - 0.34f * turnAmount;   // squash = turning edge-on
            float faceOffsetX = 11.0f * turnAmount * (m_facingRight ? 1.0f : -1.0f);

            D2D1_POINT_2F pivot = D2D1::Point2F(cx, cy + gBob);
            D2D1::Matrix3x2F squash = D2D1::Matrix3x2F::Scale(D2D1::SizeF(bodyScaleX, 1.0f), pivot);
            D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(leanDeg, pivot);
            m_target->SetTransform(squash * rot);
            DrawEggBody(cx, cy, gBob);
            DrawHead(cx, gHeadY, EyeExpression::Neutral, false, faceOffsetX);
            DrawPlantSymbol(cx, cy + gBob);
            m_target->SetTransform(D2D1::Matrix3x2F::Identity());

            // Arms trail slightly more than the body — a real twist, not
            // just a tilt, sweeping backward as she turns to look ahead.
            float armLean = leanDeg * 1.15f;
            float armScaleX = 1.0f - 0.22f * turnAmount;
            DrawArm(cx, cy, gBob, true, false, 0, armLean, 0.0f, armScaleX);
            DrawArm(cx, cy, gBob, false, false, 0, armLean, 0.0f, armScaleX);
            break;
        }
        case RobotState::Greeting: {
            float waveAngle = t * 6.0f * (float)M_PI;
            DrawGroundShadow(cx, cy + bob + 66, 44);
            DrawHoverGlow(cx, cy + bob + 66);
            DrawEggBody(cx, cy, bob);
            DrawHead(cx, headY, EyeExpression::Happy, false);

            // The waving hand pops out of its resting spot before settling
            // into the wave, then eases back in before returning to idle.
            float total = m_nextBehaviorTime;
            float dislocate;
            if (m_stateTimer < 0.22f) {
                dislocate = EaseOutQuad(Clamp01(m_stateTimer / 0.22f));
            } else if (m_stateTimer > total - 0.3f) {
                dislocate = SmoothStep(Clamp01((total - m_stateTimer) / 0.3f));
            } else {
                dislocate = 1.0f;
            }

            DrawArm(cx, cy, bob, true, false, 0);
            DrawArm(cx, cy, bob, false, true, waveAngle, 0.0f, dislocate);
            DrawPlantSymbol(cx, cy + bob);
            break;
        }
        case RobotState::Working: {
            float wBob = 3.0f * sinf(t * 2.5f * (float)M_PI);
            float wHeadY = cy + wBob - 74.0f;
            DrawGroundShadow(cx, cy + wBob + 66, 42);
            DrawHoverGlow(cx, cy + wBob + 66);
            DrawEggBody(cx, cy, wBob);
            DrawHead(cx, wHeadY, EyeExpression::Curious, false);
            DrawArm(cx, cy - 10 + wBob, wBob, true, false, 0);
            DrawArm(cx, cy - 10 + wBob, wBob, false, false, 0);
            DrawPlantSymbol(cx, cy + wBob);
            DrawGear(cx - 12, cy - 102 + wBob + 3 * sinf(t * 4 * (float)M_PI), 6);
            DrawGear(cx + 12, cy - 106 + wBob + 3 * sinf(t * 4 * (float)M_PI + (float)M_PI), 5);
            break;
        }
        case RobotState::Celebrating: {
            float cBob = 5.0f * sinf(t * 3.0f * (float)M_PI);
            float cHeadY = cy + cBob - 74.0f;
            DrawGroundShadow(cx, cy + cBob + 66, 46);
            DrawHoverGlow(cx, cy + cBob + 66);
            DrawEggBody(cx, cy, cBob);
            DrawHead(cx, cHeadY, EyeExpression::Happy, false);
            DrawArm(cx, cy - 10 + cBob, cBob, true, false, 0);
            DrawArm(cx, cy - 10 + cBob, cBob, false, false, 0);
            DrawPlantSymbol(cx, cy + cBob);
            for (int i = 0; i < 6; i++) {
                float angle = (i / 6.0f) * 2.0f * (float)M_PI + t * 2;
                float dist = 40 + 8 * sinf(t * 5 * (float)M_PI + i);
                float sx = cx + dist * cosf(angle);
                float sy = cy + dist * sinf(angle);
                D2D1_ELLIPSE sp = D2D1::Ellipse(D2D1::Point2F(sx, sy), 3, 3);
                m_target->FillEllipse(sp, m_brushNeon.Get());
            }
            break;
        }
        case RobotState::Sleeping: {
            float sBob = 2.0f * sinf(t * 0.8f * (float)M_PI);
            float sHeadY = cy + sBob - 74.0f;
            DrawGroundShadow(cx, cy + sBob + 66, 40);
            DrawEggBody(cx, cy, sBob);
            DrawHead(cx, sHeadY, EyeExpression::Neutral, true);
            DrawArm(cx, cy, sBob, true, false, 0);
            DrawArm(cx, cy, sBob, false, false, 0);
            break;
        }
    }
}
