<!-- SPDX-License-Identifier: MIT -->
# Nexus 6P (angler) GPU plan: Adreno 430 on drm/msm

**Status (2026-10-18, late evening): G1-G5 pass; G6 resets on first GPU use.** Kernel branch
`omamobile-angler` in `~/android/mainline/linux-angler`, output
`~/android/mainline/out-angler-gpu` (the RAM-root rollback stays in
`out-angler-boot`). Goal: a render node (`/dev/dri/renderD128`) for the A430
next to simpledrm, so Hyprland and the shell render on the GPU instead of
llvmpipe (about 3.7 core-seconds per frame today, nexus_linux_plan.md).

The secure SMMU path now works: with the multimedia domains/clocks held,
`SECURE_PTBL_INIT` returns 0, the v1 secure context is left alone, and
`qcom-iommu` registers contexts ASID 0 and 2. With the GPU node enabled,
`msm 1.13.0` creates `card1`/`renderD128`; `a420_pm4.fw` and `a420_pfp.fw`
load. The first EGL/GBM offscreen clear resets the SoC after the IOMMU
resume and `restore_sec_cfg` return 0. No renderer string or frame yet.

**Latest handoff:** [GPU-HANDOFF.md](GPU-HANDOFF.md) is the entry point for
the next agent. It records the passing SMMU gate, the OCMEM stride (0037) and
SCM-owner (0038) fixes, the G5 render node and the G6 reset. Patches
0031-0038 are exported, uncommitted kernel edits; do not apply them again in
the current worktree. The phone was left recovered on the installed kernel
in running Arch.

## Facts

Sources: Google's angler kernel, `kernel/msm` `android-msm-angler-3.10-oreo-mr1`
(`33ace82f`, the 8.1 build the phone shipped with: `msm8994-gpu.dtsi`,
`msm8994-iommu.dtsi`, `msm-iommu-v1.dtsi`, `msm-gdsc.dtsi`,
`msm8994-regulator.dtsi`, `drivers/iommu/msm_iommu*-v1.c`, copies in
`~/android/reference/angler/kernel-src`), and mainline 7.3-rc7.

| Item | Downstream | Mainline |
| --- | --- | --- |
| GPU | `qcom,kgsl-3d0@fdb00000`, 0x40000 (overlaps the SMMU; KGSL maps only what it uses), SPI 33, chip id `0x04030000` | `a4xx_catalog.c`: `0x04030002`, `a420_pm4.fw`/`a420_pfp.fw`, GMEM 1.5 MiB. We map 0x10000 like msm8974's A330 |
| Clocks | `oxili_gfx3d_clk` (core), `oxilicx_ahb_clk` (iface), `oxili_rbbmtimer_clk` | same in `mmcc-msm8994`; the core clock's source is the RPM's (`RPM_SMD_GFX3D_CLK_SRC`, `CLK_SET_RATE_PARENT`), so a rate is an RPM vote |
| Frequencies | 600, 400, 300 (initial), 150, 27 MHz | OPP table with the same rates |
| Power | `gdsc_oxili_gx` (parent supply `pmi8994_s2_corner`) on `gdsc_oxili_cx` | `OXILI_GX_GDSC` (parent `OXILI_CX_GDSC`, `.supply = "VDD_GFX"`) |
| VDD_GFX | PMI8994 S2+S3, an RPM voltage corner | rpmpd `MSM8994_VDDGFX` exists. Sony Kitakami instead drives S2 as an SPMI regulator at 980 mV, always on, because the GPU takes one power domain. On our phone S2/S3 read **1.095 V, enabled** (regulator sysfs, read-only), left on by the bootloader/RPM |
| GMEM | OCMEM (`gpu-ocmem` bus votes) | `ocmem: sram@fdd00000` with a 1.5 MiB `gmu_sram` child, already in `msm8994.dtsi` |
| IOMMU | `kgsl_iommu@fdb10000`, `qcom,msm-smmu-v1`, secure id 18, context banks from 0x8000: `fdb18000` user (SIDs 0 and 1 on angler), `fdb19000` priv (none on angler), `fdb1a000` secure; NS context IRQ SPI 241 | `qcom_iommu` (`qcom,msm-iommu-v1`): TZ owns the global space, Linux programs only context banks after `qcom_scm_restore_sec_cfg(sec_id, ctx)`, the same call downstream makes. msm8916's GPU uses secure id 18 too |
| Firmware | | `qcom/a420_pm4.fw`, `qcom/a420_pfp.fw` in upstream linux-firmware (Arch Linux ARM `linux-firmware-qcom`); installed on the phone |

