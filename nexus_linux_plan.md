<!-- SPDX-License-Identifier: MIT -->
# Nexus 6P Linux and Omarchy Mobile feasibility plan

**Status (2026-10-18): N1 passes; N2a (text console) passes; N3a (Arch
root on userdata) passes; N3b (Omarchy Mobile shell on the panel) passes in
software rendering.** The test handset boots mainline Linux into Arch Linux ARM
on userdata with Omarchy and the Omarchy Mobile shell, autologged in through
SDDM into Hyprland on simpledrm, with touch detected, on one CPU, reached over
USB SSH, every other eMMC partition read-only. See the results at the end. No device support is
claimed. Proposed kernel branch: `omamobile-angler-boot`; proposed worktree:
`~/android/mainline/wt-angler-boot`; proposed output:
`~/android/mainline/out-angler-boot`. These are reserved names, not existing
build artifacts.

## Goal and decision

Determine whether a 2015 Huawei Nexus 6P (`angler`, Qualcomm Snapdragon 810 /
MSM8994) can run native Arch Linux ARM, Hyprland and Omarchy Mobile without
Android underneath.

The first useful target is a Wi-Fi pocket computer: reliable boot, USB recovery
access, display, touch, accelerated graphics and a usable mobile shell. Calls,
SMS, cameras, fingerprint and Android apps are later work. A working Linux
console is a milestone, but does not establish that Hyprland will work.

**Initial assessment:** plausible as a research port. Existing Linux work gives
us a starting point, but the graphics path and the boot/recovery safety model
must be established before adapting the installer. No completion date is
justified until those gates pass.

## Evidence and uncertainties

Here, **[V]** means verified in the cited source or repository file, **[R]**
means reported by another project without verification on our handset, and
**[I]** means an inference or proposal requiring validation.

