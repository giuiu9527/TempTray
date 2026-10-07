// TempTrayNative - compact CPU/GPU temperature on the Windows 11 taskbar, left of the tray.
// Native Win32 rewrite of TempTray (no .NET, no LibreHardwareMonitor) to keep memory use small.
//
// Taskbar embedding / placement follows TrafficMonitor (Anti 996 License):
//   https://github.com/zhongyang219/TrafficMonitor  (Win11TaskbarDlg.cpp, TaskBarDlg.cpp)
//   - SetParent into Shell_TrayWnd (here: created directly as its child)
//   - x = TrayNotifyWnd.left - width + 2, vertically centred on the Start button
//   - per-pixel-alpha layered window
// Sensors:
//   CPU: AMD Ryzen Master SDK (PMTable.dTemperature), loaded dynamically; the SDK is NOT redistributed.
//   GPU: NVIDIA NvAPI (nvapi64.dll, part of the NVIDIA driver).
#include <windows.h>
#include <stdio.h>
#include <string>
#include <math.h>
#include "IPlatform.h"
#include "IDeviceManager.h"
#include "ICPUEx.h"

static const wchar_t* kClass = L"TempTrayNativeWnd";
static const UINT_PTR kTimerId = 1;

// ---------------------------------------------------------------- sensors
struct Sensors
{
    IPlatform* platform = nullptr;
    ICPUEx* cpu = nullptr;
    PMTable pm;
    // NvAPI
    typedef int (*Enum_t)(void**, unsigned*);
    typedef int (*Thermal_t)(void*, int, void*);
    Thermal_t nvThermal = nullptr;
    void* nvGpu = nullptr;

    void Init()
    {
        InitAmd();
        InitNv();
    }

    void InitAmd()
    {
        std::wstring dir = L"C:\\Program Files\\AMD\\RyzenMasterSDK\\bin";
        wchar_t env[MAX_PATH];
        if (GetEnvironmentVariableW(L"RYZEN_MASTER_SDK_DIR", env, MAX_PATH)) dir = std::wstring(env) + L"\\bin";
        SetDllDirectoryW(dir.c_str());
        HMODULE m = LoadLibraryW((dir + L"\\Platform.dll").c_str());
        if (!m) return;
        typedef IPlatform& (*GetPlatform_t)(void);
        auto getPlatform = (GetPlatform_t)GetProcAddress(m, "GetPlatform");
        if (!getPlatform) return;
        IPlatform& p = getPlatform();
        if (!p.Init()) return;
        platform = &p;
        cpu = (ICPUEx*)p.GetIDeviceManager().GetDevice(dtCPU, 0);
    }

    void InitNv()
    {
        HMODULE m = LoadLibraryW(L"nvapi64.dll");
        if (!m) return;
        typedef void* (*QI)(unsigned);
        auto qi = (QI)GetProcAddress(m, "nvapi_QueryInterface");
        if (!qi) return;
        auto init = (int (*)())qi(0x0150E828);        // NvAPI_Initialize
        auto enumGpu = (Enum_t)qi(0xE5AC921F);        // NvAPI_EnumPhysicalGPUs
        nvThermal = (Thermal_t)qi(0xE3640A56);        // NvAPI_GPU_GetThermalSettings
        if (!init || !enumGpu || !nvThermal || init() != 0) { nvThermal = nullptr; return; }
        void* h[64] = {};
        unsigned n = 0;
        if (enumGpu(h, &n) != 0 || n == 0) { nvThermal = nullptr; return; }
        nvGpu = h[0];
    }

    float CpuTemp()
    {
        if (!cpu) return -1;
        if (cpu->GetPMTableData(pm) != 0) return -1;
        return (float)pm.dTemperature;
    }

    float GpuTemp()
    {
        if (!nvThermal || !nvGpu) return -1;
        struct { unsigned version, count; struct { int controller, defMin, defMax, cur, target; } s[3]; } ts = {};
        ts.version = sizeof(ts) | (1 << 16);
        if (nvThermal(nvGpu, 15, &ts) != 0 || ts.count == 0) return -1;
        return (float)ts.s[0].cur;
    }

