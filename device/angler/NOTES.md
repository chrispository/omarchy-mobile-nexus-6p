# Nexus 6P (angler) notes for agents

Hardware facts and gotchas found on the real phone (ANGLER-VN2, bootloader
angler-03.78), with the reason for each. Plan and results:
[nexus_linux_plan.md](../../nexus_linux_plan.md).

## Boot

- **The bootloader picks the appended DTB by `qcom,board-id`:** upstream's DTB
  says 8026/0x1f5a and this phone is 0x1f57, which gives `dtb not found`.
  `build-n1` retags it (`BOARD_ID`).
- **The kernel must end below 0x01e00000** (`0x8000` + Image header
  `image_size`, BSS included). The bootloader puts the DTB there, and a bigger
  kernel overwrites it and hangs on the Google logo. `build-n1` checks this.
- **Only CPU0 comes up:** the stock TZ has no real PSCI (`CPU_ON` returns -22).
  SMP needs lk2nd.
- **The bootloader appends the serial number and Wi-Fi/BT MACs to the
  cmdline:** never commit `/proc/cmdline` or the top of `dmesg`.
- **The container's `mkbootimg` is broken** (no `gki` module): use the host's.
- **Reach fastboot without buttons:** `reboot-to bootloader` writes
  `0x77665500` to the restart-reason word in shared IMEM (`0xfe87f65c`) and
  restarts (PS_HOLD, a warm reset); the bootloader reads and clears it.
  `recovery` is `0x77665502`. Alpine's busybox has no `devmem`.
- **The APSS watchdog (`0xf9017000`) counts about 176 times slower than the
  32768 Hz `qcom_wdt` assumes** (the count is `STS >> 1`). `-T 20` never bit
  in 30 minutes; `-T 1` bit 176 s after the feeder stopped and the phone came
  back from `boot`. So a driver second is about three real minutes. The bark
  IRQ never fired.
- **`switch_root` frees the initramfs:** a background loop that calls
  `sleep` or `grep` spins once they are gone (the USB self-recovery loop would
  reach its `sysrq c` at once). Stop them before switching; only builtins and
  already-running binaries (fbkick, watchdog) are safe after it.

## Storage, input, userspace

- **The eMMC is `mmcblk1`** (sdhci-msm, HS400 works). Find partitions by
  `PARTNAME` (`/dev/by-name/` from the initramfs), never by number.
- **Never set the whole eMMC disk read-only:** partitions inherit it, so
  userdata (the root) goes read-only too. Guard partitions one by one and
  remove the whole-disk node instead.
- **BAM DMA (`f9904000`, `f9944000`) fails to probe (-22),** and every QUP
  that lists `dmas` defers forever, silently (empty `i2c_qup` errors). The
  touch bus drops its `dmas`; QUP falls back to PIO.
- **Touch is a Synaptics S3718AR (RMI4, F12)** at 0x70 on BLSP1 QUP2: IRQ
  GPIO 77, reset GPIO 96, bus switch GPIO 38, L22 and L30 (kernel branch
  `omamobile-angler`, exported in `kernel-patches/`). The PM8994 regulators
  come from Petr Vorel's postmarketOS patch.
- **pacman 7 needs Landlock** for its download sandbox
  (`CONFIG_SECURITY_LANDLOCK`, `LSM` includes `landlock`).
- **Internet for the phone without sudo on the desktop:** keep
  `ssh -R 127.0.0.1:1080 root@172.16.42.1` open, and use
  `all_proxy=socks5h://127.0.0.1:1080` on the phone
  (`/etc/profile.d/omamobile-proxy.sh`). There's no RTC: set the clock from
  the desktop (`date -u -s @$(date +%s)`).

## Display (MDSS 1.9, dual-DSI command-mode panel)

- **The MDP is at MDSS + 0x1000** (`0xfd901000`), not + 0x100 as on 8974.
  The MDSS HW version at `0xfd900000` reads `0x10090000` (`msm8x94_config`).
- **What the bootloader leaves set up:** one 1440x2560 RGB888 buffer at
  `0x03400000`, stride 4320 (`0x10e0`). VIG0 scans the left 720 columns
  through CTL0 to INTF2, and VIG1 scans the right 720 (`SRC_XY` x = 720)
  through CTL1 to INTF3. Byte order in memory is B, G, R (DRM `RGB888`,
  simplefb `r8g8b8`); verified with colour bars.

  | Block | Address | Value left by the bootloader |
  | --- | --- | --- |
  | VIG0 / VIG1 `SRC0_ADDR` | `0xfd905014` / `0xfd907014` | `0x03400000` |
  | VIG `SRC_YSTRIDE0` | + `0x24` | `0x10e0` |
  | VIG `SRC_FORMAT` / `UNPACK` | + `0x30` / + `0x34` | `0x0002243f` / `0x00020001` |
  | CTL0 / CTL1 `LAYER` | `0xfd902000` / `0xfd902204` | VIG0 / VIG1 |
  | CTL0 / CTL1 `TOP` | + `0x14` | `0x21f20` / `0x21f30` (cmd mode, INTF2/3) |
  | CTL `START` | + `0x1c` | write 1 to send one frame |

- **Command mode: a frame goes out only when CTL_START is written.** No
  tear-check or autorefresh is set up (PP0-PP3 read 0), so framebuffer writes
  stay invisible until a kick. `tools/fbkick` writes START on both CTLs
  (`loop HZ` for a console); it refuses unless MDSS still holds the splash
  setup above. Angler only.
- **The buffer is no-map reserved memory** (`dfps-data@3400000` +
  `cont_splash_mem@3401000` in the DTS), so `/dev/mem` maps it as device memory:
  **an unaligned access is a bus error.** Write it with aligned 32-bit stores,
  never byte-wise `memcpy` of 3-byte pixels.
- **Map enough of MDSS:** VIG1 is at `+0x7000`. A short mapping silently read
  0 before it faulted.
- **simpledrm runs on the same buffer** (`/dev/dri/card0`, native RGB888,
  XRGB8888 emulated) and fbkick keeps pushing frames: a KMS device for
  Hyprland until the msm driver drives the panel.
- **`/dev/mem` works for this:** `STRICT_DEVMEM=y` without `IO_STRICT_DEVMEM`
  allows unclaimed MMIO and no-map memory. `tools/peek` is a read-only reader.
