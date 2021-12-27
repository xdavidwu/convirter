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
