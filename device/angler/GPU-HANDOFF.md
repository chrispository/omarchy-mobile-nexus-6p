<!-- SPDX-License-Identifier: MIT -->
# Handoff: the Nexus 6P (angler, MSM8994) GPU SMMU resets the SoC

A self-contained brief for whoever picks this up next. The full plan, with
the earlier steps, is [GPU-PLAN.md](GPU-PLAN.md); the safety rules are in
[AGENTS.md](../../AGENTS.md) and the phone's specifics in
[NOTES.md](NOTES.md). State as of 2026-10-18 evening.

## Goal

A render node for the Adreno 430 (drm/msm, `qcom,adreno-430.2`, firmware
`qcom/a420_pm4.fw` and `a420_pfp.fw`, installed) beside simpledrm, so
Hyprland and the Omarchy Mobile shell stop rendering in llvmpipe on four
A53s at about 770 MHz (about 1 fps, and the battery drains on USB power).

**The blocker:** drm/msm requires an IOMMU for the GPU (`msm_iommu_new()`
returns `-ENODEV` without one; the old VRAM carveout fallback is gone in
7.3), and bringing up the GPU's SMMU with mainline `qcom_iommu` resets the
SoC inside a secure-world call.

## Hardware and firmware facts

From Google's angler kernel, `kernel/msm` `android-msm-angler-3.10-oreo-mr1`
(`33ace82f`; copies of the files named here in
`~/android/reference/angler/kernel-src`, private):

- SMMU `kgsl_iommu@fdb10000` (0x10000), `qcom,msm-smmu-v1`,
  `qcom,iommu-secure-id = <18>`, context banks from 0x8000:
  `fdb18000` gfx3d_user (SIDs 0, 1 on angler), `fdb19000` gfx3d_priv (no
  SIDs on angler), `fdb1a000` gfx3d_secure (SID 2, `qcom,secure-context`,
  `linux,contiguous-region = <&secure_mem>`). NS context IRQ SPI 241; global
  IRQs SPI 229-232; angler overrides the BFB (TBU tuning) register list
  (`msm8994-iommu.dtsi`). Clocks `oxili_gfx3d_clk` (core),
  `oxilicx_ahb_clk` (iface); `vdd-supply` oxili_cx GDSC, alt the GX GDSC.
- Driver `drivers/iommu/msm_iommu-v1.c` and `msm_iommu_sec.c`: with a
  secure id, the globals belong to TZ. At attach it enables regulators, bus
  vote and clocks, then `scm_restore_sec_cfg(sec_id, ctx_num)` (ctx numbered
  from the first bank), then writes the BFB registers, then programs the
  context bank (TTBR, SCTLR) itself.
- **At boot (subsys_initcall), unconditionally,** `msm_iommu_sec_ptbl_init()`:
  if the MP service is version 1.1.1 or later,
  `IOMMU_SET_CP_POOL_SIZE(300 MiB, 0)`; then `IOMMU_SECURE_PTBL_SIZE`; then
  `dma_alloc_attrs(dummy dev, size, DMA_ATTR_NO_KERNEL_MAPPING)`, which in
  the vendor arm64 DMA code flushes the range and **removes it from the
  linear map** (`__dma_remap(page, size, 0, true)`); then
  `IOMMU_SECURE_PTBL_INIT(paddr, size, 0)`.
- GPU at `0xfdb00000`, SPI 33, `qcom,chipid = <0x04030000>`, 600/400/300/
  150/27 MHz, GMEM in OCMEM. VDD_GFX is PMI8994 S2+S3 as an RPM corner; it
  reads 1.095 V and on at boot (regulator sysfs).
- TZ uses the SMC32 convention (`qcom_scm: convention: smc arm 32`).

## What runs today

