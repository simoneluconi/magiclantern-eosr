24.00p
======

R6 1.9.0 only. Records true 24.00p in 4K UHD through Canon's own
"24.00p" setting, which the R6 firmware hides and ignores. With the entry
ON, Canon's Movie rec. quality menu shows a "24.00p" row (Disable /
Enable, SET toggles it; Canon stores it like on its other bodies).

With Enable, 4K UHD 23.98p (NTSC, full or crop) records 24.00p: the
sensor runs on Canon's 24.00p timing, the file has a 24000/1000 time
base. FHD and every other rate are unchanged: FHD stays 23.98p (the R6
has no FHD 24.00p mode). Canon's screens still say 23.98P.

Needs an ML core with PROP_MOVIE_CONTROL_PARAM (R6.190 from a331a5c01).

Usage
-----

Set Canon's movie size to 4K UHD 23.98p, switch the entry ON in
LiveView, then set Canon's 24.00p row to Enable. If it was already
Enable (it survives a restart), set it to Disable and back to Enable
once, or change the frame rate and back: Canon applies its setting only
when it rebuilds the movie parameters.

The menu shows Canon's stored value and "24.00" when 24.00p is live.
Changes wait for LiveView: not in playback, not while recording.
Not together with the opengate module: turn open gate OFF first.

Risk
----

24.00p locks the sensor to a 60.00 Hz sync that a stock R6 never uses.
The first time may end in Err 70 or a frozen LiveView: pull the
battery. Nothing is stored but Canon's own setting, which a camera
without ML ignores: the next boot is stock 23.98p.

Check a first clip on a computer: 24000/1000 time base and 24 frames
per second of wall time, sound in sync.

Turning it off
--------------

Switch the entry OFF in LiveView. If 24.00p is live, Canon keeps it
until its next rebuild: set the 24.00p row to Disable, or change the
frame rate once and back, to get 23.98p again. A restart also does.

:Author: simoneluconi
:License: GPL
:Summary: True 24.00p in 4K UHD on the R6 (Canon's 24.00p row)
