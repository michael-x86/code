

If you are looking for clean APIs and high-level comfort, this is not it.
If you want to see what the machine is actually doing — instruction by instruction — you're in the right place.
# x86 Assembly Kernel

A minimal operating system kernel written entirely in x86 assembly using NASM.

Built from scratch on Linux with no libc, no runtime, and no external abstractions. Every subsystem is implemented directly against the hardware: memory management, interrupts, scheduling, paging, terminal I/O, and disk access.

This project exists to explore bare-metal system design at instruction level precision.

---

## Overview

The kernel boots through a custom bootloader and enters a fully handcrafted 32-bit protected mode environment.

The system currently includes:

- Higher-half kernel mapping
- Paging and virtual memory infrastructure
- Interrupt and exception handling
- PIT-driven preemptive multitasking
- VGA terminal and keyboard input
- Dynamic memory allocation
- ATA PIO disk access
- Simple syscall layer
- Command shell and program loader

No hidden layers exist between the code and the CPU.

If something works, it is because the processor executed exactly the instructions written for it.

---

# Current Features

## Core Architecture

- 32-bit protected mode kernel
- Custom bootloader
- Higher-half kernel mapping at `0xC0000000+`
- Identity mapping preserved during paging transition
- GDT setup and protected mode transition
- Full IDT initialization (256 entries)
- PIC remapping (`IRQ0–IRQ15 → 0x20–0x2F`)
- PIT timer interrupt handling
- IRQ-driven preemptive multitasking
- Round-robin task scheduler
- Software context switching
- Software-only kernel task model

## Memory Management

- Physical memory bitmap allocator
- Virtual heap allocator
- Dynamic page allocation and freeing
- Runtime page mapping (`map_page`)
- Virtual → physical address translation
- Page table management
- TLB invalidation support (`invlpg`)
- Demand-style virtual memory groundwork

## Terminal / Shell

- VGA text-mode terminal
- Higher-half VGA mapping (`0xC00B8000`)
- Hardware cursor management
- PS/2 keyboard input handling
- Scancode → ASCII translation
- Command shell / dispatcher
- Command argument parsing (`argc` / `argv`)
- Command history system
- History navigation (up/down arrows)
- Empty-command filtering

## Disk / Program Loading

- ATA PIO disk driver
- Sector-based program loader
- Dynamic command fallback loader

## Debugging Utilities

- Register dump utilities
- Stack dump utilities
- Hex and decimal formatting utilities
- Memory inspection / dump utilities
- Page fault detection infrastructure

## System Interface

- Basic syscall layer via `int 0x80`
- Kernel-side syscall dispatch table

## Toolchain

- NASM flat-binary build system
- Fully freestanding kernel architecture
- Zero-libc environment

---

# Design Philosophy

## Zero Abstraction

If it is not explicitly written, it does not exist.

## Instruction-Level Control

Every register, flag, interrupt frame, and memory mapping is intentional.

## Hardware-First Engineering

The kernel is designed around CPU behavior and hardware constraints rather than high-level software conventions.

## Constraints as Discipline

- No memory-to-memory instructions
- No undefined behavior
- No compiler-generated runtime
- No dependency on external libraries

---

# Technical Notes

- Language: x86 Assembly (NASM syntax)
- Architecture: IA-32 / x86
- Execution mode: 32-bit protected mode
- Memory model: Flat memory model
- Build format: Raw flat binaries
- Runtime: Freestanding

---

# Project Structure

```text
/boot        - Bootloader and mode transition code
/kernel      - Core kernel systems
```

---

# Build & Run

## Requirements

- NASM
- QEMU

## Build

```bash
nasm -f bin kernel.asm -o kernel.bin
nasm -f bin bootloader.asm -o bootloader.bin

cat bootloader.bin kernel.bin > os.img
```

## Run

```bash
qemu-system-i386 \
-drive format=raw,file=os.img \
-device isa-debug-exit,iobase=0xf4,iosize=0x04
```

---

# Development Status

Actively developed.

The project is experimental and intentionally low-level. Expect architectural changes, broken interfaces, and unfinished subsystems while core infrastructure evolves.

---

# Planned Work

- Networking stack

---

# Why This Exists

Modern systems hide the machine behind layers of abstraction.

This project removes those layers completely.

The goal is not convenience.  
The goal is understanding:

- how interrupts actually work
- how paging behaves
- how context switching happens
- how hardware is programmed directly
- how operating systems function beneath modern tooling

Everything here is built directly against the architecture, one instruction at a time.
```