| Finding | Evidence and implication |
| --- | --- |
| [V] The reference phone is the Pixel 7 Pro, not the standard Pixel 7. | [Device matrix](docs/DEVICES.md). The Nexus 6P is not currently supported. |
| [V] Angler has an upstream board DTS. | [Linux board source](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/msm8994-huawei-angler-rev-101.dts). It describes the board, reserved memory, buttons, UART and enabled SDHC storage. A DTS file is not proof of complete hardware support. |
| [R] postmarketOS lists screen, touch, Wi-Fi, USB networking and battery support. | [Angler device page](https://wiki.postmarketos.org/wiki/Google_Nexus_6P_%28huawei-angler%29), available revision dated 2025-05-29. The page discusses both downstream and mainline kernels; do not attribute the feature table wholesale to mainline. |
| [R] The downstream kernel lacks DRM support; mainline support is basic. | Same postmarketOS page. Its framebuffer workarounds for wlroots compositors do not establish compatibility with this project's Hyprland version. |
| [R] 3D acceleration is untested, audio is partial, and calls/SMS are broken in the documented port. | Same postmarketOS page. Recheck current source and reproduce results before relying on these statuses. |
| [V] The current image tools require Pixel-style boot partitions and slot B. | [Descriptor validation and image tooling](bin/omamobile), [installer](tools/install-phone), [cheetah descriptor](device/cheetah/device.conf). A new descriptor alone is insufficient. |
| [I] Much of the user interface can be reused. | Arch Linux ARM and the shell target aarch64, but hardware services, dependency choices, memory use and graphics still need review. |

Use upstream Linux, postmarketOS and this repository as citable implementation
sources. Pin exact commits and record source licences when implementation
begins. Firmware must come from the owner's own factory image; never put vendor
binaries, private captures or calibration data in this repository.

## Safety boundary: resolve before booting an experiment

The Nexus 6P uses an older, unslotted boot layout and eMMC storage. Confirm the
actual handset's partition map rather than assuming Pixel names or geometry.
There is no Pixel-style stock slot A fallback to rely on.

The existing [AGENTS.md](AGENTS.md) and [CONTRIBUTING.md](CONTRIBUTING.md) rules
remain in force. This document does **not** authorize an exception to their
slot-B-only write policy. Persistent installation is blocked until the human
explicitly approves a separate Nexus storage and recovery policy and reviewed
tooling implements it. Do not loosen `UFS_RW`, bypass validation, run an
unsuffixed flash command, or point the Pixel installer at this device.

Before any experimental boot:

- Confirm the owner's bootloader unlock state and recovery access. Unlocking
  erases data and requires its own explicit approval; it is not a prerequisite
  to silently perform during discovery.
- Keep the matching stock factory image and a tested recovery procedure on the
  laptop. Back up owner data and make read-only partition backups through an
  already authorized access path. If acquiring that access needs a boot,
  unlock, flash or other mutation, stop for a separate approved procedure.
- Identify the Nexus-specific modem NV and calibration partitions from actual
  evidence. Do not assume Pixel `efs` or `modem_a/b` names exist. Store backups
  and checksums outside the repository, and rebaseline after stock Android boots.
- Implement a fail-closed eMMC guard before enabling storage in the experimental
  kernel. For initial RAM-root experiments, expose no writable persistent
  storage. Cover whole disks, partitions and eMMC boot devices; leave RPMB
  untouched and do not enable userspace provisioning services.
- Use a laptop log stream, a validated watchdog and a recovery plan for risky
  driver probes. Validate Nexus watchdog behavior; cheetah watchdog and USB
  rescue scripts cannot be assumed to work. If the safety net cannot be
  established, stop rather than continuing blind.
- Ask before every reboot, temporary boot, overlay boot, flash or irreversible
  write. Announce screen, brightness, sound, haptics and shell changes first.
  Change one hardware subsystem per boot; new DT nodes stay disabled by default.

Never copy cheetah's DECON register writes, Exynos PMU accesses, Trusty/AoC
startup sequences or boot-success metadata writes onto the Nexus.

## Implementation stages

### N0: inventory and recovery design

Record the handset variant, installed build, lock state, storage layout, boot
image format, available recovery paths and hardware condition privately.
Record only sanitized conclusions in public notes. Inspect the current
postmarketOS device package, both kernel options and outstanding MSM8994 issues.
Produce a pinned source list and distinguish downstream-only features from
mainline features.

**Deliverable:** `device/angler/BOOT-PLAN.md`, an evidence-based partition and
recovery design, and a backup manifest stored off-repo. Move detailed subsystem
work into `device/angler/*-PLAN.md` as it begins.

### N1: minimal kernel and RAM-root boot

Start from upstream Angler support plus the smallest justified, licensed patch
set. Use a dedicated worktree/output and arm64 configuration with only the
hardware needed for diagnostics. Keep modem, audio DSP, cameras and unused
devices disabled. Package a minimal initramfs with PID 1 that cannot exit,
logging and USB networking where supported.

Investigate temporary boot support and the exact boot-image/DTB packaging
offline. Do not assume Pixel header versions, offsets or boot arguments apply.
Prefer a human-approved temporary boot if the bootloader supports it; otherwise
stop until a separately approved recovery and installation design exists.

**Pass:** repeatable Linux boot, kernel release recorded, laptop diagnostics,
validated recovery and no writable persistent storage.

### N2: display, input and graphics feasibility

Establish which display driver, panel support, GPU firmware and Mesa driver the
chosen kernel actually provides. The expected Qualcomm userspace direction is
Mesa Freedreno rather than Pixel Mali/Panthor; validate Adreno 430 support on
the selected versions and handset.

Check DRM connectors, modes, input events, EGL renderer and a minimal compositor
before starting the full shell. Bootloader framebuffer output alone does not
pass the accelerated graphics gate. Software rendering may help diagnosis but
does not count as accelerated support.

**Pass:** working display and touch through the intended Linux stack, verified
hardware rendering, and a minimal Hyprland session without repeated faults.
If only the downstream framebuffer path works, document that outcome and decide
whether to pursue driver development or a different desktop as a separate scope.

### N3: minimal Arch Linux ARM and Omarchy

Bring up a minimal aarch64 Arch userspace in RAM or over a validated network
root before proposing any persistent root filesystem. Record the RAM budget;
the documented 3 GB handset needs measurement with the shell and applications.

Audit [rootfs/packages.omamobile](rootfs/packages.omamobile) and device service
dependencies. Reuse the mobile shell and generic integrations selectively.
Exclude Tensor-specific audio, sensor, GNSS, fingerprint and modem services,
Mali-only components and their startup hooks. Do not install the complete
cheetah root image and hope unused hardware services remain harmless.

**Pass:** login, touch keyboard, launcher, terminal and one representative app
work in Hyprland with recorded memory use and no persistent-storage dependency.

### N4: device integration and persistence

Create `omamobile-device-angler` for validated board policy. Separate portable
shell behavior from device services. Add a device descriptor only alongside
reviewed support for its boot format, storage type and recovery model.

Persistent storage is a separate approval gate: define exactly which partition
would hold Linux, what owner data would be erased, what remains recoverable,
and how writes are restricted and verified. Add meaningful offline tests for
image packaging, partition rejection and storage-guard behavior. Existing Pixel
protections must remain intact. Do not add an installer entry or claim support
until the resulting runbook has passed on hardware.

### N5: useful handheld behavior

Validate Wi-Fi, battery reporting, thermal monitoring and charging first. Keep
charging changes supervised and require fresh backup checks before PMIC work.
Then bring up Bluetooth, audio, sensors and suspend independently. Establish a
safe speaker path before playing tones, with conservative limits based on Nexus
hardware rather than Pixel amplifier settings.

Measure idle power, charge behavior, temperatures, wake reliability and memory
pressure. Add hang markers and reversible service activation before enabling a
feature at boot. Cameras, GNSS, fingerprint, cellular and Waydroid remain separate
projects with their own feasibility and safety gates.

## Gated runbook

Only N0's laptop inspection is ready to run. Later rows define gates, not an
executable flashing recipe. Replace each deferred action with exact reviewed
commands, image hashes, paths and recovery instructions before requesting the
human's approval. Unknown device details are a stop condition.

| Gate / risk | Commands or action | Expect / pass | Stop condition | Rollback |
| --- | --- | --- | --- | --- |
| N0a / low | On laptop: `git status --short`; inspect `device/cheetah/device.conf`, `bin/omamobile`, `tools/install-phone` and `rootfs/packages.omamobile`. | Identify assumptions without changing files or contacting a device. | Unexpected local changes conflict with proposed work. | None; read-only inspection. |
| N0b / low | Human-approved existing access only: inventory Android with targeted read-only queries, or query fastboot while the human has already placed the handset there. Exact capture commands follow device identification. | Private inventory, source revisions and recovery design. | Unlock, reboot, boot or write needed; identifiers might reach public output. | End inspection; keep stock state. |
| N0c / moderate | Read-only backups through the approved access path; laptop `sha256sum -c SHA256SUMS` in the private backup directory. | Correct partition coverage, sufficient space and matching hashes. | Missing calibration/NV coverage, unreadable partition or mismatch. | Stop; retain originals and investigate. |
| N1a / low, laptop only | In the dedicated kernel worktree, run `scripts/checkpatch.pl --strict` on patches; build with explicit `O=...` and `W=1`; run binding checks and `dt-validate` on the built board DTB and overlays. | No new warnings, valid DT and inspected initramfs/image contents. | Failed checks, missing guard coverage or uncertain image layout. | Revise offline; do not deploy. |
| N1b / high | Deferred: separately approved temporary boot of the reviewed RAM-root image, with log capture and validated safety nets. | Linux access, recorded `uname -r`, read-only storage and recovery confirmed. | Lost diagnostics, storage writable, watchdog unavailable or unexpected reset. | Human uses the documented stock recovery path; do not issue blind commands. |
| N2 / moderate to high | Deferred: announce display changes; inspect DRM/input/EGL and run the minimal compositor under logging/watchdog. | Intended mode, responsive touch and hardware renderer verified. | GPU faults, unusable output, overheating or USB loss. | Stop the session over a fresh connection; if hung, human-approved return to known-good boot. |
| N3 / moderate | Deferred: launch the audited Arch/Hyprland environment without persistent writes. | Basic shell and applications usable within measured memory limits. | Device-specific services load unexpectedly, OOM loop or graphics regression. | Stop the test environment and return to the minimal RAM root. |
| N4 / high, blocked | No persistent write commands supplied. Requires explicit policy approval and a tested Nexus-specific tool/runbook. | Approved partition plan, offline tests and recovery path. | Any attempt to reuse the Pixel installer or bypass its guard. | Cancel installation; preserve the existing stock partitions. |
| N5 / high per subsystem | Deferred: one subsystem's approved diagnostic runbook at a time, with backup checks where required. | Repeatable subsystem-specific acceptance evidence. | New storage errors, unsafe temperatures, USB loss or unexpected firmware writes. | Disable the experiment; use its documented undo or human-approved known-good boot. |

## Results and acceptance record

### 2026-10-18: N0 inventory, unlock, first N1 RAM boot

- **N0b:** stock Android 8.1.0 (`OPP6.171019.012`, patch 2017-11-05),
  bootloader `angler-03.78`, radio `angler-03.85`, hardware revision
  `ANGLER-VN2`, 3 GB LPDDR4, 32 GB Samsung eMMC (`mmcblk0`, 44 partitions,
  unslotted) plus RPMB. Irreplaceable partitions: `modemst1`, `modemst2`,
  `fsg`, `persist`, `oeminfo`, `sec`. Private inventory in
  `~/android/reference/angler/`.
- **Unlock:** owner-approved, data wiped (nothing of value on the phone).
  `fastboot flashing unlock` → `unlocked: yes`. The owner chose to skip
  partition backups; `fastboot boot` of TWRP 3.7.0 (signature verified) gives
  a temporary root shell without flashing anything.
- **N1b, first boot: `dtb not found`.** The bootloader matches the appended
  DTB on `qcom,board-id`. Upstream tags `8026` (`0x1f5a`, "Angler Row VN1");
  this phone is `ANGLER-VN2`, board ID `0x1f57` (from the board IDs in TWRP's
  appended DTBs). Our image retags a copy of the upstream DTB with `fdtput`.
- **N1b, second boot: frozen Google logo, no USB.** The bootloader unpacks the
  kernel at `0x8000` and puts the DTB at `0x01e00000` and the ramdisk at
  `0x02000000` (the same offsets postmarketOS uses). arm64 defconfig's kernel
  is 40.9 MiB with BSS and overwrote both. `device/angler/kernel.config`
  builds Qualcomm platform support only and drops subsystems N1 doesn't use:
  25.9 MiB, ending at `0x019f8000`. Check `image_size` in the Image header on
  every config change.
- **N1b, third boot: pass.** `7.3.0-rc7-angler-n1+` (mainline `32c52ec87` plus
  the fragment, no patches). The USB gadget enumerated 2 s after the bootloader
  handed over: NCM + ACM, `ssh root@172.16.42.1`. 2.8 GB RAM visible. No eMMC
  driver and no block devices besides loop. Rollback is a long Power press;
  `fastboot boot` is one-shot, so the next boot is stock.
- **Only CPU0 runs.** The stock TrustZone doesn't implement PSCI (reports
  `v65535.65534`, `CPU_ON` returns -22), so `maxcpus=4` brings up one A53.
  Getting more cores needs a PSCI provider such as lk2nd (a flash, so its own
  approval).
