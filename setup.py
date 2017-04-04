import platform
from distutils.core import setup, Extension

module1 = Extension("_psautohint",
                    include_dirs = [
                        "source/include",
                    ],
                    sources = [
                        "source/_psautohint.c",
                        "source/ac/ac.c",
                        "source/ac/ac_C_lib.c",
                        "source/ac/acfixed.c",
                        "source/ac/auto.c",
                        "source/ac/bbox.c",
                        "source/ac/charpath.c",
                        "source/ac/charprop.c",
                        "source/ac/check.c",
                        "source/ac/control.c",
                        "source/ac/eval.c",
                        "source/ac/fix.c",
                        "source/ac/flat.c",
                        "source/ac/fontinfo.c",
                        "source/ac/gen.c",
                        "source/ac/head.c",
                        "source/ac/logging.c",
                        "source/ac/memory.c",
                        "source/ac/merge.c",
                        "source/ac/misc.c",
                        "source/ac/pick.c",
                        "source/ac/read.c",
                        "source/ac/report.c",
                        "source/ac/shuffle.c",
                        "source/ac/stemreport.c",
                        "source/ac/write.c",
                    ]
        )

setup (name = "PsAutoHint",
       version = "1.0",
       description = "Python wrapper for Adobe's PostScrupt autohinter",
       ext_modules = [module1])

