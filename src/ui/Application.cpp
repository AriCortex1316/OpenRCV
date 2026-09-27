#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

#include "openrcv/storage/VolumeEnumerator.h"
#include "openrcv/ui/Application.h"
#include "openrcv/workflow/ImageSourceService.h"

namespace {

constexpr wchar_t kWindowClass[] = L"OpenRCVWindow";
constexpr COLORREF kColorBackground = RGB(243, 246, 250);
constexpr COLORREF kColorSurface = RGB(255, 255, 255);
constexpr COLORREF kColorSidebar = RGB(22, 31, 49);
constexpr COLORREF kColorSidebarMuted = RGB(154, 166, 188);
constexpr COLORREF kColorPrimary = RGB(42, 101, 214);
constexpr COLORREF kColorPrimaryPressed = RGB(31, 78, 170);
constexpr COLORREF kColorText = RGB(30, 41, 59);
constexpr COLORREF kColorMuted = RGB(100, 116, 139);
constexpr COLORREF kColorBorder = RGB(221, 227, 235);
enum ControlId : int {
    IDC_NAV_INSTALL = 100,
    IDC_NAV_SETTINGS,
    IDC_ISO_PATH,
    IDC_BROWSE,
    IDC_IMAGE,
    IDC_VOLUME,
    IDC_REFRESH_VOLUMES,
    IDC_SKIP_NETWORK,
    IDC_FORMAT_TARGET,
    IDC_LANGUAGE,
    IDC_REGION,
    IDC_VALIDATE,
    IDC_LOG,
    IDC_DISABLE_UPDATE,
    IDC_SHOW_EXTENSIONS,
    IDC_DISABLE_WIDGETS,
    IDC_DISABLE_FAST_STARTUP,
    IDC_DISABLE_UAC,
    IDC_PREVENT_BITLOCKER,
    IDC_EXCLUDE_DRIVERS,
};

struct AppState {
    HWND window{};
    HWND isoPath{};
    HWND image{};
    HWND volume{};
    HWND language{};
    HWND region{};
    HWND log{};
    HFONT titleFont{};
    HFONT headingFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    HBRUSH surfaceBrush{};
    HBRUSH editBrush{};
    int activePage{};
    std::vector<HWND> installControls;
    std::vector<HWND> settingsControls;
    openrcv::workflow::ImageSourceService imageSource;
};

AppState g_app;

void AppendLog(const std::wstring& text) {
    if (!g_app.log) return;
    const int length = GetWindowTextLengthW(g_app.log);
    SendMessageW(g_app.log, EM_SETSEL, length, length);
    const std::wstring line = text + L"\r\n";
    SendMessageW(g_app.log, EM_REPLACESEL, FALSE,
                 reinterpret_cast<LPARAM>(line.c_str()));
}

HFONT CreateUiFont(int pointSize, int weight) {
    HDC dc = GetDC(nullptr);
    const int height = -MulDiv(pointSize, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, dc);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void SetControlFont(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND AddControl(DWORD exStyle, const wchar_t* cls, const wchar_t* text,
                DWORD style, int x, int y, int width, int height, int id) {
    HWND control = CreateWindowExW(exStyle, cls, text,
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, g_app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SetControlFont(control, g_app.bodyFont);
    return control;
}

HWND AddLabel(const wchar_t* text, int x, int y, int width, int height,
              HFONT font = nullptr) {
    HWND label = AddControl(0, L"STATIC", text, SS_LEFT, x, y, width, height, -1);
    if (font) SetControlFont(label, font);
    return label;
}

HWND AddButton(const wchar_t* text, int x, int y, int width, int height, int id) {
    return AddControl(0, L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP,
                      x, y, width, height, id);
}

void AddInstall(HWND control) { g_app.installControls.push_back(control); }
void AddSettings(HWND control) { g_app.settingsControls.push_back(control); }

void LoadImages() {
    SendMessageW(g_app.image, CB_RESETCONTENT, 0, 0);
    wchar_t pathBuffer[32768]{};
    GetWindowTextW(g_app.isoPath, pathBuffer, static_cast<int>(std::size(pathBuffer)));
    const openrcv::workflow::ImageSourceResult result = g_app.imageSource.LoadIso(pathBuffer);
    for (const std::wstring& message : result.messages) AppendLog(message);
    for (const openrcv::imaging::WindowsImage& image : result.images) {
        const std::wstring item = std::to_wstring(image.index) + L" - " + image.name;
        const LRESULT position = SendMessageW(g_app.image, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(item.c_str()));
        SendMessageW(g_app.image, CB_SETITEMDATA, position, image.index);
    }
    if (result.success) {
        SendMessageW(g_app.image, CB_SETCURSEL, 0, 0);
    }
}

void BrowseIso() {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = g_app.window;
    dialog.lpstrFilter = L"Windows ISO (*.iso)\0*.iso\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrDefExt = L"iso";
    if (GetOpenFileNameW(&dialog)) {
        SetWindowTextW(g_app.isoPath, path);
        AppendLog(L"选择 ISO：" + std::wstring(path));
        LoadImages();
    }
}

void RefreshVolumes() {
    SendMessageW(g_app.volume, CB_RESETCONTENT, 0, 0);
    const std::vector<openrcv::storage::VolumeInfo> volumes =
        openrcv::storage::EnumerateInstallVolumes();
    for (const openrcv::storage::VolumeInfo& volume : volumes) {
        const std::uint64_t totalGiB = volume.totalBytes / (1024ull * 1024ull * 1024ull);
        const std::uint64_t freeGiB = volume.freeBytes / (1024ull * 1024ull * 1024ull);
        std::wstring item = volume.root;
        item += L"  ";
        item += volume.label.empty() ? L"(无卷标)" : volume.label;
        item += L"  ";
        item += volume.fileSystem.empty() ? L"未知格式" : volume.fileSystem;
        item += L"  " + std::to_wstring(totalGiB) + L" GB，剩余 " +
                std::to_wstring(freeGiB) + L" GB";
        SendMessageW(g_app.volume, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(item.c_str()));
    }
    if (SendMessageW(g_app.volume, CB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(g_app.volume, CB_SETCURSEL, 0, 0);
    }
    AppendLog(L"已刷新可用的固定磁盘卷。");
}

void SelectTab(int index) {
    g_app.activePage = index;
    for (HWND control : g_app.installControls) ShowWindow(control, index == 0 ? SW_SHOW : SW_HIDE);
    for (HWND control : g_app.settingsControls) ShowWindow(control, index == 1 ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_app.window, nullptr, TRUE);
    HWND install = GetDlgItem(g_app.window, IDC_NAV_INSTALL);
    HWND settings = GetDlgItem(g_app.window, IDC_NAV_SETTINGS);
    if (install) InvalidateRect(install, nullptr, TRUE);
    if (settings) InvalidateRect(settings, nullptr, TRUE);
}

void ValidateSelection() {
    if (SendMessageW(g_app.image, CB_GETCURSEL, 0, 0) == CB_ERR) {
        MessageBoxW(g_app.window, L"请先选择包含 Windows 镜像的 ISO。", L"OpenRCV", MB_ICONWARNING);
        return;
    }
    if (SendMessageW(g_app.volume, CB_GETCURSEL, 0, 0) == CB_ERR) {
        MessageBoxW(g_app.window, L"没有选择安装分区。", L"OpenRCV", MB_ICONWARNING);
        return;
    }
    AppendLog(L"配置检查通过。安装执行模块尚未接入。");
    MessageBoxW(g_app.window,
        L"配置检查通过。\n\n安装执行模块尚未接入。",
        L"OpenRCV", MB_OK | MB_ICONINFORMATION);
}

void CreateUi(HWND window) {
    g_app.window = window;
    g_app.titleFont = CreateUiFont(20, FW_SEMIBOLD);
    g_app.headingFont = CreateUiFont(12, FW_SEMIBOLD);
    g_app.bodyFont = CreateUiFont(10, FW_NORMAL);
    g_app.smallFont = CreateUiFont(9, FW_NORMAL);
    g_app.surfaceBrush = CreateSolidBrush(kColorSurface);
    g_app.editBrush = CreateSolidBrush(RGB(248, 250, 252));

    AddButton(L"  安装 Windows", 14, 112, 156, 44, IDC_NAV_INSTALL);
    AddButton(L"  安装设置", 14, 164, 156, 44, IDC_NAV_SETTINGS);

    AddInstall(AddLabel(L"安装来源", 238, 120, 300, 26, g_app.headingFont));
    AddInstall(AddLabel(L"Windows ISO", 238, 158, 180, 22, g_app.smallFont));
    g_app.isoPath = AddControl(WS_EX_STATICEDGE, L"EDIT", L"",
                               ES_AUTOHSCROLL | WS_TABSTOP, 238, 181, 528, 32, IDC_ISO_PATH);
    SendMessageW(g_app.isoPath, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(9, 9));
    AddInstall(g_app.isoPath);
    AddInstall(AddButton(L"选择 ISO", 778, 181, 108, 32, IDC_BROWSE));
    AddInstall(AddLabel(L"Windows 版本", 238, 226, 180, 22, g_app.smallFont));
    g_app.image = AddControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                             238, 249, 648, 240, IDC_IMAGE);
    AddInstall(g_app.image);
    AddInstall(AddLabel(L"目标分区", 238, 294, 180, 22, g_app.smallFont));
    g_app.volume = AddControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                              238, 317, 528, 240, IDC_VOLUME);
    AddInstall(g_app.volume);
    AddInstall(AddButton(L"刷新分区", 778, 317, 108, 32, IDC_REFRESH_VOLUMES));
    AddInstall(AddControl(0, L"BUTTON", L"跳过联网并创建本地管理员 User（空密码）",
                          BS_AUTOCHECKBOX | WS_TABSTOP, 238, 365, 360, 24, IDC_SKIP_NETWORK));
    HWND format = AddControl(0, L"BUTTON", L"格式化当前安装分区",
                             BS_AUTOCHECKBOX | WS_TABSTOP, 610, 365, 260, 24, IDC_FORMAT_TARGET);
    SendMessageW(format, BM_SETCHECK, BST_CHECKED, 0);
    AddInstall(format);
    AddInstall(AddLabel(L"语言", 238, 408, 90, 22, g_app.smallFont));
    g_app.language = AddControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                238, 431, 220, 160, IDC_LANGUAGE);
    SendMessageW(g_app.language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"简体中文 (zh-CN)"));
    SendMessageW(g_app.language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English (en-US)"));
    SendMessageW(g_app.language, CB_SETCURSEL, 0, 0);
    AddInstall(g_app.language);
    AddInstall(AddLabel(L"地区", 474, 408, 90, 22, g_app.smallFont));
    g_app.region = AddControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                              474, 431, 190, 160, IDC_REGION);
    SendMessageW(g_app.region, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"美国 (en-US)"));
    SendMessageW(g_app.region, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"中国 (zh-CN)"));
    SendMessageW(g_app.region, CB_SETCURSEL, 0, 0);
    AddInstall(g_app.region);
    AddInstall(AddButton(L"检查配置", 690, 427, 196, 38, IDC_VALIDATE));

    const struct { const wchar_t* text; int id; bool checked; } settings[] = {
        {L"禁用 Windows Update 自动更新", IDC_DISABLE_UPDATE, false},
        {L"显示已知文件类型的扩展名", IDC_SHOW_EXTENSIONS, true},
        {L"禁用 Widgets", IDC_DISABLE_WIDGETS, false},
        {L"禁用 Fast Startup（快速启动）", IDC_DISABLE_FAST_STARTUP, false},
        {L"禁用 UAC（不推荐，会降低安全性）", IDC_DISABLE_UAC, false},
        {L"阻止 Windows 自动启用 BitLocker", IDC_PREVENT_BITLOCKER, false},
        {L"禁止 Windows Update 自动更新驱动", IDC_EXCLUDE_DRIVERS, false},
    };
    AddSettings(AddLabel(L"安装设置", 238, 120, 300, 26, g_app.headingFont));
    AddSettings(AddLabel(L"这些选项将在系统部署后写入离线 Windows。",
                         238, 151, 560, 22, g_app.smallFont));
    int y = 190;
    for (const auto& setting : settings) {
        HWND checkbox = AddControl(0, L"BUTTON", setting.text,
            BS_AUTOCHECKBOX | WS_TABSTOP, 238, y, 600, 26, setting.id);
        SendMessageW(checkbox, BM_SETCHECK,
                     setting.checked ? BST_CHECKED : BST_UNCHECKED, 0);
        AddSettings(checkbox);
        y += 38;
    }

    AddLabel(L"运行日志", 238, 514, 240, 25, g_app.headingFont);
    g_app.log = AddControl(WS_EX_STATICEDGE, L"EDIT", L"OpenRCV 已启动。\r\n",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL | WS_VSCROLL,
        238, 545, 648, 112, IDC_LOG);
    SetControlFont(g_app.log, g_app.smallFont);
    SendMessageW(g_app.log, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(10, 10));
    SelectTab(0);
    RefreshVolumes();
}

