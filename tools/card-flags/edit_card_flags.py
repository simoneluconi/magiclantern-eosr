#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright 2025 Kajetan Krykwiński (@kitor)
# Added SCRIPT flag support on FAT16/32 and to remove already set flags.
# Implemented argparse, rewritten a bit for modern Python
#
# Copyright © 2013 Diego Elio Pettenò <flameeyes@flameeyes.eu>
# Inspired by Trammel's and arm.indiana@gmail.com's shell script
#
#         DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
#                     Version 2, December 2004
#
#  Copyright (C) 2004 Sam Hocevar <sam@hocevar.net>
#
#  Everyone is permitted to copy and distribute verbatim or modified
#  copies of this license document, and changing it is allowed as long
#  as the name is changed.
#
#             DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
#    TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION
#
#   0. You just DO WHAT THE FUCK YOU WANT TO.
#
#
# patch the SD/CF card bootsector to make it bootable on Canon DSLR
# See http://chdk.setepontos.com/index.php/topic,4214.0.html
#
# Supports FAT16, FAT32 and EXFAT devices, on Linux, macOS and Windows,
# and on disk image files.

description = """
Write card flags.

To make card Magic Lantern / CHDK bootable, at least EOS_DEVELOP and BOOTDISK
has to be set. For Canon Basic scripting (CHDK, MagicLantern on Digic 8 and up)
SCRIPT flag has to be set too.

If only partition is provided, script defaults to setting all three flags.

The partition is a block device on Linux / macOS (e.g. /dev/sdb1), a drive
letter on Windows (e.g. E: - run as Administrator), or a disk image file.

Note that SCRIPT flag works only with FAT16/FAT32 formatted cards.

This is a design decision, as we found Canon code has a bug in handling
32GB and less EXFAT cards.
On cards above 32GB flag would go to "sector before partition start", while
on 32GB or less it is required in partition MBR at position that simply
destroys EXFAT filesystem.
Likely an oversight as Canon code will format only > 32G cards as EXFAT.

We recently found out that old cameras (tested on 70D) have problems with
booting from some EXFAT cards. 16G cards created on Linux worked fine on
77D and R, but bootloader failed to find autoexec.bin on 70D.
Which means camera sees flags fine but can't find the file. Yet the card
works fine in camera after flags removal. At the moment of writing
reason is unknown.
64GB EXFAT card formatted in camera worked fine. Possibly a bug in bootloader
implementation of exfat in older models.
"""

import os
import re
import sys
import struct
import argparse
from argparse import RawDescriptionHelpFormatter
from dataclasses import dataclass

SECTOR = 512
# the longest boot record we write to: exFAT main + backup boot region
BOOT_AREA = 24 * SECTOR
EXFAT_REGION = 12 * SECTOR

@dataclass
class FAT16:
    eos_develop: int = 43
    bootdisk: int = 64
    script: int = 496

@dataclass
class FAT32:
    eos_develop: int = 71
    bootdisk: int = 92
    script: int = 496

@dataclass
class EXFAT:
    eos_develop: int = 130
    bootdisk: int = 122
    script: int = None


class FileDevice:
    """A block device or an image file, through plain reads and writes
    (mmap doesn't work on devices everywhere)."""

    def __init__(self, path):
        self.f = open(path, 'r+b', buffering=0)

    def read(self, offset, size):
        self.f.seek(offset)
        data = self.f.read(size)
        if len(data) != size:
            raise SystemExit(f'Short read at {offset:x}: {len(data)} of {size} bytes')
        return bytearray(data)

    def write(self, offset, data):
        self.f.seek(offset)
        if self.f.write(bytes(data)) != len(data):
            raise SystemExit(f'Short write at {offset:x}')
        os.fsync(self.f.fileno())

    def close(self):
        self.f.close()


