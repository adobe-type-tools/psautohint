import io
import os

from distutils import log
from setuptools import setup, Extension, Command

from setuptools.dist import Distribution
from distutils.errors import DistutilsSetupError
from distutils.sysconfig import customize_compiler as _customize_compiler
from distutils.dep_util import newer_group
from distutils.ccompiler import show_compilers

from setuptools.command.build_clib import build_clib as _build_clib
from setuptools.command.build_ext import build_ext as _build_ext


class Executable(Extension):
    pass


def customize_compiler(compiler):
    """The default distutils' customize_compiler does not forward CFLAGS,
    LDFLAGS and CPPFLAGS when linking executables, so we need to do it here.
    """
    _customize_compiler(compiler)

    if compiler.compiler_type == "unix":
        linker_exe = " ".join(compiler.linker_exe)
        if 'LDFLAGS' in os.environ:
            linker_exe += ' ' + os.environ['LDFLAGS']
        if 'CFLAGS' in os.environ:
            linker_exe += ' ' + os.environ['CFLAGS']
        if 'CPPFLAGS' in os.environ:
            linker_exe += ' ' + os.environ['CPPFLAGS']

        compiler.set_executable("linker_exe", linker_exe)


class CustomDistribution(Distribution):
    """ Adds an 'executables' setup keyword which must be a list of
    Exectutable instances.
    Pass it to 'distclass' setup keyword to replace the default setuptools
    Distribution class.
    """

    def __init__(self, attrs=None):
        self.executables = None
        if attrs:
            executables = attrs.get("executables")
            if executables:
                del attrs["executables"]
                self.executables = executables
        Distribution.__init__(self, attrs)

    def has_executables(self):
        return self.executables and len(self.executables) > 0


# this code is mostly derived from distutils' build_ext, only that it links a
# native executable (independent of python) instead of an extension module.

