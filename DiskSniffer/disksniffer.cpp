#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Comctl32.lib")

constexpr int IDC_DRIVE = 101;
constexpr int IDC_PATH = 102;
constexpr int IDC_DEPTH = 103;
constexpr int IDC_MINPCT = 104;
constexpr int IDC_SCAN = 105;

constexpr UINT WM_SCAN_DONE = WM_APP + 1;
constexpr UINT WM_SCAN_STATUS = WM_APP + 2;

struct Node {
    std::wstring name;
    std::wstring path;
    uint64_t size = 0;
    bool directory = false;
    bool collapsed = false;
    std::vector<std::unique_ptr<Node>> children;
};

struct ScanStats {
    uint64_t skippedBytes = 0;
    uint64_t skippedItems = 0;
};

struct Tile {
    RECT rect{};
    Node* node = nullptr;
    int depth = 0;
};

HINSTANCE g_instance = nullptr;
HWND g_main = nullptr;
HWND g_drive = nullptr;
HWND g_path = nullptr;
HWND g_depth = nullptr;
HWND g_minPct = nullptr;
HWND g_scan = nullptr;

std::unique_ptr<Node> g_root;
std::vector<Tile> g_tiles;
std::vector<std::wstring> g_history;
std::map<std::wstring, std::unique_ptr<Node>> g_rootCache;
std::wstring g_currentPath;
std::wstring g_status = L"Ready";
std::wstring g_hoverText;
Node* g_hoverNode = nullptr;
RECT g_hoverRect{};
bool g_trackingMouse = false;
std::atomic<bool> g_scanning{false};
std::atomic<ULONGLONG> g_lastStatusTick{0};

std::wstring formatSize(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }
    wchar_t buffer[64];
    if (unit == 0) {
        swprintf_s(buffer, L"%llu %s", static_cast<unsigned long long>(bytes), units[unit]);
    } else {
        swprintf_s(buffer, L"%.1f %s", value, units[unit]);
    }
    return buffer;
}

std::wstring baseName(const std::wstring& path) {
    size_t end = path.find_last_not_of(L"\\/");
    if (end == std::wstring::npos) return path;
    size_t pos = path.find_last_of(L"\\/", end);
    if (pos == std::wstring::npos) return path.substr(0, end + 1);
    return path.substr(pos + 1, end - pos);
}

std::wstring joinPath(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) return right;
    wchar_t last = left[left.size() - 1];
    if (last == L'\\' || last == L'/') return left + right;
    return left + L"\\" + right;
}

uint64_t fileSizeFromData(const WIN32_FIND_DATAW& data) {
    ULARGE_INTEGER value{};
    value.HighPart = data.nFileSizeHigh;
    value.LowPart = data.nFileSizeLow;
    return value.QuadPart;
}

void postStatus(const std::wstring& text) {
    ULONGLONG now = GetTickCount64();
    ULONGLONG last = g_lastStatusTick.load();
    if (now - last < 120) return;
    if (!g_lastStatusTick.compare_exchange_strong(last, now)) return;

    auto* heapText = new std::wstring(text);
    PostMessageW(g_main, WM_SCAN_STATUS, 0, reinterpret_cast<LPARAM>(heapText));
}

RECT statusRectFor(HWND hwnd) {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    rect.top = rect.bottom - 22;
    return rect;
}

std::wstring statusText() {
    return g_hoverText.empty() ? g_status : g_hoverText;
}

void resetHover(HWND hwnd) {
    g_hoverNode = nullptr;
    g_hoverText.clear();
    SetRectEmpty(&g_hoverRect);
    g_trackingMouse = false;
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

uint64_t scanNode(Node& node, int depth, int maxDepth, ScanStats& stats) {
    std::wstring pattern = joinPath(node.path, L"*");
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        stats.skippedItems++;
        return node.size;
    }

    std::vector<std::unique_ptr<Node>> children;
    do {
        const wchar_t* name = data.cFileName;
        if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            stats.skippedItems++;
            continue;
        }

        auto child = std::make_unique<Node>();
        child->name = name;
        child->path = joinPath(node.path, name);
        child->directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (child->directory) {
            postStatus(L"Scanning " + child->path);
            scanNode(*child, depth + 1, maxDepth, stats);
        } else {
            child->size = fileSizeFromData(data);
        }

        node.size += child->size;
        if (depth < maxDepth && child->size > 0) {
            if (depth + 1 >= maxDepth && child->directory) {
                child->children.clear();
                child->collapsed = true;
            }
            children.push_back(std::move(child));
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
    std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
        return a->size > b->size;
    });
    node.children = std::move(children);
    return node.size;
}

