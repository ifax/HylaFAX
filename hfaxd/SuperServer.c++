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
#include "Sys.h"
#include "Dispatcher.h"
#include "HylaFAXServer.h"
#include "SuperServer.h"
#include "Socket.h"

#define	MAXTRIES	10

SuperServer::SuperServer(const char* k, int bl) : kind(k)
{
    backlog = bl;
    ntries = 0;
    Dispatcher::instance().startTimer(0,1,this);	// schedule setup
}
SuperServer::~SuperServer() {}

void
SuperServer::timerExpired(long, long)
{
    if (!startServer()) {
	if (++ntries >= MAXTRIES) {
	    logError("HylaFAX %s: Unable to init server, "
		"giving up after %u tries.", (const char*) kind, ntries);
	    return;
	}
	logNotice("HylaFAX %s: Unable to init server, "
	    "trying again in %u seconds.", (const char*) kind, 5*ntries);
	Dispatcher::instance().startTimer(5*ntries,0, this);
    } else
	logNotice("HylaFAX %s Protocol Server: restarted.", (const char*) kind);
}

int
SuperServer::inputReady(int fd)
{
    int c = Socket::accept(fd, NULL, NULL);
    if (c < 0) {
	if (errno == EINTR)
	    return (0);
	logError("HylaFAX %s: accept: %m", (const char*) kind);
	_exit(-1);
    }
    pid_t pid = fork();
    switch (pid) {
    case 0:				// child
	/*
	 * Child process, setup to process protocol requests.
	 * We unlink the dispatcher hook to this code and
	 * setup the link to the main handler that processes
	 * protocol requests.  This routine is dispatched
	 * when data is received on stdin (for compatibility
	 * with servers started via inetd).
	 */
	HylaFAXServer* app; app = newChild();	// XXX for __GNUC__
	Dispatcher::instance().unlink(fd);
	HylaFAXServer::closeLogging();		// close any open syslog fd
	HylaFAXServer::closeAllBut(c);
	if (dup2(c, STDIN_FILENO) < 0 || dup2(c, STDOUT_FILENO) < 0) {
	    logError("HylaFAX %s: dup2: %m", (const char*) kind);
	    _exit(-1);
	}
	if (c != STDIN_FILENO && c != STDOUT_FILENO)
	    Sys::close(c);
	HylaFAXServer::setupLogging();	// reopen syslog before chroot
	app->open();			// opening greeting
	break;
    case -1:				// fork failure
	logError("HylaFAX %s: Cannot fork: %m", (const char*) kind);
	break;
    default:				// parent
	Sys::close(c);
	Dispatcher::instance().startChild(pid, this);
	break;
    }
    return (0);				// indicate data was consumed
}

void
SuperServer::childStatus(pid_t, int)
{
    /*
     * Nothing to do here - childStatus means it's already been reaped, and
     * thus off the queue from the Dispatcher
     */
}
