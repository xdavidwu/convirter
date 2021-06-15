executable(
  'v2c',
  'v2c.c',
  dependencies: [libguestfs, libarchive],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)

executable(
  'c2v',
  'c2v.c',
  dependencies: [libguestfs, libarchive],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)