uint64_t getDriveUsedBytes(const std::wstring& path) {
    wchar_t root[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), root, MAX_PATH)) return 0;

    ULARGE_INTEGER freeAvailable{};
    ULARGE_INTEGER totalBytes{};
    ULARGE_INTEGER totalFree{};
    if (!GetDiskFreeSpaceExW(root, &freeAvailable, &totalBytes, &totalFree)) return 0;
    if (totalBytes.QuadPart < totalFree.QuadPart) return 0;
    return totalBytes.QuadPart - totalFree.QuadPart;
}

bool isVolumeRoot(const std::wstring& path) {
    wchar_t root[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), root, MAX_PATH)) return false;
    std::wstring normalized = path;
    if (!normalized.empty() && normalized.back() != L'\\' && normalized.back() != L'/') {
        normalized += L"\\";
    }
    return _wcsicmp(normalized.c_str(), root) == 0;
}

std::wstring volumeRootKey(const std::wstring& path) {
    wchar_t root[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), root, MAX_PATH)) return L"";
    std::wstring key = root;
    std::transform(key.begin(), key.end(), key.begin(), towupper);
    return key;
}

void collapseSmallNodes(Node& node, uint64_t rootSize, double minRatio, int depth) {
    if (rootSize == 0 || minRatio <= 0) return;
    for (auto& child : node.children) {
        double ratio = static_cast<double>(child->size) / static_cast<double>(rootSize);
        if (depth > 0 && ratio < minRatio) {
            child->children.clear();
            child->collapsed = true;
            continue;
        }
        collapseSmallNodes(*child, rootSize, minRatio, depth + 1);
    }
}

std::unique_ptr<Node> scanPath(const std::wstring& path, int maxDepth, double minRatio) {
    auto root = std::make_unique<Node>();
    root->name = baseName(path);
    root->path = path;
    root->directory = true;
    ScanStats stats{};
    scanNode(*root, 0, maxDepth, stats);

    uint64_t usedBytes = isVolumeRoot(path) ? getDriveUsedBytes(path) : 0;
    if (usedBytes > root->size) {
        auto skipped = std::make_unique<Node>();
        skipped->name = L"<Protected / System>";
        skipped->path = path;
        skipped->size = usedBytes - root->size;
        skipped->directory = false;
        skipped->collapsed = true;
        root->size = usedBytes;
        root->children.push_back(std::move(skipped));
        std::sort(root->children.begin(), root->children.end(), [](const auto& a, const auto& b) {
            return a->size > b->size;
        });
    }

    collapseSmallNodes(*root, root->size, minRatio, 0);
    return root;
}

double rectWidth(const RECT& rect) {
    return static_cast<double>(rect.right - rect.left);
}

double rectHeight(const RECT& rect) {
    return static_cast<double>(rect.bottom - rect.top);
}

void layoutNode(Node* node, RECT rect, int depth);

double childSizeSum(const std::vector<std::unique_ptr<Node>>& children, size_t first, size_t last) {
    double total = 0;
    for (size_t i = first; i < last; i++) total += static_cast<double>(children[i]->size);
    return total;
}

void layoutChildren(Node* node, size_t first, size_t last, RECT rect, int depth) {
    if (!node || first >= last) return;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width < 3 || height < 3) return;

    if (last - first == 1 || width < 8 || height < 8) {
        layoutNode(node->children[first].get(), rect, depth + 1);
        return;
    }

    double total = childSizeSum(node->children, first, last);
    if (total <= 0) return;

    double half = total / 2.0;
    double running = 0;
    size_t split = first + 1;
    double bestDistance = total;
    double leftSum = 0;
    for (size_t i = first; i < last - 1; i++) {
        running += static_cast<double>(node->children[i]->size);
        double distance = std::abs(half - running);
        if (distance < bestDistance) {
            bestDistance = distance;
            split = i + 1;
            leftSum = running;
        }
    }
    if (leftSum <= 0) leftSum = childSizeSum(node->children, first, split);

    if (width >= height) {
        int splitWidth = static_cast<int>(std::round(width * (leftSum / total)));
        splitWidth = std::clamp(splitWidth, 3, width - 3);
        RECT left{rect.left, rect.top, rect.left + splitWidth, rect.bottom};
        RECT right{rect.left + splitWidth, rect.top, rect.right, rect.bottom};
        layoutChildren(node, first, split, left, depth);
        layoutChildren(node, split, last, right, depth);
    } else {
        int splitHeight = static_cast<int>(std::round(height * (leftSum / total)));
        splitHeight = std::clamp(splitHeight, 3, height - 3);
        RECT top{rect.left, rect.top, rect.right, rect.top + splitHeight};
        RECT bottom{rect.left, rect.top + splitHeight, rect.right, rect.bottom};
        layoutChildren(node, first, split, top, depth);
        layoutChildren(node, split, last, bottom, depth);
    }
}

