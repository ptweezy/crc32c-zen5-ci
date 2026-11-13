#!/bin/bash
# Succeed if this CPU can run the AVX-512 VPCLMULQDQ code; otherwise say why not.
for f in avx512bw avx512vl vpclmulqdq; do
  if ! grep -qw "$f" /proc/cpuinfo; then
    cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs)
    echo "::notice::$cpu lacks $f: userspace run skipped"
    exit 1
  fi
done
