#!/bin/bash
# Compare the machine code of one kernel function between two vmlinux files,
# ignoring the addresses that differ only because other code moved.
#
# usage: compare-codegen.sh <vmlinux-A> <vmlinux-B> [function]
A=$1
B=$2
fn=${3:-crc32c}

norm() {
  objdump -d --no-show-raw-insn --disassemble="$fn" "$1" \
    | grep -E '^ffffffff[0-9a-f]+:' \
    | sed -E \
        -e 's/^ffffffff[0-9a-f]+:[[:space:]]+//' \
        -e "s/ffffffff[0-9a-f]+ <$fn\+0x[0-9a-f]+>/<$fn+N>/" \
        -e 's/ffffffff[0-9a-f]+ <([A-Za-z_0-9.]+)>/<\1>/' \
        -e 's/\$0xffffffff[0-9a-f]+/$ADDR/' \
        -e 's/-0x[0-9a-f]+\(,%r/TABLE(,%r/'
}

norm "$A" > /tmp/codegen-A.txt
norm "$B" > /tmp/codegen-B.txt
echo "$fn: $(wc -l < /tmp/codegen-A.txt) instructions in A, $(wc -l < /tmp/codegen-B.txt) in B"
if diff /tmp/codegen-A.txt /tmp/codegen-B.txt > /tmp/codegen.diff; then
  echo "identical"
else
  echo "differences (< A, > B):"
  cat /tmp/codegen.diff
fi
