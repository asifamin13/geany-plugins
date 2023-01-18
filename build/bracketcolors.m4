AC_DEFUN([GP_CHECK_BRACKETCOLORS],
[
    GP_ARG_DISABLE([Bracketcolors], [auto])
    GP_CHECK_PLUGIN_DEPS([Bracketcolors], [BRACKETCOLORS],
                         [$GP_GTK_PACKAGE >= 2.8])
    GP_COMMIT_PLUGIN_STATUS([Bracketcolors])
    AC_CONFIG_FILES([
        bracketcolors/Makefile
        bracketcolors/src/Makefile
    ])
])