class build_exe(Command):

    description = "build C/C++ executables (compile/link to build directory)"

    sep_by = " (separated by '%s')" % os.pathsep
    user_options = [
        ('build-lib=', 'b',
         "directory for compiled executables"),
        ('build-temp=', 't',
         "directory for temporary files (build by-products)"),
        ('inplace', 'i',
         "ignore build-lib and put compiled executables into the source " +
         "directory alongside your pure Python modules"),
        ('include-dirs=', 'I',
         "list of directories to search for header files" + sep_by),
        ('define=', 'D',
         "C preprocessor macros to define"),
        ('undef=', 'U',
         "C preprocessor macros to undefine"),
        ('libraries=', 'l',
         "external C libraries to link with"),
        ('library-dirs=', 'L',
         "directories to search for external C libraries" + sep_by),
        ('rpath=', 'R',
         "directories to search for shared C libraries at runtime"),
        ('link-objects=', 'O',
         "extra explicit link objects to include in the link"),
        ('debug', 'g',
         "compile/link with debugging information"),
        ('force', 'f',
         "forcibly build everything (ignore file timestamps)"),
        ('compiler=', 'c',
         "specify the compiler type"),
        ('asan', None, 'debug + Address Sanitizer'),
    ]

    boolean_options = ['inplace', 'debug', 'force', 'asan']

    help_options = [
        ('help-compiler', None,
         "list available compilers", show_compilers),
    ]

    def initialize_options(self):
        self.executables = None
        self.build_lib = None
        self.plat_name = None
        self.build_temp = None
        self.inplace = 0

        self.include_dirs = None
        self.define = None
        self.undef = None
        self.libraries = None
        self.library_dirs = None
        self.rpath = None
        self.link_objects = None
        self.debug = None
        self.force = None
        self.compiler = None
        self.asan = None

    def finalize_options(self):
        self.set_undefined_options('build',
                                   ('build_lib', 'build_lib'),
                                   ('build_temp', 'build_temp'),
                                   ('compiler', 'compiler'),
                                   ('debug', 'debug'),
                                   ('force', 'force'),
                                   )

        self.executables = self.distribution.executables

        self.ensure_string_list('libraries')
        self.ensure_string_list('link_objects')

        if self.libraries is None:
            self.libraries = []
        if self.library_dirs is None:
            self.library_dirs = []
        elif isinstance(self.library_dirs, str):
            self.library_dirs = self.library_dirs.split(os.pathsep)

        if self.rpath is None:
            self.rpath = []
        elif isinstance(self.rpath, str):
            self.rpath = self.rpath.split(os.pathsep)

        if self.asan:
            # implies debug
            self.debug = 1

            asancflags = " -O0 -g -fsanitize=address"
            cflags = os.environ.get('CFLAGS', '') + asancflags
            os.environ['CFLAGS'] = cflags

            asanldflags = " -fsanitize=address -shared-libasan"
            ldflags = os.environ.get('LDFLAGS', '') + asanldflags
            os.environ['LDFLAGS'] = ldflags

        # for executables under windows use different directories
        # for Release and Debug builds.
        if os.name == 'nt':
            if self.debug:
                self.build_temp = os.path.join(self.build_temp, "Debug")
            else:
                self.build_temp = os.path.join(self.build_temp, "Release")

        # The argument parsing will result in self.define being a string, but
        # it has to be a list of 2-tuples.  All the preprocessor symbols
        # specified by the 'define' option will be set to '1'.  Multiple
        # symbols can be separated with commas.
        if self.define:
            defines = self.define.split(',')
            self.define = [(symbol, '1') for symbol in defines]

        # The option for macros to undefine is also a string from the
        # option parsing, but has to be a list.  Multiple symbols can also
        # be separated with commas here.
        if self.undef:
            self.undef = self.undef.split(',')

    def run(self):
        from distutils.ccompiler import new_compiler

        # 'self.executables', as supplied by setup.py, is a list of
        # Executable instances.
        if not self.executables:
            return

        # If we were asked to build any C/C++ libraries, make sure that the
        # directory where we put them is in the library search path for
        # linking executables.
        if self.distribution.has_c_libraries():
            build_clib = self.get_finalized_command('build_clib')
            self.libraries.extend(build_clib.get_library_names() or [])
            self.library_dirs.append(build_clib.build_clib)
            # make sure build_clib is run before build_exe (no-op if command
            # has already run)
            self.run_command("build_clib")

        # Setup the CCompiler object that we'll use to do all the
        # compiling and linking
        self.compiler = new_compiler(compiler=self.compiler,
                                     verbose=self.verbose,
                                     dry_run=self.dry_run,
                                     force=self.force)
        customize_compiler(self.compiler)

        # And make sure that any compile/link-related options (which might
        # come from the command-line or from the setup script) are set in
        # that CCompiler object -- that way, they automatically apply to
        # all compiling and linking done here.
        if self.include_dirs is not None:
            self.compiler.set_include_dirs(self.include_dirs)
        if self.define is not None:
            # 'define' option is a list of (name,value) tuples
            for (name, value) in self.define:
                self.compiler.define_macro(name, value)
        if self.undef is not None:
            for macro in self.undef:
                self.compiler.undefine_macro(macro)
        if self.libraries is not None:
            self.compiler.set_libraries(self.libraries)
        if self.library_dirs is not None:
            self.compiler.set_library_dirs(self.library_dirs)
        if self.rpath is not None:
            self.compiler.set_runtime_library_dirs(self.rpath)
        if self.link_objects is not None:
            self.compiler.set_link_objects(self.link_objects)

        # Now actually compile and link everything.
        self.build_executables()

    def check_executables_list(self, executables):
        """Ensure that the list of executables is valid, i.e. it is a
        list of Executable objects.
        Raise DistutilsSetupError if the structure is invalid anywhere;
        just returns otherwise.
        """
        if (not isinstance(executables, list) or
                not all(isinstance(e, Executable) for e in executables)):
            raise DistutilsSetupError(
                "'executables' option must be a list of Extension instances")

    def get_source_files(self):
        self.check_executables_list(self.executables)
        filenames = []
        for exe in self.executables:
            filenames.extend(exe.sources)
            filenames.extend(exe.depends)
        return filenames

    def get_outputs(self):
        self.check_executables_list(self.executables)
        outputs = []
        for exe in self.executables:
            outputs.append(self.get_exe_fullpath(exe.name))
        return outputs

    def build_executables(self):
        self.check_executables_list(self.executables)
        for exe in self.executables:
            self.build_executable(exe)

    def build_executable(self, exe):
        sources = exe.sources
        if sources is None or not isinstance(sources, (list, tuple)):
            raise DistutilsSetupError(
                "in 'executables' option (executable '%s'), "
                "'sources' must be present and must be "
                "a list of source filenames" % exe.name)
        sources = list(sources)

        exe_path = self.get_exe_fullpath(exe.name)
        depends = sources + exe.depends
        if not (self.force or newer_group(depends, exe_path, 'newer')):
            log.debug("skipping '%s' executable (up-to-date)", exe.name)
            return
        else:
            log.info("building '%s' executable", exe.name)

        extra_args = exe.extra_compile_args or []

        macros = exe.define_macros[:]
        for undef in exe.undef_macros:
            macros.append((undef,))

        objects = self.compiler.compile(sources,
                                        output_dir=self.build_temp,
                                        macros=macros,
                                        include_dirs=exe.include_dirs,
                                        debug=self.debug,
                                        extra_postargs=extra_args,
                                        depends=exe.depends)

        if exe.extra_objects:
            objects.extend(exe.extra_objects)
        extra_args = exe.extra_link_args or []

        language = exe.language or self.compiler.detect_language(sources)

        pkg_dir, exe_filename = os.path.split(self.get_exe_filename(exe.name))
        # '.exe' suffix is added automatically as needed by the CCompiler
        exe_name, _ = os.path.splitext(exe_filename)

        # we compile the executable into a subfolder of build_temp and
        # finally copy only the executable file to the destination, ignoring
        # the other build by-products
        build_temp = os.path.join(self.build_temp, pkg_dir)

        self.compiler.link_executable(
            objects, exe_name,
            output_dir=build_temp,
            libraries=exe.libraries,
            library_dirs=exe.library_dirs,
            runtime_library_dirs=exe.runtime_library_dirs,
            extra_postargs=extra_args,
            debug=self.debug,
            target_lang=language)

        self.mkpath(os.path.dirname(exe_path))
        self.copy_file(os.path.join(build_temp, exe_filename), exe_path)

    # -- Name generators -----------------------------------------------
    # (executable names, filenames, whatever)
    def get_exe_fullpath(self, exe_name):
        """Returns the path of the filename for a given executable.

        The file is located in `build_lib` or directly in the package
        (inplace option).
        """
        modpath = exe_name.split('.')
        filename = self.get_exe_filename(modpath[-1])

        if not self.inplace:
            # no further work needed
            # returning :
            #   build_dir/package/path/filename
            filename = os.path.join(*modpath[:-1] + [filename])
            return os.path.join(self.build_lib, filename)

        # the inplace option requires to find the package directory
        # using the build_py command for that
        package = '.'.join(modpath[0:-1])
        build_py = self.get_finalized_command('build_py')
        package_dir = os.path.abspath(build_py.get_package_dir(package))

        # returning
        #   package_dir/filename
        return os.path.join(package_dir, filename)

    def get_exe_filename(self, exe_name):
        r"""Convert the name of an executable (eg. "foo.bar") into the name
        of the file from which it will be loaded (eg. "foo/bar", or
        "foo\bar.exe").
        """
        from distutils.sysconfig import get_config_var
        exe_path = exe_name.split('.')
        exe_suffix = get_config_var('EXE')
        return os.path.join(*exe_path) + exe_suffix


