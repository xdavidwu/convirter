project('convirter', 'c')

libarchive = dependency('libarchive')
libguestfs = dependency('libguestfs')
json_c = dependency('json-c')

libconvirter_files = []
libconvirter_include = include_directories('include')

subdir('lib')

libconvirter = library(
  'convirter',
  libconvirter_files,
  include_directories: [libconvirter_include],
  dependencies: [libarchive, json_c]
)

subdir('src')
subdir('tests')
