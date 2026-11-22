r"""
Write Magic Lantern boot flags (EOS_DEVELOP + BOOTDISK) on an exFAT volume, on Windows.

Offsets as in tools/card-flags/edit_card_flags.py (exFAT): BOOTDISK at 0x7A, EOS_DEVELOP at 0x82,
in both the main boot region (sectors 0..11) and the backup region (sectors 12..23).
The boot region checksum (sector 11 / 23) is recomputed per the exFAT spec:
    checksum = ((checksum >> 1) | (checksum << 31)) + byte   (mod 2^32), skipping bytes 106, 107, 112.
Before writing, the existing checksum is recomputed and must match, which validates the code.
Refuses unless autoexec.bin exists on the volume. Locks and dismounts the volume while writing.
Must be run as Administrator.

usage: python write_exfat_boot_flags.py <drive_letter> <backup_dir>
"""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import sys

SECTOR = 512
REGION = 12 * SECTOR
FLAGS = [(0x7A, b"BOOTDISK"), (0x82, b"EOS_DEVELOP")]

GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
FILE_SHARE_RW = 0x1 | 0x2
OPEN_EXISTING = 3
FSCTL_LOCK_VOLUME, FSCTL_UNLOCK_VOLUME, FSCTL_DISMOUNT_VOLUME = 0x90018, 0x9001C, 0x90020

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.CreateFileW.restype = wt.HANDLE
k32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, wt.LPVOID, wt.DWORD, wt.DWORD, wt.HANDLE]
k32.DeviceIoControl.argtypes = [wt.HANDLE, wt.DWORD, wt.LPVOID, wt.DWORD, wt.LPVOID, wt.DWORD,
                                ctypes.POINTER(wt.DWORD), wt.LPVOID]
k32.SetFilePointerEx.argtypes = [wt.HANDLE, ctypes.c_longlong, ctypes.POINTER(ctypes.c_longlong), wt.DWORD]
k32.ReadFile.argtypes = [wt.HANDLE, wt.LPVOID, wt.DWORD, ctypes.POINTER(wt.DWORD), wt.LPVOID]
k32.WriteFile.argtypes = [wt.HANDLE, wt.LPCVOID, wt.DWORD, ctypes.POINTER(wt.DWORD), wt.LPVOID]
k32.CloseHandle.argtypes = [wt.HANDLE]
INVALID = wt.HANDLE(-1).value


def ioctl(h, code, what):
    n = wt.DWORD()
    if not k32.DeviceIoControl(h, code, None, 0, None, 0, ctypes.byref(n), None):
        raise SystemExit(f"{what} failed, error {ctypes.get_last_error()}")


def seek(h, off):
    if not k32.SetFilePointerEx(h, off, None, 0):
        raise SystemExit(f"seek failed, error {ctypes.get_last_error()}")


def read(h, off, size):
    seek(h, off)
    buf = ctypes.create_string_buffer(size)
    n = wt.DWORD()
    if not k32.ReadFile(h, buf, size, ctypes.byref(n), None) or n.value != size:
        raise SystemExit(f"read failed, error {ctypes.get_last_error()}")
    return bytearray(buf.raw)


def write(h, off, data):
    seek(h, off)
    n = wt.DWORD()
    if not k32.WriteFile(h, bytes(data), len(data), ctypes.byref(n), None) or n.value != len(data):
        raise SystemExit(f"write failed, error {ctypes.get_last_error()}")


def boot_checksum(region):
    c = 0
    for i in range(11 * SECTOR):
        if i in (106, 107, 112):
            continue
        c = ((((c >> 1) | (c << 31)) & 0xFFFFFFFF) + region[i]) & 0xFFFFFFFF
    return c


def main():
    letter, backup_dir = sys.argv[1].rstrip(":"), sys.argv[2]
    os.makedirs(backup_dir, exist_ok=True)
    autoexec = rf"{letter}:\autoexec.bin"
    if not os.path.isfile(autoexec) or os.path.getsize(autoexec) == 0:
        raise SystemExit(f"{autoexec} missing or empty, refusing to write BOOTDISK")

    h = k32.CreateFileW(rf"\\.\{letter}:", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_RW, None,
                        OPEN_EXISTING, 0, None)
    if h == INVALID:
        raise SystemExit(f"open failed, error {ctypes.get_last_error()}")
    try:
        ioctl(h, FSCTL_LOCK_VOLUME, "lock")
        ioctl(h, FSCTL_DISMOUNT_VOLUME, "dismount")
        both = read(h, 0, 2 * REGION)
        with open(os.path.join(backup_dir, "backup_exfat_boot_regions.bin"), "wb") as b:
            b.write(both)
        for base, name in ((0, "main"), (REGION, "backup")):
            region = both[base:base + REGION]
            if region[3:11] != b"EXFAT   " or region[510:512] != b"\x55\xAA":
                raise SystemExit(f"[{name}] not an exFAT boot sector")
            stored = struct.unpack_from("<I", region, 11 * SECTOR)[0]
            if boot_checksum(region) != stored:
                raise SystemExit(f"[{name}] existing checksum mismatch, refusing (implementation or card problem)")
            for off, flag in FLAGS:
                print(f"[{name}] 0x{off:02X} before: {bytes(region[off:off + len(flag)])!r}")
                region[off:off + len(flag)] = flag
            region[11 * SECTOR:12 * SECTOR] = struct.pack("<I", boot_checksum(region)) * 128
            write(h, base, region[0:SECTOR])
            write(h, base + 11 * SECTOR, region[11 * SECTOR:12 * SECTOR])
            check = read(h, base, REGION)
            if check != region or boot_checksum(check) != struct.unpack_from("<I", check, 11 * SECTOR)[0]:
                raise SystemExit(f"[{name}] VERIFY FAILED")
            print(f"[{name}] flags + checksum written and verified")
    finally:
        try:
            ioctl(h, FSCTL_UNLOCK_VOLUME, "unlock")
        finally:
            k32.CloseHandle(h)
    print("DONE")


if __name__ == "__main__":
    main()
