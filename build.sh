#!/bin/bash
# Build script for userland programs
# Usage: ./build.sh [program_name]
# If no program specified, builds all .asm files in commands/

set -e

PROJECT_ROOT="/home/janko/dev/code"
COMMANDS_DIR="$PROJECT_ROOT/commands"
BUILD_DIR="$PROJECT_ROOT/build/bin"
LIB_DIR="$PROJECT_ROOT/lib"

# Ensure build directory exists
mkdir -p "$BUILD_DIR"

# Set up include path for nasm
export NASM_INCLUDE="$LIB_DIR:"

# Function to compile a single program
compile_program() {
    local asm_file="$1"
    local base_name=$(basename "$asm_file" .asm)
    local output_file="$BUILD_DIR/$base_name"
    
    echo "Compiling: $base_name"
    nasm -i "$LIB_DIR/" -f bin -o "$output_file" "$asm_file" 2>&1
    
    if [ -f "$output_file" ]; then
        local size=$(stat -c%s "$output_file")
        echo "  -> $output_file ($size bytes)"
    else
        echo "  -> ERROR: Compilation failed!"
        return 1
    fi
}

# If a specific program is specified
if [ $# -eq 1 ]; then
    asm_file="$COMMANDS_DIR/$1.asm"
    if [ ! -f "$asm_file" ]; then
        echo "Error: $asm_file not found!"
        exit 1
    fi
    compile_program "$asm_file"
else
    # Build all .asm files
    echo "Building all userland programs..."
    echo "================================"
    
    for asm_file in "$COMMANDS_DIR"/*.asm; do
        compile_program "$asm_file"
    done
    
    echo "================================"
    echo "Build complete!"
fi
