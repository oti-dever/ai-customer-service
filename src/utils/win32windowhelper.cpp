#include "win32windowhelper.h"
#include "logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <shlobj.h>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <algorithm>
#include <cstring>

namespace {
struct EnumData {
    QVector<WindowInfo>* list = nullptr;
    DWORD currentPid = 0;
};

using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
using AdjustWindowRectExForDpiFn = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);

QPixmap pixmapFromHIcon(HICON iconHandle, int width, int height)
{
    if (!iconHandle || width <= 0 || height <= 0) return {};

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) return {};

    BITMAPV5HEADER bi;
    std::memset(&bi, 0, sizeof(bi));
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = width;
    bi.bV5Height = -height;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO*>(&bi),
                                      DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        ReleaseDC(nullptr, screenDc);
        return {};
    }

    HDC memDc = CreateCompatibleDC(screenDc);
    HGDIOBJ oldObject = SelectObject(memDc, bitmap);
    std::memset(bits, 0, size_t(width) * size_t(height) * 4);
    DrawIconEx(memDc, 0, 0, iconHandle, width, height, 0, nullptr, DI_NORMAL);

    QImage image(reinterpret_cast<const uchar*>(bits), width, height, QImage::Format_ARGB32_Premultiplied);
    QPixmap pixmap = QPixmap::fromImage(image.copy());

    SelectObject(memDc, oldObject);
    DeleteDC(memDc);
    DeleteObject(bitmap);
    ReleaseDC(nullptr, screenDc);
    return pixmap;
}

QIcon iconFromHandle(HICON iconHandle)
{
    if (!iconHandle) return {};
    QIcon icon;
    // 勿用 small/large 作变量名：Windows 头文件常 #define small char，会破坏编译。
    const QPixmap pm16 = pixmapFromHIcon(iconHandle, 16, 16);
    const QPixmap pm32 = pixmapFromHIcon(iconHandle, 32, 32);
    if (!pm16.isNull()) icon.addPixmap(pm16);
    if (!pm32.isNull()) icon.addPixmap(pm32);
    return icon;
}

HICON tryGetWindowHIcon(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return nullptr;

    auto fetchIcon = [&](WPARAM kind) -> HICON {
        return reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, kind, 0));
    };

    HICON icon = fetchIcon(ICON_BIG);
    if (!icon) icon = fetchIcon(ICON_SMALL2);
    if (!icon) icon = fetchIcon(ICON_SMALL);
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
    return icon;
}

UINT queryWindowDpi(HWND hwnd)
{
    static auto getDpiForWindowFn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpiForWindowFn && hwnd && IsWindow(hwnd)) {
        return getDpiForWindowFn(hwnd);
    }

    HDC dc = GetDC(hwnd ? hwnd : nullptr);
    if (!dc) return 96;
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd ? hwnd : nullptr, dc);
    return dpi > 0 ? UINT(dpi) : 96;
}

BOOL adjustWindowRectForCurrentDpi(RECT* rect, DWORD style, BOOL hasMenu, DWORD exStyle, UINT dpi)
{
    static auto adjustForDpiFn = reinterpret_cast<AdjustWindowRectExForDpiFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (adjustForDpiFn) {
        return adjustForDpiFn(rect, style, hasMenu, exStyle, dpi);
    }
    return AdjustWindowRectEx(rect, style, hasMenu, exStyle);
}
}

