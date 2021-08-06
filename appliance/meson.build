tar = find_program('tar')
grep = find_program('grep')
sh = find_program('sh')
sed = find_program('sed')
cp = find_program('cp')

id_like = run_command(sh, '-c', '. /etc/os-release && printf "%s" "$ID_LIKE"').stdout().split(' ')
id = run_command(sh, '-c', '. /etc/os-release && printf "%s" "$ID"').stdout()

if id == 'arch' or id_like.contains('arch')
  message('Using packages list for Arch Linux')
  packages_list = 'packages.archlinux'
  busybox_path = '/bin/busybox'
  image_size = '512M'
elif id == 'debian' or id_like.contains('debian')
  message('Using packages list for Debian')
  packages_list = 'packages.debian'
  busybox_path = '/bin/busybox'
  image_size = '128M'
elif id == 'fedora' or id_like.contains('fedora')
  message('Using packages list for Fedora')
  packages_list = 'packages.fedora'
  busybox_path = '/sbin/busybox'
  image_size = '128M'
else
  warning('Using generic packages list for appliance')
  packages_list = 'packages.generic'
  busybox_path = '/bin/busybox'
  image_size = '128M'
endif

packages = run_command(grep, '-v', '^#', '--', packages_list).stdout().strip().split('\n')
# custom_target directory itself as output seems to work
supermin_appliance = custom_target('supermin-prepare',
  output: 'supermin.d',
  command: [supermin, '--prepare', packages, '-o', '@OUTPUT@'],
  env: {'LC_ALL': 'C'})

init = custom_target('generate-init',
  input: 'init.in',
  output: 'init',
  command: [sed, 's|@BUSYBOX_PATH@|' + busybox_path + '|g', '@INPUT@'],
  capture: true)

udhcpc_script = custom_target('generate-udhcpc.script',
  input: 'udhcpc.script.in',
  output: 'udhcpc.script',
  command: [sed, 's|@BUSYBOX_PATH@|' + busybox_path + '|g', '@INPUT@'],
  capture: true)

setuidgid = custom_target('copy-setuidgid',
  input: 'setuidgid',
  output: 'setuidgid',
  command: [cp, '@INPUT@', '@OUTPUT@'])

# custom_target does not work with output under directories
init_tar = custom_target('gen-init-tar',
  input: [init, udhcpc_script, setuidgid],
  output: 'init.tar.gz',
  command: [
    tar,
    '-zcf', '@OUTDIR@' / 'supermin.d' / 'init.tar.gz',
    '-C', '@OUTDIR@',
    '--numeric-owner', '--owner=0', '--group=0', '--mode=500',
    'init', 'udhcpc.script', 'setuidgid'],
  depends: [supermin_appliance])

full_appliance = custom_target('supermin-build',
  output: 'appliance.d',
  command: [supermin, '--build', '@OUTDIR@' / 'supermin.d', '-f', 'ext2', '--size', image_size, '--copy-kernel', '-o', '@OUTPUT@'],
  env: {'LC_ALL': 'C'},
  build_by_default: true,
  depends: [supermin_appliance, init_tar])

appliance = '@BUILD_ROOT@' / 'appliance' / 'appliance.d'
