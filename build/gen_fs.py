#!/usr/bin/env python3
"""
Generate the initial filesystem image (fs.bin) for the block-based filesystem.

On-disk layout:
  LBA 0       bootloader (not touched here)
  LBA 1       superblock (512 bytes)
  LBA 2..N    inode table (INODE_COUNT * INODE_SIZE bytes, sector-aligned)
  LBA N+1..M  block bitmap (1 bit per data block, sector-aligned)
  LBA M+1..   data blocks (BLOCK_SIZE = 4096 bytes each)

Directory entries are stored in data blocks with the format:
  dword  inode_number
  word   rec_len
  byte   name_len
  byte   file_type (1=file 2=dir)
  char[] name
"""
import os, sys, struct

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."
OUT  = sys.argv[2] if len(sys.argv) > 2 else "fs.bin"

# Must match constants.inc
BLOCK_SIZE    = 4096
BLOCK_SECTORS = 8        # 4096 / 512
INODE_SIZE    = 128
INODE_COUNT   = 256
INODE_TABLE_LBA = 2
FS_MAGIC      = 0xEF53
INOFS_CRC     = 126      # word: CRC-16 of inode bytes [0, INOFS_CRC)


# CRC-16/CCITT (poly 0x1021, init 0xFFFF, MSB-first) — must match ecc.inc.
def _build_crc16_table():
    table = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if (c & 0x8000) else (c << 1)
            c &= 0xFFFF
        table.append(c)
    return table

_CRC16_TABLE = _build_crc16_table()

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc = ((crc << 8) ^ _CRC16_TABLE[((crc >> 8) ^ b) & 0xFF]) & 0xFFFF
    return crc

# Default directories to create at build time
default_dirs = ["/", "/bin", "/proc", "/var", "/var/log", "/usr", "/dev", "/lib", "/etc"]
scan = [("bin", "exec"), ("proc", "file"), ("var/log", "file"),
        ("usr", "file"), ("dev", "file"), ("lib", "file"), ("etc", "file")]

# Gather files to embed
entries = []   # (vpath, kind, host_path|None)
for d in default_dirs:
    entries.append((d, "dir", None))

for top, kind in scan:
    base = os.path.join(ROOT, top)
    if not os.path.isdir(base):
        continue
    for name in sorted(os.listdir(base)):
        host = os.path.join(base, name)
        if not os.path.isfile(host):
            continue
        sz = os.path.getsize(host)
        entries.append(("/" + top + "/" + name, kind, host))

# Compute layout
inode_table_bytes = INODE_COUNT * INODE_SIZE
inode_table_sectors = (inode_table_bytes + 511) // 512

# Estimate data blocks needed
file_blocks_needed = 0
dir_count = 1  # root
file_count = 0
for vpath, kind, host in entries:
    if host is not None:
        sz = os.path.getsize(host)
        blocks = (sz + BLOCK_SIZE - 1) // BLOCK_SIZE
        if blocks == 0:
            blocks = 1
        file_blocks_needed += blocks
        file_count += 1
    elif kind == "dir" and vpath != "/":
        dir_count += 1

dir_blocks_needed = dir_count  # each dir needs at least 1 block
total_data_blocks = file_blocks_needed + dir_blocks_needed + 64  # spare

# Block bitmap
bitmap_bytes = (total_data_blocks + 7) // 8
bitmap_sectors = (bitmap_bytes + 511) // 512

# Data region
data_start_lba = INODE_TABLE_LBA + inode_table_sectors + bitmap_sectors
total_disk_sectors = data_start_lba + total_data_blocks * BLOCK_SECTORS

print(f"  Layout: superblock=LBA1, inode_table=LBA{INODE_TABLE_LBA}..{INODE_TABLE_LBA+inode_table_sectors-1}, "
      f"bitmap=LBA{INODE_TABLE_LBA+inode_table_sectors}..{data_start_lba-1}, "
      f"data=LBA{data_start_lba}..{total_disk_sectors-1}")
print(f"  Inodes: {INODE_COUNT}, Data blocks: {total_data_blocks}, "
      f"Capacity: {total_data_blocks * BLOCK_SIZE // 1024} KB")

# State (must be defined before use in superblock or elsewhere)
next_inode = 1  # 0 reserved for root
next_block = 1  # 1-based: 0 means "no block" in direct block pointers

