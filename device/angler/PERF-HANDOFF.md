<!-- SPDX-License-Identifier: MIT -->
# Nexus 6P performance handoff: start here

Updated 2026-10-18 (evening). For any agent picking up angler performance
work. Read [AGENTS.md](../../AGENTS.md) and the angler rules below first.
The CPU work (cpufreq, the A57s, thermal) has its own plan:
[CPU-PLAN.md](CPU-PLAN.md). The GPU: [GPU-HANDOFF.md](GPU-HANDOFF.md).

## Angler rules (on top of AGENTS.md)

- **One phone, one owner of it at a time.** Kernel code can be written and
  built in parallel; anything that reboots the phone, boots an image, pushes
  modules or restarts the session goes through the human, one agent at a
  time. Ask before every reboot, boot, flash or push; announce anything that
  changes the screen or the feel of the phone.
- **One kernel tree, no worktrees** (the owner's preference):
  `~/android/mainline/linux-angler`. Edit only your subsystem's files; use
  your own `KERNEL_OUT` if you build at the same time as someone else. A test
  image carries every uncommitted edit in the tree: say which ones in your
  results.
- Reboot/boot only with `device/angler/tools/cycle`; wait until
  `/.omamobile/pending` is gone before cycling, or the next boot stays in the
  RAM root. Don't reboot below about 3.5 V.
- Never print `/proc/cmdline` or the start of dmesg (serial number, MACs):
  filter `command line` and `androidboot` in every grep. Never commit
  identifiers or `logs/`.
- SDDM: `systemctl start sddm` only, never `enable`.
- Never touch `/dev/ttyACM0` (another device on this desktop).
- SSH: `ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
  -o LogLevel=ERROR root@172.16.42.1`.
- The GPU needs, after each boot of a GPU image: register the IOMMU, load
  `msm`, then `echo on > /sys/bus/platform/devices/fdb00000.gpu/power/control`
  (runtime suspend/resume freezes the SoC; GPU-HANDOFF). Never read
  `/sys/kernel/debug/dri/1/gpu`.

## Where the time goes (profile, 2026-10-18, 7.3.0-rc7-angler-gpu)

30 s of normal use (opening apps, scrolling) with Hyprland rendering on the
A430 through kmsro onto simpledrm. `perf record -a -g`, then `perf report
--sort comm` (data on the phone: `/root/perf.data`, `/root/trace-top.txt`).

| Share of 4 cores | What |
| --- | --- |
| 54% | CPU busy overall (four A53s at a fixed 768 MHz) |
| ~30% | Chromium (startup; about a quarter of it in `ld-linux`, plus fontconfig) |
| 5.6% | Hyprland |
| ~9% | quickshell (main, render and image-loader threads) |
| 5.3% | imv-wayland (`_IO_fread`) |
| 5.2% | squeekboard |
| 4.4% | `find` (a filesystem scan during normal use) |
| 3.5% | gtk-launch; 3.2% omamobile-launcher (Python) |
| 3.5% | kswapd0 (memory reclaim) |
| ~5.5% | simpledrm's kworkers: XRGB8888 → RGB888 conversion (`drm_fb_xrgb8888_to_rgb888_line`) and `memcpy_toio` of every frame |

GPU devfreq (1 Hz samples): 27 MHz 34×, 150 MHz 5×, 600 MHz 4×: **not
GPU-bound**. Memory: about 750 MB available after the run, 541 reclaim
stalls, about 8,300 major faults (no swap: program pages are dropped and
re-read from eMMC); quickshell's RSS 410 MB.

## App launches (2026-10-18 evening, A53 cpufreq C1, schedutil)

Measured on the phone with `/root/launch-time DESKTOP_ID` (tap-to-window
through `omamobile-launch`, polling `hyprctl clients`) and
`/root/launch-trace` (strace of the launcher and of Hyprland's execs, plus
`perf record -a -g`).

- **foot: about 1.3 s**, of which about 1 s is the launch chain: Python
  `omamobile-launch` startup 0.3 s, two `hyprctl` calls 0.13-0.18 s,
  `uwsm-app`/`systemd-run` 0.2 s, then **`gtk-launch` 0.4-0.8 s** (it
  initializes GTK3 to look up one desktop file; CPU-bound). `gio launch
  <path>` does the same through GIO in about 70 ms: with a patched copy of
  `omamobile-launch` the launch dropped from 1.20/1.29 s to 1.03/1.10 s.
  The governor hardly matters (performance vs schedutil: about 60 ms).
- **Chromium: 7.4 s cold, 3.1 s for a new window when already running.**
  About half the samples are Chromium's own startup code, 7% the dynamic
  linker, 5% kswapd/kcompactd; free memory fell to 66 MB.
- **Chromium renders in software:** its GPU process runs with
  `--use-gl=disabled`. With `--enable-logging=stderr`:
  `eglCreateContext: Requested version is not supported` → `ES version
  fallback is disabled` → `Exiting GPU process`. The cause: the FD430 reports
  **`GL_MAX_SAMPLES` 1** (Mesa freedreno has no MSAA on a4xx; probe
  `/root/gl-caps.c`), and ANGLE limits a GLES backend without 4x MSAA on the
  ES 3.0 renderbuffer formats to ES 2.0 (`renderergl_utils.cpp`,
  `GenerateCaps`); Chromium requires ES 3.0. `--use-angle=gl|gles|gles-egl`,
  `--ignore-gpu-blocklist`, `--disable-es3-gl-context`: all the same. Every
  Omarchy web app (WhatsApp, YouTube, Maps, ...) is Chromium.
- **MSAA is the only thing keeping Chromium off the GPU** (track M,
  2026-10-18). A test Mesa 26.2.4 (freedreno only, built in the ALARM
  container with `device/angler/mesa-patches/build-test-mesa.sh`, patch
  `mesa-patches/0001-freedreno-a4xx-fake-4x-MSAA-EXPERIMENT.patch`: claims
  2x/4x on render targets and depth, allocates single-sampled) installed
  apart in `/opt/mesa-msaa` (system Mesa untouched). `gl-caps` then reports
  `MAX_SAMPLES 4`, and Chromium's GPU process runs ANGLE on the A430 (no
  `--use-gl=disabled`, maps `/opt/mesa-msaa/lib/libgallium-26.2.4.so`,
  `renderD128` open; UI and pages draw correctly). Test harness on the
  phone: `/root/chr-msaa` (`MESA=1|0`, own profile in `/tmp/chr-msaa`, the
  phone's scale 0.83) with `/tmp/bench.html` (300 cards with gradients,
  shadows and a CSS animation, auto-scrolled 12 s by rAF):

  | Chromium | fps | median frame | p95 frame | first paint |
  | --- | --- | --- | --- | --- |
  | software (system Mesa) | 6.6 | 122 ms | 230 ms | 2.7-3.1 s |
  | GPU (test Mesa) | 12.4 | 67 ms | 87-96 ms | 2.2-2.3 s |

  `--enable-gpu-rasterization`, `--ignore-gpu-blocklist`,
  `--enable-zero-copy`: no change. During the GPU run the A430 sits at
  600 MHz (its top OPP) the whole time, so this page is now GPU-bound;
  CPU: Chromium main 33%, its Compositor thread 32%, simpledrm's frame
  conversion and copy 12% (track D). Fake MSAA means content that asks
  for MSAA is simply not antialiased; ES 3.0 has no `sampler2DMS`, so
  nothing reads individual samples.
