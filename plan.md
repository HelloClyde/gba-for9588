# BBK 9588 GBA 模拟器移植计划

> 分析日期：2026-07-22
> 目标平台：BBK 9588 `kj409588/C200`，JZ4740/MIPS32 little-endian
> SDK：[HelloClyde/bbk9588-bda-sdk](https://github.com/HelloClyde/bbk9588-bda-sdk)
> 本地验证环境：通过 `BBK9588_EMULATOR_ROOT` 指定的 BBK 9588 模拟器

## 0. 当前实现状态（2026-07-22）

当前已发布可构建、可运行 Emerald 并跨冷启动恢复存档的 gpSP/BBK9588 `v0.1.4`；公开版本
可用，但仍未满足本文全部 MVP 性能、长测、回归和许可验收标准。`[x]` 表示已有代码及对应
模拟器/真机/CI 证据；条目要求的真机、公开 ROM 或长时间测试未完成时仍保持 `[ ]`。

已验证基线：

- SDK 固定到 `73ef26afacea117b0b46ee9213d2efa80724a3a4`，gpSP 固定到
  `69e86ebe89f14c3f5f75b809c12c0a953b3d6ce4`。
- 多文件 freestanding BDA、构造器/BSS、JIT/cache、内存、RGB565、PCM 探针均在
  `bbk9588-emulator-v0.1.5` 通过。
- MIPS dynarec 的布局敏感问题已定位：gpSP 把宿主 `$gp` 复用为 GBA `r13`，9588 的宿主
  中断会异步重建 `$gp` 并污染 GBA SP。BBK 专用 MIPS stub 在生成代码执行期间关闭中断，
  调用 SDK/C 前恢复原 CP0 Status，返回 JIT 后再次保护 `$gp`；同时禁用不安全的运行时
  JAL self-patch。
- 修复后的 dynarec headless 已完成 600 帧：第 180 帧 PC `03007D56`、画面 hash
  `485915C5`，与解释器逐项一致；第 600 帧 PC `08006C10`、最终 hash `07AA6585`，
  `RESULT=PASS`。
- 真机 frontend 保留文件选择器返回的绝对路径，不再退化为仅文件名；文件选择器默认
  从 `A:\GAMEBOY\` 打开。16 MiB Emerald ROM、Flash 1M 存档、窗口、控制层和 PCM
  均已在真机完成初始化。
- 首版真机 DRC 日志停在第一帧 `RUN_EXEC_BEGIN`。已定位到 gpSP MIPS stub 强制使用
  `.set mips32r2` 和 R2 专用 `di/ehb`，而 JZ4740 是 MIPS32 R1；QEMU 使用 24Kf，因而
  未暴露该指令集不兼容。IRQ guard 已改为 R1 兼容的 CP0 Status `mfc0/mtc0` 实现，
  修正版已由真机确认能够运行。
- 当前公开版本为 `v0.1.4`（commit `b0cbc99`），基于 R39 ROM page host-safe I/O，固定
  frameskip `1`、音频默认开启；Release `GBA.bda` 为 847580 字节，SHA-256
  `63ce35ea8571badd4abb7c25d89af00ef0fc01aa38447e39ec7119d39a9d39bf`，tag CI 构建、公开
  fixture 校验和 Release 上传均通过。
- AUDIO R5 真机 600 帧区间测得 `emu_fps100=3720`、`video_fps100=1863`；音频开启后
  `audio=432/short=0/drop=0/bp=0`，且 `touch=3/release=4`，证明触摸坐标和抬起消息均
  正常。该区间同时出现 `wake=0/evt=217`：GPIO 门控把空闲态持续误判为按下，使 event
  poll 无兜底唤醒并最终阻塞；死机不是触摸消息处理失败。
- 新增隔离诊断包 `GBA9588_CPU_TEST_R2.BDA`，菜单标题 `GBA CPU TEST R2`，frameskip
  `59`，每 60 个逻辑帧仅提交 1 帧，并每 120 帧及退出时记录 fps，以区分 CPU/JIT 与
  图形渲染瓶颈；共 `838428` 字节，SHA-256
  `3a2d539cb46a6cf81097b1afb39ac9f7839a1858c1f5a342be0ca544295e432d`。
- CPU TEST R2 在每 60 帧仅提交 1 帧时，120 帧区间仍只有 `10.0–10.76 fps`，证明
  视频渲染/提交不是当前主瓶颈；但长按 Start 的退出区间升到 `25.04 fps`，用户也明确
  感到持续按键时音频流畅许多，指向无输入时 event poll 等待或固件调度降速。
- 新增 `GBA9588_EVENT_WAKE_R1.BDA`：frameskip `59`，每次 event poll 前向自己的 frame
  投递并吞掉私有空消息，同时记录 `evt/wake/wake_err`；菜单标题 `GBA EVENT WAKE`，共
  `838684` 字节，SHA-256
  `9cb1d054bad939981ed805763477fd24a53cb1a6f7093a202598b71dd157699a`。
- EVENT WAKE R1 真机 600 帧测得 `evt=1702`，即 event pump 累计占用 `42.55 s`；整个
  区间约 `45 s`。wake 投递全部成功，但旧循环消费 wake 后继续第二次空 poll，仍被阻塞。
  AUDIO R3 改为每次最多处理一个事件，并跟踪单个 pending wake，避免私有消息堆积。
- AUDIO R3 真机稳定区间达到 `57.83–58.11` emulated fps、约 `29` rendered fps，声音
  已流畅；但背压循环也调用事件服务，导致 1800 帧累计投递 `21255` 个 wake，真实触摸
  消息被私有消息压住，`drop=12224/bp=608`，随后无退出尾部地死机。
- AUDIO R4 只在主循环投递 wake；通过已验证的 `bda_touch_pressed_9588()` 在按下/刚抬起
  时让 event poll 处理真实触摸消息，并统计 `touch/release`。PCM 背压不再假定
  `bda_sys_delay(1)` 等于 1 ms，改用 25 ms 单调时钟最多等待 4 tick（100 ms）。
- AUDIO R4 真机停在首帧 `EVENTS_BEGIN`；结合 R5 的 `wake=0`，这更符合 pen GPIO 在当前
  应用生命周期里持续返回低电平后进入阻塞 poll，不能再据此判定固定地址调用崩溃。
  从模拟器 NAND 提取原始 `C200.bin` 后反汇编确认 `0x80059f68` 不依赖 `$gp`，实现确为
  `lw 0xb0010100`、掩码 `0x00040000`、active-low 归一化返回。
- AUDIO R6 不再用 GPIO 判断触摸。它读取顶层 frame 实际使用的固件 event state pointer
  `0x80825930`，在 ring queue、输入刷新、timer/release 等 ready flag 已置位时让真实事件
  优先进入 event poll；完全空闲时才投递一个私有 wake。日志新增 `ready/flags/seen`，用于
  验证真实触摸是否命中门控且空闲帧仍有防阻塞唤醒。
- AUDIO R6 真机 600 帧日志为 `wake=592/ready=0/flags=0/seen=0/touch=0/release=0`，随后
  再次死机。这证明触摸不是先异步置 event-ready flag 再等待应用 poll；固件必须进入可阻塞
  poll 后才采集/转换触摸，R6 的私有 wake 仍会抢先返回并饿死触摸。
- SDK 更新到包含已验证高分辨率 timer API 的 `0b97299`。R7 在窗口循环前启动 timer、退出
  后严格停止，用标称 1 ms counter 记录 `evt_ms`；该 API 是计数器而不是 callback，不能
  自己投递延迟 wake。R7 改采样 firmware touch latch `0x807f7110`：前 60 帧只校准空闲态，
  连续两帧偏离空闲态才允许无 wake 的真实触摸 poll，抬起再保留一次无 wake poll。若 latch
  在真机完全不变化，R7 会继续私有唤醒而不是进入阻塞，因此应保持运行并在日志中显示
  `flip=0/touch=0`。诊断间隔临时从 600 帧缩短为 120 帧，以尽快观察校准和触摸转换。
- AUDIO R7 真机只运行到 `FRAME_END=2`，首帧日志仍为 `cal=0/latch=0/wake=1`，没有进入
  120 帧诊断点。冻结发生在锁存校准尚未改变事件策略时；相对 R6/R7 唯一持续新增的机制是
  1 kHz firmware timer。该 timer 本身已由 SDK 独立真机验证，但 gpSP DRC 为保护复用的
  `$gp` 会在生成代码期间关闭宿主中断，两者组合存在 IRQ 冲突风险。AUDIO R8 保留新版 SDK
  和锁存校准，完全不启动高分辨率 timer，`evt_ms` 改由 25 ms tick 粗略换算，以隔离该变量。
- AUDIO R8 在不启动高分辨率 timer 时运行到 360 帧，证明 R7 的两帧冻结确实包含 timer
  组合风险，但长期冻结另有原因。R8 的 120/240/360 帧区间分别达到 `26.74/48.97/51.61`
  emulated fps；360 帧时 `wake=352/wake_disp=352/wake_err=0`，随后在 480 帧日志前冻结。
  firmware latch 在完成 60 次校准后始终为 `latch=idle=0/flip=0`，确认地址
  `0x807f7110` 不能作为此应用的异步触摸门控。
- AUDIO R9 移除 firmware latch 和私有 `0x7f01` ring 消息。每循环改为向 frame 投递
  `BDA_MSG_REDRAW_INPUT(0xb1)`：该消息在固件 `GUI+0x03c` 中只设置 redraw/input pending
  bit，不写 ring queue，重复投递可合并；分发后继续进入默认窗口过程以刷新输入。5 秒存档
  checkpoint 新增 `SAVE_CHECKPOINT_BEGIN_FRAME/END_RESULT`，用于区分事件 poll 与存档卡死。
- AUDIO R9 真机运行到 6240 帧，视频/音频均无提交错误、short write、drop 或 backpressure，
  且全部 checkpoint 都以 `END_RESULT=0` 结束，排除存档路径。触摸消息最终达到
  `touch=33/release=40`，但有明显延迟；6240 次 `0xb1` notify 只有第一次进入 wndproc，
  `wake_disp=1`，证明 redraw pending bit 不能作为周期事件源。event poll 累计跨越 1964 个
  25 ms tick，即至少 49.1 秒，最终再次在无退出尾部的情况下冻结。
- AUDIO R10 不再在空闲主循环调用阻塞 event poll。前 60 帧用短期私有 wake 完成启动消息
  清理，并对真机验证过的 GPIO 查询 `0x80059f68` 做空闲电平校准；之后只在去抖后的按下/
  抬起边沿进入 event poll，直到收到对应 window touch/release 消息。R5 曾证明窗口触摸消息
  可用，但当时直接相信固定 active-low 语义，导致空闲态被误判为持续按下；R10 改为运行期
  校准实际极性。日志新增 `gpio/idle/cal/flip/pp/rp` 和 `pump/pump_ret/pump_empty`。
- AUDIO R10 真机停在首个 `EVENTS_BEGIN`，尚未进入 warmup notify。该位置的第一项新增操作
  是调用 SDK 固定地址 wrapper `0x80059f68`，与 R4 的首轮停点一致，确认这个函数虽在嵌入
  原版 BDA 的独立探针中通过，但不能用于当前 standalone DRC frame。AUDIO R11 保留 R10
  的校准与边沿门控，改为直接读取 R5 已经在本应用运行到 600 帧的 GPIO MMIO
  `0xb0010100` bit 18；构建反汇编确认不再引用 `0x80059f68`。
- AUDIO R11 真机运行 2727 帧后由 Escape 正常退出并得到 `RESULT=PASS`；禁用空闲 event
  poll 后 `evt_ms=0`，区间最高达到 `58.53` emulated fps，证明事件等待是此前主要节拍和
  稳定性问题。但 `gpio=idle=0/flip=0` 全程不变，用户触摸也没有生成消息，确认该 GPIO
  MMIO 在当前真机应用上下文同样不能作为触摸电平源。
- AUDIO R12 移除全部固定函数、GPIO 和 latch 读取，创建一个 1x1 隐藏 `gifctrl` 子控件，
  使用双帧 GIF 的 10 ms GUI control timer 作为 event poll 的固件唤醒源。该控件 timer
  路径和 `0x144` 消息生命周期已由 SDK 的 8013 探针验证；R12 在每轮只执行一次标准
  `poll -> step -> dispatch`，触摸继续走 frame 的 `message=1/2`，退出时先 destroy 子控件
  再 stop/release/close parent。日志记录 `timer/touch/release` 和 create/destroy 结果。
- AUDIO R12 真机运行到 5433 帧，`timer=2584`、`pump_ret=5433`、`touch=18/release=23`，
  证明 GUI control timer 能稳定唤醒事件循环且触摸可用。退出尾部为 `RESULT=PASS`、
  `LOOP_EXIT_CAUSE=2`、`TOUCH_START_MAX_TICKS=32`、`EXIT_INPUT_MASK=0x100`，不是 ROM 或 DRC
  崩溃，而是触摸 Start 连续 800 ms 命中了旧退出快捷键。R13 删除触摸 Start 退出，只保留
  实体 Escape 长按退出，并记录 `TOUCH_START_EXIT=DISABLED`。
- R12 的 `short/drop/bp` 均为 0，只能证明 PCM 写接口没有报错，不能证明 firmware 播放队列
  未播空。后半段逻辑帧率降到 `33.80-42.10 fps`；按相邻日志的 `audio*512+queue` 增量计算，
  该区间实际只生成约 `12.5-15.5 kHz` 样本，低于硬件固定消耗的 `22.05 kHz`，足以产生
  间歇 underrun。R13 新增区间 `aud_hz`，目标值约为 `22050`。
- R12 每 120 帧同步打开、追加并关闭日志文件，且每 5 秒用逐 bit CRC 扫描 128 KiB 存档；
  这些诊断操作会放大弱 CPU 上的周期抖动。R13 将普通运行日志降到每 600 帧，未变化的
  checkpoint 不再写两行运行日志，CRC 改为结果等价的 256 项查表实现，并在退出时记录
  `SAVE_CHECKPOINT_MAX_TICKS`。实际存档仍保持 5 秒检查和 A/B 双槽写入策略。
- AUDIO R13 真机最后一行是 `core=1800`，无退出尾部。三个 600 帧区间的 `aud_hz` 分别为
  `15146/17614/19136`，直接确认供音低于 22050 Hz；`short/drop/bp` 仍全为 0，说明问题是
  生成速度不足而非 PCM API 短写。按区间 fps 推算总运行时间约 40.2 秒，主循环在写完
  `core=1800` 日志后紧接着检查第 8 个 5 秒 checkpoint；若该轮首次检测到 SRAM 变化，
  会同步写入并回读 128 KiB。当前只能高度怀疑冻结位于该路径，尚不能排除下一轮 event poll。
- AUDIO R14 为单变量隔离包：运行中完全禁用 checkpoint，CRC 回退到 R12 已验证实现；只在
  实体 Escape 正常退出时，先销毁 GUI timer、停止 PCM，再执行 A/B 存档写入。退出日志新增
  `AUDIO_STOP=PASS`、`SAVE_EXIT_BEGIN` 和 `SAVE_EXIT_RESULT`。该诊断包异常断电或强制退出会
  丢失本次游戏内存档，必须正常退出才能落盘。
- AUDIO R14 真机运行到 5069 帧，越过 R13 的 1800 帧冻结点；`pump=5069/timer=2477`、
  `touch=12/release=19`，最终实体 Escape 退出后 timer destroy、audio stop、exit save 和
  `RESULT=PASS` 全部完成。这强烈支持运行期 checkpoint/CRC 扫描是 R13 冻结触发条件；但
  `SAVE_EXIT_RESULT=0/SAVE_WRITES=0` 表示本轮存档内容未变化，实际 128 KiB 写入仍待联合验证。
  稳定区间仅 `46.60-52.40 fps`、`aud_hz=17204-19345`，仍低于 22050 Hz。
- R14 每个逻辑帧都进入 event poll，约 101 秒运行中累计阻塞 14.425 秒；5069 次 pump 只有
  2477 次是 GUI timer，说明 poll 频率约为 timer 的两倍。AUDIO R15 改为每 2 个逻辑帧 pump
  一次：目标速度下约 30 次/秒，仍高于实测 timer 约 24.5 次/秒，保持 30fps 视频设置且触摸
  最坏只增加一帧延迟。周期保存和退出顺序保持 R14 不变，用于单独验证事件开销。
- SDK 更新并固定到 `05f30f598668ee142a244e02c7258b6e913f0ff2`，新增已由模拟器和真机验证的
  Frame window timer API。它以 `(frame,timer_id)` 注册、直接向顶层 wndproc 投递 `0x144`；
  20/40 ms 周期已在真机验证，10 ms 调度粒度已在模拟器验证并由 R16 继续做真机联合验证。
  它不启动 R7 使用过的 TCU0/IRQ `0x17` 高分辨率 counter，因此不与 DRC 的宿主 IRQ guard
  冲突。新版 SDK 全量 unittest 为 256 项通过、6 项按环境跳过。
- AUDIO R16 取代尚未真机测试的 R15：删除 `bda_controls.h`、嵌入 GIF 和隐藏 gifctrl，改用
  `bda_gui_window_timer_start(g_frame,0x958,10)`；wndproc 只消费匹配 id 的 window timer，退出
  保存前显式 stop。真正的 10 ms 唤醒频率高于 59.7 Hz 逻辑循环，因此恢复每帧 event pump，
  预期不再等待约 40 ms 的 GIF 帧。BDA 字符串检查确认 `GIF89a/GIFCTRL` 资源匹配数为 0。
- FREEZE DIAG R17 用无文件 I/O 的 volatile 阶段标记和 GDB 原子快照复现两次冻结，分别停在
  `core=10822` 和 `core=8116`。outer/core stage 均为 `3`，确认冻结发生在
  `execute_arm_translate()`；最终 `PC=0xBFC00380`、`Cause=0x40808018`、`EPC=0x81C98DF4`。
  EPC 对应 `mips_update_gba` 恢复宿主 IRQ 的 `mtc0 Status`，异常码 `6` 为 instruction bus
  error。模拟器未映射 Boot Exception Vector，因此异常后继续在 `0xBFC00380` 取指异常并占满
  单核，画面、PCM 和 DMA 同时停止。
- 根因是 DRC IRQ guard 的 GP 恢复顺序错误：旧代码先 `mtc0 Status` 开 IRQ，应用 `$gp` 要等到
  后续 `jal update_gba` 的 delay slot 才恢复。若 pending IRQ 在两者之间到达，固件 handler
  看到的 `$gp` 仍是 GBA `r13`，从错误基址访问固件状态并触发取指异常。R18 在开 IRQ 前先从
  `GP_SAVE($16)` 恢复应用 `$gp`；返回 JIT 时仍保持先关 IRQ、再恢复 GBA 寄存器的顺序。
- IRQ GP FIX R18 已在 8014 模拟器连续运行约 9 分钟到 `core=32473`，超过 R17 两个冻结点；
  guest IPS、PCM 和 DMA 全程推进，`short/drop/bp=0`，稳定区间为 `59.55-59.85 fps`、
  `29.77-29.92 video fps`、`21985-22095 Hz`。实体 Escape 退出后 window timer、PCM、存档和
  Frame 生命周期全部完成，最终 `RESULT=PASS`。仍需同一包真机复测触摸、音频和长时间退出。
- HELP R19 在控制层顶部增加 `?` 图标按钮，调用 SDK 同步 `bda_help_page()` 显示按键、触摸、
  ROM 目录和正常退出保存说明，并标注 `gpSP / Exophase / HelloClyde`。模态页打开前停止 PCM 和
  10 ms Frame window timer，返回后重启两者并重置性能统计基线；帮助页在 8014 已完成可视打开
  和 Escape 返回验证。声音按钮命中区由 56 px 收窄到 33 px。8014 自动触摸注入有时只改变
  固件 Escape 电平而不投递窗口触摸消息（日志为 `touch=0`），因此按钮单击、返回后音频连续性和
  前 8 次 `TOUCH_COORD x/y/hit` 诊断仍以真机结果为准。
- HELP R20 按系统页已验证生命周期修正调用顺序：停止 timer/PCM 后先释放应用 draw context，
  使用 `bda_help_page(0, title, body)`，返回后重新激活原 Frame、无条件获取 draw context，并把
  `g_full_redraw` 置位；主循环使用缓存的最后一帧显式重绘游戏区，再重绘 dirty 控制层，避免
  系统页与应用绘图上下文争用或返回后短暂空白。
- HELP ROM R21 将系统帮助正文改为固件 GBK 中文，保留 gpSP、Exophase 和 HelloClyde 署名；
  顶部新增文件夹按钮。按下后先通过 SDK 是/否对话框确认，再打开 `A:\GAMEBOY\` 选择器；取消
  不影响当前游戏，选择成功后 checkpoint 旧存档、卸载旧 core 并加载新 ROM。新 ROM 加载失败
  时恢复旧路径并尝试重载原游戏；所有系统模态页统一执行 draw context 释放/重新激活/获取、
  timer 与 PCM 暂停恢复以及完整重绘。
- TOUCH R22 保留 10 ms window timer，每轮限量派发最多 4 条已排队消息；遇到触摸坐标后
  立即结束本轮派发，确保按下状态至少进入一个 gpSP 帧。音频背压等待期间同步处理窗口
  事件，减少偶发的 30-100 ms 触摸延迟，并记录 drain 上限、触摸提前结束和批量峰值。
- BALANCE R23 根据 R22 真机结果收紧调度：每帧最多派发 2 条消息，保留触摸坐标提前结束；
  音频背压等待恢复为 PCM 独占服务，不再穿插 GUI dispatch。目标是在清理 timer 后及时处理
  触摸的同时，避免最多 4 次 GUI 派发抢占 PCM 补充时机而造成音频卡顿。事件清理开始前和
  每派发一条 GUI 消息后检查并提交就绪 PCM 块，使音频与触摸交错执行而不是互相阻塞。
- R23 发行包使用 `assets/gba-icon.png`，由 SDK packer 自动生成 BDA 所需的四档 RGB565/VX
  图标；应用和解释器目标使用 GBA 图标，底层诊断探针继续保留 SDK 默认图标。
- TOUCH R24 根据 R23 真机日志取消持续双重事件泵：空闲和已建立触摸期间每帧只派发一条
  消息；仅当 SDK 已验证的 9588 触摸电平显示刚按下、窗口坐标尚未到达时，临时最多追取
  4 条消息，坐标一到立即停止。触摸抬起电平同时用于清除滞后的本地按键状态。目标是恢复
  约 59.7 FPS/22050 Hz，并避免 R23 每帧触顶造成的 GUI dispatch 死机。
- STABLE R25 根据 R24 在首个 `EVENTS_BEGIN` 内冻结的真机结果，禁止在 gpSP DRC 上调用
  `bda_touch_pressed_9588()` 及其他固定地址/GPIO 触摸函数。事件泵恢复为每帧严格一次，
  保留事件前后的 PCM 提交；新增 timer、`0xb1` redraw 和 other 消息分类计数，用真机队列
  构成指导后续触摸优化，不再通过重复 dispatch 或固定固件函数猜测触摸边沿。
- EVENTMAP R26 保持 R25 单次事件泵行为不变，为 `other` 消息增加四槽内存直方图，按
  `message/handle/wparam` 聚合并每 600 帧输出一次 `EVENT_TYPES`。R25 真机 1313 帧正常退出，
  其中 timer 612、redraw 1、other 648；必须识别 other 后才能从消息源头优化触摸排队。
- BALANCE R27 根据 R26 真机 1280 帧日志把 window timer 从 10 ms 调整为 SDK 已完成真机
  验证的 40 ms，事件泵仍严格每帧一次。R26 的 640 次视频提交对应 643 条 `other`，首批
  消息是内部对象上的 `0x30/0x33` 成对状态消息；高频 timer 与绘制状态消息长期占满单泵
  预算。R27 限制 timer 产生速率，为触摸队列留余量，并改为按 message code 聚合输出
  `EVENT_CODES`，不再因每次变化的内部 handle 导致直方图 overflow。
- EVENTMAP R28 撤回 R27 的 40 ms timer 实验并恢复 R26 的 10 ms 调度。R27 真机在
  `core=600` 后冻结，timer 已降至 115 条但 `other=470`、`pump_empty=0`，证明 timer
  频率不是队列饱和的主因。R28 只把按 message code 聚合的槽数扩展到 16 个，以识别
  R27 前四个低频启动消息之后造成 463 次 overflow 的高频消息，不改变单泵运行行为。
- STABLE R29 的模拟器采样确认高频 `0x144/handle=0/wparam=0` 是事件 wrapper 完成
  step/dispatch 后留下的结构状态，不能据此判定事件队列积压。R29 保持 10 ms window
  timer 和每帧一次事件泵，修复 DRC 关闭主机中断的竞态：MIPS 异常入口允许破坏
  `$k0/$k1`，旧宏可能在读取 Status 与清除 IE 之间被抢占并把损坏值写回 CP0；改用会
  被固件异常现场保存的 `$t0/$t1`，同时在 DRC 入口采用相同顺序。
- R29 在 9588 模拟器中完成 6 分钟连续触摸压力测试并运行到 `core=25200`：模拟速度
  59.55-59.85 FPS、视频 29.77-29.92 FPS、音频 21985-22095 Hz，视频提交错误、音频
  short/drop/backpressure 和 crash snapshot 均为 0。模拟器启动 ROM 时的 underrun 计数
  在压力阶段未继续增加；下一步仍需真机验证 `$k0/$k1` 竞态修复是否消除长期冻结。
- R29 真机在 `core=1800` 前仅收到 18 个触摸坐标，但事件 wrapper 的 post-dispatch
  状态累计出现 `0x04=70`、`0x0B=35`，符合真实触摸消息链处理不足和按键延迟；随后无
  退出尾日志的冻结也证明只修正 CP0 临界区使用的寄存器还不够。
- R30 将主机 IRQ 从进入 DRC 起一直屏蔽到一帧结束，并在恢复 `$gp`、`$s0-$s7`、
  `$fp`、`$ra` 后才恢复调用者 CP0 Status，避免固件 IRQ/调度器看到半恢复的 guest
  寄存器上下文。初始双泵参数在模拟器连续运行约 10643 个核心帧、269 次触摸并正常
  `RESULT=PASS`，未再冻结，但速度约 43 FPS、音频生成约 15.9 kHz，性能代价不可接受。
- R30 最终参数采用 20 ms window timer 与每帧单泵：timer 约 50 条/s，低于约 60 帧/s
  的事件消费能力，不再像 R29 的 10 ms timer + 单泵那样持续积压，也不承担双泵成本。
  触摸坐标到达后仍立即停止事件泵；音频仍使用真机已验证的 1024-byte block。
- R30 最终参数在 9588 模拟器完成 174 次自动触摸并运行到约 `core=15000` 后正常退出：
  模拟速度 59.55-59.70 FPS、视频 29.77-29.85 FPS、音频 21985-22040 Hz，
  `crash_snapshot` 为空；window timer 和音频正常停止，视频/控件提交错误及音频
  short/drop/backpressure 均为 0，最终 `RESULT=PASS`。真机仍需重点复测短触摸延迟和
  超过 R29 `core=1800` 冻结点后的稳定性。
- R30 发布二进制进一步把 `$sp` 的恢复移到 CP0 Status 写入之前，确保 IRQ 开放时
  `$gp/$s0-$s7/$fp/$ra/$sp` 均已回到主程序上下文。该二进制在模拟器继续运行到约
  `core=9500`，完成 35 次短触摸压力和一次正常时长释放后可正常退出，清理尾部仍为
  `WINDOW_TIMER_STOP=1`、`AUDIO_STOP=PASS`、`RESULT=PASS`。模拟器会合并部分 100 ms
  短触摸 release，可能暂时保留触摸掩码；真机现有日志未出现该比例的 release 丢失。
- R30 真机加载 4 MiB ROM 后只能达到约 35.15 FPS、音频生成约 12977 Hz；虽然核心执行
  到 349 帧并由 Escape 正常退出、视频提交和清理均为 PASS，但用户观察不到游戏正常
  进入。该结果否定了“整帧屏蔽宿主 IRQ”的方案：它会饿死真机固件的调度、音频和显示
  服务，模拟器满速结论不能外推到真机。
- R31 回退到 R18/R29 已验证的 GP-safe IRQ 窗口：只在 DRC guest 寄存器活跃区屏蔽 IRQ，
  调用 `update_gba` 前先恢复应用 `$gp` 再恢复 Status，C 回调返回后使用异常现场会保存的
  `$t0/$t1` 原子关闭 IRQ。20 ms window timer 和每帧单泵保持不变，用于避免 R29 的
  10 ms timer 生产速度高于事件消费速度；返回主程序前恢复 `$sp` 的改进也继续保留。
- R31 在 9588 模拟器分别加载 4 MiB 的 `advancewars.gba` 和中文数独 ROM，前者实际显示
  启动动画，后者实际进入游戏主菜单；不是只依据 video callback 判定。数独启动 600 帧
  时约 56.64 FPS/20911 Hz，后续同一运行达到 59.70 FPS/22040 Hz，证明 R30 的启动回归
  已消除。下一步以 R31 真机复测相同 4 MiB ROM，并继续观察长期冻结点。
- R31 正式包使用 `GBA` 作为 BDA 头标题和 `GBA.bda` 作为文件名，避免固件菜单截断；诊断版本号仅
  保留在运行日志。修复打包器错误地把 RGBA 图标合成到黑底的问题：四套 VX 图标现在把
  全透明像素编码为 RGB565 洋红色键 `0xf81f`，并保留可见抗锯齿边缘的原始颜色。
- R32 更新 SDK 并切换到已验证的 `bda_gui_raw_event_fetch()`：游戏运行阶段完全停用 Window
  Timer 和窗口消息泵，每次最多消费 8 条 raw 事件；触摸 DOWN/MOVE/UP 使用
  `bda_gui_touch_position()` 获取坐标，启动残留 MOVE/UP 在首个 DOWN 前被忽略。实体键每次只
  读取一个 6-byte input packet，不解释 raw key event 的 `value`。
- gpSP 在 `update_gba()` 中每累计 8192 GBA cycles 调用一次前端输入，约 0.49 ms；按键 IRQ
  只在该函数原有的统一中断检查处提升，避免子帧输入直接改变 DRC 的 CPU 模式或返回 PC。
  PCM 背压等待不再额外调用输入，避免一次 600 帧测试产生百万级无效 raw poll。
- R32 在 9588 模拟器加载 16 MiB Emerald 跑到 `core=12000`：稳定为 59.70-59.85 逻辑 FPS、
  29.85-29.92 显示 FPS、22040-22095 Hz，视频错误、音频 short/drop/backpressure 均为 0；
  raw A/B、声音开关、中文帮助页、换 ROM 确认与取消均已通过。
- R32 真机长按日志显示每 600 帧约执行 20573 次输入轮询，长按阶段 MOVE 可增加 758 条；
  每条 MOVE 都强制重绘完整 `240x160` 控制层，导致逻辑速度从 49.89 FPS 降至 38.46 FPS、
  音频生成率从 18420 Hz 降至 14199 Hz，随后无退出尾部地冻结。R33 将轮询改为每 16384
  cycles（约 0.98 ms），同批 MOVE 合并为一次坐标读取，并只在命中状态变化时重绘控制层。
- R33 真机连续运行到 `core=24989` 后完整进入退出流程；549 条 MOVE 只产生 349 次坐标读取、
  28 次命中变化和 41 次控制层绘制，运行期未冻结。日志最终写出 `RESULT=PASS`，但返回桌面
  时画面冻结，证明故障位于 frame 所有权交接：R33 直接 `stop -> release -> close`，没有等待
  ESC 抬起，也没有让固件 event poll 完成 detach。R34 在 raw 输入停止后按已验证顺序执行
  `wait release -> stop -> release -> exit-only pump -> end draw -> close -> return`。
- R34 真机运行到 `core=3600` 后无退出尾日志地冻结；最后区间仍为 53.45 FPS、19733 Hz，
  `short/drop/bp=0`，raw 队列上限命中维持 21，DOWN/UP 已配对，因此不是音频背压或输入队列
  持续积压。剩余 DRC 风险是 `update_gba()` 回调只恢复了应用 `$gp`，开放 IRQ 时
  `$s0-$s7/$fp` 仍保存 GBA guest 值。R35 在每次 C 回调前把完整宿主 callee-saved 上下文从
  DRC 栈恢复，并单独保存 guest 的 block PC 与 CPSR flag cache；回调后原子关闭 IRQ、恢复
  register-base 与 guest cache，再由现有路径加载其余 guest 寄存器；
  16K 输入周期、raw 上限和音频参数保持不变，以隔离此次稳定性修复。
- R35 真机成功运行到 `core=19200`，期间完成一次换 ROM，并继续运行约 7200 帧；最后区间
  `video_err/short/drop/bp=0`、raw DOWN/UP 配对，仍无退出尾日志地冻结。完整 callee-saved
  恢复延长了运行时间但没有消除故障，说明在 DRC guest 上下文内开放 IRQ 的方案仍不可靠。
  R36 不再在 `mips_update_gba` 或 CPU sleep loop 内开放 IRQ：每累计 16384 cycles 设置一次
  host-yield，先从 DRC 返回并恢复 `$gp/$s0-$s7/$fp/$ra/$sp` 与 CP0 Status，再在 libretro
  主机上下文读取 raw 触摸和实体键，随后继续当前 GBA 帧。最长 IRQ 屏蔽约 1 ms，避免 R30
  整帧屏蔽导致固件服务饥饿，同时彻底移除 guest 寄存器活跃时的固件抢占窗口。
- R36 真机仅运行到 `core=600` 后冻结，区间速度降至 45.37 FPS/16752 Hz，证明每 16K cycles
  强制退出并重入 DRC 的开销不可接受，且该边界仍未解决停机。R37 撤回 subframe host-yield，
  恢复 R35 的纯核心 `update_gba()` IRQ 窗口，但把 `bda_gui_raw_event_fetch()`、触摸坐标和
  6-byte 实体键 packet 全部移到每个完整 GBA 帧的 libretro 宿主边界。该版本用于隔离 SDK
  输入调用与 DRC 回调交叉是否为剩余冻结条件；诊断日志临时缩短为每 120 帧记录一次。
- R37 真机运行到 `core=1800` 后无退出尾日志地冻结；最后一次记录仍有 52.74 FPS、19472 Hz，
  `video_err/short/drop/bp=0`，`raw_poll=core=1800`，raw 上限命中仅 29 次。输入已经完全移出
  DRC 回调仍会冻结，因此 SDK 输入与 guest 上下文交叉不是必要条件；声音开关也只改变 PCM
  样本是否填零，静音时仍持续提交相同音频块，不能解释 R34/R35/R37 的共同停机。
- R38 不再改变输入、音频或 IRQ 策略，增加 ROM/RAM translation cache 当前字节数及 ROM、
  RAM、主动回收计数。JIT 任一缓存达到 75% 时，在 `retro_run()` 输入完成后、进入 DRC 前的
  完整宿主帧边界调用 `flush_dynarec_caches()`，同时回收 ROM/RAM 缓存，避免容量耗尽时从
  递归 block translation 内回卷。运行日志新增 `jit_rom/jit_ram/jit_rf/jit_wf/jit_pf`，用于
  判断冻结是否与 JIT 增长或回收紧邻发生。
- R38 真机最后记录到 `core=600` 后冻结：ROM JIT 为 `782716/2097152` 字节，只有约 37%，
  `jit_rf=0/jit_pf=0`；RAM 的 72 次失效在 `core=240` 前已经完成，之后计数不再变化。
  因此现有区间不支持“translation cache 写满或主动回收导致冻结”，JIT 容量不再是首要嫌疑。
- R39 修复另一条仍处于 guest 上下文的固件调用链：2 MiB ROM cache miss 时，gpSP 的生成代码
  会直接进入 `load_gamepak_page()`，其中执行 SDK `seek/read`。新 MIPS 包装器保存 JIT/C 调用者
  的全部 callee-saved 状态，恢复顶层宿主 `$gp/$s0-$s7/$fp/$ra` 后执行 32 KiB page I/O，并在
  返回时精确恢复包装器入口的 CP0 Status，兼容直接 JIT miss 与 `update_gba()` 内嵌套 miss。
  日志继续每 120 帧记录 `rom_pg/rom_last/rom_stage/rom_safe`，避免提高 NAND 日志频率干扰
  音频；正常运行时 `rom_stage=0` 且 `rom_safe` 应随 DRC 期间的 `rom_pg` 增长。
- R39 真机完成 17819 个逻辑帧，按区间计数估算约 6.1 分钟；150 个运行采样中
  `rom_stage` 始终为 0，最终 `ROM_PAGE_LOADS=ROM_PAGE_HOST_SAFE=1795`，证明全部运行期
  ROM miss 都经过宿主安全包装器并成功返回。期间完成 4 次 ROM JIT 主动回收、106 次 RAM
  cache 失效，仍持续推进；8909 帧视频提交、PCM short/drop/backpressure 均为 0。实体 Escape
  退出后音频停止、存档写入、窗口关闭全部成功，最终 `SAVE_WRITES=1/RESULT=PASS`。该结果比
  R38 冻结点长约 29.7 倍并强力支持 ROM page I/O 上下文竞态假设，但尚未超过 R35 的 19200
  帧，也未完成 10/30 分钟稳定性门槛。稳态区间平均约 49.4 逻辑 FPS、24.7 显示 FPS、
  18.3 kHz 音频生成率，说明偶发音频断续的剩余原因仍是模拟速度不足，而不是 PCM 短写。
- 当前工作树 R40 修复 MIPS DRC 的 backup region `0x0E` 读取：SRAM/Flash 使用 8 位总线，
  16/32 位读取必须把同一字节复制到全部 bus lane。修复前三个可写存档 fixture 均停在
  `Failed test 004`；修复后推进到与上游 gpSP 解释器相同的 `Failed test 006`，最终帧 hash
  从 `A5530AF6` 变为 `1EE0FBA6`。全部 10 个公开 fixture 重新执行 120 帧 DRC，视频、PC、
  CPSR 签名 `10/10` 通过；正式 `GBA.bda` 已重建并通过 SDK validator，大小 847580 字节，
  SHA-256 `b906e499a51096b7e3bb11f1cd55f56295950f58cefe29d518c120d990ffa407`。正式应用经系统
  文件选择器启动公开 Memory ROM，连续执行 12803 个逻辑帧，约 `60/30 FPS`、`22.28 kHz` 音频，
  视频错误、PCM short/drop/backpressure 均为 0；实体 Escape 长按完成音频停止、窗口关闭和
  退出，最终 `RESULT=PASS`。这是模拟器功能回归，R40 仍待真机复测。
- 当前工作树 R43 增加运行期 `frameskip=0/1/2` 切换，控制栏显示 `60/30/20`，只改变画面
  提交频率，不改变 59.7275 Hz GBA 逻辑时序；声音、frameskip 与实体 A/B 交换状态使用固定
  目录下的 CRC A/B 双槽配置，只在启动读取、正常退出写入，热循环不增加 NAND I/O。音频
  重采样热路径已去除通用整数除法，正常 PCM 提交直接引用环形缓冲连续块，运行日志间隔由
  120 帧改为 600 帧以减少同步文件 I/O，并可生成完全关闭周期日志的独立性能包。扬声器
  短按保持静音/恢复语义，长按约 500 ms 在 100/75/50/25% 间循环；衰减相对系统当前音量
  计算，退出时恢复原值。配置 v2 继续兼容 28 字节 v1 双槽。4 MiB ROM cache 后续真机测试
  出现死机，构建入口已删除并固定为 2 MiB。默认 `-Os` 正式包已通过 SDK validator，大小 851612 字节，
  SHA-256 `1158a74229dcff37499ba65b9bf43b5d00eacf05a8f13418e6d3c0285594de7b`；
  动态模拟器与真机验证按当前安排后置。
- 当前工作树 R44 在不启用固件 timer、不改变 IRQ 策略的前提下加入只读 CP0 Count 阶段剖析：
  分别累计核心、视频提交、音频重采样、PCM 服务、ROM page I/O 和控制层提交耗时，并用每帧
  wall sample 安全覆盖 Count 回绕。运行日志输出区间千分比、Count/25 ms 校准值和视频、音频、
  ROM 单次最大耗时；`perf_cpu_pm` 是从核心包含时间中扣除嵌套回调后的近似值。重复的周期
  `FRAME_HEARTBEAT` 文件写入已删除。默认 `-Os` BDA 已通过 SDK validator，大小 853404 字节，
  SHA-256 `609db81b08af63cdcf7ecbdee667ad393c9479d37a1addfe7cac612fd26b4d5e`；
  2026-07-22 真机冒烟确认可正常运行，尚未覆盖 10/30 分钟长测、全部切换档位和配置迁移。
- 私有 Emerald fixture 已完成新游戏、角色移动、设置时钟和游戏内保存。顶部为原生
  `240x160` 游戏画面，底部为 A/B/L/R/D-pad/Start/Select 触摸控制层；实体方向键也已在
  游戏内验证，不再穿透到 9588 桌面。
- Emerald 首次受控运行共 19154 帧：视频提交错误 `0`、控制层提交错误 `0`、音频短写
  `0`、音频丢弃 `0`、背压跳过 `0`，触屏长按 Start 正常回到 9588 菜单，最终
  `RESULT=PASS`。
- 游戏内 Flash 1M 保存生成 `POKEMON.SV0/SV1` 两个 131096 字节槽：generation 3/4、
  payload 均为 131072 字节，CRC `F5BCF679/DD0B2210` 均与实际 payload 一致。QEMU 冷重启后
  标题菜单显示玩家“拉尔德”、时间 `0:03` 的“继续游戏”，载入后回到二楼卧室。
- 冷启动恢复运行记录 `SAVE_OPEN=PASS`、`SAVE_WRITES=0`、受控退出原因 `2`、
  `RESULT=PASS`；A/B 槽 generation 和 CRC 未变化，证明读取没有误写存档。
- 新增 `m6_save` 探针：128 KiB A/B 双槽 generation 1/2 写入、截断最新槽后回退旧槽、
  重建 generation 2 并跨 QEMU 冷重启恢复均通过。
- 私有测试 ROM 已按大小、GBA header 和 SHA-256 通过本地 fixture 校验，ROM 与校验值不进入仓库。

当前阻断项：

1. **真机持续稳定性仍待验证。** R39 已完成 17819 帧、约 6.1 分钟真机运行，1795 次 ROM
   page I/O、4 次主动 JIT 回收、真实存档写入和正常退出均通过；仍需超过模拟器 32473 帧记录，
   并完成 10/30 分钟、连续触摸、声音开关、帮助页和换 ROM 联合验证。
2. **音频供给和节拍尚未最终收敛。** R39 稳态平均约 `49.4` 逻辑 FPS、`24.7` 显示 FPS、
   `18.3 kHz` 音频生成率，仍低于固定的 22.05 kHz 播放速率，足以造成偶发 underrun。
   PCM short/drop/backpressure 均为 0，说明首要缺口是核心速度和精确 pacing，而不是写接口错误。
3. **公开正确性覆盖仍不足。** 10 个公开 ARM/Thumb/内存/基础 PPU/SRAM/Flash fixture 已在
   9588 模拟器中逐项完成 120 帧 DRC；最终 RGB565 画面均已人工检查，并固定整段视频、最终
   帧、PC 和 CPSR 签名。Memory/SaveNone 显示 `All tests passed`，基础 PPU 画面正确；ARM
   停在与解释器相同的 225，Thumb 停在 227，三项可写存档停在与解释器相同的 006。仍缺
   mGBA test suite、自制 homebrew、EEPROM 和 SRAM/EEPROM 跨重启矩阵，不能据此宣称
   CPU/PPU/save 全部正确。

## 1. 结论

主内核选择 **[libretro/gpSP](https://github.com/libretro/gpsp)**，以当前分析过的
`69e86ebe89f14c3f5f75b809c12c0a953b3d6ce4` 为初始固定版本，不跟随浮动的
`master`。移植方式是保留 gpSP 的 libretro 边界，在 BDA 内实现一个极小的 libretro
frontend，不移植 RetroArch。

选择 gpSP 的核心原因：

1. 已有 MIPS32/MIPS64 动态重编译器，最有希望在 JZ4740 上达到可玩速度。
2. 原生支持小 ROM cache 和 32 KiB 页面换入，不要求把最大 32 MiB ROM 全部载入内存。
3. 视频输出已经是 `240x160 RGB565`，可直接放进 9588 的 `240x320` 画面，不需颜色转换或缩放。
4. libretro 已把视频、音频、输入、文件系统隔离为回调，平台适配面较清晰。
5. 当前代码仍在维护，分析版本的音频路径已经改为整数/定点实现，适合无可靠 FPU 假设的目标。

**mGBA 不作为首发内核。** 它更准确、MPL-2.0 许可也更宽松，但当前 ARM7TDMI
执行路径是解释器，没有 gpSP 的 MIPS dynarec。在 9588 上应把它用作桌面参考实现和
测试结果对照，不把有限时间先花在一个大概率无法实时运行的生产内核上。

## 2. 候选内核对比

| 内核 | 9588 适配价值 | 主要问题 | 许可证 | 结论 |
|---|---|---|---|---|
| [libretro/gpSP](https://github.com/libretro/gpsp) | MIPS dynarec、低内存 ROM 分页、RGB565、libretro 回调 | freestanding libc、可执行代码缓存、I/D cache 同步需要适配 | GPL-2.0 | **采用** |
| [mGBA](https://github.com/mgba-emu/mgba) | 高兼容性、纯软件渲染、无硬依赖、维护成熟 | 无 MIPS dynarec，代码和平台层更大，JZ4740 实时性风险高 | MPL-2.0 | 桌面参考/失败回退实验 |
| [NanoBoyAdvance](https://github.com/nba-emu/NanoBoyAdvance) | 周期精确，适合正确性验证 | C++20、重准确性、依赖现代构建环境；GitHub 仓库已迁移并归档 | GPL-3.0+ | 不采用 |
| [VBA-M](https://github.com/visualboyadvance-m/visualboyadvance-m) | 兼容性和历史积累较多 | 桌面依赖和代码体量大，不面向低端 MIPS 手持设备 | GPL-2.0+ | 不采用 |

## 3. 已确认的平台约束

- SDK 使用 `mipsel-none-elf-gcc 15.2.0`，目标为 MIPS32 little-endian。
- BDA 是 freestanding 程序，默认 `-ffreestanding -fno-builtin -nostdlib`，没有宿主 libc。
- 当前 `bda-pack` 只直接编译一个 C 文件；gpSP 需要多文件 C/C++/MIPS 汇编构建流程。
- 屏幕为 `240x320`，公开图形路径支持 RGB565、VX、compatible context 和 dirty rect。
- GBA 原生画面为 `240x160 RGB565`，可以 1:1 显示，并保留 160 行给触摸控制区。
- 公开输入只有方向、Enter、Escape 六个实体键；GBA 的 L/R/Start/Select 需要触摸补足。
- 公开音频格式固定为 `22050 Hz / signed 16-bit / mono`；gpSP 内部输出为
  `65536 Hz / signed 16-bit / stereo`，必须做定点降采样和混单声道。
- SDK 已公开 1 ms firmware timer，但 gpSP DRC 的宿主 IRQ guard 与该 timer 组合在 R7
  真机测试中疑似冲突；当前生产循环只使用 25 ms 单调 tick，不能直接做精确 59.7275 fps 调度。
- SDK 已在模拟器中验证约 6.5 MiB 的多块同时 heap 分配，但未给出真机可用内存上限。
- 本地 QEMU 当前配置 160 MiB RAM，这不能代表真机 heap，也不能代表真机执行速度。
- Raw PCM 已在模拟器和真机验证，但退出必须调用 `bda_audio_stop()` 并恢复衰减状态。

## 4. 目标架构

```text
bda_main / 窗口生命周期
          |
          v
BBK9588 minimal libretro frontend
  - environment / VFS
  - RGB565 video
  - touch + key input
  - PCM resampler/ring buffer
  - clock / pacing
  - save persistence
          |
          v
gpSP core + MIPS dynarec
          |
          v
BBK 9588 SDK public API
```

建议目录：

```text
sdk/                          固定 SDK Git submodule
.deps/gpsp/                   bootstrap 到固定上游版本，不提交工作树
src/app/                      bda_main、窗口和菜单
src/platform/bbk9588/         video/audio/input/vfs/clock/jit/libc shim
src/ui/                       触摸按键和状态栏
tools/                        多文件编译、链接、BDA 打包、模拟器部署
tests/                        host 单测、BDA 探针、公开 ROM 测试清单
docs/                         上游版本、许可证、真机验证记录
```

## 5. 实施阶段

### M0：仓库和构建链

- [x] 固定 SDK 与 gpSP 的确切 commit，记录到 `third_party/UPSTREAM.md`。
- [x] 建立多文件 C/C++/MIPS 汇编构建，不依赖当前单文件 `bda-pack` 编译入口。
- [x] C++ 使用 `-fno-exceptions -fno-rtti -fno-threadsafe-statics`，不链接 libstdc++。
- [x] 默认 `-msoft-float -mno-abicalls -fno-pic -G0`；只选择性链接必需的 libgcc helper。
- [x] 自定义 linker script，明确 `.text/.rodata/.data/.bss/.jit`，避免把大块零填充写进 BDA。
- [x] 先用两份 C、一个 C++、一份汇编构成 smoke BDA，验证入口、静态初始化、BSS 清零和退出。
- [x] 用 SDK validator 检查 header、checksum、入口和图标区。

退出条件：可重复构建并部署一个多文件 BDA，模拟器中连续启动/退出 10 次无资源泄漏。

### M1：可行性探针

- [x] **JIT/W^X 探针**：`m1_runtime` 在 heap 执行返回 42/43 的 MIPS 代码；实际 gpSP DRC
  也已在模拟器和真机持续执行，证明 BDA heap 可执行。
- [x] **cache 同步探针**：libgcc clear-cache 路径不可用后，已实现 JZ4740 32-byte D-cache
  writeback + I-cache invalidate 汇编；JIT 代码生成、改写和回收均已在模拟器与真机验证。
- [ ] **内存探针**：按 256 KiB/1 MiB 块逐步分配、写边界、逆序释放，分别记录模拟器和
  真机的稳定上限及碎片化行为。`m1_runtime` 已实现 256 KiB 分块和 2 MiB 碎片恢复测试，
  模拟器约 6.5 MiB 结果已记录，仍缺真机稳定上限。
- [ ] **计时探针**：读取 CP0 Count，并用 25 ms firmware tick 校准频率和漂移；确认能否作为
  高精度 pacing 时钟。`m1_runtime` 已实现采样，R44 正式应用也已只读 Count 并记录
  `perf_c25`；尚未确认真机漂移，不把它接入 pacing。
- [x] **显示探针**：验证 `240x160` raw RGB565 picture 提交；若该尺寸不稳定，回退到
  `240x160` VX + compatible context。
- [x] **音频探针**：测 1024-byte PCM block 的 ready/write 节奏、队列深度、退出 stop 和
  衰减恢复。

JIT/W^X 和 cache sync 是 dynarec 硬门槛，已由 R39 真机运行直接通过。4 MiB ROM cache 已因
真机死机被否决，正式版固定 2 MiB，因此 heap 上限探针只保留为平台诊断；CP0 Count 校准只在
引入新的高精度 pacing 时需要。两者都不是当前配置的发布阻断项。解释器只用于诊断，不把
“能启动但不可玩”当作完成。

### M2：gpSP headless bring-up

- [x] 先关闭 dynarec，用解释器验证 libretro 调用顺序、ROM 加载和一帧执行。
- [x] 实现最小 environment callback：RGB565、system/save directory、变量和 VFS。
- [x] 实现 freestanding shim：`memcpy/memmove/memset/memcmp`、字符串、alloc/free/calloc/realloc、
  必需的格式化和编译器 runtime helper。
- [x] 用 BDA VFS 替换 POSIX `fopen/stat/mmap/time` 路径，不移植完整 libretro-common OS 层。
- [x] 禁用首发不需要的 RFU/联机、cheat、rewind、ZIP/7z、录制、调试器和 savestate UI。
- [x] 默认使用 gpSP 自带开放 BIOS，不把 Nintendo BIOS 放入仓库或发布包。
- [x] 打开 MIPS dynarec，加入 `BBK9588` 专用 translation cache 分配和 cache sync。

BBK 专用 dynarec 约束：生成代码不能在宿主中断可抢占的状态下把 `$gp` 当 GBA `r13`
使用；所有返回 SDK/C 的边界必须恢复调用者 CP0 Status，返回 JIT 后再关闭中断。
JZ4740 只按 MIPS32 R1 构建，中断保护不得使用 R2 的 `di/ei/ehb`。运行时 JAL self-patch
在本平台保持禁用，构建必须编译打过补丁的 `mips/mips_stub.S`。

退出条件：公开 homebrew ROM 能选取、启动并稳定执行 300 帧，帧 hash 不全黑且无越界。

### M3：视频输出

- [x] gpSP RGB565 framebuffer 直接提交，不做中间 RGB888 转换。
- [x] 默认布局：顶部 `240x160` 为 GBA 画面，底部 `240x160` 为触摸控制区。
- [x] 优先使用 raw picture 原生尺寸提交；不稳定时使用一次 VX draw + 一次 guarded copy。
- [ ] 控制区已只在按键、声音或帧率状态变化时重新绘制，R41 删除了背景覆盖前对 76.8 KiB
  像素缓冲区的冗余 `memset`；每次变化仍提交整个 `240x160` 控制半屏，尚未缩小到单按键
  dirty rect。
- [ ] 真机默认 frameskip `1`：GBA 逻辑维持约 `59.7275 Hz`，每两帧渲染一帧，目标画面
  约 30 fps；R41 已实现触摸 `0/1/2` 运行期切换和动态 libretro 变量更新，跳帧时仍运行
  CPU、音频和输入，尚待模拟器和真机验证三档显示频率。
- [x] 验证 frame stop/release、compatible context free、end draw、close frame 的严格顺序。

退出条件：颜色、行距和画面方向正确；运行 10 分钟无 draw slot 泄漏、花屏或残影。

### M4：输入和触摸控制

- [x] 实体方向键映射 GBA D-pad，Enter 映射 A，Escape 短按映射 B。
- [x] 触摸区提供 A/B、L/R、Start/Select 和声音开关；A/B 也允许触摸，便于组合键。
- [x] Escape 短按作为 B，长按退出模拟器，避免 B 键和退出冲突。
- [x] 每个完整 GBA 帧在 libretro 宿主边界采样一次输入并更新 KEYINPUT；按键 IRQ 在随后进入
  `update_gba()` 时处理，SDK 输入函数不在 DRC guest 上下文中执行。
- [x] 处理触摸抬起、窗口失焦和退出时的全键释放，避免粘键。
- [x] 游戏运行阶段不使用窗口消息泵或 Window Timer；每次最多消费 8 条 raw 事件，坐标单独
  读取，首个 DOWN 前忽略残留 MOVE/UP；实体键只读取一次 6-byte packet。
- [x] 规避模拟器触摸包附带的伪 Escape 状态，并记录退出原因、最长 Escape/Start 按下时长
  和退出输入掩码，长按方向键不会误退出。

退出条件：方向+A/B、L/R、Start/Select 和至少两个常用组合键可稳定操作。

### M5：音频和节拍

- [x] 将 gpSP `65536 Hz stereo` 用整数相位累加器降采样到 `22050 Hz mono`。
- [x] 声音默认开启；关闭时继续向 PCM 写静音块维持相同队列背压和游戏节拍，触摸扬声器
  图标可即时切换真实采样与静音采样。
- [x] 左右声道采用饱和平均，输出 16-bit little-endian；不引入浮点运算。
- [x] 建立小型 ring buffer，以 1024-byte block 对接 `bda_audio_ready/write`。
- [ ] R42 将左右声道直接累计到降采样窗口，4-sample 窗口用移位、6-sample 窗口用带校正
  的定点倒数精确平均，并从 ring 直接提交连续 1024-byte 块，删除音频热路径整数除法和
  正常块复制；已完成源码实现，尚待音质、`aud_hz` 与真机 CPU 占用对比。
- [ ] R43 已实现相对系统音量的 100/75/50/25% 四档衰减：短按扬声器静音/恢复，长按切档，
  退出前用静音块恢复应用启动时的 attenuation；配置 v2 持久化当前档位并兼容 v1 开关值。
  尚待真机确认四档听感、短按/长按判定和返回系统后的音量恢复。
- [ ] 正常模式以音频队列背压为主 pacing，CP0 Count 为补充；静音模式只用校准时钟。
- [ ] 统计 underrun、short write、丢帧和每秒实际 emulated frames，写入可关闭的诊断日志。
  当前已有 short write、ring drop、背压，以及诊断包每 120 帧的 `emu_fps100`/`video_fps100`；
  R42 正式版恢复为每 600 帧记录，R43 构建参数允许 0/120/600，`0` 关闭运行期周期日志；
  R44 已用 CP0 Count 补齐核心、视频、重采样、PCM、ROM page I/O 和控制提交的分阶段耗时，
  仍缺 firmware underrun 事件。
- [x] 退出时写静音块应用原衰减值，再调用唯一公开安全的 `bda_audio_stop()`。

退出条件：连续 10 分钟无明显音高漂移、爆音和队列失控；返回系统菜单后无残留声音。

### M6：ROM、SRAM 和配置持久化

- [x] 用系统文件选择器筛选 `.gba`，不把 ROM 或商业 BIOS 放入仓库/发布包。
- [x] 保留文件选择器返回的绝对 ROM 路径，默认选择目录为 `A:\GAMEBOY\`。
- [x] `ROM_BUFFER_SIZE` 固定为 2 MiB；4 MiB 已在真机出现死机，不再提供构建或运行时选项。
- [x] 保持 ROM handle 打开，按 gpSP 的 32 KiB LRU 页面机制读取大 ROM。
- [x] SRAM/Flash/EEPROM 保存到 ROM 同目录或固定 save 目录。
- [x] SDK 暂无公开 rename/delete 原子替换时，使用 A/B 双槽、generation 和 CRC 恢复掉电中断写入。
- [x] 只在换 ROM 和正常退出时 checkpoint，并通过 CRC 跳过未变化的存档写入；运行热循环
  不扫描或写 NAND。
- [x] 用独立 `m6_save` BDA 对 128 KiB payload 验证两代 A/B 写入、最新槽截断回退和跨
  QEMU 冷重启恢复。
- [x] 用私有 Emerald fixture 在游戏内生成 Flash 1M 存档，验证 128 KiB 双槽 CRC、受控退出、
  QEMU 冷重启后的“继续游戏”和保存地点恢复。
- [ ] R43 已实现 frameskip、四档音量和实体 A/B 交换方案的 CRC A/B 双槽持久化；v2 可读取
  现有 v1 的声音开关并在下一次写入升级。ROM cache 固定为 2 MiB；尚缺配置槽损坏回退和
  跨版本迁移的动态验证。

退出条件：三种 save 类型可跨应用重启恢复，模拟中断写入后至少能回退到上一代有效存档。

### M7：性能优化和兼容性

- [x] cache 预算固定为 ROM page cache 2 MiB、ROM translation cache 2 MiB、RAM translation
  cache 384 KiB，并在 75% 时于帧边界统一回收；4 MiB ROM page cache 真机死机，已删除
  `-RomCacheMiB` 构建入口，不再扩大默认值。
- [x] R44 只读 CP0 Count，分别统计 CPU emulation 近似独占时间、ROM page I/O、video present、
  audio resample、PCM service 和 controls present；记录区间千分比及单次最大耗时，日志关闭时
  在正常退出汇总全程，不引入 firmware timer 或新的 IRQ 窗口。
- [ ] 优先减少文件换页和整屏提交，再优化热点 C 代码；不先写大范围平台汇编。
- [x] 构建脚本支持 `-Optimization Os/O2/O3` 与可选 `-Lto`，非默认变体写入独立目录；
  section GC 始终开启，不使用 `-ffast-math`。`-O2` 与 `-O3+LTO` 均已完成链接和 BDA 静态
  校验；LTO 构建对 freestanding libc 单独关闭 LTO，保留 `memcpy/memset` 的具体链接符号。
- [ ] 真机比较 `-Os/-O2/-O3+LTO` 的速度、体积和长期稳定性后再决定默认档位；R42 静态
  对比中三者大小分别为 850908、967708、1070492 字节，后两者明显更大，因此没有仅凭
  优化级别替换 `-Os`。R43 默认 `-Os` 为 851612 字节，真机对比前再重建同源码候选。
- [x] `-RuntimeLogIntervalFrames 0/120/600` 生成独立目录；`0` 仍保留首帧和正常退出汇总，
  但运行期间不再周期性打开、追加和关闭日志文件。
- [x] QEMU 只用于功能和 ABI；R29/R39 已在真机记录 fps、输入负载、音频生成率、ROM page
  和 JIT 回收数据。当前性能尚未达到退出条件，但不再用 QEMU fps 代替真机结论。
- [x] 按真机 30 fps 目标启用 frameskip 1，不牺牲音频和 GBA 逻辑时序。

退出条件：选定测试集真机 emulated speed 达到 98% 以上；默认 frameskip 1 时平均
rendered fps 达到 29 以上，长时间无崩溃；未达标时有明确热点数据。

### M8：回归、发布和合规

- [x] GitHub Actions 完成干净构建、显式 `bda-validate`、公开 fixture 校验、SHA-256、artifact
  与 tag Release；本地脚本自动部署模拟器专用 NAND、逐项运行 DRC headless BDA 并导出日志。
  2026-07-22 全部 10 项各执行 120 帧并通过；固件和 NAND 不进入仓库或 CI。
- [ ] 现有开放 ARM/Thumb/内存/基础 PPU/SRAM/Flash fixture 已接入 120 帧 DRC headless 回归，
  并固定人工检查过的最终 RGB565 画面、整段视频 hash、最终帧 hash、PC 与 CPSR 当前行为基线；
  R40 全部 10 项通过。仍需引入 mGBA test suite、自制 homebrew、EEPROM fixture，以及由独立
  已知正确实现生成的语义基线。
- [ ] 使用本机私有 `BPEE` Pokemon Emerald ROM 做 16 MiB ROM 分页、Flash 1M、RTC、
  音频、输入和长时间性能回归；只引用 `tests/roms/fixtures.local.psd1`，不提交 ROM。
  当前已完成 1795 次 ROM 分页、Flash 1M、基础音频、触摸/实体输入、冷启动恢复和约 6.1
  分钟真机运行；RTC 精度与 10/30 分钟性能仍待验证。
- [ ] 维护“模拟器通过 / 真机通过 / 已知失败”兼容矩阵。
- [x] 发布 workflow 和 v0.1.4 Release 只包含 `GBA.bda` 与 SHA-256，不含商业 ROM、Nintendo
  BIOS、BBK 固件或 NAND；私有 fixture 路径被 gitignore 排除。
- [x] 仓库保留 gpSP GPL-2.0 许可证、上游说明、完整补丁和构建脚本；每个 tag/Release 都有
  对应 GitHub 源码归档，可从干净 checkout 重建 BDA。
- [ ] 公开发布前确认 Apache-2.0 SDK inline headers 与 GPL-2.0-only gpSP 的组合许可边界；
  必要时取得 SDK header 的 GPLv2 双许可/例外，或在 GPLv2 平台层重新实现纯 ABI wrapper。

## 6. 关键验收标准

MVP 完成必须同时满足：

1. 从干净环境一条 PowerShell 命令可构建并验证 `.bda`。
2. 可通过文件选择器打开合法 `.gba`，不要求把完整 ROM 读入内存。
3. `240x160` RGB565 画面正确，实体键和触摸键覆盖全部 GBA 按键。
4. 音频为 22050 Hz/16-bit/mono，运行和退出都不死锁、不留残音。
5. SRAM/Flash/EEPROM 至少各有一个测试通过，并可跨重启恢复。
6. 模拟器连续运行 30 分钟无窗口、draw context、文件和 heap 泄漏。
7. 真机选定测试集 emulated speed 不低于 98%，默认 frameskip 1 时 rendered fps 不低于
   29；QEMU fps 不计入该性能结论。
8. JIT cache 刷新、应用退出和存档中断恢复均有独立回归探针。

## 7. MVP 明确不做

- ZIP/7z ROM、金手指、回放、录像、网络联机、RFU、rewind。
- 完整 savestate 管理 UI。
- 任意缩放、滤镜、旋转和 RGB888 渲染。
- 直接访问未公开的固定固件函数地址来“补停”音频或绕过窗口生命周期。
- 在没有真机数据前宣称全速或全兼容。

## 8. 下一轮开发顺序

1. 根据 R44 阶段字段确定优化顺序；ROM cache 固定 2 MiB，不切换默认优化级别，
   也不把 CP0 Count 接入 pacing。若控制层提交占比明显且 SDK 路径能实际缩小传输区域，再实现
   单按键 dirty rect；否则保留当前状态变化时整块提交，避免增加 staging copy。
2. 补公开 RTC/日历 API 后接入真实时间；接口未公开前继续使用当前单调运行时间，不调用固定地址。
3. 完成配置格式、性能日志字段和兼容矩阵文档，保持 v1/v2 配置读取兼容。
4. 用当前 R44 正式 BDA 真机复测 SRAM/Flash 游戏，再连续运行 10 分钟和 30 分钟，逐个测试
   方向、A/B/L/R、Start/Select、声音、帮助和换 ROM；日志应显示
   `INPUT_ARCH=RAW_EVENT_FRAME_BOUNDARY`、
   `WINDOW_EVENT_PUMP=DISABLED`，
   且 `raw_poll` 与核心帧数相同、`raw_max<=8`、音频 short/drop/backpressure 为 0。重点确认
   `rom_stage=0`、`rom_safe` 随运行期分页增长，并记录冻结前最后一行的 ROM page 与 JIT 字段。
5. 真机比较短按、拖动和长按触摸的响应时间，确认首个 DOWN 后 MOVE 连续到达、抬起不粘键，
   长按同一按钮时 `ctrl_draw` 不随 MOVE 持续增加，并验证实体 Escape 长按可以完成保存和正常退出。
6. 补 EEPROM fixture，并为现有 SRAM fixture 增加跨应用重启恢复；对 Emerald 的 Flash 1M
   最新槽做损坏回退测试，将真实游戏路径纳入可重复的存档回归。
7. 引入公开 homebrew 与 mGBA test suite，建立 CPU/PPU/DMA/timer/save 的解释器与
   dynarec 回归矩阵，不把商业 ROM 作为唯一正确性依据。
8. 验证 R44 的 20/30/60 FPS、四档音量、AB/BA 映射与配置跨启动恢复；比较默认和关闭周期
   日志两个构建的模拟速度；补精确 fps/underrun 指标，
   根据真机数据收敛 pacing。
9. 完成 30 分钟资源稳定性、十次启动/退出、许可证组合审查和可复现发布构建。

## 9. 参考资料

- [BBK 9588 BDA SDK README](https://github.com/HelloClyde/bbk9588-bda-sdk)
- [SDK 兼容性矩阵](https://github.com/HelloClyde/bbk9588-bda-sdk/blob/main/docs/compatibility.md)
- [SDK 游戏绘图 API](https://github.com/HelloClyde/bbk9588-bda-sdk/blob/main/docs/verified/game_rendering_api.md)
- [SDK Raw PCM API](https://github.com/HelloClyde/bbk9588-bda-sdk/blob/main/docs/verified/audio_pcm_api.md)
- [gpSP README 与 MIPS dynarec 说明](https://github.com/libretro/gpsp)
- [mGBA](https://github.com/mgba-emu/mgba)
- [NanoBoyAdvance](https://github.com/nba-emu/NanoBoyAdvance)
- [VBA-M](https://github.com/visualboyadvance-m/visualboyadvance-m)
