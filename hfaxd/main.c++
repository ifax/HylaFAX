/*	$Id$ */
/*
 * Copyright (c) 1995-1996 Sam Leffler
 * Copyright (c) 1995-1996 Silicon Graphics, Inc.
 * HylaFAX is a trademark of Silicon Graphics
 *
 * Permission to use, copy, modify, distribute, and sell this software and 
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 * 
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
 * 
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
 * OF THIS SOFTWARE.
 */
#include "port.h"
#include "InetFaxServer.h"
#if CONFIG_UNIXTRANSPORT
#include "UnixFaxServer.h"
#endif
#ifdef OLDPROTO_SUPPORT
#include "OldProtocol.h"
#endif
#ifdef SNPP_SUPPORT
#include "SNPPServer.h"
#endif
#ifdef HTTP_SUPPORT
#include "HTTPServer.h"
#endif
#include "Dispatcher.h"
#include "Array.h"
#include "Sys.h"
#include "Socket.h"
#include "config.h"

static	jmp_buf problem;

static void
sigCleanup(int sig)
{
    logError("CAUGHT SIGNAL %d", sig);
    longjmp(problem, 1);
}

static void
fatal(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogError(fmt, ap);
    va_end(ap);
    exit(-1);
}

#define	PATH_DEVNULL	"dev/null"
#define	PATH_DEVTCP	"dev/tcp"
#define	PATH_NETCONFIG	"etc/netconfig"
#define	PATH_DEVSOCKSYS	"dev/socksys"

/*
 * Verify and possibly setup the chroot'd filesystem as
 * required by the system.  Specifically, create a private
 * copy of /dev/null and any networking-related files
 * required by SVR4-based TCP/IP support.  We do this work
 * because the process runs chroot'd to the top of the
 * spooling area so normal files in the root filesystem
 * are inaccessible.
 *
 * NB: This work could be done once in a setup script but
 *     creating duplicates of character special device files
 *     is not simple from the shell.
 */
static void
CheckSpoolingSetup(void)
{
    struct stat sb;
    uid_t ouid = geteuid();
    (void) seteuid(0);
    mode_t omask = umask(0);
    /*
     * Craft a private /dev/null in the chroot'd filesystem
     * for use by syslog because some syslogs require this
     * to function correctly.
     */
    if (!Sys::isCharSpecialFile(PATH_DEVNULL)) {
	if (!Sys::isCharSpecialFile("/" PATH_DEVNULL, sb))
	    fatal("stat(%s): %s", "/" PATH_DEVNULL, strerror(errno));
	if (mknod(PATH_DEVNULL, sb.st_mode, sb.st_rdev) < 0)
	    fatal("Could not create %s: %s", PATH_DEVNULL, strerror(errno));
    }
    /*
     * If the system appears to support SVR4-style TCP/IP
     * support then craft a private copy of the necessary
     * files so that socket-related calls can be made after
     * chroot'ing to the top of the spooling area.
     *
     * NB: It is assumed that the dev subdirectory is already
     *     present (make install or similar should create it).
     */
    if (Sys::isCharSpecialFile("/" PATH_DEVTCP, sb) &&
      !Sys::isCharSpecialFile(PATH_DEVTCP)) {
	if (mknod(PATH_DEVTCP, sb.st_mode, sb.st_rdev) < 0)
	    fatal("Could not create %s: %s", PATH_DEVTCP, strerror(errno));
	/*
	 * Copy /etc/netconfig if not already present.
	 */
	if (Sys::stat(PATH_NETCONFIG, sb) < 0) {
	    int src = Sys::open("/" PATH_NETCONFIG, O_RDONLY);
	    if (src >= 0) {
		int dst = Sys::open(PATH_NETCONFIG, O_WRONLY|O_CREAT, 0444);
		if (dst < 0)
		    fatal("creat(%s): %s", PATH_NETCONFIG, strerror(errno));
		char buf[4096];
		int cc;
		while ((cc = read(src, buf, sizeof (buf))) > 0)
		    if (write(dst, buf, cc) < 0)
			fatal("write(%s): %s", PATH_NETCONFIG, strerror(errno));
		close(dst);
		close(src);
	    } else
		logWarning("%s: Cannot open: %s",
		    "/" PATH_NETCONFIG, strerror(errno));
	}
    }
    /*
     * SCO OS 5 apparently needs a /dev/socksys to implement
     * setsockopt calls (sigh); create one in the chroot'd
     * area if one exists in the root filesystem.
     */
    if (Sys::isCharSpecialFile("/" PATH_DEVSOCKSYS, sb) &&
      !Sys::isCharSpecialFile(PATH_DEVSOCKSYS))
	if (mknod(PATH_DEVSOCKSYS, sb.st_mode, sb.st_rdev) < 0)
	    fatal("Could not create %s: %s", PATH_DEVSOCKSYS, strerror(errno));
    (void) umask(omask);
    seteuid(ouid);
}

/*
 * Break the association with the controlling tty.
 * Note that we do not close all the open file descriptors
 * because many systems cache open descriptors within libraries
 * for performance reasons and do not react well when you close
 * them w/o telling them about it (and some don't react well
 * even when you *DO* tell them).  Since we know we're called
 * very early on from main in all our apps we just assume that
 * we only need to remove the stdin+stdout+stderr before forking
 * and starting a new session.
 */
