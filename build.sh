#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-run}"

case "$MODE" in
  clean)
    make clean
    ;;
  bootloader)
    make build
    make bootloader
    ;;
  kernel)
    make build
    make kernel
    ;;
  build|rebuild)
    make clean
    make all
    ;;
  run)
    make clean
    make all
    make run
    ;;
  *)
    echo "Usage: $0 {clean|bootloader|kernel|build|rebuild|run}"
    exit 1
    ;;
esac
