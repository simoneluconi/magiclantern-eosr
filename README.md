Magic Lantern for the Canon EOS R6 (firmware 1.9.0) - unofficial fork
=====================================================================

> **Unofficial, experimental, use at your own risk.**
>
> - This is **not** an official Magic Lantern release. It is not affiliated with, reviewed
>   or endorsed by the Magic Lantern team. Please don't report problems with it to them.
> - Most of the R6-specific code was **written with the help of AI (LLMs)**. It started
>   from andrewpoison's LLM-assisted R6 port and was then cleaned up and extended with
>   Claude (Anthropic). Every such commit says so in its message. Magic Lantern does not
>   accept AI-generated code, so none of this is meant for, or will be submitted to,
>   the official project.
> - It has been tested on **one** camera. It comes with **no warranty** (see the GPL in
>   [`COPYING`](COPYING)). Keep a way to pull the battery quickly, and don't use it for
>   work that matters.

Magic Lantern (ML) is an independent program that runs alongside Canon's own firmware.
It is loaded from the memory card each time the camera starts; Canon's firmware itself is
not modified. This fork adds support for the original **EOS R6 (Mark I)** on firmware
**1.9.0**, plus a few features the R6 does not have by itself.

## Supported camera

| Camera | Firmware | State |
|---|---|---|
| Canon EOS R6 (Mark I) | **1.9.0** | supported (experimental) |
| Canon EOS R6, any other firmware | - | **not supported**: ML checks the firmware signature and refuses to start |
| Canon EOS R6 Mark II, other models | - | **not supported** by this fork |

The rest of the code base still builds for the other cameras Magic Lantern supports, but
this fork is only maintained and tested for the R6.

## What works

