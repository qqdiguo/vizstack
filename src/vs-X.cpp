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
// vs-X
//
// VizStack's X server wrapper. This will be the mechanism through which the X server will be launched.
// 
// This program mimics the X server in the way that it handles signals like SIGUSR1, SIGHUP and SIGTERM.
// So, in a way, it is _almost_ a replacement program for the X server.
//
// The reasons to create this wrapper
//   1. Start X servers that run in an exact, known configuration
//   2. Keep track of when X servers are ready, by notifying the visualization stack.
//   3. Allow only those who have the rights to start X servers.
//
// The overall flow of this program is
//
// 1. Determine whether the configuration is standalone or not.
// 2. If this is a standalone configuration, then get the X configuration from a known location, convert it
//    into a configuration file and start the X server.
// 
// 3. If not a standalone configuration, then contact the System State Manager, giving it the following information
//    - X server number whose startup is being requested.
//    - Invoking username
//
// 4. The system state manager will return back the XML configuration, which we convert to the real configuration.
// 5. Start the real X server with this configuration
// 6. After the real X server has started up (SIGUSR1), intimate the SSM about successful startup. Update SSM with the
//    X server-centric view of the configuration.
// 7. When the real X server dies, intimate the SSM about the same.
// 8. If the SSM sends a message asking for the X server to be killed, then do the same.
// 
// NOTE: This is intended to be an SUID root binary.
// To improve "security" w.r.t using X, execute permissions for everyone could be removed from it (except the owner, i.e. root).
// This way, the access to the X server would only be through vs-X.
//
// With VizStack, one or more X servers (on the same machine) can start up or shut down almost at the same time
// With the 180 series drivers, I've observed that this causes the machine to lock-up. Adding a delay between X
// server startups fixed the startup crash. However, the crash then shifted to the cleanup part. VizStack, in a 
// bid to ensure cleanup, terminates the X server by disconnecting the SSM socket connection. When vs-X detects
// the disconnected SSM socket, it signals the X server to terminate, waits for the X server to dies, and then
// finally exits. This resulted in the X servers trying to cleanup at the same time.
//
// What are the solutions ? 
//   1. get nvidia to fix it 
//   2. workaround this
//
// I'm sticking to (2)...
//
// The code below serializes startup and shutdown of X servers. The serialization is implemented via a named
// semaphore. The semaphore is taken before starting and stopping X servers, and released after the operation is
// complete.
//
// Doing just an exclusive access didn't work. So I'm adding a few seconds of sleep next.
//
// I've incorporated a 5 second delay below. This was derived from experimentation on a 
// xw8600s with two FX 56800. Note that on a system with a lot of GPUs, this would result in
// a good amount of delay if a single job used a lot of X servers. This would affect the failure
// timeout values used in the client API, for instance.
//
// FIXME: Is there a way to find a good timeout value ? I need to push this to a configuration
// file.
//
// At this time, it looks like (5+2)*(number_of_gpus) in the system is a good timeout value for
// the client API.
//
// We use file locking in /var/lock/vs-X to implement exclusive access. This works pretty well,
// and given that /var/lock is guaranteed not be in NFS, shouldn't pose a problem.
// I had to remove the semaphore based solution. Once my X servers wouldn't start and I imagine
// some cleanup path had missed unlocking the semaphore.
//
// File locks are more reliable because they are given up when the process exits ! The result is
// less flaky software !
//

#define XSERVER_LOCK_DELAY 5

// FIXME: this has a dependency on vsapi. Could be unified using masterPort
#define SSM_UNIX_SOCKET_ADDRESS "/tmp/vs-ssm-socket"

//#define USE_MUNGE

#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <pwd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/file.h>

#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netdb.h>
#include "vsdomparser.hpp"
#include <xercesc/framework/StdOutFormatTarget.hpp>
#include <xercesc/framework/LocalFileFormatTarget.hpp>
#include <xercesc/dom/DOMWriter.hpp>

#include <X11/Xlib.h>
#include <X11/Xauth.h>
XERCES_CPP_NAMESPACE_USE
using namespace std;

int g_signotify_pipe[2];

bool g_debugPrints=false;

#define CODE_SIGCHLD '0'
#define CODE_SIGUSR1  '1'
#define CODE_SIGHUP   '2'
#define CODE_SIGTERM  '3'
#define CODE_SIGINT   '4'

#define RETRY_ON_EINTR(ret, x)  { \
	while(1) { \
		ret = x; \
		if(ret == -1) { \
			if(errno==EINTR) \
				 continue; \
		} \
		else \
			break; \
	} \
}

struct OptVal
{
	string name;
	string value;
};

void closeSocket(int fd)
{
	shutdown(fd, SHUT_RDWR);
	close(fd);
}
void addNotification(int whichPipe[2], unsigned char c)
{
	// we check this since the child might have closed the pipe
	// to indicate no signal handling needed
	if (whichPipe[1] != -1) 
	{
		write(whichPipe[1], &c, 1);
	}
}

void sigchild_handler(int sig)
{
	addNotification(g_signotify_pipe, CODE_SIGCHLD);
}

void usr1_handler(int sig)
{
	addNotification(g_signotify_pipe, CODE_SIGUSR1);
}

void hup_handler(int sig)
{
	addNotification(g_signotify_pipe, CODE_SIGHUP);
}

void term_handler(int sig)
{
	addNotification(g_signotify_pipe, CODE_SIGTERM);
}

void int_handler(int sig)
{
	addNotification(g_signotify_pipe, CODE_SIGINT);
}

#define XSERVER_PATH_1 "/usr/bin/X" // RHEL 5.1, above?
#define XSERVER_PATH_2 "/usr/X11R6/bin/X" // Older RHEL, SLES

int g_parent_sig_pipe[2];
void parent_sighdlr(int sig)
{
	switch(sig)
	{
		case SIGTERM:
			addNotification(g_parent_sig_pipe, CODE_SIGTERM);
			break;
		case SIGINT:
			addNotification(g_parent_sig_pipe, CODE_SIGINT);
			break;
		case SIGUSR1:
			addNotification(g_parent_sig_pipe, CODE_SIGUSR1);
			break;
		case SIGCHLD:
			addNotification(g_parent_sig_pipe, CODE_SIGCHLD);
			break;
	}
}

