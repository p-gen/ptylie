bin_PROGRAMS = ptylie
ptylie_SOURCES = ptylie.c
dist_man_MANS = ptylie.1
EXTRA_DIST = COPYRIGHT LICENSE.rst README.rst ptylie.rst build-aux \
	version .clang-format ptylie.gif ptylie.spec examples

install-exec-hook:
	chmod 4755 $(DESTDIR)$(bindir)/ptylie
