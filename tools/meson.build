executable(
  'oci-archive-inspect',
  'oci-archive-inspect.c',
  dependencies: [libarchive],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)

executable(
  'convirter-tree',
  'convirter-tree.c',
  dependencies: [libguestfs],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)

executable(
  'convirter-tree-oci',
  'convirter-tree-oci.c',
  dependencies: [libguestfs],
  link_with: [libconvirter],
  include_directories: [libconvirter_include]
)
