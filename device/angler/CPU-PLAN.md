<!-- SPDX-License-Identifier: MIT -->
# Nexus 6P CPU plan: all eight cores, at speed

Status 2026-10-18 (evening): C0 done (read-only facts below); C1 (A53
cpufreq, 600-1555.2 MHz) passes C1.1-C1.5 on the phone; C1.6 (session use
with schedutil) running. Kernel tree `~/android/mainline/linux-angler`
(branch `omamobile-angler`, uncommitted edits; no worktrees), output
`~/android/mainline/out-angler-gpu`, test release `7.3.0-rc7-angler-gpu`
(the GPU edits are in the same tree, so test images carry both).

Read [AGENTS.md](../../AGENTS.md), [nexus_linux_plan.md](../../nexus_linux_plan.md)
(sections "four cores" and "why the session is slow") and
[PERF-HANDOFF.md](PERF-HANDOFF.md) first.

## Goal

Today: four A53s (CPU0-3) at the bootloader's 768 MHz, no cpufreq, no
cpuidle; the A57s (CPU4-7) boot with `msm8994_cpu_boot.cpus=0xfe` but sit at
600 MHz on the auxiliary clock, with their rail always on, and cost about
165 mA at idle, so they are off by default. The session is CPU-bound (the
GPU idles at 27 MHz). The target: A53 up to 1555.2 MHz and A57 up to
1958.4 MHz, scaling down when idle, with thermal limits, and the A57s on by
default.

## Facts (C0, read on the phone 2026-10-18, read-only)

Sources: Google's angler kernel (`kernel/msm`, oreo-mr1, saved under
`~/android/reference/angler/kernel-src/`: `clock-cpu-8994.c`,
`msm8994-regulator.dtsi`, `msm8994-v2-pm.dtsi`, plus `msm8994-v2.dtsi` and
`drivers/clk/qcom/clock-pll.c` from the same commit). The tool:
`device/angler/tools/mmio-read` (read-only `/dev/mem`, one line per register).

| What | Address | Read | Meaning |
| --- | --- | --- | --- |
| A53 PLL0 mode | `0xf9015000` | `0x80000007` | locked, out, reset_n, bypassnl |
| A53 PLL0 L | `0xf9015004` | `0x50` | 80 × 19.2 = 1536 MHz VCO |
| A53 PLL0 USER_CTL | `0xf9015010` | `0x11f` | post divider (bits 8-9) = 2 |
| A53 PLL0 CONFIG_CTL | `0xf9015014` | `0x004d6968` | the v2 value, already set by the bootloader |
| A53 PLL1 (alt) mode | `0xf9015040` | `0x41000000` | off |
| A53 mux | `0xf900d054` | `0x80000004` | HF 0 (LF), LF 2 (PLL0 main): **768 MHz** |
| A53 L2 PWR_CTL | `0xf900d014` | `0x90022218` | on |
| A53 mem-acc 1-4 | `0xf900d084`-`090` | 0 | the fuse corner 3/4 value |
| SAWs `0xf9065000`, `0xf9012000` | VCTL `+0x1c` | 0 | the SPM sends no voltage commands |
| CCI PLL | `0xf9011000`/`04` | `0x80000007` / L `0x1c` | CCI mux `0xf911211c` = `0x80000010` |
| PM8994 S8 (VDD_APC0) | SPMI SID 1 `0x2940/41` | range 0, `0xb8` | FTS2.5: 80 mV + 184 × 5 mV = **1000 mV**, on |
| PM8994 S11 (VDD_APC1, gang S9-S11) | `0x3240/41` | `0xa4` | 900 mV, off (A57s not booted) |
| PM8994 S2 (VDD_MX) | `0x1740/41` | `0xbd` | 1025 mV |

PMIC registers are read through `/sys/kernel/debug/regmap/0-01/registers`
(`dd bs=9 skip=<reg> count=1`). The SPMI regulator driver already registers
`s8`, `s11` etc. (`fc4cf000.spmi:pmic@1:regulators`), and its readings agree.
S8/S9-S11 are **FTS2.5** (subtype 0x09): range 0 starts at 80 mV, not 0.

- Thermal: no tsens on mainline msm8994 (only `pm8994-thermal`, the PMIC
  die). MSM8994 is the Snapdragon 810: thermal limits matter.
- No PSCI in TZ: the cores boot through `msm8994-cpu-boot` (spin table,
  SCM boot address, ACC power-up) and can't be offlined or power-collapsed
  through PSCI.

### Downstream v2 frequency plan and voltage ceilings

