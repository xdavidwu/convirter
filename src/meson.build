v2c = executable(
  'v2c',
  'v2c.c',
  '../common/guestfs.c',
  '../common/common-config.c',
  dependencies: [libguestfs, libarchive],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)

v2c_mkfindlayerfilter = executable(
  'v2c-mkfindlayerfilter',
  'v2c-mkfindlayerfilter.c',
  dependencies: [libgcrypt, libm],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)

v2c_findlayer = executable(
  'v2c-findlayer',
  'v2c-findlayer.c',
  '../common/guestfs.c',
  dependencies: [libgcrypt, libguestfs],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)

c2v = executable(
  'c2v',
  'c2v.c',
  '../common/guestfs.c',
  '../common/common-config.c',
  dependencies: [libguestfs, libarchive],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)
