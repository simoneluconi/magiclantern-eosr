# Using Magic Lantern on the EOS R6

Back to [README](../../README.md).

## Opening and using the ML menu

| Action | Button |
|---|---|
| Open / close the ML menu | **Trash (erase)** |
| Change the selected item ("SET") | **SET**, or **joystick centre** |
| Move around | joystick, rear dial |
| Change tab | main (top) dial |
| Open a submenu | **Q** (RATE also acts as Q inside the ML menu) |

Trash and the joystick centre were confirmed on the camera. The other rows follow the usual ML conventions and the R6 button map in the code. If something behaves differently, please report it.

The ML menu draws on top of Canon's own screens. It also opens while a movie is recording. While the menu is open, the dials and the lens control ring only move the ML menu; they don't change exposure. When the menu closes, Canon redraws its screen, including the REC indicator.

In SET's pick lists and edit mode the other menu items are hidden until you press SET again. Change the value with the rear dial or the joystick.

## Features

Only features the R6 does not have already are enabled. Every new option starts off, or on "Canon".

### Movie > MOV/MP4 time limit

- Choices: shorter limits, **29:59 (Canon)**, **45 min** and **59:59**. High Frame Rate (100/120p) stays at Canon's 7:29.
- Above 29:59 ML also patches Canon's movie writer (its per-clip tables and the 8 MB index buffer). Without that the camera would stop with Err 70 after the limit, or leave an unplayable file.
- It also lets Canon's player open clips over 30 minutes; without that the camera says "Cannot playback movie".
- Tested: a 60-minute FHD 50p clip on exFAT stopped at 59:59, played fine on a PC and plays back on the camera.
- Don't trim clips over 30 minutes on the camera: Canon's movie editor keeps its own limits.
- The limit can't be changed while recording. It takes effect from the next clip.
- Heat still applies: in 4K the heat timer (below) or the temperature sensors may stop the clip earlier.

### Movie > Heat timer

- **Canon / 2x / 3x / 4x.** Slows down Canon's time-based heat budget, the timer behind the 30 / 40 minute 4K limits. On camera 2x halved the drain rate (4K 25p).
- The real temperature sensors are not touched: the camera still warns, stops and shuts down on temperature. See [OVERHEAT.md](OVERHEAT.md).
- Can't be changed while recording.

### Temperatures and the LiveView top bar

- **Debug > Internal Temp** lists the temperature sensors, Canon's overheat status, the movie heat timer (minutes left) and the heat budget.
- With **Overlay > Global Draw** on and Canon's **clean INFO screen**, ML's top bar shows the temperature, and in movie mode the heat timer and budget. These are hidden in Full HD, where Canon has no heat timer. In movie mode the right end of the top bar is always left free for Canon's REC indicator. If the bar is full, the clock is hidden first.
- The battery icon at the right of the bottom bar shows the exact charge in %, as in Canon's Battery info menu, updated every 10 seconds. Its colour follows Canon's levels: full and 3 bars in white, half and a quarter in yellow, low and empty in red. **Debug > Battery level** also estimates the time left; the estimate is reliable once the charge has dropped by a couple of percent.
- After a mode change (photo/movie, mode dial), ML waits 3 seconds while Canon shows its own info.

### Overlays (photo mode)

**Spotmeter**, **False color**, **Waveform** and **Vectorscope** in the Overlay menu. They need Global Draw and Canon's clean INFO screen. They work in photo LiveView, but not yet in movie mode.

### Modules: Lua scripts and the file manager

1. Open the **Modules** tab, select **lua** and/or **file_man** and press **SET** to mark them for loading.
2. Restart the camera: modules are loaded at start-up. If something goes wrong while loading, hold **SET** while powering on to start without modules.
3. **file_man** adds **Debug > File Manager**, a browser for the card. It can also delete files.
4. **lua** adds the **Scripts** tab, with one entry per `.lua` file in `ML/scripts` on the card. Select a script, press **Q** and choose **Run Script**. **Autorun** starts it at every power-on.
   - The example scripts (calc, pong, sokoban, menutest, hello...) were tested.
   - Scripts that change camera settings or take pictures fail, because this build does not allow property writes. Avoid **api_test**: it tests those calls too.
   - Script tasks get at most 32 KiB of stack on the R6.

### Display > Red display and colour schemes

- **Display > Red display** (OFF / 100 / 50 / 25 / 12 %) turns the whole screen and the viewfinder red, for night and astro use. It affects the display only, not the pictures. The setting is saved.
- **Display > Advanced settings > Color scheme** changes only ML's own graphics; "Pure Red" is the night version.

### Shoot > Motion Detect

- Takes a picture when the image changes ("Expo. change" or "Frame diff.").
- Works in photo LiveView. It uses Canon's own shutter path, so pictures and shutdown behave normally.
- Opening the ML menu stops detection.

### Audio > Beep, test tones

- ML's beeps (screenshot, motion detect, Lua scripts...) use ML's own tone, so they sound different from Canon's AF and self-timer beeps.
- **Tone Frequency**, **Tone Waveform** and **Speaker Volume** change the sound. **Play test tone** plays it for 5 seconds.
- **Enable Beeps** turns ML's beeps off; the LED blinks instead. ML never beeps while a movie is recording.

### Debug > Screenshot

- Choose it, close the menu and wait for the countdown and the beep. A 720x480 `VRAMn.BMP` is saved in the card root.
- It contains ML's graphics (menu, bars, overlays) and, in photo LiveView, the LiveView image at ML's lower resolution.
- Canon's own graphics are not in the file.

## Settings

- ML settings are stored in `ML/SETTINGS/magic.cfg` on the card. This works on FAT32 and exFAT.
- With **Prefs > Config options > Config AutoSave** on (the default), settings are saved when the camera shuts down.
- **Prefs > Config options > Save config now** saves immediately.
- To reset everything, delete `ML/SETTINGS/` on a PC.

## Updating ML

1. Copy the new `autoexec.bin` **and** the new `ML/` folder from the same zip over the old ones. You can keep `ML/SETTINGS/`.
   - Always update `autoexec.bin` and `ML/modules/R6_190.sym` together. A mismatch gives a symbol-file warning (see [TROUBLESHOOTING.md](TROUBLESHOOTING.md)).
   - Don't delete `autoexec.bin` first and copy the new one later. A flagged card without it hangs the camera.
2. Flush and safely eject (Windows), or `sync` and unmount (Linux).
3. The card flags stay in place. You don't need to set them again.

On builds with PTP enabled, you can also update over USB (see [USB-PTP.md](USB-PTP.md)).

## Formatting an ML card in the camera

A camera format deletes ML **and** the card flags: Canon writes a fresh boot sector (seen on the R6). The camera then boots normally from that card, without ML. To get ML back, set the card up again on a PC ([INSTALL.md](INSTALL.md#step-2-prepare-the-ml-card)).

## Two cards

Testing used one ML card at a time. What happens with two flagged cards has not been tested.