| Feature | Where | State |
|---|---|---|
| Loading ML from the card (slot 1 or 2, FAT32 or exFAT), ML menu, saving settings | Trash button opens the menu | works |
| **Clip length up to 59:59** instead of 29:59 (Canon's movie writer and player are patched too) | Movie > MOV/MP4 time limit | works, a 60 min clip records and plays back on the camera |
| **Heat timer** 2x / 3x / 4x: slows Canon's time-based 4K heat limit; the temperature sensors still protect the camera | Movie > Heat timer | works |
| Temperatures, heat timer and budget, in °C or °F | Debug > Internal Temp, LiveView top bar | works |
| Spotmeter, false color, waveform | Overlay | works in photo mode |
| Red display for night / astro, ML colour schemes incl. "Pure Red" | Display | works |
| Motion detect (pictures through Canon's own shutter path) | Shoot > Motion Detect | works |
| Lens info, depth of field, auto exposure values | Focus, Expo | works |
| File transfer over USB (PTP), on builds compiled with PTP | see [USB-PTP](docs/r6/USB-PTP.md) | works, advanced |

## What does not work (yet)

- **Overlays in movie mode**: spotmeter, false color and waveform only draw in photo LiveView.
- **ML menu while recording**: it closes again right away (Canon's menu can't be opened under it while recording).
- **Trimming long clips on the camera**: clips over 30 minutes play back on the camera, but Canon's movie editor keeps its own limits.
- **Modules** (dual ISO, MLV / RAW video, Lua scripts...) are not included.
- ML overlays are not shown in the viewfinder.
- Zebras, focus peaking, histogram, intervalometer and bulb timer are **not enabled on purpose**: the R6 has its own.

## Documentation

1. [Installing and starting ML](docs/r6/INSTALL.md): boot flag, preparing the card, first start
2. [Using ML](docs/r6/USAGE.md): controls, settings, every feature listed above, updating
3. [Troubleshooting](docs/r6/TROUBLESHOOTING.md): the camera hangs, freezes or shows errors
4. [Overheat protection and the 29:59 limit](docs/r6/OVERHEAT.md): how they work and what the ML options change
5. [USB / PTP file transfer](docs/r6/USB-PTP.md): advanced

## Installing, in short

Read [INSTALL](docs/r6/INSTALL.md) first; this is only an overview.

1. Check that the camera is on firmware **1.9.0** (Canon menu > Setup > Firmware).
2. **Turn on the camera's boot flag once**, with the Canon Basic script in
   [`tools/r6/bootflag-enable/`](tools/r6/bootflag-enable/). It runs from an SDHC card of 32 GB or less, in slot 2.
   It checks the firmware signature first. [`tools/r6/bootflag-disable/`](tools/r6/bootflag-disable/) turns the flag off again.
3. **Prepare an ML card**: format it in the camera, unzip `magiclantern.zip` to the card root, and set the
   `EOS_DEVELOP` and `BOOTDISK` flags with [`tools/card-flags/edit_card_flags.py`](tools/card-flags/edit_card_flags.py)
   (Linux) or the scripts in [`tools/r6/`](tools/r6/) (Windows).
4. Insert the card and power on. The **Trash** button opens the ML menu.

A card without the ML flags still boots the camera normally. Formatting the ML card in the camera
removes ML and its flags.

## Building from source

Builds are done on Linux (WSL works). You need:

- the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) for
  `arm-none-eabi` (12.3 was used), `make`, `python3` and `zip`;
- a checkout with Unix line endings. On Windows, clone inside WSL, or set `core.autocrlf=false`
  before cloning.

```sh
cd platform/R6.190
make ARM_BINPATH=/path/to/arm-gnu-toolchain/bin             # build/autoexec.bin, build/magiclantern.zip
make ARM_BINPATH=... CONFIG_PTP=y CONFIG_PTP_CHDK=y         # same, with USB file transfer
```

`magiclantern.zip` contains `autoexec.bin` and the `ML/` folder. Always copy `autoexec.bin` and
`ML/modules/R6_190.sym` from the same build.

The R6 work is on the branch `r6.190-clean`. Its R6-specific code is mostly in `platform/R6.190/`,
plus generic changes in `src/` that other cameras can opt into (for example
`CONFIG_MMU_RUNTIME_PAGES`, `MVR_WRITER_PATCHES`, `CONFIG_REC_INDICATOR_IN_TOP_BAR`).
Commit messages explain the reverse-engineering behind each change, with firmware addresses.

## No Canon code

This repository contains **no Canon firmware, ROM dumps or decompiled Canon code**: only
addresses and descriptions of what the firmware does, as in every Magic Lantern port. Please
don't attach ROM dumps to issues or merge requests.

## Reporting problems

Open an issue in **this** repository. Say which build you used and what you did, step by step:
mode, menu item, card, and whether USB was connected. If the camera hung, say how it recovered
(battery pull, card removed).

## Credits

- The **Magic Lantern** developers and contributors, for Magic Lantern itself and the DIGIC 6/7/8/X work
  this builds on: [magiclantern.fm](https://www.magiclantern.fm/).
- **andrewpoison**, for the first R6 1.9.0 port, shared in the R5/R6 thread on the Magic Lantern forum.
- **lclevy**'s [Canon Basic examples](https://github.com/lclevy/cbasic_examples), the base of the boot flag scripts.
- The **CHDK** project, whose PTP protocol and `ptpcam` are used for USB transfer.

## License

GPL v2, like Magic Lantern: see [`COPYING`](COPYING). Canon, EOS and related names are trademarks
of Canon Inc.; this project is not affiliated with Canon.

---

### About upstream Magic Lantern

Magic Lantern is an open framework, licensed under GPL, for developing extensions to Canon's
official firmware. It is developed by photo and video enthusiasts and adds features such as HDR
images and video, timelapse, motion detection, focus assist tools and manual audio controls.
See [magiclantern.fm](https://www.magiclantern.fm/). The ML team also maintains a patched QEMU for
emulating camera ROMs: [qemu-eos](https://github.com/reticulatedpines/qemu-eos).
