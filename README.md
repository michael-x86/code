

If you are looking for clean APIs and high-level comfort, this is not it.
If you want to see what the machine is actually doing — instruction by instruction — you're in the right place.
# x86 Assembly Kernel

A minimal operating system kernel written entirely in x86 assembly using NASM.

Built from scratch on Linux with no libc, no runtime, and no external abstractions. 
Every subsystem is implemented directly against the hardware: 
memory management, interrupts, scheduling, paging, terminal I/O, and disk access.

This project exists to explore bare-metal system design at instruction level precision.

---

## Overview

The kernel boots through a custom bootloader and enters a fully handcrafted 
32-bit protected mode environment.

The system currently includes:

- Paging and virtual memory infrastructure
- PIT-driven preemptive multitasking
- VGA terminal and keyboard input
- ATA PIO disk access
- Command shell and program loader
- Full 32-bit protected mode operation
- Round-robin task switching with 3 concurrent tasks
- Memory Management - Page-based with bitmap tracking
- Full IDT setup with IRQ remapping
- Syscalls (0x80 interface)

No hidden layers exist between the code and the CPU.
If something works, it is because the processor executed exactly the instructions written for it.

# Filesystem                                                              
                                                                              
  • **Simple Inode-based FS** - Directory structure with file abstraction     
  • **File Operations** - Read, list, navigate directories                    
  • **Program Loading** - Execute binaries from disk                          
  • **Path Resolution** - Absolute and relative path support
---

## Debugging Utilities

- Register dump utilities
- Stack dump utilities
- Hex and decimal formatting utilities
- Memory inspection / dump utilities
- Page fault detection infrastructure

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
- QEMU```

---

# Development Status

Actively developed.

The project is experimental and intentionally low-level.

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