void layoutNode(Node* node, RECT rect, int depth) {
    if (!node || node->size == 0) return;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width < 3 || height < 3) return;

    g_tiles.push_back({rect, node, depth});
    if (node->children.empty() || width < 24 || height < 24) return;

    RECT inner = rect;
    InflateRect(&inner, -2, -18);
    if (inner.right <= inner.left || inner.bottom <= inner.top) return;

    layoutChildren(node, 0, node->children.size(), inner, depth);
}

COLORREF colorForDepth(int depth, bool directory) {
    static COLORREF colors[] = {
        RGB(83, 145, 198), RGB(191, 143, 96), RGB(125, 164, 110),
        RGB(168, 122, 170), RGB(198, 123, 116), RGB(105, 166, 173)
    };
    COLORREF base = colors[depth % 6];
    if (!directory) {
        return RGB(GetRValue(base) + 20 > 255 ? 255 : GetRValue(base) + 20,
                   GetGValue(base) + 20 > 255 ? 255 : GetGValue(base) + 20,
                   GetBValue(base) + 20 > 255 ? 255 : GetBValue(base) + 20);
    }
    return base;
}

void drawTreemap(HDC hdc, RECT area) {
    g_tiles.clear();
    if (!g_root) return;
    layoutNode(g_root.get(), area, 0);

    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));
    SetBkMode(hdc, TRANSPARENT);

    for (const Tile& tile : g_tiles) {
        RECT r = tile.rect;
        HBRUSH brush = CreateSolidBrush(colorForDepth(tile.depth, tile.node->directory));
        FillRect(hdc, &r, brush);
        DeleteObject(brush);
        FrameRect(hdc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (tile.node == g_hoverNode) {
            HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 245, 140));
            HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
            HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, hollow));
            Rectangle(hdc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        if ((r.right - r.left) > 70 && (r.bottom - r.top) > 34) {
            std::wstring text = tile.node->name + L"\n" + formatSize(tile.node->size);
            RECT textRect = r;
            InflateRect(&textRect, -4, -4);
            SetTextColor(hdc, RGB(15, 20, 24));
            DrawTextW(hdc, text.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
        }
    }

    SelectObject(hdc, oldFont);
}

std::wstring getWindowText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(len, L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    return text;
}

int getIntText(HWND hwnd, int fallback) {
    std::wstring text = getWindowText(hwnd);
    try {
        int value = std::stoi(text);
        return value > 0 ? value : fallback;
    } catch (...) {
        return fallback;
    }
}

double getPercentText(HWND hwnd, double fallback) {
    std::wstring text = getWindowText(hwnd);
    try {
        double value = std::stod(text);
        return value >= 0 ? value / 100.0 : fallback;
    } catch (...) {
        return fallback;
    }
}

std::wstring selectedDriveRoot() {
    int index = static_cast<int>(SendMessageW(g_drive, CB_GETCURSEL, 0, 0));
    if (index < 0) return L"";
    wchar_t buffer[32]{};
    SendMessageW(g_drive, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(buffer));
    return buffer;
}

bool isAbsolutePathText(const std::wstring& path) {
    return path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
}

std::wstring trimLeadingSlash(std::wstring path) {
    while (!path.empty() && (path.front() == L'\\' || path.front() == L'/')) {
        path.erase(path.begin());
    }
    return path;
}

std::wstring resolvePathInput() {
    std::wstring input = getWindowText(g_path);
    if (isAbsolutePathText(input)) return input;

    std::wstring drive = selectedDriveRoot();
    if (drive.empty()) return input;

    input = trimLeadingSlash(input);
    if (input.empty()) return drive;
    return joinPath(drive, input);
}