QString baseNameFromPath(const wchar_t* path, DWORD length) {
    if (!path || length == 0) return {};
    const wchar_t* end = path + length;
    const wchar_t* p = end - 1;
    while (p >= path && *p != L'\\' && *p != L'/') --p;
    if (p >= path) ++p; else p = path;
    return QString::fromWCharArray(p, int(end - p));
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumData*>(lParam);
    if (!data || !data->list) return TRUE;
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    wchar_t title[512] = {0};
    GetWindowTextW(hwnd, title, 511);
    if (title[0] == L'\0') return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->currentPid) return TRUE;

    wchar_t className[256] = {0};
    GetClassNameW(hwnd, className, 255);
    QString qTitle = QString::fromWCharArray(title);
    QString qClass = QString::fromWCharArray(className);

    const QString titleLower = qTitle.toLower();
    const QString classLower = qClass.toLower();
    if (titleLower == QStringLiteral("program manager")
        || titleLower.contains(QStringLiteral("系统托盘溢出窗口"))
        || classLower == QStringLiteral("progman")
        || classLower == QStringLiteral("toplevelwindowforoverflowxamlisland")) {
        return TRUE;
    }

    QString processName;
    QString processPath;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        wchar_t buffer[MAX_PATH] = {0};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size)) {
            processName = baseNameFromPath(buffer, size);
            processPath = QString::fromWCharArray(buffer, int(size));
        }
        CloseHandle(hProcess);
    }

    WindowInfo info;
    info.platformName = qTitle;
    info.processName = processName;
    info.processPath = processPath;
    info.windowTitle = qTitle;
    info.className = qClass;
    info.handle = reinterpret_cast<quintptr>(hwnd);

    const QString procLower = processName.toLower();
    const QString clsLower = qClass.toLower();

    if (procLower == QStringLiteral("textinputhost.exe")
        || qTitle.contains(QStringLiteral("Windows 输入体验"))
        || qTitle.contains(QStringLiteral("Windows Input Experience"))) {
        return TRUE;
    }
    if (procLower.contains(QStringLiteral("system")))
        return TRUE;
    if (procLower.contains(QStringLiteral("chrome"))
        || procLower.contains(QStringLiteral("msedge"))
        || procLower.contains(QStringLiteral("qqbrowser"))
        || procLower.contains(QStringLiteral("360se"))
        || procLower.contains(QStringLiteral("sogouexplorer"))
        || clsLower.contains(QStringLiteral("chrome_widgetwin"))
        || clsLower.contains(QStringLiteral("cef"))) {
        info.isBrowserLike = true;
    }
    data->list->append(info);
    return TRUE;
}

QVector<WindowInfo> Win32WindowHelper::enumTopLevelWindows()
{
    QVector<WindowInfo> result;
    EnumData data;
    data.list = &result;
    data.currentPid = GetCurrentProcessId();
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
    return result;
}

static HWND toHwnd(quintptr handle) {
    return reinterpret_cast<HWND>(handle);
}

// ==================== Embed mode ====================

bool Win32WindowHelper::embedWindowIntoWidget(quintptr handle, QWidget* container)
{
    if (!handle || !container) return false;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return false;
    HWND parent = reinterpret_cast<HWND>(container->winId());
    if (!IsWindow(parent)) return false;

    // 若窗口处于最小化，SetWindowPos 会忽略传入尺寸；先最大化再添加，避免 SW_RESTORE 恢复成旧尺寸
    const bool iconic = (IsIconic(hwnd) != 0);
    // qInfo() << "[Win32WindowHelper] embedWindowIntoWidget: IsIconic(handle=" << handle << ") =" << iconic;
    if (iconic) {
        qInfo() << "[Win32WindowHelper] 嵌入前检测到窗口最小化，先最大化再添加";
        ShowWindow(hwnd, SW_MAXIMIZE);
    }

    // 仅 SetParent，不设 WS_CHILD，保留窗口原有样式以维持完整标题栏（min/max/close）
    SetParent(hwnd, parent);

    RECT rc;
    GetClientRect(parent, &rc);
    SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    return true;
}

void Win32WindowHelper::resizeEmbeddedWindow(quintptr handle, QWidget* container)
{
    if (!handle || !container) return;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return;
    HWND parent = reinterpret_cast<HWND>(container->winId());
    if (!IsWindow(parent)) return;
    RECT rc;
    GetClientRect(parent, &rc);
    SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_SHOWWINDOW);
}

