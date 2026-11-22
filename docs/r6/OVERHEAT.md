# How the R6 overheat protection works

Back to [README](../../README.md).

This is background reading. It is based on a **study of the R6 1.9.0 firmware** and on tests on one camera. Parts of it are confirmed in the code or on the camera. Other parts, mainly what a value means on screen, are interpretations. The difference is marked below. What the ML options change is in [the last sections](#what-the-ml-options-change); the temperature sensors are never touched.

## Two separate mechanisms

The R6 has two independent overheat mechanisms. Either one can stop recording.

### A. Real temperature sensors

- About once a second, the camera reads several temperature sensors: the main board, the rear screen, DRAM, and two others whose exact location is unclear. Shutter and stabiliser sensors are read too, but they don't count towards this protection. *(confirmed in code; the sensor names are interpretation)*
- Each sensor has several levels. The likely effects are a **warning icon**, then **stopping recording / LiveView**, then **shutdown**. A separate ISO-dependent level on the main sensor probably drives the white "image quality" thermometer. *(levels confirmed; their on-screen effects are partly interpretation)*
- The thresholds come from data stored in each camera, not from fixed numbers in the firmware, so they may differ slightly from body to body. A change of state must hold for about 3 seconds before it takes effect.
- This service has **no** image-sensor (CMOS) temperature reading.

### B. A time-based "heat budget" (the movie timer)

This is where Canon's published limits come from: **about 30 minutes in 4K 50/60p and 40 minutes in 4K up to 30p**.

- The camera keeps a budget that starts full. Every second it drains at a rate set by the movie mode and by whether you are **recording** or in **standby** (LiveView on, not recording).
- When the budget runs out in a **4K** mode, recording stops and the block stays until the camera is restarted. **Full HD never stops on the timer.** It is still protected by the sensors (A).
- Only **one sensor** corrects the timer: the warmer it is, the less budget the camera gives back, and some budget returns when it cools clearly below its peak. *(confirmed in code)* Below about 46 °C on that sensor the budget can't run out completely, which is why a camera cooled by a fan never stopped on the timer.
- While **any menu is open**, the camera uses a separate, very slow row of the table (33000 s), so the budget hardly drains in the menus. *(confirmed in code)*
- Measured on camera in 4K 25p: the budget (0..65535) dropped about 1640 units per minute, as the 2400 s of the table predict.
- The camera publishes the remaining time and raises a flag when less than 3 minutes remain, probably for the on-screen warning.

**Values found in the firmware.** These are the seconds it takes to empty a full budget, before the temperature correction. Which mode is which is partly inferred.

| Mode | Standby | Recording | Can stop on the timer |
|---|---|---|---|
| 4K 50/60p | 1800 | 1798 (about 30 min) | yes, recording and standby |
| 4K up to 30p | 3600 | 2400 (40 min) | while recording |
| 4K, probably crop, 60p | 2700 | 2100 | yes, recording and standby |
| 4K, probably crop, up to 30p | 3900 | 3300 | while recording |
| FHD 100/120p | 4800 | 3000 | never |
| FHD 50/60p | 3900 | 2700 | never |
| FHD up to 30p | 7500 | 7500 | never |
| Photo mode | 4500 | 4500 | never |

## Why changing the clock "resets" the limit

When the camera powers off, it saves the remaining budget together with a timestamp from its own clock. At power-on, if **more than 2 hours** have passed by that clock, it assumes the camera has cooled and refills the budget. Otherwise it keeps the saved value. *(confirmed in code)* The sensor protection (A) is unaffected by this.

## What "Overheat control" changes

Most likely only the **standby** rates. With the setting on, standby drains about 1.5 to 2.5 times more slowly; for example, 4K 50/60p standby goes from 1800 s to 4500 s. The recording rates stay the same. *(strong interpretation, not confirmed on camera)*

## The 29:59 clip limit is something else

The camera also stops each clip at **29:59**, or **7:29** with High Frame Rate (100/120p) on. This is a fixed per-clip limit in the user interface code. It compares the elapsed recording time with a constant in the firmware, once per second. It has nothing to do with temperature or the heat budget. It is skipped in time-lapse movies, and probably in a network/streaming mode. *(confirmed in code; the streaming case is interpretation)*

Behind it the MP4 writer has its own limits: fixed tables of 108000 entries per clip (one per video frame) and a 4 MB buffer for the file index. When a table is full the camera stops with **Err 70**; this was confirmed on camera with a test build. The audio (AAC) chunk table is even smaller: it would silently overflow after about 32.7 minutes.

## What the ML options change

- **Movie > MOV/MP4 time limit** changes the 29:59 constant, and above 29:59 also doubles the writer's tables (and the AAC table) and its index buffer (8 MB). The maximum is 59:59; HFR stays at 7:29. A 60-minute clip was recorded and checked on a PC. Canon's own player refuses clips with an index over about 3 MB (roughly 40-45 minutes).
- **Movie > Heat timer** multiplies the seconds of every row of the heat table (B) by 2, 3 or 4, so the budget drains more slowly. The stop flags, the temperature correction and the sensors (A) are unchanged. On camera, 2x halved the drain in 4K 25p.
- Both work by replacing a few words of the firmware **in RAM** while ML runs (the MMU remaps those ROM pages). Nothing is written to the camera's firmware. With "Canon" selected, or without ML, the camera behaves as stock.

## Safety note

The sensor-based protection (A) protects the camera's electronics and prevents surface temperatures that can burn skin. ML leaves it completely untouched. The timer (B) probably also exists because some parts, such as the image sensor and stabiliser, are not monitored by this temperature service. A longer timer means the camera relies on the sensors alone for longer, so watch the temperatures (Debug > Internal Temp, or the LiveView top bar) and don't leave a hot camera recording unattended.

## Technical notes

For developers. These are ROM0 addresses in firmware 1.9.0.

- Temperature polling task `TempSvc` at E099B468, every 1000 ms. Status published as property 0x8004000F.
- Decision function E04E9418. Sensor thresholds E04E9122. Heat budget E04E902A. Per-mode table E0D9CEA4 (24 entries).
- Budget persistence: PROP_MOVIE_REMAIN_TIME 0x02050043 and PROP_MOVIE_REC_DATE 0x02050045 (refilled after more than 7200 s). "Overheat control" is probably 0x800000D8.
- Clip limit: E074ECC6 returns 1799000 ms, or 449000 ms with HFR (pool words E074ED68 / E074F168). The check is in the movie record second-count handler (DlgLiveView).
- MP4 writer: mp4wInitializeMmlibParam E05F07CA (capacity E05F0BE8, table sizes E05F0BE4 / 0BEC / 0BF0, AAC stsc size at E05F08D2), moov buffers E05F02B0, header compares E03A38D6 / E05EDAF0. Details in platform/R6.190/consts.h (MVR_WRITER_PATCHES).
- Temperatures (°C + 128): 0x80030035 DRAM, 0x800300C6 / 0x8003008D MIMI L / R, 0x8003009E, 0x80030081 TFT, 0x80030062 SH. Remaining heat time 0x800000D9.
