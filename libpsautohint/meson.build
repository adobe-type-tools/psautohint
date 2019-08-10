project('psautohint', 'c', version : '1.7.0', default_options : 'c_std=c99')

pkg = import('pkgconfig')
cc = meson.get_compiler('c')

version_h = vcs_tag(input : 'src/version.h.in', output : 'version.h')
libm = cc.find_library('m', required : false)



cflags = [
  '-Wcast-align',
  '-Wdeclaration-after-statement',
  '-Wdouble-promotion',
  '-Wextra',
  '-Wimplicit-fallthrough',
  '-Wmissing-prototypes',
  '-Wmissing-variable-declarations',
  '-Wpedantic',
  '-Wstrict-prototypes',
  '-Wstringop-truncation',
  '-Wunreachable-code-break',
  '-Wunused-macros',
  '-Werror',
]

add_global_arguments(cc.get_supported_arguments(cflags), language : 'c')

libpsautohint = library(
  'psautohint',
  'include/psautohint.h',
  'src/ac.c',
  'src/acfixed.c',
  'src/ac.h',
  'src/auto.c',
  'src/basic.h',
  'src/bbox.c',
  'src/bbox.h',
  'src/buffer.c',
  'src/charpath.c',
  'src/charpath.h',
  'src/charpathpriv.c',
  'src/charprop.c',
  'src/check.c',
  'src/control.c',
  'src/eval.c',
  'src/fix.c',
  'src/flat.c',
  'src/fontinfo.c',
  'src/fontinfo.h',
  'src/gen.c',
  'src/head.c',
  'src/logging.c',
  'src/logging.h',
  'src/memory.c',
  'src/memory.h',
  'src/merge.c',
  'src/misc.c',
  'src/opcodes.h',
  'src/optable.c',
  'src/optable.h',
  'src/pick.c',
  'src/psautohint.c',
  'src/read.c',
  'src/report.c',
  'src/shuffle.c',
  'src/stemreport.c',
  'src/write.c',
  version_h,
  include_directories : include_directories(['include']),
  c_args : '-DAC_C_LIB_EXPORTS',
  dependencies : libm,
  install : true,
)

autohintexe = executable(
  'autohintexe',
  'autohintexe.c',
  include_directories : include_directories(['include']),
  link_with : libpsautohint,
  install : true,
)

pkg.generate(
  libraries : libpsautohint,
  version : meson.project_version(),
  name : meson.project_name(),
  filebase : meson.project_name(),
  description : 'A library for autohinting PostScript fonts',
)