class WindowsVolume:
    """A Windows volume (\\\\.\\E:), locked and dismounted while open.
    Needs Administrator rights."""

    def __init__(self, letter):
        import ctypes
        import ctypes.wintypes as wt
        self.ctypes, self.wt = ctypes, wt
        k32 = ctypes.WinDLL('kernel32', use_last_error=True)
        k32.CreateFileW.restype = wt.HANDLE
        k32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, wt.LPVOID,
                                    wt.DWORD, wt.DWORD, wt.HANDLE]
        k32.DeviceIoControl.argtypes = [wt.HANDLE, wt.DWORD, wt.LPVOID, wt.DWORD,
                                        wt.LPVOID, wt.DWORD,
                                        ctypes.POINTER(wt.DWORD), wt.LPVOID]
        k32.SetFilePointerEx.argtypes = [wt.HANDLE, ctypes.c_longlong,
                                         ctypes.POINTER(ctypes.c_longlong), wt.DWORD]
        k32.ReadFile.argtypes = [wt.HANDLE, wt.LPVOID, wt.DWORD,
                                 ctypes.POINTER(wt.DWORD), wt.LPVOID]
        k32.WriteFile.argtypes = [wt.HANDLE, wt.LPCVOID, wt.DWORD,
                                  ctypes.POINTER(wt.DWORD), wt.LPVOID]
        k32.CloseHandle.argtypes = [wt.HANDLE]
        self.k32 = k32

        GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
        FILE_SHARE_READ_WRITE = 0x1 | 0x2
        OPEN_EXISTING = 3
        self.h = k32.CreateFileW(rf'\\.\{letter}:', GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ_WRITE, None, OPEN_EXISTING, 0, None)
        if self.h == wt.HANDLE(-1).value:
            raise SystemExit(f'Cannot open {letter}: (error {ctypes.get_last_error()}); '
                             'run as Administrator')
        # the file system must not write the boot sectors back over ours
        self._ioctl(0x90018, 'lock volume')       # FSCTL_LOCK_VOLUME
        self._ioctl(0x90020, 'dismount volume')   # FSCTL_DISMOUNT_VOLUME

    def _ioctl(self, code, what):
        n = self.wt.DWORD()
        if not self.k32.DeviceIoControl(self.h, code, None, 0, None, 0,
                                        self.ctypes.byref(n), None):
            raise SystemExit(f'Cannot {what} (error {self.ctypes.get_last_error()}); '
                             'close any program using the card')

    def _seek(self, offset):
        if not self.k32.SetFilePointerEx(self.h, offset, None, 0):
            raise SystemExit(f'Seek failed (error {self.ctypes.get_last_error()})')

    def read(self, offset, size):
        self._seek(offset)
        buf = self.ctypes.create_string_buffer(size)
        n = self.wt.DWORD()
        if not self.k32.ReadFile(self.h, buf, size, self.ctypes.byref(n), None) \
                or n.value != size:
            raise SystemExit(f'Read failed (error {self.ctypes.get_last_error()})')
        return bytearray(buf.raw)

    def write(self, offset, data):
        self._seek(offset)
        n = self.wt.DWORD()
        if not self.k32.WriteFile(self.h, bytes(data), len(data),
                                  self.ctypes.byref(n), None) or n.value != len(data):
            raise SystemExit(f'Write failed (error {self.ctypes.get_last_error()})')

    def close(self):
        try:
            self._ioctl(0x9001C, 'unlock volume')  # FSCTL_UNLOCK_VOLUME
        finally:
            self.k32.CloseHandle(self.h)


def open_device(name):
    if os.name == 'nt' and re.fullmatch(r'[A-Za-z]:?', name):
        return WindowsVolume(name[0].upper())
    return FileDevice(name)


def setboot(dev, base, offsets, args):
    print('Applying parameters on device')
    if offsets.eos_develop and args.eos_develop:
        off = base + offsets.eos_develop
        print(f'  writing EOS_DEVELOP at offset {off:x} (Volume Label)')
        dev[off:off+11] = b'EOS_DEVELOP'
    if offsets.bootdisk and args.bootdisk:
        off = base + offsets.bootdisk
        print(f'  writing BOOTDISK at offset {off:x}  (Boot code)')
        dev[off:off+8] = b'BOOTDISK'
    if offsets.script and args.script:
        off = base + offsets.script
        print(f'  writing SCRIPT at offset {off:x}')
        dev[off:off+6] = b'SCRIPT'

    if not offsets.script and args.script:
       print("  Selected FS doesn't support SCRIPT flag, ommiting!")


    if args.remove_unused_flags:
        if offsets.eos_develop and not args.eos_develop:
            off = base + offsets.eos_develop
            print(f'  removing EOS_DEVELOP at offset {off:x} (Volume Label)')
            dev[off:off+11] = b'\0\0\0\0\0\0\0\0\0\0\0'
        if offsets.bootdisk and not args.bootdisk:
            off = base + offsets.bootdisk
            print(f'  removing BOOTDISK at offset {off:x} (Boot code)')
            dev[off:off+8] =  b'\0\0\0\0\0\0\0\0'
        if offsets.script and not args.script:
            off = base + offsets.script
            print(f'  removing SCRIPT at offset {off:x}')
            dev[off:off+6] = b'\0\0\0\0\0\0'


