#include "win32windowhelper.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
struct EnumData {
    QVector<WindowInfo>* list = nullptr;
    DWORD currentPid = 0;
};
}

QString baseNameFromPath(const wchar_t* path, DWORD length) {
    if (!path || length == 0) {
        return {};
    }
    const wchar_t* end = path + length;
    const wchar_t* p = end - 1;
    while(p >= path && *p != L'\\' && *p != L'/') {
        --p;
    }
    if (p >= path) {
        ++p;
    } else {
        p = path;
    }
    return QString::fromWCharArray(p, int(end - p));
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumData*>(lParam);
    if (!data || !data->list) {
        return TRUE;
    }
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return TRUE;
    }
    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }
    wchar_t title[512] = {0};
    GetWindowTextW(hwnd, title, 511);
    if (title[0] == L'\0') {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->currentPid) {
        return TRUE;
    }
    wchar_t className[256] = {0};
    GetClassNameW(hwnd, className, 255);
    QString qTitle = QString::fromWCharArray(title);
    QString qClass = QString::fromWCharArray(className);
    QString processName;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        wchar_t buffer[MAX_PATH] = {0};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size)) {
            processName = baseNameFromPath(buffer, size);
        }
        CloseHandle(hProcess);
    }
    WindowInfo info;
    info.platformName = qTitle;
    info.processName = processName;
    info.windowTitle = qTitle;
    info.className = qClass;
    info.handle = reinterpret_cast<quintptr>(hwnd);

    const QString procLower = processName.toLower();
    const QString clsLower = qClass.toLower();
    if (procLower.contains(QStringLiteral("chrome"))
        || procLower.contains(QStringLiteral("msedge"))
        || procLower.contains(QStringLiteral("qqbrowser"))
        || procLower.contains(QStringLiteral("360se"))
        || procLower.contains(QStringLiteral("sogouexplorer"))
        || clsLower.contains(QStringLiteral("chrome_widgetwin"))
        || clsLower.contains(QStringLiteral("cef"))
        ) {
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

HWND toHwnd(quintptr handle) {
    return reinterpret_cast<HWND>(handle);
}

bool Win32WindowHelper::embedWindowIntoWidget(quintptr handle, QWidget *container)
{
    if (!handle || !container) {
        return false;
    }
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) {
        return false;
    }
    HWND parent = reinterpret_cast<HWND>(container->winId());
    if (!IsWindow(parent)) {
        return false;
    }

    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    style |= WS_CHILD;
    SetWindowLongW(hwnd, GWL_STYLE, style);
    SetParent(hwnd, parent);

    RECT rc;
    GetClientRect(parent, &rc);
    SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_SHOWWINDOW);

    return true;
}

void Win32WindowHelper::detachWindow(quintptr handle)
{
    if (!handle) return;

    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) {
        return;
    }
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style &= ~WS_CHILD;
    style |= WS_OVERLAPPEDWINDOW;
    SetWindowLongW(hwnd, GWL_STYLE, style);
    SetParent(hwnd, nullptr);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void Win32WindowHelper::resizeEmbeddedWindow(quintptr handle, QWidget *container)
{
    if (!handle || !container) {
        return;
    }
    HWND hwnd = toHwnd(handle);
    if (!IsWindow(hwnd)) {
        return;
    }
    HWND parent = reinterpret_cast<HWND>(container->winId());
    if (!IsWindow(parent)) {
        return;
    }
    RECT rc;
    GetClientRect(parent, &rc);
    SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_SHOWWINDOW);
}
