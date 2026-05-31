#include <windows.h>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <mutex>
#include <string>
#include <vector>
#include <thread>

namespace {
enum class OperationMode {
    Single = 0,
    Combo = 1,
};

enum class ClickMode {
    MouseLeft = 0,
    MouseRight = 1,
    MouseMiddle = 2,
    Keyboard = 3,
};

struct ActionSpec {
    ClickMode kind = ClickMode::MouseLeft;
    WORD vk = VK_SPACE;
    UINT scanCode = 0;
    bool extended = false;
    bool keyReady = false;
};

constexpr int kEditCpsId = 1001;
constexpr int kStartButtonId = 1002;
constexpr int kStopButtonId = 1003;
constexpr int kStatusTextId = 1004;
constexpr int kModeSingleId = 1005;
constexpr int kModeComboId = 1006;
constexpr int kRadioLeftId = 1007;
constexpr int kRadioRightId = 1008;
constexpr int kRadioMiddleId = 1009;
constexpr int kRadioKeyboardId = 1010;
constexpr int kEditKeyId = 1011;
constexpr int kComboListId = 1012;
constexpr int kComboAddId = 1013;
constexpr int kComboRemoveId = 1014;
constexpr int kHotkeyToggleId = 1;
constexpr int kHotkeyQuitId = 2;
constexpr int kAppIconId = 101;

HWND g_mainWindow = nullptr;
HWND g_modeSingleRadio = nullptr;
HWND g_modeComboRadio = nullptr;
HWND g_editCps = nullptr;
HWND g_editKey = nullptr;
HWND g_radioLeft = nullptr;
HWND g_radioRight = nullptr;
HWND g_radioMiddle = nullptr;
HWND g_radioKeyboard = nullptr;
HWND g_comboListBox = nullptr;
HWND g_comboAddButton = nullptr;
HWND g_comboRemoveButton = nullptr;
HWND g_statusText = nullptr;
HFONT g_uiFont = nullptr;
WNDPROC g_oldCpsEditProc = nullptr;
WNDPROC g_oldKeyEditProc = nullptr;
bool g_capturingKey = false;
bool g_normalizingCps = false;
std::atomic<bool> g_running{false};
std::atomic<bool> g_shouldExit{false};
std::atomic<int> g_operationMode{static_cast<int>(OperationMode::Single)};
std::atomic<int> g_clickMode{static_cast<int>(ClickMode::MouseLeft)};
std::atomic<int> g_clickKeyVk{VK_SPACE};
std::atomic<unsigned int> g_clickKeyScanCode{0};
std::atomic<bool> g_clickKeyExtended{false};
std::atomic<bool> g_keyCaptured{false};
ActionSpec g_singleAction;
std::vector<ActionSpec> g_comboSteps;
std::vector<ActionSpec> g_runtimeActions;
int g_selectedComboIndex = -1;
std::thread g_clickThread;

bool IsKeyboardMode();
void BuildStatusMessage(const wchar_t* prefix);
void UpdateModeUi();
void ApplyUiFont(HWND hwnd);
void ApplyFontToChildren(HWND parent);
void LoadActionIntoEditor(const ActionSpec& action);
ActionSpec CaptureActionFromEditor();
void SaveEditorActionToModel();
void EnsureComboStepsExist();
void RefreshComboListBox();
void ApplyOperationMode(OperationMode mode);
bool BuildRuntimeActions();

std::wstring Trim(const std::wstring& value) {
    size_t begin = 0;
    while (begin < value.size() && iswspace(value[begin])) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && iswspace(value[end - 1])) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::wstring ToUpper(std::wstring value) {
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(towupper(ch));
    }
    return value;
}

void ApplyUiFont(HWND hwnd) {
    if (g_uiFont != nullptr && hwnd != nullptr) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    }
}

BOOL CALLBACK ApplyFontToChildProc(HWND child, LPARAM) {
    ApplyUiFont(child);
    return TRUE;
}

void ApplyFontToChildren(HWND parent) {
    if (parent != nullptr) {
        EnumChildWindows(parent, ApplyFontToChildProc, 0);
    }
}

