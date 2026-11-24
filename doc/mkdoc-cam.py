#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Render the user guide in BMP format (with RLE compression)
# in order to use it as a help system in the camera menu.
# Outputs: a lot of files in cam/ subdirectory
# (page-%03d.bmh for each page, and menuidx.dat)
#
# Default: the full user guide (MANUAL.txt + MN-*.txt), into cam/.
#
# A camera can have its own short guide instead:
#   python3 mkdoc-cam.py --guide r6cam --out ../platform/R6.190/doc --ml-palette --no-antialias
# reads r6cam/index.txt (the list of sources) and r6cam/ug-template-cam.tex.
#
# Needs: python3 with docutils, numpy and PIL, lualatex (TeX Live),
# pdftoppm (poppler-utils), ruby; pandoc for the full guide only.

# Author: Alex Dumitrache <broscutamaker@gmail.com>
# License: GPL

import os, re, sys, glob, shutil, argparse
import urllib.parse

import numpy as np
from PIL import Image

from mkdoc_utils import system_or_exit
from mkdoc_utils import include
from mkdoc_utils import sed_sub_tex_spec_chars
from menuindex import write_menu_index

parser = argparse.ArgumentParser(description="Render the in-camera help pages")
parser.add_argument("--guide", metavar="DIR",
    help="build the guide from DIR/index.txt and DIR/ug-template-cam.tex, instead of the full user guide")
parser.add_argument("--out", metavar="DIR", default="cam",
    help="output directory (default: cam)")
parser.add_argument("--ml-palette", action="store_true",
    help="match colors to ML's own palette (indexed2rgbLUT in src/bmp.c, for FEATURE_VRAM_RGBA cameras) "
         "instead of Canon's (data/vram/Palette.jpg)")
parser.add_argument("--no-antialias", action="store_true",
    help="render the text without antialiasing (about half the size)")
parser.add_argument("--tex-only", action="store_true",
    help="stop after lualatex (the rest is slow)")
args = parser.parse_args()

rst2latex = os.getenv("RST2LATEX") or shutil.which("rst2latex.py") or "rst2latex"
template = "ug-template-cam.tex"
outdir = args.out

o = open("userguide.rst", "w")
if args.guide:
    template = os.path.join(args.guide, template)
    for l in open(os.path.join(args.guide, "index.txt")).readlines():
        l = l.strip()
        if l and not l.startswith("#"):
            include(o, os.path.join(args.guide, l))
else:
    o.write("""Magic Lantern v2.3
==================================

""")
    include(o, "MANUAL.txt", 1);
    include(o, "MN-AUDIO.txt");
    include(o, "MN-EXPO.txt");
    include(o, "MN-OVERLAY.txt");
    include(o, "MN-MOVIE.txt");
    include(o, "MN-SHOOT.txt");
    include(o, "MN-FOCUS.txt");
    include(o, "MN-DISPLAY.txt");
    include(o, "MN-PREFS.txt");
    include(o, "MN-DEBUG.txt");
    include(o, "EPILOGUE.txt");

o.close()

def sub(file, fr, to):
    txt = open(file).read()
    txt = re.sub(fr, to, txt);
    f = open(file,"w")
    f.write(txt)
    f.close()

def replace(file, fr, to):
    txt = open(file).read()
    txt = txt.replace(fr, to);
    f = open(file,"w")
    f.write(txt)
    f.close()

def fixwikilinks(file):
    txt = open(file).read()

    while 1:
        m = re.search(r"\[\[([^]|]+)([^]]*)\]\]", txt, re.MULTILINE)
        if not m: break
        origstr = "[[" + m.groups()[0] + m.groups()[1] + "]]"
        print(origstr)
        x = m.groups()[0]
        x = x.replace("_", " ")
        txt = txt.replace(origstr, "`%s <http://magiclantern.wikia.com/wiki/%s>`_" % (x, urllib.parse.quote(x)))

    #~ sub("INSTALL.rst", , ")

    f = open(file,"w")
    f.write(txt)
    f.close()