void selectDriveForPath(const std::wstring& path) {
    std::wstring root = volumeRootKey(path);
    if (root.empty()) return;

    int count = static_cast<int>(SendMessageW(g_drive, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; i++) {
        wchar_t buffer[32]{};
        SendMessageW(g_drive, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(buffer));
        std::wstring item = buffer;
        std::transform(item.begin(), item.end(), item.begin(), towupper);
        if (item == root) {
            SendMessageW(g_drive, CB_SETCURSEL, i, 0);
            return;
        }
    }
}

std::wstring relativePathForDisplay(const std::wstring& path) {
    std::wstring root = volumeRootKey(path);
    if (root.empty()) return path;

    std::wstring normalized = path;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    if (_wcsnicmp(normalized.c_str(), root.c_str(), root.size()) != 0) return path;

    std::wstring relative = normalized.substr(root.size());
    return trimLeadingSlash(relative);
}

void updatePathEditFor(const std::wstring& path) {
    selectDriveForPath(path);
    SetWindowTextW(g_path, relativePathForDisplay(path).c_str());
}

void startScan() {
    if (g_scanning.exchange(true)) return;

    g_history.clear();
    g_rootCache.clear();
    EnableWindow(g_scan, FALSE);
    std::wstring path = resolvePathInput();
    updatePathEditFor(path);

    int maxDepth = getIntText(g_depth, 5);
    double minRatio = getPercentText(g_minPct, 0.002);
    g_status = L"Scanning " + path;
    g_lastStatusTick = 0;
    RECT statusRect = statusRectFor(g_main);
    InvalidateRect(g_main, &statusRect, FALSE);

    std::thread([path, maxDepth, minRatio]() {
        auto result = scanPath(path, maxDepth, minRatio);
        PostMessageW(g_main, WM_SCAN_DONE, 0, reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}

void cacheCurrentRootIfVolumeRoot() {
    if (!g_root || !isVolumeRoot(g_root->path)) return;
    std::wstring key = volumeRootKey(g_root->path);
    if (key.empty()) return;
    g_rootCache[key] = std::move(g_root);
    g_tiles.clear();
    resetHover(g_main);
}

bool restoreCachedRoot(const std::wstring& path) {
    std::wstring key = volumeRootKey(path);
    if (key.empty()) return false;
    auto it = g_rootCache.find(key);
    if (it == g_rootCache.end() || !it->second) return false;

    resetHover(g_main);
    g_root = std::move(it->second);
    g_rootCache.erase(it);
    g_currentPath = g_root ? g_root->path : path;
    updatePathEditFor(g_currentPath);
    g_status = L"Done: " + g_currentPath + L" / " + formatSize(g_root ? g_root->size : 0);
    g_scanning = false;
    EnableWindow(g_scan, TRUE);
    InvalidateRect(g_main, nullptr, FALSE);
    return true;
}

void startScanPath(const std::wstring& path, bool pushCurrent) {
    if (path.empty() || g_scanning.load()) return;
    if (pushCurrent && !g_currentPath.empty() && _wcsicmp(path.c_str(), g_currentPath.c_str()) == 0) return;
    if (pushCurrent && g_root && !g_currentPath.empty()) {
        g_history.push_back(g_currentPath);
        cacheCurrentRootIfVolumeRoot();
    }

    updatePathEditFor(path);
    if (g_scanning.exchange(true)) return;

    resetHover(g_main);
    EnableWindow(g_scan, FALSE);
    int maxDepth = getIntText(g_depth, 5);
    double minRatio = getPercentText(g_minPct, 0.002);
    g_status = L"Scanning " + path;
    g_lastStatusTick = 0;
    RECT statusRect = statusRectFor(g_main);
    InvalidateRect(g_main, &statusRect, FALSE);

    std::thread([path, maxDepth, minRatio]() {
        auto result = scanPath(path, maxDepth, minRatio);
        PostMessageW(g_main, WM_SCAN_DONE, 0, reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}

void goBack() {
    if (g_scanning.load() || g_history.empty()) return;
    std::wstring path = g_history.back();
    g_history.pop_back();
    if (isVolumeRoot(path) && restoreCachedRoot(path)) return;
    startScanPath(path, false);
}

bool textInputHasFocus() {
    HWND focus = GetFocus();
    return focus == g_path || focus == g_depth || focus == g_minPct;
}

void showShellContextMenu(HWND hwnd, const std::wstring& path, POINT screenPoint) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) return;

    IShellFolder* parent = nullptr;
    PCUITEMID_CHILD child = nullptr;
    if (FAILED(SHBindToParent(pidl, IID_IShellFolder, reinterpret_cast<void**>(&parent), &child)) || !parent || !child) {
        CoTaskMemFree(pidl);
        return;
    }

    IContextMenu* contextMenu = nullptr;
    HRESULT hr = parent->GetUIObjectOf(hwnd, 1, &child, IID_IContextMenu, nullptr, reinterpret_cast<void**>(&contextMenu));
    if (SUCCEEDED(hr) && contextMenu) {
        HMENU menu = CreatePopupMenu();
        if (menu && SUCCEEDED(contextMenu->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL))) {
            SetForegroundWindow(hwnd);
            int command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, hwnd, nullptr);
            if (command > 0) {
                CMINVOKECOMMANDINFOEX info{};
                info.cbSize = sizeof(info);
                info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                info.hwnd = hwnd;
                info.lpVerb = MAKEINTRESOURCEA(command - 1);
                info.lpVerbW = MAKEINTRESOURCEW(command - 1);
                info.nShow = SW_SHOWNORMAL;
                info.ptInvoke = screenPoint;
                contextMenu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
            }
        }
        if (menu) DestroyMenu(menu);
        contextMenu->Release();
    }

    parent->Release();
    CoTaskMemFree(pidl);
}

void fillDrives() {
    DWORD mask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; letter++) {
        if (!(mask & (1 << (letter - L'A')))) continue;
        wchar_t root[] = {letter, L':', L'\\', L'\0'};
        UINT type = GetDriveTypeW(root);
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
            SendMessageW(g_drive, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(root));
        }
    }
    SendMessageW(g_drive, CB_SETCURSEL, 0, 0);
}

void createControls(HWND hwnd) {
    CreateWindowW(L"STATIC", L"Drive", WS_CHILD | WS_VISIBLE, 10, 12, 40, 22, hwnd, nullptr, g_instance, nullptr);
    g_drive = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 54, 8, 90, 220, hwnd, reinterpret_cast<HMENU>(IDC_DRIVE), g_instance, nullptr);
    fillDrives();

    CreateWindowW(L"STATIC", L"Path", WS_CHILD | WS_VISIBLE, 154, 12, 36, 22, hwnd, nullptr, g_instance, nullptr);
    g_path = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 194, 8, 360, 24, hwnd, reinterpret_cast<HMENU>(IDC_PATH), g_instance, nullptr);

    CreateWindowW(L"STATIC", L"Depth", WS_CHILD | WS_VISIBLE, 566, 12, 45, 22, hwnd, nullptr, g_instance, nullptr);
    g_depth = CreateWindowW(L"EDIT", L"5", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 612, 8, 42, 24, hwnd, reinterpret_cast<HMENU>(IDC_DEPTH), g_instance, nullptr);

    CreateWindowW(L"STATIC", L"Min %", WS_CHILD | WS_VISIBLE, 666, 12, 46, 22, hwnd, nullptr, g_instance, nullptr);
    g_minPct = CreateWindowW(L"EDIT", L"0.2", WS_CHILD | WS_VISIBLE | WS_BORDER, 716, 8, 50, 24, hwnd, reinterpret_cast<HMENU>(IDC_MINPCT), g_instance, nullptr);

    g_scan = CreateWindowW(L"BUTTON", L"Scan", WS_CHILD | WS_VISIBLE, 780, 7, 72, 26, hwnd, reinterpret_cast<HMENU>(IDC_SCAN), g_instance, nullptr);
}

