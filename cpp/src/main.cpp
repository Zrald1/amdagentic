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

#pragma comment(lib, "comctl32.lib")

// Global handles
static WindowManager* g_windowMgr = nullptr;
static RobotRenderer* g_renderer = nullptr;
static AgentClient* g_agent = nullptr;
static TrayIcon* g_tray = nullptr;

#define IDT_ANIMATE 1001
#define ANIMATE_INTERVAL_MS 16

// Input dialog control IDs
#define IDC_INPUT_EDIT   2001
#define IDC_VOICE_BTN    2002
#define IDC_SEND_BTN     2003
#define IDC_CANCEL_BTN   2004

// Custom dialog procedure for the task input
static INT_PTR CALLBACK InputDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            // Set title
            SetWindowTextW(hDlg, L"Ask Aria");
            // Focus the edit field
            SetFocus(GetDlgItem(hDlg, IDC_INPUT_EDIT));
            return TRUE;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_SEND_BTN: {
                    wchar_t text[1024] = {0};
                    GetDlgItemTextW(hDlg, IDC_INPUT_EDIT, text, 1024);
                    // TODO: send to agent client
                    if (g_agent) {
                        // For now just show a message
                        std::wstring response = g_agent->Chat(text);
                        SetDlgItemTextW(hDlg, IDC_INPUT_EDIT, response.c_str());
                    }
                    EndDialog(hDlg, 1);
                    return TRUE;
                }
                case IDC_CANCEL_BTN:
                    EndDialog(hDlg, 0);
                    return TRUE;
                case IDC_VOICE_BTN:
                    // TODO: implement voice input
                    MessageBoxW(hDlg, L"Voice input coming soon!", L"Aria", MB_OK | MB_ICONINFORMATION);
                    return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            EndDialog(hDlg, 0);
            return TRUE;
    }
    return FALSE;
}

// Input dialog — created with CreateWindowEx (no .rc file needed)
static HWND g_dlgHwnd = nullptr;

static LRESULT CALLBACK InputDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Dark background
            // Edit control (text input)
            HWND hEdit = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN,
                10, 10, 360, 80, hwnd, (HMENU)IDC_INPUT_EDIT, nullptr, nullptr);
            // Send button
            CreateWindowExW(0, L"BUTTON", L"Send",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                10, 100, 80, 28, hwnd, (HMENU)IDC_SEND_BTN, nullptr, nullptr);
            // Voice button
            CreateWindowExW(0, L"BUTTON", L"Voice",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                100, 100, 80, 28, hwnd, (HMENU)IDC_VOICE_BTN, nullptr, nullptr);
            // Cancel button
            CreateWindowExW(0, L"BUTTON", L"Cancel",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                290, 100, 80, 28, hwnd, (HMENU)IDC_CANCEL_BTN, nullptr, nullptr);
            SetFocus(hEdit);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_SEND_BTN: {
                    wchar_t text[1024] = {0};
                    GetWindowTextW(GetDlgItem(hwnd, IDC_INPUT_EDIT), text, 1024);
                    if (g_agent && wcslen(text) > 0) {
                        std::wstring response = g_agent->Chat(text);
                        SetWindowTextW(GetDlgItem(hwnd, IDC_INPUT_EDIT), response.c_str());
                    }
                    DestroyWindow(hwnd);
                    return 0;
                }
                case IDC_CANCEL_BTN:
                    DestroyWindow(hwnd);
                    return 0;
                case IDC_VOICE_BTN:
                    MessageBoxW(hwnd, L"Voice input coming soon!", L"Aria", MB_OK | MB_ICONINFORMATION);
                    return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_dlgHwnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowInputDialog(HINSTANCE hInstance, HWND parent) {
    if (g_dlgHwnd) {
        SetForegroundWindow(g_dlgHwnd);
        return;
    }

    // Register the dialog window class once
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = InputDlgProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = L"AriaInputDialog";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    // Center on screen near the robot
    RECT rc;
    GetWindowRect(parent, &rc);
    int x = rc.left;
    int y = rc.top - 180;
    if (y < 0) y = rc.bottom + 10;

    g_dlgHwnd = CreateWindowExW(0, L"AriaInputDialog", L"Ask Aria",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, 390, 170, nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_dlgHwnd, SW_SHOW);
    UpdateWindow(g_dlgHwnd);
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

                // Check if head was clicked → show input dialog
                if (g_renderer->WantsInputDialog()) {
                    g_renderer->ClearInputDialogFlag();
                    ShowInputDialog((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), hwnd);
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
    g_agent->SetServerUrl(L"http://localhost:8080");

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
