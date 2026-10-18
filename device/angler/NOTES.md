# Nexus 6P (angler) notes for agents

Hardware facts and gotchas found on the real phone (ANGLER-VN2, bootloader
angler-03.78), with the reason for each. Plan and results:
[nexus_linux_plan.md](../../nexus_linux_plan.md).

## 2026-10-18 late evening: GPU SMMU works, renderD128 exists, G6 resets

- **The GPU SMMU secure path now works.** Holding the five multimedia SMMU
  power domains and the 15 vendor clocks around `SECURE_PTBL_INIT` made the
  call return 0 (it had always reset the SoC before). `VENUS0_VCODEC0_CLK`
  is rateless because MMCC never registers `vcodec0_clk_src`; enabling that
  one gate without a rate is safe. The holds release cleanly.
- **Never touch a `qcom,msm-iommu-v1-sec` context bank from Linux.** Probe
  used to resume its parent and read/write `ARM_SMMU_CB_FSR`; that reset the
  SoC immediately after the secure table was initialized. Marking v1-sec as
  `secured_ctx` (0036), as v2-sec already was, leaves the bank to TZ. With
  that, contexts ASID 0 and 2 populate and `qcom-iommu` registers.
- **OCMEM needs the four-byte PSGSC stride.** Mainline used
  `0x3c + 1*i`; Google's GPL `ocmem_core.c` uses `PSCGC_CTL_IDX(x) = x*4`.
  On msm8994's three regions the second write was unaligned and oopsed in
  `update_range()`. 0037 fixes it.
- **OCMEM lock/unlock must use the SIP owner.** Mainline omitted
  `ARM_SMCCC_OWNER_SIP`; the availability check uses SIP, so it reported the
  call as present while the call itself returned `-EIO`. 0038 adds the owner.
  Google's driver uses `SCM_SIP_FNID`.
- **G5 passes.** With the GPU node enabled and `msm` loaded by hand,
  `msm 1.13.0` creates `card1` and `renderD128` beside simpledrm; the node
  is bound to `adreno` (`qcom,adreno-430.2`). `a420_pm4.fw` and
  `a420_pfp.fw` load. The GPU's GX/CX domains are on and its clocks idle at
  27 MHz core / 80 MHz iface.
- **G6 resets.** The first EGL/GBM offscreen clear loads both firmware
  files, resumes the IOMMU, gets 0 from `restore_sec_cfg`, and then the SoC
  resets. No fault or oops reached the logs; pstore was empty. The phone
  recovered to the installed kernel. The next step is to trace the
  attach/context-programming and GPU boot path, not to repeat the test
  unchanged. See [GPU-HANDOFF.md](GPU-HANDOFF.md).
- The only persistent test change is the rebuilt `ocmem.ko` in the
  `7.3.0-rc7-angler-gpu` module directory; the old file is backed up at
  `/tmp/ocmem.ko.before-0037` on the phone. No boot partition was changed.

## 2026-10-18: GPU SMMU secure-table test and recovery

- On `7.3.0-rc7-angler-gpu` (patch 0030), powering the GPU SMMU through
  runtime PM around secure-table initialization still lost USB at the secure
  call. Netconsole stopped after reporting the reserved table at
  `0x0e900000`; no registration return marker arrived. Successful clock
  enable calls do not establish nonzero or physically running clocks.
- The phone subsequently returned to the installed `7.3.0-rc7-angler+`
  kernel, Arch `running`, with its boot confirmation complete. The owner
  saw the login prompt. The reset source was not established. No installed
  boot image was changed. `tools/cycle`'s fast restart successfully reached
  fastboot for the owner-approved one-shot test boot.
- Vendor `msm_iommu_dev-v1.c:375-388` sets a clock rate if it starts at
  zero. Clock diagnostics subsequently passed without secure calls.
  `clk-smd-rpm.c` ignores set-rate while disabled, so the first DT-based
  27 MHz assignment did not take effect. Requesting 27 MHz after enabling
  the clock did take effect, but secure-table initialization still reset.
- Zeroing the reserved table through WC and unmapping it before the SMC
  also still reset. The phone recovered to installed Arch, running and
  boot-confirmed, battery 3.844 V. The multimedia-domain images were
  subsequently booted; the secure path, render node and G6 reset are in the
  late-evening section above. Start at [GPU-HANDOFF.md](GPU-HANDOFF.md) for
  exact state and next steps.
- `tools/cycle`'s Python fast path cannot work in RAM recovery, which has
  no Python. It now detects non-systemd and uses the existing static
  `reboot-to`, which restarts directly there. Both reboot paths have run.
  Let Arch's boot confirmation finish before a risky probe; otherwise an
  induced reset makes the next boot stay in RAM as designed.

## Boot

- **The bootloader picks the appended DTB by `qcom,board-id`:** upstream's DTB
  says 8026/0x1f5a and this phone is 0x1f57, which gives `dtb not found`.
  `build-n1` retags it (`BOARD_ID`).
- **The kernel must end below 0x01e00000** (`0x8000` + Image header
  `image_size`, BSS included). The bootloader puts the DTB there, and a bigger
  kernel overwrites it and hangs on the Google logo. `build-n1` checks this.