**Findings that set the order:**

- **drm/msm needs an IOMMU now.** `msm_iommu_new()` returns `-ENODEV` without
  one and `msm_gpu_init()` fails; the old VRAM carveout fallback is gone. The
  earlier idea of running without an IOMMU first doesn't work on this kernel.
- **`qcom_iommu` takes the context number from the bank's offset** (`reg /
  0x1000`) unless `qcom,ctx-asid` says otherwise, and passes it to the secure
  world. Downstream counts from the first bank at 0x8000, so the DT keeps the
  SMMU at `0xfdb10000` and gives `iommu-ctx@8000` `qcom,ctx-asid = <0>`.
- **Downstream also writes "BFB" TBU tuning registers** after the secure
  setup (`qcom,iommu-bfb-regs`, angler's own list in `msm8994-iommu.dtsi`);
  `qcom_iommu` has no equivalent. Probably performance only; if translation
  misbehaves, this is the first difference to look at.
- **Loading mmcc as it was would cut the display.** genpd keeps a GDSC that is
  on at boot on only until its provider's `sync_state`; with no consumer of
  `MDSS_GDSC`, that runs right after mmcc probes, and the bootloader's MDSS
  (which simpledrm and fbkick rely on) loses power. `pd_ignore_unused`
  doesn't cover `sync_state`. This is why `mmcc_msm8994` is blacklisted. The
  fix, as msm8992-xiaomi-libra does: the simplefb node in the DTS holds the
  MDSS clocks (AHB, AXI, VSYNC, MDP, and BYTE/PCLK/ESC for both DSI links)
  and `MDSS_GDSC`, and mmcc is built in so simpledrm still probes before the
  initramfs. `clk_ignore_unused` stays on the cmdline.
- **No msm8994 interconnect driver:** with no `interconnects` property
  drm/msm skips its bus votes. Memory bandwidth then stays wherever RPM has
  it; measure before worrying.
- **drm/msm binds a GPU-only device with `msm.separate_gpu_kms=1`**
  (`msm_gpu_no_components()`): the render node sits next to simpledrm, which
  keeps scanning out.

## Initial kernel changes (see Results for hardware status)

On `omamobile-angler`:

1. `dt-bindings: iommu: qcom,iommu: Add MSM8994`.
2. `arm64: dts: qcom: msm8994: Add the Adreno 430 GPU and its IOMMU`, both
   `disabled`: `gpu@fdb00000` (`qcom,adreno-430.2`, GX GDSC, OCMEM through
   `sram = <&gmu_sram>`, OPPs 27-600 MHz) and `gpu_iommu: iommu@fdb10000`.
3. `arm64: dts: qcom: msm8994-huawei-angler: Describe the bootloader
   framebuffer`, with the MDSS clocks and GDSC (replaces build-n1's `fdtput`
   node).

`device/angler/kernel.config`: `CONFIG_MSM_MMCC_8994=y`. `build-n1` takes
`ENABLE="<DT paths>"` to set disabled nodes `okay` in a test image only.

## Runbook

Every step is a one-shot `fastboot boot` of a test image (pre-approved by the
owner) unless it says otherwise; `boot` keeps `angler-n1.img`, so a hang or
reset comes back to the known Arch setup. Start each with the battery above
3.7 V, a kmsg stream to the laptop (`ssh ... dmesg -w > logs/...`) and
`device/angler/tools/cycle` to reach fastboot. **Announce first: G1-G3 can
blank or freeze the panel.** Change one thing per boot.

| Step | Image / command | Expect / Pass | Stop | Rollback | Risk |
| --- | --- | --- | --- | --- | --- |
| G0 (read-only, done) | regulator sysfs, `pm_genpd_summary`, config | S2/S3 1.095 V on; rpmpd bound; `DRM_MSM=m`, `QCOM_IOMMU=y`, `QCOM_OCMEM=m` | | | none |
| G1 | `out-angler-gpu/angler-n1.img` (no `ENABLE`) | panel keeps updating after boot; `pm_genpd_summary` shows `mdss_gdsc` on with the framebuffer device; `clk_summary` lists `oxili_*` off; no new errors from `mmcc`/simpledrm | panel freezes or goes dark (SSH still works: save `dmesg`, `clk_summary`, `pm_genpd_summary`) | `reboot-to bootloader` or a long Power press; `boot` is unchanged | moderate |
| G2 | on the phone: `pacman -S linux-firmware-qcom` (with the proxy) | `/usr/lib/firmware/qcom/a420_pm4.fw` present | | `pacman -R` | low |
| G3 | `ENABLE="/soc@0/iommu@fdb10000"` | `qcom_iommu` probes, no context fault; GX GDSC on while probing, then off | SoC hang (watchdog reset) or fault storm | as G1 | moderate |
| G4 | on the phone: `/etc/modprobe.d/omamobile-gpu-bringup.conf` with `blacklist msm` and `options msm separate_gpu_kms=1`; drop `ocmem` from the blacklist for this boot | the file in place | | delete the file | low |
| G5 | `ENABLE="/soc@0/iommu@fdb10000 /soc@0/gpu@fdb00000"`, then by hand under the log stream: `modprobe ocmem; modprobe msm` | `Initialized msm`, `/dev/dri/renderD128`, firmware loaded, no `hangcheck` | GPU hang/fault messages, SoC hang | `rmmod msm` if it answers; otherwise reset | high |
| G6 | `eglinfo -B` (surfaceless/GBM on renderD128), then `kmscube`-style offscreen test | renderer `FD430` (freedreno), frames without faults | as G5 | as G5 | moderate |
| G7 | Hyprland with the render node (`AQ_DRM_DEVICES` order: simpledrm for scanout, msm for rendering), the shell up | frame time and CPU against the llvmpipe numbers | regressions, GPU faults | restart SDDM without the setting | moderate |

Then, separately: VDD_GFX as a real consumer (rpmpd corner or the Kitakami
regulator), so RPM can't lower the rail under a running GPU; GPU devfreq
limits; and making it the default (package version bump, the `mmcc` and
`ocmem` blacklist lines go, `boot` flash with the owner's go).

## Results

### 2026-10-18 late evening: secure SMMU, render node, and the G6 reset

All tests ran `7.3.0-rc7-angler-gpu` with the watchdog active, `nowayout=1`,
a live feeder, SSH kmsg capture and a verified netconsole marker before
registering the IOMMU or loading modules. Images and checksums are in
[GPU-HANDOFF.md](GPU-HANDOFF.md).

- **MM power probe passes.** `angler-gpu-mm-power-probe2.img` held the five
  multimedia domains and 15 clocks; 14 report nonzero rates. Clock 121
  (`VENUS0_VCODEC0_CLK`) is rateless because MMCC never registers
  `vcodec0_clk_src`; 0034 tolerates only that known gate. The GPU SMMU's
  iface/bus were 60/27 MHz, the probe stopped before secure calls, and the
  holds released with `-11`. No fault.
- **Secure table initialization returns 0.** The same holds around
  `SECURE_PTBL_INIT` made `angler-gpu-mm-power-secure2.img` reach
  `table init returned 0` and release the holds (0). The SoC then reset. A
  log-only image (`secure3`, 0035) placed the reset at the secure context
  bank: the user ASID 0 probed cleanly, and `iommu-ctx@a000` reset between
  `parent resume returned 0` and `FSR cleared`.
- **v1 secure contexts are now treated as secured (0036).** Boot
  `angler-gpu-mm-power-secure4.img`: both contexts populated (ASID 0 and 2),
  `qcom-iommu` bound, `/sys/class/iommu/soc@0:iommu@fdb10000` appeared, and
  SSH/watchdog stayed healthy. The secure bank is not programmed by Linux.
- **OCMEM needed two fixes.** With the GPU node enabled, the first
  `ocmem_allocate()` oopsed with an alignment fault in `update_range`; 0037
  corrects the PSGSC control stride from 1 to 4 (vendor `ocmem_core.c` uses
  `PSCGC_CTL_IDX(x) = x * 0x4`). The lock then failed with `-EIO`; 0038 sets
  `ARM_SMCCC_OWNER_SIP` on the lock/unlock descriptors (vendor uses
  `SCM_SIP_FNID`). The test kernel's `ocmem.ko` was rebuilt and installed;
  the old file is backed up on the phone.
- **G5 passes.** `angler-gpu-g5-0038.img` created `card1` and
  `renderD128`; the node is bound to `adreno`, compatible
  `qcom,adreno-430.2`; `msm 1.13.0` initialized on minor 1. The GPU's
  `oxili_gx_gdsc` and `oxili_cx_gdsc` are on, and the GPU clocks idle at
  27 MHz core / 80 MHz iface.
- **G6 resets.** A small EGL/GBM offscreen clear opened `renderD128`; the
  kernel loaded `a420_pm4.fw` and `a420_pfp.fw`, then the IOMMU resumed,
  `restore_sec_cfg` returned 0, and the SoC reset. No context fault, GPU
  fault, hangcheck or oops reached the log; pstore was empty. The phone
  recovered to installed Arch. The next step is to trace the
  attach/context-programming and GPU boot path, not to repeat the test
  unchanged.

### 2026-10-18: powered secure-table test still loses USB

On `7.3.0-rc7-angler-gpu`, owner-approved one-shot boot of patch 0030's
`angler-gpu-g3late.img` succeeded. `tools/cycle`'s fast restart reached
fastboot; SSH returned 10 seconds after the boot command. The previous
Arch boot had not confirmed, so the initramfs moved its pending marker
aside and stayed in RAM. The watchdog was active with `nowayout=1`,
`timeout=5` and a running feeder. Both SSH `/dev/kmsg` capture and a verified
netconsole stream were in place before the late driver registration.

Registration reached `pgtable size: 151552` and the reserved address
`0x0e900000`, then SSH disconnected without returning from registration.
The same secure-call failure remains despite successful runtime-PM resume
and clock-enable return codes. Clock rates were not measured during the
call; powering other multimedia domains is still an untested hypothesis.
Full sanitized excerpt and the next diagnostic gate are in
[GPU-HANDOFF.md](GPU-HANDOFF.md#try-6-2026-10-18-730-rc7-angler-gpu).
No GPU render node has been demonstrated, and no installed boot image was
changed.

### Clock probe gate (completed; retained for reference)

Patch `0031-BRINGUP-iommu-qcom-Inspect-clocks-before-secure-table.patch`
is an uncommitted diagnostic diff on top of 0030 in the kernel worktree.
It adds the read-only boot parameter `qcom_iommu.ptbl_probe_only=1`.
After runtime resume it reports runtime-PM active state and clock-framework
rates, then returns `-EAGAIN` before CP pool sizing or secure table setup.
It intentionally leaves the IOMMU unbound. Clock-framework rates are not
measurements of the physical clock signals.

Built and subsequently tested: `~/android/mainline/out-angler-gpu/angler-gpu-probe-only.img`,
sha256 `83ebc839521a7ad2de26eb7c5c5884eddbcdc9c97c2211911aad88c0716f83a0`.
Checkpatch `--strict --no-signoff`: zero errors, warnings or checks.
`make ARCH=arm64 LLVM=1 O=~/android/mainline/out-angler-gpu LOCALVERSION=-gpu W=1 -j16 Image.gz qcom/msm8994-huawei-angler-rev-101.dtb`
completed; no warnings in the changed driver. The broader initial build
reported warnings in other files (including generated syscall tables and
`pmic-sec-write.c`); this is not a warning-free whole-tree build. The initial
module rebuild was stopped after the built-in driver compiled; no module
source or ABI changed in this diagnostic. The final image reuses the prior
test's DTB and initramfs: IOMMU `okay`, GPU `disabled`, table reservation
`0x0e900000` / `0x40000`. No DT source or bindings changed. Kernel memory
ends at `0x01ae8000`, below the DTB at `0x01e00000`; the packed image header
contains both diagnostic parameters. Hardware results follow below.

The reason to check: Google's GPL driver `msm_iommu_dev-v1.c:375-388`
calls `clk_round_rate(clk, 1000)` and `clk_set_rate()` when a core or
alternate clock has a zero rate. This bring-up path has no corresponding
rate selection. Whether this explains the failure remains unproven.

1. **Read-only, no risk:** verify the phone runs its installed kernel and
   battery exceeds 3.7 V. Confirm the previous boot has finished and no
   `/.omamobile/pending` marker remains. Stop if the phone is unreachable
   or battery is low; leave it on the known setup.
2. **Owner-approved one-shot boot, moderate risk:** announce the screen
   interruption, then run
   `device/angler/tools/cycle ~/android/mainline/out-angler-gpu/angler-gpu-probe-only.img`.
   The image must have `qcom_iommu.late=1` and
   `qcom_iommu.ptbl_probe_only=1`, only the IOMMU node enabled, and the GPU
   disabled. Expect SSH on `7.3.0-rc7-angler-gpu`; verify both parameters
   before continuing. Stop if either parameter is missing or false.
   Rollback is the unchanged installed image; ask before another reboot.
3. **Driver probe, moderate risk:** verify watchdog active, `nowayout=1`
   and a running feeder. Start a laptop kmsg stream and netconsole, confirm
   a marker arrives, then write `1` to
   `/sys/module/qcom_iommu/parameters/register`. Expect the clock report,
   `stopping before secure table calls`, and probe failure `-EAGAIN`.
   No secure-table size/address message should appear. Stop if it does,
   USB drops or faults appear. A zero required clock rate is a finding to
   investigate offline, not permission to continue to the secure call.
   Do not retry the driver or load `msm` in this boot. If it hangs, wait for
   recovery and ask the owner to check the screen before more commands.

### 2026-10-18: clock diagnostics and assigned-rate test

The owner authorized continued diagnostic boots for this session.

- The probe-only image passed on `7.3.0-rc7-angler-gpu`: runtime active `1`,
  interface `80000000`, bus `2147483647`, optional TBU `0` Hz. Registration
  returned after the deliberate `-EAGAIN`, with no secure-table calls. The
  bus rate is the RPM driver's initial software vote (`INT_MAX`), not a
  measurement of its physical frequency.
- A second image added `assigned-clocks = <&mmcc OXILI_GFX3D_CLK>` and
  `assigned-clock-rates = <27000000>` to the enabled IOMMU node. Image
  `angler-gpu-clock27.img`, sha256
  `4e3da049ca7e9c848dce4f8dc71a5d3637cc77518f4826bb1c75335313413ad8`.
  `W=1` Image/DTB build completed; no changed-driver warnings. `dt-validate`
  diagnostics were identical to the base DTB (including the pre-existing
  unsupported `memory-region` property added in 0029). Watchdog active,
  `nowayout=1`, feeder running, kmsg/netconsole captured before registration.
  Runtime active was `1`, interface `80000000`, but bus remained
  `2147483647` Hz. USB was lost after the reserved-table address again.
  **This did not test secure initialization at 27 MHz.**
- Source evidence for the failed assignment:
  `drivers/clk/qcom/clk-smd-rpm.c:348-349` returns success immediately when
  `r->enabled` is false, before saving `r->rate`. The next diagnostic sets
  `qcom_iommu.ptbl_bus_rate=27000000` after runtime resume and clock enable,
  checks the reported rate matches, and stops on a set-rate error or
  mismatch before any secure-table call. Risk remains high for a test
  that proceeds to secure-table initialization; the installed boot image
  remains the rollback. Keep netconsole and watchdog safeguards for it.
- `angler-gpu-clock27-enabled.img` (sha256
  `bb236b17508aad6126fa041b12d734879c58b527385b5579b60452eefca569cc`)
  sets the rate after enabling the clock. On `7.3.0-rc7-angler-gpu`,
  netconsole confirmed runtime active `1`, interface `80000000`, bus
  `27000000`, optional TBU `0` Hz. The last message was again the reserved
  table address, and USB was lost. Thus an explicit 27 MHz vote did not
  resolve the failure. Watchdog and both log streams were active. The
  preceding assigned-rate test recovered automatically to the installed
  kernel; Arch was `running`, uptime 121 seconds, battery 3.855 V.

### Buffer gate (completed, still resets)

Vendor `arch/arm64/mm/dma-mapping.c:205-220` zeroes its CMA allocation,
flushes the range and removes its linear mapping for
`DMA_ATTR_NO_KERNEL_MAPPING`. Mainline `kernel/dma/direct.c`'s
`dma_direct_alloc_no_mapping()` explicitly clears `__GFP_ZERO`; our
reserved-memory path also does not initialize its buffer. The next
diagnostic adds `qcom_iommu.ptbl_zero=1`, using a temporary write-combining
mapping to zero the reserved table, a write barrier and `iounmap()` before
the secure call. It retains `ptbl_bus_rate=27000000` to change one variable
from the last tested setup. This is an inference to test, not an
established requirement of TZ.

Risk: high (RAM initialization and a secure call that has reset the phone).
Before registration, verify parameters, watchdog/feeder, battery above
3.7 V and received netconsole marker. Expect `reserved table zeroed and
unmapped`, followed by secure-table initialization returning and context
bank probes. Stop on mapping failure, rate mismatch, a fault or loss of
USB; do not register again in that boot. Rollback remains the installed
image, and if automatic recovery fails, ask the owner to check the screen.

Tested `angler-gpu-zero.img`, sha256
`9ccd3d2602f889d039a1b5927e015779a7a88fc602f43e3c55392f0fc2518450`, on
`7.3.0-rc7-angler-gpu`. Arch was running and boot-confirmed before the
probe; watchdog and kmsg/netconsole were active. Runtime active `1`,
interface 80 MHz and bus 27 MHz were reported, then `reserved table zeroed
and unmapped`, then the reserved address and a reset. No return marker.
Recovery to installed `7.3.0-rc7-angler+` was verified over SSH: Arch
running, uptime 130 s, battery 3.844 V, no pending marker.

The first attempt to boot this image from RAM recovery did not reboot:
`cycle` required Python there. It now detects non-systemd and uses the
existing static `reboot-to` binary instead. The corrected path booted this
image successfully. ADC `vph_pwr` before that boot was 3.695 V; fuel gauge
on the test kernel was about 3.84 V. Keep the 3.7 V preferred gate and the
3.5 V minimum from NOTES; charge rather than assume the gauges agree.

### Other multimedia SMMU power (tested; superseded by the G5/G6 results)

0033 adds optional `qcom_iommu.ptbl_mm_power=1`. It uses MMCC/genpd and
clock APIs to hold Venus, JPEG, VFE, CPP and FD and the 15 SMMU clocks in
the GPL vendor DT. MDSS already has the framebuffer consumer and GPU has
the IOMMU runtime reference. There are no new DT nodes. The helper releases
the holds on return.

The probe-only image first stopped at `VENUS0_VCODEC0_CLK` (MMCC never
registers its `vcodec0_clk_src`); 0034 tolerates that one rateless gate and
the probe then passed: five domains, 15 clocks, `-11` release, no secure
call. With the same holds, the secure image reached
`table init returned 0`. The later v1-sec fix (0036) made the contexts
populate and the IOMMU register. The G5/G6 results above supersede this
gate; the remaining work is the first GPU use, not the SMMU setup.

### 2026-10-18: G0 and the G1 image (laptop only)

- G0 read-only checks as in the table; the phone was then sent to fastboot to
  charge (the session drains it faster than USB refills it).
- Kernel patches 0022-0024 (`device/angler/kernel-patches`), `dtbs_check`:
  no warnings on the new nodes (the remaining ones are older: haptics, LED
  trigger, fuel gauge); `dt_binding_check` for `qcom,iommu.yaml` clean;
  checkpatch: only the missing `Signed-off-by` (the owner signs off) and the
  `qcom,adreno-430.2` literal-match false positive (the binding's pattern
  covers it).
- G1 image: `7.3.0-rc7-angler+`, `out-angler-gpu/angler-n1.img` sha256
  `da37ce76…dc7dd`, image_size ends at `0x1ad8000`. Not booted yet.
- **Test images, one kernel** (`LOCALVERSION=-gpu`: release
  `7.3.0-rc7-angler-gpu`, so its modules install beside the everyday
  kernel's), in `out-angler-gpu`: `angler-gpu-g1.img` (no nodes enabled,
  sha256 `faf94566…5f27`), `-g3.img` (the IOMMU, `ca93568a…4fd5`), `-g5.img`
  (the IOMMU and the GPU, `57aa0e86…e7`). `device/angler/tools/gpu-check`
  reads each step's Pass items (kernel log, genpd, clocks, DRM) read-only.
- **Order when the phone is back in Arch on the everyday kernel:** 1.
  `KERNEL_OUT=~/android/mainline/out-angler-gpu device/angler/tools/modules-push`;
  2. `pacman -S linux-firmware-qcom` (proxy); 3.
  `/etc/modprobe.d/omamobile-gpu-bringup.conf` (`blacklist msm`, `options msm
  separate_gpu_kms=1`). None of these changes the everyday boot. Then G1, G3
  and G5 with `device/angler/tools/cycle <image>` and `gpu-check` after each.

### 2026-10-18 (afternoon/evening): G1 passes, G3 blocked (7.3.0-rc7-angler-gpu)

- **G1 passes:** mmcc built in, simplefb holding `MDSS_GDSC` and the MDSS
  clocks; loading `ocmem` (`8 ports, 3 regions, 32 macros, interleaved`) ran
  mmcc's `sync_state`, and `mdss_gdsc` stayed on with the framebuffer as its
  consumer. The console's text froze about 30 s into userspace on every boot
  (fbcon blanked, `fb0/blank` 4, unblank refused with `EBUSY`, no DRM
  client): not the clocks. Not yet explained.
- **Found on the way:** `qcom_fg` registered its battery (and thermal zone)
  before setting up SRAM access: an oops in `wait_for_common()` from the
  thermal worker (fixed, patch 0025). `kernel-modules-hook` deleted the
  everyday kernel's modules after a test-kernel boot (device-angler 0.1.10).
  A reboot from a test kernel once hung after `systemd-shutdown`'s SIGTERM,
  which also disarmed the watchdog (busybox's magic close): the init now sets
  `nowayout` (`CONFIG_WATCHDOG_SYSFS`), and netconsole (`CONFIG_NETCONSOLE`,
  UDP 6666 to the desktop, allowed on the USB interface in the desktop's ufw)
  with `initcall_debug` showed a clean shutdown on the next try. `cycle`
  now addresses only the angler's serial: another phone was in fastboot.
- **G3, first try:** `qcom-iommu-ctx ... probe failed with error -22`:
  `qcom_scm_restore_sec_cfg(18, 0)` refused, and each failure left the GPU
  clocks enabled (fixed, patch 0026).
- **The vendor kernel initializes the secure page table first**
  (`msm_iommu_sec_ptbl_init()` at boot, unconditionally); mainline does it
  only with a secure context bank, so the DT got `iommu-ctx@a000`
  (`qcom,msm-iommu-v1-sec`, ASID 2, angler's `gfx3d_secure`).
- **G3 with it resets the SoC.** Early, before USB, and ramoops kept nothing
  (the bootloader seems to clear the splash reservation). With the BRINGUP
  switch `qcom_iommu.late=1` (patch 0027) and netconsole:
  `iommu sec: pgtable size: 151552`, `pgtable at 0x00000000dde00000` (CMA,
  top of RAM), then a reset in `SECURE_PTBL_INIT`, no kernel message. Setting
  the CP pool size first as downstream does (patch 0028; TZ accepts it)
  changes nothing.
- **Open:** whether the secure world wants the table elsewhere (unmapped, or
  in the first DDR bank) or this SMMU isn't TZ-managed at all and needs the
  host-programmed mode of the 2026 msm8974 `qcom_iommu` series
  (non-TZ globals, BFB tables, power-collapse restore). Next: a no-map
  reserved region for the table; failing that, the msm8974 approach.