- **Bootloader cmdline:** the stock bootloader appends the serial number and
  Wi-Fi/Bluetooth MAC addresses to the kernel command line. Never paste
  `/proc/cmdline` or the top of `dmesg` from this phone into the repository.

### 2026-10-18: N2a, text console on the bootloader's framebuffer

- **Pass (bootloader framebuffer only, not the N2 gate).**
  `7.3.0-rc7-angler-n1+`, image sha256 `ddc13131…fe54`. simplefb on the splash
  buffer (`0x03400000`, 1440x2560 `r8g8b8`) with fbcon in Terminus 16x32
  (90x80 characters); kernel messages are readable on the panel.
- The panel is command mode and nothing drives MDSS, so `fbkick loop 10` in
  the initramfs writes CTL0/CTL1 `START` to send each frame. Before that,
  `fbkick pattern` drew colour bars that showed in the expected order, which
  confirms the B, G, R byte order. The register map and quirks are in
  [device/angler/NOTES.md](device/angler/NOTES.md).
- The bootloader appends `console=NULL` after our `console=tty0`; kernel
  messages still reach fbcon.
- Next for N2: mainline MDSS/DSI with a panel driver for real DRM, then touch,
  then Adreno 430.

### 2026-10-18: unattended loop, eMMC, N3a Arch root