const Tile* tileAt(POINT point) {
    for (auto it = g_tiles.rbegin(); it != g_tiles.rend(); ++it) {
        if (PtInRect(&it->rect, point)) return &(*it);
    }
    return nullptr;
}

void updateHover(HWND hwnd, LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    const Tile* tile = tileAt(point);
    Node* node = tile ? tile->node : nullptr;
    if (node == g_hoverNode) return;

    RECT oldRect = g_hoverRect;
    bool hadHover = g_hoverNode != nullptr;
    g_hoverNode = node;
    if (tile) {
        g_hoverRect = tile->rect;
        g_hoverText = tile->node->path + L" / " + formatSize(tile->node->size);
    } else {
        SetRectEmpty(&g_hoverRect);
        g_hoverText.clear();
    }

    if (hadHover) InvalidateRect(hwnd, &oldRect, FALSE);
    if (tile) InvalidateRect(hwnd, &g_hoverRect, FALSE);
    RECT statusRect = statusRectFor(hwnd);
    InvalidateRect(hwnd, &statusRect, FALSE);

    if (!g_trackingMouse) {
        TRACKMOUSEEVENT event{};
        event.cbSize = sizeof(event);
        event.dwFlags = TME_LEAVE;
        event.hwndTrack = hwnd;
        g_trackingMouse = TrackMouseEvent(&event) != FALSE;
    }
}