# Build superblock (512 bytes)
superblock = bytearray(512)
struct.pack_into('<I', superblock, 0, FS_MAGIC)           # magic
struct.pack_into('<I', superblock, 4, BLOCK_SIZE)          # block_size
struct.pack_into('<I', superblock, 8, total_data_blocks)   # total_blocks
struct.pack_into('<I', superblock, 12, INODE_COUNT)        # inode_count
# free_blocks and free_inodes populated after all allocations
struct.pack_into('<I', superblock, 24, INODE_TABLE_LBA)    # inode_table_lba
struct.pack_into('<I', superblock, 28, INODE_TABLE_LBA + inode_table_sectors)  # bitmap_lba
struct.pack_into('<I', superblock, 32, data_start_lba)     # data_lba
struct.pack_into('<I', superblock, 36, 1)                  # mount_count
struct.pack_into('<I', superblock, 40, total_data_blocks)  # max_blocks

# Build inode table (all zeros = all free)
inode_table = bytearray(inode_table_sectors * 512)

# Build block bitmap (all zeros = all free)
bitmap = bytearray(bitmap_sectors * 512)

# State
next_inode = 1  # 0 reserved for root
next_block = 1  # 1-based: 0 means "no block" in direct block pointers

def alloc_inode(inode_type):
    global next_inode
    if next_inode >= INODE_COUNT:
        raise RuntimeError("Out of inodes")
    inum = next_inode
    next_inode += 1
    offset = inum * INODE_SIZE
    struct.pack_into('<I', inode_table, offset + 0, inode_type)
    struct.pack_into('<I', inode_table, offset + 4, 0)
    struct.pack_into('<I', inode_table, offset + 8, 0)
    return inum

def alloc_block():
    global next_block
    if next_block >= total_data_blocks:
        raise RuntimeError("Out of data blocks")
    bnum = next_block
    next_block += 1
    return bnum

def set_inode_block(inode_num, block_index, block_num):
    offset = inode_num * INODE_SIZE + 12 + block_index * 4
    struct.pack_into('<I', inode_table, offset, block_num)

def make_dirent(inode_num, name, file_type):
    name_bytes = name.encode('ascii', errors='replace')
    name_len = len(name_bytes)
    rec_len = 8 + name_len
    if rec_len % 4 != 0:
        rec_len += 4 - (rec_len % 4)
    entry = struct.pack('<IHBB', inode_num, rec_len, name_len, file_type)
    entry += name_bytes
    entry += b'\x00' * (rec_len - len(entry))
    return entry

# Track directory entries: (block_num, name, child_inode, file_type)
dir_entries = []

def add_dir_entry(dir_inode, name, child_inode, file_type):
    offset = dir_inode * INODE_SIZE + 12
    block_num = struct.unpack_from('<I', inode_table, offset)[0]
    if block_num == 0:
        block_num = alloc_block()
        struct.pack_into('<I', inode_table, offset, block_num)
    dir_entries.append((block_num, name, child_inode, file_type))

# Create root directory (inode 0)
struct.pack_into('<I', inode_table, 0 * INODE_SIZE + 0, 2)  # type = dir

path_to_inode = {"/": 0}

# Create directories
for vpath, kind, host in entries:
    if kind == "dir" and vpath != "/":
        parts = vpath.rstrip("/").split("/")
        parent_path = "/".join(parts[:-1])
        if parent_path == "":
            parent_path = "/"
        parent_inum = path_to_inode.get(parent_path, 0)
        name = parts[-1]
        inum = alloc_inode(2)
        path_to_inode[vpath] = inum
        add_dir_entry(parent_inum, name, inum, 2)

# Create files
for vpath, kind, host in entries:
    if host is not None:
        parts = vpath.rstrip("/").split("/")
        parent_path = "/".join(parts[:-1])
        if parent_path == "":
            parent_path = "/"
        parent_inum = path_to_inode.get(parent_path, 0)
        name = parts[-1]
        sz = os.path.getsize(host)
        inum = alloc_inode(1)
        struct.pack_into('<I', inode_table, inum * INODE_SIZE + 4, sz)
        blocks_needed = (sz + BLOCK_SIZE - 1) // BLOCK_SIZE
        if blocks_needed == 0:
            blocks_needed = 1
        for b in range(min(blocks_needed, 12)):
            block_num = alloc_block()
            set_inode_block(inum, b, block_num)
        path_to_inode[vpath] = inum
        file_type = 2 if kind == "exec" else 1
        add_dir_entry(parent_inum, name, inum, file_type)

