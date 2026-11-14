#!/bin/bash
# Fetch the kernel release that the patches are based on into ./linux and apply
# patches to it: the ones named on the command line, or kernel/*.patch.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
if [ $# -gt 0 ]; then
  patches=()
  for p in "$@"; do patches+=("$(realpath "$p")"); done
else
  patches=("$here"/kernel/*.patch)
fi

git clone -q --depth 1 --branch v7.4-rc2 https://github.com/torvalds/linux.git linux
cd linux
for p in "${patches[@]}"; do
  echo "applying $(basename "$p")"
  git apply "$p"
done
git status --short