void clearHover(HWND hwnd) {
    if (!g_hoverNode) {
        g_trackingMouse = false;
        return;
    }

    RECT oldRect = g_hoverRect;
    g_hoverNode = nullptr;
    SetRectEmpty(&g_hoverRect);
    g_hoverText.clear();
    g_trackingMouse = false;
    InvalidateRect(hwnd, &oldRect, FALSE);
    RECT statusRect = statusRectFor(hwnd);
    InvalidateRect(hwnd, &statusRect, FALSE);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        createControls(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_SCAN) {
            startScan();
        } else if (LOWORD(wParam) == IDC_DRIVE && HIWORD(wParam) == CBN_SELCHANGE) {
            SetWindowTextW(g_path, L"");
        }
        return 0;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SCAN_STATUS: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
        g_status = *text;
        RECT statusRect = statusRectFor(hwnd);
        InvalidateRect(hwnd, &statusRect, FALSE);
        return 0;
    }
    case WM_SCAN_DONE: {
        g_root.reset(reinterpret_cast<Node*>(lParam));
        g_hoverNode = nullptr;
        g_hoverText.clear();
        SetRectEmpty(&g_hoverRect);
        g_currentPath = g_root ? g_root->path : L"";
        g_status = L"Done: " + g_root->path + L" / " + formatSize(g_root->size);
        g_scanning = false;
        EnableWindow(g_scan, TRUE);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        updateHover(hwnd, lParam);
        return 0;
    case WM_LBUTTONUP: {
        SetFocus(hwnd);
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const Tile* tile = tileAt(point);
        if (tile && tile->node && tile->node->directory) {
            startScanPath(tile->node->path, true);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        SetFocus(hwnd);
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const Tile* tile = tileAt(point);
        if (tile && tile->node) {
            POINT screenPoint = point;
            ClientToScreen(hwnd, &screenPoint);
            showShellContextMenu(hwnd, tile->node->path, screenPoint);
        }
        return 0;
    }
    case WM_XBUTTONUP:
        if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) {
            goBack();
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_BACK) {
            if (!textInputHasFocus()) goBack();
            return 0;
        }
        break;
    case WM_MOUSELEAVE:
        clearHover(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        int width = client.right - client.left;
        int height = client.bottom - client.top;
        if (width <= 0 || height <= 0) {
            EndPaint(hwnd, &ps);
            return 0;
        }
        HDC memDc = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
        HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDc, bitmap));

        RECT bg = client;
        HBRUSH back = CreateSolidBrush(RGB(238, 241, 245));
        FillRect(memDc, &bg, back);
        DeleteObject(back);

        RECT map = client;
        map.top = 42;
        map.bottom -= 24;
        if (map.bottom > map.top) {
            if (g_root) {
                drawTreemap(memDc, map);
            } else {
                SetTextColor(memDc, RGB(80, 90, 100));
                SetBkMode(memDc, TRANSPARENT);
                DrawTextW(memDc, L"Choose a drive or enter a path, then click Scan.", -1, &map, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        RECT statusRect = statusRectFor(hwnd);
        SetTextColor(memDc, RGB(20, 24, 28));
        SetBkMode(memDc, TRANSPARENT);
        std::wstring currentStatus = statusText();
        DrawTextW(memDc, currentStatus.c_str(), -1, &statusRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        BitBlt(hdc, 0, 0, width, height, memDc, 0, 0, SRCCOPY);
        SelectObject(memDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    g_instance = instance;
    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX icc{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"DiskSnifferWindow";
    RegisterClassW(&wc);

    g_main = CreateWindowExW(0, wc.lpszClassName, L"DiskSniffer", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1120, 760,
                             nullptr, nullptr, instance, nullptr);
    ShowWindow(g_main, show);
    UpdateWindow(g_main);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_BACK) {
            if (!textInputHasFocus()) {
                goBack();
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    OleUninitialize();
    return static_cast<int>(msg.wParam);
}
