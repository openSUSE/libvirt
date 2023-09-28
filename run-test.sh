#!/bin/bash

hvisor="KVM"
daemon="virtqemud"
if [ -e /proc/xen/privcmd ]; then
    hvisor="XEN"
    daemon="virtxend"
fi

# Reset testing environment
#
# Restart daemons and wait 5 seconds for it to come to life
if systemctl --quiet is-enabled libvirtd; then
    systemctl restart libvirtd
    for count in `seq 0 5`; do
	if systemctl status libvirtd &>/dev/null; then
	    break
	fi
	if [ $count -eq 5 ]; then
	    echo "libvirtd is taking too long to restart, aborting.."
	    exit 1
	fi
	sleep 1
    done
fi

if systemctl --quiet is-enabled $daemon; then
    systemctl restart $daemon
    for count in `seq 0 5`; do
	if systemctl status $daemon &>/dev/null; then
	    break
	fi
	if [ $count -eq 5 ]; then
	    echo "$daemon is taking too long to restart, aborting.."
	    exit 1
	fi
	sleep 1
    done
fi

# Ensure default network is active
if [ -z "$(virsh net-list | grep default)" ]; then
    virsh net-start default
    if [ $? -ne 0 ]; then
	echo "Unable to start default network"
	exit 1
    fi
fi

# Cleanup any tck resources lying around from a previous run
if virsh list --all| grep tck &>/dev/null; then
    # Destroy any active domains
    TCK_VMS=$(virsh list | grep tck | awk '{print $2}')
    for VM in $TCK_VMS; do
	virsh destroy $VM
    done
    # Undefine any inactive domains
    TCK_VMS=$(virsh list --inactive| grep tck | awk '{print $2}')
    for VM in $TCK_VMS; do
	virsh undefine $VM
    done
fi
if virsh vol-list --pool tck 2>/dev/null| grep tck &>/dev/null; then
    # Delete any vols
    TCK_VOLS=$(virsh vol-list --pool tck | grep tck | awk '{print $1}')
    for VOL in $TCK_VOLS; do
	virsh vol-delete $VOL --pool tck
    done
fi
if virsh pool-list --all| grep tck &>/dev/null; then
    # Destroy any active pools
    TCK_POOLS=$(virsh pool-list | grep tck | awk '{print $1}')
    for POOL in $TCK_POOLS; do
	virsh pool-destroy $POOL
    done
    # Undefine any inactive pools
    TCK_POOLS=$(virsh pool-list --inactive| grep tck | awk '{print $1}')
    for POOL in $TCK_POOLS; do
	virsh pool-undefine $POOL
    done
fi
if virsh nwfilter-list | grep tck &>/dev/null; then
    for FILTER in tck-testcase nwfiltertestfilter tck-vm1-filter tck-vm2-filter; do
	virsh nwfilter-undefine $FILTER
    done
fi

# Run the test
libvirt_ver="$(rpm -q --queryformat "%{NAME} version %{VERSION}-%{RELEASE}" libvirt-libs)"
echo "Testing $libvirt_ver on $hvisor hypervisor"
/usr/bin/libvirt-tck

retval=$?
if [ $retval -eq 0 ]; then
    echo "Tests passed"
else
    echo "Tests failed"
fi

exit $retval
