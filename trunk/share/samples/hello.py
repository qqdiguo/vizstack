#!/usr/bin/env python
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

# Import vsapi
import vsapi

# Connect to SSM
ra = vsapi.ResourceAccess()
alloc = ra.allocate([
           [vsapi.Server(), vsapi.GPU()]
        ])

# Find the Server & GPU allocated to us
# Allocated resources follow the input order
res = alloc.getResources()
srv = res[0][0]
gpu = res[0][1]

# Setup the server with a screen containing
# the GPU
scr = vsapi.Screen(0)
scr.setFBProperty('resolution', [1000,1000])
scr.setGPU(gpu)
srv.addScreen(scr)

# Propagate the server configuration to the
# SSM
alloc.setupViz(ra)

# Start the X server
alloc.startViz(ra)

# Run xwininfo on it & wait till it exits
proc = scr.run('xwininfo -root')
proc.wait()

# Stop the X server
alloc.stopViz(ra)

# Give up the resources
ra.deallocate(alloc)

# Disconnect from the SSM
ra.stop()

