.SUFFIXES: .1.html .1 .xml .rest.pdf .restscatter.pdf .summary.pdf .png .pdf .stack.pdf .aggr.pdf .scatter.pdf .aggrtemp.pdf
.PHONY: clean

include Makefile.configure

PREFIX 		 = /usr/local
VERSION		 = 0.0.8
LDFLAGS		+= -L/usr/local/lib -ldivecomputer -lm
CFLAGS		+= -I/usr/local/include -DVERSION="\"$(VERSION)\""
OBJS		 = common.o \
		   main.o \
		   download.o \
		   list.o \
		   xml.o
BINOBJS		 = divecmd2csv.o \
		   divecmd2divecmd.o \
		   divecmd2grap.o \
		   divecmd2json.o \
		   divecmd2term.o \
		   parser.o
PREBINS		 = divecmd2pdf \
		   divecmd2ps
BINS		 = divecmd \
		   divecmd2csv \
		   divecmd2divecmd \
		   divecmd2grap \
		   divecmd2json \
		   divecmd2term
MAN1S		 = divecmd.1 \
		   divecmd2csv.1 \
		   divecmd2divecmd.1 \
		   divecmd2grap.1 \
		   divecmd2json.1 \
		   divecmd2pdf.1 \
		   divecmd2ps.1 \
		   divecmd2term.1
PNGS		 = daily.aggr.png \
		   daily.aggrtemp.png \
		   daily.rest.png \
		   daily.restscatter.png \
		   daily.scatter.png \
		   daily.stack.png \
		   daily.summary.png \
		   daily.temp.png \
		   multiday.restscatter.png \
		   multiday.rsummary.png \
		   multiday.stack.png \
		   short.stack.png
PDFS		 = daily.aggr.pdf \
		   daily.aggrtemp.pdf \
		   daily.rest.pdf \
		   daily.restscatter.pdf \
		   daily.scatter.pdf \
		   daily.stack.pdf \
		   daily.summary.pdf \
		   daily.temp.pdf \
		   multiday.restscatter.pdf \
		   multiday.rsummary.pdf \
		   multiday.stack.pdf \
		   short.stack.pdf
HTMLS		 = divecmd.1.html \
		   divecmd2csv.1.html \
		   divecmd2divecmd.1.html \
		   divecmd2grap.1.html \
		   divecmd2json.1.html \
		   divecmd2pdf.1.html \
		   divecmd2ps.1.html \
		   divecmd2term.1.html \
		   index.html
CSSS		 = index.css \
		   mandoc.css
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/divecmd
XMLS		 = daily.xml \
		   day1.xml \
		   day2.xml \
		   multiday.xml \
		   temperature.xml
BUILT		 = screenshot1.png \
		   screenshot2.png \
		   slider.js
COMPAT_OBJS	 = compat_err.o \
		   compat_progname.o \
		   compat_reallocarray.o \
		   compat_strlcat.o \
		   compat_strlcpy.o \
		   compat_strtonum.o

all: $(BINS)

www: $(HTMLS) $(PDFS) $(PNGS) divecmd.tar.gz divecmd.tar.gz.sha512

installwww: www
	mkdir -p $(WWWDIR)
	mkdir -p $(WWWDIR)/snapshots
	install -m 0444 $(CSSS) $(HTMLS) $(PDFS) $(PNGS) $(XMLS) $(BUILT) $(WWWDIR)
	install -m 0444 divecmd.tar.gz $(WWWDIR)/snapshots/divecmd-$(VERSION).tar.gz
	install -m 0444 divecmd.tar.gz.sha512 $(WWWDIR)/snapshots/divecmd-$(VERSION).tar.gz.sha512
	install -m 0444 divecmd.tar.gz $(WWWDIR)/snapshots
	install -m 0444 divecmd.tar.gz.sha512 $(WWWDIR)/snapshots

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -m 0755 $(BINS) $(PREBINS) $(DESTDIR)$(BINDIR)
	install -m 0444 $(MAN1S) $(DESTDIR)$(MANDIR)/man1

divecmd: $(OBJS) $(COMPAT_OBJS)
	$(CC) -o $@ $(OBJS) $(COMPAT_OBJS) $(LDFLAGS) 

divecmd2divecmd: divecmd2divecmd.o parser.o $(COMPAT_OBJS)
	$(CC) -o $@ divecmd2divecmd.o parser.o $(COMPAT_OBJS) -lexpat

divecmd2term: divecmd2term.o parser.o $(COMPAT_OBJS)
	$(CC) -o $@ divecmd2term.o parser.o $(COMPAT_OBJS) -lexpat -lm

divecmd2grap: divecmd2grap.o parser.o $(COMPAT_OBJS)
	$(CC) -o $@ divecmd2grap.o parser.o $(COMPAT_OBJS) -lexpat

divecmd2json: divecmd2json.o parser.o $(COMPAT_OBJS)
	$(CC) -o $@ divecmd2json.o parser.o $(COMPAT_OBJS) -lexpat

divecmd2csv: divecmd2csv.o parser.o $(COMPAT_OBJS)
	$(CC) -o $@ divecmd2csv.o parser.o $(COMPAT_OBJS) -lexpat

$(PNGS): $(PDFS)

$(PDFS): divecmd2grap divecmd2divecmd

$(OBJS): extern.h config.h

$(COMPAT_OBJS): config.h

$(BINOBJS): parser.h config.h

divecmd.tar.gz:
	mkdir -p .dist/divecmd-$(VERSION)/
	install -m 0644 configure *.c *.h Makefile *.1 .dist/divecmd-$(VERSION)
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

divecmd.tar.gz.sha512: divecmd.tar.gz
	sha512 divecmd.tar.gz >$@

index.html: index.xml
	sed "s!@VERSION@!$(VERSION)!g" index.xml >$@

clean:
	rm -f $(OBJS) $(COMPAT_OBJS) $(BINS) $(BINOBJS) $(HTMLS) $(PDFS) $(PNGS)
	rm -f divecmd.tar.gz divecmd.tar.gz.sha512

distclean: clean
	rm -f Makefile.configure config.h config.log

.1.1.html:
	mandoc -Thtml -Ostyle=mandoc.css $< >$@

.xml.summary.pdf:
	./divecmd2grap -m summary $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.restscatter.pdf:
	./divecmd2grap -m restscatter $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

multiday.restscatter.pdf: day1.xml day2.xml
	./divecmd2grap -s date -m restscatter day1.xml day2.xml | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.rest.pdf:
	./divecmd2grap -m rest $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.stack.pdf:
	./divecmd2grap -m stack $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

short.stack.pdf: multiday.xml
	./divecmd2grap -s date -d -m stack multiday.xml | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.aggr.pdf:
	./divecmd2grap -m aggr $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

daily.aggrtemp.pdf: temperature.xml
	./divecmd2grap -m aggrtemp temperature.xml | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

daily.temp.pdf: temperature.xml
	./divecmd2divecmd -s temperature.xml | ./divecmd2grap -am temp | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

multiday.stack.pdf: multiday.xml
	./divecmd2grap -s date -m stack multiday.xml | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

multiday.rsummary.pdf: day1.xml day2.xml
	./divecmd2grap -s date -m rsummary day1.xml day2.xml | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.xml.scatter.pdf:
	./divecmd2grap -m scatter $< | groff -Gp -Tpdf -P-p5.8i,8.3i >$@

.pdf.png:
	convert -density 120 $< -flatten -trim $@
