# x86 Assembly Kernel Project

A minimal, terminal-driven operating system kernel written entirely in x86 assembly (NASM), built from scratch on Linux. No abstractions. No runtime. Just silicon, opcodes, and intent.

## Overview

This project is a deep dive into bare-metal system design, where every instruction is deliberate and every byte has a purpose. The kernel boots via a custom bootloader and drops straight into a handcrafted terminal environment.

No compiler safety nets. No hidden layers. 
If something works, it’s because the CPU executed exactly what was written — nothing more, nothing less.

## Current Features

* 32-bit protected mode kernel
* Custom bootloader (done, no training wheels needed)
* Text-mode terminal interface
* Keyboard input via direct hardware handling
* Command parsing (no libc, no excuses)
* Command arguments support
* Fixed command history (64 × 64-byte entries)
* History navigation (up/down, no wrap-around hacks)
* Duplicate and empty command filtering
* Memory (de)allocation
* Paging (page tables, virtual memory groundwork)
* Timer interrupt handling (PIT-driven)
* IDT and "full" interrupt coverage 

## Design Philosophy

* **Zero abstraction**: If it isn’t explicitly written, it doesn’t exist
* **Instruction-level control**: Every register, every flag, every side effect matters
* **Hardware-first**: Designed with the CPU, not for it
* **Constraints as discipline**: No memory-to-memory ops, no shortcuts, no undefined behavior tolerated

## Technical Notes

* NASM syntax
* x86 target
* Runs in protected mode
* Flat memory model (for now)
* Manual register and stack discipline at all times

## Project Structure

```id="z91kdp"
/boot        - Bootloader (real mode → protected mode jump)
/kernel      - Core kernel logic
```

## Build & Run

Requirements:

* NASM
* QEMU (for sanity preservation)

Example:

```id="7n4q2y"
nasm -f bin kernel.asm -o kernel.bin
nasm -f bin bootloader.asm -o bootloader.bin
cat bootloader.bin kernel.bin > os.img
emu-system-i386 -full-screen \
-drive format=raw,file=os.img \
-device isa-debug-exit,iobase=0xf4,iosize=0x04
```

## Roadmap

* Expanded command system
* Paging and real memory management
* Full interrupt coverage
* Timer-driven scheduling

## Why This Exists

Because understanding modern systems requires going below them. 
This kernel is not about convenience — it's about control, learning, and pushing hardware directly.
This strips everything down to the metal — no frameworks, no APIs, no illusions. 
Just direct interaction with the machine, one instruction at a time.

## Status

Actively developed. Expect rough edges, breaking changes, and unapologetically low-level code.

If you are looking for clean APIs and high-level comfort, this is not it.
If you want to see what the machine is actually doing — instruction by instruction — you're in the right place.
