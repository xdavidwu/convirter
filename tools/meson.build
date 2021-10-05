executable(
  'oci-archive-inspect',
  'oci-archive-inspect.c',
  dependencies: [libarchive],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)

executable(
  'convirter-tree',
  ['convirter-tree.c', '../common/guestfs.c'],
  dependencies: [libguestfs],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)

executable(
  'convirter-diff',
  ['convirter-diff.c', '../common/guestfs.c'],
  dependencies: [libguestfs],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)
