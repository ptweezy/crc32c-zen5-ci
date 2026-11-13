#!/bin/bash
# Describe the machine: CPU identity and the features the CRC code cares about.
echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs)"
echo "vendor: $(grep -m1 'vendor_id' /proc/cpuinfo | cut -d: -f2 | xargs)" \
     "family: $(grep -m1 'cpu family' /proc/cpuinfo | cut -d: -f2 | xargs)" \
     "model: $(grep -m1 -E '^model\s*:' /proc/cpuinfo | cut -d: -f2 | xargs)" \
     "stepping: $(grep -m1 'stepping' /proc/cpuinfo | cut -d: -f2 | xargs)"
echo "cpus: $(nproc)"
for f in sse4_2 pclmulqdq avx2 vpclmulqdq avx512bw avx512vl; do
  grep -qw "$f" /proc/cpuinfo && echo "$f: yes" || echo "$f: no"
done
${CC:-gcc} --version | head -1
as --version | head -1
