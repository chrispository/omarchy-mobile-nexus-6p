<!-- SPDX-License-Identifier: MIT -->
# Nexus 6P (angler) GPU plan: Adreno 430 on drm/msm

**Status (2026-10-18): designed and built, nothing booted yet.** Kernel branch
`omamobile-angler` in `~/android/mainline/linux-angler`, output
`~/android/mainline/out-angler-gpu` (the RAM-root rollback stays in
`out-angler-boot`). Goal: a render node (`/dev/dri/renderD128`) for the A430
next to simpledrm, so Hyprland and the shell render on the GPU instead of
llvmpipe (about 3.7 core-seconds per frame today, nexus_linux_plan.md).

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
| Firmware | | `qcom/a420_pm4.fw`, `qcom/a420_pfp.fw` in upstream linux-firmware (Arch Linux ARM `linux-firmware-qcom`); not on the phone yet |

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

## Kernel changes (built, not run)

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