def exfat_checksum(area, base):
    # boot region checksum over sectors 0..10, skipping VolumeFlags (106, 107)
    # and PercentInUse (112): rotate right by one bit, add the byte, mod 2^32
    checksum = 0
    for index in range(0, 11 * SECTOR):
        if index in {106, 107, 112}:
            continue
        checksum = ((((checksum << 31) | (checksum >> 1)) & 0xFFFFFFFF)
                    + area[base + index]) & 0xFFFFFFFF
    return checksum


def exfat_stored_checksum(area, base):
    return struct.unpack_from('<I', area, base + 11 * SECTOR)[0]


def setExfat(area, base, args):
    print(f'Updating EXFAT VBR at {base:x}')
    setboot(area, base, EXFAT, args)

    print('Calculating VBR checksum')
    checksum_chunk = struct.pack('<I', exfat_checksum(area, base)) * 128
    area[base + 11 * SECTOR:base + 12 * SECTOR] = checksum_chunk


def main():
    parser = argparse.ArgumentParser(
                formatter_class = RawDescriptionHelpFormatter,
                description=description
            )
    parser.add_argument("partition",
            help="partition: /dev path (Linux, macOS), drive letter like E: "
                 "(Windows, as Administrator), or a disk image file")
    parser.add_argument("--eos-develop", action="store_true",
            help="Set EOS_DEVELOP flag")
    parser.add_argument("--bootdisk", action="store_true",
            help="Set BOOTDISK flag")
    parser.add_argument("--script", action="store_true",
            help="Set SCRIPT flag")
    parser.add_argument("--remove-unused-flags", action="store_true",
            help="Use to clear flags that are not selected")
    parser.add_argument("--backup", metavar="FILE",
            help="Save the boot sectors to FILE before writing")
    parser.add_argument("--dry-run", action="store_true",
            help="Show what would change, write nothing")

    args = parser.parse_args()

    if not any([args.eos_develop, args.bootdisk,
            args.script, args.remove_unused_flags]):
        print('No flags selected. Defaulting to "set all flags, remove none"')
        args.eos_develop = True
        args.bootdisk = True
        args.script = True

    device = open_device(args.partition)
    try:
        original = device.read(0, BOOT_AREA)
        area = bytearray(original)

        if area[54:62] == b'FAT16   ':
            print('Identified FAT16 partition')
            setboot(area, 0, FAT16, args)
        elif area[82:90] == b'FAT32   ':
            print('Identified FAT32 partition')
            setboot(area, 0, FAT32, args)
        elif area[3:11] == b'EXFAT   ':
            print('Identified EXFAT partition')
            for base in (0, EXFAT_REGION):  # main, then backup boot region
                if exfat_checksum(area, base) != exfat_stored_checksum(area, base):
                    raise SystemExit(f'EXFAT boot region at {base:x} has a bad checksum '
                                     'already; refusing to write. Format the card in '
                                     'camera first.')
                setExfat(area, base, args)
        else:
            raise SystemExit(f"""\
Device {args.partition} is not a valid filesystem in the supported range.
Supported filesystems are FAT16, FAT32, EXFAT.
Format your card in camera before using this script.""")

        if area == original:
            print('Flags already as requested, nothing to write')
            return
        if args.dry_run:
            print('Dry run: nothing written')
            return
        if args.backup:
            with open(args.backup, 'wb') as f:
                f.write(original)
            print(f'Saved the original boot sectors to {args.backup}')

        # write only the sectors that changed
        for s in range(0, BOOT_AREA, SECTOR):
            if area[s:s+SECTOR] != original[s:s+SECTOR]:
                device.write(s, area[s:s+SECTOR])
        if device.read(0, BOOT_AREA) != area:
            raise SystemExit('VERIFY FAILED: the boot sectors read back differ')
        print('Written and verified')
    finally:
        device.close()

if __name__ == '__main__':
    main()
