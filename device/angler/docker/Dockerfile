# SPDX-License-Identifier: MIT
# Nexus 6P (angler) kernel build environment: Ubuntu clang/lld, dtc and aarch64 tools.
FROM ubuntu:24.04
ARG UID=1000
ARG GID=1000
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 LC_ALL=C.UTF-8
RUN apt-get update && apt-get install -y --no-install-recommends \
      bc bison build-essential ca-certificates clang lld llvm flex git kmod cpio \
      libelf-dev libssl-dev python3 python3-pip device-tree-compiler mkbootimg \
      gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu libc6-dev-arm64-cross curl xz-utils zstd lz4 file rsync \
    && rm -rf /var/lib/apt/lists/*
RUN (id -u $UID >/dev/null 2>&1 && userdel -r $(id -nu $UID) || true) \
    && (getent group $GID || groupadd -g $GID builder) \
    && useradd -m -u $UID -g $GID -s /bin/bash builder
USER builder
