#!/bin/bash
# asm — thin wrapper that delegates to build/asm
# All build artifacts (os.img, *.bin, fs.inc) stay inside build/
exec "$(dirname "$0")/build/asm" "$@"
