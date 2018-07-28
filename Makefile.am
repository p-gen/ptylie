bin_PROGRAMS = ptylie
ptylie_SOURCES = ptylie.c tree.c
dist_man_MANS = ptylie.1
ptylie_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
ptylie_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)
EXTRA_DIST = COPYRIGHT LICENSE.rst README.rst ptylie.rst build-aux \
             version .clang-format ptylie.gif ptylie.spec examples \
             ptylie-rpmlintrc Changelog build.sh

install-exec-hook:
	chmod 4755 $(DESTDIR)$(bindir)/ptylie
