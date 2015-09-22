#include <cstdio>
#include <cassert>
#include <ctime>
#include <stack>
#include <windows.h>

static HHOOK g_hHook;
static std::stack<HWND> g_hwnds;
static HWND g_mainwnd;
static HWND g_nextwindow;
static UINT WM_SHELLHOOKMESSAGE;

class spawn_cmd_window {
public:
    spawn_cmd_window() {
        ::AllocConsole();
        ::freopen("CONIN$", "r", stdin);
        ::freopen("CONOUT$", "w", stdout);
        ::freopen("CONOUT$", "w", stderr);
    }

    ~spawn_cmd_window() {
        ::FreeConsole();
    }
};

#ifdef _DEBUG
#define debug_out(x) printf##x
#else
#define debug_out(x)
#endif

bool pre_message_handle(MSG* pmsg) {
    if(pmsg->hwnd == g_mainwnd && pmsg->message == WM_KEYDOWN && pmsg->wParam == VK_ESCAPE) {
        SendMessage(g_mainwnd, WM_CLOSE, 0, 0);
        return true;
    }

    return false;
}

// determine if window minimizable
// returns: 0 - notable, 1 - able, -1 - (no WS_MINIMIZEBOX, user determine)
int is_window_minimizable(HWND hwnd) {
    if (!IsWindow(hwnd)) return 0;

    DWORD dw_style = GetWindowLongPtr(hwnd, GWL_STYLE);
    DWORD dw_ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

    if (!dw_style || !dw_ex_style) {
        // some menus have these styles set
        return 0;
    }

    DWORD includes = WS_CHILDWINDOW; // satisfied when has one included
    DWORD excludes = WS_VISIBLE | WS_MINIMIZEBOX; // satisfied when has one excluded
    if ((dw_style & includes) != 0 || (dw_style & excludes) != excludes) {
        return 0;
    }

    includes = WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP;
    excludes = 0;
    if ((dw_ex_style & includes) != 0 || (dw_ex_style & excludes) != excludes) {
        return 0;
    }

    return 1;
}

void set_title_info(HWND hwnd) {
    char buf[2048];
    int n = 0;

    time_t now = time(NULL);
    tm* local_now = localtime(&now);
    n += sprintf(buf, "%02d:%02d:%02d", local_now->tm_hour, local_now->tm_min, local_now->tm_sec);
    n += sprintf(buf + n, " @ ");

    n += GetClassName(hwnd, buf + n, 128);

    buf[n] = '\0';

    SetWindowText(g_mainwnd, buf);
}

void capital_handler(bool shift, HWND hForeground = NULL, bool minimize = true) {
    if (!shift) {
        debug_out(("captital handler: shift = false\n"));
        if(!hForeground) hForeground = GetForegroundWindow();
        if(!IsWindow(hForeground)) return;

        if(g_hwnds.size() && g_hwnds.top() == hForeground) {
            debug_out(("the same window, exiting\n"));
            return;
        }

        int iwm = is_window_minimizable(hForeground);
        /*const char* text = "此窗口默认不允许最小化，是否依然要最小化？";
        DWORD_PTR result;
        if((iwm == -1 && ::SendMessageTimeout(g_mainwnd, UM_MSGBOX, WPARAM(text), MB_YESNO|MB_ICONQUESTION,
                SMTO_NORMAL, 3000, &result) && result == IDYES)
            || iwm == 1
        )*/
        if(iwm == -1 || iwm == 1)
        {
            g_hwnds.push(hForeground);
            if(minimize) {
                SendMessage(hForeground, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                debug_out(("minimizing window\n"));
            }

            //HWND next_wnd = get_next_window();
            //SetForegroundWindow(next_wnd);

            //char buf[128];
            //GetWindowText(next_wnd, buf, 128);
            //debug_out(("(min)setforeground window: %s\n", buf));
        } else {
            debug_out(("not minimizable\n"));
        }

        // set title info
        set_title_info(hForeground);
    }
    else {
        debug_out(("captital handler: shift = true\n"));
        while (g_hwnds.size() && !IsWindow(g_hwnds.top()))
            g_hwnds.pop();

        if (g_hwnds.size()) {
            if(IsIconic(g_hwnds.top())) {
                ShowWindow(g_hwnds.top(), SW_RESTORE);
                debug_out(("restoring window\n"));
            }
            //SetForegroundWindow(g_hwnds.top());
            BringWindowToTop(g_hwnds.top());
            debug_out(("(restore)setforeground window\n"));
            g_hwnds.pop();
        }
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        if (wParam == WM_KEYUP || wParam == WM_KEYDOWN) {
            KBDLLHOOKSTRUCT* pKbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            if (pKbd->vkCode == VK_CAPITAL) {
                if (wParam == WM_KEYUP) {
                    bool shift = ((unsigned short)GetAsyncKeyState(VK_LSHIFT) & 0x8000)
                        || ((unsigned short)GetAsyncKeyState(VK_RSHIFT) & 0x8000);
                    debug_out(("KeyboardHook: shift: %d\n", shift));
                    capital_handler(shift);
                }
                return 1;
            }
        }
    }

    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

LRESULT __stdcall Capswitch_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    /*case UM_MSGBOX:
        {
            LPCTSTR text = reinterpret_cast<char*>(wParam);
            UINT type = static_cast<UINT>(lParam);
            return MessageBox(hWnd, text, "", type);
        }*/
    case WM_CREATE:
        {
            g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
            WM_SHELLHOOKMESSAGE = RegisterWindowMessage("SHELLHOOK");
            if(WM_SHELLHOOKMESSAGE) {
                RegisterShellHookWindow(hWnd);
            }
            return 0;
        }
    case WM_DESTROY:
        {
            UnhookWindowsHookEx(g_hHook);
            DeregisterShellHookWindow(hWnd);
            PostQuitMessage(0);
            return 0;
        }
    }

    if(WM_SHELLHOOKMESSAGE && uMsg == WM_SHELLHOOKMESSAGE) {
        if(wParam == HSHELL_FLASH && GetForegroundWindow() != HWND(lParam)) {
            debug_out(("ShellHook: %08X\n", HWND(lParam)));
            capital_handler(false, HWND(lParam), false);
            return 0;
        }
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
#ifdef _DEBUG
    spawn_cmd_window scw;
#endif
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hInstance = hInstance;
    wc.lpfnWndProc = Capswitch_WndProc;
    wc.lpszClassName = "CAPSWITCH";
    wc.style = CS_VREDRAW | CS_HREDRAW;
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, "创建窗口类失败", NULL, MB_ICONERROR);
        return -1;
    }

    g_mainwnd = CreateWindowEx(WS_EX_CLIENTEDGE, "CAPSWITCH", "capswitch", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 250, NULL, NULL, hInstance, NULL);
    if (!g_mainwnd) {
        MessageBox(NULL, "创建窗口失败", NULL, MB_ICONERROR);
        return -1;
    }

    ShowWindow(g_mainwnd, nShowCmd);
    UpdateWindow(g_mainwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if(!pre_message_handle(&msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}