A53 (apc0, `qcom,cpr-corner-map` `1 2 2 2 3 3 3 4 4 4 4 4 4`), corner:
384→3, 460.8→4, 600→5, 672→6, 768→7, 864→8, 960→9, 1248→10, 1344→11,
1478.4→12, 1555.2→13 MHz. Ceiling override per corner 1-13 (mV): 900 ×4,
1000 ×3, 1115 ×2, 1180 ×4. Floors: 700 700 700 725 780 800 825 835 850 915
970 980 1000. The bootloader runs 768 MHz at exactly that corner's ceiling,
1000 mV, so it runs open loop at the ceilings.

A57 (apc1, 17 corners): 384/480/633.6→5, 768→6, 864→7, 960→8, 1248→9,
1344→10, 1440→11, 1536→12, 1632→13, 1728→14, 1824→16, 1958.4→17 (speed
bin 1 stops at 1766.4→15; bin fuse `qcom,speed-bin-fuse-sel = <22 0 3 0>`,
QFPROM row 22 at `0xfc4bc000`). Ceilings in `msm8994-regulator.dtsi`
`apc1_vreg_corner` (up to 1225 mV).

Per fuse corner (1-4): VDD_MX must be at least RPM corner
SVS_KRAIT/SVS_SOC/NOMINAL/SUPER_TURBO (`qcom,vdd-mx-corner-map = <3 4 5 7>`;
mainline rpmpd levels 2/3/4/6). Memory accelerator (`mem-acc`): A53 `0x02`
at fuse corners 1-2, `0x00` at 3-4; A57 `0x02`/`0x06` (type 4 `0x0b`) at
`0xf900f084`-`090`.

### Clock tree (v2)

Per cluster: PLL0 (VCO 1209.6-1996.8 MHz, L only), its post-divided main
output (/2, /4), XO, and SYS_APCS_AUX (GPLL0, 600 MHz) on the LF mux (bits
1-2: 0 XO, 2 PLL0 main, 3 aux); HF mux (bits 3-4: 0 LF, 3 PLL0 early,
1 PLL1). Relock: park on aux, PLL off, write L and post divider, the
`variable_rate_pll` enable sequence (test_ctl, bypass, 10 µs, reset_n,
200 µs, lock), then switch back.

## Design

- `drivers/clk/qcom/clk-cpu-8994.c` (`qcom,msm8994-cpu-clk`, new; patch
  `kernel-patches/0044`): the
  cluster clocks above; 600 MHz runs from aux, ≥1209.6 MHz from the VCO,
  the rest /2 or /4. It holds a fixed VDD_MX super-turbo vote (rpmpd
  `MSM8994_VDDMX_AO`) while bound. A lock failure leaves the cluster on
  aux (600 MHz) and returns an error.
- The A57 clock is registered too, but reads its PLL and mux only while
  the cluster's L2 is powered (`PWR_CTL` bits 9 and 28, readable when off)
  and allows only 600 MHz (aux); the A57s get a one-OPP table. Every
  possible CPU needs an OPP table: with none on CPU4-7, cpufreq-dt's
  `opp-shared` scan stopped at CPU4, CPU1 was left out of policy0 and
  the probe failed (`failed to set regulators`, -EBUSY; first image `c1`).
- cpufreq-dt (the platdev makes the device for any CPU with an OPP v2
  table), with `cpu-supply` = PM8994 S8 through `qcom_spmi-regulator`
  (constraints 1000-1180 mV, always on).
- OPPs at the ceiling voltages (open loop). CPR closed loop is a later
  power saving, not needed for speed.

## Phases and runbook

Each step: the phone streams kmsg to `logs/` (`ssh ... 'cat /dev/kmsg' >
logs/angler-cpu-<step>-kmsg.log`), the watchdog feeder runs, and a hang ends
in a watchdog reset into the `boot` partition's kernel (no cpufreq), so the
rollback is "reboot".

### C1: A53 cpufreq (image `out-angler-gpu/angler-cpu-c1b.img`, sha256 `c2f51956…`)

Image: `build-n1` with `KERNEL_OUT=~/android/mainline/out-angler-gpu
LOCALVERSION=-gpu ENABLE="/soc@0/iommu@fdb10000 /soc@0/gpu@fdb00000"` and the
G7 cmdline plus `cpufreq.default_governor=userspace` (the cores stay at
768 MHz until a step sets them). Modules: `modules-cpu-c1.tar.zst`.
`clk_cpu_8994` is blacklisted on the phone for this stage
(`/etc/modprobe.d/omamobile-cpufreq-bringup.conf`) and loaded by hand.