class CustomBuildClib(_build_clib):
    """Includes in the sdist all the libraries' headers (filenames
    specified in the 'obj_deps' dict of a library's build_info dict).
    """

    def get_source_files(self):
        filenames = _build_clib.get_source_files(self)

        msg = (
            "in 'libraries' option (library '%s'), "
            "'obj_deps' must be a dictionary of "
            "type 'source: list'"
        )
        for (lib_name, build_info) in self.libraries:
            sources = build_info['sources']

            obj_deps = build_info.get('obj_deps', dict())
            if not isinstance(obj_deps, dict):
                raise DistutilsSetupError(msg % lib_name)

            global_deps = obj_deps.get('', [])
            if not isinstance(global_deps, (list, tuple)):
                raise DistutilsSetupError(msg % lib_name)

            filenames.extend(global_deps)

            for source in sources:
                extra_deps = obj_deps.get(source, [])
                if not isinstance(extra_deps, (list, tuple)):
                    raise DistutilsSetupError(msg % lib_name)

            filenames.extend(extra_deps)

        return filenames

    def build_libraries(self, libraries):
        for (lib_name, build_info) in libraries:
            sources = build_info.get('sources')
            if sources is None or not isinstance(sources, (list, tuple)):
                raise DistutilsSetupError(
                    "in 'libraries' option (library '%s'), "
                    "'sources' must be present and must be "
                    "a list of source filenames" % lib_name)
            sources = list(sources)

            log.info("building '%s' library", lib_name)

            # First, compile the source code to object files in the library
            # directory.  (This should probably change to putting object
            # files in a temporary build directory.)
            macros = build_info.get('macros')
            include_dirs = build_info.get('include_dirs')

            objects = self.compiler.compile(sources,
                                            output_dir=self.build_temp,
                                            macros=macros,
                                            include_dirs=include_dirs,
                                            debug=self.debug)

            # Now "link" the object files together into a static library.
            # (On Unix at least, this isn't really linking -- it just
            # builds an archive.  Whatever.)
            self.compiler.create_static_lib(objects, lib_name,
                                            output_dir=self.build_clib,
                                            debug=self.debug)


