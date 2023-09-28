#!/bin/bash

repo='virt_staging_repo'
pkgs='libvirt-daemon-qemu libvirt-daemon-xen libvirt-daemon-config-network libvirt-daemon-config-nwfilter libvirt-client'

if ! zypper repos --show-enabled-only | grep -q $repo; then
    if ! zypper mr -e $repo; then
	exit 1
    fi
fi

zypper ref --repo $repo
zypper install --force --no-confirm --allow-vendor-change --allow-downgrade --repo $repo $pkgs

ret=$?
exit $ret

