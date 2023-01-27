include $(top_srcdir)/build/vars.build.mk
plugin = bracketcolors

geanyplugins_LTLIBRARIES = bracketcolors.la

bracketcolors_srcs = \
    bracketcolors.cc \
    BracketMap.cc \
    BracketMap.h

bracketcolors_la_SOURCES = $(bracketcolors_srcs)

nodist_bracketcolors_la_SOURCES = bracketcolors-ignore-styles.h
BUILT_SOURCES = bracketcolors-ignore-styles.h
CLEANFILES = bracketcolors-ignore-styles.h
bracketcolors-ignore-styles.h: generate_ignore_styles.py
	$(PYTHON) generate_ignore_styles.py --stdout --sci_lexer $(GEANY_INCLUDEDIR)/geany/scintilla/SciLexer.h >$@

bracketcolors_la_CXXFLAGS = $(GEANY_CFLAGS) $(AM_CXXFLAGS) $(AM_CPPFLAGS) -DG_LOG_DOMAIN=\"BracketColors\"
bracketcolors_la_LIBADD = $(COMMONLIBS)

include $(top_srcdir)/build/cppcheck.mk