class CustomBuildExt(_build_ext):

    def run(self):
        if self.distribution.has_c_libraries():
            self.run_command("build_clib")
        _build_ext.run(self)


cmdclass = {
    'build_clib': CustomBuildClib,
    'build_ext': CustomBuildExt,
    'build_exe': build_exe,
}


libraries = [
    (
        "psautohint",
        {
            "sources": [
                "libpsautohint/src/ac.c",
                "libpsautohint/src/acfixed.c",
                "libpsautohint/src/auto.c",
                "libpsautohint/src/bbox.c",
                "libpsautohint/src/buffer.c",
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
            "include_dirs": [
                "libpsautohint/include",
            ],
            "obj_deps": {
                # TODO: define per-file dependecies instead of global ones
                # so that only the modified object files are recompiled
                "": [
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
                    # "libpsautohint/src/version.h",  # autogenerated
                ],
            },
            "macros": [
                ("AC_C_LIB_EXPORTS", None),
            ],
            "cflags": [
                # fixes segmentation fault when python (and thus the extension
                # module) is compiled with -O3 and tree vectorize:
                # https://github.com/khaledhosny/psautohint/issues/16
                "-fno-tree-vectorize",
            ] if os.name != "nt" else [],
        }
    ),
]

ext_modules = [
    Extension(
        "psautohint._psautohint",
        include_dirs=[
            "libpsautohint/include",
        ],
        sources=[
            "python/psautohint/_psautohint.c",
        ],
    ),
]

executables = [
    Executable(
        "psautohint.autohintexe",
        sources=[
            "libpsautohint/autohintexe.c"
        ],
        include_dirs=[
            "libpsautohint/include",
        ],
        # when building with MSVC on Windows, don't link the C math library.
        # XXX what about MinGW or Cygwin?

        # Normally we wouldn't need to pass '-lpsautohint' explicitly here as
        # that's done by default automatically for all the libraries built by
        # build_clib. The problem is Distutils CCompiler places the global
        # linker options after the ones specific to the extension/executable
        # being linked, and when linking libpsautohint in the standalone
        # executable, we want '-lpsautohint' to be placed before '-lm' in the
        # linker command line, otherwise we get linker errors (strangely, only
        # when running gcc with --coverage):
        # https://travis-ci.org/adobe-type-tools/psautohint/jobs/423944212#L581
        # It should be ok if '-lpsautohint' is mentioned twice.
        libraries=["psautohint"] + (["m"] if os.name != "nt" else []),
    ),
]

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
      libraries=libraries,
      ext_modules=ext_modules,
      executables=executables,
      entry_points={
          'console_scripts': [
              "psautohint = psautohint.__main__:main",
              "psstemhist = psautohint.__main__:stemhist",
          ],
      },
      python_requires='>3.7',
      setup_requires=["setuptools_scm"],
      install_requires=[
          'fonttools[ufo]>=4.22.0',
      ],
      extras_require={
          "testing": [
              "pytest >= 3.0.0, <4",
              "pytest-cov >= 2.5.1, <3",
              "pytest-xdist >= 1.22.2, <1.28.0",
              "pytest-randomly >= 1.2.3, <2",
          ],
      },
      cmdclass=cmdclass,
      distclass=CustomDistribution,
      zip_safe=False,
      classifiers=[
          'Development Status :: 5 - Production/Stable',
          'Environment :: Console',
          'Intended Audience :: Developers',
          'License :: OSI Approved :: Apache Software License',
          'Natural Language :: English',
          'Operating System :: OS Independent',
          'Programming Language :: Python',
          'Programming Language :: Python :: 3 :: Only',
          'Programming Language :: Python :: 3.7',
          'Topic :: Text Processing :: Fonts',
          'Topic :: Multimedia :: Graphics',
          'Topic :: Multimedia :: Graphics :: Graphics Conversion',
      ],
      )
