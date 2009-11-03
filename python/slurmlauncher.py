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

import scheduler
import subprocess
import string
import launcher
import random
import sys
import slurmscheduler
import localscheduler
import domutil
import time

class SLURMLauncher(launcher.Launcher):

    rootNodeName = "SLURMReservation"

    def serializeToXML(self):
        ret = "<%s>"%(SLURMLauncher.rootNodeName)
        ret = ret + "%d"%(self.schedId)
        ret = ret + "</%s>"%(SLURMLauncher.rootNodeName)
        return ret

    def deserializeFromXML(self, domNode):
        if domNode.nodeName != SLURMLauncher.rootNodeName:
            raise ValueError, "Failed to deserialize SLURMLauncher. Programmatic error"

        try:
	        self.schedId = int(domutil.getValue(domNode))
        except ValueError, e:
            raise ValueError, "Failed to deserialize SLURMLauncher. Invalid scheduler ID '%s'"%(domutil.getValue(domNode))

        self.nodeList = None
        self.scheduler = None

    def __init__(self, schedId=None, nodeList=None, scheduler=None):
        self.__isCopy = False
        self.schedId = schedId
        # nodeList and scheduler will be None if this object is being deserialized
        # if this object is deserialized, then deleting this object, or calling its
        # deallocate function will not remove the allocation.
        self.nodeList = nodeList
        self.scheduler = scheduler

    def getSchedId(self):
        return self.schedId

    def __del__(self):
        # nodeList can be if we were deserialized
        # in this case, we don't delete the job!
        if (self.nodeList is not None) and (len(self.nodeList)>0): 
            self.deallocate()

    def __getstate__(self):
        sched_save = self.scheduler
        del self.scheduler
        odict = self.__dict__.copy()
	self.scheduler = sched_save
        return odict

    def __setstate__(self, dict):
        """
        This function is implemented to keep track of deepcopies.  If deepcopy is used,
        then this function will be used, and we keep track of the fact that this is a 
        copy.

        If isCopy is true, then the destructor will not remove the allocation.
        """
        self.__dict__.update(dict)
	self.scheduler = None
        self.__isCopy = True

    def getSchedId(self):
        return self.schedId

    def run(self, cmd, node, thisStdin=None, thisStdout=None, thisStderr=None, launcherEnv=None):
        slots = 1
        op_options = ""

        srun_cmd = "srun"
        srun_args = "--jobid=%d -w %s -N1 %s"%(self.schedId, node, cmd)
        cmd_list = srun_args.split(" ")
        cmd_list = filter(lambda x: len(x)>0, cmd_list) # trim extra spaces
        try:
            # NOTE: set close_fds = True as we don't want the child to inherit our FDs and then 
            # choke other things ! e.g. the SSM will not clean up a connection if close_fds is not set to True
            p = subprocess.Popen([srun_cmd]+cmd_list, stdout=thisStdout, stderr=thisStderr, stdin=thisStdin, close_fds=True, env=launcherEnv)
        except OSError:
                return None

        return localscheduler.VizProcess(p)

    def deallocate(self):
        # If we're a copy, then we have not much to do
        if self.__isCopy == True:
            self.schedId = None
            return

        if self.schedId is None:
            return

        # Deallocate this SLURM Job
        # Note that this will kill all the job steps associated with the job
        try:
            p = subprocess.Popen(["scancel"]+["-q", str(self.schedId)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True)
        except OSError, e:
            raise SLURMError(e.__str__)

        if(p.returncode == 1):
            raise SLURMError(p.communicate()[1])

        if self.scheduler is not None:
            self.scheduler.deallocate(self)
            self.scheduler = None

        self.schedId = None

