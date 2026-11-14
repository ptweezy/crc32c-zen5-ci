#!/bin/bash
# Build a 32-bit x86 kernel with the CRC KUnit tests from the given source tree
# and boot it directly in QEMU.  There is no root filesystem: KUnit runs before
# the kernel looks for one, so the test results are on the console by the time
# it panics and QEMU exits.
#
# usage: kunit-i386.sh <kernel-source-dir> [build-dir]
src=$1
out=${2:-/tmp/kb-i386-kunit}
here=$(cd "$(dirname "$0")" && pwd)

set -e
mkdir -p "$out"
make -C "$src" ARCH=i386 O="$out" defconfig > /dev/null
(cd "$src" && ARCH=i386 ./scripts/kconfig/merge_config.sh -m -O "$out" "$out/.config" "$here/crc-kunit.config" > /dev/null)
make -C "$src" ARCH=i386 O="$out" olddefconfig > /dev/null
grep -E '^CONFIG_(X86_32|KUNIT|CRC_KUNIT_TEST|CRC32_ARCH)=' "$out/.config" | tr '\n' ' '; echo
make -C "$src" ARCH=i386 O="$out" -j"$(nproc)" > "$out/build.log" 2>&1
set +e

timeout 180 qemu-system-x86_64 -enable-kvm -cpu host -m 1G -nographic -no-reboot \
    -kernel "$out/arch/x86/boot/bzImage" \
    -append "console=ttyS0 panic=-1" > "$out/console.txt" 2>&1 < /dev/null
echo "qemu exit: $?"
grep -m1 -E 'Linux version' "$out/console.txt" | sed -E 's/^\[ *[0-9.]+\] //' | cut -c1-110
grep -E 'ok [0-9]+ crc|# crc: pass|not ok' "$out/console.txt" | sed -E 's/^\[ *[0-9.]+\] +//'
grep -E 'crc32c_benchmark: len=' "$out/console.txt" | sed -E 's/^\[ *[0-9.]+\] +# //' | tr '\n' ';' | cut -c1-700; echo
