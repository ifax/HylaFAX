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
#include "faxApp.h"
#include "HylaClient.h"
#include "Sys.h"
#include "Dispatcher.h"
#include "Trigger.h"
#include "config.h"

#include <errno.h>

fxIMPLEMENT_StrKeyPtrValueDictionary(HylaClientDict, HylaClient*)

tseq_t	HylaClient::lruseq = 0;
HylaClientDict HylaClient::clients;
HylaClient::SchedReaper HylaClient::schedReaper;

HylaClient::HylaClient(const fxStr& fn) : fifoName(fn)
{
    fifo = -1;
    refs = 0;
    seqnum = 0;
    lrunum = lruseq;
    reap = false;

    clients[fn] = this;
}

HylaClient::~HylaClient()
{
    if (refs != 0)
	logError("Client deleted with %u refs; FIFO " | fifoName, refs);
    if (fifo != -1)
	Sys::close(fifo);
    clients.remove(fifoName);
}

/*
 * Return a HylaClient for the process listening on
 * the specified FIFO name. 
 */
HylaClient&
HylaClient::getClient(const fxStr& name)
{
    HylaClient* hc = clients[name];
    return (hc ? *hc : *new HylaClient(name));
}

/*
 * Send a message to the process listening on the
 * client's FIFO.  If an error is encountered the
 * client is scheduled to be purged at the next
 * opportune time.
 */
bool
HylaClient::send(const char* msg, u_int msgLen)
{
     if (reap)					// ignore if marked for reaping
	return (false);
     seqnum++;					// count message
again:
    if (fifo < 0) {
#ifdef FIFOSELECTBUG
	/*
	 * We try multiple times to open the appropriate FIFO
	 * file because the system has a kernel bug that forces
	 * the server to close+reopen the FIFO file descriptors
	 * for each message received on the FIFO (yech!).
	 */
	int tries = 0;
	do {
	    if (tries > 0)
		sleep(1);
	    fifo = Sys::open(fifoName, O_WRONLY|O_NDELAY);
	} while (fifo == -1 && errno == ENXIO && ++tries < 5);
#else
	fifo = Sys::open(fifoName, O_WRONLY|O_NDELAY);
#endif
	if (fifo < 0) {
	    if (errno == EMFILE && reapFIFO())
		goto again;
	    logError("HylaClient::send: %s: Cannot open FIFO: %m",
		(const char*) fifoName);
	    schedReap();
	    return (false);
	}
	/*
	 * NB: We mark the descriptor for non-blocking i/o; this
	 *     is important to avoid potential deadlocks caused by
	 *     clients that block waiting to write a message into
	 *     our FIFO while we block trying to write into theirs.
	 *     We expect our FIFO to always be more full than theirs
	 *     and in addition it is critical that we never block
	 *     for any long period of time.
	 */
    }
    if (fifo >= 0) {
	int n = Sys::write(fifo, msg, msgLen);
	if (n == -1 && (errno == EBADF || errno == EPIPE)) {
	    Sys::close(fifo), fifo = -1;
	    schedReap();
	    return (false);
	}
	if ((unsigned) n != msgLen)
	    logError(
		"HylaClient::send: %s: write failed (return %d, seq# %u, %m)",
		(const char*) fifoName, seqnum, n);
	lrunum = lruseq++;			// update last use seqnum
    }
    return (true);
}

/*
 * Out of file descriptors for sending notification
 * messages; search the collection of triggers for
 * the oldest trigger with an open descriptor and
 * reclaim it.
 */
bool
HylaClient::reapFIFO()
{
    HylaClient* cand = NULL;
    u_int candage = 0;
    for (HylaClientDictIter iter(clients); iter.notDone(); iter++) {
	HylaClient* hc = iter.value();
	if (hc->fifo != -1) {
	    struct stat sb;
	    // cleanup deadwood while we're at it
	    if (Sys::fstat(hc->fifo, sb) != -1) {
		u_int age = lruseq - hc->lrunum;
		if (!cand || age > candage) {
		    candage = age;
		    cand = hc;
		}
	    } else
		hc->schedReap();
	}
    }
    if (cand) {
	Sys::close(cand->fifo);
	cand->fifo = -1;
	return (true);
    } else
	return (false);
}

void
HylaClient::purge()
{
    for (HylaClientDictIter iter(clients); iter.notDone(); iter++) {
	struct stat sb;
	HylaClient* hc = iter.value();
	if (hc->fifo != -1 && Sys::stat(hc->fifoName, sb) == -1) {
	    if (hc->refs > 0)
		Trigger::purgeClient(hc);
	    else
		delete hc;
	}
    }
}

void
HylaClient::schedReap()
{
    reap = true;
    schedReaper.start();
}

/*
 * Trigger Reaper Support; this class is used to
 * delete instances when the application is idle;
 * this is necesarry because we sometimes recognize
 * a client has gone away at a point where we cannot
 * "delete this".
 */

HylaClient::SchedReaper::SchedReaper() { started = false; }
HylaClient::SchedReaper::~SchedReaper() {}

void
HylaClient::SchedReaper::timerExpired(long, long)
{
    started = false;
    /*
     * Reclaim clients that have gone away.
     *
     * NB: the explicit reference to clients is required for the
     *     AIX compiler (XXX)
     */
    for (HylaClientDictIter iter(HylaClient::clients); iter.notDone(); iter++) {
	HylaClient* hc = iter.value();
	if (hc->reap) {
	    if (hc->refs > 0)
		Trigger::purgeClient(hc);
	    else
		delete hc;
	}
    }
}

/*
 * Set a timeout so that the reaper runs the
 * next time the dispatcher is invoked.
 */
void
HylaClient::SchedReaper::start()
{
    if (!started) {
	Dispatcher::instance().startTimer(0,1, this);
	started = true;
    }
}
