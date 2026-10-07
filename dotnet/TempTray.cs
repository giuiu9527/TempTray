// TempTray - compact CPU/GPU temperature shown on the Windows 11 taskbar, left of the tray.
// Window embedding / placement / transparency approach follows TrafficMonitor
// (https://github.com/zhongyang219/TrafficMonitor, Win11TaskbarDlg.cpp & TaskBarDlg.cpp, Anti 996 License):
//   - SetParent() into Shell_TrayWnd so the widget is a real child of the taskbar
//   - x = TrayNotifyWnd.left - width + 2, vertically centred on the Start button
//   - per-pixel-alpha layered window, so text blends with the taskbar background
// Sensor data comes from LibreHardwareMonitorLib (same library TrafficMonitor uses).
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using LibreHardwareMonitor.Hardware;
using Microsoft.Win32;

class TaskbarWidget : NativeWindow
{
    [DllImport("user32")] static extern IntPtr FindWindow(string c, string n);
    [DllImport("user32")] static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string n);
    [DllImport("user32")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
    [DllImport("user32")] static extern bool UpdateLayeredWindow(IntPtr h, IntPtr hdcDst, IntPtr pptDst, ref SIZE psize,
        IntPtr hdcSrc, ref POINT pptSrc, int crKey, ref BLENDFUNCTION pblend, int flags);
    [DllImport("user32")] static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32")] static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("user32")] static extern uint GetDpiForSystem();
    [DllImport("user32")] static extern IntPtr SetCapture(IntPtr h);
    [DllImport("user32")] static extern bool ReleaseCapture();
    [DllImport("gdi32")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32")] static extern bool DeleteDC(IntPtr dc);
    [DllImport("gdi32")] static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
    [DllImport("gdi32")] static extern bool DeleteObject(IntPtr o);
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int l, t, r, b; }
    [StructLayout(LayoutKind.Sequential)] struct POINT { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] struct SIZE { public int cx, cy; }
    [StructLayout(LayoutKind.Sequential)] struct BLENDFUNCTION { public byte Op, Flags, Alpha, Format; }

    const int WS_CHILD = 0x40000000, WS_VISIBLE = 0x10000000, SS_NOTIFY = 0x100;
    const int WS_EX_LAYERED = 0x80000, WS_EX_NOACTIVATE = 0x08000000;
    const uint SWP_NOSIZE = 1, SWP_NOMOVE = 2, SWP_NOACTIVATE = 0x10;
    const int WM_LBUTTONDOWN = 0x201, WM_LBUTTONUP = 0x202, WM_MOUSEMOVE = 0x200, WM_RBUTTONUP = 0x205;

    readonly string iniPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "TempTray.ini");
    readonly ContextMenuStrip menu = new ContextMenuStrip();
    readonly Timer timer = new Timer { Interval = 1000 };
    Computer pc;
    IntPtr parent;
    int offsetX;                       // extra gap to the left of the tray, adjustable by dragging
    int dragFromX, dragFromOffset; bool dragging;
    bool light;
    float cpu = float.NaN, gpu = float.NaN;
    string lastKey = "";
    int w, h;

    public TaskbarWidget()
    {
        try { offsetX = int.Parse(File.ReadAllText(iniPath).Trim()); } catch { offsetX = 0; }
        menu.Items.Add("退出", null, (s, e) => Application.Exit());
        pc = new Computer { IsCpuEnabled = true, IsGpuEnabled = true };
        try { pc.Open(); } catch { }
        LoadTheme();
        SystemEvents.UserPreferenceChanged += (s, e) => { LoadTheme(); lastKey = ""; };
        timer.Tick += (s, e) => Tick();
        timer.Start();
        Tick();
    }

    void LoadTheme()
    {
        try
        {
            object v = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "SystemUsesLightTheme", 0);
            light = v != null && (int)v != 0;
        }
        catch { light = false; }
    }

    void Tick()
    {
        ReadSensors();
        IntPtr tray = FindWindow("Shell_TrayWnd", null);
        if (tray == IntPtr.Zero) return;
        if (tray != parent || Handle == IntPtr.Zero)      // first run, or explorer restarted
        {
            if (Handle != IntPtr.Zero) DestroyHandle();
            parent = tray;
            var cp = new CreateParams { ClassName = "Static", Style = WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                                        ExStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE, Parent = tray, X = 0, Y = 0, Width = 10, Height = 10 };
            CreateHandle(cp);
            lastKey = "";
        }
        Render();
        // the Win11 XAML layer (DesktopWindowContentBridge) covers the whole taskbar; stay above it
        SetWindowPos(Handle, IntPtr.Zero, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void ReadSensors()
    {
        float c = float.NaN, g = float.NaN, cMax = float.NaN;
        try
        {
            foreach (var hw in pc.Hardware)
            {
                hw.Update();
                foreach (var s in hw.Sensors)
                {
                    if (s.SensorType != SensorType.Temperature || !s.Value.HasValue) continue;
                    float v = s.Value.Value;
                    if (hw.HardwareType == HardwareType.Cpu)
                    {
                        if (s.Name.IndexOf("Tctl", StringComparison.OrdinalIgnoreCase) >= 0 ||
                            s.Name.IndexOf("Package", StringComparison.OrdinalIgnoreCase) >= 0) c = v;
                        else if (float.IsNaN(cMax) || v > cMax) cMax = v;
                    }
                    else if ((hw.HardwareType == HardwareType.GpuNvidia || hw.HardwareType == HardwareType.GpuAmd) &&
                             s.Name.IndexOf("Core", StringComparison.OrdinalIgnoreCase) >= 0 &&
                             (hw.HardwareType == HardwareType.GpuNvidia || float.IsNaN(g)))
                        g = v;      // prefer the discrete NVIDIA card over an iGPU
                }
            }
        }
        catch { }
        cpu = float.IsNaN(c) ? cMax : c;
        gpu = g;
    }

    static string Val(float v) { return (float.IsNaN(v) ? "--" : Math.Round(v).ToString()) + "°C"; }

    void Render()
    {
        string key = Val(cpu) + "|" + Val(gpu) + "|" + light + "|" + offsetX;
        RECT tr; if (!GetWindowRect(parent, out tr)) return;
        RECT nr; IntPtr notify = FindWindowEx(parent, IntPtr.Zero, "TrayNotifyWnd", null);
        int notifyLeft = tr.r - tr.l - 88;                                   // fallback: system clock area
        if (notify != IntPtr.Zero && GetWindowRect(notify, out nr) && nr.l > tr.l) notifyLeft = nr.l - tr.l;
        int trayH = tr.b - tr.t;
        int startH = trayH;
        RECT sr; IntPtr start = FindWindowEx(parent, IntPtr.Zero, "Start", null);
        if (start != IntPtr.Zero && GetWindowRect(start, out sr) && sr.b > sr.t) startH = sr.b - sr.t;
        key += "|" + notifyLeft + "|" + trayH;
        if (key == lastKey) return;
        lastKey = key;

        float dpi = 96; try { dpi = GetDpiForSystem(); } catch { }
        float px = 9f * dpi / 72f;
        using (var font = new Font("Microsoft YaHei UI", px, FontStyle.Regular, GraphicsUnit.Pixel))
        {
            Size labelSz, valSz, rowSz;
            using (var tmp = new Bitmap(1, 1))
            using (var g = Graphics.FromImage(tmp))
            {
                g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                labelSz = Size.Ceiling(g.MeasureString("CPU:", font, 1000, StringFormat.GenericTypographic));
                valSz = Size.Ceiling(g.MeasureString("100°C", font, 1000, StringFormat.GenericTypographic));
                rowSz = Size.Ceiling(g.MeasureString("Ag", font, 1000, StringFormat.GenericTypographic));
            }
            int gap = (int)(4 * dpi / 96), pad = (int)(2 * dpi / 96);
            w = pad + labelSz.Width + gap + valSz.Width + pad;
            h = Math.Min(trayH, rowSz.Height * 2 + pad);
            int rowH = h / 2;

            using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppPArgb))
            {
                using (var g = Graphics.FromImage(bmp))
                {
                    g.Clear(Color.FromArgb(1, 0, 0, 0));     // alpha 1: invisible but keeps the whole area clickable
                    g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                    using (var br = new SolidBrush(light ? Color.Black : Color.White))
                    {
                        var left = new StringFormat(StringFormat.GenericTypographic) { LineAlignment = StringAlignment.Center };
                        var right = new StringFormat(StringFormat.GenericTypographic) { Alignment = StringAlignment.Far, LineAlignment = StringAlignment.Center };
                        g.DrawString("CPU:", font, br, new RectangleF(pad, 0, labelSz.Width + 2, rowH), left);
                        g.DrawString("GPU:", font, br, new RectangleF(pad, rowH, labelSz.Width + 2, rowH), left);
                        float vx = pad + labelSz.Width + gap;
                        g.DrawString(Val(cpu), font, br, new RectangleF(vx, 0, valSz.Width, rowH), right);
                        g.DrawString(Val(gpu), font, br, new RectangleF(vx, rowH, valSz.Width, rowH), right);
                    }
                }
                int x = notifyLeft - w + 2 - offsetX;
                int y = (startH - h) / 2 + (trayH - startH);
                SetWindowPos(Handle, IntPtr.Zero, x, y, w, h, SWP_NOACTIVATE);
                Blit(bmp);
            }
        }
    }

    void Blit(Bitmap bmp)
    {
        IntPtr screenDc = GetDC(IntPtr.Zero), memDc = CreateCompatibleDC(screenDc);
        IntPtr hBmp = bmp.GetHbitmap(Color.FromArgb(0)), old = SelectObject(memDc, hBmp);
        var size = new SIZE { cx = bmp.Width, cy = bmp.Height };
        var src = new POINT();
        var blend = new BLENDFUNCTION { Op = 0, Flags = 0, Alpha = 255, Format = 1 };   // AC_SRC_OVER, AC_SRC_ALPHA
        UpdateLayeredWindow(Handle, screenDc, IntPtr.Zero, ref size, memDc, ref src, 0, ref blend, 2);
        SelectObject(memDc, old); DeleteObject(hBmp); DeleteDC(memDc); ReleaseDC(IntPtr.Zero, screenDc);
    }

    protected override void WndProc(ref Message m)
    {
        switch (m.Msg)
        {
            case WM_LBUTTONDOWN: dragging = true; dragFromX = Cursor.Position.X; dragFromOffset = offsetX; SetCapture(Handle); break;
            case WM_MOUSEMOVE:
                if (dragging) { offsetX = Math.Max(0, dragFromOffset - (Cursor.Position.X - dragFromX)); Render(); }
                break;
            case WM_LBUTTONUP:
                if (dragging) { dragging = false; ReleaseCapture(); try { File.WriteAllText(iniPath, offsetX.ToString()); } catch { } }
                break;
            case WM_RBUTTONUP: menu.Show(Cursor.Position); break;
        }
        base.WndProc(ref m);
    }

    public void Shutdown()
    {
        timer.Stop();
        try { pc.Close(); } catch { }
        if (Handle != IntPtr.Zero) DestroyHandle();
    }
}

static class Program
{
    static void Log(Exception ex)
    {
        try { File.AppendAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "TempTray.log"), DateTime.Now + " " + ex + Environment.NewLine); } catch { }
    }

    [STAThread]
    static void Main()
    {
        bool created;
        using (var mtx = new System.Threading.Mutex(true, "TempTray_single_instance", out created))
        {
            if (!created) return;
            Application.EnableVisualStyles();
            Application.ThreadException += (s, e) => Log(e.Exception);
            AppDomain.CurrentDomain.UnhandledException += (s, e) => Log(e.ExceptionObject as Exception);
            var widget = new TaskbarWidget();
            Application.Run();
            widget.Shutdown();
        }
    }
}
