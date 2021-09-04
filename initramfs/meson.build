sed = find_program('sed')

initramfs_data_path = get_option('datadir') / meson.project_name() / 'initramfs'

custom_target('c2v-mkboot',
  build_by_default: true,
  input: 'c2v-mkboot.sh',
  command: [sed, 's|%CONVIRTER_DATA%|' + get_option('prefix') / initramfs_data_path + '|', '@INPUT@'],
  output: 'c2v-mkboot',
  capture: true,
  install: true,
  install_mode: 'rwxr-xr-x',
  install_dir: get_option('bindir')
)

install_data(
  ['busybox', 'modprobe', 'setuidgid', 'init', 'udhcpc.script', 'files.list'],
  install_dir: initramfs_data_path
)
