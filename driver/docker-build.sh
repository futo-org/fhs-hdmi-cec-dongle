#!/bin/sh
# Compile-test the module against distro kernel headers in containers.
# Usage: docker-build.sh [debian|fedora|dkms]...   (default: all)
set -eu

cd "$(dirname "$0")"
SRC="$(pwd)"
TARGETS="${*:-debian fedora dkms}"

run() {
	image="$1"
	shift
	echo "=== $image: $*"
	docker run --rm -v "$SRC":/src:ro "$image" sh -euc "$*"
}

# Build in a writable copy so the source tree stays clean.
BUILD='cp -r /src /build && cd /build && make KDIR="$KDIR" W=1 KCFLAGS=-Werror modules && modinfo fhs-cec.ko | grep -E "^(version|license|alias)"'

for target in $TARGETS; do
	case "$target" in
	debian)
		run debian:bookworm "
			apt-get update -qq >/dev/null
			apt-get install -y -qq build-essential linux-headers-\$(dpkg --print-architecture) kmod >/dev/null
			KDIR=\$(ls -d /usr/src/linux-headers-*-\$(dpkg --print-architecture) | head -1)
			echo kernel \$(basename \$KDIR)
			$BUILD"
		;;
	fedora)
		run fedora:latest "
			dnf -y -q install gcc make kernel-devel kmod elfutils-libelf-devel >/dev/null
			KDIR=\$(ls -d /usr/src/kernels/* | head -1)
			echo kernel \$(basename \$KDIR)
			$BUILD"
		;;
	dkms)
		run debian:bookworm "
			apt-get update -qq >/dev/null
			apt-get install -y -qq build-essential dkms linux-headers-\$(dpkg --print-architecture) kmod >/dev/null
			KVER=\$(ls /lib/modules | head -1)
			cp -r /src /build && cd /build && make dkms-tarball
			tar -xzf fhs-cec-*.tar.gz -C /usr/src
			dkms add fhs-cec/\$(cat VERSION)
			dkms build fhs-cec/\$(cat VERSION) -k \$KVER
			dkms status"
		;;
	*)
		echo "unknown target: $target" >&2
		exit 1
		;;
	esac
done
