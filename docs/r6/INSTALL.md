# Installing and starting Magic Lantern on the EOS R6 (1.9.0)

Back to [README](../../README.md). Read [TROUBLESHOOTING.md](TROUBLESHOOTING.md) before you start.

There are three steps:

1. Turn on the camera's **boot flag**, once, with a small Canon Basic script.
2. Prepare an **ML card**: set two markings ("flags") in the card's boot sector and copy the ML files.
3. Insert the card and power on.

## What you need

- An R6 on firmware **1.9.0**.
- For step 1: an **SDHC card of 32 GB or less**. It can be an old, slow card.
- For step 2: any SD card. A 256 GB exFAT SDXC card was used in testing.
- A PC with a card reader, Python 3, and administrator (Windows) or root (Linux) rights.
- The ML build: the zip from the **[latest release](https://github.com/simoneluconi/magiclantern-eosr/releases/latest)**. It contains `autoexec.bin` and the `ML/` folder.

## About card flags

The camera recognises special cards by short text markings at fixed places in the card's boot sector:

| Flag | Purpose | FAT32 offset | exFAT offset |
|---|---|---|---|
| `SCRIPT` | run a Canon Basic script | 0x1F0 | not supported |
| `EOS_DEVELOP` | ML boot card | 0x47 | 0x82 |
| `BOOTDISK` | ML boot card | 0x5C | 0x7A |

On exFAT, the flags go into both the main boot region (sectors 0-11) and the backup boot region (sectors 12-23). The boot-region checksum in sectors 11 and 23 must then be recomputed (bytes 106, 107 and 112 are skipped). If you don't recompute it, the card can become unreadable.

**Tools:**

- **[`tools/card-flags/edit_card_flags.py`](../../tools/card-flags/edit_card_flags.py)** from ML. It works on FAT16, FAT32 and exFAT and takes a device path such as `/dev/sdX1`, so in practice it runs on **Linux**. Use the version in this repository. Older versions compute the exFAT checksum wrongly.
- **Windows helper scripts** in [`tools/r6/`](../../tools/r6/):
  - `write_script_flag.py` writes `SCRIPT` on a FAT32 card. It also writes `SCRIPT` into the sector just before the partition, where Canon looks on larger cards.
  - `write_boot_flags.py` writes `EOS_DEVELOP` and `BOOTDISK` on FAT32.
  - `write_exfat_boot_flags.py` writes both flags on exFAT. It recomputes the checksum, and it first checks that the existing checksum is correct.

  On Windows, raw sectors can only be written as Administrator, with the volume locked and dismounted. The exFAT script does that itself. All three scripts save a backup of the sectors they change and read them back to verify. The boot-flag scripts refuse to run if `autoexec.bin` is not on the card yet.

  On Windows, copy the ML files to the card **after** writing the exFAT flags: the flag script dismounts the volume, and files copied just before were lost once in testing. Then check them against the zip.
- **Canon Basic scripts** for step 1 in [`tools/r6/bootflag-enable/`](../../tools/r6/bootflag-enable/) and [`tools/r6/bootflag-disable/`](../../tools/r6/bootflag-disable/).

Double-check the drive letter or device name every time. Writing to the wrong disk can destroy its data.

---

## Step 1: turn on the boot flag (once)

Canon Basic scripts ran **only from card slot 2**, and only from an **SDHC card (32 GB or less) formatted FAT32**. They did not run from slot 1 or from SDXC/exFAT cards.

1. Format the SDHC card **in the camera**. The camera formats cards of 32 GB or less as FAT32.
2. On the PC, set the `SCRIPT` flag. With ML's tool on Linux, after unmounting the card: `sudo python3 edit_card_flags.py --script /dev/sdX1`
3. Copy two files to the card root:
   - `script.req`, a text file containing exactly `for DC_scriptdisk`
   - `extend.m`, the enable script. It computes the firmware signature, calls `EnableBootDisk()` only if the signature matches R6 1.9.0, and writes the result to `BOOTFLAG.TXT` on the card.
4. Eject the card safely (see the Windows warning in step 2).
5. Put the card in **slot 2**. Power on, press **Playback (▶)**, then press **SET**. Wait a few seconds, then power off.
6. Read `BOOTFLAG.TXT` on the PC. You should see:
   ```
   fw signature: 0xe95feb0e (expected 0xe95feb0e)
   EnableBootDisk() called
   ```
   - If it says `signature mismatch, boot flag NOT changed`, your firmware is not 1.9.0. Stop here.
   - If there is no `BOOTFLAG.TXT`, the script did not run. Check the slot, the card type, the `SCRIPT` flag and both files.

> **Important:** some guides also set `EOS_DEVELOP` and `BOOTDISK` on the script card. In testing, the script ran with only `SCRIPT` set. Once the boot flag is on, any card with `BOOTDISK` but no `autoexec.bin` **makes the camera hang at power-on**. Don't power on again with such a card until ML is on it.

### Turning the boot flag off again

Do the same procedure with an `extend.m` that calls only `DisableBootDisk()`, using the same `script.req`. This script writes no log file. After that, the camera ignores ML cards.

---

## Step 2: prepare the ML card

Any card works for ML itself, in **slot 1 or 2**, FAT32 or exFAT.

1. Format the card **in the camera**. Cards larger than 32 GB become exFAT.
2. Unzip the release zip to the **card root**. You should end up with `autoexec.bin` and the `ML/` folder next to `DCIM`.
3. **On Windows, flush and eject before anything else.** Use "Safely remove hardware" and wait until Windows says the card can be removed. In testing, pulling a card without this lost data that hadn't been written yet. That corrupted the exFAT filesystem, deleted files, and the damaged card then hung the camera at boot.
4. Set `EOS_DEVELOP` and `BOOTDISK`:
   - Linux: unmount the card, run `sudo python3 edit_card_flags.py --eos-develop --bootdisk /dev/sdX1`, then `sync`.
   - Windows: run `write_boot_flags.py <letter> <backup folder>` (FAT32) or `write_exfat_boot_flags.py <letter> <backup folder>` (exFAT) as Administrator.
     On exFAT, check afterwards that the `ML/` folder is still complete, and copy it again if not (see the note under *Tools* above).
5. Eject safely again.
6. Optional check: compare the files on the card with the zip, for example with SHA-256 checksums. A read-only `chkdsk` should report no problems; errors that show up only in "System Volume Information" were seen and appear harmless.

---

## Step 3: first start

1. With the camera **off**, insert the ML card.
2. Power on. ML loads automatically, and the camera should look and behave normally.
3. Press the **Trash (erase)** button. The ML menu should open.
4. Power off normally. Settings are saved to `ML/SETTINGS/magic.cfg`.

Next: [USAGE.md](USAGE.md). If anything else happens, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
