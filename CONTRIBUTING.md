# Contributing to the x86 Kernel

First of all: thanks for stopping by.

Whether you're fixing a typo, optimizing a few instructions, adding a new syscall, or making the scheduler slightly less angry, contributions are welcome.

This project is intentionally low-level. Most of the code is written in NASM, runs without an operating system, and occasionally reminds you that the CPU does exactly what you told it to do—not what you meant.

---

# Getting Started

1. Fork the repository.
2. Clone your fork.

```bash
git clone https://github.com/michael-x86/code.git
cd code
```

3. Create a branch.

```bash
git checkout -b feature/my-awesome-feature
```

or

```bash
git checkout -b fix/my-bug
```

4. Make your changes.
5. Build and test.
6. Push your branch.
7. Open a Pull Request.

---

# Development Environment

You'll need:

* **NASM** — assembler
* **Python 3** — used by `gen_fs.py`
* **QEMU (qemu-system-i386)** — because rebooting a real machine 400 times gets old

---

# Building

Build only:

```bash
make
```

Run in fullscreen:

```bash
make fullscreen
```

If QEMU immediately triple-faults, congratulations—you've discovered another exciting debugging session.

---

# Coding Style

## NASM Assembly

Keep things consistent.

### Labels

Use lowercase with underscores.

```asm
setup_paging
.next_page
copy_kernel
```

### Constants

Use uppercase with underscores.

```asm
CODE_SEG
DATA_SEG
PAGE_PRESENT
```

### Comments

Document things that aren't immediately obvious.

Good comments explain:

* hardware assumptions
* register usage
* side effects
* why something exists

Bad comments explain that:

```asm
inc eax ; increment eax
```

We know.

Separate major sections with:

```asm
; ------------------------------------------------------------
; Paging initialization
; ------------------------------------------------------------
```

### Function Documentation

Every non-trivial function should describe its interface.

```asm
; ------------------------------------------------------------
; Allocate one physical page
;
; in:
;   none
;
; out:
;   eax = physical address
;   CF  = 0 success
;   CF  = 1 no free pages
; ------------------------------------------------------------
alloc_page:
```

Preserve registers whenever appropriate and match the surrounding code style.

---

## Python (`gen_fs.py`)

* Follow PEP 8.
* Comment serialization logic.
* Document any changes to the filesystem layout.
* If you modify the 68-byte record format, update the documentation.

Future you will appreciate it.

---

# Testing

Before opening a Pull Request:

* Build successfully.
* Boot successfully.
* Test the feature you changed.
* Verify you didn't accidentally break something unrelated.

If your change affects:

* paging
* interrupts
* task switching
* ATA
* memory allocation

please test those paths carefully.

"Compiles" is not the same thing as "works."

---

# Commit Messages

Keep the first line short (50 characters or fewer).

Good:

```
Add sys_read syscall
```

```
Fix page allocator overflow
```

```
Improve ATA sector validation
```

Less good:  

```
Fixed some stuff 
```

```
Update
```

The commit body should explain **why** the change exists.

Example:

```text
Add higher-half task mapping

Future user-mode processes require isolated address spaces.
This introduces separate page table entries that prepare the
scheduler for per-task virtual memory.
```

Reference issues when appropriate.

---

# What Can I Contribute?

Pretty much anything.

Ideas include:

* Bug fixes
* Performance improvements
* Documentation
* New shell commands
* New syscalls
* Memory management improvements
* Better debugging tools
* Scheduler improvements
* Filesystem enhancements
* Keyboard or ATA improvements
* New examples

If it makes the kernel smaller, faster, cleaner, or easier to understand, it's probably welcome.

---

# Pull Request Checklist

Before clicking **Create Pull Request**:

* [ ] Code builds successfully.
* [ ] Kernel still boots.
* [ ] New assembly functions include `in:` / `out:` documentation.
* [ ] Comments explain *why*, not just *what*.
* [ ] Commit messages are meaningful.
* [ ] Documentation was updated if necessary.
* [ ] No unrelated changes accidentally slipped in.
* [ ] You resisted the urge to `jmp $` as a permanent solution.

---

# Debugging Wisdom

A few timeless observations:

* If everything suddenly stops working, check the stack.
* If the stack looks fine, check your segment registers.
* If those look fine, it's probably paging.
* If it isn't paging... it will be eventually.
* Undefined behavior is just undocumented creativity.
* There are only two hard problems in kernel development:

  1. Interrupts
  2. Paging
  3. Counting

---

# Questions?

Open an issue using the **question** label.

Bug reports are most useful when they include:

* what happened
* what you expected
* how to reproduce it
* emulator or hardware used
* screenshots if applicable

Bonus points if you've already narrowed the problem down.

---

# License

By contributing, you agree that your contributions are licensed under the MIT License.

---

Happy hacking.

And remember:

> Every kernel eventually reaches a point where adding one `push` fixes everything... until the next reboot.
