r"""
Write the Magic Lantern boot card flags (EOS_DEVELOP + BOOTDISK) on a FAT32 volume, on Windows.

FAT32 offsets, as in tools/card-flags/edit_card_flags.py:
  EOS_DEVELOP at 0x47 (volume label field), BOOTDISK at 0x5C.
Refuses unless autoexec.bin exists on the volume: a BOOTDISK card without it
hangs a boot-flag-enabled camera at startup.

Backs up the boot sector before writing and reads it back to verify.
Must be run as Administrator.

usage: python write_boot_flags.py <drive_letter> <backup_dir>
"""
import os
import sys

SECTOR = 512
FLAGS = [(0x47, b"EOS_DEVELOP"), (0x5C, b"BOOTDISK")]


def main():
    letter, backup_dir = sys.argv[1].rstrip(":"), sys.argv[2]
    os.makedirs(backup_dir, exist_ok=True)

    autoexec = rf"{letter}:\autoexec.bin"
    if not os.path.isfile(autoexec) or os.path.getsize(autoexec) == 0:
        raise SystemExit(f"{autoexec} missing or empty, refusing to write BOOTDISK")

    with open(rf"\\.\{letter}:", "r+b", buffering=0) as f:
        sec = bytearray(f.read(SECTOR))
        if len(sec) != SECTOR:
            raise SystemExit("short read")
        with open(os.path.join(backup_dir, "backup_vbr_before_bootflags.bin"), "wb") as b:
            b.write(sec)
        if sec[0x52:0x5A] != b"FAT32   " or sec[0x1FE:0x200] != b"\x55\xAA":
            raise SystemExit("not a FAT32 boot sector")
        for off, flag in FLAGS:
            print(f"0x{off:03X} before: {bytes(sec[off:off + len(flag)])!r}")
            sec[off:off + len(flag)] = flag
        f.seek(0)
        f.write(bytes(sec))
        f.flush()
        os.fsync(f.fileno())
        f.seek(0)
        if bytearray(f.read(SECTOR)) != sec:
            raise SystemExit("VERIFY FAILED")
    print("EOS_DEVELOP + BOOTDISK written and verified")
    print("SCRIPT at 0x1F0 still present:", sec[0x1F0:0x1F6] == b"SCRIPT")


if __name__ == "__main__":
    main()
