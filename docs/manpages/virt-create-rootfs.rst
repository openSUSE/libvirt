==================
virt-create-rootfs
==================

---------------------------------------------------------
A tool to create a root file system for distro containers
---------------------------------------------------------

:Manual section: 1
:Manual group: Virtualization Support

.. contents::

SYNOPSIS
========


``virt-create-rootfs`` [*OPTION*]


DESCRIPTION
===========

The ``virt-create-rootfs`` program is a shell script setting up a root file
system for a distribution container.

The basic structure of most virt-create-rootfs usage is:

  ``virt-create-rootfs`` -r /path/to/root -d distro-name


OPTIONS
=======

``-h``, ``--help``

Display command line help usage then exit.

``-r``, ``--root``

Set the path where to create the new root file system.

``-d``, ``--distro``

Set the name of distribution to use for the root file system.

As of now, only SLED-<XXX>, SLES-<XXX> and openSUSE-<XXX> are implemented
where <XXX> is the version number. Examples are openSUSE-15.3, openSUSE-tumbleweed,
and SLES-15.3. Note that SUSEConnect is required to handle SLE distributions.

``-a``, ``--arch``

Set the target architecture of the root file system to either i586 or x86_64.

``-c``, ``--regcode``

Set the registration code for the product to install in the root file system.
For SLE distributions, use a registration code from SUSE Customer Center.

``-u``, ``--url``

For SLE distributions, set the registration server to use.
Default: https://scc.suse.com.

``--dry-run``

Don't do anything, just report what would be done.


COPYRIGHT
=========

Copyright (C) 2014 SUSE LINUX Products GmbH, Nuernberg, Germany.


LICENSE
=======

``virt-create-rootfs`` is distributed under the terms of the GNU LGPL v2+.
This is free software; see the source for copying conditions. There
is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE


SEE ALSO
========

virsh(1), `https://libvirt.org/ <https://libvirt.org/>`_
