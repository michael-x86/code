# Makefile for x86 OS userland programs
# Usage: make [program_name] or make all or make clean

PROJECT_ROOT = /home/janko/dev/code
COMMANDS_DIR = $(PROJECT_ROOT)/commands
BUILD_DIR = $(PROJECT_ROOT)/build/bin
LIB_DIR = $(PROJECT_ROOT)/lib

# All .asm files in commands/
ASM_FILES = $(wildcard $(COMMANDS_DIR)/*.asm)
PROGRAMS = $(notdir $(ASM_FILES:.asm=))

# Default target
.PHONY: all clean help list $(PROGRAMS)

all: $(addprefix $(BUILD_DIR)/, $(PROGRAMS))

# Rule for each program name (make alloc, make cat, etc.)
$(PROGRAMS):
	@echo "Compiling: $@"
	@mkdir -p $(BUILD_DIR)
	@nasm -i $(LIB_DIR)/ -f bin -o $(BUILD_DIR)/$@ $(COMMANDS_DIR)/$@.asm
	@echo "  -> $(BUILD_DIR)/$@ ($(shell stat -c%s $(BUILD_DIR)/$@ 2>/dev/null || echo '?') bytes)"

# Pattern rule for full path compilation
$(BUILD_DIR)/%: $(COMMANDS_DIR)/%.asm | $(BUILD_DIR)
	@echo "Compiling: $*"
	@nasm -i $(LIB_DIR)/ -f bin -o $@ $<
	@echo "  -> $@ ($(shell stat -c%s $@) bytes)"

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned build directory"

# Help
help:
	@echo "Usage:"
	@echo "  make              - Build all programs"
	@echo "  make alloc        - Build single program (e.g., alloc)"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make list         - List available programs"
	@echo "  make help         - Show this help"

# List available programs
list:
	@echo "Available programs:"
	@for prog in $(PROGRAMS); do echo "  $$prog"; done
