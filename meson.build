project('convirter', 'c')

cc = meson.get_compiler('c')

libarchive = dependency('libarchive')
libgcrypt = dependency('libgcrypt')
libguestfs = dependency('libguestfs')
json_c = dependency('json-c')
libm = cc.find_library('m', required: false)

libconvirter_files = []
libconvirter_include = include_directories('include')

subdir('lib')

libconvirter = library(
  'convirter',
  libconvirter_files,
  include_directories: [libconvirter_include],
  dependencies: [libarchive, libgcrypt, json_c, libguestfs]
)

if get_option('build_appliance')
  subdir('appliance')
endif

subdir('src')
subdir('tools')
subdir('initramfs')

subdir('tests')
