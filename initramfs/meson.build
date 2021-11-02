sed = find_program('sed')

initramfs_data_path = get_option('datadir') / meson.project_name() / 'initramfs'

c2v_mkboot = custom_target('c2v-mkboot',
  build_by_default: true,
  input: 'c2v-mkboot.sh',
  command: [sed, 's|%CONVIRTER_DATA%|' + get_option('prefix') / initramfs_data_path + '|', '@INPUT@'],
  output: 'c2v-mkboot',
  capture: true,
  install: true,
  install_mode: 'rwxr-xr-x',
  install_dir: get_option('bindir')
)

data = ['init', 'udhcpc.script', 'files.list']

if get_option('build_initramfs_utils')
  utils_dir = meson.current_source_dir() / 'utils'

  custom_target('busybox',
    command: [utils_dir / 'busybox' / 'build.sh', utils_dir / 'busybox', '@OUTPUT@'],
    output: 'busybox',
    install: true,
    install_dir: initramfs_data_path
  )

  custom_target('modprobe',
    command: [utils_dir / 'kmod' / 'build.sh', utils_dir / 'kmod', '@OUTPUT@'],
    output: 'modprobe',
    install: true,
    install_dir: initramfs_data_path
  )

  custom_target('chdir-setuidgid',
    command: [utils_dir / 'c2v-initramfs-utils' / 'build.sh', utils_dir / 'setuidgid', '@OUTPUT@'],
    output: 'chdir-setuidgid',
    install: true,
    install_dir: initramfs_data_path
  )
else
  data += ['busybox', 'modprobe', 'chdir-setuidgid']
endif

install_data(
  data,
  install_dir: initramfs_data_path
)
