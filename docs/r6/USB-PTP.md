# Advanced: file transfer over USB (PTP)

Back to [README](../../README.md). **For experienced users only.** A mistake here can leave the card without a working `autoexec.bin`, and the camera then hangs at power-on (see [TROUBLESHOOTING.md](TROUBLESHOOTING.md)).

## What it is

Builds compiled with `CONFIG_PTP=y CONFIG_PTP_CHDK=y` answer CHDK-style PTP commands (operation code 0x9999) over USB. With them you can upload and download files on the ML card without taking it out. Over PTP, **the ML card is drive `A:`**. Canon's normal USB connection keeps working alongside.

## The right ptpcam

Use `tools/ptpcam` from the cleaned R6 branch. Build it on Linux; it needs the libusb 0.1 compatibility library, and its Makefile builds a 32-bit binary (`-m32`), so you may need the 32-bit libusb development package.

The R6 is a USB 3 (SuperSpeed) camera and sends 1024-byte packets. **Older ptpcam builds read 512-byte packets, and the camera freezes until you remove the battery.**

The tested setup was Linux; on Windows, WSL with usbipd-win was used to pass the camera through. Close any other program that grabs the camera (photo import tools, EOS Utility, gphoto2 sessions). You may need root, or a udev rule.

## Commands

    ptpcam --chdk=version
    ptpcam --chdk="download A:/ML/SETTINGS/magic.cfg magic.cfg"
    ptpcam --chdk="upload autoexec.bin A:/AUTOEXEC.BIN"

- `upload <file on PC> <path on camera>`
- `download <path on camera> <file on PC>`

## Updating ML over USB (careful)

1. Download the current `A:/AUTOEXEC.BIN` as a backup.
2. Upload the new `autoexec.bin` **and** `ML/modules/R6_190.sym` from the same build.
3. Download both again and compare them byte by byte with your files.
4. Restart the camera only when they match.

## Limits

- Files of a few hundred KB were tested (for example a 300 KB round trip, and `autoexec.bin`). Files larger than about 512 KB may fail with this build and are not recommended.
- USB connection drains the battery.
- If the camera freezes (ERR 70), remove the battery and report what you ran.
