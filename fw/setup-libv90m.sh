#!/bin/sh
# Fetch and unpack libv90modem alone (the modem itself, pinned; unpacked at
# $top/libv90modem) -- what a host-only `make check` needs, without the
# cross toolchain, kernel, busybox or OpenVPN that fw/setup.sh also fetches
# for a full firmware build. fw/setup.sh calls this script too.
#   sh fw/setup-libv90m.sh
set -e
fw=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$fw")
# The pin stays declared in fw/setup.sh, where upstream's version bumps land
# (two lines, LIBV90M_VER=/LIBV90M_SHA=) -- read it from there instead of
# keeping a second copy here that a bump would then also have to touch.
eval "$(grep -E '^LIBV90M_(VER|SHA)=' "$fw/setup.sh")"

get() { # get URL FILE SHA256
    mkdir -p "$(dirname "$2")"
    if [ -f "$2" ] && echo "$3  $2" | sha256sum -c --status; then echo "have $(basename "$2")"; return; fi
    echo "fetching $1"
    curl -fL --retry 3 -o "$2.part" "$1"
    echo "$3  $2.part" | sha256sum -c --status || { echo "$2: checksum does not match" >&2; exit 1; }
    mv "$2.part" "$2"
}

get https://github.com/georgica/libv90modem/releases/download/v$LIBV90M_VER/libv90modem-$LIBV90M_VER.tar.gz \
    "$fw/dl/libv90modem-$LIBV90M_VER.tar.gz" $LIBV90M_SHA

# a version stamp makes a bumped LIBV90M_VER replace it. A developer's own
# checkout is used instead with make LIBV90M=/path/to/libv90modem.
if [ ! -f "$top/libv90modem/include/v90modem.h" ] || [ "$(cat "$top/libv90modem/.v90m-version" 2>/dev/null)" != "$LIBV90M_VER" ]; then
    echo "unpacking libv90modem $LIBV90M_VER"
    rm -rf "$top/libv90modem" "$top/libv90modem-$LIBV90M_VER"
    tar -C "$top" -xzf "$fw/dl/libv90modem-$LIBV90M_VER.tar.gz"
    mv "$top/libv90modem-$LIBV90M_VER" "$top/libv90modem"
    echo "$LIBV90M_VER" > "$top/libv90modem/.v90m-version"
fi
