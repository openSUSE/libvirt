#!/bin/bash

if [ $# -lt 2 ]; then
  echo "Usage: $0 <repo-name> <repo-url>"
  exit 1
fi

repo=$1
repo_url=$2
pkgs='libvirt-daemon-qemu libvirt-daemon-xen libvirt-daemon-config-network libvirt-daemon-config-nwfilter libvirt-client'

# Ensure repo exists and is enabled
if ! zypper repos | grep -q $repo; then
    if ! zypper addrepo --no-gpgcheck --refresh $repo_url $repo; then
	exit 1
    fi
fi

if ! zypper repos --show-enabled-only | grep -q $repo; then
    if ! zypper modifyrepo --enable $repo; then
	exit 1
    fi
fi

zypper ref --repo $repo
zypper install --force --no-confirm --allow-vendor-change --allow-downgrade --repo $repo $pkgs
ret=$?

# Disable repo after package installation
zypper modifyrepo --disable $repo
exit $ret
