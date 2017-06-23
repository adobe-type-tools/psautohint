AC_INIT([PSAutohint],
        [1.6.0],
        [https://github.com/khaledhosny/psautohint/issues],
        [psautohint],
        [https://github.com/khaledhosny/psautohint])

AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
AC_PROG_CC_C99

AM_INIT_AUTOMAKE([foreign subdir-objects color-tests -Wall -Wno-portability])
AM_SILENT_RULES([yes])

# Enable compiler warnings
#AX_COMPILER_FLAGS

LT_INIT

AC_CONFIG_FILES([
    Makefile
    psautohint.pc
])

AC_OUTPUT
