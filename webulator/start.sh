#!/bin/sh
set -e

MODE="${1:-browser}"

case "$MODE" in
  browser)
    echo "Starting webulator dev server on http://localhost:1145"
    npx live-server ./ --port=1145 --ignore=".git,docs,roms" --open="?r=latest"
    ;;
  electron)
    echo "Starting webulator in Electron"
    npx electron-forge start
    ;;
  test)
    node test.js "${2:-all}"
    ;;
  *)
    echo "Usage: $0 [browser|electron|test]"
    exit 1
    ;;
esac
