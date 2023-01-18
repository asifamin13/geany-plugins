include $(top_srcdir)/build/vars.build.mk
plugin = bracketcolors

geanyplugins_LTLIBRARIES = bracketcolors.la

bracketcolors_la_SOURCES = bracketcolors.c
bracketcolors_la_CPPFLAGS = $(AM_CPPFLAGS) -DG_LOG_DOMAIN=\"BracketColors\"
bracketcolors_la_LIBADD = $(COMMONLIBS)

include $(top_srcdir)/build/cppcheck.mk
