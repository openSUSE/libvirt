#!/bin/bash

repo='virt_repo'
pkgs='libvirt-daemon-qemu libvirt-daemon-xen libvirt-daemon-config-network libvirt-daemon-config-nwfilter libvirt-client'

zypper ref --repo $repo
zypper install --force --no-confirm --allow-vendor-change --allow-downgrade --repo $repo $pkgs

ret=$?
exit $ret

