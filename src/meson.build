v2c = executable(
  'v2c',
  'v2c.c',
  '../common/guestfs.c',
  '../common/common-config.c',
  dependencies: [libguestfs, libarchive],
  link_with: [libconvirter],
  include_directories: [libconvirter_include],
  install: true
)

v2c_mkfilter = executable(
  'v2c-mkfilter',
  'v2c-mkfilter.c',
  dependencies: [libgcrypt, libm],
  link_with: [libconvirter],
  include_directories: [libconvirter_include],
  install: true
)

install_data(
  'v2c-mkdb.sh',
  rename: ['v2c-mkdb'],
  install_mode: 'rwxr-xr-x',
  install_dir: get_option('bindir')
)

v2c_findcontainer = executable(
  'v2c-findcontainer',
  'v2c-findcontainer.c',
  '../common/guestfs.c',
  dependencies: [libgcrypt, libguestfs],
  link_with: [libconvirter],
  include_directories: [libconvirter_include],
  install: true
)

c2v = executable(
  'c2v',
  'c2v.c',
  '../common/guestfs.c',
  '../common/common-config.c',
  dependencies: [libguestfs, libarchive],
  link_with: [libconvirter],
  include_directories: [libconvirter_include],
  install: true
)
