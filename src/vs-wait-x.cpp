/*
* VizStack - A Framework to manage visualization resources

* Copyright (C) 2009-2010 Hewlett-Packard
* 
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

//
// replacement for user-auth-add
//
// Adds access to the X server for the invoking user
// Connect to the X server and wait till it exits.
//
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <sys/select.h>
#include <string>
#include <sys/types.h>
#include <pwd.h>

using namespace std;

int main()
{
        struct passwd pwd, *ppwd;
        char pwd_buffer[2048];

        // Find the invoking user ID
        if(getpwuid_r(getuid(), &pwd, pwd_buffer, sizeof(pwd_buffer), &ppwd)!=0)
        {
                perror("ERROR : Failed to username of invoking user\n");
                exit(-1);
        }

	Display *dpy = XOpenDisplay(0);
	if(!dpy)
	{
		cerr << "Unable to connect to X server." << endl;
		exit(-1);
	}

	string cmd;
	cmd = "xhost +si:localuser:";
	cmd += pwd.pw_name;
	int ret = system(cmd.c_str());
	if(ret != 0)
	{
		cerr << "Failed to add user access to X server. Exiting." << endl;
		exit(-1);
	}

	// Get the FD of the connection to the X server. This lets
	// us implement "wait-on-exit".
	int xfd = ConnectionNumber(dpy);

	bool doExit=false;
	while(!doExit)
	{
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(xfd, &rfds);
		int ret = select(xfd+1, &rfds, NULL, NULL, NULL);
		if(ret==1)
		{
			//cout << "X connection closed" << endl;
			exit(0);
		}
	}

	exit(0);
}
