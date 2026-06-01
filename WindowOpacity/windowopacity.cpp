#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <string>

#pragma comment(lib, "Comctl32.lib")

constexpr int IDC_HOTKEY_UP = 101;
constexpr int IDC_HOTKEY_DOWN = 102;
constexpr int IDC_HOTKEY_RESET = 103;
constexpr int IDC_STEP = 104;
constexpr int IDC_APPLY = 105;
constexpr int IDC_STATUS = 106;

constexpr int HOTKEY_UP = 1;
constexpr int HOTKEY_DOWN = 2;
constexpr int HOTKEY_RESET = 3;
constexpr int MIN_OPACITY = 15;
constexpr int MAX_OPACITY = 100;

struct HotkeySpec {
    UINT modifiers = 0;
    UINT vk = 0;
};

constexpr HotkeySpec DEFAULT_UP{MOD_CONTROL | MOD_SHIFT, VK_OEM_6};
constexpr HotkeySpec DEFAULT_DOWN{MOD_CONTROL | MOD_SHIFT, VK_OEM_4};
constexpr HotkeySpec DEFAULT_RESET{MOD_CONTROL | MOD_SHIFT, VK_OEM_5};

HINSTANCE g_instance = nullptr;
HWND g_main = nullptr;
HWND g_hotkeyUp = nullptr;
HWND g_hotkeyDown = nullptr;
HWND g_hotkeyReset = nullptr;
HWND g_step = nullptr;
HWND g_status = nullptr;
HFONT g_font = nullptr;
HWND g_lastTarget = nullptr;

std::wstring getText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(len, L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    return text;
}

int getStep() {
    try {
        int value = std::stoi(getText(g_step));
        return std::clamp(value, 1, 50);
    } catch (...) {
        return 10;
    }
}

void setStatus(const std::wstring& text) {
    SetWindowTextW(g_status, text.c_str());
}

std::wstring windowTitle(HWND hwnd) {
    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, 256);
    if (title[0] == L'\0') return L"(无标题窗口)";
    return title;
}

bool isOwnWindow(HWND hwnd) {
    return hwnd == g_main || IsChild(g_main, hwnd);
}

HWND currentTargetWindow() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || isOwnWindow(hwnd)) hwnd = g_lastTarget;
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || isOwnWindow(hwnd)) return nullptr;
    return hwnd;
}

BYTE currentAlpha(HWND hwnd) {
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_LAYERED) == 0) return 255;

    BYTE alpha = 255;
    COLORREF colorKey = 0;
    DWORD flags = 0;
    if (GetLayeredWindowAttributes(hwnd, &colorKey, &alpha, &flags) && (flags & LWA_ALPHA)) {
        return alpha;
    }
    return 255;
}

void applyAlpha(HWND hwnd, BYTE alpha) {
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_LAYERED) == 0) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    }
    SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
}

int alphaToOpacity(BYTE alpha) {
    return static_cast<int>((alpha * 100 + 127) / 255);
}

BYTE opacityToAlpha(int opacity) {
    opacity = std::clamp(opacity, MIN_OPACITY, MAX_OPACITY);
    return static_cast<BYTE>((opacity * 255 + 50) / 100);
}

void adjustOpacity(int delta) {
    HWND target = currentTargetWindow();
    if (!target) {
        setStatus(L"未找到可调整的前台窗口");
        return;
    }

    int opacity = alphaToOpacity(currentAlpha(target));
    opacity = std::clamp(opacity + delta, MIN_OPACITY, MAX_OPACITY);
    applyAlpha(target, opacityToAlpha(opacity));
    g_lastTarget = target;

    setStatus(windowTitle(target) + L" / 不透明度 " + std::to_wstring(opacity) + L"%");
}

void resetOpacity() {
    HWND target = currentTargetWindow();
    if (!target) {
        setStatus(L"未找到可调整的前台窗口");
        return;
    }

    applyAlpha(target, 255);
    g_lastTarget = target;
    setStatus(windowTitle(target) + L" / 不透明度 100%");
}

WORD defaultHotkey(BYTE modifiers, BYTE vk) {
    return MAKEWORD(vk, modifiers);
}

bool readHotkey(HWND hwnd, UINT& modifiers, UINT& vk) {
    WORD value = static_cast<WORD>(SendMessageW(hwnd, HKM_GETHOTKEY, 0, 0));
    vk = LOBYTE(value);
    BYTE flags = HIBYTE(value);
    modifiers = 0;
    if (flags & HOTKEYF_CONTROL) modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_ALT) modifiers |= MOD_ALT;
    if (flags & HOTKEYF_SHIFT) modifiers |= MOD_SHIFT;
    return vk != 0;
}

bool registerOneHotkey(int id, UINT modifiers, UINT vk) {
    UnregisterHotKey(g_main, id);
    return RegisterHotKey(g_main, id, modifiers | MOD_NOREPEAT, vk) != FALSE;
}

bool registerFromControl(int id, HWND hotkeyControl) {
    UINT modifiers = 0;
    UINT vk = 0;
    if (!readHotkey(hotkeyControl, modifiers, vk)) return false;
    return registerOneHotkey(id, modifiers, vk);
}

