include $(top_srcdir)/build/vars.build.mk
plugin = bracketcolors

geanyplugins_LTLIBRARIES = bracketcolors.la

bracketcolors_srcs = \
    bracketcolors.cc \
    BracketMap.cc \
    BracketMap.h \
    utils.h

bracketcolors_la_SOURCES = $(bracketcolors_srcs)
bracketcolors_la_CXXFLAGS = $(GEANY_CFLAGS) $(AM_CXXFLAGS) $(AM_CPPFLAGS) -DG_LOG_DOMAIN=\"BracketColors\"
bracketcolors_la_LIBADD = $(COMMONLIBS)

include $(top_srcdir)/build/cppcheck.mk