std::wstring GetCapturedKeyDisplayName(WORD vk, UINT scanCode, bool extended) {
    if (vk >= L'A' && vk <= L'Z') {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= L'0' && vk <= L'9') {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return std::wstring(L"Num ") + std::to_wstring(vk - VK_NUMPAD0);
    }

    switch (vk) {
    case VK_DECIMAL:
        return L"Num .";
    case VK_DIVIDE:
        return L"Num /";
    case VK_MULTIPLY:
        return L"Num *";
    case VK_SUBTRACT:
        return L"Num -";
    case VK_ADD:
        return L"Num +";
    case VK_SEPARATOR:
        return L"Num ,";
    case VK_RETURN:
        return extended ? L"Num Enter" : L"Enter";
    case VK_SPACE:
        return L"Space";
    case VK_TAB:
        return L"Tab";
    case VK_ESCAPE:
        return L"Esc";
    case VK_BACK:
        return L"Backspace";
    case VK_DELETE:
        return L"Delete";
    case VK_INSERT:
        return L"Insert";
    case VK_HOME:
        return L"Home";
    case VK_END:
        return L"End";
    case VK_PRIOR:
        return L"Page Up";
    case VK_NEXT:
        return L"Page Down";
    case VK_LEFT:
        return L"Left";
    case VK_RIGHT:
        return L"Right";
    case VK_UP:
        return L"Up";
    case VK_DOWN:
        return L"Down";
    case VK_SHIFT:
        return scanCode == 0x36 ? L"右 Shift" : L"左 Shift";
    case VK_CONTROL:
        return extended ? L"右 Ctrl" : L"左 Ctrl";
    case VK_MENU:
        return extended ? L"右 Alt" : L"左 Alt";
    }

    if (vk >= VK_F1 && vk <= VK_F24) {
        return std::wstring(L"F") + std::to_wstring(vk - VK_F1 + 1);
    }

    switch (vk) {
    case VK_OEM_1:
        return L";";
    case VK_OEM_PLUS:
        return L"=";
    case VK_OEM_COMMA:
        return L",";
    case VK_OEM_MINUS:
        return L"-";
    case VK_OEM_PERIOD:
        return L".";
    case VK_OEM_2:
        return L"/";
    case VK_OEM_3:
        return L"`";
    case VK_OEM_4:
        return L"[";
    case VK_OEM_5:
        return L"\\";
    case VK_OEM_6:
        return L"]";
    case VK_OEM_7:
        return L"'";
    case VK_OEM_102:
        return L"< >";
    }

    wchar_t translated[8] = {};
    BYTE keyState[256] = {};
    const HKL layout = GetKeyboardLayout(0);
    const int result = ToUnicodeEx(vk, scanCode, keyState, translated, static_cast<int>(std::size(translated)), 0, layout);
    if (result == 1) {
        std::wstring text(1, translated[0]);
        if (text[0] >= L'a' && text[0] <= L'z') {
            text[0] = static_cast<wchar_t>(towupper(text[0]));
        }
        return text;
    }

    wchar_t name[64] = {};
    LONG lParam = static_cast<LONG>((scanCode & 0xFF) << 16);
    if (extended) {
        lParam |= 1L << 24;
    }
    if (GetKeyNameTextW(lParam, name, static_cast<int>(std::size(name))) > 0) {
        return name;
    }

    return std::wstring(L"VK_") + std::to_wstring(vk);
}

std::wstring GetActionKindDisplayName(ClickMode kind) {
    switch (kind) {
    case ClickMode::MouseLeft:
        return L"鼠标左键";
    case ClickMode::MouseRight:
        return L"鼠标右键";
    case ClickMode::MouseMiddle:
        return L"鼠标中键";
    case ClickMode::Keyboard:
        return L"键盘按键";
    default:
        return L"未知";
    }
}

std::wstring FormatActionLabel(const ActionSpec& action) {
    switch (action.kind) {
    case ClickMode::MouseLeft:
    case ClickMode::MouseRight:
    case ClickMode::MouseMiddle:
        return GetActionKindDisplayName(action.kind);
    case ClickMode::Keyboard:
        if (!action.keyReady) {
            return L"键盘按键 - 未捕获";
        }
        return std::wstring(L"键盘 ") + GetCapturedKeyDisplayName(action.vk, action.scanCode, action.extended);
    }

    return L"未知";
}

ActionSpec CaptureActionFromEditor() {
    ActionSpec action;
    action.kind = static_cast<ClickMode>(g_clickMode.load(std::memory_order_relaxed));
    action.vk = static_cast<WORD>(g_clickKeyVk.load(std::memory_order_relaxed));
    action.scanCode = g_clickKeyScanCode.load(std::memory_order_relaxed);
    action.extended = g_clickKeyExtended.load(std::memory_order_relaxed);
    action.keyReady = g_keyCaptured.load(std::memory_order_relaxed);
    return action;
}