- **Layout now:** `boot` holds a known-good RAM-root image (N2 plus watchdog
  and `reboot-to`, no eMMC driver), `recovery` holds TWRP 3.7.0_9-0 (sha256
  `21e41c06…`, TeamWin GPG-verified). Stock Android's kernel is gone; system
  and vendor are untouched.
- **No buttons needed:** `reboot-to bootloader` (IMEM restart reason) reaches
  fastboot from Linux, and a test image goes in with `fastboot boot`. A test
  that crashes or hangs while busy resets into the known-good image.
- **Watchdog:** the APSS watchdog counts at full speed only while the CPU is
  busy. `-T 1` raced the feeder at boot and every such image was reset within
  seconds (bootreason `watchdog_nonsec`); `-T 5`, fed every second, is stable.
- **eMMC:** `sdhci-msm` with HS400 works: Samsung BGND3R, 29.1 GiB, 44
  partitions, as `mmcblk1`. The guard sets every partition and boot area
  read-only by `PARTNAME` except userdata, and removes the whole-disk node.
  It must not set the whole disk read-only: that makes userdata read-only too.
- **N3a: pass.** `7.3.0-rc7-angler+`. `install-arch-root --format` put the
  signed Arch Linux ARM aarch64 tarball (sha256 `42a4eeaa…b319`) on userdata
  (25 GB ext4) in 5 minutes over USB. The initramfs switches to it; systemd
  reached `running` with no failed units in 1 min 23 s, sshd answers on
  172.16.42.1, and `omamobile-boot-ok` cleared the dead-man marker. fbkick
  and the watchdog feeder carry on from the initramfs.

