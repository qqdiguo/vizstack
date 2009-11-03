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

from vsapi import extractObjects, Server, VizError
from pprint import pprint
import vsapi
import os
import time
import subprocess
import re
import sys
from glob import glob
from xml.dom import minidom
import xml
import domutil
import copy
import metascheduler

def isFrameLockAvailable(resList):
	"""
	Checks if all GPUs in input list are connected to FrameLock devices or not.
	Return True if yes, False if not.

	NOTE: This cannot detect that GPUs are not connected to the same framelock chain.
	This cannot detect other case like mixing framelock devices.
	"""
	flChain = __getFrameLockChain(resList)
	# 0. Ensure that all GPUs are connected to framelock devices
	nonFrameLockGPUs = 0
	for member in flChain:
		gpuDetails = member['gpu_details']
		if gpuDetails['FrameLockDeviceConnected']!=True:
			nonFrameLockGPUs += 1
	if nonFrameLockGPUs>0:
		return False
	return True

def disableFrameLock(resList):
	"""
	Disables Frame Lock. The list of X servers to enable frame-lock on is extracted from the input list.
	The input list could contain Servers or VizResourceAggregate objects.
	"""
	flChain = __getFrameLockChain(resList)
	# 0. Ensure that all GPUs are connected to framelock devices
	nonFrameLockGPUs = 0
	for member in flChain:
		gpuDetails = member['gpu_details']
		if gpuDetails['FrameLockDeviceConnected']!=True:
			nonFrameLockGPUs += 1
	if nonFrameLockGPUs>0:
		raise VizError(VizError.BAD_CONFIGURATION, "%d GPUs out of %d GPUs are not connected to the frame lock device. Frame lock requires all GPUs to be connected to G-Sync cards. Perhaps you passed a wrong list?"%(len(flChain)-enableCount, len(flChain)))
	
	# 1. Ensure that framelock is already active on all GPUs
	enableCount = 0
	for member in flChain:
		gpuDetails = member['gpu_details']
		if gpuDetails['FrameLockEnable']==True:
			enableCount += 1
	if enableCount!=len(flChain):
		raise VizError(VizError.BAD_CONFIGURATION, "Frame lock is not enabled in %d GPUs out of %d GPUs. Probably you have passed a wrong list?"%(len(flChain)-enableCount, len(flChain)))

	# 2. Ensure that all displays are running at the same refresh rate!
	masterRefreshRate = None
	badRRList = []
	numBadPorts = 0
	totalPorts = 0
	for member in flChain:
		gpuDetails = member['gpu_details']
		for portIndex in gpuDetails['ports']:
			totalPorts += 1
			thisRR = gpuDetails['ports'][portIndex]['RefreshRate']
			if masterRefreshRate is None:
				masterRefreshRate = thisRR
			elif thisRR != masterRefreshRate:
				numBadPorts += 1
				if thisRR not in badRRList:
					badRRList.append(thisRR)
	if numBadPorts>0:
		raise VizError(VizError.BAD_CONFIGURATION, "Frame lock master refresh rate is %s Hz. %d output ports have a different refresh rate %s Hz. Perhaps you have included two or more framelock chains in the input?"%(masterRefreshRate, numBadPorts, badRRList))

	# 3. Disable frame lock on all GPUs
	for member in flChain:
		server = member['server']
		screen = member['screen']
		gpu_index = member['gpu_index']
		__set_nvidia_settings(server, '[gpu:%d]/FrameLockEnable'%(gpu_index), '0')

	# 4. Reset master/slave too...
	for member in flChain:
		server = member['server']
		screen = member['screen']
		gpu_index = member['gpu_index']
		__set_nvidia_settings(server, '[gpu:%d]/FrameLockMaster'%(gpu_index), '0x00000000')
		__set_nvidia_settings(server, '[gpu:%d]/FrameLockSlaves'%(gpu_index), '0x00000000')

	# Next check if framelock actually got disabled.
	# Check FrameLockSyncRate on all GPUs.
	flChain = __getFrameLockChain(resList)

	masterFLSR = None
	badFLSRList = []
	numBadGPUs = 0
	for member in flChain:
		gpuDetails = member['gpu_details']
		thisFLSR = gpuDetails['FrameLockSyncRate']
		if masterFLSR is None:
			masterFLSR = thisFLSR
		elif thisFLSR != masterFLSR:
			numBadGPUs += 1
			if thisFLSR not in badFLSR:
				badFLSR.append(thisFLSR)

	if (masterFLSR!='0') or (numBadGPUs>0):	
		raise VizError(VizError.BAD_CONFIGURATION, "Unable to disable lock due to unknown reasons.")
	#pprint(flChain)
	return "Disabled Frame Lock @ %s Hz on %d GPUs connected to %d display devices."%(masterRefreshRate, len(flChain), totalPorts)