void LoadActionIntoEditor(const ActionSpec& action) {
    g_capturingKey = false;
    g_clickMode.store(static_cast<int>(action.kind), std::memory_order_relaxed);
    g_clickKeyVk.store(action.vk, std::memory_order_relaxed);
    g_clickKeyScanCode.store(action.scanCode, std::memory_order_relaxed);
    g_clickKeyExtended.store(action.extended, std::memory_order_relaxed);
    g_keyCaptured.store(action.keyReady, std::memory_order_relaxed);

    CheckRadioButton(g_mainWindow, kRadioLeftId, kRadioKeyboardId,
                     action.kind == ClickMode::MouseRight ? kRadioRightId
                     : action.kind == ClickMode::MouseMiddle ? kRadioMiddleId
                     : action.kind == ClickMode::Keyboard ? kRadioKeyboardId
                     : kRadioLeftId);

    if (action.kind == ClickMode::Keyboard && action.keyReady) {
        SetWindowTextW(g_editKey, GetCapturedKeyDisplayName(action.vk, action.scanCode, action.extended).c_str());
    } else if (action.kind == ClickMode::Keyboard) {
        SetWindowTextW(g_editKey, L"点击后按键捕获");
    }

    EnableWindow(g_editKey, action.kind == ClickMode::Keyboard);
}

void SaveEditorActionToModel() {
    const ActionSpec action = CaptureActionFromEditor();
    if (static_cast<OperationMode>(g_operationMode.load(std::memory_order_relaxed)) == OperationMode::Single) {
        g_singleAction = action;
        return;
    }

    if (g_selectedComboIndex >= 0 && g_selectedComboIndex < static_cast<int>(g_comboSteps.size())) {
        g_comboSteps[static_cast<size_t>(g_selectedComboIndex)] = action;
    }
}

void EnsureComboStepsExist() {
    if (g_comboSteps.empty()) {
        g_comboSteps.push_back(g_singleAction);
        g_selectedComboIndex = 0;
    }
}

