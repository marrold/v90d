#!/bin/sh
# Fetch and unpack what v90d is built from but is not ours: the cross
# toolchain (Bootlin, GCC with uClibc-ng for ARMv5), Linux, busybox,
# OpenVPN with mbed TLS (the VPN client for the IWANGO games; fw/mkvpn.sh),
# and libv90modem (the modem itself, pinned; unpacked at $top/libv90modem).
# Each download is checked against the SHA-256 written here; a file that is
# already there and checks is not fetched again. Then busybox is built with
# fw/busybox.config.
#   sh fw/setup.sh        (about 700 MB of downloads, 2.5 GB unpacked)
# After that: make -C fw release    (see README.md)
set -e
fw=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$fw")
TC=armv5-eabi--uclibc--stable-2023.08-1
KVER=6.6.63
BB=busybox-1.36.1
# the modem: libv90modem, pinned to a released source tarball (a GitHub
# release asset, so its bytes do not change under us). Bump both lines to
# move to a new modem; libv90m.mk builds whatever is unpacked at $top/libv90modem.
LIBV90M_VER=0.6.1
LIBV90M_SHA=e7b3fb850c8f5aeaf19ca2105c5d7b2f4d24a164ca3e8f869ba829fa968d796e

get() { # get URL FILE SHA256
    mkdir -p "$(dirname "$2")"
    if [ -f "$2" ] && echo "$3  $2" | sha256sum -c --status; then echo "have $(basename "$2")"; return; fi
    echo "fetching $1"
    curl -fL --retry 3 -o "$2.part" "$1"
    echo "$3  $2.part" | sha256sum -c --status || { echo "$2: checksum does not match" >&2; exit 1; }
    mv "$2.part" "$2"
}

get https://toolchains.bootlin.com/downloads/releases/toolchains/armv5-eabi/tarballs/$TC.tar.bz2 \
    "$top/toolchain/$TC.tar.bz2" b8e4a8426819eb226d30facf93288855b7710165b5aecc2224b65936b64bc3ad
get https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KVER.tar.xz \
    "$fw/dl/linux-$KVER.tar.xz" d1054ab4803413efe2850f50f1a84349c091631ec50a1cf9e891d1b1f9061835
get https://busybox.net/downloads/$BB.tar.bz2 \
    "$fw/dl/$BB.tar.bz2" b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314
get https://swupdate.openvpn.org/community/releases/openvpn-2.6.12.tar.gz \
    "$fw/dl/openvpn-2.6.12.tar.gz" 1c610fddeb686e34f1367c347e027e418e07523a10f4d8ce4a2c2af2f61a1929
get https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-2.28.9/mbedtls-2.28.9.tar.bz2 \
    "$fw/dl/mbedtls-2.28.9.tar.bz2" e85ea97aaf78dd6c0a5ba2e54dd5932ffa15f39abfc189c26beef7684630c02b
get https://github.com/georgica/libv90modem/releases/download/v$LIBV90M_VER/libv90modem-$LIBV90M_VER.tar.gz \
    "$fw/dl/libv90modem-$LIBV90M_VER.tar.gz" $LIBV90M_SHA

[ -x "$top/toolchain/$TC/bin/arm-linux-gcc" ] || { echo "unpacking the toolchain"; tar -C "$top/toolchain" -xjf "$top/toolchain/$TC.tar.bz2"; "$top/toolchain/$TC/relocate-sdk.sh" >/dev/null; }
mkdir -p "$fw/build"
[ -d "$fw/build/linux-$KVER" ] || { echo "unpacking Linux $KVER"; tar -C "$fw/build" -xJf "$fw/dl/linux-$KVER.tar.xz"; }
[ -d "$fw/build/$BB" ] || { echo "unpacking $BB"; tar -C "$fw/build" -xjf "$fw/dl/$BB.tar.bz2"; }
# libv90modem goes at $top/libv90modem (where libv90m.mk looks by default); a
# version stamp makes a bumped LIBV90M_VER replace it. A developer's own
# checkout is used instead with make LIBV90M=/path/to/libv90modem.
if [ ! -f "$top/libv90modem/include/v90modem.h" ] || [ "$(cat "$top/libv90modem/.v90m-version" 2>/dev/null)" != "$LIBV90M_VER" ]; then
    echo "unpacking libv90modem $LIBV90M_VER"
    rm -rf "$top/libv90modem" "$top/libv90modem-$LIBV90M_VER"
    tar -C "$top" -xzf "$fw/dl/libv90modem-$LIBV90M_VER.tar.gz"
    mv "$top/libv90modem-$LIBV90M_VER" "$top/libv90modem"
    echo "$LIBV90M_VER" > "$top/libv90modem/.v90m-version"
fi
if [ ! -x "$fw/build/$BB/busybox" ]; then
    echo "building busybox"
    cp "$fw/busybox.config" "$fw/build/$BB/.config"
    make -s -C "$fw/build/$BB" ARCH=arm CROSS_COMPILE="$top/toolchain/$TC/bin/arm-linux-" oldconfig </dev/null >/dev/null
    make -s -C "$fw/build/$BB" ARCH=arm CROSS_COMPILE="$top/toolchain/$TC/bin/arm-linux-" -j"$(nproc)" busybox
fi
ls -l "$fw/build/$BB/busybox"
sh "$fw/mkvpn.sh"
echo "ready: make -C fw release"
