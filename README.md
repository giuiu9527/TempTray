# TempTray

在 Windows 11 任务栏、系统托盘左边，常驻显示 **CPU / GPU 温度** 的极简小组件。

A tiny Windows 11 widget that shows CPU / GPU temperature on the taskbar, just left of the system tray.

```
CPU:  73°C
GPU:  45°C
```

- 两行精简显示，背景透明，字体与颜色跟随系统深浅色主题
- 真正挂在任务栏上（任务栏子窗口），不是浮在最上层的窗口
- 左键按住左右拖动可微调与托盘的间距（自动保存），右键 → 退出
- 资源管理器重启后自动重新挂载
- 单文件源码，只用 Windows 自带的 C# 编译器，不需要 Visual Studio

## 参考与致谢 / References

本项目的"嵌入任务栏"思路来自 **[TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)**（作者 zhongyang219，Anti 996 License），具体参考了：

- `TrafficMonitor/Win11TaskbarDlg.cpp`：用 `FindWindowEx` 找到 `TrayNotifyWnd` / `Start`，窗口横坐标 = 通知区左边缘 − 窗口宽度 + 2，纵向对着"开始"按钮居中
- `TrafficMonitor/TaskBarDlg.cpp`：`SetParent` 把窗口设为 `Shell_TrayWnd` 的子窗口、分层窗口实现背景透明
- 显示版式（左侧标签、右对齐数值、两行）参考了 TrafficMonitor 任务栏窗口的默认样式

温度数据来自 **[LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)** 的 `LibreHardwareMonitorLib`（MPL-2.0），TrafficMonitor 的硬件监控插件用的也是它。

This project is **not** affiliated with TrafficMonitor. It re-implements TrafficMonitor's Windows 11 taskbar-embedding technique in a minimal standalone tool. Sensors are read through LibreHardwareMonitorLib.

## 构建 / Build

1. 取得 `LibreHardwareMonitorLib.dll`（.NET Framework 4.7.2 版本，已测试 0.9.4），放到本目录。来源任选其一：
   - [LibreHardwareMonitor Releases](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases)
   - NuGet 包 `LibreHardwareMonitorLib`
   - TrafficMonitor 硬件监控插件压缩包里自带的那份
2. 运行：

```powershell
.\build.ps1
```

3. 运行 `TempTray.exe`。读取温度需要加载内核驱动，所以程序会请求**管理员权限**（UAC 弹窗）。

## 已知限制 / Limitations

- 仅在 Windows 11（含 24H2/25H2 的 XAML 任务栏）上测试过；需要 Win8+ 才支持分层子窗口
- 只显示 CPU（AMD 取 Tctl/Tdie，Intel 取 Package）和独立显卡（NVIDIA 优先，AMD 核显仅在没有 NVIDIA 时使用）
- 不同软件读取同一颗 CPU 的温度可能相差几度，请把它当作趋势参考
- 目前不带开机自启，需要的话可用任务计划程序（以最高权限运行）

## 许可证 / License

Anti 996 License Version 1.0 (Draft)，与 TrafficMonitor 相同，见 [LICENSE](LICENSE)。
