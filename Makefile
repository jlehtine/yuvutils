PACKAGE_TARNAME = jl-yuvutils
PACKAGE_VERSION = VERSION
package_tarnamever = $(PACKAGE_TARNAME)-$(PACKAGE_VERSION)

MJPEGTOOLS_INCLUDE_PATH = -I/usr/local/include/mjpegtools
MJPEGTOOLS_LIBMJPEGUTILS = -lmjpegutils

prefix = /usr/local
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
mandir = $(prefix)/man
srcdir = .

CC = gcc
CFLAGS = -O2 -Wall -pedantic
CPPFLAGS = -DNDEBUG $(MJPEGTOOLS_INCLUDE_PATH)
LIBS = $(MJPEGTOOLS_LIBMJPEGUTILS) -lm
VPATH = $(srcdir)

BINARIES = yuvresample yuvinfo yuvadjust

all: $(BINARIES)

clean:
	rm -f *.o
	rm -f $(BINARIES)

install: $(BINARIES)
	test -d '$(DESTDIR)$(bindir)' \
		|| ( umask 022 && mkdir -p '$(DESTDIR)$(bindir)' )
	for f in $(BINARIES); do \
		cp $$f '$(DESTDIR)$(bindir)' \
			&& chmod 755 '$(DESTDIR)$(bindir)'"/$$f"; \
	done
	test -d '$(DESTDIR)$(mandir)/man1' \
		|| ( umask 022 && mkdir -p '$(DESTDIR)$(mandir)/man1' )
	for f in $(BINARIES:=.1); do \
		cp '$(srcdir)/man'/$$f '$(DESTDIR)$(mandir)/man1' \
			&& chmod 644 '$(DESTDIR)$(mandir)/man1'/$$f; \
	done
	@echo
	@echo 'Binaries were installed to $(DESTDIR)$(bindir).'
	@echo 'Man pages were installed to $(DESTDIR)$(mandir).'

dist:
	rm -rf '$(package_tarnamever)' '$(package_tarnamever).tar' '$(package_tarnamever).tar.gz'
	mkdir '$(package_tarnamever)'
	cp $(BINARIES:=.c) Makefile README COPYING '$(package_tarnamever)'
	mkdir '$(package_tarnamever)/man'
	cp man/*.1 '$(package_tarnamever)/man'
	mkdir '$(package_tarnamever)/html'
	cp html/*.html '$(package_tarnamever)/html'
	tar cf '$(package_tarnamever).tar' '$(package_tarnamever)'
	rm -rf '$(package_tarnamever)'
	gzip '$(package_tarnamever).tar'

%: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

.PHONY: all clean dist
