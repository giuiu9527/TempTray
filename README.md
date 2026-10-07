# TempTray

在 Windows 11 任务栏、系统托盘左边，常驻显示 **CPU / GPU 温度** 的极简小组件。

A tiny Windows 11 widget that shows CPU / GPU temperature on the taskbar, just left of the system tray.

```
CPU:  59°C
GPU:  47°C
```

- 两行精简显示，背景透明，字体与颜色跟随系统深浅色主题
- 真正挂在任务栏上（任务栏子窗口），不是浮在最上层的窗口
- 左键按住左右拖动可微调与托盘的间距（自动保存），右键 → 退出
- 资源管理器重启后自动重新挂载

## 两个版本 / Two implementations

| | `native/`（C++，推荐） | `dotnet/`（C#） |
|---|---|---|
| CPU 温度来源 | AMD Ryzen Master SDK | LibreHardwareMonitorLib |
| GPU 温度来源 | NVIDIA NvAPI（`nvapi64.dll`） | LibreHardwareMonitorLib |
| 支持的硬件 | **仅 AMD CPU + NVIDIA 显卡** | AMD / Intel CPU，NVIDIA / AMD 显卡 |
| 内存（实测，Ryzen 9 9950X + RTX 5070 Ti） | 工作集 ≈ 20 MB，私有 ≈ 5 MB | 工作集 ≈ 76 MB，私有 ≈ 46 MB |
| 编译器 | MSVC（VS 2022 生成工具） | Windows 自带的 `csc.exe`，不用装任何东西 |
| 额外依赖 | 本机已装 AMD Ryzen Master SDK | `LibreHardwareMonitorLib.dll` |

注意两者的 CPU 数值口径不同：`native` 读到的是 Ryzen Master 显示的核心温度（对应 LibreHardwareMonitor 的 `CCDs Max (Tdie)`），`dotnet` 读的是 `Core (Tctl/Tdie)`，后者通常更高几度。

## 参考与致谢 / References

本项目的"嵌入任务栏"思路来自 **[TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)**（作者 zhongyang219，Anti 996 License），具体参考了：

- `TrafficMonitor/Win11TaskbarDlg.cpp`：用 `FindWindowEx` 找到 `TrayNotifyWnd` / `Start`，窗口横坐标 = 通知区左边缘 − 窗口宽度 + 2，纵向对着"开始"按钮居中
- `TrafficMonitor/TaskBarDlg.cpp`：把窗口设为 `Shell_TrayWnd` 的子窗口、分层窗口实现背景透明
- 显示版式（左侧标签、右对齐数值、两行）参考了 TrafficMonitor 任务栏窗口的默认样式

`dotnet/` 版的温度数据来自 **[LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)** 的 `LibreHardwareMonitorLib`（MPL-2.0），TrafficMonitor 的硬件监控插件用的也是它。

This project is **not** affiliated with TrafficMonitor, AMD or NVIDIA. It re-implements TrafficMonitor's Windows 11 taskbar-embedding technique in a minimal standalone tool.

## 构建 / Build

### native（C++）

1. 安装 [Visual Studio 2022 生成工具](https://visualstudio.microsoft.com/downloads/)，勾选"使用 C++ 的桌面开发"。
2. 安装 AMD Ryzen Master SDK（含驱动）。**它的许可证不允许再分发，所以本仓库不包含它的头文件和 DLL**；编译时只引用头文件，运行时从 `C:\Program Files\AMD\RyzenMasterSDK\bin` 动态加载 `Platform.dll`。SDK 在别处时设置环境变量 `RYZEN_MASTER_SDK_DIR`。
3. 运行：

```bat
native\build.bat
```

4. 运行 `native\TempTrayNative.exe`。AMD 驱动需要**管理员权限**（UAC 弹窗）。没装 SDK 或不是 AMD CPU 时，CPU 一行显示 `--`；没有 NVIDIA 显卡时，GPU 一行显示 `--`。

### dotnet（C#）

1. 取得 `LibreHardwareMonitorLib.dll`（.NET Framework 4.7.2 版本，已测试 0.9.4），放到 `dotnet/` 目录。来源任选其一：[LibreHardwareMonitor Releases](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases)、NuGet 包 `LibreHardwareMonitorLib`。
2. 运行 `dotnet\build.ps1`，然后运行 `TempTray.exe`（同样需要管理员权限）。

## 已知限制 / Limitations

- 仅在 Windows 11（含 24H2/25H2 的 XAML 任务栏）上测试过
- `native` 只支持 AMD CPU 与 NVIDIA 显卡；其他硬件请用 `dotnet` 版
- 只显示 CPU 与第一块 NVIDIA 显卡的温度
- 目前不带开机自启，需要的话可用任务计划程序（以最高权限运行）

## 许可证 / License

Anti 996 License Version 1.0 (Draft)，与 TrafficMonitor 相同，见 [LICENSE](LICENSE)。
