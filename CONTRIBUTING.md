# Contributing

## 开发环境

```powershell
git clone https://github.com/HelloClyde/gba-for9588.git
cd gba-for9588
git submodule update --init sdk
.\tools\bootstrap.ps1
.\tools\build.ps1 -Target gpsp_app -SkipBootstrap
```

提交前至少执行：

```powershell
.\tools\verify_public_test_roms.ps1
.\tools\build.ps1 -Target gpsp_app -SkipBootstrap
```

涉及触摸、PCM、窗口生命周期、IRQ 或 dynarec 的修改还必须在真机验证。请记录测试 ROM
类型、运行帧数、模拟速度、音频采样率、退出结果和完整错误计数，但不要上传商业 ROM、
存档、固件、NAND 或包含个人路径的日志。

## 补丁范围

- gpSP 上游修改应集中在 `third_party/patches/gpsp-bbk9588-dynarec.patch`。
- BBK 平台实现放在 `src/platform/bbk9588/`，界面代码放在 `src/ui/`。
- 只使用 `sdk/sdk/include/` 中的公开 API；候选固定地址和研究 API 不得进入正式应用。
- 新增依赖必须固定版本并在 `third_party/UPSTREAM.md` 记录来源和许可证。

## 数据与许可证

贡献即表示你有权按 GPL-2.0 提交相关代码。第三方代码必须保留原版权和许可证；不接受
来源不明的 ROM、BIOS、固件、图标、音频或反编译源码。
