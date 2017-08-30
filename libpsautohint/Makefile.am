NULL =

bin_PROGRAMS = autohintexe

lib_LTLIBRARIES = libpsautohint.la

libpsautohint_la_SOURCES = \
    include/psautohint.h \
    src/ac.c \
    src/acfixed.c \
    src/ac.h \
    src/auto.c \
    src/basic.h \
    src/bbox.c \
    src/bbox.h \
    src/charpath.c \
    src/charpath.h \
    src/charpathpriv.c \
    src/charprop.c \
    src/check.c \
    src/control.c \
    src/eval.c \
    src/fix.c \
    src/flat.c \
    src/fontinfo.c \
    src/fontinfo.h \
    src/gen.c \
    src/head.c \
    src/logging.c \
    src/logging.h \
    src/memory.c \
    src/memory.h \
    src/merge.c \
    src/misc.c \
    src/opcodes.h \
    src/optable.c \
    src/optable.h \
    src/pick.c \
    src/psautohint.c \
    src/read.c \
    src/report.c \
    src/shuffle.c \
    src/stemreport.c \
    src/winstdint.h \
    src/write.c \
    $(NULL)

libpsautohint_la_CPPFLAGS = \
    -I$(top_srcdir)/include \
    $(NULL)

libpsautohint_la_CFLAGS = \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Wdeclaration-after-statement \
    -Wstrict-prototypes \
    -Wunused-macros \
    -Wmissing-prototypes \
    -fno-tree-vectorize \
    $(NULL)

autohintexe_SOURCES = \
    autohintexe.c \
    $(NULL)

autohintexe_CPPFLAGS = \
    -I$(top_srcdir)/include \
    $(NULL)

autohintexe_LDADD = \
    libpsautohint.la \
    -lm \
    $(NULL)