//
// Function called by main program before it forks off to
// create the SUID root X server
//
void parentWaitTillSUIDExits(int suidChildPid, int origParentPipe[2])
{
	struct sigaction sighandler;
	sighandler.sa_handler = parent_sighdlr;
	sighandler.sa_flags = SA_RESTART;
	sigemptyset(&sighandler.sa_mask);

	// close the read end of the pipe, since we don't want any information
	// from the child process.
	close(origParentPipe[0]);

	// We handle these signals
	sigaction(SIGINT, &sighandler, NULL);
	sigaction(SIGTERM, &sighandler, NULL);
	sigaction(SIGUSR1, &sighandler, NULL);

	fd_set rfds;
	bool loopDone = false;
	int retCode = 0;

	if(g_debugPrints)
		printf("INFO: Parent waiting for SUID child to exit\n");

	int readFD = g_parent_sig_pipe[0];

	while(!loopDone)
	{
		FD_ZERO (&rfds);
		FD_SET(readFD, &rfds);

		// NOTE: Infinite timeout select below
		int ret;
  		ret = select(readFD + 1, &rfds, NULL, NULL, NULL);

		// Handle Errors in select
		if (ret<0)
		{
			// FIXME: humm - what can we do here ?
			continue;
		}

		if(!FD_ISSET(readFD, &rfds))
			continue;

		// determine what signal we received by
		// reading the pipe
		char buf=0;
		RETRY_ON_EINTR(ret, read(readFD, &buf, 1));

		switch(buf)
		{
			case CODE_SIGCHLD:
			// handling SIGCHLD is how we get out of the loop and exit from the main program
			{
				int exitstatus;
				if(waitpid(suidChildPid, &exitstatus, WNOHANG)==suidChildPid)
				{
					loopDone = true;
					retCode = -1;
					if(g_debugPrints)
						printf("INFO: Parent got SIGCHLD. Exiting...\n");
					if(WIFEXITED(exitstatus))
					{
						retCode = exitstatus;
					}
					else
					if(WIFSIGNALED(exitstatus))
					{
						// XXX : This rarely seems to show up in practise, due to signal handling
						// by the X server
						retCode = 128+WTERMSIG(exitstatus);
					}
				}
				break;
			}
			case CODE_SIGTERM:
			// propagate TERM to child X server. When that exits, we get SIGCHLD and then we exit
				if(g_debugPrints)
					printf("INFO: Parent got SIGTERM. Propagating to SUID child...\n");
				kill(suidChildPid, SIGTERM);
				break;
			case CODE_SIGINT:
			// propagate INT to child X server. When that exits, we get SIGCHLD and then we exit
				if(g_debugPrints)
					printf("INFO: Parent got SIGINT. Propagating to SUID child...\n");
				kill(suidChildPid, SIGINT);
				break;
			case CODE_SIGUSR1:
				// propagate USR1 to parent. Child will not send us USR1 unless the signal mask
				// was setup for it properly. And who will setup the signal mask ? The caller program, of course !
				// This is relevant for both GDM as well as xinit
				if(g_debugPrints)
					printf("INFO: Parent got SIGUSR1. Propagating to SUID child...\n");
				kill(getppid(), SIGUSR1);
				break;
		}

	}

	if(g_debugPrints)
		printf("INFO: Parent done looping.\n");

	// in fact, I think the below line is redundant code
	// why ? if normal control comes here, then the child has
	// already died. 
	// 
	// The real reason we use this pipe is : if this process is
	// forcefully terminated - e.g. by kill -9, then the child
	// will be able to detect that quickly, and kill the real X server
	// termination of us 
	close(origParentPipe[1]);

	exit(retCode);
}

bool g_standalone = false;
string g_ssmHost;
string g_ssmPort;

bool getSystemType()
{
	char myHostName[256];
	gethostname(myHostName, sizeof(myHostName));

	VSDOMParserErrorHandler errorHandler;
	// Load the display device templates
	VSDOMParser *configParser = new VSDOMParser;

	errorHandler.resetErrors();
	DOMDocument *config = configParser->Parse("/etc/vizstack/master_config.xml", true, errorHandler);

	// Print out warning and error messages
	vector<string> msgs;
	errorHandler.getMessages (msgs);
	for (unsigned int i = 0; i < msgs.size (); i++)
		cout << msgs[i] << endl;

	if(!config)
	{
		cerr << "ERROR: Unable to get the local configuration of this node." << endl;
		return false;
	}

	DOMNode* rootNode = (DOMNode*)config->getDocumentElement();
	DOMNode* systemNode = getChildNode(rootNode, "system");
	DOMNode* systemTypeNode = getChildNode(systemNode, "type");
	string systemType = getValueAsString(systemTypeNode);
	bool isStandalone;
	if(systemType=="standalone")
		g_standalone = true;
	else
	{
		DOMNode *masterNode = getChildNode(systemNode, "master");
		if(!masterNode)
		{
			cerr << "ERROR: Invalid configuration. You must specify a master." << endl;
			return false;
		}
		g_ssmHost = getValueAsString(masterNode);

		DOMNode *masterPortNode = getChildNode(systemNode, "master_port");
		if(!masterPortNode)
		{
			cerr << "ERROR: Invalid configuration. You must specify a master port." << endl;
			return false;
		}

		g_ssmPort = getValueAsString(masterPortNode);
		if(g_ssmHost != "localhost")
		{
			int portNum = atoi(g_ssmPort.c_str());
			char buffer[100];
			sprintf(buffer, "%d",portNum);
			if((portNum<=0) or (g_ssmPort != buffer))
			{
				cerr << "ERROR: Invalid configuration. Please check the master port you have specified '%s' for errors." << endl;
				return false;
			}
		}
		else
		if(g_ssmPort.size()>100) // prevent buffer overruns
		{
			cerr << "ERROR: Invalid configuration. The master port you have specified '%s' is too long (limit : 100 chars)." << endl;
			return false;
		}

		g_standalone = false;
	}

	return true;
}

int write_bytes(int socket, const char *buf, int nBytes)
{
	int nBytesWritten = 0;
	const char *ptr = buf;
	while(nBytesWritten != nBytes)
	{
		int ret = send(socket, ptr, nBytes - nBytesWritten, MSG_NOSIGNAL);
		if(ret==-1)
		{
			if(errno==EINTR)
				continue;
			else
			{
				return nBytesWritten;
			}
		}
		else
		{
			nBytesWritten += ret;
			ptr += ret;
		}
	}
	return nBytesWritten;
}

