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

"""
extract-edid.py

Script to extract EDID information from Xorg.<n>.log files
generated using the nVidia driver.

Replies on particular output formatting from the nvidia driver,
generated with "-logverbose 6" options to the X server.

"""

import re
import sys
from pprint import pprint

displayNumber = 0
try:
	fname = '/var/log/Xorg.%d.log'%(displayNumber)
	f = open(fname,'r')
except IOError, e:
	print >>sys.stderr, "Failed to open file '%s'. Reason : %s"%(fname, str(e))
	sys.exit(-1)

# Extract all file data
all_lines = f.readlines()
f.close()

#Detect the starting of the EDID
# Sample lines I've seen are
#(--) NVIDIA(0): --- EDID for LPL (DFP-0) ---
#
edid_header_re = re.compile("^\(--\)[\s]+NVIDIA\(([0-9]+)\):[\s]+---[\s]+EDID[\s]+for[\s]+(.*)[\s]+\(([A-Z]+)\-([0-9]+)\)[\s]+---[\s]+$")

#
# Property lines can be
#'(--) NVIDIA(0): 32-bit Serial Number         : 0' # Value = 0
#'(--) NVIDIA(0): Serial Number String         : '  # NOTE: no Value!
#
edid_prop_re = re.compile("^\(--\)[\s]+NVIDIA\(([0-9]+)\):[\s]+(.*)[\s]+:[\s]+((.*)[\s]+)?$")

# Properties end with
edid_end_prop_re = re.compile("^\(--\)[\s]+NVIDIA\(([0-9]+)\):[\s]+$")

# EDIDs have this 'Prefer first detailed timing' property.
# If set to 'Yes', then the mode following the below line is the default mode for the
# device
#(--) NVIDIA(0): Detailed Timings:
edid_detailed_timing_re = re.compile("^\(--\)[\s]+NVIDIA\(([0-9]+)\):[\s]+Detailed Timings:[\s]*$")

# Each supported mode is shown as
# '(--) NVIDIA(0):   1280 x 800  @ 60 Hz'
edid_supported_mode_re = re.compile("^\(--\)[\s]+NVIDIA\(([0-9]+)\):[\s]+([0-9]+)[\s]+x[\s]+([0-9]+)[\s]+@[\s]+([0-9\.]+)[\s]+Hz[\s]*$")

# Modes are followed by the Raw EDID bytes
#'(--) NVIDIA(0): Raw EDID bytes:'
edid_raw_edid_start_re = re.compile("^\(--\)[\s]+NVIDIA\(([0-9]+)\):[\s]+Raw EDID bytes:[\s]*$")

# EDID data bytes are in this format
#(--) NVIDIA(0):   00 4c 50 31 34 31 57 58  31 2d 54 4c 41 32 00 ae
edid_data_re = re.compile("^\(--\)[\s]+NVIDIA\(([0-9]+)\):[\s]+"+ ("([0-9a-f]{2})[\s]+"*16)+"[\s]*$")
edid_footer_re = re.compile("^\(--\)[\s]+NVIDIA\(([0-9]+)\):[\s]+---[\s]+End[\s]+of[\s]+EDID[\s]+for[\s]+(.*)[\s]+\(([A-Z]+)\-([0-9]+)\)[\s]+---[\s]+$")

gpus = {}
# Process line by line
lineNum = -1
maxLines = len(all_lines)
while lineNum < (maxLines-1):
	lineNum += 1
	thisLine = all_lines[lineNum]
	headerMatch = edid_header_re.match(thisLine)
	# Ignore lines till the beginning of an EDID header
	if headerMatch is None:
		continue

	thisDisplay = {}
	headerProps = headerMatch.groups()
	thisDisplay['GPU'] = headerProps[0]
	thisDisplay['display_device'] = headerProps[1]
	thisDisplay['port_index'] = headerProps[2]
	thisDisplay['output_type'] = headerProps[3]
	thisDisplay['edid_modes'] = []
	thisDisplay['edid_bytes'] = []

	# The header will be followed by a list of properties
	# that the nvidia driver decodes from the EDID
	# The end of this information is indicated with a line like
	# "(--) NVIDIA(0): "
	while lineNum < (maxLines-1):
		lineNum += 1
		thisLine = all_lines[lineNum]
		mob2 = edid_end_prop_re.match(thisLine)
		if mob2 is None:
			propMatch = edid_prop_re.match(thisLine)
			if propMatch is not None:
				#print propMatch.groups()
				propMatches = propMatch.groups()
				propName = propMatches[1].rstrip()
				thisDisplay[propName]=propMatches[3]
				
			else:
				raise ValueError, "Failed parsing line:'%s'"%(thisLine)
		else:
			break

	next_is_default = False
	# Next find all supported modes
	while lineNum < (maxLines-1):
		lineNum += 1
		thisLine = all_lines[lineNum]
		mob2 = edid_raw_edid_start_re.match(thisLine)
		if mob2 is None:
			# FIXME: we have to handle "preferred mode"s here!
			# this manifests as "prefer first detailed timing" on edids
			matchedMode = edid_supported_mode_re.match(thisLine)
			if matchedMode is not None:
				#print matchedMode.groups()
				matchedMode = matchedMode.groups()
				mode_width = int(matchedMode[1])
				mode_height = int(matchedMode[2])
				mode_refresh = int(matchedMode[3])
				thisDisplay['edid_modes'].append([mode_width, mode_height, mode_refresh])
				if next_is_default:
					thisDisplay['first_detailed_timing'] = [mode_width, mode_height, mode_refresh]
					next_is_default = False
			else:
				doesMatch = edid_detailed_timing_re.match(thisLine)
				if doesMatch is not None:
					next_is_default = True
		else:
			break

	# Next leach all EDID data bytes
	# till the end of EDID
	while lineNum < (maxLines-1):
		lineNum += 1
		thisLine = all_lines[lineNum]
		footerMatch = edid_footer_re.match(thisLine)
		if footerMatch is None:
			# Till we reach the footer, we may get data bytes
			edidData = edid_data_re.match(thisLine)
			if edidData is not None:
				#print edidData.groups()
				for edidByte in edidData.groups()[1:]:
					thisDisplay['edid_bytes'].append(edidByte)
		else:
			break
	hsr = thisDisplay['Valid HSync Range'].split('-')
	hsyncMin = hsr[0].lstrip().rstrip().split(' ')[0]
	hsyncMax = hsr[1].lstrip().rstrip().split(' ')[0]
	print hsyncMin, hsyncMax
	vsr = thisDisplay['Valid VRefresh Range'].split('-')
	vrefreshMin = vsr[0].lstrip().rstrip().split(' ')[0]
	vrefreshMax = vsr[1].lstrip().rstrip().split(' ')[0]
	print vrefreshMin, vrefreshMax

	pprint(thisDisplay)
