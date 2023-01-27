AC_DEFUN([GP_CHECK_BRACKETCOLORS],
[
    GP_ARG_DISABLE([Bracketcolors], [auto])
    GP_CHECK_PLUGIN_GTK3_ONLY([Bracketcolors])
    GP_CHECK_PLUGIN_DEPS([Bracketcolors], [BRACKETCOLORS], [geany >= 1.38])

    dnl Check for python3 interpreter to generate styles header
    dnl Check for c++17. We really only need c++11 but scintilla is
    dnl compiled with c++17 so just use that

    PYTHON_VERSION=3.0
    
    AS_IF(
        [test "$enable_bracketcolors" = "auto"], [
            AM_PATH_PYTHON(
                [${PYTHON_VERSION}],
                [AX_CXX_COMPILE_STDCXX_17(, optional)]
                [enable_bracketcolors=no]
            )
            AS_IF(
                [test "$PYTHON" != :], [
                    AS_IF(
                        [test ${HAVE_CXX17} = 1], [
                            enable_bracketcolors=yes
                        ],
                        [enable_bracketcolors=no]
                    )
                ],
                [enable_bracketcolors=no]                
            )
        ],
        [test "$enable_bracketcolors" = "yes"], [
            AM_PATH_PYTHON(
                [${PYTHON_VERSION}],
                [AX_CXX_COMPILE_STDCXX_17(, mandatory)],
                [AC_MSG_ERROR([Could not find python3 interpreter])]
            )
        ]
    )

    GP_COMMIT_PLUGIN_STATUS([Bracketcolors])
    AC_CONFIG_FILES([
        bracketcolors/Makefile
        bracketcolors/src/Makefile
    ])
])