def enableFrameLock(resList):
	"""
	Enable Frame Lock. The list of X servers to enable frame-lock on is extracted from the input list. Frame Lock should not be in an enabled state on any of the servers.
	The input list may contain Servers or VizResourceAggregate objects.
	"""

	flChain = __getFrameLockChain(resList)

	# Next do sanity checks

	# 0. Ensure that all GPUs are connected to framelock devices
	nonFrameLockGPUs = 0
	for member in flChain:
		gpuDetails = member['gpu_details']
		if gpuDetails['FrameLockDeviceConnected']!=True:
			nonFrameLockGPUs += 1
	if nonFrameLockGPUs>0:
		raise VizError(VizError.BAD_CONFIGURATION, "%d GPUs out of %d GPUs are not connected to the frame lock device. Frame lock requires all GPUs to be connected to G-Sync cards."%(len(flChain)-enableCount, len(flChain)))

	# 1. Ensure that framelock is not already active
	enableCount = 0
	for member in flChain:
		gpuDetails = member['gpu_details']
		if gpuDetails['FrameLockEnable']==True:
			enableCount += 1
	if enableCount>0:
		raise VizError(VizError.BAD_CONFIGURATION, "Frame lock is enabled in %d GPUs out of %d GPUs. You need to disable framelock on these before trying to enable framelock."%(enableCount, len(flChain)))

	# 2. Ensure that all displays are running at the same refresh rate!
	masterRefreshRate = None
	badRRList = []
	numBadPorts = 0
	totalPorts = 0
	for member in flChain:
		gpuDetails = member['gpu_details']
		for portIndex in gpuDetails['ports']:
			totalPorts += 1
			thisRR = gpuDetails['ports'][portIndex]['RefreshRate']
			if masterRefreshRate is None:
				masterRefreshRate = thisRR
			elif thisRR != masterRefreshRate:
				numBadPorts += 1
				if thisRR not in badRRList:
					badRRList.append(thisRR)
	if numBadPorts>0:
		raise VizError(VizError.BAD_CONFIGURATION, "Frame lock master refresh rate is %s Hz. %d output ports have a different refresh rate %s Hz. Framelock requires all display outputs to run at the same refresh rate."%(masterRefreshRate, numBadPorts, badRRList))


	# 3. We need to ensure that the first GPU is "master-able"???

	# 4. Ensure that all X servers have the same stereo setting
	# nVidia documentation mentions the following limitation --
	#
	# "All X Screens (driving the selected client/server display devices) must
	# have the same stereo setting. See Appendix B for instructions on how to
	# set the stereo X option."
	#
	stereoModesInUse = []
	for member in flChain:
		srvScreen = member['screen']
		try:
			scrStereoMode = srvScreen.getFBProperty('stereo')
		except:
			scrStereoMode = None
		if scrStereoMode not in stereoModesInUse:
			stereoModesInUse.append(scrStereoMode)

	if len(stereoModesInUse)>1:
		raise VizError(VizError.BAD_CONFIGURATION, "All X servers need to be running with the same stereo setting if you want to enable framelock on them.")
	
	
	# All sanity checks done. So time to enable frame lock...
	# 1. Setup the first available display on first GPU as master. If there are another other displays on the first GPU, set them up as slaves
	# 2. Setup all displays on all other other GPUs as slave
	# 3. Enable frame lock on all GPUs
	# 4. Toggle test signal on master

	# 1. Setup the first available display on first GPU as master. If there are another other displays on the first GPU, set them up as slaves
	masterServer = flChain[0]['server']
	masterScreen = flChain[0]['screen']
	masterGPUIndex = flChain[0]['gpu_index']
	masterGPUDetails = flChain[0]['gpu_details']
	isMaster = True
	for portIndex in gpuDetails['ports']:
		portMask = __encodeDisplay(gpuDetails['ports'][portIndex]['type'], portIndex)
		if isMaster:
			__set_nvidia_settings(masterServer, '[gpu:%d]/FrameLockMaster'%(masterGPUIndex), '0x%08x'%(portMask))
		else:
			__set_nvidia_settings(masterServer, '[gpu:%d]/FrameLockSlaves'%(masterGPUIndex), '0x%08x'%(portMask))
		isMaster = False

	# 2. Setup all displays on all other other GPUs as slave
	for member in flChain[1:]:
		slaveServer = member['server']
		slaveScreen = member['screen']
		slaveGPUIndex = member['gpu_index']
		slaveGPUDetails = member['gpu_details']
		portMask = 0
		for portIndex in slaveGPUDetails['ports']:
			portMask = portMask | __encodeDisplay(slaveGPUDetails['ports'][portIndex]['type'], portIndex)
		__set_nvidia_settings(slaveServer, '[gpu:%d]/FrameLockSlaves'%(slaveGPUIndex), '0x%08x'%(portMask))
		__set_nvidia_settings(slaveServer, '[gpu:%d]/FrameLockMaster'%(slaveGPUIndex), '0x00000000')
	
	# 3. Enable frame lock on all GPUs
	for member in flChain:
		server = member['server']
		screen = member['screen']
		gpu_index = member['gpu_index']
		__set_nvidia_settings(server, '[gpu:%d]/FrameLockEnable'%(gpu_index), '1')

	# 4. Toggle test signal on master

	# nvidia-settings needs a window manager running to toggle the test 
	# signal. So we start one...
	# FIXME: current experimentation shows this may not be necessary ??
	# We suppress all output. If running the window manager fails then a window manager is already running &
	# there will be no problems.
	sched = masterServer.getSchedulable()
	p = sched.run("/usr/bin/env DISPLAY=%s metacity"%(masterServer.getDISPLAY()), stdout=open("/dev/null","w"), stderr=open("/dev/null","w"))
	time.sleep(2)

	__set_nvidia_settings(masterServer, '[gpu:%d]/FrameLockTestSignal'%(masterGPUIndex), '1')
	__set_nvidia_settings(masterServer, '[gpu:%d]/FrameLockTestSignal'%(masterGPUIndex), '0')

	# Kill window manager
	p.kill()

	# Next check if framelock actually got enabled.
	# Check FrameLockSyncRate on all GPUs.
	flChain = __getFrameLockChain(resList)

	masterFLSR = None
	badFLSRList = []
	numBadGPUs = 0
	for member in flChain:
		gpuDetails = member['gpu_details']
		thisFLSR = gpuDetails['FrameLockSyncRate']
		if masterFLSR is None:
			masterFLSR = thisFLSR
		elif thisFLSR != masterFLSR:
			numBadGPUs += 1
			if thisFLSR not in badFLSRList:
				badFLSRList.append(thisFLSR)
	
	if (numBadGPUs>0):
		raise VizError(VizError.BAD_CONFIGURATION, "Unable to setup frame lock on %d GPUs. Please ensure that all the GPUs in the passed list are chained properly via cabling."%(numBadGPUs))
	elif masterFLSR=='0':
		pprint(flChain)
		raise VizError(VizError.BAD_CONFIGURATION, "Unable to setup frame lock due to unknown reasons.")

	#pprint(flChain)
	return (masterRefreshRate, "Enabled Frame Lock @ %s Hz on %d GPUs connected to %d display devices"%(masterRefreshRate, len(flChain), totalPorts))

