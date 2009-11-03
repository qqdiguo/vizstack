#!/bin/bash

# VizStack - A Framework to manage visualization resources

# Copyright (C) 2009-2010 Hewlett-Packard
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

VIZSTACK_VERSION=1.0
VIZRT_VERSION=1.0

if test -f /etc/SuSE-release ;
then
    DISTRO=suse
    RPM_PATH=/usr/src/packages
fi
if test -f /etc/redhat-release;
then
    DISTRO=redhat
    RPM_PATH=/usr/src/redhat
fi
if test -f /etc/debian_version;
then
    DISTRO=debian
    RPM_PATH=/usr/src/rpm
fi

# Build the target directory structure we need
# Trying to confirm to FHS 2.3
rm -Rf /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/share
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/share/doc
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/src
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/usr/X11R6/bin
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/etc/vizstack
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/etc/vizstack/templates
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/etc/vizstack/templates/displays
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/etc/vizstack/templates/gpus
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/etc/vizstack/templates/keyboard
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/etc/vizstack/templates/mouse
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/etc/profile.d
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/lib64/security
mkdir -p /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/man/man1

# NOTE : /var/run/vizstack is created in the SPEC file

# Copy scripts, python files, template, src files to the directory structure
#   Scripts
cp -r bin /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack

#   Admin scripts, SSM
cp -r sbin /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack

#   Python Modules
cp -r python /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack

# Sources
cp -r src/{*.c,*.cpp,*.hpp,*.py,SConstruct,*.txt} /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/src

# README
cp -r doc/README /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/share/doc
cp -r doc/VizStack-Documentation.pdf /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/share/doc
cp -r doc/README /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/
cp -r COPYING /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/

#   Template files, XML schema and Samples
cp -r share/samples /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/share
cp -r share/schema /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/share
cp -r share/templates /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/share

# gdm.conf template
mv /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/share/templates/gdm.conf.template /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/etc/vizstack/templates

#   Environment setup fileds
cp -r etc/profile.d /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/etc

# Build the source code
cd src
scons
cd -

# Copy the built binaries
cp src/vs-X /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/usr/X11R6/bin
cp src/vs-generate-xconfig /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/bin
cp src/vs-Xkill /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/bin
cp src/vs-GDMlauncher /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/bin
cp src/vs-aew /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/bin
cp src/vs-Xv /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/bin
cp src/pam_vizstack_rgs_setuser.so /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/lib64/security
cp src/vs-wait-x /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/bin

# Generate the man pages if asciidoc has been installed
if [ -f /usr/bin/a2x ]; then
    a2x -f manpage -d manpage doc/manpages/viz-avizovr.txt -D /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/man/man1
    a2x -f manpage -d manpage doc/manpages/viz-rgs.txt -D /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/man/man1
    a2x -f manpage -d manpage doc/manpages/viz-tvnc.txt -D /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/man/man1
    a2x -f manpage -d manpage doc/manpages/viz-vgl.txt -D /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/man/man1
    a2x -f manpage -d manpage doc/manpages/viz-paraview.txt -D /tmp/vizstack-tmp/vizstack-${VIZSTACK_VERSION}/opt/vizstack/man/man1
fi

# Vizconn stuff
rm -Rf /tmp/vizrt-tmp/vizrt-${VIZRT_VERSION}
mkdir -p /tmp/vizrt-tmp/vizrt-${VIZRT_VERSION}/opt/vizrt/bin

# Copy vizconn stuff to the staging dir
cp vizconn/remotevizconnector /tmp/vizrt-tmp/vizrt-${VIZRT_VERSION}/opt/vizrt/bin
cp vizconn/sshconnector.py /tmp/vizrt-tmp/vizrt-${VIZRT_VERSION}/opt/vizrt/bin

# Remove subversion information from the packaging tree
find /tmp/vizstack-tmp -type d -name ".svn" | xargs rm -rf
find /tmp/vizstack-tmp -type f -name "*~" | xargs rm -f
find /tmp/vizstack-tmp -type f -name ".scons*" | xargs rm -f

# Remove subversion information from the packaging tree
find /tmp/vizrt-tmp -type d -name ".svn" | xargs rm -rf
find /tmp/vizrt-tmp -type f -name "*~" | xargs rm -f

# Last steps to build the RPM
cp vizstack.spec ${RPM_PATH}/SPECS
pushd /tmp/vizstack-tmp
tar -zcvf vizstack-${VIZSTACK_VERSION}.tar.gz vizstack-${VIZSTACK_VERSION}
cp vizstack-${VIZSTACK_VERSION}.tar.gz ${RPM_PATH}/SOURCES
rpmbuild -ba ${RPM_PATH}/SPECS/vizstack.spec

popd

# Build the vizrt rpm also
cp vizrt.spec ${RPM_PATH}/SPECS
pushd /tmp/vizrt-tmp
tar -zcvf vizrt-${VIZRT_VERSION}.tar.gz vizrt-${VIZRT_VERSION}
cp vizrt-${VIZRT_VERSION}.tar.gz ${RPM_PATH}/SOURCES
rpmbuild -ba ${RPM_PATH}/SPECS/vizrt.spec

popd
