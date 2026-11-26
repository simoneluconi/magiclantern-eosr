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

- Choices: shorter limits, **29:59 (Canon)**, **45 min**, **59:59**, **1:29:59** and **1:59:59**.
- Up to 30p a clip can last up to 1:59:59. At 50/60p it stops at 59:59 at most.
- The shooting screen shows the remaining time in minutes, for example 119:59, as Canon's recording counter does.
- High Frame Rate (100/120p): with a limit above 29:59 it records up to 29:59; with 29:59 (Canon) or less it keeps Canon's 7:29. A 10-minute 100p clip was recorded and played on a PC; playback on the camera past 7:29 is not tested yet.
- Above 29:59 ML also patches Canon's movie writer (its per-clip tables, the audio tables and the 8 MB index buffer). Without that the camera would stop with Err 70 after the limit, or leave an unplayable file.
- It also lets Canon's player open clips over 30 minutes; without that the camera says "Cannot playback movie".
- Tested: a 60-minute FHD 50p clip on exFAT stopped at 59:59, played fine on a PC and plays back on the camera. 2-hour clips are not tested on camera yet.
- Don't trim clips over 30 minutes on the camera: Canon's movie editor keeps its own limits.
- The limit can't be changed while recording. It takes effect from the next clip.
- Heat still applies: in 4K the heat timer (below) or the temperature sensors may stop the clip earlier.

### Movie > Heat timer

- **Canon / 2x / 3x / 4x.** Slows down Canon's time-based heat budget, the timer behind the 30 / 40 minute 4K limits. On camera 2x halved the drain rate (4K 25p).
- The real temperature sensors are not touched: the camera still warns, stops and shuts down on temperature. See [OVERHEAT.md](OVERHEAT.md).
- Can't be changed while recording.

### Movie > Open gate 4K and 24.00p

- From the modules **opengate** (the whole sensor height: 4096x2736 in 3:2, or 3840x2880 in 4:3 for anamorphic lenses; 25p, 29.97p, 23.98p) and **fps24** (true 24.00p in 4K UHD). Load them in the **Modules** tab, then turn the entry on in the Movie menu. One at a time.
- How to set the camera for each one is on its info screen: select the module in the **Modules** tab and press **Q**.

### Movie > ALL-I

- From the module **alli**. Turn it ON, then choose **For editing (ALL-I)** in Canon's Movie rec. quality menu: 4K UHD and FHD, 23.98p, 25p and 29.97p.
- Also in Canon Log and HDR PQ (10-bit H.265, 4:2:2).
- 4K ALL-I is 470 Mbps, about 60 MB/s: use a V90 card. FHD is 90 Mbps (135 Mbps in 10-bit).
- Turn it OFF in LiveView with the ML menu closed: an ALL-I setting goes back to IPB of the same size and rate. The same happens at start-up if the entry is OFF.
- Not together with open gate.
- Not tested on camera yet.

### Overlay > Cropmarks

- Framing lines over the LiveView, from the bitmaps in `ML/CROPMKS` on the card. Press **Q** on the entry to choose one; you can add your own BMPs.
- They need Global Draw. In movie mode they are drawn over the 16:9 image.

### Temperatures and the LiveView top bar

- **Debug > Internal Temp** lists the temperature sensors, Canon's overheat status, the movie heat timer (minutes left) and the heat budget.
- With **Overlay > Global Draw** on and Canon's **clean INFO screen**, ML's top bar shows the temperature, and in movie mode the heat timer and budget. These are hidden in Full HD, where Canon has no heat timer. In movie mode the right end of the top bar is always left free for Canon's REC indicator. If the bar is full, the clock is hidden first.
- The battery icon at the right of the bottom bar shows the exact charge in %, as in Canon's Battery info menu, updated every 10 seconds. Its colour follows Canon's levels: full and 3 bars in white, half and a quarter in yellow, low and empty in red. **Debug > Battery level** also estimates the time left; the estimate is reliable once the charge has dropped by a couple of percent.
- After a mode change (photo/movie, mode dial), ML waits 3 seconds while Canon shows its own info.

### Overlays

**Spotmeter**, **False color**, **Waveform** and **Vectorscope** in the Overlay menu. They need Global Draw and Canon's clean INFO screen. They work in photo and movie mode, also while recording. In movie mode they cover the 16:9 image, and ML's bars sit in the black bands above and below it. For about 2 seconds after a photo/movie change they are not drawn. They don't draw under ML's own graphics (bars, the spotmeter box). In the viewfinder, ML draws on Canon's first INFO screen there (the one with only the exposure below the image); Canon's own info can still leave gaps in ML's bars.

### Modules: Lua scripts and the file manager

1. Open the **Modules** tab, select **lua** and/or **file_man** and press **SET** to mark them for loading.
2. Restart the camera: modules are loaded at start-up. If something goes wrong while loading, hold **SET** while powering on to start without modules.
3. **file_man** adds **Debug > File Manager**, a browser for the card. It can also delete files.
4. **lua** adds the **Scripts** tab, with one entry per `.lua` file in `ML/scripts` on the card. Select a script, press **Q** and choose **Run Script**. **Autorun** starts it at every power-on.
   - The example scripts (calc, pong, sokoban, menutest, hello...) were tested.
   - Scripts that change camera settings or take pictures fail, because this build does not allow property writes. Avoid **api_test**: it tests those calls too.
   - Script tasks get at most 32 KiB of stack on the R6.

### Display > Anamorphic

- Shows the image desqueezed for anamorphic lenses: **5:4 (1.25)** to **2:1**. It works on the screen and in the viewfinder, in photo and movie mode, at full resolution and frame rate, also while recording.
- Canon's display does the stretch itself, and ML's overlays follow the desqueezed image.
- The change shows when the ML menu closes. A change made while recording applies after the clip.
- With open gate 4:3 the 4:3 frame is shown desqueezed too.
- Only the preview changes: pictures, movies and playback stay as recorded. HDMI is not desqueezed.

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
- It contains ML's graphics (menu, bars, overlays) and, in LiveView, the LiveView image at ML's lower resolution (in movie mode the 16:9 image, with black bands).
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

Format the ML card from Canon's menu as usual. The format screen says **Format card, keep ML [INFO]**: ML copies its files to memory, Canon formats the card, then ML writes its files and the card flags back and restarts the camera. Your `ML/SETTINGS` are kept.

- Press **INFO** on the format screen to switch to **Format card, remove ML**.
- Wait for the format screen to answer the buttons: they are held back for a moment while ML copies its files.
- ML keeps up to 2 MB of files: fonts, modules and settings first, then the rest of `ML/`. The format screen shows the size, and how many files were not kept.
- Formatting the other card leaves ML alone.

If the format screen says **ML not kept (no memory)**, ML is removed with the format; set the card up again on a PC ([INSTALL.md](INSTALL.md#step-2-prepare-the-ml-card)).

## Two cards

Testing used one ML card at a time. What happens with two flagged cards has not been tested.
