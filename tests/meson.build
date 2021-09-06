if get_option('e2e_tests')
  skopeo = find_program('skopeo')

  hello_world_oci = custom_target('skopeo-download-hello-world',
    output: 'hello-world.tar',
    command: [skopeo, 'copy', 'docker://hello-world@sha256:1b26826f602946860c279fce658f31050cff2c596583af237d971f4629b57792', 'oci-archive:@OUTPUT@'])

  test('c2v converts hello-world',
    c2v,
    args: [hello_world_oci, 'hello-world.qcow2'],
    timeout: 300)
endif

sh = find_program('sh')

test('c2v-mkboot',
  sh,
  args: [c2v_mkboot.full_path(), '-d', meson.source_root() / 'initramfs']
  )