| Step | Command (phone) | Expect / Pass | Stop | Risk |
| --- | --- | --- | --- | --- |
| C1.0 | push modules (`KERNEL_OUT=... modules-push`), add the blacklist, boot the image (`tools/cycle`) | Arch up, `nproc` 4, no `cpufreq` dir yet | no SSH: wait for the watchdog | low |
| C1.1 | `modprobe clk_cpu_8994` | `a53_clk at 768000000 Hz (mux 0x80000004, PLL L 80)`; `policy0` with 600-1555.2 MHz, governor userspace, cur 768000 | probe error: unload, report | low |
| C1.2 | `echo 672000 > .../policy0/scaling_setspeed` (PLL relock at 1344 /2, same voltage) | cur 672000; mux `0x...04`, L 70; a cycles loop reads ~672 MHz | hang (watchdog) | moderate |
| C1.3 | 600000 (aux), then 768000 | aux path works both ways | as above | moderate |
| C1.4 | 864000, 960000 (S8 to 1115 mV first) | `s8` microvolts 1115000; clock matches | as above | moderate |
| C1.5 | 1248000 … 1555200 (1180 mV, VCO direct) | each step: 60 s of four busy loops without errors; `pm8994-thermal` temperature noted | lockups, MCE/SError, temp > 80 °C | moderate |
| C1.6 | `schedutil` governor for an hour of session use | the session faster; the profile from PERF-HANDOFF repeated | | low |
| C1.7 | Pass: drop the blacklist, `=y` or autoload, governor default; record in Results | | | |

### C2: A57 cluster

A57 PLL `0xf9016000`, mux `0xf900f054`, S11 as the gang leader (S9/S10 follow:
check the SPMI regulator's gang handling; never write S9/S10 alone), A57
mem-acc per fuse corner, speed-bin fuse for the OPP table (bin 0 to 1958.4,
bin 1 to 1766.4). Read the A57 PLL only on a boot with `cpus=0xfe` (the
cluster is otherwise unpowered: reading an unpowered block can abort).
`msm8994-cpu-boot` sets S11 through the SAW VCTL (`0xb8`, 1000 mV) and the mux
to aux: the clock driver must agree with it (probe after it; read state).

### C3: thermal (needed before the A57s run at speed or by default)

tsens on msm8994 (downstream `qcom,msm8994-tsens` at `0xfc4a8000`, 16
sensors, calibration in QFPROM): driver support plus thermal zones with
cpufreq cooling per cluster. Separate kernel task; can run in parallel with
C1/C2 code (see PERF-HANDOFF).

### C4: A57 idle cost

With cpufreq the A57s idle at 384 MHz and about 900 mV instead of 600 MHz
and 1000 mV. Measure (`power-ab`-style, battery current) against the 165 mA
before. If it's still too high: SPM-driven WFI clock gating/retention
(`qcom,saw2-spm-cmd-wfi`/`-ret` in `msm8994-v2-pm.dtsi`), or cluster power
collapse, which needs our own cpu_ops (no PSCI): research.

### C5 (later): MX vote follows the rate; CCI scaling; CPR closed loop or fuse-based open-loop voltages (cooler)

## Results

### 2026-10-18 evening: C1 passes on the bench (7.3.0-rc7-angler-gpu)

Owner-approved boots of `angler-cpu-c1.img` (cpufreq-dt probe failure,
above) and `angler-cpu-c1b.img`, battery 3.92 V, `clk_cpu_8994` blacklisted
and loaded by hand, governor userspace. Log `logs/angler-cpu-c1b-kmsg.log`.
Measured: `perf stat` cycles / task-clock of a busy loop pinned to CPU1;
load: four workers hashing an 8 MB buffer and comparing with the reference.

- C1.1: `a53_clk at 768000000 Hz (mux 0x80000004, PLL L 80)`, `a57_clk:
  cluster off`; policy0 = CPU0-3, nine frequencies, cur 768000.
- C1.2-C1.4: 672 (PLL relock, 1344/2), 600 (aux) and back, 864/960 with S8
  at 1115 mV (the PMIC reads `0x2941` = `0xcf`): each measured within
  0.01% (671.95, 599.95, 863.94, 959.94 MHz). 15 s load at 960: 0 errors.
- C1.5: 1248 (VCO direct, 1180 mV), 1344, 1478.4: 0 errors; 1555.2 MHz
  (measured 1555.11): 60 s on four cores, 7,205 hashes, **0 errors**,
  PMIC die 47.7 °C (from 39 °C).
- 200 random transitions across all nine OPPs: requested = read back
  every time, no lock failures.
- C1.6: governor schedutil, GPU up (render test passes), SDDM started.
