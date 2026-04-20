apparmor_gen_profiles = [
  'usr.lib.libvirt.virt-aa-helper',
  'usr.sbin.virtqemud',
  'usr.sbin.virtxend',
]

if conf.has('WITH_MONOLITHIC_LIBVIRTD')
  apparmor_gen_profiles += ['usr.sbin.libvirtd']
endif

apparmor_gen_profiles_conf = configuration_data({
  'sysconfdir': sysconfdir,
  'sbindir': sbindir,
  'runstatedir': runstatedir,
  'libexecdir': libexecdir,
})

apparmor_dir = sysconfdir / 'apparmor.d'

foreach name : apparmor_gen_profiles
  configure_file(
    input: '@0@.in'.format(name),
    output: name,
    configuration: apparmor_gen_profiles_conf,
    install: true,
    install_dir: apparmor_dir,
  )
endforeach

install_data(
  [ 'libvirt-qemu', 'libvirt-lxc' ],
  install_dir: apparmor_dir / 'abstractions',
)

install_data(
  [ 'TEMPLATE.qemu', 'TEMPLATE.lxc' ],
  install_dir: apparmor_dir / 'libvirt',
)