# Below, g_crt2 means CRT connected to physical port 2 on the GPU
g_crt1 = 0x00000002 # 0x00000002 - CRT-1
g_crt2 = 0x00000001 # 0x00000001 - CRT-2
g_dfp1 = 0x00020000 # 0x00040000 - DFP-1
g_dfp2 = 0x00010000 # 0x00020000 - DFP-2
# Logically ORing gives port combinations
# so -- 0x00000003 = CRT-0 and CRT-1 both connected

def __decodeMonitorMask(mask):
	result = {}
	if mask & g_crt2: result[0]={'type':'analog', 'name':'CRT-0'}
	if mask & g_dfp2: result[0]={'type':'digital', 'name':'DFP-0'}
	if mask & g_crt1: result[1]={'type':'analog', 'name':'CRT-1'}
	if mask & g_dfp1: result[1]={'type':'digital', 'name':'DFP-1'}
	return result

def __encodeDisplay(displayType, port):
	# FIXME: something wrong here. Did nvidia change lot of things from 4500 to 4600 ??
	# or are there any recent driver changes which are causing this to bork ?
	# I've inverted values for dfp1, dfp2 and crt1, crt2 to get things working on
	# QP of 5600, with driver 185.18.14
	if displayType=='analog':
		if port==0: return g_crt2
		if port==1: return g_crt1
		raise "Invalid port"
	elif displayType=='digital':
		if port==0: return g_dfp2
		if port==1: return g_dfp1
		raise "Invalid port"
	#else:
	raise "Invalid display type"

