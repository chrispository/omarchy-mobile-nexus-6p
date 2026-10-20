#!/bin/bash
set -euo pipefail
pacman -Sy --noconfirm --needed meson ninja libdrm wayland wayland-protocols libglvnd \
  python-mako python-yaml python-packaging python-pycparser python-ply glslang \
  expat zlib zstd libelf bison flex pkgconf gcc >/tmp/pacman.log 2>&1 || { tail -20 /tmp/pacman.log; exit 1; }
cd /src/mesa-26.2.4
[ -d build ] || meson setup build --prefix=/opt/mesa-msaa --libdir=lib --buildtype=release -Db_ndebug=true \
  -Dplatforms=wayland -Dgallium-drivers=freedreno -Dvulkan-drivers= -Dglx=disabled \
  -Degl=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true -Dglvnd=enabled -Dgbm=enabled \
  -Dllvm=disabled -Dvideo-codecs= -Dgallium-va=disabled -Dgallium-rusticl=false \
  -Dtools= -Dbuild-tests=false -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled \
  -Dzstd=enabled -Dexpat=enabled
ninja -C build
DESTDIR=/src/install ninja -C build install >/dev/null
echo BUILD-OK