void RefreshComboListBox() {
    if (!g_comboListBox) {
        return;
    }

    SendMessageW(g_comboListBox, LB_RESETCONTENT, 0, 0);
    for (size_t index = 0; index < g_comboSteps.size(); ++index) {
        const std::wstring label = std::to_wstring(index + 1) + L". " + FormatActionLabel(g_comboSteps[index]);
        SendMessageW(g_comboListBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }

    if (g_selectedComboIndex >= 0 && g_selectedComboIndex < static_cast<int>(g_comboSteps.size())) {
        SendMessageW(g_comboListBox, LB_SETCURSEL, g_selectedComboIndex, 0);
    }
}

void ApplyOperationMode(OperationMode mode) {
    SaveEditorActionToModel();
    g_operationMode.store(static_cast<int>(mode), std::memory_order_relaxed);
    CheckRadioButton(g_mainWindow, kModeSingleId, kModeComboId,
                     mode == OperationMode::Combo ? kModeComboId : kModeSingleId);

    if (mode == OperationMode::Combo) {
        EnsureComboStepsExist();
        if (g_selectedComboIndex < 0 || g_selectedComboIndex >= static_cast<int>(g_comboSteps.size())) {
            g_selectedComboIndex = 0;
        }
        LoadActionIntoEditor(g_comboSteps[static_cast<size_t>(g_selectedComboIndex)]);
    } else {
        LoadActionIntoEditor(g_singleAction);
    }

    const BOOL comboVisible = mode == OperationMode::Combo;
    ShowWindow(g_comboListBox, comboVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(g_comboAddButton, comboVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(g_comboRemoveButton, comboVisible ? SW_SHOW : SW_HIDE);

    RefreshComboListBox();
    UpdateModeUi();
    BuildStatusMessage(L"状态: 已停止");
}

void SelectComboStep(int index) {
    if (index < 0 || index >= static_cast<int>(g_comboSteps.size())) {
        return;
    }

    SaveEditorActionToModel();
    g_selectedComboIndex = index;
    LoadActionIntoEditor(g_comboSteps[static_cast<size_t>(index)]);
    RefreshComboListBox();
    BuildStatusMessage(L"状态: 已停止");
}

void AddComboStep() {
    SaveEditorActionToModel();
    g_comboSteps.push_back(g_singleAction);
    g_selectedComboIndex = static_cast<int>(g_comboSteps.size()) - 1;
    LoadActionIntoEditor(g_comboSteps.back());
    RefreshComboListBox();
}

void RemoveComboStep() {
    if (g_comboSteps.empty() || g_selectedComboIndex < 0 || g_selectedComboIndex >= static_cast<int>(g_comboSteps.size())) {
        return;
    }

    g_comboSteps.erase(g_comboSteps.begin() + g_selectedComboIndex);
    if (g_comboSteps.empty()) {
        g_comboSteps.push_back(g_singleAction);
    }

    if (g_selectedComboIndex >= static_cast<int>(g_comboSteps.size())) {
        g_selectedComboIndex = static_cast<int>(g_comboSteps.size()) - 1;
    }

    LoadActionIntoEditor(g_comboSteps[static_cast<size_t>(g_selectedComboIndex)]);
    RefreshComboListBox();
}

void NormalizeCpsText(HWND hwnd) {
    if (g_normalizingCps) {
        return;
    }

    g_normalizingCps = true;
    wchar_t buffer[64] = {};
    GetWindowTextW(hwnd, buffer, static_cast<int>(std::size(buffer)));

    std::wstring filtered;
    filtered.reserve(std::size(buffer));
    for (const wchar_t* ptr = buffer; *ptr != L'\0'; ++ptr) {
        const wchar_t ch = *ptr;
        if (ch >= L'0' && ch <= L'9') {
            filtered.push_back(ch);
        }
    }

    if (filtered.empty()) {
        SetWindowTextW(hwnd, L"");
    } else if (filtered != buffer) {
        SetWindowTextW(hwnd, filtered.c_str());
    }

    g_normalizingCps = false;
}

void BeginKeyCapture(HWND hwnd) {
    if (!IsKeyboardMode()) {
        return;
    }
    g_capturingKey = true;
    SetWindowTextW(hwnd, L"请按下按键...");
    SetFocus(hwnd);
}

void CommitCapturedKey(HWND hwnd, WORD vk, LPARAM lParam) {
    const UINT scanCode = static_cast<UINT>((lParam >> 16) & 0xFF);
    const bool extended = (lParam & (1L << 24)) != 0;

    g_clickKeyVk.store(vk, std::memory_order_relaxed);
    g_clickKeyScanCode.store(scanCode, std::memory_order_relaxed);
    g_clickKeyExtended.store(extended, std::memory_order_relaxed);
    g_keyCaptured.store(true, std::memory_order_relaxed);
    g_capturingKey = false;

    const std::wstring display = GetCapturedKeyDisplayName(vk, scanCode, extended);
    SetWindowTextW(hwnd, display.c_str());
    SaveEditorActionToModel();
    RefreshComboListBox();
    if (!g_running.load(std::memory_order_relaxed)) {
        BuildStatusMessage(L"状态: 已停止");
    }
}

LRESULT CALLBACK CpsEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CHAR:
        if ((wParam >= L'0' && wParam <= L'9') || wParam == VK_BACK || wParam == VK_DELETE) {
            break;
        }
        if (wParam == L'\r') {
            return 0;
        }
        return 0;
    case WM_PASTE:
        if (OpenClipboard(hwnd)) {
            const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
            if (handle) {
                const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(handle));
                if (data) {
                    std::wstring filtered;
                    filtered.reserve(wcslen(data));
                    for (const wchar_t* ptr = data; *ptr != L'\0'; ++ptr) {
                        if (*ptr >= L'0' && *ptr <= L'9') {
                            filtered.push_back(*ptr);
                        }
                    }
                    GlobalUnlock(handle);
                    if (!filtered.empty()) {
                        SetWindowTextW(hwnd, filtered.c_str());
                    }
                }
            }
            CloseClipboard();
        }
        return 0;
    case WM_KEYUP:
    case WM_KEYDOWN:
        break;
    }

    const LRESULT result = CallWindowProcW(g_oldCpsEditProc, hwnd, msg, wParam, lParam);
    if (msg == WM_CHAR || msg == WM_PASTE) {
        NormalizeCpsText(hwnd);
    }
    return result;
}

