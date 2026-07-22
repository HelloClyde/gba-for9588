# GBA 测试 ROM

仓库只保存可重新构建、许可允许分发的测试 ROM 源码。商业 ROM、Nintendo BIOS 和
用户私有存档不得提交到 Git。

## 本地私有 fixture

本地商业 ROM 仅用于合法持有者自己的兼容性测试。复制模板并填写本机路径与 ROM 信息；
`fixtures.local.psd1` 已被 Git 忽略，ROM 本体也不得复制到工作区。

```powershell
Copy-Item .\tests\roms\fixtures.local.example.psd1 `
  .\tests\roms\fixtures.local.psd1
# 编辑 fixtures.local.psd1 后执行：
.\tools\verify_test_roms.ps1
```

测试脚本会核对大小、GBA header 和 SHA-256，避免在文件被替换后继续沿用旧基线。

## 公开 fixture

公开回归使用 MIT 许可的 [jsmolka/gba-tests](https://github.com/jsmolka/gba-tests)，固定版本记录在
`deps.lock.psd1`。`bootstrap.ps1` 将源码和上游预构建 ROM 检出到被 Git 忽略的
`.deps/gba-tests`；`public-fixtures.psd1` 再固定每个 ROM 的相对路径、大小和 SHA-256。

```powershell
.\tools\bootstrap.ps1 -SkipToolchain
.\tools\verify_public_test_roms.ps1
```

首批回归覆盖 ARM、Thumb、内存、基础 PPU、无存档、SRAM、Flash 64K 和 Flash 128K。
EEPROM 不在该套件内，仍需补一份可重新构建的独立 fixture。

安装 `bbk9588-emulator-v0.1.5` 后，可以把这些 ROM 逐个送入 DRC headless BDA，执行
120 帧并导出日志。`public-fixtures.psd1` 固定 120 帧时的整段视频 hash、最终画面 hash、
PC 和 CPSR；每个最终画面已经人工检查并记录预期内容，脚本会拒绝任何未确认的行为漂移。
这仍是 gpSP 当前行为基线，不等同于独立参考实现给出的全正确结论。它只使用模拟器的专用
`runtime\bda_test` NAND，不修改原始镜像；模拟器和 NAND 不会进入仓库或 CI。
每项最后一帧同时导出为 `240x160` little-endian RGB565 文件，供语义结果检查使用。

| Fixture | DRC 最终画面 | 结论 |
|---|---|---|
| ARM | `Failed test 225` | 与上游 gpSP 解释器一致 |
| Thumb | `Failed test 227` | BBK MIPS DRC 已通过解释器停留的 211，空 rlist 停在 227 |
| Memory | `All tests passed` | 通过 |
| PPU Hello | `Hello world!` | 通过 |
| PPU Shades | 蓝色渐变色带 | 通过 |
| PPU Stripes | 蓝色竖向交替条纹 | 通过 |
| Save None | `All tests passed` | 通过 |
| SRAM | `Failed test 006` | 与上游 gpSP 解释器一致 |
| Flash 64K | `Failed test 006` | 与上游 gpSP 解释器一致 |
| Flash 128K | `Failed test 006` | 与上游 gpSP 解释器一致 |

```powershell
$env:BBK9588_EMULATOR_ROOT = 'E:\bbk9588-emulator-v0.1.5'
.\tools\test_public_roms_in_emulator.ps1 -ResetImage
```

用 `-Fixture Arm,Thumb` 可只运行指定项。结果保存在被 Git 忽略的
`tests\output\public-roms\dynarec\`。使用 `-Core Interpreter` 可运行 gpSP 解释器，并将
输出放在 `tests\output\public-roms\interpreter\`；解释器模式不套用生产 DRC 签名。使用
`-Core PatchedInterpreter` 可在关闭 DRC 时保留全部 BBK C 补丁，用于定位差异来源。

## 私有兼容性测试建议

1. Bring-up：加载 ROM，执行至少 300 帧，确认画面不是全黑且 core 没有崩溃。
2. 视频与输入：进入标题画面，验证 RGB565、方向键、A/B、Start 和 Select。
3. 音频：连续运行标题和开场，检查降采样、队列 underrun、退出 stop 和残音。
4. ROM 分页：在 2 MiB ROM cache 配置下运行大容量 ROM，统计 32 KiB page fault。
5. 存档：验证 SRAM、Flash 和 EEPROM 的检测、写入、退出刷新和重新启动恢复。
6. RTC：验证游戏时钟路径不会因为缺少宿主 `time()` 而崩溃；精度测试单独记录。
7. 性能：真机连续运行 10 分钟，记录 emulated speed、rendered fps 和音频 underrun。

测试生成的 `.sav/.srm` 和日志放在 `tests/output/`，不要写回或修改原始 `.gba` 文件。
