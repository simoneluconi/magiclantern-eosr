In-camera help for the EOS R6 (firmware 1.9.0)
==============================================

A short guide with only what the R6 build has, instead of the full user
guide (../MANUAL.txt, ../MN-*.txt). Same help system: Help tab in ML menu,
or INFO on a menu entry (src/menuhelp.c).

Sources:  index.txt (list), MANUAL.txt, MN-*.txt, ug-template-cam.tex
Pages:    ../../platform/R6.190/doc (menuidx.dat, page-NNN.bmh), committed;
          the R6 build copies them to ML/doc in the zip.

Rebuild after editing the texts (Linux or WSL), from doc/:

    python3 mkdoc-cam.py --guide r6cam --out ../platform/R6.190/doc --ml-palette --no-antialias
    python3 r6cam/check.py ../platform/R6.190/doc

--ml-palette: colors from ML's own palette (src/bmp.c), as the R6 has no
Canon palette. --no-antialias: text like ML's bitmap fonts; antialiased
pages are twice as large (RLE).

Needs python3 (docutils, numpy, PIL), TeX Live with lualatex (Ubuntu:
texlive-luatex texlive-latex-extra texlive-fonts-recommended
texlive-pictures), poppler-utils (pdftoppm, pdftotext) and ruby.

Rules for the texts:
- a title with `````` is a menu entry, spelled exactly as in the menu
  (.name in the code); "A and B" gives two entries for the same text;
- \helplabel{Name} (raw latex) gives the current page one more name;
- a title with ------ or ~~~~~~ starts a new page; all its text has to fit there
  (check.py tells if it doesn't). The card format keeps ML's files in a
  2 MB buffer: keep the whole folder around 300 KB.
