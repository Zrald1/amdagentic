// Aria — Agentic AI Desktop Companion (C++ native Win32 + Direct2D)

#include "window_manager.h"
#include "tray_icon.h"
#include "robot_renderer.h"
#include "agent_client.h"
#include "resource.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <richedit.h>

#pragma comment(lib, "comctl32.lib")

static HMODULE g_richEditDll = nullptr;

// Custom message for async chat response
#define WM_CHAT_RESPONSE (WM_USER + 100)
#define WM_CHAT_ERROR    (WM_USER + 101)

// Chat history entry: user message + assistant response
struct ChatEntry {
    std::wstring userMsg;
    std::wstring assistantMsg;
    bool isVoice = false;
};
static std::vector<ChatEntry> g_chatHistory;
static std::atomic<bool> g_chatInProgress(false);
static std::wstring g_lastUserMsg;
static std::wstring g_pendingResponse;

// Control ID for conversation display (needed by RefreshConversation)
#define IDC_BUBBLE_CONVO 3015

// Loading animation timer
#define IDT_LOADING 1002
static int g_loadingDots = 0;

// Refresh the conversation Rich Edit control with Messenger-style layout:
// User messages right-aligned, Aria messages left-aligned, all black text.
static void RefreshConversation(HWND hwnd) {
    HWND hConvo = GetDlgItem(hwnd, IDC_BUBBLE_CONVO);
    if (!hConvo) return;

    // Clear all text using WM_SETTEXT
    SetWindowTextW(hConvo, L"");

    // Default character format: black text
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = RGB(0, 0, 0);

    // Paragraph format for alignment
    PARAFORMAT2 pf = {};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_ALIGNMENT;

    for (const auto& entry : g_chatHistory) {
        // User message — right aligned (like Messenger)
        pf.wAlignment = PFA_RIGHT;
        SendMessageW(hConvo, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        SendMessageW(hConvo, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        std::wstring userLine = entry.userMsg + L"\r\n";
        SendMessageW(hConvo, EM_REPLACESEL, FALSE, (LPARAM)userLine.c_str());

        // Aria response — left aligned with "Aria:" prefix
        if (!entry.assistantMsg.empty()) {
            pf.wAlignment = PFA_LEFT;
            SendMessageW(hConvo, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
            SendMessageW(hConvo, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            std::wstring ariaLine = L"Aria: " + entry.assistantMsg + L"\r\n";
            SendMessageW(hConvo, EM_REPLACESEL, FALSE, (LPARAM)ariaLine.c_str());
        }

        // Blank line separator
        SendMessageW(hConvo, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    }

    // Loading indicator — always show when chat in progress
    if (g_chatInProgress.load()) {
        pf.wAlignment = PFA_LEFT;
        SendMessageW(hConvo, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        SendMessageW(hConvo, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        std::wstring loading = L"Aria: Thinking";
        for (int i = 0; i < g_loadingDots; i++) loading += L".";
        loading += L"\r\n";
        SendMessageW(hConvo, EM_REPLACESEL, FALSE, (LPARAM)loading.c_str());
    }

    // Scroll to bottom — move cursor to end and scroll into view
    LRESULT textLen = SendMessageW(hConvo, WM_GETTEXTLENGTH, 0, 0);
    SendMessageW(hConvo, EM_SETSEL, textLen, textLen);
    SendMessageW(hConvo, EM_SCROLLCARET, 0, 0);
}

// Global handles
static WindowManager* g_windowMgr = nullptr;
static RobotRenderer* g_renderer = nullptr;
static AgentClient* g_agent = nullptr;
static TrayIcon* g_tray = nullptr;

#define IDT_ANIMATE 1001
#define ANIMATE_INTERVAL_MS 16

// Manga bubble control IDs
#define IDC_BUBBLE_EDIT   3001
#define IDC_BUBBLE_SEND   3002
#define IDC_BUBBLE_VOICE  3003
#define IDC_BUBBLE_CANCEL 3004
#define IDC_BUBBLE_TITLE  3005
#define IDC_BUBBLE_SETTINGS 3006
#define IDC_BUBBLE_URL_LABEL 3007
#define IDC_BUBBLE_URL       3008
#define IDC_BUBBLE_KEY_LABEL 3009
#define IDC_BUBBLE_KEY       3010
#define IDC_BUBBLE_MODEL_LABEL 3011
#define IDC_BUBBLE_MODEL      3012
#define IDC_BUBBLE_HISTORY    3013
#define IDC_BUBBLE_HISTORY_LIST 3014
#define IDC_BUBBLE_EXPAND     3016
#define IDC_BUBBLE_SHRINK     3017

// ── Modern manga speech bubble — neon blue + white ─────────────────────
// Design: clean webtoon-style rounded rectangle, white background, neon
// blue (#00CFFF) outline with a soft glow halo, neon blue accent header
// bar, black text for readability.  Tail points down at the robot.
// Color-key transparency (magenta = transparent) makes only the bubble
// visible — no window frame, just the bubble shape.

// Neon blue palette (matches EVE's eye color)
#define NEON_BLUE      RGB(0, 207, 255)
#define NEON_BLUE_DIM  RGB(120, 220, 255)
#define NEON_BLUE_GLOW RGB(200, 245, 255)
#define NEON_BLUE_BAR  RGB(0, 180, 235)
#define TEXT_DARK      RGB(20, 20, 20)
#define TEXT_BLACK     RGB(0, 0, 0)
#define TEXT_NEON      RGB(0, 160, 220)

static HWND g_bubbleHwnd = nullptr;
static HFONT g_bubbleFont = nullptr;
static HFONT g_bubbleTitleFont = nullptr;
static HBRUSH g_bubbleBgBrush = nullptr;
static HBRUSH g_bubbleEditBgBrush = nullptr;
static bool g_settingsVisible = false;
static bool g_expanded = false;
static HFONT g_bubbleSmallFont = nullptr;

// Owner-draw button data for neon-styled buttons
struct NeonButton {
    const wchar_t* label;
    COLORREF textColor;
    COLORREF borderColor;
    COLORREF bgColor;
};

static NeonButton g_btnData[] = {
    { L"Send",     RGB(255,255,255), NEON_BLUE,      NEON_BLUE_BAR  },
    { L"Voice",    TEXT_DARK,        NEON_BLUE_DIM,  RGB(235,250,255) },
    { L"Set",      TEXT_DARK,        NEON_BLUE_DIM,  RGB(235,250,255) },
    { L"History",  TEXT_DARK,        NEON_BLUE_DIM,  RGB(235,250,255) },
    { L"Expand",   TEXT_DARK,        NEON_BLUE_DIM,  RGB(235,250,255) },
    { L"Exit",     TEXT_DARK,        RGB(180,180,180), RGB(245,245,245) },
};

static LRESULT CALLBACK BubbleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Subclass proc for input edit control — Enter key sends message
static WNDPROC g_origEditProc = nullptr;
static LRESULT CALLBACK InputEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        // Enter sends the message (don't insert newline)
        HWND hParent = GetParent(hwnd);
        if (hParent) {
            SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(IDC_BUBBLE_SEND, BN_CLICKED), 0);
        }
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_RETURN && (GetKeyState(VK_SHIFT) & 0x8000)) {
        // Shift+Enter = newline (pass through)
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK BubbleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_bubbleFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_bubbleTitleFont = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_bubbleSmallFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_bubbleBgBrush = CreateSolidBrush(RGB(255, 255, 255));
            g_bubbleEditBgBrush = CreateSolidBrush(RGB(250, 253, 255));

            // Title label — neon blue text
            HWND hTitle = CreateWindowExW(0, L"STATIC", L"Ask Aria",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                28, 12, 284, 22, hwnd, (HMENU)IDC_BUBBLE_TITLE, nullptr, nullptr);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_bubbleTitleFont, TRUE);

            // Conversation display — Rich Edit for Messenger-style alignment
            // Load rich edit library if not already loaded
            if (!g_richEditDll) g_richEditDll = LoadLibraryW(L"msftedit.dll");
            HWND hConvo = CreateWindowExW(0, L"RICHEDIT50W", L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                28, 38, 284, 120, hwnd, (HMENU)IDC_BUBBLE_CONVO, nullptr, nullptr);
            SendMessageW(hConvo, WM_SETFONT, (WPARAM)g_bubbleFont, TRUE);
            // White background, black text
            SendMessageW(hConvo, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(255, 255, 255));
            CHARFORMAT2W cf = {};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_COLOR;
            cf.crTextColor = RGB(0, 0, 0);
            SendMessageW(hConvo, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);

            // Input box — small, flat modern style, for typing new messages
            HWND hEdit = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                28, 166, 284, 32, hwnd, (HMENU)IDC_BUBBLE_EDIT, nullptr, nullptr);
            SendMessageW(hEdit, WM_SETFONT, (WPARAM)g_bubbleFont, TRUE);
            // Subclass to intercept Enter key for sending
            g_origEditProc = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)InputEditProc);

            // Owner-draw buttons — Send, Voice, Set, History, Expand, Exit
            HWND hSend = CreateWindowExW(0, L"BUTTON", L"Send",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                28, 206, 44, 24, hwnd, (HMENU)IDC_BUBBLE_SEND, nullptr, nullptr);
            SendMessageW(hSend, WM_SETFONT, (WPARAM)g_bubbleFont, TRUE);

            HWND hVoice = CreateWindowExW(0, L"BUTTON", L"Voice",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                76, 206, 44, 24, hwnd, (HMENU)IDC_BUBBLE_VOICE, nullptr, nullptr);
            SendMessageW(hVoice, WM_SETFONT, (WPARAM)g_bubbleFont, TRUE);

            HWND hSettings = CreateWindowExW(0, L"BUTTON", L"Set",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                124, 206, 36, 24, hwnd, (HMENU)IDC_BUBBLE_SETTINGS, nullptr, nullptr);
            SendMessageW(hSettings, WM_SETFONT, (WPARAM)g_bubbleFont, TRUE);

            HWND hHistory = CreateWindowExW(0, L"BUTTON", L"History",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                164, 206, 50, 24, hwnd, (HMENU)IDC_BUBBLE_HISTORY, nullptr, nullptr);
            SendMessageW(hHistory, WM_SETFONT, (WPARAM)g_bubbleFont, TRUE);

            HWND hExpand = CreateWindowExW(0, L"BUTTON", L"Expand",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                218, 206, 50, 24, hwnd, (HMENU)IDC_BUBBLE_EXPAND, nullptr, nullptr);
            SendMessageW(hExpand, WM_SETFONT, (WPARAM)g_bubbleFont, TRUE);

            HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Exit",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                272, 206, 40, 24, hwnd, (HMENU)IDC_BUBBLE_CANCEL, nullptr, nullptr);
            SendMessageW(hCancel, WM_SETFONT, (WPARAM)g_bubbleFont, TRUE);

            // Settings panel (hidden by default) — URL, API Key, Model
            HWND hUrlLabel = CreateWindowExW(0, L"STATIC", L"Base URL",
                WS_CHILD | SS_LEFT, 28, 244, 80, 16, hwnd, (HMENU)IDC_BUBBLE_URL_LABEL, nullptr, nullptr);
            SendMessageW(hUrlLabel, WM_SETFONT, (WPARAM)g_bubbleSmallFont, TRUE);

            HWND hKeyLabel = CreateWindowExW(0, L"STATIC", L"API Key",
                WS_CHILD | SS_LEFT, 28, 274, 80, 16, hwnd, (HMENU)IDC_BUBBLE_KEY_LABEL, nullptr, nullptr);
            SendMessageW(hKeyLabel, WM_SETFONT, (WPARAM)g_bubbleSmallFont, TRUE);

            HWND hModelLabel = CreateWindowExW(0, L"STATIC", L"Model",
                WS_CHILD | SS_LEFT, 28, 304, 80, 16, hwnd, (HMENU)IDC_BUBBLE_MODEL_LABEL, nullptr, nullptr);
            SendMessageW(hModelLabel, WM_SETFONT, (WPARAM)g_bubbleSmallFont, TRUE);

            HWND hUrl = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | ES_AUTOHSCROLL,
                100, 242, 212, 20, hwnd, (HMENU)IDC_BUBBLE_URL, nullptr, nullptr);
            SendMessageW(hUrl, WM_SETFONT, (WPARAM)g_bubbleSmallFont, TRUE);

            HWND hKey = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | ES_AUTOHSCROLL,
                100, 272, 212, 20, hwnd, (HMENU)IDC_BUBBLE_KEY, nullptr, nullptr);
            SendMessageW(hKey, WM_SETFONT, (WPARAM)g_bubbleSmallFont, TRUE);

            HWND hModel = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | ES_AUTOHSCROLL,
                100, 302, 212, 20, hwnd, (HMENU)IDC_BUBBLE_MODEL, nullptr, nullptr);
            SendMessageW(hModel, WM_SETFONT, (WPARAM)g_bubbleSmallFont, TRUE);

            // Pre-fill settings from g_agent defaults
            if (g_agent) {
                SetWindowTextW(hUrl, L"https://developer.amd.com.cn/radeon/api/v1");
                SetWindowTextW(hKey, L"rc-c042ad0acc56669f7b46e70f924189b5ac51664ce329f5b2");
                SetWindowTextW(hModel, L"Qwen3.6-35B-A3B");
            }

            // Show existing conversation if any
            RefreshConversation(hwnd);

            SetFocus(hEdit);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right, h = rc.bottom;

            // Double-buffer to avoid flicker
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

            // Fill with magenta (transparent via color key)
            HBRUSH magBrush = CreateSolidBrush(RGB(255, 0, 255));
            FillRect(memDC, &rc, magBrush);
            DeleteObject(magBrush);

            // Bubble geometry — body extends to include buttons
            int bL = 10, bT = 10, bR = w - 10, bB = h - 42;
            int radius = 22;
            int tailX = w / 2;
            int tailW = 16, tailH = 28;

            HPEN nullPen = (HPEN)GetStockObject(NULL_PEN);
            HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
            HBRUSH glowBrush = CreateSolidBrush(NEON_BLUE_GLOW);
            HBRUSH dimGlowBrush = CreateSolidBrush(NEON_BLUE_DIM);

            // ── 1. Glow halo: concentric rounded rects, progressively brighter ──
            // Outer glow (widest, lightest)
            SelectObject(memDC, nullPen);
            SelectObject(memDC, glowBrush);
            RoundRect(memDC, bL - 6, bT - 6, bR + 6, bB + 6, (radius + 6) * 2, (radius + 6) * 2);
            // Mid glow
            SelectObject(memDC, dimGlowBrush);
            RoundRect(memDC, bL - 3, bT - 3, bR + 3, bB + 3, (radius + 3) * 2, (radius + 3) * 2);

            // ── 2. Tail fill (white, behind bubble) ──
            SelectObject(memDC, nullPen);
            SelectObject(memDC, whiteBrush);
            POINT tail[] = {
                {tailX - tailW, bB - 2},
                {tailX + tailW, bB - 2},
                {tailX, bB + tailH}
            };
            Polygon(memDC, tail, 3);

            // ── 3. White bubble body ──
            RoundRect(memDC, bL, bT, bR, bB, radius * 2, radius * 2);

            // ── 4. Neon blue accent header bar (top inside bubble) ──
            HBRUSH barBrush = CreateSolidBrush(NEON_BLUE_BAR);
            // Draw a thin neon bar across the top of the bubble interior
            RECT barRect = {bL + 2, bT + 2, bR - 2, bT + 5};
            // Use a rounded clip by filling a thin rounded rect
            SelectObject(memDC, nullPen);
            SelectObject(memDC, barBrush);
            RoundRect(memDC, bL + 1, bT + 1, bR - 1, bT + 8, radius * 2, radius * 2);
            DeleteObject(barBrush);

            // ── 5. Neon blue outline (2.5px) ──
            HPEN neonPen = CreatePen(PS_SOLID, 2, NEON_BLUE);
            SelectObject(memDC, neonPen);
            SelectObject(memDC, GetStockObject(NULL_BRUSH));
            RoundRect(memDC, bL, bT, bR, bB, radius * 2, radius * 2);

            // ── 6. Erase bottom outline where tail connects ──
            SelectObject(memDC, nullPen);
            SelectObject(memDC, whiteBrush);
            RECT eraseRect = {tailX - tailW, bB - 2, tailX + tailW, bB + 2};
            FillRect(memDC, &eraseRect, whiteBrush);

            // ── 7. Tail outline (neon blue, two sides) ──
            SelectObject(memDC, neonPen);
            SelectObject(memDC, GetStockObject(NULL_BRUSH));
            MoveToEx(memDC, tailX - tailW, bB, nullptr);
            LineTo(memDC, tailX, bB + tailH);
            LineTo(memDC, tailX + tailW, bB);

            // ── 8. Tail glow (thin dim outline around tail) ──
            HPEN glowPen = CreatePen(PS_SOLID, 2, NEON_BLUE_DIM);
            SelectObject(memDC, glowPen);
            MoveToEx(memDC, tailX - tailW - 2, bB, nullptr);
            LineTo(memDC, tailX, bB + tailH + 2);
            LineTo(memDC, tailX + tailW + 2, bB);

            SelectObject(memDC, nullPen);
            DeleteObject(neonPen);
            DeleteObject(glowPen);
            DeleteObject(whiteBrush);
            DeleteObject(glowBrush);
            DeleteObject(dimGlowBrush);

            // ── 9. Modern thin borders around edit controls ──
            HPEN thinPen = CreatePen(PS_SOLID, 1, NEON_BLUE_DIM);
            SelectObject(memDC, thinPen);
            SelectObject(memDC, GetStockObject(NULL_BRUSH));
            // Conversation area border (rounded)
            RoundRect(memDC, 26, 36, 314, 160, 8, 8);
            // Input box border (rounded)
            RoundRect(memDC, 26, 164, 314, 200, 8, 8);
            DeleteObject(thinPen);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (!dis) return 0;
            int btnIdx = -1;
            switch (dis->CtlID) {
                case IDC_BUBBLE_SEND:     btnIdx = 0; break;
                case IDC_BUBBLE_VOICE:    btnIdx = 1; break;
                case IDC_BUBBLE_SETTINGS: btnIdx = 2; break;
                case IDC_BUBBLE_HISTORY:  btnIdx = 3; break;
                case IDC_BUBBLE_EXPAND:   btnIdx = 4; break;
                case IDC_BUBBLE_CANCEL:   btnIdx = 5; break;
            }
            if (btnIdx < 0) return 0;
            NeonButton* btn = &g_btnData[btnIdx];

            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;

            // Modern rounded background
            HBRUSH bgBrush = CreateSolidBrush(btn->bgColor);
            HPEN nullPen2 = (HPEN)GetStockObject(NULL_PEN);
            SelectObject(hdc, nullPen2);
            SelectObject(hdc, bgBrush);
            RoundRect(hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, 10, 10);
            DeleteObject(bgBrush);

            // Border (neon blue, thicker on press)
            int borderWidth = (dis->itemState & ODS_SELECTED) ? 2 : 1;
            HPEN borderPen = CreatePen(PS_SOLID, borderWidth, btn->borderColor);
            SelectObject(hdc, borderPen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, 10, 10);
            DeleteObject(borderPen);

            // Text
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, btn->textColor);
            HFONT oldFont = (HFONT)SelectObject(hdc, g_bubbleFont);
            int len = (int)wcslen(btn->label);
            RECT textRect = rc;
            DrawTextW(hdc, btn->label, len, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont);

            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdcCtl = (HDC)wParam;
            // For STATIC labels (title), use transparent with neon text
            SetBkMode(hdcCtl, TRANSPARENT);
            SetTextColor(hdcCtl, TEXT_NEON);
            return (LRESULT)g_bubbleBgBrush;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdcCtl = (HDC)wParam;
            // White background for input edit, pure black text
            SetBkColor(hdcCtl, RGB(255, 255, 255));
            SetTextColor(hdcCtl, TEXT_BLACK);
            static HBRUSH whiteEditBrush = CreateSolidBrush(RGB(255, 255, 255));
            return (LRESULT)whiteEditBrush;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_BUBBLE_SEND: {
                    if (g_chatInProgress.load()) return 0;

                    // Apply settings from the settings fields before sending
                    if (g_agent) {
                        wchar_t url[512] = {0}, key[512] = {0}, model[128] = {0};
                        GetWindowTextW(GetDlgItem(hwnd, IDC_BUBBLE_URL), url, 512);
                        GetWindowTextW(GetDlgItem(hwnd, IDC_BUBBLE_KEY), key, 512);
                        GetWindowTextW(GetDlgItem(hwnd, IDC_BUBBLE_MODEL), model, 128);
                        if (wcslen(url) > 0) g_agent->SetServerUrl(url);
                        if (wcslen(key) > 0) g_agent->SetApiKey(key);
                        if (wcslen(model) > 0) g_agent->SetModel(model);
                    }
                    wchar_t text[1024] = {0};
                    GetWindowTextW(GetDlgItem(hwnd, IDC_BUBBLE_EDIT), text, 1024);
                    if (g_agent && wcslen(text) > 0) {
                        g_lastUserMsg = text;
                        g_chatInProgress.store(true);

                        // Add user message to history immediately (empty response for now)
                        g_chatHistory.push_back({g_lastUserMsg, L"", false});

                        // Clear input box
                        SetWindowTextW(GetDlgItem(hwnd, IDC_BUBBLE_EDIT), L"");
                        EnableWindow(GetDlgItem(hwnd, IDC_BUBBLE_SEND), FALSE);
                        SetTimer(hwnd, IDT_LOADING, 400, nullptr);

                        // Set robot to thinking state
                        if (g_renderer) g_renderer->SetThinking(true);

                        // Update conversation display with user message + loading
                        RefreshConversation(hwnd);

                        // Launch async chat thread
                        std::thread([hwnd]() {
                            g_pendingResponse = g_agent->Chat(g_lastUserMsg);
                            PostMessageW(hwnd, WM_CHAT_RESPONSE, 0, 0);
                        }).detach();
                    }
                    return 0;
                }
                case IDC_BUBBLE_HISTORY: {
                    // Scroll conversation to top to review history
                    HWND hConvo = GetDlgItem(hwnd, IDC_BUBBLE_CONVO);
                    if (hConvo) {
                        SendMessageW(hConvo, EM_LINESCROLL, 0, -99999);
                    }
                    return 0;
                }
                case IDC_BUBBLE_SETTINGS: {
                    g_settingsVisible = !g_settingsVisible;
                    int ids[] = { IDC_BUBBLE_URL_LABEL, IDC_BUBBLE_URL,
                                  IDC_BUBBLE_KEY_LABEL, IDC_BUBBLE_KEY,
                                  IDC_BUBBLE_MODEL_LABEL, IDC_BUBBLE_MODEL };
                    for (int id : ids) {
                        HWND ctl = GetDlgItem(hwnd, id);
                        if (ctl) ShowWindow(ctl, g_settingsVisible ? SW_SHOW : SW_HIDE);
                    }
                    // Hide history list if open
                    HWND hList = GetDlgItem(hwnd, IDC_BUBBLE_HISTORY_LIST);
                    if (hList) ShowWindow(hList, SW_HIDE);

                    int newH = g_settingsVisible ? 460 : 280;
                    RECT rc; GetWindowRect(hwnd, &rc);
                    int newW = 340;
                    SetWindowPos(hwnd, HWND_TOPMOST, rc.left, rc.top - (newH - (rc.bottom - rc.top)),
                                 newW, newH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                case IDC_BUBBLE_EXPAND: {
                    g_expanded = !g_expanded;
                    if (g_expanded) {
                        int screenW = GetSystemMetrics(SM_CXSCREEN);
                        int screenH = GetSystemMetrics(SM_CYSCREEN);
                        int newW = screenW / 2;
                        int newH = screenH / 2;
                        SetWindowPos(hwnd, HWND_TOPMOST, 20, 20, newW, newH, SWP_SHOWWINDOW);
                        g_btnData[4].label = L"Shrink";
                    } else {
                        g_btnData[4].label = L"Expand";
                        int newH = g_settingsVisible ? 460 : 280;
                        RECT rc2; GetWindowRect(hwnd, &rc2);
                        SetWindowPos(hwnd, HWND_TOPMOST, rc2.left, rc2.top, 340, newH, SWP_SHOWWINDOW);
                    }
                    // Resize child controls to fill new window
                    RECT crc; GetClientRect(hwnd, &crc);
                    int cw = crc.right, ch = crc.bottom;
                    int convoH = ch - 110;
                    int inputY = convoH + 8;
                    int btnY = inputY + 36;
                    HWND hConvo2 = GetDlgItem(hwnd, IDC_BUBBLE_CONVO);
                    if (hConvo2) MoveWindow(hConvo2, 28, 38, cw - 56, convoH - 38, TRUE);
                    HWND hEdit2 = GetDlgItem(hwnd, IDC_BUBBLE_EDIT);
                    if (hEdit2) MoveWindow(hEdit2, 28, inputY, cw - 56, 32, TRUE);
                    int btnW2 = 50, btnGap2 = 4;
                    int totalBtnW2 = 6 * btnW2 + 5 * btnGap2;
                    int startX2 = (cw - totalBtnW2) / 2;
                    int ids2[] = { IDC_BUBBLE_SEND, IDC_BUBBLE_VOICE, IDC_BUBBLE_SETTINGS,
                                  IDC_BUBBLE_HISTORY, IDC_BUBBLE_EXPAND, IDC_BUBBLE_CANCEL };
                    for (int i = 0; i < 6; i++) {
                        HWND btn = GetDlgItem(hwnd, ids2[i]);
                        if (btn) MoveWindow(btn, startX2 + i * (btnW2 + btnGap2), btnY, btnW2, 24, TRUE);
                    }
                    InvalidateRect(hwnd, nullptr, TRUE);
                    RefreshConversation(hwnd);
                    return 0;
                }
                case IDC_BUBBLE_CANCEL:
                    DestroyWindow(hwnd);
                    return 0;
                case IDC_BUBBLE_VOICE:
                    MessageBoxW(hwnd, L"Voice input coming soon!", L"Aria", MB_OK | MB_ICONINFORMATION);
                    return 0;
            }
            break;
        }
        case WM_TIMER: {
            if (wParam == IDT_LOADING) {
                g_loadingDots = (g_loadingDots + 1) % 4;
                RefreshConversation(hwnd);
                return 0;
            }
            break;
        }
        case WM_CHAT_RESPONSE: {
            KillTimer(hwnd, IDT_LOADING);
            g_chatInProgress.store(false);
            EnableWindow(GetDlgItem(hwnd, IDC_BUBBLE_SEND), TRUE);

            std::wstring response = g_pendingResponse;

            // Update the last history entry with the AI response
            if (!g_chatHistory.empty()) {
                g_chatHistory.back().assistantMsg = response;
            }

            // Update conversation display
            RefreshConversation(hwnd);

            // Restore focus to input
            SetFocus(GetDlgItem(hwnd, IDC_BUBBLE_EDIT));

            // Stop thinking animation
            if (g_renderer) g_renderer->SetThinking(false);
            return 0;
        }
        case WM_CHAT_ERROR: {
            KillTimer(hwnd, IDT_LOADING);
            g_chatInProgress.store(false);
            EnableWindow(GetDlgItem(hwnd, IDC_BUBBLE_SEND), TRUE);
            if (!g_chatHistory.empty()) {
                g_chatHistory.back().assistantMsg = L"Error: Could not reach AI server.";
            }
            RefreshConversation(hwnd);
            SetFocus(GetDlgItem(hwnd, IDC_BUBBLE_EDIT));
            if (g_renderer) g_renderer->SetThinking(false);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (g_bubbleFont) { DeleteObject(g_bubbleFont); g_bubbleFont = nullptr; }
            if (g_bubbleTitleFont) { DeleteObject(g_bubbleTitleFont); g_bubbleTitleFont = nullptr; }
            if (g_bubbleSmallFont) { DeleteObject(g_bubbleSmallFont); g_bubbleSmallFont = nullptr; }
            if (g_bubbleBgBrush) { DeleteObject(g_bubbleBgBrush); g_bubbleBgBrush = nullptr; }
            if (g_bubbleEditBgBrush) { DeleteObject(g_bubbleEditBgBrush); g_bubbleEditBgBrush = nullptr; }
            g_bubbleHwnd = nullptr;
            g_settingsVisible = false;
            g_expanded = false;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void UpdateBubblePosition(HWND robotHwnd) {
    if (!g_bubbleHwnd || !robotHwnd) return;
    if (g_expanded) return; // Don't reposition when expanded
    RECT rc;
    GetWindowRect(robotHwnd, &rc);
    int bubbleW = 340, bubbleH = g_settingsVisible ? 460 : 280;
    int x = rc.left + (rc.right - rc.left) / 2 - bubbleW / 2;
    int y = rc.top - bubbleH + 15;
    if (y < 0) y = rc.bottom + 10;
    SetWindowPos(g_bubbleHwnd, HWND_TOPMOST, x, y, bubbleW, bubbleH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void ShowMangaBubble(HINSTANCE hInstance, HWND parent) {
    if (g_bubbleHwnd) {
        SetForegroundWindow(g_bubbleHwnd);
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = BubbleWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = L"AriaMangaBubble";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassW(&wc);
        registered = true;
    }

    // Position above the robot window
    RECT rc;
    GetWindowRect(parent, &rc);
    int bubbleW = 340, bubbleH = 280;
    int x = rc.left + (rc.right - rc.left) / 2 - bubbleW / 2;
    int y = rc.top - bubbleH + 15; // tail overlaps robot slightly
    if (y < 0) y = rc.bottom + 10;

    g_bubbleHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"AriaMangaBubble", L"",
        WS_POPUP,
        x, y, bubbleW, bubbleH,
        nullptr, nullptr, hInstance, nullptr);

    // Color-key transparency: magenta pixels become transparent
    SetLayeredWindowAttributes(g_bubbleHwnd, RGB(255, 0, 255), 255, LWA_COLORKEY);

    ShowWindow(g_bubbleHwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_bubbleHwnd);
    SetFocus(g_bubbleHwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_renderer = new RobotRenderer();
            if (!g_renderer->Initialize(hwnd)) {
                MessageBox(hwnd, L"Failed to initialize Direct2D", L"Error", MB_ICONERROR);
                return -1;
            }
            SetTimer(hwnd, IDT_ANIMATE, ANIMATE_INTERVAL_MS, nullptr);
            return 0;
        }

        case WM_TIMER: {
            if (wParam == IDT_ANIMATE && g_renderer) {
                g_renderer->Update();
                g_renderer->Render();

                // Keep manga bubble positioned above the robot
                UpdateBubblePosition(hwnd);

                // Check if head was clicked → show input dialog
                if (g_renderer->WantsInputDialog()) {
                    g_renderer->ClearInputDialogFlag();
                    ShowMangaBubble((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), hwnd);
                }
            }
            return 0;
        }

        case WM_PAINT: {
            if (g_renderer) g_renderer->Render();
            ValidateRect(hwnd, nullptr);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (g_renderer) {
                int mouseX = GET_X_LPARAM(lParam);
                int mouseY = GET_Y_LPARAM(lParam);
                g_renderer->OnMouseDown(mouseX, mouseY);
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_renderer) {
                int mouseX = GET_X_LPARAM(lParam);
                int mouseY = GET_Y_LPARAM(lParam);
                g_renderer->OnMouseMove(mouseX, mouseY);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_renderer) {
                int mouseX = GET_X_LPARAM(lParam);
                int mouseY = GET_Y_LPARAM(lParam);
                g_renderer->OnMouseUp(mouseX, mouseY);
                ReleaseCapture();
            }
            return 0;
        }

        case WM_RBUTTONDOWN: {
            if (g_tray) g_tray->ShowContextMenu(hwnd);
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_TRAY_SHOW: g_windowMgr->Show(); break;
                case ID_TRAY_HIDE: g_windowMgr->Hide(); break;
                case ID_TRAY_QUIT: DestroyWindow(hwnd); break;
            }
            return 0;
        }

        case WM_DESTROY: {
            KillTimer(hwnd, IDT_ANIMATE);
            if (g_renderer) { delete g_renderer; g_renderer = nullptr; }
            PostQuitMessage(0);
            return 0;
        }

        case WM_SIZE: {
            if (g_renderer) g_renderer->OnResize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize common controls
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    g_windowMgr = new WindowManager();
    if (!g_windowMgr->Create(hInstance, WndProc)) {
        MessageBox(nullptr, L"Failed to create window", L"Error", MB_ICONERROR);
        return 1;
    }

    g_tray = new TrayIcon();
    g_tray->Initialize(hInstance, g_windowMgr->GetHwnd());

    g_agent = new AgentClient();
    // AMD Radeon Developer API (OpenAI-compatible)
    g_agent->SetServerUrl(L"https://developer.amd.com.cn/radeon/api/v1");
    g_agent->SetApiKey(L"rc-c042ad0acc56669f7b46e70f924189b5ac51664ce329f5b2");
    g_agent->SetModel(L"Qwen3.6-35B-A3B");

    g_windowMgr->Show();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_tray) delete g_tray;
    if (g_agent) delete g_agent;
    if (g_windowMgr) delete g_windowMgr;

    return (int)msg.wParam;
}