LRESULT CALLBACK KeyEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_SETFOCUS:
        BeginKeyCapture(hwnd);
        return 0;
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (g_capturingKey) {
            CommitCapturedKey(hwnd, static_cast<WORD>(wParam), lParam);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        g_capturingKey = false;
        if (g_clickMode.load(std::memory_order_relaxed) == static_cast<int>(ClickMode::Keyboard)) {
            SetWindowTextW(hwnd, GetCapturedKeyDisplayName(static_cast<WORD>(g_clickKeyVk.load(std::memory_order_relaxed)),
                                                           g_clickKeyScanCode.load(std::memory_order_relaxed),
                                                           g_clickKeyExtended.load(std::memory_order_relaxed)).c_str());
        }
        break;
    }

    return CallWindowProcW(g_oldKeyEditProc, hwnd, msg, wParam, lParam);
}

bool IsKeyboardMode() {
    return g_clickMode.load(std::memory_order_relaxed) == static_cast<int>(ClickMode::Keyboard);
}

void UpdateModeUi() {
    const bool keyboardMode = IsKeyboardMode();
    EnableWindow(g_editKey, keyboardMode);
    if (keyboardMode && !g_keyCaptured.load(std::memory_order_relaxed)) {
        SetWindowTextW(g_editKey, L"点击后按键捕获");
    }
}

int ReadCpsFromUi() {
    if (!g_editCps) {
        return 50;
    }

    wchar_t buffer[64] = {};
    GetWindowTextW(g_editCps, buffer, static_cast<int>(std::size(buffer)));

    int cps = 0;
    try {
        cps = std::stoi(buffer);
    } catch (...) {
        cps = 50;
    }

    if (cps < 1) {
        cps = 1;
    }
    if (cps > 10000) {
        cps = 10000;
    }
    return cps;
}

void SetStatusText(const std::wstring& text) {
    if (g_statusText) {
        SetWindowTextW(g_statusText, text.c_str());
    }
}

void SendMouseClick(DWORD downFlag, DWORD upFlag) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = downFlag;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = upFlag;
    SendInput(2, inputs, sizeof(INPUT));
}

void SendKeyboardClick(WORD vk, UINT scanCode, bool extended) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 0;
    inputs[0].ki.wScan = static_cast<WORD>(scanCode);
    inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);

    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;

    if (scanCode == 0 && (vk >= VK_F1 && vk <= VK_F24)) {
        const WORD functionScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        inputs[0].ki.wScan = functionScan;
        inputs[1].ki.wScan = functionScan;
    }

    SendInput(2, inputs, sizeof(INPUT));
}

