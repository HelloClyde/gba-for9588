![GBA for BBK 9588](assets/readme-header.webp)

[![Build](https://github.com/HelloClyde/gba-for9588/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/HelloClyde/gba-for9588/actions/workflows/build.yml)
[![Latest Release](https://img.shields.io/github/v/release/HelloClyde/gba-for9588)](https://github.com/HelloClyde/gba-for9588/releases/latest)
[![License: GPL v2](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE)

基于 [libretro/gpSP](https://github.com/libretro/gpsp) 的 BBK 9588 原生 GBA 模拟器。
项目使用 MIPS 动态重编译器，通过 [BBK 9588 BDA SDK](sdk/) 构建为独立 `GBA.bda`。

> 当前仍是测试版本。模拟器和真机均已完成 ROM 加载、画面、声音、触摸控制、换 ROM 与
> 存档恢复验证；触摸延迟和长时间真机稳定性仍在继续测试。

## 下载

从 [GitHub Releases](https://github.com/HelloClyde/gba-for9588/releases/latest) 下载最新版本，
或直接下载 [GBA.bda](https://github.com/HelloClyde/gba-for9588/releases/latest/download/GBA.bda)。
发布页同时提供 `GBA.bda.sha256` 校验文件。

## 功能

- gpSP MIPS32 dynamic recompiler，以及便于诊断的解释器目标
- GBA 59.7275 Hz 逻辑时序，默认约 30 FPS 画面输出
- 22050 Hz 单声道 PCM，声音默认开启且可在触摸控制栏切换
- 从 `A:\GAMEBOY\` 选择 `.gba` ROM
- SRAM、Flash、EEPROM 以及带 CRC 的 A/B 双槽存档
- 触摸屏 GBA 按键、实体方向键、帮助页和运行中切换 ROM
- 适配 16 MiB/32 MiB ROM 的 32 KiB 分页读取

## 性能优化

移植版保持 GBA 核心 `59.7275 Hz` 的逻辑时序，不把游戏逻辑降到 30 FPS；约 30 FPS 仅指
屏幕提交频率。当前主要采用以下优化：

- **MIPS32 动态重编译**：启用 gpSP MIPS dynarec，使用可执行 JIT 缓冲区和 32 字节粒度的
  D-cache 写回/I-cache 失效，避免逐条解释 ARM 指令；缩小 translation cache 与 ROM branch
  hash，以适应 9588 的可用内存。
- **按需读取 ROM**：不把 16 MiB/32 MiB ROM 整体装入内存。构建配置使用 2 MiB ROM cache，
  miss 时按 32 KiB 页面读取并替换，兼顾内存占用与文件 I/O 次数。
- **逻辑帧与显示帧解耦**：固定 `frameskip=1`，核心仍执行约 59.7 帧/秒，只提交约 30 帧/秒；
  画面以原生 `240x160 RGB565` 路径输出，不做缩放、色彩校正或帧混合。触摸控制层仅在按键
  或声音状态变化时重绘。
- **分块 PCM 流水线**：将 gpSP 的 65536 Hz 立体声降采样为 22050 Hz 单声道，使用 4096
  sample 环形缓冲区和 512 sample 硬件写块。事件派发前后优先补充 PCM，并对背压等待设置
  上限，避免 GUI 处理长期饿死音频。
- **受控事件调度**：使用 20 ms Frame window timer 唤醒固件事件循环，每个逻辑帧最多派发
  一条消息；收到触摸坐标后立即结束本轮派发，使输入状态尽快进入下一帧，同时避免 timer
  消息生产速度超过消费速度。
- **移出热循环的 I/O**：普通性能日志每 600 个逻辑帧记录一次；运行中不周期扫描或写入
  128 KiB 存档，只在换 ROM 和正常退出时 checkpoint，并通过 CRC 跳过未变化的存档写入。
- **面向小内存的构建**：使用 `-Os`、独立函数/数据 section、链接期 `--gc-sections` 和精简
  translation cache，减少 BDA、JIT 元数据和常驻内存占用。

在 9588 模拟器压力测试中，当前 30 FPS 调度方案的稳定区间约为 `59.55-59.85` 逻辑 FPS、
`29.77-29.92` 显示 FPS 和 `21985-22095 Hz` 音频生成率。该数据用于回归比较，真机表现仍会
受 ROM、固件版本和触摸事件负载影响。

## 构建

环境要求：Windows、PowerShell、Git 和 Python 3.10 或更高版本。首次构建会下载固定版本的
gpSP、公开测试 ROM 源以及经过 SHA-256 校验的 MIPS 交叉工具链。

```powershell
git submodule update --init sdk
.\tools\build.ps1 -Target gpsp_app
```

输出文件：`build\gpsp_app\GBA.bda`。

只初始化依赖而不下载工具链：

```powershell
.\tools\bootstrap.ps1 -SkipToolchain
```

## 安装与使用

1. 将 `GBA.bda` 放入设备应用程序目录。
2. 将合法持有的 `.gba` 文件放入 `A:\GAMEBOY\`。
3. 启动 `GBA`，从系统文件选择器打开 ROM。
4. 长按实体退出键返回系统菜单；退出和换 ROM 时会刷新存档。

仓库和发布包不包含商业 ROM、Nintendo BIOS、BBK 固件、NAND 镜像或模拟器数据。

## 测试

校验公开的 MIT 许可 GBA 测试 ROM：

```powershell
.\tools\bootstrap.ps1 -SkipToolchain
.\tools\verify_public_test_roms.ps1
```

私有 ROM 测试只读取被 Git 忽略的本地配置，配置方法见
[tests/roms/README.md](tests/roms/README.md)。模拟器通过不能替代真机性能和稳定性验证。

## 构建目标

| 目标 | 用途 |
|---|---|
| `gpsp_app` | 正式 DRC 应用，输出 `GBA.bda` |
| `gpsp_app_interpreter` | 解释器对照版本 |
| `gpsp_app_cpu_test` | CPU/事件调度诊断版本 |
| `gpsp_headless` | 无窗口 gpSP 冒烟测试 |
| `gpsp_dynarec` | 无窗口 DRC 冒烟测试 |
| `m0_smoke`、`m1_runtime`、`m1_av`、`m6_save` | SDK、运行时、音视频与存档探针 |

## 目录

```text
sdk/                    BBK 9588 SDK Git submodule
src/app/                BDA 主程序与 gpSP frontend
src/platform/bbk9588/   文件、音频、存档、cache 与 JIT 平台层
src/ui/                 触摸控制层
third_party/patches/    固定 gpSP 版本上的移植补丁
tests/                  BDA 探针和 ROM fixture 清单
tools/                  依赖初始化、构建、打包与校验脚本
plan.md                 移植过程、验证数据和后续计划
```

## 鸣谢

- gpSP 原作者 Exophase，以及 libretro/gpSP 维护者
- BBK 9588 SDK 与 9x88 移植：HelloClyde

## 许可证

本仓库中的 gpSP 派生补丁和 9588 移植代码按 [GNU GPL v2](LICENSE) 发布。
`sdk` submodule、gpSP 和测试依赖保留各自许可证；详见
[NOTICE.md](NOTICE.md) 与 [third_party/UPSTREAM.md](third_party/UPSTREAM.md)。
