# Filesystem

## Overview

The kernel includes an in-kernel virtual filesystem generated at build time from
the project's content directories. It supports directory listing, file read/write,
file creation and deletion, and persistence of runtime-created files across
reboots.

## Structure

Each file record is 68 bytes of metadata plus a 1024-byte content buffer.

```
Offset  Size  Field
0       4     parent inode index
4       4     type (0 = directory, 1 = file)
8       4     content length
12      4     start LBA (for persisted content on disk)
16      48    name (null-terminated, max 47 chars + NUL)
64      4     valid flag
```

## Content Directories

At build time, `gen_fs.py` walks the project's content directories and embeds
their contents into the kernel image:

| Host path     | In-OS path  | Description                    |
|---------------|-------------|--------------------------------|
| `build/bin/`  | `/bin/`     | Compiled userland programs     |
| `build/proc/` | `/proc/`    | System information files       |
| `build/etc/`  | `/etc/`     | Configuration files            |
| `build/var/log/` | `/var/log/` | Log files                |

Anything you place in these host directories appears at the matching path inside
the OS. Rebuilding picks up changes automatically.

## Persistence

- **Build-seeded files** (`/proc`, `/etc`, `/var/log`, `/bin`) live inside the
  kernel image. They reset to build-time content on every boot.
- **Runtime-created files** live in 16 "spare slots." Each slot occupies a
  fixed 3-sector region on disk starting at LBA 256. On `create` / `write` /
  `unlink`, the kernel updates RAM and immediately writes the slot via PIO ATA.
- On boot, each spare slot is read back from disk and replayed into RAM.
- The build script backs up the FS region before reassembling the kernel and
  restores it after, so persisted files survive rebuilds.

## Limits

| Constant         | Value   | Description                    |
|------------------|---------|--------------------------------|
| `FS_CAPACITY`    | 1024    | Max bytes per file             |
| `FS_SPARE_COUNT` | 16      | Number of runtime-creatable files |
| Max name length  | 47      | Characters (48 including NUL)  |

## Paths

- Absolute: `/proc/cpuinfo`
- Relative: `cat foo`
- `.` and `..` are supported
- **Not supported**: multi-level relative paths like `cd foo/bar` (only a single
  relative component)
