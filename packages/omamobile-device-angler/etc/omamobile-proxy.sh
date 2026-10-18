# SPDX-License-Identifier: MIT
# Internet over the USB link without NAT on the desktop: the desktop keeps a
# reverse SOCKS proxy open on the phone's 127.0.0.1:1080
# (device/angler/tools/proxy runs ssh -R). pacman and curl use it.
export all_proxy=socks5h://127.0.0.1:1080
