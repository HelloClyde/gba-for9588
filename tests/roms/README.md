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

## 私有兼容性测试建议

1. Bring-up：加载 ROM，执行至少 300 帧，确认画面不是全黑且 core 没有崩溃。
2. 视频与输入：进入标题画面，验证 RGB565、方向键、A/B、Start 和 Select。
3. 音频：连续运行标题和开场，检查降采样、队列 underrun、退出 stop 和残音。
4. ROM 分页：在 2 MiB ROM cache 配置下运行大容量 ROM，统计 32 KiB page fault。
5. 存档：验证 SRAM、Flash 和 EEPROM 的检测、写入、退出刷新和重新启动恢复。
6. RTC：验证游戏时钟路径不会因为缺少宿主 `time()` 而崩溃；精度测试单独记录。
7. 性能：真机连续运行 10 分钟，记录 emulated speed、rendered fps 和音频 underrun。

测试生成的 `.sav/.srm` 和日志放在 `tests/output/`，不要写回或修改原始 `.gba` 文件。
