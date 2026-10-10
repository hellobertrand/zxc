# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Bertrand Lebonnois and contributors.

project('zxc', 'c',
  version : '0.14.1',
  license : 'BSD-3-Clause',
  default_options : ['c_std=c17', 'buildtype=release', 'warning_level=2',
                     'pkgconfig.relocatable=true'],
  meson_version : '>= 0.63.0',
)

cc = meson.get_compiler('c')
fs = import('fs')
host_cpu = host_machine.cpu_family()

msvc_syntax = cc.get_argument_syntax() == 'msvc'
is_cl = cc.get_id() == 'msvc'

# As CMake: /wd4244 (lossless uint64 -> size_t narrowing), /wd4310 (deliberate
# constant truncation by a cast).
msvc_narrowing_args = msvc_syntax ? ['/wd4244', '/wd4310'] : []
# As CMake: /W4 for MSVC, top-level only (warning_level=3 is /W4 there).
msvc_w4 = msvc_syntax and not meson.is_subproject() ? ['warning_level=3'] : []

# As CMake's ZXC_WARNING_FLAGS; warning_level=2 gives -Wall -Wextra.
if not meson.is_subproject() and not msvc_syntax
  add_project_arguments(cc.get_supported_arguments([
    '-Wshadow', '-Wformat=2', '-Wundef', '-Wpointer-arith', '-Wvla', '-Wcast-qual',
    '-Wdouble-promotion', '-Wimplicit-fallthrough', '-Wmissing-prototypes',
    '-Wstrict-prototypes']), language : 'c')
endif

# As CMake: GCC on Win64 may spill AVX vectors with aligned moves (PR54412).
if cc.get_id() == 'gcc' and host_machine.system() == 'windows' and host_cpu == 'x86_64'
  uvm_args = cc.get_supported_arguments(['-Wa,-muse-unaligned-vector-move'])
  add_project_arguments(uvm_args, language : 'c')
  add_project_link_arguments(uvm_args, language : 'c')
endif

# As CMake's zxc_apply_lib_flags: conversions, on 64-bit targets only.
lib_warning_args = []
if not meson.is_subproject() and not msvc_syntax and cc.sizeof('void*') == 8 and not (
    cc.get_id() == 'gcc' and cc.version().version_compare('<10'))
  lib_warning_args = cc.get_supported_arguments(['-Wconversion', '-Wsign-conversion'])
endif

# Keep in sync with ZXC_SOVERSION in CMakeLists.txt (bumps only on an ABI break).
zxc_soversion = '5'

# =============================================================================
# Windows DLL export control
# =============================================================================

building_static = get_option('default_library') == 'static'
lib_export_args = building_static ? ['-DZXC_STATIC_DEFINE'] : ['-Dzxc_lib_EXPORTS']

if building_static
  lib_consumer_args = ['-DZXC_STATIC_DEFINE']
elif get_option('default_library') == 'shared'
  lib_consumer_args = ['-DZXC_DLL_IMPORT']
else
  # default_library=both: the consumer picks static or shared per dependency, so
  # neither define is knowably right. Staying silent links against either.
  lib_consumer_args = []
endif

# =============================================================================
# Platform defines
# =============================================================================

if host_machine.system() == 'linux'
  add_project_arguments('-D_POSIX_C_SOURCE=200809L', '-D_XOPEN_SOURCE=700',
                        '-D_FILE_OFFSET_BITS=64',
                        '-D_LARGEFILE_SOURCE', language : 'c')
elif host_machine.system() == 'darwin'
  add_project_arguments('-D_GNU_SOURCE', language : 'c')
endif

# =============================================================================
# Include directories
# =============================================================================

inc_public    = include_directories('include')
inc_private   = include_directories('src/lib')

# Rapidhash: system or vendored, mirroring ZXC_USE_SYSTEM_RAPIDHASH in CMake.
# Resolved at setup time so a missing header names the option to set.
if get_option('use_system_rapidhash')
  if not cc.has_header('rapidhash.h', args : get_option('c_args'))
    error('use_system_rapidhash is enabled but rapidhash.h was not found. ' +
          'Install rapidhash or add its include directory to c_args.')
  endif
  rapidhash_inc = []
else
  if not fs.exists('src/lib/vendors/rapidhash.h')
    error('Vendored rapidhash.h not found in src/lib/vendors. If the vendored ' +
          'copy was removed on purpose (packaging against a system rapidhash), ' +
          'configure with -Duse_system_rapidhash=true.')
  endif
  rapidhash_inc = [include_directories('src/lib/vendors')]
endif

libzxc_includes = [inc_public, inc_private] + rapidhash_inc

# =============================================================================
# SIMD runtime dispatch
# =============================================================================
# Compiles compress/decompress/huffman per-ISA so the runtime dispatcher can
# route to the fastest implementation available at startup.

variant_sources = ['src/lib/zxc_compress.c',
                   'src/lib/zxc_decompress.c',
                   'src/lib/zxc_huffman.c']

variant_sets = [['_default', []]]

if host_cpu == 'x86_64'
  # No _sse2 variant (SSE2 = x86-64 baseline, already active in _default).
  if is_cl
    variant_sets += [['_avx2',   ['/arch:AVX2', '/D__BMI__', '/D__BMI2__', '/D__LZCNT__']]]
    variant_sets += [['_avx512', ['/arch:AVX512', '/D__BMI__', '/D__BMI2__', '/D__LZCNT__']]]
  else
    variant_sets += [['_avx2',   ['-mavx2', '-mbmi', '-mbmi2', '-mlzcnt']]]
    variant_sets += [['_avx512', ['-mavx512f', '-mavx512bw', '-mavx512vbmi', '-mavx512vbmi2', '-mbmi', '-mbmi2', '-mlzcnt']]]
  endif
