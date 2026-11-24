24.00p
======

R6 1.9.0 only. Brings back Canon's own "24.00p" row in the Movie rec.
quality menu, which the R6 firmware hides. With the entry ON the row shows
Disable / Enable, SET toggles it, and Canon stores the choice like on its
other bodies (it survives a restart; "Clear all camera settings" resets it).

Work in progress: for now the movie frame rate does not change, 4K 23.98p
still records 23.98p. Recording true 24.00p in 4K is the next step. A
stored "Enable" is harmless without ML: the camera records 23.98p.

The row patches are applied in LiveView (not in playback or while
recording). Not together with the opengate module.

:Author: simoneluconi
:License: GPL
:Summary: Canon's 24.00p menu row on the R6
