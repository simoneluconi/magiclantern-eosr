#!/usr/bin/env python3
# Check the help pages made by: mkdoc-cam.py --guide r6cam --out DIR ...
# Run from doc/, right after it:  python3 r6cam/check.py DIR
# - no menu spills onto a second page;
# - each name in menuidx.dat points to the page that shows it;
# - menuidx.dat has "\n" line ends and every page file is there.
import os, sys, subprocess

outdir = sys.argv[1] if len(sys.argv) > 1 else "cam"
errors = 0

def page_text(n):
    txt = subprocess.run(["pdftotext", "-f", str(n), "-l", str(n), "-layout", "UserGuide-cam.pdf", "-"],
                         capture_output=True, text=True).stdout
    return " ".join(txt.split()).lower()

index = [l.rstrip("\n").split(" ", 2) for l in open("menuindex.txt").readlines()[1:]]
extra = set(n.strip() for p, t, n in index if t == "label")  # not titles: can't be checked
titled = [int(p) for p, t, n in index if t in ("section", "subsection")]
last = max(int(p) for p, t, n in index)
if titled != list(range(2, last + 1)):
    errors += 1
    print("ERROR: a page is too full: pages", sorted(set(range(2, last + 1)) - set(titled)), "have no title of their own")

raw = open(os.path.join(outdir, "menuidx.dat"), "rb").read()
if b"\r" in raw or not raw.endswith(b"\n"):
    errors += 1
    print("ERROR: menuidx.dat must have \\n line ends only")

texts = {}
total = len(raw)
for line in raw.decode().splitlines():
    page, name = int(line[:3]), line[4:]
    if name in ("About Magic Lantern", "end"):
        continue
    if name in extra:
        print("%03d %-28s (\\helplabel)" % (page, name))
        continue
    if page not in texts:
        texts[page] = page_text(page)
    shown = name.lower() in texts[page]
    if not shown:
        errors += 1
    print("%03d %-28s %s" % (page, name, "" if shown else "ERROR: not on this page"))

for n in range(1, last + 1):
    f = os.path.join(outdir, "page-%03d.bmh" % n)
    if not os.path.isfile(f):
        errors += 1
        print("ERROR: missing", f)
    else:
        total += os.path.getsize(f)

print("%d pages, %d KB" % (last, total // 1024))
if total > 400 * 1024:
    print("WARNING: large; the card format keeps ML's files in a 2 MB buffer")
sys.exit(1 if errors else 0)