def labelhack(file): # bug in rst2latex? it forgets to place labels in tex source
    txt = ""
    for l in open(file).readlines():
        txt += l
        m = re.match(".. _(.*):", l)
        if m:
            label = m.groups()[0]
            txt += r""".. raw:: latex

    \subsubsection*{}\label{%s}
""" % label.lower().replace("/"," ").replace("   ", " ").replace("  ", " ").replace(" ", "-").replace(".", "-")
    f = open(file,"w")
    f.write(txt)
    f.close()

nonewlineitems = [
    'WBShift','Aperture','PictureStyle',"REC PicStyle",
    'Focus delay', 'Focus A', 'Rack Focus', "Focus StepD", "Focus Dist",
    'Hyperfocal', 'DOF Near', 'DOF Far',
    "LiveView Zoom", "Crop Factor Display",
    'Screenshot',
    'Config', "config",
    "Don't click",
    "Mirror",
    "Waveform",
    'Movie REC', 'Movie Restart',
    'Analog Gain', "DigitalGain", 'AGC', 'Zoom in PLAY',
    "Turn off", "Battery remaining",
    "MOV Exposure Lock", "Light Adjust", "Cropmarks (PLAY)",
    "Lock Shutter", "Force HDMI",
    "Free Memory", "EFIC temperature", "Shutter Count", "Battery remaining"
    "LV button", "Quick Erase", "Shutter Lock", "Shutter Button"]
def should_add_newline(l):
    return 1
    for it in nonewlineitems:
        if it in l:
            return 0
    return 1


def add_menu_items_to_contents(file):
    txt = ""
    for l in open(file).readlines():
        m = re.match(r"^\*\*(.*)\*\*\ *$", l)
        if m and should_add_newline(l):
            txt += r"""
.. raw:: latex

    \newpage

"""
        txt += l
        if m:
            item = m.groups()[0]
            txt += r"""
  .. raw:: latex

      \addcontentsline{toc}{subsubsection}{%s}
""" % item.replace("**","").replace("_", r"\_")
    f = open(file,"w")
    f.write(txt)
    f.close()

system_or_exit(r"sed -i -e s/.*{{.*}}.*//g userguide.rst")

if not args.guide:
    system_or_exit("pandoc -f rst -t latex -o credits.tex CREDITS.txt")

fixwikilinks("userguide.rst")
#~ labelhack("userguide.rst")
#~ add_menu_items_to_contents("userguide.rst")
system_or_exit(r"sed -i -e 's/^#.*$//g' userguide.rst")
system_or_exit("%s userguide.rst --output-encoding=utf8 --template=%s --table-style booktabs > UserGuide-cam.tex" % (rst2latex, template))
system_or_exit("sed -i -e \"/\\\\label{[^}]*}%$/d\" UserGuide-cam.tex")
if not args.guide:
    # full guide: one page for each menu entry
    sub("UserGuide-cam.tex", r"\\subsubsection", r"\\newpage\\subsubsection")
    sub("UserGuide-cam.tex", r"\\subsection", r"\\newpage\\subsection")
# (a short guide has one page for each menu, see its template)

system_or_exit(r"sed -i -e 's/width=10cm/width=7cm/g' UserGuide-cam.tex") # hack for liveview screen

sed_sub_tex_spec_chars("UserGuide-cam.tex")

replace("UserGuide-cam.tex", r"""\newpage\subsection*{\phantomsection%
  Movie mode%""", r"""\subsection*{\phantomsection%
  Movie mode%""");

replace("UserGuide-cam.tex", r"""\newpage\subsection*{\phantomsection%
  PLAY mode shortcuts%""", r"""\subsection*{\phantomsection%
  PLAY mode shortcuts%""");

system_or_exit("lualatex -interaction=batchmode UserGuide-cam.tex")

if args.tex_only:
    raise SystemExit # from this point it's very slow

os.makedirs(outdir, exist_ok=True)
for f in glob.glob(os.path.join(outdir, "*")):
    os.remove(f)

# menuindex.txt (written by lualatex) -> menuidx.dat
write_menu_index(outdir, menu_header = not args.guide)

