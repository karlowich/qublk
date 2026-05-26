project(
  'qublk',
  'c',
  version: '0.0.1',
  license: 'BSD-3-Clause',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
    'b_ndebug=if-release',
    'buildtype=release',
  ],
)

if host_machine.system() != 'linux'
  error('qublk only builds on Linux (needs ublk_drv and <linux/ublk_cmd.h>)')
endif

cc = meson.get_compiler('c')

dep_xnvme = dependency('xnvme', required: true)
dep_uring = dependency('liburing', version: '>=2.3', required: true)
dep_thread = dependency('threads')

if not cc.has_header('linux/ublk_cmd.h')
  error('linux/ublk_cmd.h not found; kernel headers >= 6.0 required')
endif

deps = [dep_xnvme, dep_uring, dep_thread]

subdir('src')
