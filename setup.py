import io
import os
import sys

from distutils import log
from distutils.command.build_scripts import build_scripts
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext


class CustomBuildExt(build_ext):

    def build_extension(self, ext):
        compiler_type = self.compiler.compiler_type

        if compiler_type == "unix":
            if ext.extra_compile_args is None:
                ext.extra_compile_args = []
            # fixes segmentation fault when python (and thus the extension
            # module) is compiled with -O3 and tree vectorize:
            # https://github.com/khaledhosny/psautohint/issues/16
            ext.extra_compile_args.append("-fno-tree-vectorize")

        build_ext.build_extension(self, ext)


class MesonExecutableTarget(str):

    def __new__(self, name, src):
        filename = name + ".exe" if sys.platform == "win32" else name
        return str.__new__(self, filename)

    def __init__(self, name, src):
        self.src = src


class CustomBuildScripts(build_scripts):
    """ Calls meson and ninja to build native executables that are installed
    like scripts in Python's bin or Scripts folder.
    This replaces the distutils build_scripts command.
    """

    user_options = build_scripts.user_options + [
        ('build-temp=', 't',
         "directory for temporary meson/ninja build by-products"),
    ]

    def initialize_options(self):
        build_scripts.initialize_options(self)
        self.build_temp = None

    def finalize_options(self):
        build_scripts.finalize_options(self)
        if self.build_temp is None:
            # group meson/ninja build files under a 'scripts' subfolder
            # inside the default build_temp folder
            build_cmd = self.distribution.get_command_obj("build")
            build_cmd.ensure_finalized()
            self.build_temp = os.path.join(build_cmd.build_temp, "scripts")

    def run(self):
        if not self.scripts:
            return
        self.build_executables()

    def build_executables(self):
        for target in self.scripts:
            if isinstance(target, MesonExecutableTarget):
                self.build_executable(target)
            else:
                from distutils.errors import DistutilsSetupError
                raise DistutilsSetupError(
                    "expected MesonExecutableTarget, found %s: %r"
                    % (type(target).__name__, target)
                )

    def configure(self, src):
        if os.path.exists(os.path.join(self.build_temp, "build.ninja")):
            if self.force:
                self.spawn(["ninja", "-C", self.build_temp, "reconfigure"])
            else:
                log.info("build directory already configured")
            return

        self.mkpath(self.build_temp)
        self.spawn(["meson",
                    "--buildtype=release",
                    "--strip",
                    "--default-library=static",
                    "--backend=ninja",
                    self.build_temp,
                    src])

    def build_executable(self, target):
        self.configure(target.src)

        if self.force:
            self.spawn(["ninja", "-C", self.build_temp, "-t", "clean", target])
        self.spawn(["ninja", "-C", self.build_temp, target])

        executable = os.path.join(self.build_temp, target)
        self.mkpath(self.build_dir)
        outfile = os.path.join(self.build_dir, target)
        self.copy_file(executable, outfile)

        # when building executable with VS2008 a *.exe.manifest xml file is
        # generated alongside it. This links the executable to the respective
        # MSVC runtime DLL. If this is not in the same folder as the exe, an
        # error dialog "cannot find MSVCR90.DLL" pops up.
        # TODO Embed the manifest inside the executable using mt.exe:
        # https://msdn.microsoft.com/en-us/library/ms235591.aspx
        if sys.platform == "win32" and os.path.exists(executable + ".manifest"):
            self.copy_file(executable + ".manifest",
                           outfile + ".manifest")


module1 = Extension("psautohint._psautohint",
                    include_dirs=[
                        "libpsautohint/include",
                    ],
                    sources=[
                        "python/psautohint/_psautohint.c",
                        "libpsautohint/src/ac.c",
                        "libpsautohint/src/acfixed.c",
                        "libpsautohint/src/auto.c",
                        "libpsautohint/src/bbox.c",
                        "libpsautohint/src/charpath.c",
                        "libpsautohint/src/charpathpriv.c",
                        "libpsautohint/src/charprop.c",
                        "libpsautohint/src/check.c",
                        "libpsautohint/src/control.c",
                        "libpsautohint/src/eval.c",
                        "libpsautohint/src/fix.c",
                        "libpsautohint/src/flat.c",
                        "libpsautohint/src/fontinfo.c",
                        "libpsautohint/src/gen.c",
                        "libpsautohint/src/head.c",
                        "libpsautohint/src/logging.c",
                        "libpsautohint/src/memory.c",
                        "libpsautohint/src/merge.c",
                        "libpsautohint/src/misc.c",
                        "libpsautohint/src/optable.c",
                        "libpsautohint/src/pick.c",
                        "libpsautohint/src/psautohint.c",
                        "libpsautohint/src/read.c",
                        "libpsautohint/src/report.c",
                        "libpsautohint/src/shuffle.c",
                        "libpsautohint/src/stemreport.c",
                        "libpsautohint/src/write.c",
                    ],
                    depends=[
                        "libpsautohint/include/psautohint.h",
                        "libpsautohint/src/ac.h",
                        "libpsautohint/src/basic.h",
                        "libpsautohint/src/bbox.h",
                        "libpsautohint/src/charpath.h",
                        "libpsautohint/src/fontinfo.h",
                        "libpsautohint/src/logging.h",
                        "libpsautohint/src/memory.h",
                        "libpsautohint/src/opcodes.h",
                        "libpsautohint/src/optable.h",
                        "libpsautohint/src/winstdint.h",
                        "libpsautohint/src/version.h",
                    ],
                    )

with io.open("README.md", encoding="utf-8") as readme:
    long_description = readme.read()

VERSION_TEMPLATE = """\
/* file generated by setuptools_scm
   don't change, don't track in version control */
#define PSAUTOHINT_VERSION "{version}"
"""

setup(name="psautohint",
      use_scm_version={
          "write_to": "libpsautohint/src/version.h",
          "write_to_template": VERSION_TEMPLATE,
      },
      description="Python wrapper for Adobe's PostScript autohinter",
      long_description=long_description,
      long_description_content_type='text/markdown',
      url='https://github.com/adobe-type-tools/psautohint',
      author='Adobe Type team & friends',
      author_email='afdko@adobe.com',
      license='Apache License, Version 2.0',
      package_dir={'': 'python'},
      packages=['psautohint'],
      ext_modules=[module1],
      scripts=[
          MesonExecutableTarget("autohintexe", src="libpsautohint"),
      ],
      entry_points={
          'console_scripts': [
              "psautohint = psautohint.__main__:main",
          ],
      },
      setup_requires=["setuptools_scm"],
      install_requires=[
          'fonttools>=3.1.2',
          'ufoLib',
      ],
      extras_require={
          "testing": [
              "pytest >= 3.0.0, <4",
              "pytest-cov >= 2.5.1, <3",
              "pytest-xdist >= 1.22.2, <2",
              "pytest-randomly >= 1.2.3, <2",
          ],
      },
      cmdclass={
          'build_ext': CustomBuildExt,
          'build_scripts': CustomBuildScripts,
      },
      classifiers=[
          'Development Status :: 4 - Beta',
          'Environment :: Console',
          'Intended Audience :: Developers',
          'License :: OSI Approved :: Apache Software License',
          'Natural Language :: English',
          'Operating System :: OS Independent',
          'Programming Language :: Python',
          'Programming Language :: Python :: 2',
          'Programming Language :: Python :: 3',
          'Topic :: Text Processing :: Fonts',
          'Topic :: Multimedia :: Graphics',
          'Topic :: Multimedia :: Graphics :: Graphics Conversion',
      ],
      )