int read_bytes(int socket, char *buf, int nBytes)
{
	int nBytesRead = 0;
	char *ptr = buf;
	while(nBytesRead != nBytes)
	{
		int ret = recv(socket, ptr, nBytes - nBytesRead, MSG_WAITALL);
		if(ret==-1)
		{
			if(errno==EINTR)
				continue;
			else
			{
				return nBytesRead;
			}
		}
		else
		if(ret==0)
		{
			// EOF !
			break;
		}
		else
		{
			nBytesRead += ret;
			ptr += ret;
		}
	}
	return nBytesRead;
}

bool write_message(int socket, const char* data, unsigned int dataLen)
{
	char sizeStr[6];

	// FIXME: can't handle messages larger than this due to the protocol limitations
	// centralize the protocol limitations.
	if(dataLen>99999) 
	{
		return false;
	}
	sprintf(sizeStr,"%d", dataLen);
	while(strlen(sizeStr)<5)
		strcat(sizeStr, " ");

	if(write_bytes(socket, sizeStr, 5)!=5)
	{
		return false;
	}

	if(write_bytes(socket, data, dataLen)!=dataLen)
	{
		return false;
	}

	return true;
}

char* read_message(int socket)
{
	char sizeStr[6];

	// get length first
	if(read_bytes(socket, sizeStr, 5)!=5)
	{
		fprintf(stderr, "Socket error: Unable to get message length\n");
		return 0;
	}

	int numMsgBytes = atoi(sizeStr);
	if(numMsgBytes<0)
	{
		fprintf(stderr, "Socket error: Bad message length %d\n", numMsgBytes);
		return 0;
	}

	char *message = new char[numMsgBytes+1];
	if(read_bytes(socket, message, numMsgBytes)!=numMsgBytes)
	{
		fprintf(stderr, "Socket error: Unable to read entire message\n");
		delete []message;
		return 0;
	}

	// Null terminate the string - so easy to forget these details at times :-(
	message[numMsgBytes] = 0;

	return message;
}

char *munge_encode(const char *message)
{
	// FIXME: Do thorough error logging in this function
	// failure here should be catchable !!!

	// Summary for this function is:
	// 1. we create a temporary file
	// 2. put the message as the contents of the file
	// 3. we pass that as input to munge
	// 4. we get the credential from stdout
	// 5. On error, undo stuff and return 0 !

	char tempInputFile[256];
	strcpy(tempInputFile, "/tmp/munge_encode_tmpXXXXXX");
	int fd = mkstemp(tempInputFile);
	if (fd==-1)
		return 0;

	// close the fd
	close(fd);

	FILE *fp=fopen(tempInputFile, "w");
	if(!fp)
	{
		unlink(tempInputFile);
		return 0;
	}
	if(fwrite(message, strlen(message), 1, fp)!=1)
	{
		fclose(fp);
		unlink(tempInputFile);
		return 0;
	}
	fclose(fp);

	char cmdLine[4096];
	sprintf(cmdLine, "munge --input %s",tempInputFile);
	fp = popen(cmdLine,"r");
	if(!fp)
	{
		fprintf(stderr, "ERROR Error - unable to run munge\n");
		unlink(tempInputFile);
		return 0;
	}

	string fileContent;
	char data[4096];
	while(1)
	{
		int nRead = fread(data, 1, sizeof(data), fp);

		if(nRead>0)
		{
			data[nRead]=0;
			fileContent = fileContent + data;
		}

		if (feof(fp))
		{
			break;
		}
	}
	int exitCode = pclose(fp);


	if(exitCode != 0)
	{
		fprintf(stderr, "ERROR Error - unable to get munge credential\n");
		unlink(tempInputFile);
		return false;
	}

	unlink(tempInputFile);
	return strdup(fileContent.c_str());
}


int g_lockFD = -1;
bool g_haveLock = false;

#define X_LOCK_FILE "/var/lock/vs-X"
bool take_lock()
{
	g_lockFD = open("/var/lock/vs-X", O_WRONLY | O_CREAT | O_TRUNC , S_IRUSR | S_IWUSR);
	if(g_lockFD==-1)
	{
		perror("ERROR: Failed to create lock file '" X_LOCK_FILE "'. Reason :");
		return false;
	}

	int ret;
	RETRY_ON_EINTR(ret, flock(g_lockFD,  LOCK_EX));
	if (ret==-1)
	{
		perror("ERROR: Failed to lock '" X_LOCK_FILE "'. Reason :");
		close(g_lockFD);
		g_lockFD = -1;
		return false;
	}

	g_haveLock = true;
	return true;
}

void free_lock()
{
	int ret;
	RETRY_ON_EINTR(ret, flock(g_lockFD,  LOCK_UN));
	
	close(g_lockFD);
	g_lockFD = -1;
	g_haveLock = false;
}

bool have_lock()
{
	return g_haveLock;
}

void take_lock_once()
{
	if(have_lock())
		return;
	take_lock();
}

void free_lock_once()
{
	if(!have_lock())
		return;
	free_lock();
}


