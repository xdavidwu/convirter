tar = find_program('tar')
grep = find_program('grep')
sh = find_program('sh')

id_like = run_command(sh, '-c', '. /etc/os-release && printf "%s" "$ID_LIKE"').stdout().split(' ')
id = run_command(sh, '-c', '. /etc/os-release && printf "%s" "$ID"').stdout()

if id == 'arch' or id_like.contains('arch')
  message('Using packages list for Arch Linux')
  packages_list = 'packages.archlinux'
elif id == 'debian' or id_like.contains('debian')
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
  command: [tar, 'zcf', '@OUTDIR@' / 'supermin.d' / 'init.tar.gz', '@INPUT@'],
  depends: [supermin_appliance])

full_appliance = custom_target('supermin-build',
  output: 'appliance.d',
  command: [supermin, '--build', '@OUTDIR@' / 'supermin.d', '-f', 'ext2', '--size', '512M', '-o', '@OUTPUT@'],
  env: {'LC_ALL': 'C'},
  build_by_default: true,
  depends: [supermin_appliance, init_tar])

appliance = '@BUILD_ROOT@' / 'appliance' / 'appliance.d'
