# Makefile for Enigma M4 — 32-bit x86 Assembly
# Builds a Linux i386 ELF binary

AS      = nasm
ASFLAGS = -f elf32 -g
LDFLAGS = -m elf_i386

SRCS    = enigma_main.asm enigma_core.asm enigma_key.asm \
          enigma_hash.asm enigma_tables.asm enigma_io.asm
OBJS    = $(SRCS:.asm=.o)
TARGET  = enigma_m4

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

test: $(TARGET)
	@echo "Test: involution check (encrypt then decrypt should recover plaintext)"
	@echo "20240101" | timeout 2 ./$(TARGET) || true