int main(int argc, char**argv)
{
	char x_config_path[4096];
	char xorg_config_path[4096];
	char serverinfo_path[4096];
	int ssmSocket=-1;
	char myHostName[256];
	int origParentPipe[2];
	int userUid;
	int ownerUid;
	bool ignoreMissingDevices = false;

	if(getenv("VS_X_DEBUG"))
	{
		g_debugPrints = true;
	}

	userUid = ownerUid = getuid(); // This function can't fail :-)

	if(gethostname(myHostName, sizeof(myHostName))<0)
	{
		perror("ERROR: Unable to get my hostname");
		exit(-1);
	}

	// Start using the XML parser
	VSDOMParser::Initialize();

	if(!getSystemType())
	{
		return -1;
	}

	int notifyParent=0; // Notify parent process of X server readiness by SIGUSR1 ?
	char xdisplay[256];
	char *xauthority = 0;
	char user_xauthority_path[4096];
	strcpy(user_xauthority_path, "");

	struct passwd pwd, *ppwd;
	char pwd_buffer[2048];
	char *xuser=NULL;
	int rgsPromptUser = 0;

	// Default value of DISPLAY. May be overridden on the command line
	strcpy(xdisplay, ":0");

	if(access("/var/run/vizstack", F_OK)!=0)
	{
		fprintf(stderr, "ERROR: Directory /var/run/vizstack does not exist. I cannot proceed without this.\n");
		exit(-1);
	}

	// Create argument list for X server
	vector<char*> childArgs;
	// X server is the child process
	char *cmd=NULL;

	childArgs.push_back(NULL); // NOTE: this will be filled later
	for (int i=1;i<argc;i++)
	{
		//
		// ensure that some args can't be used --
		// e.g -layout, -config, -sharevts, -novtswitch
		// we'll use these ourselves, and overrides don't make sense on the command line
		//
		if((strcmp(argv[i],"-config")==0) || (strcmp(argv[i],"-layout")==0) || (strcmp(argv[i],"-sharevts")==0) || (strcmp(argv[i],"-novtswitch")==0))
		{
			fprintf(stderr, "ERROR: You're not allowed to use the command line argument '%s'. Usage of this is limited to VizStack.\n", argv[i]);
			exit(-1);
		}
		else
		if(argv[i][0]==':')
		{
			// this is the display
			strcpy(xdisplay,argv[i]);
		}
		else
		if(strcmp(argv[i],"-auth")==0)
		{
			xauthority = strdup(argv[i+1]);
			// this is an SUID root binary. We will copy this file as the authority file
			// later one. We should't allow a user to copy arbitrary files, this will be
			// a security hole
			if(access(xauthority, R_OK)!=0)
			{
				perror("Access denied to auth file");
				exit(-1);
			}
		}
		else
		if(strcmp(argv[i],"--rgs-prompt-user")==0)
		{
			rgsPromptUser = 1;
			continue; // don't propagate this option to X since it does not understand it
		}
		else
		if(strcmp(argv[i],"--ignore-missing-devices")==0)
		{
			ignoreMissingDevices = true;
		}
		
		childArgs.push_back(argv[i]);
	}

#if 0
	printf("ARGS are :\n");
	for(int i=0;i<argc;i++)
		printf("%d='%s'\n", i, argv[i]);
#endif

	// Find the invoking user ID
	if(getpwuid_r(getuid(), &pwd, pwd_buffer, sizeof(pwd_buffer), &ppwd)!=0)
	{
		perror("ERROR : Failed to username of invoking user\n");
		exit(-1);
	}

	xuser = strdup(pwd.pw_name);

	// Create a pipe to detect death of the parent process
	if (pipe(origParentPipe)<0)
	{
		perror("ERROR : Could not create pipe. System may be running out of resources");
		exit(-1);
	}

	//
	// Setup for handling the child process exiting very quickly
	// we register SIGCHLD before forking off
	//
	pipe(g_parent_sig_pipe);

	struct sigaction sighandler;
	sighandler.sa_handler = parent_sighdlr;
	sighandler.sa_flags = SA_RESTART;
	sigemptyset(&sighandler.sa_mask);
	sigaction(SIGCHLD, &sighandler, NULL);

	// Fork here to control SUID child
	// Fork is needed else the caller of the vs-X cannot control the
	// SUID child!
	int suidChildPid = fork();
	if(suidChildPid != 0)
	{
		parentWaitTillSUIDExits(suidChildPid, origParentPipe);
	}

	close(g_parent_sig_pipe[1]);
	close(g_parent_sig_pipe[0]);
	g_parent_sig_pipe[0] = g_parent_sig_pipe[1] = -1;

	// Close the write end since we don't need to communicate
	// anything to the parent process
	close(origParentPipe[1]);

	// Now shift to become the root user, else the RGS module loaded by the X server 
	// crashes on startup.
	//
	// This is done by setting the both real user id and group id to 0. (setting only the real
	// user ID does not do the trick. group id must change as well)
	//
	// This approach is implemented with the assumption that this binary is SUID(and SGID) root.
	//
	// Becoming the root is a necessity for the following reason:
	//
	// The RGS module crashes on startup from the X server, if the X server is not run by
	// the root user.
	//
	int status;
	status = setreuid(0, 0);
	if (status != 0)
	{
        	perror("ERROR: Unable to set effective user id to root");
	        exit(-1);
	}
	status = setregid(0, 0);
	if (status != 0)
	{
        	perror("ERROR: Unable to set effective group id to root");
	        exit(-1);
	}

	// Determine the right X server to use. This differs across distros.
	if(access(XSERVER_PATH_1, X_OK)==0)
	{
		childArgs[0]=XSERVER_PATH_1;
	}
	else
	if(access(XSERVER_PATH_2, X_OK)==0)
	{
		childArgs[0]=XSERVER_PATH_2;
	}
	else
	{
		fprintf(stderr, "ERROR : Cannot find an appropriate X server. Cannot continue.\n");
		exit(-1);
	}

	//
	// Generate the xorg.conf file name
	// This will be /var/run/vizstack/xorg-0.conf, where 0 is the X server number
	// We don't keep this file in /etc/X11. Creating a file in /etc/X11 would allow
	// regular users of X to use it later. We want to avoid this.
	//
	sprintf(xorg_config_path, "/var/run/vizstack/xorg-%s.conf", xdisplay+1); // NOTE: remove the colon to have filenames without colons. Windows doesn't like colons in filenames.
	sprintf(serverinfo_path, "/var/run/vizstack/serverinfo-%s.xml", xdisplay+1); // NOTE: remove the colon to have filenames without colons. Windows doesn't like colons in filenames.
	bool configInTempFile = false;

	// Create a lock file for exclusive access
	// need to do this before creating the configuration files. Why?
	// If we don't do this now, an X server that is cleaning up may
	// erase our config files.
	if(!take_lock())
	{
		exit(-1);
	}

	// Need to create the config file before we fork off the X server
	if(g_standalone)
	{
		sprintf(x_config_path, "/etc/vizstack/standalone/Xconfig-%s.xml", xdisplay+1);
		configInTempFile = false;
	}
	else
	{
		// connect to the master, retrieve the configuration for the specified display
		// check whether the user corresponds to the current user.
		// next, dump the config in a file and set x_config_path to that. Phew!

		struct hostent* hp;
		struct hostent ret;
		char buffer[1000];
		int h_errnop;

		// If the master is on "localhost", then we use Unix Domain Sockets
		if(g_ssmHost == "localhost")
		{
			ssmSocket = socket(AF_UNIX, SOCK_STREAM, 0);
			if(ssmSocket < 0)
			{
				perror("ERROR - Unable to create a socket to communicate with the SSM");
				exit(-1);
			}

			struct sockaddr_un sa;
			sa.sun_family = AF_UNIX;
			strcpy(sa.sun_path, g_ssmPort.c_str()); // on Linux, the path limit seems to be 118
			if(connect(ssmSocket, (sockaddr*) &sa, sizeof(sa.sun_family)+sizeof(sa.sun_path))<0)
			{
				perror("ERROR - Unable to connect to the local SSM");
				closeSocket(ssmSocket);
				exit(-1);
			}

			strcpy(myHostName, "localhost"); // our identity changes...

		}
		else
		{
			// resolve the SSM host first
			gethostbyname_r(g_ssmHost.c_str(), &ret, buffer, sizeof(buffer), &hp, &h_errnop);
			if(!hp)
			{
				perror("ERROR - unable to resolve SSM hostname");
				exit(-1);
			}

			ssmSocket = socket(AF_INET, SOCK_STREAM, 0);
			if(ssmSocket < 0)
			{
				perror("ERROR - Unable to create a socket to communicate with the SSM");
				exit(-1);
			}

			struct sockaddr_in si;
			si.sin_family = AF_INET;
			si.sin_port = htons(atoi(g_ssmPort.c_str()));
			memcpy((void*)&si.sin_addr.s_addr, hp->h_addr, 4); // Copying an IPv4 address => 4 bytes

			// Connect to the SSM
			if(connect(ssmSocket, (sockaddr*) &si, sizeof(si))<0)
			{
				perror("ERROR - Unable to connect to the SSM");
				closeSocket(ssmSocket);
				exit(-1);
			}
		}

		string myIdentity;
		myIdentity = "<serverconfig>";
		myIdentity += "<hostname>";
		myIdentity += myHostName;
		myIdentity += "</hostname>";
		myIdentity += "<server_number>";
		myIdentity += (xdisplay+1);
		myIdentity += "</server_number>";
		myIdentity += "</serverconfig>";

		char *encodedCred = 0;
		if(g_ssmHost == "localhost")
			encodedCred = strdup(myIdentity.c_str());
		else
		{
			// If the SSM is not on a local host, then it uses Munge to authenticate us.
			// We need to send out a a munge encoded packet in the beginning
			// so we need to call munge next, and use that to authenticate.
			encodedCred = munge_encode(myIdentity.c_str());

			if(!encodedCred)
			{
				fprintf(stderr,"ERROR - Unable to create a munge credential\n");
				closeSocket(ssmSocket);
				exit(-1);
			}
		}
		if(!write_message(ssmSocket, encodedCred, strlen(encodedCred)))
		{
			fprintf(stderr,"ERROR - Unable to send my identity to SSM\n");
			free(encodedCred);
			closeSocket(ssmSocket);
			exit(-1);
		}

		// Free the memory
		free(encodedCred);
		encodedCred = 0;

		// Authentication done, so now we need to get the X configuration
		// of this server from the SSM
		// we do this by sending a query message to the SSM
		char message[1024];
		sprintf(message,
			"<ssm>\n"
			"	<get_serverconfig>\n"
			"		<serverconfig>\n"
			"			<hostname>%s</hostname>\n"
			"			<server_number>%s</server_number>\n"
			"		</serverconfig>\n"
			"	</get_serverconfig>\n"
			"</ssm>\n", myHostName, xdisplay+1);

		if(!write_message(ssmSocket, message, strlen(message)))
		{
			fprintf(stderr, "ERROR - unable to send the query message to SSM\n");
			closeSocket(ssmSocket);
			exit(-1);
		}

		char *serverConfiguration = 0;
		if((serverConfiguration = read_message(ssmSocket))==0)
		{
			fprintf(stderr, "ERROR - unable to receive X configuration\n");
			closeSocket(ssmSocket);
			exit(-1);
		}

		// Validate the XML
		// FIXME: force the document to use serverconfig.xsd. This may catch any errors
		// in the serverconfig
		VSDOMParserErrorHandler errorHandler;
		VSDOMParser *configParser = new VSDOMParser;
		errorHandler.resetErrors();
		DOMDocument *config = configParser->Parse(serverConfiguration, false, errorHandler);
		if(!config)
		{
			// Print out warning and error messages
			vector<string> msgs;
			errorHandler.getMessages (msgs);
			for (unsigned int i = 0; i < msgs.size (); i++)
				cout << msgs[i] << endl;

			fprintf(stderr, "ERROR - bad return XML from SSM.\n");
			fprintf(stderr, "Return XML was --\n");
			fprintf(stderr, "----------------------------------------------\n");
			fprintf(stderr, "%s", serverConfiguration);
			fprintf(stderr, "\n----------------------------------------------\n");
			closeSocket(ssmSocket);
			exit(-1);
		}

		// Ensure that return status is "success"
		DOMNode* rootNode = (DOMNode*)config->getDocumentElement();
		DOMNode* responseNode = getChildNode(rootNode, "response");
		int status = getValueAsInt(getChildNode(responseNode, "status"));
		if(status!=0)
		{
			string msg = getValueAsString(getChildNode(responseNode, "message"));
			fprintf(stderr, "ERROR - Failure returned from SSM : %s\n", msg.c_str());
			delete configParser;
			closeSocket(ssmSocket);
			exit(-1);
		}
		
		DOMNode* retValNode = getChildNode(responseNode, "return_value");
		DOMNode *serverConfig = getChildNode(retValNode, "serverconfig");
		DOMNode *ownerNode = getChildNode(serverConfig, "owner");
		if(!ownerNode)
		{
			fprintf(stderr, "ERROR: Can't proceed; the owner for the X server is not specified by the SSM\n");
			delete configParser;
			closeSocket(ssmSocket);
			exit(-1);
		}
		DOMNode *svTypeNode = getChildNode(serverConfig, "server_type");
		if(!svTypeNode)
		{
			fprintf(stderr, "ERROR: Can't proceed; the type of the X server was not specified by the SSM\n");
			delete configParser;
			closeSocket(ssmSocket);
			exit(-1);
		}
		string svType = getValueAsString(svTypeNode);
		if (svType != "normal")
		{
			fprintf(stderr, "ERROR: vs-X manages only 'normal' X servers. It can't manage servers of type '%s'\n",svType.c_str());
			delete configParser;
			closeSocket(ssmSocket);
			exit(-1);
		}
		ownerUid = getValueAsInt(ownerNode);
		// Allow the owner of this server to start the X server.
		// Plus,
		// allow uid = 0 (i.e. root) to start the X server. This case i
		// happens when the X server is started via GDM. Here GDM would 
		// be running with id = root, and we just have to allow this. 
		// Hmm - I don't like these special cases
		if((ownerUid != userUid) && (userUid != 0)) 
		{
			fprintf(stderr, "ERROR: You don't have permission to start the X server %s. It is owned by user ID=%d\n",xdisplay, ownerUid);
			delete configParser;
			closeSocket(ssmSocket);
			exit(-1);
		}

		{
			// Translate owner UID to name

			struct passwd pwd, *ppwd;
			char pwd_buffer[2048];
			if(getpwuid_r(ownerUid, &pwd, pwd_buffer, sizeof(pwd_buffer), &ppwd)!=0)
			{
				perror("ERROR : Failed to find username of invoking user\n");
				exit(-1);
			}

			xuser = strdup(pwd.pw_name);
		}

		// write the configuration to the right file
		configInTempFile = true;
		sprintf(x_config_path,"/var/run/vizstack/xconfig-%s.xml", xdisplay+1);

		// Serialize the XML into the temporary file. 
		// FIXME: Could consider sending most of the below code to where it really belongs -
		// inside the DOM parser class.
		XMLCh tempStr[100];
		XMLString::transcode("LS", tempStr, 99);
		DOMImplementation *impl = DOMImplementationRegistry::getDOMImplementation(tempStr);
		//StdOutFormatTarget *tgt= new StdOutFormatTarget;
		LocalFileFormatTarget *tgt= new LocalFileFormatTarget(x_config_path);
		DOMWriter *writer = ((DOMImplementationLS*)impl)->createDOMWriter();
		errorHandler.resetErrors();
		writer->setErrorHandler(&errorHandler);
		writer->writeNode(tgt, *serverConfig);
		if (errorHandler.haveErrors())
		{
			fprintf(stderr, "ERROR - error while writing out the X config file\n");
			unlink(x_config_path);
			closeSocket(ssmSocket);
			exit(-1);
		}
		delete writer;
		delete tgt;

		delete configParser;

	}

	// If an authority file was specified, then copy it to a known place so that
	// access is easy...
	if(xauthority)
	{
		sprintf(user_xauthority_path, "/var/run/vizstack/Xauthority-%s", xdisplay+1);
		string cmd;
		cmd = "/opt/vizstack/bin/vs-generate-authfile ";
		cmd += xdisplay;
		cmd += " ";
		cmd += xauthority;
		cmd += " ";
		cmd += user_xauthority_path;
		if(system(cmd.c_str())!=0)
		{
			fprintf(stderr, "ERROR: Failed to create authority file for user access.\n");
			exit(-1);
		}
		if(chown(user_xauthority_path, ownerUid, 0)!=0)
		{
			fprintf(stderr, "ERROR: Failed to set owner of the X authority file.\n");
			exit(-1);
		}
		if(chmod(user_xauthority_path, S_IRUSR)!=0)
		{
			fprintf(stderr, "ERROR: Failed to change mode of the X authority file.\n");
			exit(-1);
		}
	}

	// convert the config file into a proper X server configuration file
	// that we can use as input to the X server
	char genCmd[4096];
	sprintf(genCmd, "/opt/vizstack/bin/vs-generate-xconfig --input=%s --output=%s --server-info=%s", x_config_path, xorg_config_path, serverinfo_path);
	if(ignoreMissingDevices)
		strcat(genCmd, " --ignore-missing-devices");
	int ret = system(genCmd);
	if(ret!=0)
	{
		exit(ret);
	}

	int usesAllGPUs = 0;
	VSDOMParser *infoParser = new VSDOMParser;
	vector<OptVal> cmdArgVal;
	DOMDocument *serverinfo;
	{
		// Find information from the serverinfo file.
		//
		VSDOMParserErrorHandler errorHandler;
		serverinfo = infoParser->Parse(serverinfo_path, true, errorHandler);
		if(!serverinfo)
		{
			cerr << "ERROR: Unable to get serverinfo." << endl; //FIXME: This must never happen
			exit(-1);
		}

		DOMNode* rootNode = (DOMNode*)serverinfo->getDocumentElement();
		DOMNode* usesAllGPUNode = getChildNode(rootNode, "uses_all_gpus");
		usesAllGPUs = getValueAsInt(usesAllGPUNode);

		vector<DOMNode*> cmdArgNodes = getChildNodes(rootNode, "x_cmdline_arg");
		for(unsigned int i=0;i<cmdArgNodes.size();i++)
		{
			OptVal ov;
			ov.name = getValueAsString(getChildNode(cmdArgNodes[i],"name"));
			DOMNode *valNode = getChildNode(cmdArgNodes[i],"value");
			if(valNode)
				ov.value = getValueAsString(valNode);

			// don't allow anybody to sabotage our scheme.
			if((ov.name == "config") || (ov.name=="sharevts") || (ov.name=="novtswitch") || (ov.name=="xinerama") || (ov.name=="layout"))
				continue;
	
			ov.name = "-" + ov.name;
			cmdArgVal.push_back(ov);
		}
	}


	// Register SIGCHLD handler first so that we get exit notifications from our child X
	// server
	struct sigaction siginfo;
	siginfo.sa_handler = sigchild_handler;
	siginfo.sa_flags = SA_RESTART; 
	sigemptyset (&siginfo.sa_mask);
	sigaction(SIGCHLD, &siginfo, NULL);

	// fill in the configuration file name on the child X server's command line
	childArgs.push_back("-config");
	childArgs.push_back(xorg_config_path);
	if(usesAllGPUs==0) // If the X server does not use all GPUs in the system, then we need to use sharevts & novtswitch
	{
		childArgs.push_back("-sharevts");
		childArgs.push_back("-novtswitch");
	}
	for(unsigned int i=0;i<cmdArgVal.size();i++)
	{
		childArgs.push_back(strdup(cmdArgVal[i].name.c_str()));
		if(cmdArgVal[i].value.size()>0)
			childArgs.push_back(strdup(cmdArgVal[i].value.c_str()));
	}
	childArgs.push_back(NULL);

	// Check if SIGUSR1 is set to IGN. If so, then we'll need
	// to propagate SIGUSR1 to the parent.
	struct sigaction usr1;
	sigaction(SIGUSR1, NULL, &usr1);
	if(usr1.sa_handler == SIG_IGN)
	{
		if(g_debugPrints)
			printf("INFO : Parent requested SIGUSR1 notification\n");
		notifyParent=1;
	}

	// Activate signal handler for SIGUSR1
	//
	// We need to do this before the fork to avoid timing
	// issues
	//
	pipe(g_signotify_pipe);
	usr1.sa_handler = usr1_handler;
	usr1.sa_flags = SA_RESTART; 
	sigemptyset (&usr1.sa_mask);
	sigaction(SIGUSR1, &usr1, NULL);

	pid_t childpid = fork();

	if (childpid<0)
	{
		perror("ERROR : vs-X failed - fork error");
		exit(-1);
	}

	if (childpid==0)
	{
		// FD gets duplicated, so we free it here
		// This child process inherits the shared lock.
		// So, if we use lock_free(), then it's equivalent
		// to giving up the lock. So, we just need to close 
		// the FD here.
		close(g_lockFD);

		// Close the FDs on the child
		// FIXME: more elegant and generic code could close all FDs till MAX_FD
		close(g_signotify_pipe[0]);
		close(g_signotify_pipe[1]);
		close(origParentPipe[0]);

		// Close the connection to the System State Manager.
		// We need to do this since we'll exec the X server next
		close(ssmSocket);

		// Set SIGUSR1 to IGN. This will cause the X server we 'exec'
		// next to send the parent a SIGUSR1. This signal will indicate
		// to the parent that it is ready to accept connections.

		struct sigaction usr1;
		usr1.sa_handler = SIG_IGN;
		usr1.sa_flags = SA_RESTART; 
		sigaction(SIGUSR1, &usr1, NULL);

		// Start the X server as the child process
		execv(childArgs[0], &childArgs[0]);

		// If exec returns, an error happened
		perror("ERROR : vs-X failed - exec failed");
		exit(-1);
	}

	// Control will come here in the parent - i.e. the original vs-X process

	// Register the HUP & TERM handlers, since we'll need to propagate these
	// explicitly to the child X server we started
	// Also, propagate SIGINT to the child

	siginfo.sa_handler = hup_handler;
	siginfo.sa_flags = SA_RESTART; 
	sigemptyset (&siginfo.sa_mask);
	sigaction(SIGHUP, &siginfo, NULL);

	siginfo.sa_handler = term_handler;
	siginfo.sa_flags = SA_RESTART; 
	sigemptyset (&siginfo.sa_mask);
	sigaction(SIGTERM, &siginfo, NULL);

	siginfo.sa_handler = int_handler;
	siginfo.sa_flags = SA_RESTART; 
	sigemptyset (&siginfo.sa_mask);
	sigaction(SIGINT, &siginfo, NULL);

	// We wait for one of the following events
	// a. Child X server exit (i.e. SIGCHLD)
	// b. SIGUSR1 signal from child process - i.e. X server
	// c. Signals to pass on to the child X server
	//     - SIGHUP
	//     - SIGTERM
	//     - SIGINT (^C)
	//

	fd_set rfds;

	bool xInitDone = false;
	char xuser_filename[256];
	int retCode=0;
	bool loopDone=false;

	sprintf(xuser_filename, "/var/run/vizstack/xuser-%s", xdisplay+1);

	while(!loopDone)
	{
		FD_ZERO (&rfds);
		FD_SET (g_signotify_pipe[0], &rfds);
		int maxFD = g_signotify_pipe[0];

		// Monitor the SSM socket.
		// SSM will close the socket when it wants us to exit
		// In the future we can use this to do X related activities from the
		// SSM.
		if(ssmSocket != -1)
			FD_SET(ssmSocket, &rfds);
		if (ssmSocket>maxFD)
			maxFD = ssmSocket;
		// Monitor parent process exit
		if(origParentPipe[0]!=-1)
			FD_SET(origParentPipe[0], &rfds);
		if (origParentPipe[0]>maxFD)
			maxFD = origParentPipe[0];

		if(g_debugPrints)
			printf("INFO : Waiting for child process\n");

		// NOTE: Infinite timeout select below
		int ret;

		// FIXME: we need to add a timeout below
		// why ? currently, we're sending TERM to the X server
		// to kill it. That's a safe thing to do. But what if the
		// X server doesn't die. Killing it -9 is bad since that may
		// cause system instability (mild word for "hard hang"!)
		// We can help in this situation by
		// waiting <n> seconds for the child to die
		// if the child doesn't die during that time, then kill -9 it
		// send information about kill -9 to SSM, since this is a really 
		// bad case.
  		ret = select(maxFD + 1, &rfds, NULL, NULL, NULL);

		// Handle Errors in select
		if (ret<0)
		{
			// FIXME: humm - what can we do here ?
			continue;
		}

		// Handle SSM socket activity
		if((ssmSocket!=-1) && FD_ISSET(ssmSocket, &rfds))
		{
			// Do a read of 1 byte
			char buf=0;
			RETRY_ON_EINTR(ret, read(ssmSocket, &buf, 1));

			// If the SSM closes the socket, then we act as if we had got
			// SIGTERM
			if(ret==0)
			{
				if(g_debugPrints)
					printf("INFO : SSM closed connection. Killing X server using SIGTERM\n");

				take_lock_once ();

				// delay a bit to give time for thing to stability. This has
				// been done to avoid X servers cleaning up in rapid succession.
				sleep(XSERVER_LOCK_DELAY);
				kill(childpid, SIGTERM);
				closeSocket(ssmSocket);
				ssmSocket = -1;
			}
		}

		// Handle parent exit
		if((origParentPipe[0]!=-1) && FD_ISSET(origParentPipe[0], &rfds))
		{
				// Do a read of 1 byte
				char buf=0;
				RETRY_ON_EINTR(ret, read(origParentPipe[0], &buf, 1));

				// If the parent dies, then we kill the child
				// this way we ensure cleanup in all cases.
				if(ret==0)
				{
						close(origParentPipe[0]);
						origParentPipe[0] = -1;
						if(g_debugPrints)
							printf("INFO : Parent vs-X died. Killing X server using SIGTERM\n");

						take_lock_once();

						// delay a bit to give time for thing to stability. This has
						// been done to avoid X servers cleaning up in rapid succession.
						sleep(XSERVER_LOCK_DELAY);
						kill(childpid, SIGTERM);
				}
				else
				{
					// free our X server lock
					free_lock_once();
					fprintf(stderr, "FATAL: Bad case - parent isn't supposed to write to us!\n");
					exit(-1);
				}
		}

		// All processing after this is for the notify pipe
		// if there's nothing to do there, then just continue
		if (!FD_ISSET(g_signotify_pipe[0], &rfds))
			continue;

		// determine what signal we received by
		// reading the pipe
		char buf=0;
		RETRY_ON_EINTR(ret, read(g_signotify_pipe[0], &buf, 1));

		switch(buf)
		{
			case CODE_SIGCHLD:
				// a child process exited.
				{
					int exitstatus;
					// did our child X server exit ?
					if(waitpid(childpid, &exitstatus, WNOHANG)==childpid)
					{
						loopDone = true;
						retCode = -1;
						if(WIFEXITED(exitstatus))
						{
							if(g_debugPrints)
								printf("INFO : Child X server exited\n");
							retCode = WEXITSTATUS(exitstatus);
						}
						else
						if(WIFSIGNALED(exitstatus))
						{

							// XXX : This rarely seems to show up in practise, due to signal handling
							// by the X server
							if(g_debugPrints)
								printf("INFO : Child X server killed by signal %d\n", WTERMSIG(exitstatus));

							retCode = 128+WTERMSIG(exitstatus);
						}
					}
					else
					{
						// other child process exits cause us to come here.
						// we use "xauth" and when that exits, we'll come here.
						// ignore this
					}
				}
				break;
			case CODE_SIGUSR1: 
				// Do SIGUSR1 handling only once.
				// Why ? Sometimes I have noticed that I get a SIGUSR1 everytime
				// a client connects and disconnects
				if(!xInitDone)
				{
					if(notifyParent)
					{
						if(g_debugPrints)
							printf("INFO : Propagating SIGUSR1 to parent\n");
						kill(getppid(), SIGUSR1);
					}
					else
					{
						if(g_debugPrints)
							printf("INFO : No action taken on SIGUSR1 from child X server\n");
					}

					// delay a bit to give time for the driver to initialize.
					// this takes more time compared to just X server startup.
					// X server possibly allows connections before the driver completely inits
					sleep(XSERVER_LOCK_DELAY);

					// free our X server lock so that other X servers can start
					free_lock();

					// sleep some more time, so that xinit can come up & run our access control
					// program. FIXME: Will this save us from race conditions forever ??
					sleep(XSERVER_LOCK_DELAY);

					//
					// Record the name of the user for whom this X server is intended in /var/run/vizstack/rgsuser
					// This information can be used to find who owns the X server. At the time of this writing,
					// it is intended to be used by the RGS PAM module to restrict access to the X server.
					//
					if(g_debugPrints)
						printf("INFO : Creating record of X server allocation\n");
					FILE *fp=fopen(xuser_filename,"w");
					if(fp==NULL)
					{
						perror("ERROR: Unable to access vizstack xuser files");
						exit(-1);
					}

					// record our PID as the X server's PID
					// this allows us to track X server kills directly
					fprintf(fp, "%s %d %d", xuser, getpid(), rgsPromptUser); 
					fclose(fp);

					xInitDone = true; // remember that we suceeded in creating the X server

					// Tell the SSM that we're up!
					string notifyMessage = 
						"<ssm>"
							"<update_x_avail>"
								"<newState>1</newState>"
									"<serverconfig>";
					notifyMessage += "<hostname>";
					notifyMessage += myHostName;
					notifyMessage += "</hostname>";
					notifyMessage += "<server_number>";
					notifyMessage += (xdisplay+1);
					notifyMessage += "</server_number>";
					notifyMessage +=
									"</serverconfig>"
								"</update_x_avail>"
						"</ssm>";

					// if we're not running standalone, then intimate the SSM that this
					// X server is available.
					if(ssmSocket != -1)
					{
							if(!write_message(ssmSocket, notifyMessage.c_str(), notifyMessage.size()))
							{
									fprintf(stderr,"ERROR - Unable to send start message to SSM\n");
									exit(-1);
							}
					}

					// TODO: Update System State Manager with information that the X server has started.
				}
				break;

			case CODE_SIGHUP: 
				if(g_debugPrints)
					printf("INFO : Propagating SIGHUP to child\n");
				kill(childpid, SIGHUP);
				break;

			case CODE_SIGTERM: 
				if(g_debugPrints)
					printf("INFO : Propagating SIGTERM to child\n");

				take_lock_once();

				// delay a bit to give time for thing to stability. This has
				// been done to avoid X servers cleaning up in rapid succession.
				sleep(XSERVER_LOCK_DELAY);

				kill(childpid, SIGTERM);
				break;

			case CODE_SIGINT: 
				if(g_debugPrints)
					printf("INFO : Propagating SIGINT to child\n");

				take_lock_once();

				// delay a bit to give time for thing to stability. This has
				// been done to avoid X servers cleaning up in rapid succession.
				sleep(XSERVER_LOCK_DELAY);
				kill(childpid, SIGTERM);
				break;
		}
	}

	if(xInitDone)
	{
		// Remove the X usage record, if we created one
		if(g_debugPrints)
			printf("INFO : Removing record of X server allocation.\n");
		unlink(xuser_filename); 
	}

	//  delete all the temporary edids created
	DOMNode* rootNode = (DOMNode*)serverinfo->getDocumentElement();
	vector<DOMNode*> tempEdidNodes = getChildNodes(rootNode, "temp_edid_file");
	for(unsigned int i=0;i<tempEdidNodes.size();i++)
	{
		string fname=getValueAsString(tempEdidNodes[i]);
		unlink(fname.c_str());
	}
	delete infoParser;

	// remove the other temporary config files we created:
	//    1. the X config file
	//    2. the serverinfo file
	unlink(xorg_config_path);
	unlink(serverinfo_path);
	if(strlen(user_xauthority_path)>0)
	{
		unlink(user_xauthority_path);
	}
	if(configInTempFile)
	{
		unlink(x_config_path);
	}

	// Update System State Manager with information that the X server has stopped.
	// NOTE: we need to do this after removing the X org file, else there is a chance of
	// an improper config file in place. This is not a big problem, but it's nice to be
	// consistent
	string notifyMessage = 
		"<ssm>"
			"<update_x_avail>"
				"<newState>0</newState>"
					"<serverconfig>";
	notifyMessage += "<hostname>";
	notifyMessage += myHostName;
	notifyMessage += "</hostname>";
	notifyMessage += "<server_number>";
	notifyMessage += (xdisplay+1);
	notifyMessage += "</server_number>";
	notifyMessage += "</serverconfig>"
				"</update_x_avail>"
		"</ssm>";

	if(ssmSocket!=-1)
	{
			if(!write_message(ssmSocket, notifyMessage.c_str(), notifyMessage.size()))
			{
					fprintf(stderr,"ERROR - Unable to send start message to SSM\n");
			}
	}

	// close the connection to the SSM
	if(ssmSocket!=-1)
		closeSocket(ssmSocket);

	// When we come here, then the X server will not be running
	// but we'll have the lock, so release it
	if(have_lock())
	{
		if(g_debugPrints)
			printf("INFO : Freeing our lock\n");
		free_lock();
	}

	exit(retCode);
}
