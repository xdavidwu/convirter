supermin = find_program('supermin')
tar = find_program('tar')
grep = find_program('grep')
sh = find_program('sh')

id_like = run_command(sh, '-c', '. /etc/os-release && printf "%s" "$ID_LIKE"').stdout()
id = run_command(sh, '-c', '. /etc/os-release && printf "%s" "$ID"').stdout()

if id == 'arch' or id_like == 'arch'
  message('Using packages list for Arch Linux')
  packages_list = 'packages.archlinux'
elif id == 'debian' or id_like == 'debian'
  message('Using packages list for Debian')
  packages_list = 'packages.debian'
else
  warning('Using generic packages list for appliance')
  packages_list = 'packages.generic'
endif

packages = run_command(grep, '-v', '^#', '--', packages_list).stdout().strip().split('\n')
# custom_target directory itself as output seems to work
supermin_appliance = custom_target('supermin-prepare',
  output: 'supermin.d',
  command: [supermin, '--prepare', packages, '-o', '@OUTPUT@'],
  env: {'LC_ALL': 'C'})

# custom_target does not work with output under directories
init_tar = custom_target('gen-init-tar',
  input: ['init', 'udhcpc.script'],
  output: 'init.tar.gz',
  command: [tar, 'zcf', '@OUTPUT@', '@INPUT@'])

cp = find_program('cp')
# meson does not check output after command
copy_init_tar = custom_target('copy-init-tar',
  output: 'dummy',
  command: [cp, '@OUTDIR@' / 'init.tar.gz', '@OUTDIR@' / 'supermin.d' / 'init.tar.gz'],
  build_by_default: true,
  depends: [supermin_appliance, init_tar])

full_appliance = custom_target('supermin-build',
  output: 'appliance.d',
  command: [supermin, '--build', '@OUTDIR@' / 'supermin.d', '-f', 'ext2', '-o', '@OUTPUT@'],
  env: {'LC_ALL': 'C'},
  build_by_default: true,
  depends: copy_init_tar)

qemu = find_program('qemu-system-x86_64')
appliance = '@BUILD_ROOT@' / 'appliance.d'
run_target('test-appliance',
  command: [qemu, '-enable-kvm', '-nographic', '-nodefaults', '-kernel', appliance / 'kernel', '-initrd', appliance / 'initrd', '-hda', appliance / 'root', '-serial', 'stdio', '-append', 'console=ttyS0 root=/dev/sda panic=-1', '-netdev', 'user,id=user.0', '-device', 'e1000,netdev=user.0', '-no-reboot' ],
  depends: full_appliance)