elif host_cpu == 'arm'
  # 32-bit ARM only: NEON is optional there (runtime getauxval dispatch).
  # AArch64 needs no extra variant: NEON is baseline and lives in _default.
  variant_sets += [['_neon32', ['-march=armv7-a', '-mfpu=neon']]]
endif

variant_objects = []
foreach info : variant_sets
  foreach src : variant_sources
    obj = static_library(
      'zxc' + info[0] + '_' + src.underscorify(),
      src,
      override_options : msvc_w4,
      c_args : info[1] + msvc_narrowing_args + lib_warning_args + ['-DZXC_FUNCTION_SUFFIX=' + info[0]] + lib_export_args,
      include_directories : libzxc_includes,
      pic : true,
    )
    variant_objects += obj.extract_all_objects(recursive : false)
  endforeach
endforeach

# =============================================================================
# Core library
# =============================================================================

libzxc_sources = files(
  'src/lib/zxc_common.c',
  'src/lib/zxc_pivco_tables.c',
  'src/lib/zxc_dict.c',
  'src/lib/zxc_driver.c',
  'src/lib/zxc_dispatch.c',
  'src/lib/zxc_pstream.c',
  'src/lib/zxc_seekable.c',
)

thread_dep = dependency('threads')

libzxc = library('zxc',
  libzxc_sources,
  name_prefix : 'lib',
  override_options : msvc_w4,
  objects : variant_objects,
  c_args : lib_export_args + msvc_narrowing_args + lib_warning_args,
  gnu_symbol_visibility : 'hidden',
  include_directories : libzxc_includes,
  dependencies : [thread_dep],
  install : true,
  version : meson.project_version(),
  soversion : zxc_soversion,
)

libzxc_dep = declare_dependency(
  link_with : libzxc,
  include_directories : inc_public,
  compile_args : lib_consumer_args,
)
meson.override_dependency('libzxc', libzxc_dep)

# Static link target for tests that access internal symbols.
if get_option('default_library') == 'static'
  libzxc_static = libzxc
else
  libzxc_static = static_library('zxc_static',
    libzxc_sources,
    override_options : msvc_w4,
    objects : variant_objects,
    c_args : ['-DZXC_STATIC_DEFINE'] + msvc_narrowing_args + lib_warning_args,
    include_directories : libzxc_includes,
    dependencies : [thread_dep],
    build_by_default : false,
  )
endif

# =============================================================================
# pkg-config & headers
# =============================================================================

pkgconfig = import('pkgconfig')
pkgconfig.generate(libzxc,
  name : 'libzxc',
  filebase : 'libzxc',
  description : 'High-performance asymmetric lossless compression',
  version : meson.project_version(),
  url : 'https://github.com/hellobertrand/zxc',
  extra_cflags : lib_consumer_args,
)

install_headers(
  'include/zxc.h',
  'include/zxc_buffer.h',
  'include/zxc_constants.h',
  'include/zxc_dict.h',
  'include/zxc_error.h',
  'include/zxc_export.h',
  'include/zxc_opts.h',
  'include/zxc_pstream.h',
  'include/zxc_seekable.h',
  'include/zxc_stream.h',
)

# =============================================================================
# CLI (standalone only, off by default)
# =============================================================================

if not meson.is_subproject() and get_option('build_cli')
  math_dep = cc.find_library('m', required : false)

  cli_c_args = msvc_narrowing_args + (is_cl ? ['-D_CRT_SECURE_NO_WARNINGS'] : [])
  cli_link_args = msvc_syntax ? ['setargv.obj'] : []

  executable('zxc',
    'src/cli/main.c',
    override_options : msvc_w4,
    c_args : cli_c_args,
    link_args : cli_link_args,
    dependencies : [libzxc_dep, math_dep],
    install : true,
  )

  # "unzxc" (zxc -d) and "zxccat" (zxc -dc): symlinks to zxc and to its man
  # page, as in CMake. Symbolic links and man pages are POSIX-only.
  if host_machine.system() != 'windows'
    install_man('src/cli/zxc.1')
    foreach alias : ['unzxc', 'zxccat']
      install_symlink(alias,
        pointing_to : 'zxc',
        install_dir : get_option('bindir'),
      )
      install_symlink(alias + '.1',
        pointing_to : 'zxc.1',
        install_dir : get_option('mandir') / 'man1',
        install_tag : 'man',
      )
    endforeach
  endif
endif

# =============================================================================
# Tests (standalone only)
# =============================================================================

if not meson.is_subproject()
  test_exe = executable('zxc_test',
    files(
      'tests/test_main.c',
      'tests/test_common.c',
      'tests/test_buffer_api.c',
      'tests/test_block_api.c',
      'tests/test_context_api.c',
      'tests/test_static_ctx.c',
      'tests/test_pstream_api.c',
      'tests/test_stream_api.c',
      'tests/test_container.c',
      'tests/test_seekable.c',
      'tests/test_seekable_mt.c',
      'tests/test_format.c',
      'tests/test_encoder_pin.c',
      'tests/test_misc.c',
      'tests/test_dict.c',
    ),
    override_options : msvc_w4,
    c_args : ['-DZXC_STATIC_DEFINE'] + msvc_narrowing_args +
             (is_cl ? ['-D_CRT_SECURE_NO_WARNINGS'] : []),
    include_directories : libzxc_includes,
    link_with : libzxc_static,
    dependencies : [thread_dep],
  )
  test('zxc_test', test_exe)
endif