- **No PSCI:** the stock TZ's `CPU_ON` returns -22, and lk2nd has no msm8994
  CPU power-up either. Every CPU uses `spin-table` (release address at 0x88
  of a reserved page at `0x062ff000`). `msm8994-cpu-boot` (built in, an early
  initcall between `smp_prepare_cpus()` and `smp_init()`) sets the cores'
  boot address with a raw SCM call (`SET_ADDR_MC`, SMC32: SCM hasn't probed
  yet) and powers them up with Google's downstream sequence
  (`cpu_pwr_ctl.c`). They must be up before `smp_init()`: an A57 onlined
  later from an A53-only system is refused (`Detected conflict for
  capability 82 (Spectre-v2)`), since its errata weren't set up at boot.
- **The A57 cluster** needs its rail (L2 SAW `VCTL` 0xb8), its L2 and a
  clock (its mux resets to XO; the driver picks GPLL0, 600 MHz). It works
  (`msm8994_cpu_boot.cpus=0xfe`), but with no CPU idle states the four A57s
  idle in WFI at full clock and cost about 165 mA, which stalls charging on a
  500 mA USB port. The default is the A53 cluster until there's power
  collapse or A57 cpufreq.
- **Notification LED:** PMI8994 LPG channels 3/2/1 = red/green/blue. List
  the multicolor subnodes red first: power-supply triggers pass intensities
  in RGB order, not by colour id.
- **Vibrator:** PMI8994 haptics (LRA), driver from msm8996 mainline. Stock
  drives it at 1.2-1.8 V; the driver's own maximum is 3.6 V, so the DT
  caps it (`qcom,vmax-mv`).
- **lk2nd** needs this phone's board id (`lk2nd-patches/`); `fastboot boot
  lk2nd.img` works, but our kernel booted through it hung. Not needed now.
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
- **Boot with `reboot=warm`:** once `qcom-pon` lets the PMIC power-key driver
  probe, its reboot notifier makes every restart a PMIC hard reset unless
  `reboot_mode` is warm. A hard reset loses the IMEM restart reason: the
  phone powered off and came back with bootreason `charge`.
- **`switch_root` frees the initramfs:** a background loop that calls
  `sleep` or `grep` spins once they are gone (the USB self-recovery loop would
  reach its `sysrq c` at once). Stop them before switching; only builtins and
  already-running binaries (fbkick, watchdog) are safe after it.

## Power

- **Suspend (s2idle) takes USB down and nothing brings it back,** so the
  phone looks hung: dark screen, no gadget. Only a Power press wakes it.
  upower suspended it at a 2% fuel-gauge reading, and device-angler 0.1.7
  makes upower power off instead. Before calling something a hang, check
  `journalctl -b -1` for `PM: suspend entry`.
- **The charger starts at a 100 mA input limit** on a USB data port, and the
  gadget used to ask the host for 2 mA: the phone drained while plugged in.
  Ask for 500 mA (`MaxPower`) and set `qcom-smbchg-usb/input_current_limit`.
- **Don't reboot below about 3.5 V** (`qcom-battery/voltage_now`, or
  `vph_pwr` on PM8994 ADC channel 7): a reboot's load spike browned the phone
  out twice at 2.8 to 3.2 V. It then charges with the LED red and comes back
  on the image in `boot`.
- **The bootloader reports no battery values** (`fastboot getvar
  battery-voltage` is empty).

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

- **Kernel modules live in the Arch root** (`/usr/lib/modules/<release>`,
  `tools/modules-push`); the boot image stays under its size limit and the
  initramfs needs none. Push them with every kernel build.
- **Blacklist `mmcc_msm8994`, `ocmem`, `rmtfs_mem`** (omamobile-device-angler):
  udev would load them for existing DT nodes, and the multimedia clock driver
  could gate the bootloader-configured display.
- **ufw needs nftables, `nft_compat` and the xtables matches as modules**,
  `ip6t_rt` among them (`before6.rules` drops RH0 with `-m rt`). A failed ufw
  start left `deny incoming` with none of the allow rules, so SSH on usb0 was
  cut; the way back in was the USB serial console (`ttyGS0` getty, logged in
  as the user, from a container with `--device /dev/ttyACM0`).
- **The phone serves the desktop's address on usb0** (networkd DHCP server),
  so ufw must allow 67/udp there (`omamobile-usb-firewall.service`).
- **Omarchy's idle handling turns DPMS off:** Hyprland stops drawing, but
  there's no backlight device yet, so the panel keeps showing the last frame
  with the light on, and `grim` blocks waiting for a frame.
  `hyprctl dispatch 'hl.dsp.dpms({ action = "enable" })'` brings it back.
- **The touchscreen reports both axes flipped** (a top-left tap reads about
  (1317, 2444) of 1439x2559). Until the DT says `touchscreen-inverted-x/y`,
  the user's Hyprland config sets `input.touchdevice.transform = 2`.
- **Everything is drawn in software** (llvmpipe on simpledrm; four A53s at
  about 770 MHz with no cpufreq): about 1 fps at about 3.7 core-seconds per
  frame, and the Omarchy shell redraws all its windows nonstop. Measure with
  `perf` (installed on the phone; the kernel has no ftrace) and count
  `DRM_IOCTL_MODE_ATOMIC` with `strace` on Hyprland. Never `SIGSTOP`
  Hyprland or quickshell to get a quiet benchmark while the owner is using
  the phone: the screen freezes. And never restart the shell while it holds
  the lock.
- **Device packages add once-only user steps** in
  `/usr/lib/omamobile/setup-user.d/` (omamobile-shell 0.2.8+); angler's
  turns Omarchy's screensaver off.

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