void Win32WindowHelper::detachWindow(quintptr handle, const QRect& targetClientRect)
{
    if (!handle) return;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return;

    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style &= ~WS_CHILD;
    style |= WS_OVERLAPPEDWINDOW;
    SetWindowLongW(hwnd, GWL_STYLE, style);

    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    SetParent(hwnd, nullptr);

    if (targetClientRect.isValid()) {
        RECT frameRect{0, 0, targetClientRect.width(), targetClientRect.height()};
        AdjustWindowRectEx(&frameRect, DWORD(style), FALSE, DWORD(exStyle));
        const int outerX = targetClientRect.x() + frameRect.left;
        const int outerY = targetClientRect.y() + frameRect.top;
        const int outerW = frameRect.right - frameRect.left;
        const int outerH = frameRect.bottom - frameRect.top;
        SetWindowPos(hwnd, HWND_TOP, outerX, outerY, outerW, outerH,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        return;
    }

    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

// ==================== Float-follow mode ====================

void Win32WindowHelper::setupFloatFollow(quintptr handle,
                                         quintptr ownerHandle,
                                         bool useOwner,
                                         bool useToolWindow)
{
    if (!handle) return;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return;

    if (useToolWindow) {
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW;
        exStyle &= ~WS_EX_APPWINDOW;
        SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle);
    }

    // Owner relationship: owned window always stays above its owner in Z-order
    if (useOwner && ownerHandle) {
        HWND ownerHwnd = toHwnd(ownerHandle);
        if (IsWindow(ownerHwnd))
            SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(ownerHwnd));
    }

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_HIDEWINDOW);
}

void Win32WindowHelper::showWindowAt(quintptr handle,
                                     int x,
                                     int y,
                                     int w,
                                     int h,
                                     bool raiseAbove)
{
    if (!handle) return;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return;
    // const bool iconic = (IsIconic(hwnd) != 0);
    // qInfo() << "[Win32WindowHelper] showWindowAt: IsIconic(handle=" << handle << ") =" << iconic
    //         << "target=(" << x << "," << y << "," << w << "x" << h << "), raiseAbove=" << raiseAbove;
    // 与旧项目一致：始终使用 SW_RESTORE，恢复为普通窗口后 SetWindowPos 才能生效
    ShowWindow(hwnd, SW_RESTORE);
    SetWindowPos(hwnd, raiseAbove ? HWND_TOPMOST : HWND_TOP, x, y, w, h,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    if (raiseAbove) {
        SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, w, h,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
    }
}

void Win32WindowHelper::hideWindow(quintptr handle)
{
    if (!handle) return;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return;
    ShowWindow(hwnd, SW_HIDE);
}

void Win32WindowHelper::detachFloatFollow(quintptr handle,
                                          bool showWindow,
                                          bool restoreTaskbarEntry,
                                          bool clearOwner)
{
    if (!handle) return;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return;

    if (clearOwner) {
        SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, 0);
    }

    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (restoreTaskbarEntry) {
        exStyle &= ~WS_EX_TOOLWINDOW;
        exStyle |= WS_EX_APPWINDOW;
    } else {
        exStyle |= WS_EX_TOOLWINDOW;
        exStyle &= ~WS_EX_APPWINDOW;
    }
    SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle);

    UINT flags = SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED;
    if (showWindow)
        flags |= SWP_SHOWWINDOW;
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, flags);

    if (!showWindow) {
        ShowWindow(hwnd, SW_HIDE);
    }
}

void Win32WindowHelper::minimizeWindow(quintptr handle)
{
    if (!handle) return;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return;
    ShowWindow(hwnd, SW_MINIMIZE);
}

QString Win32WindowHelper::windowTitle(quintptr handle)
{
    if (!handle) return {};
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return {};
    wchar_t title[512] = {};
    const int n = GetWindowTextW(hwnd, title, 511);
    if (n <= 0) return {};
    return QString::fromWCharArray(title, n);
}

