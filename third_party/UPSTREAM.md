# Upstream dependencies

The BBK 9588 SDK is tracked as the `sdk` Git submodule. Other reproducible
upstream checkouts are placed in the ignored `.deps` directory by
`tools/bootstrap.ps1`; their exact revisions are recorded in `deps.lock.psd1`.

| Component | Revision | License | Purpose |
|---|---|---|---|
| [HelloClyde/bbk9588-bda-sdk](https://github.com/HelloClyde/bbk9588-bda-sdk) | `sdk` submodule | Apache-2.0 | Public BBK 9588 ABI headers, BDA packer and toolchain setup |
| [libretro/gpSP](https://github.com/libretro/gpsp) | `69e86ebe89f14c3f5f75b809c12c0a953b3d6ce4` | GPL-2.0 | GBA core and MIPS dynamic recompiler |
| [jsmolka/gba-tests](https://github.com/jsmolka/gba-tests) | `a7113b67e63f83a9b321696ddd7042ccfad6c881` | MIT | Redistributable ARM, Thumb, memory, PPU, SRAM and Flash regression ROMs |

No commercial ROM, Nintendo BIOS, firmware image, NAND image, or toolchain
archive is vendored in this repository.
