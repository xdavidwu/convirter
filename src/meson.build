v2c = executable(
  'v2c',
  'v2c.c',
  '../common/guestfs.c',
  dependencies: [libguestfs, libarchive],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)

c2v = executable(
  'c2v',
  'c2v.c',
  dependencies: [libguestfs, libarchive],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)