def __set_nvidia_settings(xServer, prop,val):
	sched = xServer.getSchedulable()
	cmd = 'nvidia-settings --display=%s -a %s=%s'%(xServer.getDISPLAY(), prop,val)
	#print cmd
	p = sched.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	p.wait()
	return p.getExitCode()

def __getGPUDetails(srv, scr, gpuIndex):
	gpuInfo = {}
	sched = srv.getSchedulable()
	cmd = "nvidia-settings --ctrl-display=%s --display=%s -q [gpu:%d]/EnabledDisplays"%(srv.getDISPLAY(), srv.getDISPLAY(), gpuIndex)
	#print cmd
	p = sched.run(cmd, stdout=subprocess.PIPE)
	p.wait()
	content =  p.getStdOut().split('\n')
	reobj = re.compile('^Attribute .*: (0x[0-9a-f]+).*$')
	l=content[1].rstrip().lstrip()
	#print l
	mobj=reobj.match(l)
	outputPortInfo = __decodeMonitorMask(int(mobj.groups(0)[0],16))
	for portNum in outputPortInfo:
		cmd = "nvidia-settings --ctrl-display=%s --display=%s -q [gpu:%d]/RefreshRate3[%s]"%(srv.getDISPLAY(), srv.getDISPLAY(), gpuIndex, outputPortInfo[portNum]['name'])
		p = sched.run(cmd, stdout=subprocess.PIPE)
		p.wait()
		content =  p.getStdOut().split('\n')
		reobj = re.compile("^Attribute 'RefreshRate3' \(.*\): ([0-9\.]+).*$")
		l=content[1].rstrip().lstrip()
		mobj=reobj.match(l)
		outputPortInfo[portNum]['RefreshRate']=mobj.groups(0)[0]
	gpuInfo['ports']=outputPortInfo

	cmd = "nvidia-settings --ctrl-display=%s --display=%s -q [gpu:%d]/FrameLockAvailable"%(srv.getDISPLAY(), srv.getDISPLAY(), gpuIndex)
	p = sched.run(cmd, stdout=subprocess.PIPE)
	p.wait()
	content =  p.getStdOut().split('\n')
	reobj = re.compile("^Attribute 'FrameLockAvailable' \(.*\): ([0-9\.]+)\..*$")
	l=content[1].rstrip().lstrip()
	mobj=reobj.match(l)
	if mobj is None:
		gpuInfo['FrameLockDeviceConnected']=False
		return gpuInfo
	else:
		gpuInfo['FrameLockDeviceConnected']=True

	gpuInfo['FrameLockAvailable']=mobj.groups(0)[0]

	cmd = "nvidia-settings --ctrl-display=%s --display=%s -q FrameLockSyncRate"%(srv.getDISPLAY(), scr.getDISPLAY())
	p = sched.run(cmd, stdout=subprocess.PIPE)
	p.wait()
	content =  p.getStdOut().split('\n')
	reobj = re.compile("^Attribute 'FrameLockSyncRate' \(.*\): ([0-9\.]+)\..*$")
	l=content[1].rstrip().lstrip()
	mobj=reobj.match(l)
	gpuInfo['FrameLockSyncRate']=mobj.groups(0)[0]

	cmd = "nvidia-settings --ctrl-display=%s --display=%s -q [gpu:%d]/FrameLockEnable"%(srv.getDISPLAY(), scr.getDISPLAY(),gpuIndex)
	p = sched.run(cmd, stdout=subprocess.PIPE)
	p.wait()
	content =  p.getStdOut().split('\n')
	reobj = re.compile("^Attribute 'FrameLockEnable' \(.*\): ([0-9\.]+)\..*$")
	l=content[1].rstrip().lstrip()
	mobj=reobj.match(l)
	gpuInfo['FrameLockEnable']=bool(int(mobj.groups(0)[0]))
	return gpuInfo