Kernel tree `~/android/mainline/linux-angler`, branch `omamobile-angler`
(7.3-rc7), exported as `device/angler/kernel-patches/0022-0030`; test build
output `~/android/mainline/out-angler-gpu`, release `7.3.0-rc7-angler-gpu`
(`LOCALVERSION=-gpu`, so its modules sit beside the everyday kernel's).

- DT (`msm8994.dtsi`, disabled by default; `build-n1 ENABLE=` turns nodes on
  in a test image): `gpu_iommu: iommu@fdb10000`,
  `"qcom,msm8994-iommu", "qcom,msm-iommu-v1"`, `ranges = <0 0xfdb10000
  0x10000>`, clocks iface `OXILICX_AHB_CLK`, bus `OXILI_GFX3D_CLK`,
  `power-domains = <&mmcc OXILI_GX_GDSC>`, secure id 18,
  `memory-region = <&iommu_sec_mem>` (no-map, `0x0e900000`, 256 KiB);
  `iommu-ctx@8000` (`-ns`, `qcom,ctx-asid = <0>`, SPI 241) and
  `iommu-ctx@a000` (`-sec`, `qcom,ctx-asid = <2>`, SPI 241). GPU node
  `gpu@fdb00000` with `iommus = <&gpu_iommu 0>`, `sram = <&gmu_sram>`.
- The display path passed (G1): simplefb in the angler DTS holds the MDSS
  clocks and `MDSS_GDSC`, mmcc is built in, and mmcc's `sync_state` no longer
  powers the display down. OCMEM probes.
- `qcom_iommu` changes: resume no longer leaks clocks when
  `restore_sec_cfg` fails (0026); BRINGUP `qcom_iommu.late=1` defers
  registration until `echo 1 > /sys/module/qcom_iommu/parameters/register`
  (0027); CP pool size set before the table, as downstream (0028); the table
  from `memory-region` when given (0029); the SMMU powered (runtime PM)
  around the table setup, with resume skipping `restore_sec_cfg` until the
  table exists (0030, **built, not yet run**: image
  `out-angler-gpu/angler-gpu-g3late.img`, sha256 `872285bb…`).

## Results so far

| Try | Setup | Result |
| --- | --- | --- |
| 1 | user context only, no secure context | `qcom-iommu-ctx fdb18000.iommu-ctx: probe ... failed with error -22`: `qcom_scm_restore_sec_cfg(18, 0)` refused (from `qcom_iommu_resume()`) |
| 2 | + secure context (so the driver does the secure page table) | SoC reset at boot, about 0.4 s, before USB; nothing in ramoops |
| 3 | 2 with `late=1`, netconsole | `iommu sec: pgtable size: 151552`, `pgtable at 0x00000000dde00000` (CMA, top of RAM), then a reset in `SECURE_PTBL_INIT`, no kernel message |
| 4 | 3 + CP pool size 300 MiB first | the pool call succeeds; the same reset |
| 5 | 4 + the table in the no-map region at `0x0e900000` | the same reset |
| 6 | 5 + SMMU powered around the call | not run yet |

## Hypotheses, most likely first

1. **TZ touches the SMMU (or another SMMU) during the table setup, and it is
   unpowered or unclocked:** an unclocked access on this SoC hangs the bus.
   Try 6 tests the GPU SMMU itself. If it still resets, TZ may touch every
   secure SMMU instance (MDP, VFE, venus, JPEG, CPP), all powered down here
   (their GDSCs read off); downstream ran its table init at
   subsys_initcall, maybe while the bootloader still had those on. A test:
   power the camera/venus/MDSS GDSCs and clocks around the call.
2. **The call's arguments or calling details differ** from what this TZ
   expects (arginfo `QCOM_SCM_ARGS(3, RW, VAL, VAL)`, the address as the RW
   argument, the spare value, the size), or TZ wants the table in a specific
   range (vendor `secure_mem` is a 300 MiB CMA region; the table there?).
   Compare `qcom_scm_iommu_secure_ptbl_init()` with the vendor `scm_call2()`
   path byte for byte.
3. **This SMMU shouldn't go through the secure path at all.** A 2026
   linux-arm-kernel series adds MSM8974 to `qcom_iommu` with a host-programmed
   mode (non-TZ globals, short-descriptor tables, BFB prefetch tables, power
   collapse restore) for SMMUs TZ doesn't manage, and notes
   `restore_sec_cfg is only accepted for the secured instances`. Angler's
   vendor DT gives this SMMU a secure id, so it should be secured, but the
   `-EINVAL` in try 1 deserves a second look (wrong context number? the
   vendor counts from 0x8000, and `qcom_iommu_resume()` always passes 0).
4. **Skip the IOMMU in drm/msm** for a first light: a local, not-for-upstream
   patch to let a4xx run with a no-IOMMU VM, as older kernels did, to prove
   the GPU, firmware, OCMEM and Mesa (freedreno) work at all while the SMMU is
   solved. The GPU would then reach physical memory unchecked; acceptable on
   this expendable test phone only, and only as a diagnostic.

## How to test without getting stuck

- One-shot boots only (`fastboot boot`, nothing flashed; `boot` keeps the
  everyday image). `device/angler/tools/cycle <img>` reboots to fastboot and
  boots it, **only ever to the angler's serial** (another phone is often in
  fastboot on this desktop; the serial is in
  `~/android/reference/angler/serial`, private). It now restarts with SysRq
  `u` + `b` after writing the IMEM restart reason, because a clean systemd
  shutdown hung twice after its final SIGTERM (on both kernels) and needed a
  long Power press. **The fast path is untested.**
- Live capture: netconsole to the desktop (UDP 6666 on the USB interface,
  allowed in the desktop's ufw; `socat -u UDP-RECV:6666 OPEN:logs/...`),
  configured on the phone through `/sys/kernel/config/netconsole`. Then
  register `qcom_iommu` by hand. ramoops at `0x04000000` doesn't survive:
  the bootloader seems to clear the splash reservation it sits in.
- `device/angler/tools/gpu-check` reads the state read-only. Never paste the
  kernel command line or the top of `dmesg`: the bootloader appends the
  serial number and Wi-Fi/Bluetooth MACs.
- Don't reboot below about 3.5 V. A wall charger charges far faster than
  the desktop's 500 mA port.