void showRegisterStatus(bool okUp, bool okDown, bool okReset) {
    if (okUp && okDown && okReset) {
        setStatus(L"快捷键已启用");
        return;
    }

    std::wstring failed = L"注册失败: ";
    bool first = true;
    auto append = [&](const wchar_t* name) {
        if (!first) failed += L", ";
        failed += name;
        first = false;
    };
    if (!okUp) append(L"调高");
    if (!okDown) append(L"调低");
    if (!okReset) append(L"恢复");
    setStatus(failed + L"；可能被占用");
}

void registerDefaultHotkeys() {
    bool okUp = registerOneHotkey(HOTKEY_UP, DEFAULT_UP.modifiers, DEFAULT_UP.vk);
    bool okDown = registerOneHotkey(HOTKEY_DOWN, DEFAULT_DOWN.modifiers, DEFAULT_DOWN.vk);
    bool okReset = registerOneHotkey(HOTKEY_RESET, DEFAULT_RESET.modifiers, DEFAULT_RESET.vk);
    showRegisterStatus(okUp, okDown, okReset);
}

void registerHotkeysFromControls() {
    bool okUp = registerFromControl(HOTKEY_UP, g_hotkeyUp);
    bool okDown = registerFromControl(HOTKEY_DOWN, g_hotkeyDown);
    bool okReset = registerFromControl(HOTKEY_RESET, g_hotkeyReset);
    showRegisterStatus(okUp, okDown, okReset);
}

void applyFont(HWND hwnd) {
    if (g_font) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
}

HWND label(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, g_instance, nullptr);
    applyFont(hwnd);
    return hwnd;
}

void createControls(HWND hwnd) {
    g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    label(hwnd, L"调高不透明度", 18, 18, 92, 24);
    g_hotkeyUp = CreateWindowW(HOTKEY_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                               118, 12, 190, 30, hwnd, reinterpret_cast<HMENU>(IDC_HOTKEY_UP), g_instance, nullptr);
    applyFont(g_hotkeyUp);

    label(hwnd, L"调低不透明度", 18, 54, 92, 24);
    g_hotkeyDown = CreateWindowW(HOTKEY_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                                 118, 48, 190, 30, hwnd, reinterpret_cast<HMENU>(IDC_HOTKEY_DOWN), g_instance, nullptr);
    applyFont(g_hotkeyDown);

    label(hwnd, L"恢复", 18, 90, 92, 24);
    g_hotkeyReset = CreateWindowW(HOTKEY_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                                  118, 84, 190, 30, hwnd, reinterpret_cast<HMENU>(IDC_HOTKEY_RESET), g_instance, nullptr);
    applyFont(g_hotkeyReset);

    label(hwnd, L"步进", 18, 126, 92, 24);
    g_step = CreateWindowW(L"EDIT", L"10", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                           118, 120, 64, 30, hwnd, reinterpret_cast<HMENU>(IDC_STEP), g_instance, nullptr);
    applyFont(g_step);
    label(hwnd, L"%", 188, 126, 24, 24);

    HWND apply = CreateWindowW(L"BUTTON", L"应用快捷键", WS_CHILD | WS_VISIBLE,
                               218, 120, 90, 30, hwnd, reinterpret_cast<HMENU>(IDC_APPLY), g_instance, nullptr);
    applyFont(apply);

    g_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
                             18, 164, 290, 26, hwnd, reinterpret_cast<HMENU>(IDC_STATUS), g_instance, nullptr);
    applyFont(g_status);

    SendMessageW(g_hotkeyUp, HKM_SETHOTKEY, defaultHotkey(HOTKEYF_CONTROL | HOTKEYF_SHIFT, DEFAULT_UP.vk), 0);
    SendMessageW(g_hotkeyDown, HKM_SETHOTKEY, defaultHotkey(HOTKEYF_CONTROL | HOTKEYF_SHIFT, DEFAULT_DOWN.vk), 0);
    SendMessageW(g_hotkeyReset, HKM_SETHOTKEY, defaultHotkey(HOTKEYF_CONTROL | HOTKEYF_SHIFT, DEFAULT_RESET.vk), 0);
    registerDefaultHotkeys();
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_main = hwnd;
        createControls(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_APPLY) {
            registerHotkeysFromControls();
        }
        return 0;
    case WM_HOTKEY:
        if (wParam == HOTKEY_UP) {
            adjustOpacity(getStep());
        } else if (wParam == HOTKEY_DOWN) {
            adjustOpacity(-getStep());
        } else if (wParam == HOTKEY_RESET) {
            resetOpacity();
        }
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_UP);
        UnregisterHotKey(hwnd, HOTKEY_DOWN);
        UnregisterHotKey(hwnd, HOTKEY_RESET);
        if (g_font) DeleteObject(g_font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    g_instance = instance;
    INITCOMMONCONTROLSEX icc{sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WindowOpacityWindow";
    RegisterClassW(&wc);

    g_main = CreateWindowExW(0, wc.lpszClassName, L"WindowOpacity", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 340, 245,
                             nullptr, nullptr, instance, nullptr);
    ShowWindow(g_main, show);
    UpdateWindow(g_main);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
