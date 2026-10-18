# Morning notes, 2026-10-18

## Update, about 09:05: the phone needs a hand

- **The phone is stuck:** the screen is black, the LED is orange, and it's gone from USB since 08:48. Hold **Power for about 20 s** until it restarts. It then boots the known-good RAM-only image, and I take it back to Arch from there. The battery was fine (3.6 V, charging).
- **What happened:** I started the graphical session to see how it runs on four cores. About 2.5 minutes in, the phone dropped off USB. Omarchy's idle settings never suspend, so it's most likely a hang under the session's load. The suspects are the CPU rail (it stays at the bootloader's voltage, with no regulation) and heat (no thermal throttling yet). The kernel log should have survived in RAM; I'll read it after the reset.
- **So this can't strand it again:** device package 0.1.6 feeds the hardware watchdog from Arch too, so a hang resets the phone on its own. It's built and goes on at the next boot.
- **Also new since 08:30:** the **notification LED** works and shows the charge state (orange while charging, green when full, as on stock). The **vibrator** driver loads; I haven't made it buzz yet. That's a 30-second test with you holding the phone (runbook H1-H3 in the plan).

## Update, about 08:30

- **All eight cores work.** The four A53s (CPU0-3) now come up at every boot, with no extra service. The four A57s (CPU4-7) also booted and ran work, at 600 MHz.
- **The A57s are off by default for now.** This firmware gives Linux no way to put idle cores to sleep, so the idle A57s draw about 165 mA. That uses up everything the desktop's USB port has left for the battery: with them on, charging stopped. Once cores can sleep when idle, they can stay on. To try them, add `msm8994_cpu_boot.cpus=0xfe` to the cmdline.
- **Charging is back to about 170 mA,** at 3.55 V and climbing.
- Pushed to branch **`v7`** (device package 0.1.5, kernel patches 0013-0015).
- Still pending: the touch test, and your call on flashing the charger-capable image to `boot` (see below).

## Update, about 06:45

- **Charging works.** The PMI8994 charger and fuel-gauge drivers run. The bug that drained the phone: the charger allowed only 100 mA from the USB port, because the gadget asked the desktop for 2 mA. It now asks for 500 mA, and the device package sets the charger to match. The phone charged at about 200 mA while idle (2.85 → 3.21 V). Committed and pushed.
- **The battery is still nearly empty,** and two reboots at about 3 V browned the phone out. It's off now, charging with the **red LED** on, and will boot by itself once it has enough charge. Nothing is damaged. I'm not rebooting it again until it's above about 3.5 V.
- **SDDM (the graphical session) is disabled** until the battery recovers. `systemctl enable --now sddm` turns it back on.
- **More cores:** see the 08:30 update above.
- **One decision for you:** may I flash the charger-capable image to `boot`? After a brownout, the phone comes back on whatever is in `boot`, and the image there now has no charger driver.

Good morning. **The Nexus 6P now boots Arch Linux ARM with Omarchy and the Omarchy Mobile phone shell, and it shows on the phone's own screen:** wallpaper, status bar, notifications, app dock and launcher. Graphics are still software-only, so it's slow.

**One thing first:** the phone is frozen on lk2nd's screen (the bootloader from the last experiment) and needs you. Hold Power for about 20 s until it restarts. It then boots on its own into the known-good RAM-only Linux in `boot`, and I can take it back to Arch from there without buttons.

**Done overnight** (all pushed; branches `v3`–`v6`, latest is `v6`):
- **Arch Linux ARM on `userdata`**, signature-checked and fully upgraded. Every other partition stays read-only, enforced from the initramfs and again by udev.
- **No buttons needed:** I can get the phone from Linux into fastboot and boot test images myself. `reboot=warm` was needed to keep that working once the power-key driver was in.
- **Watchdog:** a test boot that hangs while the CPU is busy now resets back into the known-good image. It doesn't cover an early hang that leaves the CPU idle, which is how the last test got stuck.
- **Omarchy installed on the phone itself**, plus the Omarchy Mobile packages built on the desktop: the shell, apps, Omarchy's phone patches (13 applied cleanly) and setup. A new `omamobile-device-angler` package holds the phone-specific pieces.
- **Kernel:** touchscreen (Synaptics S3718AR, detected but not tried by hand), a DRM display device, the power key, the clock chip, temperature sensors, the firewall, and modules installed into Arch.
- **Notes for other agents** in `device/angler/NOTES.md`, and results in `nexus_linux_plan.md`.

**Things you should know:**
- **The login is user `chris` with PIN `1234`.** I picked that so the setup could finish; change it with `passwd`. SSH (`root@172.16.42.1`) is key-only.
- **The phone has internet only through the desktop**, via a background SSH tunnel I left running. pacman needs it.
- **I created a signing key for this fork's packages in `keys/`.** Git ignores it, so it never leaves your desktop; its public half is in `omamobile-keyring`.
- **A firewall bug locked me out of SSH once.** I got back in through the phone's USB serial console and fixed it; the device package now prevents it.

**Still not working:**
- **Only one of the eight cores runs**, at roughly 0.8–1.1 GHz, so the shell is slow. Getting the other cores needs lk2nd. It now builds and starts from RAM (it needed this phone's board id added), but booting our kernel through it froze the phone. The next tests are a short runbook in the plan, and should be done while you're around.
- **No GPU, battery readout, charging control or Wi-Fi yet.** The real display driver for this panel would be new driver work.

I'd like your call on two things:
1. **Try touch by hand** on the home screen and tell me if taps land in the right place. The axes may be flipped.
2. **Should I flash the Arch-capable image to `boot`?** Then a reset lands in Arch instead of the RAM-only image. Fastboot works the same either way.

Sources:
- [RFC V4 PATCH 0/6] msm8992/msm8994: Google Nexus 5X/6P initial board support: https://lore.kernel.org/all/20161021182533.GX7509@tuxbot/T/
- arm64: dts: msm8994 SoC and Huawei Angler (Nexus 6P) support: https://ayakael.net/forge/linux-uconsole/commit/feeaf56ac78d