- **zram** (`CONFIG_ZRAM=m`, in kernel.config; image `angler-cpu-c1c.img`
  carries it): 1.5 GB lz4, `vm.swappiness=100`, `page-cluster=0`, enabled by
  hand on the running kernel (not persistent yet). Cold Chromium then 6.2 s
  with 171 major faults (from 1,110), but zram held nothing yet: mostly a
  warmer page cache. Needs a longer multi-app comparison.

## Work tracks (independent unless noted)

| Track | Kind | Phone use | Notes |
| --- | --- | --- | --- |
| **CPU** (C1-C5) | kernel | reboots | In progress: [CPU-PLAN.md](CPU-PLAN.md). The biggest gain. |
| **T: tsens thermal** (CPU-PLAN C3) | kernel | one boot per test | Offline work in `drivers/thermal/qcom/tsens*.c` and `msm8994.dtsi`; parallel with CPU. Downstream `qcom,msm8994-tsens` at `0xfc4a8000` (reference: `~/android/reference/angler/kernel-src/msm8994.dtsi` around line 3112, Google's `drivers/thermal/msm8994-tsens.c`). Needed before the A57s run at speed. |
| **P1: zram** | userspace | none (no reboot) | zram swap (zstd, about half of RAM) via `zram-generator` in `omamobile-device-angler`. Measure major faults and reclaim stalls (`/proc/vmstat` `pgmajfault`, `allocstall*`) before and after. |
| **P2: who runs `find`, imv and gtk-launch** | userspace | read-only | `execsnoop`-style: `perf trace -e execve` or `strace -f -e execve` on the session; probably the launcher (`omamobile-launcher`) or the shell's wallpaper/app scan. Fix at the source (cache the result). |
| **P3: quickshell memory** | userspace | session restarts (announce) | 410 MB RSS: check decoded image sizes (wallpaper and icons at full resolution: `sourceSize`), QML caches. Owned by `packages/omamobile-shell`. |
| **P4: animations A/B** | userspace | config change (announce) | Hyprland `animations:enabled` off vs on; compare frame times (`hyprctl` / perf). |
| **P5: Chromium startup** | userspace | none | `ld-linux` relocation time: check flags in `chromium-flags.conf`, zygote/preload; maybe keep one instance warm (3.1 s vs 7.4 s per first window). |
| **P6: gio launch** | userspace | package install | `omamobile-launch`: `uwsm-app -- gio launch <desktop file path>` instead of `gtk-launch ID`: about 180 ms per launch. |
| **M: MSAA on a4xx in Mesa** | Mesa | package install | The fix for Chromium's software rendering: freedreno a4xx MSAA (the A430 hardware supports it; the vendor driver offered 4x), so `GL_MAX_SAMPLES` ≥ 4 and ANGLE offers ES 3.0. `packages/mesa` is ours (but builds only panfrost: angler needs freedreno added). **Experiment done: confirmed, about 2x scrolling fps** (above). Next: ship the fake-MSAA patch in a Mesa package for angler, then real a4xx MSAA (GMEM tiles sized for the sample count, `RB_MSAA_CONTROL`/`GRAS_SC_CONTROL` samples, resolve on `RB_COPY_CONTROL`). |
| **D: display driver** | kernel | reboots | msm's MDP5/DSI for the angler panel instead of simpledrm: removes the CPU conversion and copy of every frame (about 5% now, more with animation). Shares `drivers/gpu/drm/msm` with the GPU work: coordinate with whoever holds the GPU track. Large. |
| **G: GPU re-init freeze** | kernel | reboots | Why `a4xx_hw_init` times out when the GX domain never collapsed (GPU-HANDOFF). |

Phone-using tracks run one at a time; offline kernel work (T, D, G) can run
in parallel with them. Record each result in the track's plan with the date
and `uname -r`.
