<!-- SPDX-License-Identifier: MIT -->
# Nexus 6P GPU handoff: start here

Updated 2026-10-18 (night): 0039 confirmed on the phone; G6 now faults at the ringbuffer; 0040 built, not yet booted. Latest results: [GPU-PLAN.md](GPU-PLAN.md#results). Read
[AGENTS.md](../../AGENTS.md), [NOTES.md](NOTES.md) and
[GPU-PLAN.md](GPU-PLAN.md) before hardware work.

## Current state and next action

**The GPU SMMU works and the GPU render node exists. The first real GPU use
still resets the phone.** Secure page-table initialization now returns 0 with
the multimedia SMMU domains and clocks held, the v1 secure context is left to
the secure world, and `qcom-iommu` registers with contexts ASID 0 and 2. With
the GPU node enabled, `msm 1.13.0` creates `card1` and `renderD128` beside
simpledrm's `card0`; `a420_pm4.fw` and `a420_pfp.fw` load. The first EGL/GBM
offscreen clear then resets the SoC after the IOMMU resumes and
`restore_sec_cfg` returns 0. No renderer string, frame or compositor result
has been obtained. G5 passes; G6 is blocked.

**Phone left recovered:** SSH works on the installed `7.3.0-rc7-angler+`
kernel; Arch reported `running`, battery about 3.83 V, and no
`/.omamobile/pending` marker. No boot partition was flashed. The only
persistent test change is the rebuilt `ocmem.ko` (patch 0037) in the test
kernel's module directory `/usr/lib/modules/7.3.0-rc7-angler-gpu`; the old
file is backed up on the phone at `/tmp/ocmem.ko.before-0037`. The installed
kernel and its modules are untouched.

**Likely G6 cause (from the logs and source, not yet proven on the phone):
the GPU SMMU's context bank is never reprogrammed after a power collapse.**
`qcom_iommu` programs TTBR0/TCR/MAIR/SCTLR once, when `msm` attaches at bind
(`angler-g5c-kmsg.log`: resume, `Initialized msm`, then `iommu pm: suspend`).
The SMMU's only power domain is `OXILI_GX_GDSC`, which can collapse with the
idle GPU. On first use the resume calls only `restore_sec_cfg`, which restores
TZ's global space, not the context bank. A bank that comes back with SCTLR.M
clear passes the GPU's IOVAs (from 16 MiB up) through as physical addresses,
into the kernel image at 0x80000-0x1ae8000. That matches a silent reset with no
context fault. The vendor driver never meets this: `msm_iommu-v1.c` enables
the SMMU's GDSCs (`vdd-supply` CX, `qcom,alt-vdd-supply` GX) at attach and
holds them until detach, reprogramming the bank on every attach.

**0039 confirmed:** the bank read `SCTLR 0x00000000` before the restore.
With it the GPU reached `a4xx_hw_init`, loaded PM4/PFP, and the user bank
faulted at IOVA `0x1001000` (the ringbuffer); the SoC then reset, most likely
because the secured bank's handler on the shared SPI 241 read its `CB_FSR`.

**0040 confirmed:** no reset. The fault is logged as FSR 0x40000004 (access
flag fault) with FSYNR0 0x583 (PTWF, level 3), while the CPU's walk of the
same tables finds the page; the GEM list shows the ring mapped at 0x1001000.
The SMMU's own table walk goes wrong (GPU-PLAN Results).

**0041 confirmed, G6 passes:** with `qcom_iommu.bfb=1` the walk works and
Mesa renders on the A430 (`FD430`, GLES 3.0): clear and shader draw read
back correctly, 11 of 11 runs, no faults (GPU-PLAN Results). The BFB
registers reset with the GX GDSC, so 0041 writes them on every resume.

**Next action:** make the BFB write unconditional for the msm8994 GPU SMMU
(no parameter), then G7: Hyprland with simpledrm for scanout and msm for
rendering. The test is `device/angler/tools/egl-gpu-test.c` (built on the
phone as `/root/egl-gpu-test2`).

The owner authorized the one-shot diagnostic boots that produced these
results. Ask before any further boot, module push or persistent change.

## Worktrees, patches and artifacts

- Repository: `~/Documents/omarchy-mobile` (also accessible at
  `/mnt/delta/Documents/omarchy-mobile`).
- Kernel: `~/android/mainline/linux-angler`, branch `omamobile-angler`,
  HEAD `57deb2267` (patch 0030), based on 7.3-rc7.
- Output: `~/android/mainline/out-angler-gpu`; test release
  `7.3.0-rc7-angler-gpu`.
- **There are uncommitted kernel edits** in
  `drivers/iommu/arm/arm-smmu/qcom_iommu.c`,
  `drivers/soc/qcom/ocmem.c` and `drivers/firmware/qcom/qcom_scm.c`. They are
  exported as raw diffs 0031-0038 in `device/angler/kernel-patches/`. Already
  applied in the current worktree: **do not apply them again there**. On a
  clean tree at HEAD above, use `git apply` in numeric order (not `git am`).
- 0031-0035 are diagnostic: clock/runtime-PM reporting, reserved-table
  zeroing, the multimedia holds, tolerance for the rateless Venus clock, and
  post-secure-init trace logging.
- 0036-0038 are functional fixes found on the phone:
  - 0036 marks `qcom,msm-iommu-v1-sec` as secured. Touching that bank's
    `ARM_SMMU_CB_FSR` reset the SoC.
  - 0037 uses the four-byte PSGSC control-register stride. The old one-byte
    stride made msm8994's second OCMEM region an unaligned 32-bit write.
  - 0038 sets `ARM_SMCCC_OWNER_SIP` on the OCMEM lock/unlock SCM calls. The
    availability check used SIP, but the call itself used owner 0 and
    returned `-EIO`.
- 0039 restores the context banks on runtime resume (confirmed needed on
  the phone) and logs SCTLR before/after. 0040 stops requesting the fault
  IRQ for secured banks (functional) and traces the first faults
  (diagnostic). Both applied in the worktree. Build with `W=1` throughout:
  the earlier objects were built that way, and a build without it
  recompiles half the tree.
- Repository edits include this handoff, GPU plan, notes, patches 0031-0038,
  and the earlier `device/angler/tools/cycle` RAM-root fix. Nothing from this
  work has been committed or pushed.

All images live in `~/android/mainline/out-angler-gpu/`:

| Image | SHA256 | State |
| --- | --- | --- |
| `angler-gpu-mm-power-probe2.img` | `3ae6f8313a0af39867759f702cae7e7b481d731a33a14cdc3ec1238c06b0267b` | Tested; probe gate passed |
| `angler-gpu-mm-power-secure2.img` | `6d2cf8e0b3d231f14c4220c914e470541b1718208c7a1d7d219a22dcf22d6012` | Tested; secure table returned 0, then pre-0036 reset |
| `angler-gpu-mm-power-secure3.img` | `ef34fc5e815ad4580687d9248f7962c93e7cd0fac0067ceceaec5de2967ea5d0` | Tested; pinpointed the v1-sec FSR reset |
| `angler-gpu-mm-power-secure4.img` | `128c87f004f51f986bba5bbe451aa3c641a638a240fc484410841877fd921f72` | Tested; IOMMU registered, contexts up |
| `angler-gpu-g5-0036.img` | `ffe59c03230cfcdd5185e8b7ca1a9a599ea1fc3c4cbaa436598b25ba27d95e57` | Tested; OCMEM stride oops |
| `angler-gpu-g6-bfb.img` | `c54bf9a1ae2c34749850291ba6bef5ea8051ca904b91942c4db7329f21bd9c32` | Tested (0041, `bfb=1`): G6 passes, FD430 renders |
| `angler-gpu-g6-irqfix.img` | `724b83f2dafbb644e54c67c404d99103a55c2a9b9cda51754d6be1a765bc4861` | Tested (0040): no reset; access flag fault at the ringbuffer logged |
| `angler-gpu-g6-ctxrestore.img` | `104a304556cd6a07b33ad39cc3eeb2da5cbc5c526daa3ca186bad723144777a0` | Tested (0039): bank restored, then ringbuffer fault and reset |
| `angler-gpu-g5-0038.img` | `fb06ca3ed0d50a8663eb8b2e622266e74a610627eac70f3c3682200ba1e47966` | Tested; `renderD128`, then G6 reset |
| `angler-gpu-zero.img` | `9ccd3d2602f889d039a1b5927e015779a7a88fc602f43e3c55392f0fc2518450` | Superseded; reset in secure init |
| `angler-gpu-clock27-enabled.img` | `bb236b17508aad6126fa041b12d734879c58b527385b5579b60452eefca569cc` | Superseded; 27 MHz confirmed, reset |
| `angler-gpu-probe-only.img` | `83ebc839521a7ad2de26eb7c5c5884eddbcdc9c97c2211911aad88c0716f83a0` | Superseded; safe probe |
| `ocmem.ko` (test module) | `c741813218a61ae23d60907d9abc393263378d336fa28128aecc389f0e67b83c` | Installed in test module dir; old backup on phone |

The G5 images use `angler-gpu-g5.dtb`, a copy of `angler.dtb` with
`/soc@0/gpu@fdb00000` changed to `okay`; the IOMMU is already `okay`.
`angler.dtb` itself still has the GPU disabled. Both use `qcom_iommu.late=1`,
`ptbl_bus_rate=27000000`, `ptbl_zero=1`, `ptbl_mm_power=1`; the probe image
also uses `ptbl_probe_only=1`. The secure-table reservation is
`0x0e900000` / `0x40000`.

Validation: checkpatch `--strict --no-signoff` clean for 0031-0038; 0039
has only a CamelCase check on the existing `ARM_SMMU_TTBRn_ASID` macro. The
latest `W=1` Image/DTB builds completed without warnings in the changed
drivers. `dt-validate` on the assigned-rate image was identical to the base
DTB except the existing `memory-region` schema issue from 0029; the latest
images use the base DTB unchanged. Do not claim a warning-free whole tree.
`bash -n tools/cycle` passed.

## What the phone has proved

All tests ran `7.3.0-rc7-angler-gpu` with the watchdog active, `nowayout=1`, a
live feeder, SSH kmsg capture and a verified netconsole marker before driver
registration or module load.

| Test | Observed result |
| --- | --- |
| MM power probe (0031-0034) | Five domains and 15 clocks enabled; clock 121 (`VENUS0_VCODEC0_CLK`) is rateless because MMCC never registers `vcodec0_clk_src`; holds released with `-11` |
| Secure table with MM holds (0033) | `SECURE_PTBL_INIT` returned 0 for the first time; holds released (0); reset immediately after |
| Post-secure-init trace (0035) | Reset at the secure context's `ARM_SMMU_CB_FSR` access; user context ASID 0 probed cleanly |
| v1-sec fix (0036) | Secure context `a000`/ASID 2 populate without touching the bank; `qcom-iommu` bound; `/sys/class/iommu/soc@0:iommu@fdb10000` exists |
| G5, OCMEM stride (0037) | OCMEM allocation no longer oopses; `qcom_scm_ocmem_lock` then returned `-EIO` |
| G5, SCM owner (0038) | Adreno probe completes; `card1`/`renderD128`; `msm 1.13.0`; firmware `a420_pm4.fw` and `a420_pfp.fw` load |
| G6 first use | EGL/GBM opens the render node, firmware loads, IOMMU resumes and `restore_sec_cfg` returns 0, then the SoC resets. No renderer string or pstore |

Last useful messages from the G6 reset (sanitized):

```text
adreno fdb00000.gpu: [drm:adreno_request_fw [msm]] loaded qcom/a420_pm4.fw
adreno fdb00000.gpu: [drm:adreno_request_fw [msm]] loaded qcom/a420_pfp.fw
qcom-iommu soc@0:iommu@fdb10000: iommu pm: resume
qcom-iommu soc@0:iommu@fdb10000: iommu pm: clock enable returned 0
qcom-iommu soc@0:iommu@fdb10000: iommu pm: restoring secure config
qcom-iommu soc@0:iommu@fdb10000: iommu pm: restore returned 0
```

No context fault, GPU fault, hangcheck or kernel oops reached the log before
the reset. Ramoops/pstore was empty after recovery.

## Facts that matter for the next investigation

Sources: Google's GPL angler kernel `android-msm-angler-3.10-oreo-mr1`
(`33ace82f`), files in `~/android/reference/angler/kernel-src`, and our
7.3-rc7 source. These private reference directories also contain device data:
do not publish their contents wholesale.

- GPU A430 at `0xfdb00000`, SPI 33; mainline compatible `qcom,adreno-430.2`.
  `qcom/a420_pm4.fw` and `a420_pfp.fw` are installed. OCMEM probes and the
  GPU's 1.5 MiB `gmu_sram` allocation now succeeds. `msm.separate_gpu_kms=1`
  gives a render node beside simpledrm.
- GPU SMMU at `0xfdb10000`, secure id 18. Contexts at offsets `0x8000`
  (user, ASID 0) and `0xa000` (secure, ASID 2), user SPI 241. Stock maps
  SIDs 0/1 to user and 2 to secure. Linux maps its user context with
  `iommus = <&gpu_iommu 0>`. The `0x9000` priv context is not present in the
  test DTB.
- The multimedia holds are required for `SECURE_PTBL_INIT`: without them the
  call reset the SoC; with them it returns 0. The holds are released after the
  call, and the GPU SMMU's own runtime PM stays around the call.
- Vendor `PSCGC_CTL_IDX(x)` is `(x) * 0x4`, not `0x1`. Vendor
  `do_lock()`/`do_unlock()` use `SCM_SIP_FNID`, i.e. owner 2, not owner 0.
  Both differences are now patches 0037 and 0038.
- `qcom_iommu_ctx_probe` must not touch `qcom,msm-iommu-v1-sec` context banks.
  The v2-only `secured_ctx` condition was the reset source; 0036 extends it.
- The G6 reset happens after firmware load and after the IOMMU runtime resume
  and secure-config restore. The next suspects, in order, are the IOMMU
  context programming/attach path, the GPU boot sequence after firmware load,
  the missing downstream BFB TBU tuning registers, and GMEM/OCMEM or power
  sequencing. None is established.
- Downstream also writes "BFB" TBU tuning registers after secure setup
  (`qcom,iommu-bfb-regs`, angler's list in `msm8994-iommu.dtsi`);
  `qcom_iommu` has no equivalent. Historically considered performance-only;
  it has not been tested.
- `clk-smd-rpm.c:348-349` returns success for set-rate while disabled, without
  saving the new rate. The explicit `ptbl_bus_rate=27000000` after enable is
  what took effect; the DT assigned-rate test did not.
- TZ convention is SMC32. Command IDs match downstream: MP 3=size, 4=init,
  5=CP pool. Three init args, types RW/VAL/VAL, spare 0. No exhaustive ABI
  audit was done.
- No host-programmed/non-TZ SMMU mode or no-IOMMU DRM fallback was built.
  Those remain hypotheses, not tested fixes. A proposed 2026 MSM8974 series
  mentioned in the earlier handoff still needs a verified citable source.

## Next gated runbook

Risk is **high**: the G6 path already resets the SoC, and the secure-table
call remains sensitive to power/clock state. Use one-shot boots, GPU enabled
only in the test DTB, `msm` blacklisted until loaded by hand, no raw register
probes. Keep the installed image as rollback. Stop on any unexpected fault,
missing safeguard, low battery or lost connection. Do not retry the same
render test unchanged.

1. Read-only: run `device/angler/tools/gpu-check`. Require battery above
   3.7 V, reachable SSH and settled Arch with no `/.omamobile/pending`.
   In RAM recovery, ADC channel 7 (`vph_pwr`) is available via
   `/sys/bus/iio/devices/iio:device0/in_voltage7_input`; the fuel gauge and
   ADC have differed (3.8x V vs about 3.70 V). Respect the conservative
   threshold; charge if needed. Do not reboot below about 3.5 V.
2. Done offline: `angler-gpu-g6-ctxrestore.img` (sha256 above). Announce
   the screen interruption before booting it.
3. Boot it with `device/angler/tools/cycle
   ~/android/mainline/out-angler-gpu/angler-gpu-g6-ctxrestore.img`. Require test kernel,
   `late=Y`, `ptbl_mm_power=Y`, `ptbl_bus_rate=27000000`, `ptbl_zero=Y`, and
   `ptbl_probe_only=N` under `/sys/module/qcom_iommu/parameters/`. Stop if
   different. Wait for Arch `running` and no pending marker.
4. Capture logs and check the watchdog **before** any driver load:
   `cat /dev/kmsg` to a private log, `watchdog0/state` active, `nowayout` 1,
   a live feeder, an existing UDP 6666 listener, and a new netconsole marker.
5. Register the IOMMU by hand:
   `echo 1 > /sys/module/qcom_iommu/parameters/register`. Require the driver
   bound and no reset. Then `modprobe ocmem; modprobe msm` (msm is
   blacklisted). Require `renderD128` and `card1` bound to `adreno`.
6. `echo 0x2 > /sys/module/drm/parameters/debug` (msm's `DBG()` then marks
   each hw_init stage, e.g. `loading PM4 ucode version`). Run the offscreen
   EGL/GBM test under the log streams. On the first-use resume, expect
   `iommu pm: ctx 0 SCTLR 0x... before restore` then `... restored` with the
   M bit (bit 0) set. A `before` value with M clear confirms the cause; M
   still set means the bank kept its state and the cause lies elsewhere
   (follow the drm debug markers). A context fault after the restore is
   progress, not a reason to retry: record FAR/FSYNR and stop. Stop at the first
   reset or fault. The expected pass is `GL_RENDERER` containing `FD430` and a
   correct pixel readback, with no fault, hangcheck or reset. A reset after
   the last trace marker is the finding; wait at least two minutes for
   recovery, then ask the owner for the screen state if unclear.
7. Only after G6 passes: try the compositor (G7) with `AQ_DRM_DEVICES`
   ordering simpledrm for scanout and msm for rendering, and compare frame
   time/CPU against the llvmpipe numbers. Do not claim GPU success from a
   render node or a firmware load alone. No test here authorizes a persistent
   installation.

## Operational traps and remaining cleanup

- `cycle` addresses only the private Angler serial from
  `~/android/reference/angler/serial`; never run untargeted fastboot commands.
  Arch uses IMEM reason + SysRq u/b because clean shutdown previously hung.
  RAM has no Python: `cycle` detects non-systemd and uses its static
  `reboot-to` binary. Both paths have been exercised.
- The test image's initramfs SSH can answer before Arch is up. Wait for
  `device/angler/tools/wait-arch` (or `/run/systemd/system` and
  `systemctl is-system-running`) before registering or loading modules.
- `msm` is blacklisted by `/etc/modprobe.d/omamobile-gpu-bringup.conf` with
  `options msm separate_gpu_kms=1`; `ocmem` is blacklisted by the device
  package. Load both by hand for a test. Register the IOMMU first.
- The test kernel's `ocmem.ko` is the 0037 build; the old file is at
  `/tmp/ocmem.ko.before-0037`. A future `tools/modules-push` from a tree
  without 0037 will replace it and the OCMEM alignment fault will return.
- Registering too soon can reset before Arch's boot confirmation. Rollback
  then stays in RAM and moves the pending marker aside. Do not delete hang
  markers to force repeated failures.
- Never print `/proc/cmdline` or the start of dmesg: bootloader identifiers.
  Filter out `command line` and `androidboot` **before** selecting diagnostic
  messages. Logs in `logs/` contain identifiers; keep them private.
- Private logs from this work: `angler-mm-power-probe2-kmsg.log`,
  `angler-mm-power-secure2-kmsg.log`, `angler-mm-power-secure3-kmsg.log`,
  `angler-mm-power-secure4-kmsg.log`, `angler-g5-kmsg.log`,
  `angler-g5b-kmsg.log`, `angler-g5c-kmsg.log`, plus the shared
  `angler-netconsole.log` and build/validation logs.
- 0031-0035 are diagnostic code, not upstream-ready fixes. 0036-0038 are
  real fixes but still need upstream review and a proper commit series. The
  0029 `memory-region` binding issue needs a follow-up if the reserved-table
  design stays. Do not weaken validation or claim that warning is resolved.