    void Shutdown() { if (platform) platform->UnInit(); }
};

// ---------------------------------------------------------------- widget
static Sensors g_sensors;
static HWND g_wnd = nullptr, g_parent = nullptr;
static int g_offset = 0;               // extra gap to the left of the tray (drag to adjust)
static bool g_dragging = false;
static int g_dragFromX = 0, g_dragFromOffset = 0;
static float g_cpu = -1, g_gpu = -1;
static bool g_light = false;
static std::wstring g_lastKey;
static UINT g_taskbarCreated = 0;
static std::wstring g_ini;

static std::wstring Val(float v)
{
    wchar_t b[32];
    if (v < 0) return L"--\u00b0C";
    swprintf_s(b, L"%d\u00b0C", (int)(v + 0.5f));
    return b;
}

static bool ReadLightTheme()
{
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS) return false;
    return v != 0;
}

static void Render()
{
    if (!g_wnd) return;
    RECT tr;
    GetWindowRect(g_parent, &tr);
    int trayH = tr.bottom - tr.top;
    int notifyLeft = tr.right - tr.left - 88;                       // fallback: clock area
    RECT nr;
    HWND notify = FindWindowExW(g_parent, nullptr, L"TrayNotifyWnd", nullptr);
    if (notify && GetWindowRect(notify, &nr) && nr.left > tr.left) notifyLeft = nr.left - tr.left;
    int startH = trayH;
    RECT sr;
    HWND start = FindWindowExW(g_parent, nullptr, L"Start", nullptr);
    if (start && GetWindowRect(start, &sr) && sr.bottom > sr.top) startH = sr.bottom - sr.top;

    std::wstring cpuS = Val(g_cpu), gpuS = Val(g_gpu);
    wchar_t keyb[160];
    swprintf_s(keyb, L"%s|%s|%d|%d|%d|%d", cpuS.c_str(), gpuS.c_str(), g_light, g_offset, notifyLeft, trayH);
    if (g_lastKey == keyb) return;
    g_lastKey = keyb;

    HDC screen = GetDC(nullptr);
    int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    HDC dc = CreateCompatibleDC(screen);
    int fontPx = MulDiv(9, dpi, 72);
    HFONT font = CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(dc, font);

    SIZE labelSz, valSz, rowSz;
    GetTextExtentPoint32W(dc, L"CPU:", 4, &labelSz);
    GetTextExtentPoint32W(dc, L"100\u00b0C", 5, &valSz);
    GetTextExtentPoint32W(dc, L"Ag", 2, &rowSz);
    int gap = MulDiv(4, dpi, 96), pad = MulDiv(2, dpi, 96);
    int w = pad + labelSz.cx + gap + valSz.cx + pad;
    // same row pitch as TrafficMonitor's taskbar window: 16 logical px per row (24 px at 150%), so rows line up
    int rowH = MulDiv(16, dpi, 96);
    int h = min(trayH, rowH * 2);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                                      // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    unsigned* px = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&px, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    // draw white text on black; the grey level is the glyph coverage, which becomes the alpha channel
    RECT all = { 0, 0, w, h };
    FillRect(dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    int vx = pad + labelSz.cx + gap;
    RECT r;
    r = { pad, 0, pad + labelSz.cx + 2, rowH };       DrawTextW(dc, L"CPU:", -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    r = { pad, rowH, pad + labelSz.cx + 2, rowH * 2 }; DrawTextW(dc, L"GPU:", -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    r = { vx, 0, vx + valSz.cx, rowH };               DrawTextW(dc, cpuS.c_str(), -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    r = { vx, rowH, vx + valSz.cx, rowH * 2 };        DrawTextW(dc, gpuS.c_str(), -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    GdiFlush();

    unsigned fg = g_light ? 0 : 255;                                 // text colour channel value
    // Grey-scale AA looks thinner than ClearType; lift mid coverage so strokes read as solid as TrafficMonitor's.
    // Dark text on a light bar needs more lift than light text on a dark bar.
    static unsigned char lut[256];
    static bool lutLight = !g_light;
    if (lutLight != g_light)
    {
        lutLight = g_light;
        double gamma = g_light ? 0.75 : 0.9;
        for (int i = 0; i < 256; i++) lut[i] = (unsigned char)(255.0 * pow(i / 255.0, gamma) + 0.5);
    }
    for (int i = 0; i < w * h; i++)
    {
        // ClearType gives per-subpixel coverage in R,G,B; averaging them keeps the 3x horizontal
        // resolution without colour fringes (a layered window cannot do real ClearType)
        unsigned cov = (((px[i] >> 16) & 0xFF) + ((px[i] >> 8) & 0xFF) + (px[i] & 0xFF)) / 3;
        unsigned a = lut[cov];
        unsigned alpha = a ? a : 1;                                  // alpha 1: invisible but keeps the area clickable
        unsigned c = fg * a / 255;                                   // premultiplied colour
        px[i] = (alpha << 24) | (c << 16) | (c << 8) | c;
    }

    int x = notifyLeft - w + 2 - g_offset;
    int y = (startH - h) / 2 + (trayH - startH);
    SetWindowPos(g_wnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
    SIZE sz = { w, h };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_wnd, screen, nullptr, &sz, dc, &src, 0, &bf, ULW_ALPHA);

    SelectObject(dc, oldBmp);
    SelectObject(dc, oldFont);
    DeleteObject(bmp);
    DeleteObject(font);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp);

static void CreateWidget()
{
    g_parent = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!g_parent) return;
    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"TempTray", WS_CHILD | WS_VISIBLE,
                            0, 0, 10, 10, g_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_lastKey.clear();
}

static void Tick(HWND timerOwner)
{
    g_cpu = g_sensors.CpuTemp();
    g_gpu = g_sensors.GpuTemp();
    g_light = ReadLightTheme();
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) return;
    if (!g_wnd || tray != g_parent)                                   // first run, or explorer restarted
    {
        if (g_wnd) DestroyWindow(g_wnd);
        g_wnd = nullptr;
        CreateWidget();
    }
    Render();
    if (g_wnd) SetWindowPos(g_wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // stay above the XAML layer
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_LBUTTONDOWN:
    {
        POINT p; GetCursorPos(&p);
        g_dragging = true; g_dragFromX = p.x; g_dragFromOffset = g_offset;
        SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_dragging)
        {
            POINT p; GetCursorPos(&p);
            g_offset = max(0, g_dragFromOffset - (p.x - g_dragFromX));
            Render();
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging)
        {
            g_dragging = false; ReleaseCapture();
            wchar_t b[16]; swprintf_s(b, L"%d", g_offset);
            WritePrivateProfileStringW(L"TempTray", L"offset", b, g_ini.c_str());
        }
        return 0;
    case WM_RBUTTONUP:
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"\u9000\u51fa");
        POINT p; GetCursorPos(&p);
        SetForegroundWindow(h);
        if (TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr) == 1) PostQuitMessage(0);
        DestroyMenu(menu);
        return 0;
    }
    }
    if (m == g_taskbarCreated && g_taskbarCreated) { g_parent = nullptr; return 0; }   // next tick re-attaches
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"TempTrayNative_single_instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_ini = exe;
    g_ini.replace(g_ini.find_last_of(L'.'), std::wstring::npos, L".ini");
    g_offset = GetPrivateProfileIntW(L"TempTray", L"offset", 0, g_ini.c_str());
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512) /* IDC_ARROW */);
    RegisterClassW(&wc);

    g_sensors.Init();
    // message-only window to own the timer, so the taskbar child can be recreated freely
    HWND timerWnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    SetTimer(timerWnd, kTimerId, 1000, nullptr);
    Tick(timerWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_TIMER && msg.hwnd == timerWnd) { Tick(timerWnd); continue; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    KillTimer(timerWnd, kTimerId);
    if (g_wnd) DestroyWindow(g_wnd);
    g_sensors.Shutdown();
    ReleaseMutex(mtx);
    return 0;
}