void FillSolidRect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void DrawRoundedPanel(HDC dc, const RECT& rect) {
    RECT shadow = rect;
    OffsetRect(&shadow, 0, 2);
    HBRUSH shadowBrush = CreateSolidBrush(RGB(228, 233, 241));
    HPEN shadowPen = CreatePen(PS_SOLID, 1, RGB(228, 233, 241));
    HGDIOBJ oldBrush = SelectObject(dc, shadowBrush);
    HGDIOBJ oldPen = SelectObject(dc, shadowPen);
    RoundRect(dc, shadow.left, shadow.top, shadow.right, shadow.bottom, 14, 14);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(shadowBrush);
    DeleteObject(shadowPen);

    HBRUSH surface = CreateSolidBrush(kColorSurface);
    HPEN border = CreatePen(PS_SOLID, 1, kColorBorder);
    oldBrush = SelectObject(dc, surface);
    oldPen = SelectObject(dc, border);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 14, 14);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(surface);
    DeleteObject(border);
}

void DrawUiText(HDC dc, const wchar_t* text, RECT rect, HFONT font,
                COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, format);
    SelectObject(dc, oldFont);
}

void PaintUi(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    FillSolidRect(dc, client, kColorBackground);

    RECT header{0, 0, client.right, 76};
    FillSolidRect(dc, header, kColorSurface);
    RECT sidebar{0, 76, 184, client.bottom};
    FillSolidRect(dc, sidebar, kColorSidebar);
    RECT divider{0, 75, client.right, 76};
    FillSolidRect(dc, divider, kColorBorder);

    RECT logo{24, 19, 60, 55};
    HBRUSH logoBrush = CreateSolidBrush(kColorPrimary);
    HPEN logoPen = CreatePen(PS_SOLID, 1, kColorPrimary);
    HGDIOBJ oldBrush = SelectObject(dc, logoBrush);
    HGDIOBJ oldPen = SelectObject(dc, logoPen);
    RoundRect(dc, logo.left, logo.top, logo.right, logo.bottom, 10, 10);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(logoBrush);
    DeleteObject(logoPen);
    DrawUiText(dc, L"O", logo, g_app.headingFont, RGB(255, 255, 255),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DrawUiText(dc, L"OpenRCV", RECT{72, 14, 320, 43}, g_app.titleFont,
               kColorText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawUiText(dc, L"Windows 部署工具", RECT{73, 43, 320, 63}, g_app.smallFont,
               kColorMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DrawUiText(dc, L"工作区", RECT{22, 87, 160, 107}, g_app.smallFont,
               kColorSidebarMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawUiText(dc, L"OpenRCV", RECT{18, client.bottom - 48, 170, client.bottom - 28},
               g_app.smallFont, kColorSidebarMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawUiText(dc, L"VERSION 0.1", RECT{18, client.bottom - 28, 170, client.bottom - 8},
               g_app.smallFont, RGB(96, 165, 250), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DrawRoundedPanel(dc, RECT{214, 104, 910, 484});
    DrawRoundedPanel(dc, RECT{214, 500, 910, 678});
    EndPaint(window, &paint);
}

void DrawButton(const DRAWITEMSTRUCT& item) {
    const int id = static_cast<int>(item.CtlID);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool isNav = id == IDC_NAV_INSTALL || id == IDC_NAV_SETTINGS;
    const bool activeNav = (id == IDC_NAV_INSTALL && g_app.activePage == 0) ||
                           (id == IDC_NAV_SETTINGS && g_app.activePage == 1);
    const bool primary = id == IDC_VALIDATE;

    COLORREF background = RGB(238, 242, 247);
    COLORREF border = kColorBorder;
    COLORREF text = kColorText;
    if (isNav) {
        background = activeNav ? kColorPrimary : kColorSidebar;
        border = background;
        text = activeNav ? RGB(255, 255, 255) : RGB(203, 213, 225);
    } else if (primary) {
        background = pressed ? kColorPrimaryPressed : kColorPrimary;
        border = background;
        text = RGB(255, 255, 255);
    } else if (pressed) {
        background = RGB(226, 232, 240);
    }
    if (disabled) text = RGB(148, 163, 184);

    HBRUSH brush = CreateSolidBrush(background);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(item.hDC, brush);
    HGDIOBJ oldPen = SelectObject(item.hDC, pen);
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top,
              item.rcItem.right, item.rcItem.bottom, 9, 9);
    SelectObject(item.hDC, oldBrush);
    SelectObject(item.hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    wchar_t caption[128]{};
    GetWindowTextW(item.hwndItem, caption, static_cast<int>(std::size(caption)));
    RECT textRect = item.rcItem;
    if (pressed) OffsetRect(&textRect, 0, 1);
    DrawUiText(item.hDC, caption, textRect, g_app.bodyFont, text,
               (isNav ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE);
    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(item.hDC, &focus);
    }
}

void DestroyUiResources() {
    if (g_app.titleFont) DeleteObject(g_app.titleFont);
    if (g_app.headingFont) DeleteObject(g_app.headingFont);
    if (g_app.bodyFont) DeleteObject(g_app.bodyFont);
    if (g_app.smallFont) DeleteObject(g_app.smallFont);
    if (g_app.surfaceBrush) DeleteObject(g_app.surfaceBrush);
    if (g_app.editBrush) DeleteObject(g_app.editBrush);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateUi(window);
        return 0;
    case WM_PAINT:
        PaintUi(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        if (reinterpret_cast<DRAWITEMSTRUCT*>(lParam)->CtlType == ODT_BUTTON) {
            DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kColorText);
        SetBkColor(dc, kColorSurface);
        return reinterpret_cast<LRESULT>(g_app.surfaceBrush);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kColorText);
        SetBkColor(dc, RGB(248, 250, 252));
        return reinterpret_cast<LRESULT>(g_app.editBrush);
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_NAV_INSTALL: SelectTab(0); return 0;
        case IDC_NAV_SETTINGS: SelectTab(1); return 0;
        case IDC_BROWSE: BrowseIso(); return 0;
        case IDC_REFRESH_VOLUMES: RefreshVolumes(); return 0;
        case IDC_VALIDATE: ValidateSelection(); return 0;
        }
        return 0;
    case WM_DESTROY:
        g_app.imageSource.Close();
        DestroyUiResources();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int openrcv::ui::RunApplication(HINSTANCE instance, int showCommand) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) return 1;

    constexpr DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                  WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT windowRect{0, 0, 940, 700};
    AdjustWindowRectEx(&windowRect, windowStyle, FALSE, 0);
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int x = workArea.left + std::max(0L, (workArea.right - workArea.left - width) / 2);
    const int y = workArea.top + std::max(0L, (workArea.bottom - workArea.top - height) / 2);
    HWND window = CreateWindowExW(0, kWindowClass, L"OpenRCV — Windows 部署工具",
        windowStyle, x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
