# Troubleshooting

Back to [README](../../README.md).

**First rule:** if the camera does not respond, **remove the battery**. Wait a few seconds and put it back. Then fix the card on a PC, or start with a different card.

## The camera hangs at power-on (black screen, does not respond)

Most likely cause: the boot flag is on, and the card has `BOOTDISK` but no readable `autoexec.bin`. Typical reasons:

- `autoexec.bin` was never copied, or it was deleted, or the copy was incomplete.
- The filesystem is damaged, for example because the card was pulled from a Windows PC without "Safely remove".

What to do:

1. Remove the battery.
2. On a PC, check the card. Run `chkdsk` or `fsck`, copy the full ML zip again, flush and eject safely.
3. Or, to just use the camera: put in a card **without** the ML flags. It boots normally.

## The card access LED blinks steadily and nothing else happens

ML found a firmware other than R6 1.9.0 and refused to start. On the cleaned build it signals this by blinking the card LED and does not continue. Remove the battery and boot with a card without ML, or update or downgrade the firmware to exactly 1.9.0.

## "Missing symbol file: ML/MODULES/R6_190.sym"

The symbols file is missing or unreadable. It must come from the same build as `autoexec.bin`. Copy the **whole** zip again (see [USAGE.md](USAGE.md#updating-ml)).

## ML menu does not open with Trash

- Check that `autoexec.bin` is on the card root and that the card has both `EOS_DEVELOP` and `BOOTDISK`.
- Check that the boot flag is on (`BOOTFLAG.TXT` from step 1 said `EnableBootDisk() called`).
- Make sure you are not inside a Canon dialog that captures the button.

## Canon Basic script does nothing (no BOOTFLAG.TXT)

Use **slot 2**, an **SDHC card of 32 GB or less, FAT32**, with the `SCRIPT` flag. Check that `script.req` contains `for DC_scriptdisk` and that `extend.m` is on the card root. Press ▶, then SET. See [INSTALL.md](INSTALL.md#step-1-turn-on-the-boot-flag-once).

## ERR 70, or the camera freezes while using ML

Remove the battery. Then report in the forum thread exactly what you did before it happened: menu item, mode, card, and any USB activity. If you were using USB/PTP, check that your `ptpcam` is the fixed version (see [USB-PTP.md](USB-PTP.md)).

## "Cannot playback movie" on a long clip

Canon's player rejects clips over 30 minutes unless ML patches it. ML does that only while **MOV/MP4 time limit** is above 29:59. If you set it back to 29:59, long clips recorded earlier show the message again. The camera carries on normally, and the file is fine: play it on a PC, or set the limit above 29:59 again.

## Err 70 at the end of a long recording

With the time limit above 29:59 this should not happen. Note the movie mode and when it stopped, and report it. With **29:59 (Canon)** selected, ML changes nothing in the movie writer.

## A new ML build does not start after an update

Remove the battery and put the card in a PC. On the R6 the memory ML may reserve at start-up is small: a build that reserves too much stops the camera before Canon's start-up completes (screen off, or lit but black). Report which build it was.

## Files missing or the card shows errors on the PC

This usually means unflushed writes on Windows. Always use "Safely remove". If needed, back up the photos, format the card in the camera, and set it up again ([INSTALL.md](INSTALL.md#step-2-prepare-the-ml-card)).

## Going back to stock

Turn the boot flag off ([INSTALL.md](INSTALL.md#turning-the-boot-flag-off-again)) and/or use cards without the flags. Canon's firmware itself was never changed.
