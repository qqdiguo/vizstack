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


DEMO_VERSION=1.0

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
rm -Rf /tmp/vizstack-opensg-demos-tmp/vizstack-opensg-demos-${DEMO_VERSION}
mkdir -p /tmp/vizstack-opensg-demos-tmp/vizstack-opensg-demos-${DEMO_VERSION}/opt/vizstack/share/demos

# Copy OpenSG demos
cp -r OpenSG /tmp/vizstack-opensg-demos-tmp/vizstack-opensg-demos-${DEMO_VERSION}/opt/vizstack/share/demos

# Build the source code, resulting in a populated bin directory
make -C /tmp/vizstack-opensg-demos-tmp/vizstack-opensg-demos-${DEMO_VERSION}/opt/vizstack/share/demos/OpenSG/src

# Remove subversion information from the packaging tree
find /tmp/vizstack-opensg-demos-tmp -type d -name ".svn" | xargs rm -rf

# Last steps to build the RPM
cp vizstack-opensg-demos.spec ${RPM_PATH}/SPECS
cd /tmp/vizstack-opensg-demos-tmp
tar -zcvf vizstack-opensg-demos-${DEMO_VERSION}.tar.gz vizstack-opensg-demos-${DEMO_VERSION}
cp vizstack-opensg-demos-${DEMO_VERSION}.tar.gz ${RPM_PATH}/SOURCES
rpmbuild -ba ${RPM_PATH}/SPECS/vizstack-opensg-demos.spec