void SendAction(const ActionSpec& action) {
    switch (action.kind) {
    case ClickMode::MouseLeft:
        SendMouseClick(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
        break;
    case ClickMode::MouseRight:
        SendMouseClick(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
        break;
    case ClickMode::MouseMiddle:
        SendMouseClick(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP);
        break;
    case ClickMode::Keyboard:
        SendKeyboardClick(action.vk, action.scanCode, action.extended);
        break;
    }
}

void BuildStatusMessage(const wchar_t* prefix) {
    const OperationMode operationMode = static_cast<OperationMode>(g_operationMode.load(std::memory_order_relaxed));
    std::wstring status = prefix;
    status += L" - 模式: ";
    status += operationMode == OperationMode::Combo ? L"组合按键" : L"单键连点";

    if (operationMode == OperationMode::Combo) {
        status += L" (" + std::to_wstring(g_comboSteps.size()) + L"项)";
    }

    const ActionSpec action = CaptureActionFromEditor();
    if (action.kind == ClickMode::Keyboard) {
        status += L" (";
        status += action.keyReady
            ? GetCapturedKeyDisplayName(action.vk, action.scanCode, action.extended)
            : L"未捕获";
        status += L")";
    }

    status += L" - F6 切换, F8 退出";
    SetStatusText(status);
}

bool BuildRuntimeActions() {
    SaveEditorActionToModel();
    g_runtimeActions.clear();

    if (static_cast<OperationMode>(g_operationMode.load(std::memory_order_relaxed)) == OperationMode::Single) {
        if (g_singleAction.kind == ClickMode::Keyboard && !g_singleAction.keyReady) {
            MessageBoxW(g_mainWindow, L"单键模式下的键盘按键还未捕获，请先点击输入框完成捕获。", L"连点器", MB_ICONWARNING);
            return false;
        }
        g_runtimeActions.push_back(g_singleAction);
        return true;
    }

    EnsureComboStepsExist();
    for (const ActionSpec& action : g_comboSteps) {
        if (action.kind == ClickMode::Keyboard && !action.keyReady) {
            MessageBoxW(g_mainWindow, L"组合序列里有未捕获的键盘按键，请先点击输入框完成捕获。", L"连点器", MB_ICONWARNING);
            return false;
        }
        g_runtimeActions.push_back(action);
    }

    return !g_runtimeActions.empty();
}

void ClickLoop() {
    using clock = std::chrono::steady_clock;

    while (!g_shouldExit.load(std::memory_order_relaxed)) {
        if (!g_running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        const int cps = ReadCpsFromUi();
        const auto interval = std::chrono::duration<double>(1.0 / static_cast<double>(cps));
        auto nextTick = clock::now();

        while (g_running.load(std::memory_order_relaxed) && !g_shouldExit.load(std::memory_order_relaxed)) {
            for (const ActionSpec& action : g_runtimeActions) {
                SendAction(action);
            }
            nextTick += std::chrono::duration_cast<clock::duration>(interval);

            for (;;) {
                if (!g_running.load(std::memory_order_relaxed) || g_shouldExit.load(std::memory_order_relaxed)) {
                    break;
                }

                const auto now = clock::now();
                if (now >= nextTick) {
                    break;
                }

                const auto remaining = nextTick - now;
                if (remaining > std::chrono::milliseconds(2)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } else {
                    std::this_thread::yield();
                }
            }
        }
    }
}

void StartClicker() {
    if (g_running.exchange(true, std::memory_order_relaxed)) {
        return;
    }

    if (!BuildRuntimeActions()) {
        g_running.store(false, std::memory_order_relaxed);
        return;
    }

    BuildStatusMessage(L"状态: 运行中");
}

void StopClicker() {
    if (!g_running.exchange(false, std::memory_order_relaxed)) {
        return;
    }

    BuildStatusMessage(L"状态: 已停止");
}

void EnsureWorkerThread() {
    if (!g_clickThread.joinable()) {
        g_clickThread = std::thread(ClickLoop);
    }
}

void SyncModeSelectionFromRadio(HWND hwnd, int commandId) {
    ClickMode mode = ClickMode::MouseLeft;
    if (commandId == kRadioLeftId) {
        mode = ClickMode::MouseLeft;
        CheckRadioButton(hwnd, kRadioLeftId, kRadioKeyboardId, kRadioLeftId);
    } else if (commandId == kRadioRightId) {
        mode = ClickMode::MouseRight;
        CheckRadioButton(hwnd, kRadioLeftId, kRadioKeyboardId, kRadioRightId);
    } else if (commandId == kRadioMiddleId) {
        mode = ClickMode::MouseMiddle;
        CheckRadioButton(hwnd, kRadioLeftId, kRadioKeyboardId, kRadioMiddleId);
    } else if (commandId == kRadioKeyboardId) {
        mode = ClickMode::Keyboard;
        CheckRadioButton(hwnd, kRadioLeftId, kRadioKeyboardId, kRadioKeyboardId);
    }

    g_clickMode.store(static_cast<int>(mode), std::memory_order_relaxed);
    SaveEditorActionToModel();
    RefreshComboListBox();
    UpdateModeUi();
    if (!g_running.load(std::memory_order_relaxed)) {
        BuildStatusMessage(L"状态: 已停止");
    }
}

void CleanupWorkerThread() {
    g_shouldExit.store(true, std::memory_order_relaxed);
    g_running.store(false, std::memory_order_relaxed);
    if (g_clickThread.joinable()) {
        g_clickThread.join();
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowExW(0, L"STATIC", L"连点器", WS_CHILD | WS_VISIBLE,
            16, 10, 180, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        CreateWindowExW(0, L"STATIC", L"操作模式", WS_CHILD | WS_VISIBLE,
            16, 40, 90, 18, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            12, 56, 300, 38, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        g_modeSingleRadio = CreateWindowExW(0, L"BUTTON", L"单键连点", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                            24, 67, 92, 20, hwnd, reinterpret_cast<HMENU>(kModeSingleId),
                            GetModuleHandleW(nullptr), nullptr);
        g_modeComboRadio = CreateWindowExW(0, L"BUTTON", L"组合按键", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                           130, 67, 92, 20, hwnd, reinterpret_cast<HMENU>(kModeComboId),
                           GetModuleHandleW(nullptr), nullptr);

        CreateWindowExW(0, L"STATIC", L"动作设置", WS_CHILD | WS_VISIBLE,
            16, 104, 90, 18, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            12, 120, 470, 180, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        CreateWindowExW(0, L"STATIC", L"组合序列", WS_CHILD | WS_VISIBLE,
            22, 136, 76, 18, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        g_comboListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                         WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP,
                         22, 154, 146, 108, hwnd, reinterpret_cast<HMENU>(kComboListId),
                         GetModuleHandleW(nullptr), nullptr);
        g_comboAddButton = CreateWindowExW(0, L"BUTTON", L"添加按键", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                           22, 268, 68, 24, hwnd, reinterpret_cast<HMENU>(kComboAddId),
                           GetModuleHandleW(nullptr), nullptr);
        g_comboRemoveButton = CreateWindowExW(0, L"BUTTON", L"删除按键", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              96, 268, 68, 24, hwnd, reinterpret_cast<HMENU>(kComboRemoveId),
                              GetModuleHandleW(nullptr), nullptr);

        g_radioLeft = CreateWindowExW(0, L"BUTTON", L"鼠标左键", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                      190, 156, 96, 20, hwnd, reinterpret_cast<HMENU>(kRadioLeftId),
                          GetModuleHandleW(nullptr), nullptr);
        g_radioRight = CreateWindowExW(0, L"BUTTON", L"鼠标右键", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                       292, 156, 96, 20, hwnd, reinterpret_cast<HMENU>(kRadioRightId),
                           GetModuleHandleW(nullptr), nullptr);
        g_radioMiddle = CreateWindowExW(0, L"BUTTON", L"鼠标中键", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                        190, 186, 96, 20, hwnd, reinterpret_cast<HMENU>(kRadioMiddleId),
                        GetModuleHandleW(nullptr), nullptr);
        g_radioKeyboard = CreateWindowExW(0, L"BUTTON", L"键盘按键", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                          292, 186, 96, 20, hwnd, reinterpret_cast<HMENU>(kRadioKeyboardId),
                          GetModuleHandleW(nullptr), nullptr);

        CreateWindowExW(0, L"STATIC", L"按键", WS_CHILD | WS_VISIBLE,
            190, 218, 40, 18, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        g_editKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"点击后按键捕获",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        230, 214, 170, 26, hwnd, reinterpret_cast<HMENU>(kEditKeyId),
                        GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"STATIC", L"点击输入框后按下目标键完成捕获", WS_CHILD | WS_VISIBLE,
            190, 244, 210, 18, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        CreateWindowExW(0, L"STATIC", L"点击频率 (CPS)", WS_CHILD | WS_VISIBLE,
            514, 72, 96, 18, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        g_editCps = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"100",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                        612, 68, 108, 24, hwnd, reinterpret_cast<HMENU>(kEditCpsId),
                        GetModuleHandleW(nullptr), nullptr);

        CreateWindowExW(0, L"BUTTON", L"开始 (F6)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            514, 108, 96, 28, hwnd, reinterpret_cast<HMENU>(kStartButtonId),
            GetModuleHandleW(nullptr), nullptr);

        CreateWindowExW(0, L"BUTTON", L"停止 (F6)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            618, 108, 96, 28, hwnd, reinterpret_cast<HMENU>(kStopButtonId),
            GetModuleHandleW(nullptr), nullptr);

        g_statusText = CreateWindowExW(0, L"STATIC",
                           L"状态: 已停止 - 模式: 单键连点 - F6 切换, F8 退出",
                                       WS_CHILD | WS_VISIBLE, 16, 316, 700, 20, hwnd,
                           reinterpret_cast<HMENU>(kStatusTextId), GetModuleHandleW(nullptr), nullptr);

        CheckRadioButton(hwnd, kModeSingleId, kModeComboId, kModeSingleId);
        CheckRadioButton(hwnd, kRadioLeftId, kRadioKeyboardId, kRadioLeftId);

        g_oldCpsEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_editCps, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CpsEditProc)));
        g_oldKeyEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_editKey, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(KeyEditProc)));
        SendMessageW(g_editCps, EM_SETLIMITTEXT, 5, 0);

        ApplyFontToChildren(hwnd);

        RegisterHotKey(hwnd, kHotkeyToggleId, MOD_NOREPEAT, VK_F6);
        RegisterHotKey(hwnd, kHotkeyQuitId, MOD_NOREPEAT, VK_F8);
        ApplyOperationMode(OperationMode::Single);
        EnsureWorkerThread();
        return 0;
    }
    case WM_COMMAND: {
        const int commandId = LOWORD(wParam);
        if (commandId == kStartButtonId) {
            StartClicker();
        } else if (commandId == kStopButtonId) {
            StopClicker();
        } else if (commandId == kModeSingleId) {
            ApplyOperationMode(OperationMode::Single);
        } else if (commandId == kModeComboId) {
            ApplyOperationMode(OperationMode::Combo);
        } else if (commandId == kRadioLeftId || commandId == kRadioRightId || commandId == kRadioMiddleId || commandId == kRadioKeyboardId) {
            SyncModeSelectionFromRadio(hwnd, commandId);
        } else if (commandId == kComboListId && HIWORD(wParam) == LBN_SELCHANGE) {
            if (static_cast<OperationMode>(g_operationMode.load(std::memory_order_relaxed)) == OperationMode::Combo) {
                const int selectedIndex = static_cast<int>(SendMessageW(g_comboListBox, LB_GETCURSEL, 0, 0));
                SelectComboStep(selectedIndex);
            }
        } else if (commandId == kComboAddId && HIWORD(wParam) == BN_CLICKED) {
            AddComboStep();
        } else if (commandId == kComboRemoveId && HIWORD(wParam) == BN_CLICKED) {
            RemoveComboStep();
        } else if (commandId == kEditCpsId && HIWORD(wParam) == EN_CHANGE) {
            NormalizeCpsText(g_editCps);
        }
        return 0;
    }
    case WM_HOTKEY: {
        const int hotkeyId = static_cast<int>(wParam);
        if (hotkeyId == kHotkeyToggleId) {
            if (g_running.load(std::memory_order_relaxed)) {
                StopClicker();
            } else {
                StartClicker();
            }
        } else if (hotkeyId == kHotkeyQuitId) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, kHotkeyToggleId);
        UnregisterHotKey(hwnd, kHotkeyQuitId);
        CleanupWorkerThread();
        if (g_uiFont != nullptr) {
            DeleteObject(g_uiFont);
            g_uiFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const wchar_t* className = L"HighFrequencyClickerWindow";

    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        metrics.lfMessageFont.lfHeight = -15;
        metrics.lfMessageFont.lfWeight = FW_NORMAL;
        wcscpy_s(metrics.lfMessageFont.lfFaceName, L"Segoe UI");
        g_uiFont = CreateFontIndirectW(&metrics.lfMessageFont);
    }
    if (!g_uiFont) {
        g_uiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    HICON appIcon = reinterpret_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(kAppIconId),
        IMAGE_ICON,
        32,
        32,
        LR_DEFAULTCOLOR));
    if (!appIcon) {
        appIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }
    wc.hIcon = appIcon;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);

    if (!RegisterClassW(&wc)) {
        MessageBoxW(nullptr, L"窗口类注册失败。", L"连点器", MB_ICONERROR);
        return 1;
    }

    const DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD windowExStyle = 0;
    RECT targetClient = {0, 0, 724, 342};
    AdjustWindowRectEx(&targetClient, windowStyle, FALSE, windowExStyle);

    g_mainWindow = CreateWindowExW(windowExStyle, className, L"连点器",
                                   windowStyle,
                                   CW_USEDEFAULT, CW_USEDEFAULT,
                                   targetClient.right - targetClient.left,
                                   targetClient.bottom - targetClient.top,
                                   nullptr, nullptr, instance, nullptr);
    if (!g_mainWindow) {
        MessageBoxW(nullptr, L"窗口创建失败。", L"连点器", MB_ICONERROR);
        return 1;
    }

    if (appIcon) {
        SendMessageW(g_mainWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
        HICON smallIcon = reinterpret_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(kAppIconId),
            IMAGE_ICON,
            16,
            16,
            LR_DEFAULTCOLOR));
        if (!smallIcon) {
            smallIcon = appIcon;
        }
        SendMessageW(g_mainWindow, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }

    ShowWindow(g_mainWindow, showCommand);
    UpdateWindow(g_mainWindow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand) {
    (void)previousInstance;
    (void)commandLine;
    return wWinMain(instance, nullptr, GetCommandLineW(), showCommand);
}
