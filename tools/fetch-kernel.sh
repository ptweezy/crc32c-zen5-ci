#!/bin/bash
# Fetch the kernel release that the patches are based on into ./linux and apply
# kernel/*.patch to it.
set -e
git clone -q --depth 1 --branch v7.4-rc2 https://github.com/torvalds/linux.git linux
cd linux
for p in ../kernel/*.patch; do
  echo "applying $p"
  git apply "$p"
done
git status --short
