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
                        "libpsautohint/src/bbox.h",
                        "libpsautohint/src/fontinfo.h",
                        "libpsautohint/src/memory.h",
                        "libpsautohint/src/winstdint.h",
                        "libpsautohint/src/basic.h",
                        "libpsautohint/src/charpath.h",
                        "libpsautohint/src/logging.h",
                        "libpsautohint/src/opcodes.h",
                    ],
                    )

setup(name="psautohint",
      version="1.1.1.dev0",
      description="Python wrapper for Adobe's PostScript autohinter",
      url='https://github.com/khaledhosny/psautohint',
      author='Khaled Hosny',
      author_email='khaledhosny@eglug.org',
      license='Apache License, Version 2.0',
      package_dir={'': 'python'},
      packages=['psautohint'],
      ext_modules=[module1],
      entry_points={
          'console_scripts': [
              "psautohint = psautohint.__main__:main",
          ],
      },
      install_requires=[
          'fonttools>=3.1.2',
      ],
      cmdclass={
          'build_ext': CustomBuildExt,
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
