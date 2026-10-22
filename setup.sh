#!/usr/bin/env bash
# setup.sh — Prepare the x86 Assembly Kernel workspace.
#
# Usage:
#   ./setup.sh          # check deps, scaffold dirs, print next steps
#   ./setup.sh --build  # run setup steps then build (./asm)
#   ./setup.sh --run    # run setup steps then build + launch in QEMU
#   ./setup.sh --debug  # run setup steps then build + launch with GDB

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

ok()   { echo -e "${GREEN}[✓]${NC} $1"; }
warn() { echo -e "${YELLOW}[!]${NC} $1"; }
fail() { echo -e "${RED}[✗]${NC} $1"; }
info() { echo -e "${CYAN}[i]${NC} $1"; }

# ── Argument parsing ──────────────────────────────────────────────
DO_BUILD=false
DO_RUN=false
DO_DEBUG=false

case "${1:-}" in
    --build) DO_BUILD=true ;;
    --run)   DO_BUILD=true; DO_RUN=true ;;
    --debug) DO_BUILD=true; DO_DEBUG=true ;;
    "")      ;;
    -h|--help)
        echo "Usage: ./setup.sh [--build | --run | --debug]"
        echo ""
        echo "  (none)    Check dependencies and scaffold directories"
        echo "  --build   Also compile the kernel (./asm)"
        echo "  --run     Also build and launch in QEMU (./asm -r)"
        echo "  --debug   Also build and launch with GDB (./asm -d)"
        exit 0
        ;;
    *)  fail "Unknown option: $1"; echo "Try ./setup.sh --help"; exit 1 ;;
esac

echo ""
echo "═══════════════════════════════════════════"
echo "  x86 Assembly Kernel — Project Setup"
echo "═══════════════════════════════════════════"
echo ""

# ── 1. Check dependencies ────────────────────────────────────────
info "Checking dependencies..."

MISSING=()
check_cmd() {
    if command -v "$1" &>/dev/null; then
        ok "$1  ($(command -v "$1"))"
    else
        fail "$1 — NOT FOUND"
        MISSING+=("$1")
    fi
}

check_cmd nasm
check_cmd python3
check_cmd qemu-system-i386

# Warn about optional but useful tools
if command -v gdb &>/dev/null; then
    ok "gdb  ($(command -v gdb))  (optional — needed for ./asm -d)"
else
    warn "gdb — not found (optional, needed for debug mode)"
fi

if command -v make &>/dev/null; then
    ok "make  ($(command -v make))  (optional)"
else
    warn "make — not found (optional)"
fi

echo ""

if [[ ${#MISSING[@]} -gt 0 ]]; then
    fail "Missing required dependencies: ${MISSING[*]}"
    echo ""
    echo "Install them with:"
    echo "  Debian/Ubuntu:  sudo apt install nasm qemu-system-x86"
    echo "  Fedora:         sudo dnf install nasm qemu-system-x86"
    echo "  Arch:           sudo pacman -S nasm qemu-system-x86"
    echo "  macOS (brew):   brew install nasm qemu"
    echo ""
    echo "Then re-run ./setup.sh"
    exit 1
fi

ok "All required dependencies satisfied."
echo ""

# ── 2. Verify NASM version ───────────────────────────────────────
NASM_VERSION=$(nasm -v 2>&1 | grep -oP '[\d.]+' | head -1)
info "NASM version: $NASM_VERSION"

# ── 3. Scaffold directory structure ──────────────────────────────
info "Scaffolding directory structure..."

DIRS=(commands)
for d in "${DIRS[@]}"; do
    if [[ ! -d "$PROJECT_DIR/$d" ]]; then
        mkdir -p "$PROJECT_DIR/$d"
        ok "Created $d/"
    else
        ok "$d/  (already exists)"
    fi
done

echo ""

# ── 4. Verify key source files are present ───────────────────────
info "Checking key source files..."

KEY_FILES=(
    "asm"
    "gen_fs.py"
    "bootloader.asm"
    "kernel.asm"
)

ALL_PRESENT=true
for f in "${KEY_FILES[@]}"; do
    if [[ -f "$PROJECT_DIR/$f" ]]; then
        ok "$f  (found)"
    else
        fail "$f — NOT FOUND"
        ALL_PRESENT=false
    fi
done

# Check that the build script is executable
if [[ -f "$PROJECT_DIR/asm" ]]; then
    if [[ -x "$PROJECT_DIR/asm" ]]; then
        ok "asm  (executable)"
    else
        warn "asm — not executable, fixing..."
        chmod +x "$PROJECT_DIR/asm"
        ok "asm  (made executable)"
    fi
fi

# Check that gen_fs.py is executable (it has a shebang)
if [[ -f "$PROJECT_DIR/build/gen_fs.py" ]]; then
    if [[ -x "$PROJECT_DIR/build/gen_fs.py" ]]; then
        ok "build/gen_fs.py  (executable)"
    else
        warn "build/gen_fs.py — not executable, fixing..."
        chmod +x "$PROJECT_DIR/build/gen_fs.py"
        ok "build/gen_fs.py  (made executable)"
    fi
fi

echo ""

if [[ "$ALL_PRESENT" == false ]]; then
    fail "Some key files are missing."
    echo "  Expected: asm gen_fs.py,"
    echo "  bootloader.asm, kernel.asm"
    exit 1
fi

# ── 5. Check for userland command sources ────────────────────────
info "Checking userland command sources..."

echo ""

# ── 6. Summary ───────────────────────────────────────────────────
echo "═══════════════════════════════════════════"
ok "Setup complete!"
echo "═══════════════════════════════════════════"
echo ""

if [[ "$DO_BUILD" == false ]]; then
    info "Next steps:"
    echo "  ./asm             # build only"
    echo "  ./asm -r          # build and run in QEMU"
    echo "  ./asm -f          # build and run fullscreen"
    echo "  ./asm -d          # build and run with GDB"
    echo ""
    echo "  ./setup.sh --run  # re-run setup + build + launch"
    echo ""
fi

# ── 7. Optional: build ───────────────────────────────────────────
if [[ "$DO_BUILD" == true ]]; then
    info "Building the kernel..."
    echo ""
    cd "$PROJECT_DIR"
    ./asm
    BUILD_EXIT=$?

    if [[ $BUILD_EXIT -ne 0 ]]; then
        fail "Build failed (exit code $BUILD_EXIT)"
        exit $BUILD_EXIT
    fi

    ok "Build succeeded."
    echo ""
fi

# ── 8. Optional: run in QEMU ─────────────────────────────────────
if [[ "$DO_RUN" == true ]]; then
    info "Launching in QEMU (windowed)..."
    echo ""
    cd "$PROJECT_DIR"
    ./asm -r
fi

if [[ "$DO_DEBUG" == true ]]; then
    info "Launching in QEMU with GDB server on localhost:1234..."
    echo ""
    cd "$PROJECT_DIR"
    ./asm -d
fi