def __getFrameLockChain(resList):
	allServers = extractObjects(Server, resList)
	flChain = []

	# Create a frame lock chain consisting of all GPUs
	for srv in allServers:
		sched = srv.getSchedulable()
		baseGPUIndex = 0
		gpuDetails = []
		for srvScreen in srv.getScreens():
			for gi in range(baseGPUIndex, baseGPUIndex+len(srvScreen.getGPUs())):
				gpuDetails = __getGPUDetails(srv, srvScreen, gi)
				flChain.append({'server':srv, 'screen':srvScreen, 'gpu_index':gi, 'gpu_details':gpuDetails})
			baseGPUIndex += len(srvScreen.getGPUs())
	return flChain

def loadResourceGroups(sysConfig):
	"""
	Parse the resource group configuration file & return the resource groups
	"""
	try:
		dom = minidom.parse(vsapi.rgConfigFile)
	except xml.parsers.expat.ExpatError, e:
		raise VizError(VizError.BAD_CONFIGURATION, "Failed to parse XML file '%s'. Reason: %s"%(vsapi.rgConfigFile, str(e)))

	root_node = dom.getElementsByTagName("resourcegroupconfig")[0]
	rgNodes = domutil.getChildNodes(root_node,"resourceGroup")
	resgroups = {}
	for node in rgNodes:
		obj = vsapi.ResourceGroup()
		obj.deserializeFromXML(node)
		try:
			obj.doValidate(sysConfig['templates']['display_device'].values())
		except ValueError, e:
			raise VizError(VizError.BAD_CONFIGURATION, "FATAL: Error validating Resource Group '%s'. Reason: %s"%(obj.getName(),str(e)))

		newResGrp = obj.getName()
		if resgroups.has_key(newResGrp):
			raise VizError(VizError.BAD_CONFIGURATION, "FATAL: Resource group '%s' defined more than once."%(newResGrp))

		resgroups[newResGrp] = obj
	return resgroups