# Write directory entries into data blocks
dir_block_data = {}
for dir_path, inum in path_to_inode.items():
    # Only directory inodes own dir-entry blocks. Files are listed in
    # path_to_inode too, but their data blocks hold file content and must
    # NOT be registered here (otherwise the write loop below would clobber
    # the file content with a zero-filled "directory" block).
    inode_type = struct.unpack_from('<I', inode_table, inum * INODE_SIZE)[0]
    if inode_type != 2:
        continue
    offset = inum * INODE_SIZE + 12
    block_num = struct.unpack_from('<I', inode_table, offset)[0]
    if block_num == 0:
        block_num = alloc_block()
        struct.pack_into('<I', inode_table, offset, block_num)
    if block_num not in dir_block_data:
        dir_block_data[block_num] = bytearray(BLOCK_SIZE)

for block_num, name, child_inode, file_type in dir_entries:
    data = dir_block_data.get(block_num)
    if data is None:
        data = bytearray(BLOCK_SIZE)
        dir_block_data[block_num] = data
    pos = 0
    while pos + 8 <= BLOCK_SIZE:
        inum_at = struct.unpack_from('<I', data, pos)[0]
        rec_len = struct.unpack_from('<H', data, pos + 4)[0]
        if inum_at == 0 or rec_len == 0:
            break
        pos += rec_len
    entry = make_dirent(child_inode, name, file_type)
    data[pos:pos+len(entry)] = entry

# Update superblock free counts to reflect actual usage
struct.pack_into('<I', superblock, 16, total_data_blocks - next_block)  # free_blocks
struct.pack_into('<I', superblock, 20, INODE_COUNT - next_inode)  # free_inodes

# Mark used blocks in bitmap (0 to next_block-1)
for i in range(next_block):
    byte_idx = i // 8
    bit_idx = i % 8
    bitmap[byte_idx] |= (1 << bit_idx)

# Stamp each allocated inode with its CRC-16 (matches write_inode in the
# kernel). Free inodes (type 0) carry no checksum and are left untouched
# so the kernel's verify_inode skips them.
for inum in range(INODE_COUNT):
    off = inum * INODE_SIZE
    itype = struct.unpack_from('<I', inode_table, off)[0]
    if itype == 0:
        continue
    crc = crc16(inode_table[off:off + INOFS_CRC])
    struct.pack_into('<H', inode_table, off + INOFS_CRC, crc)

# Build output: superblock + inode table + bitmap + data blocks
output = bytearray()
output += superblock
# Pad to inode_table_lba (LBA 2): 1 sector gap after superblock
output += b'\x00' * ((INODE_TABLE_LBA - 1) * 512)
output += inode_table
# Pad to bitmap_lba
bitmap_lba_offset = (INODE_TABLE_LBA + inode_table_sectors) * 512
current_offset = len(output)
output += b'\x00' * (bitmap_lba_offset - current_offset)
output += bitmap
# Pad to data_lba
data_lba_offset = data_start_lba * 512
current_offset = len(output)
output += b'\x00' * (data_lba_offset - current_offset)

# The kernel treats block_num 0 as "no block" and calculates sector as block_num * 8 + data_lba.
# This means the first valid block (block 1) must be at data_lba + 8 sectors.
# We pad one block (8 sectors) so that block 1 aligns correctly.
output += b'\x00' * BLOCK_SIZE

# Map file blocks: (inode_num, block_index) -> (host_path, file_offset)
file_block_map = {}
for vpath, kind, host in entries:
    if host is not None and vpath in path_to_inode:
        inum = path_to_inode[vpath]
        sz = os.path.getsize(host)
        blocks_needed = (sz + BLOCK_SIZE - 1) // BLOCK_SIZE
        if blocks_needed == 0:
            blocks_needed = 1
        for bi in range(min(blocks_needed, 12)):
            offset = inum * INODE_SIZE + 12 + bi * 4
            bn = struct.unpack_from('<I', inode_table, offset)[0]
            file_block_map[bn] = (host, bi * BLOCK_SIZE)

for bnum in range(1, next_block):
    if bnum in dir_block_data:
        output += dir_block_data[bnum]
    elif bnum in file_block_map:
        host, offset = file_block_map[bnum]
        with open(host, 'rb') as f:
            f.seek(offset)
            block_data = f.read(BLOCK_SIZE)
        block_data = block_data + b'\x00' * (BLOCK_SIZE - len(block_data))
        output += block_data[:BLOCK_SIZE]
    else:
        output += bytearray(BLOCK_SIZE)

# Pad to sector boundary
while len(output) % 512 != 0:
    output += b'\x00'

with open(OUT, 'wb') as f:
    f.write(output)

total_kb = len(output) // 1024
print(f"  Wrote {OUT}: {len(output)} bytes ({total_kb} KB)")
print(f"  Files: {file_count}, Directories: {dir_count}")
print(f"  Inodes used: {next_inode}/{INODE_COUNT}, Blocks used: {next_block}/{total_data_blocks}")