QString Win32WindowHelper::executableBaseNameForWindow(quintptr handle)
{
    if (!handle) return {};
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return {};
    DWORD pid = 0;
    if (!GetWindowThreadProcessId(hwnd, &pid) || !pid)
        return {};
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess)
        return {};
    wchar_t buffer[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size))
        name = baseNameFromPath(buffer, size);
    CloseHandle(hProcess);
    return name;
}

bool Win32WindowHelper::isWindowValid(quintptr handle)
{
    if (!handle) return false;
    return IsWindow(toHwnd(handle)) != 0;
}

bool Win32WindowHelper::isWindowVisible(quintptr handle)
{
    if (!handle) return false;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return false;
    return IsWindowVisible(hwnd) != 0;
}

bool Win32WindowHelper::isWindowHung(quintptr handle)
{
    if (!handle) return false;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return false;
    return IsHungAppWindow(hwnd) != 0;
}

bool Win32WindowHelper::isLeftMouseButtonPressed()
{
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

bool Win32WindowHelper::isPointInsideWindow(quintptr handle, const QPoint& globalPos)
{
    if (!handle) return false;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return false;

    POINT point{globalPos.x(), globalPos.y()};
    HWND hitHwnd = WindowFromPoint(point);
    if (!hitHwnd) return false;
    return hitHwnd == hwnd || IsChild(hwnd, hitHwnd);
}

QRect Win32WindowHelper::windowRect(quintptr handle)
{
    if (!handle) return {};
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return {};

    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return {};
    return QRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
}

QRect Win32WindowHelper::mapOverlaySelectionToTargetWindowRelative(quintptr overlayHwnd,
                                                                    quintptr targetHwnd,
                                                                    const QRect& selectionInOverlayClientLogical,
                                                                    qreal devicePixelRatio,
                                                                    const QSize& overlayLogicalClientSize)
{
    if (!overlayHwnd || !targetHwnd)
        return {};
    HWND hOver = reinterpret_cast<HWND>(static_cast<WId>(overlayHwnd));
    HWND hTgt = reinterpret_cast<HWND>(static_cast<WId>(targetHwnd));
    if (!IsWindow(hOver) || !IsWindow(hTgt))
        return {};

    RECT rcClient{};
    if (!GetClientRect(hOver, &rcClient))
        return {};
    const int physW = rcClient.right - rcClient.left;
    const int physH = rcClient.bottom - rcClient.top;
    if (physW <= 0 || physH <= 0)
        return {};

    double sx = 1.0;
    double sy = 1.0;
    if (overlayLogicalClientSize.isValid()
        && overlayLogicalClientSize.width() > 0
        && overlayLogicalClientSize.height() > 0) {
        sx = double(physW) / double(overlayLogicalClientSize.width());
        sy = double(physH) / double(overlayLogicalClientSize.height());
    } else {
        const qreal dpr = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
        sx = sy = dpr;
    }

    const QRect sel = selectionInOverlayClientLogical.normalized();
    POINT p0{LONG(qRound(sel.left() * sx)), LONG(qRound(sel.top() * sy))};
    POINT p1{LONG(qRound(sel.right() * sx)), LONG(qRound(sel.bottom() * sy))};
    if (!ClientToScreen(hOver, &p0) || !ClientToScreen(hOver, &p1))
        return {};

    RECT wr{};
    if (!GetWindowRect(hTgt, &wr))
        return {};

    const int sx1 = (std::min)(p0.x, p1.x);
    const int sy1 = (std::min)(p0.y, p1.y);
    const int sx2 = (std::max)(p0.x, p1.x);
    const int sy2 = (std::max)(p0.y, p1.y);
    const int x1 = sx1 - int(wr.left);
    const int y1 = sy1 - int(wr.top);
    const int x2 = sx2 - int(wr.left);
    const int y2 = sy2 - int(wr.top);
    if (x2 <= x1 || y2 <= y1)
        return {};
    return QRect(x1, y1, x2 - x1, y2 - y1);
}

QRect Win32WindowHelper::windowRectForClientRect(quintptr handle, const QRect& targetClientRect)
{
    if (!handle || !targetClientRect.isValid()) return {};
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return {};

    DWORD style = DWORD(GetWindowLongW(hwnd, GWL_STYLE));
    DWORD exStyle = DWORD(GetWindowLongW(hwnd, GWL_EXSTYLE));
    const BOOL hasMenu = GetMenu(hwnd) != nullptr;
    RECT outerRect{0, 0, targetClientRect.width(), targetClientRect.height()};
    const UINT dpi = queryWindowDpi(hwnd);
    if (!adjustWindowRectForCurrentDpi(&outerRect, style, hasMenu, exStyle, dpi)) {
        return targetClientRect;
    }

    return QRect(targetClientRect.x() + outerRect.left,
                 targetClientRect.y() + outerRect.top,
                 outerRect.right - outerRect.left,
                 outerRect.bottom - outerRect.top);
}

unsigned int Win32WindowHelper::windowDpi(quintptr handle)
{
    if (!handle) return 96;
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) return 96;
    return queryWindowDpi(hwnd);
}

