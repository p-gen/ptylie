bin_PROGRAMS = ptylie
ptylie_SOURCES = ptylie.c
#dist_man_MANS = ptylie.1
EXTRA_DIST = LICENSE.rst README.rst build-aux version .clang-format

install-exec-hook:
	chmod 4755 $(DESTDIR)$(bindir)/ptylie
