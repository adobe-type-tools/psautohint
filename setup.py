import platform
from distutils.core import setup, Extension

module1 = Extension("_psautohint",
                    define_macros = [
                        ('AC_C_LIB', 1),
                        ('ACLIB_EXPORTS', 1),
                    ],
                    include_dirs = [
                        "source/include/ac",
                        "source/ac",
                        "source/bf",
                    ],
                    sources = [
                        "source/_psautohint.c",
                        "source/ac/ac.c",
                        "source/ac/ac_C_lib.c",
                        "source/ac/acfixed.c",
                        "source/ac/auto.c",
                        "source/ac/bbox.c",
                        "source/ac/charprop.c",
                        "source/ac/check.c",
                        "source/ac/control.c",
                        "source/ac/cswrite.c",
                        "source/ac/eval.c",
                        "source/ac/fix.c",
                        "source/ac/flat.c",
                        "source/ac/fontinfo.c",
                        "source/ac/gen.c",
                        "source/ac/head.c",
                        "source/ac/merge.c",
                        "source/ac/misc.c",
                        "source/ac/path.c",
                        "source/ac/pick.c",
                        "source/ac/read.c",
                        "source/ac/report.c",
                        "source/ac/shuffle.c",
                        "source/ac/stub.c",
                        "source/ac/write.c",
                        "source/bf/charpath.c",
                        "source/bf/charpathpriv.c",
                        "source/bf/cryptprocs.c",
                        "source/bf/fileops.c",
                        "source/bf/filookup.c",
                        "source/bf/machinedep.c",
                    ]
        )

if platform.system() == "Windows":
    module1.include_dirs.append("source/include/winextras")

setup (name = "PsAutoHint",
       version = "1.0",
       description = "Python wrapper for Adobe's PostScrupt autohinter",
       ext_modules = [module1])