def loadLocalConfig():
	"""
	Load the local system configuration
	"""

	sysConfig = {
		'templates' : { 'gpu' : {} , 'display_device' : {}, 'keyboards' : {}, 'mice' : {}  }, 
		'nodes' : {},
		'resource_groups' : {}
	}

	# Load all templates...
	# NOTE: We load the templates from the global directory first.
	# Then load them from the local directory. This way, we ensure
	# that the local templates override the global ones.

	# Load GPU templates
	fileList = glob('/opt/vizstack/share/templates/gpus/*.xml')
	fileList += glob('/etc/vizstack/templates/gpus/*.xml')
	for fname in fileList:
		try:
			dom = minidom.parse(fname)
		except xml.parsers.expat.ExpatError, e:
			raise VizError(VizError.BAD_CONFIGURATION, "Failed to parse XML file '%s'. Reason: %s"%(fname, str(e)))

		newObj = vsapi.deserializeVizResource(dom.documentElement, [vsapi.GPU])	
		sysConfig['templates']['gpu'][newObj.getType()] = newObj

	# DisplayDevice templates
	fileList = glob('/opt/vizstack/share/templates/displays/*.xml')
	fileList += glob('/etc/vizstack/templates/displays/*.xml')
	for fname in fileList:
		try:
			dom = minidom.parse(fname)
		except xml.parsers.expat.ExpatError, e:
			raise VizError(VizError.BAD_CONFIGURATION, "Failed to parse XML file '%s'. Reason: %s"%(fname, str(e)))

		newObj = vsapi.deserializeVizResource(dom.documentElement, [vsapi.DisplayDevice])	
		sysConfig['templates']['display_device'][newObj.getType()] = newObj

	# Keyboard templates
	fileList = glob('/opt/vizstack/share/templates/keyboard/*.xml')
	fileList += glob('/etc/vizstack/templates/keyboard/*.xml')
	for fname in fileList:
		try:
			dom = minidom.parse(fname)
		except xml.parsers.expat.ExpatError, e:
			raise VizError(VizError.BAD_CONFIGURATION, "Failed to parse XML file '%s'. Reason: %s"%(fname, str(e)))

		newObj = vsapi.deserializeVizResource(dom.documentElement, [vsapi.Keyboard])	
		sysConfig['templates']['keyboards'][newObj.getType()] = newObj

	# Mice templates
	fileList = glob('/opt/vizstack/share/templates/mouse/*.xml')
	fileList += glob('/etc/vizstack/templates/mouse/*.xml')
	for fname in fileList:
		try:
			dom = minidom.parse(fname)
		except xml.parsers.expat.ExpatError, e:
			raise VizError(VizError.BAD_CONFIGURATION, "Failed to parse XML file '%s'. Reason: %s"%(fname, str(e)))

		newObj = vsapi.deserializeVizResource(dom.documentElement, [vsapi.Mouse])	
		sysConfig['templates']['mice'][newObj.getType()] = newObj

	# Check the master config file.	
	try:
		dom = minidom.parse(vsapi.masterConfigFile)
	except xml.parsers.expat.ExpatError, e:
		raise VizError(VizError.BAD_CONFIGURATION, "Failed to parse XML file '%s'. Reason: %s"%(vsapi.masterConfigFile, str(e)))

	root_node = dom.getElementsByTagName("masterconfig")[0]
	system_node = domutil.getChildNode(root_node, "system")
	type_node = domutil.getChildNode(system_node, "type")
	system_type = domutil.getValue(type_node)

	if system_type=='standalone':
		raise VizError(VizError.BAD_CONFIGURATION, "FATAL : Standalone configurations are not managed by the SSM")

	# Read in the node configuration file. This includes the scheduler information
	try:
		dom = minidom.parse(vsapi.nodeConfigFile)
	except xml.parsers.expat.ExpatError, e:
		raise VizError(VizError.BAD_CONFIGURATION, "Failed to parse XML file '%s'. Reason: %s"%(vsapi.nodeConfigFile, str(e)))

	root_node = dom.getElementsByTagName("nodeconfig")[0]
	nodes_node = domutil.getChildNode(root_node,"nodes")
	nodeIdx = 0
	for node in domutil.getChildNodes(nodes_node, "node"):
		nodeName = domutil.getValue(domutil.getChildNode(node,"name"))
		modelName = domutil.getValue(domutil.getChildNode(node, "model"))

		newNode = vsapi.VizNode(nodeName, modelName, nodeIdx)
		nodeIdx = nodeIdx+1

		propsNode = domutil.getChildNode(node, "properties")
		if propsNode is not None:
			for pn in domutil.getAllChildNodes(propsNode):
				newNode.setProperty(pn.nodeName, domutil.getValue(pn))
		resList = []
		gpus = []
		for gpu in domutil.getChildNodes(node, "gpu"):
			gpu_index = int(domutil.getValue(domutil.getChildNode(gpu,"index")))
			gpu_bus_id = domutil.getValue(domutil.getChildNode(gpu,"bus_id"))
			gpu_type = domutil.getValue(domutil.getChildNode(gpu,"type"))
			scanoutNode = domutil.getChildNode(gpu,"useScanOut")
			if scanoutNode is None:
				raise VizError(VizError.BAD_CONFIGURATION, "ERROR: useScanOut needs to be defined for every GPU")

			useScanOut = bool(domutil.getValue(scanoutNode))
			try:
				newGPU = copy.deepcopy(sysConfig['templates']['gpu'][gpu_type])
			except IndexError, e:
				raise VizError(VizError.BAD_CONFIGURATION, "ERROR: No such GPU type '%s'"%(gpu_type))

			newGPU.setIndex(gpu_index)
			newGPU.setHostName(nodeName)
			newGPU.setBusId(gpu_bus_id)
			newGPU.setUseScanOut(useScanOut)
			gpus.append(newGPU)
		if len(gpus)==0:
			print >>sys.stderr, "WARNING: Node %s has no GPUs."%(nodeName)
		resList += gpus

		for sli in domutil.getChildNodes(node, "sli"):
			newSLI = vsapi.deserializeVizResource(sli, [vsapi.SLI])
			if (newSLI.getIndex() is None) or (newSLI.getType() is None) or (newSLI.getGPUIndex(0) is None) or (newSLI.getGPUIndex(1) is None):
				raise VizError(VizError.BAD_CONFIGURATION, "Incompletely specified SLI bridge for host %s"%(nodeName))

			newSLI.setHostName(nodeName)
			resList.append(newSLI)

		keyboards = []
		for kbd in domutil.getChildNodes(node,"keyboard"):
			dev_index = int(domutil.getValue(domutil.getChildNode(kbd,"index")))
			dev_type = domutil.getValue(domutil.getChildNode(kbd,"type"))
			physNode = domutil.getChildNode(kbd,"phys_addr")
			if physNode is not None:
				dev_phys = domutil.getValue(physNode)
			else:
				dev_phys = None
			keyboards.append(vsapi.Keyboard(dev_index, nodeName, dev_type, dev_phys))
		resList += keyboards

		mice = []
		for mouse in domutil.getChildNodes(node,"mouse"):
			dev_index = int(domutil.getValue(domutil.getChildNode(mouse,"index")))
			dev_type = domutil.getValue(domutil.getChildNode(mouse,"type"))
			physNode = domutil.getChildNode(mouse,"phys_addr")
			if physNode is not None:
				dev_phys = domutil.getValue(physNode)
			else:
				dev_phys = None
			mice.append(vsapi.Mouse(dev_index, nodeName, dev_type, dev_phys))
		resList += mice

		X_servers = {}
		all_servers = []
		for xs in domutil.getChildNodes(node, "x_server"):
			serverTypeNode = domutil.getChildNode(xs, "type")
			if serverTypeNode is not None:
				serverType = domutil.getValue(serverTypeNode)
			else:
				serverType = vsapi.NORMAL_SERVER
			if serverType not in vsapi.VALID_SERVER_TYPES:
				raise VizError(VizError.BAD_CONFIGURATION, "ERROR: Bad server type %s"%(serverType))

			rangeNode = domutil.getChildNode(xs, "range")
			fromX = int(domutil.getValue(domutil.getChildNode(rangeNode, "from")))
			toX = int(domutil.getValue(domutil.getChildNode(rangeNode, "to")))
			if toX<fromX:
				raise VizError(VizError.BAD_CONFIGURATION, "FATAL: Bad input. Xserver range cannot have a 'to' less than 'from'")

			for xv in range(fromX, toX+1):
				if X_servers.has_key(xv):
					raise VizError(VizError.BAD_CONFIGURATION, "ERROR: Bad input. Xserver %d used more than once"%(xv))

				X_servers[xv]=None
				svr = vsapi.Server(xv, nodeName, serverType)
				all_servers.append(svr)
		if len(all_servers)==0:
			raise VizError(VizError.BAD_CONFIGURATION, "WARNING: Node %s has no X Servers."%(nodeName))
		resList += all_servers

		newNode.setResources(resList)
		sysConfig['nodes'][nodeName] = newNode

	# Process scheduler
	schedNodes = domutil.getChildNodes(root_node,"scheduler")
	if len(schedNodes)==0:
		raise VizError(VizError.BAD_CONFIGURATION, "FATAL: You need to specify at-least a scheduler")

	schedList = []
	for sNode in schedNodes:
		typeNode = domutil.getChildNode(sNode,"type")
		if typeNode is None:
			raise VizError(VizError.BAD_CONFIGRUATION, "FATAL: You need to specify a scheduler")

		# Get the scheduler specific params
		# Not specifying a parameter just results in passing an empty string
		paramNode = domutil.getChildNode(sNode,"param")
		param = ""
		if paramNode is not None:
			param=domutil.getValue(paramNode)

		nodeNodes = domutil.getChildNodes(sNode, "node")
		if len(nodeNodes)==0:
			raise VizError(VizError.BAD_CONFIGURATION, "FATAL: You need specify at-least one node per scheduler")

		nodeList = []
		for nodeNode in nodeNodes:
			nodeList.append(domutil.getValue(nodeNode))

		try:
			sched = metascheduler.createSchedulerType(domutil.getValue(typeNode), nodeList, param)
			schedList.append(sched)
		except ValueError, e:
			raise VizError(VizError.BAD_CONFIGURATION, "Error creating a scheduler : %s"%(str(e)))

	# Ensure that one node is managed by only one scheduler
	allNodeNames = []
	for sched in schedList:
		allNodeNames += sched.getNodeNames()
	allNames = {}
	for nodeName in allNodeNames:
		if allNames.has_key(nodeName):
			allNames[nodeName] += 1
		else:
			allNames[nodeName] = 1
	if len(allNames.keys())<len(allNodeNames):
		raise VizError(VizError.BAD_CONFIGURATION, "ERROR: One or more nodes have been mentioned more than once in the scheduler configuration. VizStack does not allow a single node to be managed by more than one scheduler at a time.")

	sysConfig['schedulerList'] = schedList

	# FIXME: check that all nodes are managed by some scheduler
	# If not, then that item will never be usable ! This is an
	# important debugging check

	# Load the resource groups
	resgroups = loadResourceGroups(sysConfig)
	sysConfig['resource_groups'] = resgroups

	return sysConfig


if __name__ == "__main__":
	ra = vsapi.ResourceAccess()
	alloc = ra.allocate([vsapi.ResourceGroup('desktop-right-2x2')])
	alloc.setupViz(ra)
	alloc.startViz(ra)
	refreshRate, msg =  enableFrameLock(alloc.getResources())
	print msg
	print disableFrameLock(alloc.getResources())
	#print '---- shell ----'
	#os.system('bash')
	#print '-- out of shell --'
	alloc.stopViz(ra)
	ra.stop()
