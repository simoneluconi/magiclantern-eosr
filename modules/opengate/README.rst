Open gate 4K
============

R6 1.9.0 only. Records the whole sensor height in 4K: the sensor reads all
its rows and the picture is developed and encoded at 4096x2736 (3:2), as
HEVC 4:2:2 10-bit (Canon Log on) or H.264 4:2:0 8-bit, level 6.1, at 25p,
29.97p or 23.98p (PAL and NTSC).

Needs an ML core with PROP_MOVIE_CONTROL_PARAM (R6.190 from a331a5c01).

Usage
-----

Set Canon's movie size to 4K 25p, 29.97p or 23.98p with movie crop off,
switch the entry ON and go to photo mode once: the changes are applied
there. Back in movie mode the camera records open gate; the LCD shows the
whole 3:2 frame. The entry is always OFF after a restart.

Changing between 25p, 29.97p and 23.98p keeps the open gate. Changing to
FHD, 50p or more, movie crop, Canon Log or another recording setting
removes the changes at once, before Canon restarts the movie LiveView; the
next photo-mode visit applies them again. HDMI or turning the entry off
removes them too. Nothing changes while recording.

Limits
------

The sensor runs like 4K 50p (more heat), the rolling shutter is that of a
full-height read, no 50/60p, HDMI output refused. Canon's player and
in-camera movie edit do not accept the clips: play them on a computer.
Not together with the fps24 module (24.00p).

Debug log: ML/LOGS/OPENGATE.LOG (events, Canon's movie settings, DryOS
asserts).

:Author: simoneluconi
:License: GPL
:Summary: Whole-sensor 4K recording (4096x2736) on the R6
