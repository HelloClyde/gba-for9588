# BBK 9588 GBA 模拟器移植计划

> 分析日期：2026-07-20
> 目标平台：BBK 9588 `kj409588/C200`，JZ4740/MIPS32 little-endian
> SDK：[HelloClyde/bbk9588-bda-sdk](https://github.com/HelloClyde/bbk9588-bda-sdk)
> 本地验证环境：通过 `BBK9588_EMULATOR_ROOT` 指定的 BBK 9588 模拟器

## 0. 当前实现状态（2026-07-20）

当前已完成可构建、可运行 Emerald 并跨冷启动恢复存档的 gpSP/BBK9588 原型，但还不是可发布
版本。`[x]` 只表示代码已实现并在本地模拟器验证；凡是条目本身要求真机、公开测试 ROM
或长时间性能测试的，未完成前仍保持 `[ ]`。

已验证基线：

- SDK 固定到 `05f30f598668ee142a244e02c7258b6e913f0ff2`，gpSP 固定到
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
- 最新待真机复测包为 `GBA9588_AUDIO_R16.BDA`，菜单标题/窗口标题为 `GBA AUDIO R16`，
  固定 frameskip `1`、音频默认开启，共 `839836` 字节，SHA-256
  `894251a2cdb57f30e60cc7be31cf61bb34697e64324e377c78b0a274f4f7f2d0`。
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

1. **真机持续稳定性仍待验证。** R18 已修复模拟器可重复捕获的 DRC IRQ/GP 竞态并运行到
   32473 帧；真机仍需超过该帧数并完成 10/30 分钟、触摸、音频、正常退出和真实存档写入验证。
2. **音频供给和节拍尚未最终收敛。** R12 已证明 GUI control timer 能稳定唤醒事件循环，
   触摸也可用；但繁忙区间模拟速度降到 `33.80-42.10 fps`，推算音频生成速率低于固定的
   22.05 kHz 播放速率。SDK 没有公开 firmware underrun 计数，需用 R13 的 `aud_hz`、区间
   fps 和 checkpoint 耗时继续定位 CPU、渲染、事件服务与诊断 I/O 的占比，并补 10/30 分钟
   稳定性数据。
3. **公开正确性回归不足。** 尚未用 mGBA test suite、自制 homebrew 和 SRAM/EEPROM
   fixture 覆盖 CPU、PPU、DMA、计时和三类存档。
4. **模拟器测试 NAND 有 ECC 工具链限制。** QEMU direct-write 后可能留下未重算的 RS OOB
   校验，二次部署前需要模拟器自带 `stamp_nand_ecc.py` 修复；这不是 BDA A/B 数据 CRC 失败。

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

### M1：六个硬性可行性探针

- [ ] **JIT/W^X 探针**：heap 写入返回常量 42 的 MIPS 函数，刷新 cache 后执行；改写为
  43，再刷新并执行。模拟器和真机都必须通过。
- [ ] **cache 同步探针**：验证 `__builtin___clear_cache` 是否可链接和生效；不可靠时实现
  JZ4740 的 D-cache writeback + I-cache invalidate 汇编。
- [ ] **内存探针**：按 256 KiB/1 MiB 块逐步分配、写边界、逆序释放，分别记录模拟器和
  真机的稳定上限及碎片化行为。
- [ ] **计时探针**：读取 CP0 Count，并用 25 ms firmware tick 校准频率和漂移；确认能否作为
  高精度 pacing 时钟。
- [x] **显示探针**：验证 `240x160` raw RGB565 picture 提交；若该尺寸不稳定，回退到
  `240x160` VX + compatible context。
- [x] **音频探针**：测 1024-byte PCM block 的 ready/write 节奏、队列深度、退出 stop 和
  衰减恢复。

硬门槛：JIT 探针失败时，不继续投入完整 gpSP dynarec 移植；先定位权限/cache 原因。
解释器只用于诊断，不把“能启动但不可玩”当作完成。

### M2：gpSP headless bring-up

- [x] 先关闭 dynarec，用解释器验证 libretro 调用顺序、ROM 加载和一帧执行。
- [x] 实现最小 environment callback：RGB565、system/save directory、变量和 VFS。
- [x] 实现 freestanding shim：`memcpy/memmove/memset/memcmp`、字符串、alloc/free/calloc/realloc、
  必需的格式化和编译器 runtime helper。
- [x] 用 BDA VFS 替换 POSIX `fopen/stat/mmap/time` 路径，不移植完整 libretro-common OS 层。
- [x] 禁用首发不需要的 RFU/联机、cheat、rewind、ZIP/7z、录制、调试器和 savestate UI。
- [x] 使用 gpSP 自带开放 BIOS 替代方案；可选支持用户自行提供、合法取得的 GBA BIOS。
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
- [ ] 控制区只在按键状态变化时重新提交；当前仍提交整个 `240x160` 控制半屏，尚未缩小到
  单按键 dirty rect。
- [x] 真机默认固定 frameskip `1`：GBA 逻辑维持约 `59.7275 Hz`，每两帧渲染一帧，
  目标画面约 30 fps；跳帧时仍运行 CPU、音频和输入。后续再增加 `0/1/2` 配置界面。
- [x] 验证 frame stop/release、compatible context free、end draw、close frame 的严格顺序。

退出条件：颜色、行距和画面方向正确；运行 10 分钟无 draw slot 泄漏、花屏或残影。

### M4：输入和触摸控制

- [x] 实体方向键映射 GBA D-pad，Enter 映射 A，Escape 短按映射 B。
- [x] 触摸区提供 A/B、L/R、Start/Select 和声音开关；A/B 也允许触摸，便于组合键。
- [x] Escape 长按打开退出确认或退出菜单，避免 B 键和退出冲突。
- [x] 输入在每个 `retro_run()` 前只采样一次，保留同一帧的组合键状态。
- [x] 处理触摸抬起、窗口失焦和退出时的全键释放，避免粘键。
- [x] 事件泵每帧最多消费 4 个真实事件；实体键消息由 frontend 消费，原始触摸仍交给
  默认窗口过程转换，避免方向键穿透到桌面或触摸事件被饿死。
- [x] 规避模拟器触摸包附带的伪 Escape 状态，并记录退出原因、最长 Escape/Start 按下时长
  和退出输入掩码，长按方向键不会误退出。

退出条件：方向+A/B、L/R、Start/Select 和至少两个常用组合键可稳定操作。

### M5：音频和节拍

- [x] 将 gpSP `65536 Hz stereo` 用整数相位累加器降采样到 `22050 Hz mono`。
- [x] 声音默认关闭；关闭时继续向 PCM 写静音块维持相同队列背压和游戏节拍，触摸扬声器
  图标可即时切换真实采样与静音采样。
- [x] 左右声道采用饱和平均，输出 16-bit little-endian；不引入浮点运算。
- [x] 建立小型 ring buffer，以 1024-byte block 对接 `bda_audio_ready/write`。
- [ ] 正常模式以音频队列背压为主 pacing，CP0 Count 为补充；静音模式只用校准时钟。
- [ ] 统计 underrun、short write、丢帧和每秒实际 emulated frames，写入可关闭的诊断日志。
  当前已有 short write、ring drop、背压，以及诊断包每 120 帧的 `emu_fps100`/`video_fps100`；
  25 ms 时钟只能提供区间平均值，尚缺 firmware underrun 事件和可关闭的日志配置。
- [x] 退出时写静音块应用原衰减值，再调用唯一公开安全的 `bda_audio_stop()`。

退出条件：连续 10 分钟无明显音高漂移、爆音和队列失控；返回系统菜单后无残留声音。

### M6：ROM、SRAM 和配置持久化

- [x] 用系统文件选择器筛选 `.gba`，不把 ROM 或商业 BIOS 放入仓库/发布包。
- [x] 保留文件选择器返回的绝对 ROM 路径，默认选择目录为 `A:\GAMEBOY\`。
- [x] 初始 `ROM_BUFFER_SIZE` 设为 2 MiB；内存探针允许时提升到 4 MiB。
- [x] 保持 ROM handle 打开，按 gpSP 的 32 KiB LRU 页面机制读取大 ROM。
- [x] SRAM/Flash/EEPROM 保存到 ROM 同目录或固定 save 目录。
- [x] SDK 暂无公开 rename/delete 原子替换时，使用 A/B 双槽、generation 和 CRC 恢复掉电中断写入。
- [ ] 在退出、显式菜单保存和定时 dirty checkpoint 时刷写，避免每帧写 NAND。
  当前退出和每 5 秒 dirty checkpoint 已实现并接入生产 frontend；显式保存菜单尚未实现。
- [x] 用独立 `m6_save` BDA 对 128 KiB payload 验证两代 A/B 写入、最新槽截断回退和跨
  QEMU 冷重启恢复。
- [x] 用私有 Emerald fixture 在游戏内生成 Flash 1M 存档，验证 128 KiB 双槽 CRC、受控退出、
  QEMU 冷重启后的“继续游戏”和保存地点恢复。
- [ ] 首发配置只保留 frameskip、音量衰减、按键方案和 ROM cache 大小。

退出条件：三种 save 类型可跨应用重启恢复，模拟中断写入后至少能回退到上一代有效存档。

### M7：性能优化和兼容性

- [ ] 初始 9588 cache 预算：ROM page cache 2 MiB、ROM translation cache 1~1.5 MiB、
  RAM translation cache 256 KiB；按探针结果调整。
- [ ] 分别统计 CPU emulation、ROM page fault、video present、audio resample 的耗时。
- [ ] 优先减少文件换页和整屏提交，再优化热点 C 代码；不先写大范围平台汇编。
- [ ] 用 `-O3`、section GC 和可验证的 LTO 对比，不默认启用 `-ffast-math`。
- [ ] QEMU 只验证功能和 ABI；最终 fps、输入延迟、音频稳定性必须在真机测量。
- [x] 按真机 30 fps 目标启用 frameskip 1，不牺牲音频和 GBA 逻辑时序。

退出条件：选定测试集真机 emulated speed 达到 98% 以上；默认 frameskip 1 时平均
rendered fps 达到 29 以上，长时间无崩溃；未达标时有明确热点数据。

### M8：回归、发布和合规

- [ ] 自动构建、`bda-validate`、部署到模拟器专用 NAND 副本并导出诊断日志。
- [ ] 使用开放测试 ROM、mGBA test suite 和自制 homebrew 做 CPU/PPU/DMA/save 回归。
- [ ] 使用本机私有 `BPEE` Pokemon Emerald ROM 做 16 MiB ROM 分页、Flash 1M、RTC、
  音频、输入和长时间性能回归；只引用 `tests/roms/fixtures.local.psd1`，不提交 ROM。
  当前已完成 ROM 分页、Flash 1M、基础音频、触摸/实体输入和冷启动恢复；RTC 精度与长时间
  性能仍待真机验证。
- [ ] 维护“模拟器通过 / 真机通过 / 已知失败”兼容矩阵。
- [ ] 发布物不含商业 ROM、Nintendo BIOS、固件和未经授权 NAND。
- [ ] 保留 gpSP 的 GPL-2.0 版权与许可证，并随 BDA 发布对应完整源码和构建脚本。
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

1. 用 `GBA9588_AUDIO_R16.BDA` 真机复测 window timer：启动日志必须显示
   `WINDOW_TIMER_START=PASS`、`WINDOW_TIMER_EXISTS=1`、`WINDOW_TIMER_PERIOD_MS=10` 和
   `EVENT_PUMP_FRAME_INTERVAL=1`。逐个测试方向键、A/B、Start、Select 和声音开关；重点比较
   R14 的 `emu_fps100=4660-5240/aud_hz=17204-19345/evt_ms=14425`。先运行超过 6240 帧，
   再用实体 Escape 正常退出；尾部必须显示 `WINDOW_TIMER_STOP=1` 和完整 PASS。若 timer 消息
   接近 pump、`evt_ms` 明显下降且 `aud_hz` 接近 22050，则保留 window timer；若启动即冻结或
   stop 失败，回退 R14 并提交真机日志给 SDK 继续收窄 10 ms 投递边界。
2. 分别补 SRAM 与 EEPROM fixture，并对 Emerald 的 Flash 1M 最新槽做损坏回退测试，
   将真实游戏路径纳入可重复的存档回归。
3. 引入公开 homebrew 与 mGBA test suite，建立 CPU/PPU/DMA/timer/save 的解释器与
   dynarec 回归矩阵，不把商业 ROM 作为唯一正确性依据。
4. 实现 frameskip、音量、按键方案和 ROM cache 的最小配置；补精确 fps/underrun 指标，
   根据真机数据收敛 pacing 和控制层 dirty rect。
5. 完成 30 分钟资源稳定性、十次启动/退出、许可证组合审查和可复现发布构建。

## 9. 参考资料

- [BBK 9588 BDA SDK README](https://github.com/HelloClyde/bbk9588-bda-sdk)
- [SDK 兼容性矩阵](https://github.com/HelloClyde/bbk9588-bda-sdk/blob/main/docs/compatibility.md)
- [SDK 游戏绘图 API](https://github.com/HelloClyde/bbk9588-bda-sdk/blob/main/docs/verified/game_rendering_api.md)
- [SDK Raw PCM API](https://github.com/HelloClyde/bbk9588-bda-sdk/blob/main/docs/verified/audio_pcm_api.md)
- [gpSP README 与 MIPS dynarec 说明](https://github.com/libretro/gpsp)
- [mGBA](https://github.com/mgba-emu/mgba)
- [NanoBoyAdvance](https://github.com/nba-emu/NanoBoyAdvance)
- [VBA-M](https://github.com/visualboyadvance-m/visualboyadvance-m)