static void
detachFromTTY(void)
{
    int fd = Sys::open(_PATH_DEVNULL, O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    switch (fork()) {
    case 0:	break;			// child, continue
    case -1:	_exit(1);		// error
    default:	_exit(0);		// parent, terminate
    }
    (void) setsid();
}

static void
usage(const char* appName)
{
    fatal("usage: %s [-o port] [-h port] [-l bindaddress] [-i port] [-u socket] [-q queue-directory]",
	appName);
}

fxDECLARE_PtrArray(IOHandlerArray, IOHandler*)
fxIMPLEMENT_PtrArray(IOHandlerArray, IOHandler*)
static	IOHandlerArray handlers;

static void
newInetServer(void)
{
    InetFaxServer* server = new InetFaxServer;
    server->open();
    handlers.append(server);
}

int
main(int argc, char** argv, char** envp)
{
    const char *bindaddress = NULL;

    HylaFAXServer::setLogFacility(LOG_FAX);
    HylaFAXServer::setupLogging("HylaFAX");
    HylaFAXServer::setupPermissions();

    fxStr appName = argv[0];
    u_int l = appName.length();
    appName = appName.tokenR(l, '/');

    optind = 1;
    opterr = 0;
    int c;
    const char* opts = "dHh:Ii:Oo:q:Ss:u:l:";
    /*
     * Deduce the spooling directory and whether or not to
     * detach the process from the controlling tty.  The
     * latter is complicated by the fact that we run both
     * as a master-server process and as a subprocess to
     * inetd.  If we are to act as a master-server then we
     * detach by default.  If we are invoked by inetd then
     * do not detach.  If no arguments are specified then
     * we imply a -I option (the new fax protocol) and do
     * not want to detach.  The logic is a touch convoluted
     * to do this correctly and is probably not worth the
     * effort (except to reduce configuration errors).
     */
    fxStr queueDir(FAX_SPOOLDIR);
    int detach = -1;			// unknown state
    while ((c = Sys::getopt(argc, argv, opts)) != -1)
	switch (c) {
	case 'h': case 'i': case 'o': case 's': case 'u':
	    if (detach == -1)		// detach unless explicitly specified
		detach = true;
	    break;
	case 'H': case 'I': case 'O': case 'S':
	    if (detach == -1)		// don't detach when invoked by inetd
		detach = false;
	    break;
	case 'd': detach = false; break;
	case 'q': queueDir = optarg; break;
	case '?': usage(appName);
	}
    if (detach == -1)			// no protocol options means -I
	detach = false;
    if (Sys::chdir(queueDir) < 0)
	fatal("Can not change directory to %s", (const char*)queueDir);
    CheckSpoolingSetup();
    if (detach)
	detachFromTTY();

    /*
     * Rescan the arguments and create the appropriate
     * protocol support threads.  We do this after the
     * above work for reasons I can no longer remember.
     */
    optind = 1;
    opterr = 0;
    while ((c = Sys::getopt(argc, argv, opts)) != -1)
	switch (c) {
#ifdef OLDPROTO_SUPPORT
	case 'o': handlers.append(new OldProtocolSuperServer(optarg)); break;
	case 'O':
	    { OldProtocolServer* server = new OldProtocolServer;
	      server->open();
	      handlers.append(server);
	    }
	    break;
#else
	case 'o': case 'O':
	    fatal("No support for old protocol");
	    /*NOTREACHED*/
#endif
#ifdef HTTP_SUPPORT
	case 'h': handlers.append(new HTTPSuperServer(optarg)); break;
	case 'H':
	    { HTTPFaxServer* server = new HTTPFaxServer;
	      server->open();
	      handlers.append(server);
	    }
	    break;
#else
	case 'h': case 'H':
	    fatal("No HTTP suport");
	    /*NOTREACHED*/
#endif
	case 'l':
	    bindaddress = strdup(optarg); break;
	case 'i': {
		InetSuperServer* iss;
		iss = new InetSuperServer(optarg);
		handlers.append(iss);
		if ((iss!=NULL) && (bindaddress!=NULL))
		    iss->setBindAddress(bindaddress);
		}
		break;
	case 'I': newInetServer(); break;
#ifdef SNPP_SUPPORT
	case 's': handlers.append(new SNPPSuperServer(optarg)); break;
	case 'S':
	    { SNPPServer* server = new SNPPServer;
	      server->open();
	      handlers.append(server);
	    }
	    break;
#else
	case 's': case 'S':
	    fatal("No SNPP support");
	    /*NOTREACHED*/
#endif
#if CONFIG_UNIXTRANSPORT
	case 'u': handlers.append(new UnixSuperServer(optarg)); break;
#else
	case 'u':
	    fatal("No support for Unix domain sockets");
	    /*NOTREACHED*/
#endif
	}
    if (handlers.length() == 0)
	newInetServer();

    /*
     * Startup protocol processing.
     */
    if (setjmp(problem) == 0) {
	signal(SIGHUP, fxSIGHANDLER(sigCleanup));
	signal(SIGINT, fxSIGHANDLER(sigCleanup));
	signal(SIGQUIT, fxSIGHANDLER(sigCleanup));
	signal(SIGILL, fxSIGHANDLER(sigCleanup));
	signal(SIGKILL, fxSIGHANDLER(sigCleanup));
	signal(SIGBUS, fxSIGHANDLER(sigCleanup));
	signal(SIGSEGV, fxSIGHANDLER(sigCleanup));

	for (;;)
	    Dispatcher::instance().dispatch();
    }
    /*
     * We explicitly destroy protocol threads so that any
     * resources are reclaimed (e.g. Unix domain sockets).
     */
    for (u_int i = 0, n = handlers.length(); i < n; i++)
	delete handlers[i];
    return 0;
}