print('pdf to png...')
system_or_exit("pdftoppm -r 152.2 %s -png UserGuide-cam.pdf %s/page" % ("-aa no -aaVector no" if args.no_antialias else "", outdir))

# from remap.py

# Canon's palette, from a picture of it: 16x16 color patches
def read_rgb(f):
    M = np.zeros([16,16,3])
    im = np.flipud(np.asarray(Image.open(f).convert("RGB")))
    h,w,c = im.shape
    assert c==3
    lw = w//16
    lh = h//16

    for c in range(3):
        for i in range(16):
            for j in range(16):
                ia = i*lh+5
                ib = (i+1)*lh-5
                ja = j*lw+5
                jb = (j+1)*lw-5
                M[i,j,c] = np.mean(im[ia:ib,ja:jb,c])
    return M

# ML's palette on cameras with a RGBA display buffer (FEATURE_VRAM_RGBA):
# indexed2rgbLUT from src/bmp.c, 0xAARRGGBB, with the generic values (#else)
def read_ml_palette(f):
    src = open(f).read()
    m = re.search(r"indexed2rgbLUT\[RGB_LUT_SIZE\]\s*=\s*\{(.*?)\};", src, re.DOTALL)
    assert m, "indexed2rgbLUT not found in %s" % f
    lut = re.sub(r"#ifdef.*?#else", "", m.group(1), flags=re.DOTALL)  # camera-specific values
    lut = re.sub(r"//[^\n]*", "", lut)
    lut = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{8}", lut)]
    assert len(lut) == 80, "expected 80 colors in indexed2rgbLUT, found %d" % len(lut)
    M = np.zeros([256,3])
    for i,argb in enumerate(lut):
        M[i] = [(argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF]
    return M.reshape((16,16,3))

def rgb2ind(rgb, mf, usable):
    dif = mf - rgb*np.ones((256,1))
    dif = (dif**2).sum(1)
    dif[~usable] = 1e10
    return np.argmin(dif)

def usable_colors(mf):
    usable = np.zeros(256, dtype=bool)
    usable[:80] = True  # only first 80 entries are safe
    usable[0] = False   # transparent
    usable[3] = False
    usable[0x14] = False
    return usable

def remap_rgb(im,M):
    h,w,c = im.shape
    assert c==3
    mf = M.flatten().reshape((256,3))
    usable = usable_colors(mf)
    # match each distinct color once
    packed = (im[:,:,0].astype(np.uint32) << 16) | (im[:,:,1].astype(np.uint32) << 8) | im[:,:,2]
    colors, inverse = np.unique(packed, return_inverse=True)
    ind = np.array([rgb2ind(np.array([p >> 16, (p >> 8) & 0xFF, p & 0xFF]), mf, usable) for p in colors])
    return ind[inverse].reshape((h,w))

def save_img(im,M,file):
    ix = Image.fromarray(np.flipud(im.astype(np.uint8)), "P")
    mf = M.flatten().reshape((256,3))
    ix.putpalette(mf.flatten().astype(np.uint8).tolist())
    ix.save(file)


if args.ml_palette:
    M = read_ml_palette("../src/bmp.c")
else:
    M = read_rgb("../data/vram/Palette.jpg")
for i in range(3):
    print(M[:,:,i].astype(np.uint8))

def convert_page(k):
    png = "%s/page-%03d.png" % (outdir, k)
    if not os.path.isfile(png): png = "%s/page-%02d.png" % (outdir, k)
    if not os.path.isfile(png): png = "%s/page-%d.png" % (outdir, k)
    if not os.path.isfile(png):
        print("done?")
        raise SystemExit
    bmp = "%s/page-%03d.bmp" % (outdir, k)
    bmh = "%s/page-%03d.bmh" % (outdir, k)

    print("remapping %s..." % png)
    im = np.flipud(np.asarray(Image.open(png).convert("RGB")))
    imr = remap_rgb(im,M)
    save_img(imr[1:481,0:720],M,bmp)
    system_or_exit("ruby ../src/convertrle.rb %s" % bmp)
    os.remove(bmp)
    os.remove(png)
    os.rename(bmp + ".rle", bmh)

for i in range(1,1000):
    convert_page(i)
