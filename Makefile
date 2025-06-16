PDFLATEX = pdflatex
BIBTEX = bibtex
SRC = main.tex
OUT = main.pdf

all: $(OUT)

$(OUT): $(SRC) preamble.tex bibliography.bib
	$(PDFLATEX) $(SRC)
	$(BIBTEX) $(basename $(SRC))
	$(PDFLATEX) $(SRC)
	$(PDFLATEX) $(SRC)

clean:
	rm -f $(OUT) *.aux *.bbl *.blg *.log *.toc

.PHONY: all clean