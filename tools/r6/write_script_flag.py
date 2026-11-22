r"""
Write the Canon Basic "SCRIPT" card flag on Windows.

Writes b"SCRIPT" at offset 0x1F0 of:
  1. the FAT32 volume boot sector (via the volume handle \\.\X:)
  2. the sector just before the partition start (via \\.\PhysicalDriveN),
     which is unallocated space; some Canon code looks there on cards > 32 GB.

Backs up both sectors before writing, then reads them back to verify.
Must be run as Administrator.

usage: python write_script_flag.py <drive_letter> <disk_number> <partition_offset_bytes> <backup_dir>
"""
import os
import sys

SECTOR = 512
FLAG = b"SCRIPT"
FLAG_OFF = 0x1F0


def read_sector(f, lba):
    f.seek(lba * SECTOR)
    data = f.read(SECTOR)
    if len(data) != SECTOR:
        raise IOError(f"short read at LBA {lba}")
    return bytearray(data)


def write_sector(f, lba, data):
    assert len(data) == SECTOR
    f.seek(lba * SECTOR)
    f.write(bytes(data))
    f.flush()
    os.fsync(f.fileno())


def patch(path, lba, label, backup_dir, require_fat32=False):
    with open(path, "r+b", buffering=0) as f:
        sec = read_sector(f, lba)
        backup = os.path.join(backup_dir, f"backup_{label}_lba{lba}.bin")
        with open(backup, "wb") as b:
            b.write(sec)
        print(f"[{label}] backup -> {backup}")
        if require_fat32:
            if sec[0x52:0x5A] != b"FAT32   ":
                raise SystemExit(f"[{label}] not a FAT32 boot sector: {bytes(sec[0x52:0x5A])!r}")
            if sec[0x1FE:0x200] != b"\x55\xAA":
                raise SystemExit(f"[{label}] missing 55AA signature")
            if b"BOOTDISK" in sec:
                raise SystemExit(f"[{label}] BOOTDISK already present, refusing")
        old = bytes(sec[FLAG_OFF:FLAG_OFF + len(FLAG)])
        print(f"[{label}] bytes at 0x1F0 before: {old!r}")
        sec[FLAG_OFF:FLAG_OFF + len(FLAG)] = FLAG
        write_sector(f, lba, sec)
        check = read_sector(f, lba)
        if check != sec:
            raise SystemExit(f"[{label}] VERIFY FAILED")
        print(f"[{label}] SCRIPT written and verified at LBA {lba} + 0x1F0")


def main():
    letter, disk, part_off, backup_dir = sys.argv[1].rstrip(":"), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    os.makedirs(backup_dir, exist_ok=True)
    if part_off % SECTOR or part_off < 2 * SECTOR:
        raise SystemExit(f"bad partition offset {part_off}")
    # 1. volume boot sector (LBA 0 of the volume)
    patch(rf"\\.\{letter}:", 0, "vbr", backup_dir, require_fat32=True)
    # 2. sector before partition start, on the physical disk
    gap_lba = part_off // SECTOR - 1
    patch(rf"\\.\PhysicalDrive{disk}", gap_lba, "gap", backup_dir)
    print("DONE")


if __name__ == "__main__":
    main()