QIcon Win32WindowHelper::windowIcon(const WindowInfo& info)
{
    HWND hwnd = toHwnd(info.handle);
    if (hwnd && IsWindow(hwnd)) {
        if (HICON hIcon = tryGetWindowHIcon(hwnd)) {
            const QIcon icon = iconFromHandle(hIcon);
            if (!icon.isNull())
                return icon;
        }
    }

    if (!info.processPath.isEmpty()) {
        SHFILEINFOW fileInfo{};
        if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(info.processPath.utf16()), FILE_ATTRIBUTE_NORMAL,
                           &fileInfo, sizeof(fileInfo),
                           SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | SHGFI_LARGEICON)) {
            const QIcon icon = iconFromHandle(fileInfo.hIcon);
            if (fileInfo.hIcon)
                DestroyIcon(fileInfo.hIcon);
            if (!icon.isNull())
                return icon;
        }
    }

    return {};
}

#ifdef Q_OS_WIN
QString Win32WindowHelper::resolveShortcutTarget(const QString& lnkAbsolutePath)
{
    if (!lnkAbsolutePath.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive))
        return {};
    const QFileInfo fi(lnkAbsolutePath);
    if (!fi.exists() || !fi.isFile())
        return {};

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needCoUninit = (hr == S_OK || hr == S_FALSE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return {};

    QString out;
    IShellLinkW* psl = nullptr;
    IPersistFile* ppf = nullptr;
    HRESULT hr2 = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                   reinterpret_cast<void**>(&psl));
    if (SUCCEEDED(hr2)) {
        hr2 = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
        if (SUCCEEDED(hr2)) {
            hr2 = ppf->Load(reinterpret_cast<LPCWSTR>(fi.absoluteFilePath().utf16()), STGM_READ);
            if (SUCCEEDED(hr2)) {
                wchar_t buf[MAX_PATH] = {};
                WIN32_FIND_DATAW fd{};
                hr2 = psl->GetPath(buf, MAX_PATH, &fd, SLGP_RAWPATH);
                if (SUCCEEDED(hr2))
                    out = QString::fromWCharArray(buf);
            }
            ppf->Release();
        }
        psl->Release();
    }
    if (needCoUninit)
        CoUninitialize();
    return out;
}
#endif

void Win32WindowHelper::applyNativeTopMost(QWidget* window, bool topMost)
{
    if (!window)
        return;
    const WId wid = window->winId();
    if (!wid)
        return;
    auto* hwnd = reinterpret_cast<HWND>(wid);
    if (!IsWindow(hwnd))
        return;
    SetWindowPos(hwnd, topMost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