For each run, append the date, gate, kernel release
(`uname -r`), kernel commit, DTB/image hashes, package versions, expected and
actual behavior, sanitized relevant log lines, and rollback outcome. Keep full
logs and identifiers outside the public repository. Do not update the supported
device matrix from research alone.

The first decision point is N2: if repeatable safe boot and usable Hyprland
graphics cannot be demonstrated, pause Omarchy integration and scope the missing
kernel work explicitly. Passing N3 establishes a prototype; reliable charging,
thermal behavior, recovery and suspend are needed before calling it a useful
handheld. Telephony needs separate evidence before calling it a phone.

### 2026-10-18: N3b, Omarchy and the Omarchy Mobile shell

- **Pass, software-rendered.** `7.3.0-rc7-angler+` (kernel branch
  `omamobile-angler`: PM8994 regulators, touchscreen). SDDM autologs the user
  into Hyprland 0.56 on simpledrm (no GPU renderer: aquamarine finds none, and
  frames are software-rendered); fbkick pushes them to the command-mode panel.
  The Omarchy Mobile home screen (status bar, notifications, dock, launcher)
  shows on the panel, checked by webcam and `grim`.
- **Installed on the phone itself** (`device/angler/install-omarchy`): Omarchy
  4.0.4 from `[omarchy]` edge, Omarchy's own package list (all but `nvim` and
  `dotnet-runtime` are built for aarch64), and `[omamobile]` built on the
  desktop with `tools/build-packages` (signed with this fork's key, added to
  `omamobile-keyring`): `omamobile-omarchy` (13 patches applied, none upstream
  yet), `-shell`, `-apps`, `-owl-widgets`, `-mirror`, `-firstboot`, `neard`,
  `omacam`, and the new `omamobile-device-angler`. Not built: Mesa, Hyprland
  and libcamera (Arch Linux ARM's are used), and the Pixel-only packages.
- **Touch:** Synaptics S3718AR (RMI4 F12) registers as an input device; not
  yet tried by hand.
- **Found on the way:** `reboot=warm` (a hard reset loses the IMEM restart
  reason), modules in the Arch root, ufw's kernel needs, the usb0 DHCP rule,
  and DPMS off blanking simpledrm (details in `device/angler/NOTES.md`).
- **Next:** touch by hand; performance (one A53 at 1440x2560 in software);
  the msm DRM driver for the panel and the Adreno 430 for real rendering;
  SMP; battery, charging and Wi-Fi.

### 2026-10-18 (night): N4a, SMP through lk2nd: first try hangs

- **lk2nd builds and runs from RAM.** lk2nd `b9c4f7f` plus
  `device/angler/lk2nd-patches/` (this phone's board id 0x1f57), target
  `lk2nd-msm8994` (sha256 `bc0f614e…7c1a`). From fastboot, `fastboot boot
  lk2nd.img` starts it, and it answers as `product: lk2nd-msm8994`,
  `version: 0.5`. Nothing was flashed.
- **Booting our image through it hung.** `fastboot boot angler-n1.img` to
  lk2nd (kernel DT with `spin-table@62ff000` reserved, `enable-method = "psci"`
  for lk2nd to rewrite) left lk2nd's screen up and its USB device enumerated,
  but every fastboot command hung, and no Linux gadget ever appeared: lk2nd
  froze starting the kernel, or the kernel hung before its USB driver and
  before init arms the watchdog. No watchdog reset came, so the phone needs a
  long Power press.
- **Next, one question per boot:** (1) `fastboot oem log` on a fresh lk2nd
  for its view of the image; (2) the same image with SMP skipped (any custom
  `enable-method` makes lk2nd leave the CPUs alone), to see whether lk2nd can
  start our kernel at all; (3) an lk2nd built with `DEBUG_FBCON=1`, to read
  its log on the panel by webcam; (4) then SMP with ramoops read back through
  `fastboot oem ramoops raw`.

**N4a runbook (attended; each step a one-shot RAM boot, nothing flashed).**
Start: the phone in stock fastboot (`device/angler/tools/cycle` gets there
from Linux; after a hang, hold Power about 20 s, then Volume Down + Power).

| Step | Command | Expect / Pass | Stop | Risk |
| --- | --- | --- | --- | --- |
| 1 | `fastboot boot lk2nd.img`, then `fastboot oem log` | lk2nd's log; the board found | no lk2nd fastboot | low |
| 2 | `fastboot boot angler-nosmp.img` (CPUs `enable-method = "lk2nd,skip-smp"`) | Arch over SSH on CPU0: lk2nd can start our kernel | hang: lk2nd's kernel handoff, not SMP | low |
| 3 | lk2nd built with `DEBUG_FBCON=1`, then `angler-n1.img` | its log on the panel (webcam) up to the jump | | low |
| 4 | `angler-n1.img` (spin table) | `nproc` 8 | hang: read `fastboot oem ramoops raw` from lk2nd after the forced restart | moderate |

Rollback for every step is a long Power press; `boot` keeps the known-good
image, so the phone comes back to Linux with SSH.

