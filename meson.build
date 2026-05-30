# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause

project('zxc', 'c',
  version : run_command('python3', '-c',
    'import re; h=open("include/zxc_constants.h").read(); print(".".join(re.findall(r"ZXC_VERSION_(?:MAJOR|MINOR|PATCH)\s+(\d+)",h)))',
    check : true).stdout().strip(),
  license : 'BSD-3-Clause',
  default_options : ['c_std=c17', 'buildtype=release', 'warning_level=2'],
  meson_version : '>= 0.60.0',
)

cc = meson.get_compiler('c')
host_cpu = host_machine.cpu_family()

zxc_soversion = run_command('python3', '-c',
  'import re; print(re.search(r"set\\(ZXC_SOVERSION\\s+(\\d+)\\)", open("CMakeLists.txt").read()).group(1))',
  check : true).stdout().strip()

# =============================================================================
# Platform defines
# =============================================================================

if host_machine.system() == 'linux'
  add_project_arguments('-D_GNU_SOURCE', '-D_FILE_OFFSET_BITS=64',
                        '-D_LARGEFILE_SOURCE', language : 'c')
elif host_machine.system() == 'darwin'
  add_project_arguments('-D_GNU_SOURCE', language : 'c')
endif

# =============================================================================
# Include directories
# =============================================================================

inc_public    = include_directories('include')
inc_private   = include_directories('src/lib')
rapidhash_inc = include_directories('src/lib/vendors')
libzxc_includes = [inc_public, inc_private, rapidhash_inc]

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
  variant_sets += [['_sse2',   ['-msse2']]]
  variant_sets += [['_avx2',   ['-mavx2', '-mfma', '-mbmi', '-mbmi2', '-mlzcnt']]]
  variant_sets += [['_avx512', ['-mavx512f', '-mavx512bw', '-mbmi', '-mbmi2', '-mlzcnt']]]
elif host_cpu == 'aarch64'
  variant_sets += [['_neon', ['-march=armv8-a+simd']]]
elif host_cpu == 'arm'
  variant_sets += [['_neon', ['-march=armv7-a', '-mfpu=neon']]]
endif

variant_objects = []
foreach info : variant_sets
  foreach src : variant_sources
    obj = static_library(
      'zxc' + info[0] + '_' + src.underscorify(),
      src,
      c_args : info[1] + ['-DZXC_FUNCTION_SUFFIX=' + info[0]],
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
  'src/lib/zxc_driver.c',
  'src/lib/zxc_dispatch.c',
  'src/lib/zxc_pstream.c',
  'src/lib/zxc_seekable.c',
)

thread_dep = dependency('threads')

libzxc = library('zxc',
  libzxc_sources,
  objects : variant_objects,
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
)
meson.override_dependency('libzxc', libzxc_dep)

# Static link target for tests that access internal symbols (same pattern as
# CMake's zxc_lib_static and zstd's libzstd_internal_dep).
if get_option('default_library') == 'static'
  libzxc_static = libzxc
else
  libzxc_static = static_library('zxc_static',
    libzxc_sources,
    objects : variant_objects,
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
)

install_headers(
  'include/zxc.h',
  'include/zxc_buffer.h',
  'include/zxc_constants.h',
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

  executable('zxc',
    'src/cli/main.c',
    dependencies : [libzxc_dep, math_dep],
    install : true,
  )
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
      'tests/test_seekable.c',
      'tests/test_seekable_mt.c',
      'tests/test_format.c',
      'tests/test_misc.c',
    ),
    include_directories : libzxc_includes,
    link_with : libzxc_static,
    dependencies : [thread_dep],
  )
  test('zxc_test', test_exe)
endif
