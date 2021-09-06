project('convirter', 'c')

libarchive = dependency('libarchive')
libgcrypt = dependency('libgcrypt')
libguestfs = dependency('libguestfs')
json_c = dependency('json-c')
supermin = find_program('supermin', required: get_option('build_appliance'))

libconvirter_files = []
libconvirter_include = include_directories('include')

subdir('lib')

libconvirter = library(
  'convirter',
  libconvirter_files,
  include_directories: [libconvirter_include],
  dependencies: [libarchive, libgcrypt, json_c]
)

if supermin.found()
  subdir('appliance')
endif

subdir('src')
subdir('tools')
subdir('initramfs')

subdir('tests')
