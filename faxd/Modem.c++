/*	$Id$ */
/*
 * Copyright (c) 1990-1996 Sam Leffler
 * Copyright (c) 1991-1996 Silicon Graphics, Inc.
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

#include <errno.h>

#include "Modem.h"
#include "UUCPLock.h"
#include "REDict.h"
#include "TriggerRef.h"
#include "Dispatcher.h"
#include "config.h"

REDict* ModemGroup::classes =  NULL;	// modem classes

RE*
ModemGroup::find(const char* name)
{
    if (classes == NULL)
	return (NULL);
    const REPtr* re = classes->find(name);
    return (re ? (RE*) *(REPtr*) re : (RE*) NULL);
}

void
ModemGroup::reset()
{
    delete classes, classes = NULL;
}

void
ModemGroup::set(const fxStr& name, RE* re)
{
    if (classes == NULL)
	classes = new REDict;
    (*classes)[name] = re;
}

ModemLockWaitHandler::ModemLockWaitHandler(Modem& m) : modem(m) {}
ModemLockWaitHandler::~ModemLockWaitHandler() {}
void ModemLockWaitHandler::timerExpired(long, long)
    { faxQueueApp::instance().pollForModemLock(modem); }

QLink Modem::list;			// master list of known modems

Modem::Modem(const fxStr& id)
    : fifoName(FAX_FIFO "." | id)
    , devID(id)
    , lockHandler(*this)
{
    state = DOWN;			// modem down until notified otherwise
    canpoll = true;			// be optimistic
    fd = -1;				// force open on first use
    priority = 255;			// lowest priority
    insert(list);			// place at end of master list
    lock = faxQueueApp::instance().getUUCPLock(faxApp::idToDev(id));
}

Modem::~Modem()
{
    stopLockPolling();
    delete lock;
    if (fd >= 0)
	Sys::close(fd);
    remove();
    if (!triggers.isEmpty())		// purge trigger references
	TriggerRef::purge(triggers);
}

Modem*
Modem::modemExists(const fxStr& id)
{
    for (ModemIter iter(list); iter.notDone(); iter++) {
	Modem& modem = iter;
	if (modem.devID == id)
	    return (&modem);
    }
    return (NULL);
}

/*
 * Given a modem device-id, return a reference
 * to a Modem instance.  If no instance exists,
 * one is created and added to the list of known
 * modems.
 */
Modem&
Modem::getModemByID(const fxStr& id)
{
    Modem* modem = modemExists(id);
    return *(modem ? modem : new Modem(id));
}

/*
 * Is the modem capable of handling the job.
 */
bool
Modem::isCapable(const Job& job) const
{
    if (job.willpoll && !canpoll)
	return (false);
    if (job.pagewidth && !supportsPageWidthInMM(job.pagewidth))
	return (false);
    if (job.pagelength && !supportsPageLengthInMM(job.pagelength))
	return (false);
    if (job.resolution && !supportsVRes(job.resolution))
	return (false);
    return (true);
}

/*
 * Find a modem that is capable of handling
 * work associated with the specified job.
 */
Modem*
Modem::findModem(const Job& job, const DestControlInfo& dci)
{
    RE* c = ModemGroup::find(job.device);
    if (c) {
	const fxStr& mdci = dci.getModem();
	RE* cdci = mdci != "" ? ModemGroup::find(mdci) : NULL;
	int loops = 2;

	/*
	 * At first try to find modem strictly (suitable to job and destination rules)
	 * Then try to find modem not strictly (suitable to job rules only)
	 */

	for (int i = 0 ; i < loops ; i++) {
	    /*
	     * Job is assigned to a class of modems; search
	     * the set of modems in the class according to
	     * the order specified (if any order is specified).
	     */
	    for (ModemIter iter(list); iter.notDone(); iter++) {
		Modem& modem = iter;
		if (c->Find(modem.devID) && modem.isCapable(job)) {
		    if (i == 0) {
			if (cdci) {			// destination assigned to a class of modems
			    if (!cdci->Find(modem.devID))
				continue;
			} else if (mdci != "") {	// destination assigned to an explicit modem
			    if (mdci != modem.devID)
				continue;
			}
			loops = 1;			// there is a strictly suitable modem
		    }

		    if (modem.getState() != Modem::READY) {
			continue;
		    }

		    /*
		     * Move modem to the end of the priority group
		     */

		    modem.remove();

		    if (!list.isEmpty()) {
			ModemIter iter(list);

			for ( ; iter.notDone(); iter++) {
			    if (iter.modem().priority > modem.priority)
				break;
			}
			modem.insert(iter.modem());
		    } else
			modem.insert(list);

		    return (&modem);
		}
	    }
	}
    } else {
	/*
	 * Job is assigned to an explicit modem or to an
	 * invalid class or modem.  Look for the modem
	 * in the list of known modems. 
	 */
	for (ModemIter iter(list); iter.notDone(); iter++) {
	    Modem& modem = iter;
	    if (modem.getState() != Modem::READY)
		continue;
	    if (job.device != modem.devID)
		continue;
	    return (modem.isCapable(job) ? &modem : (Modem*) NULL);
	}
    }
    return (NULL);
}

/*
 * Assign a modem for use by a job.
 */
bool
Modem::assign(Job& job)
{
    if (lock->lock()) {		// lock modem for use
	state = BUSY;		// mark in use
	job.modem = this;	// assign modem to job
	send("L", 2, false);
	return (true);
    } else {
	/*
	 * Modem is locked for use by an outbound task.
	 * This should only happen when operating in a
	 * send-only environment--a modem is presumed
	 * ready for use, only to discover when it's
	 * actually assigned that it's really busy.
	 * We mark the modem BUSY here so that if the
	 * caller requests another modem we won't try
	 * to re-assign it in findModem.
	 */
	state = BUSY;		// mark in use
	return (false);
    }
}

/*
 * Release a previously assigned modem.
 */
void
Modem::release()
{
    lock->unlock();
    /*
     * We must mark the modem READY when releasing the lock
     * because we cannot depend on the faxgetty process 
     * notifying us if/when the modem status changes.  This
     * may result in overzealous scheduling of the modem, but
     * since sender apps are expected to stablize the modem
     * before starting work it shouldn't be too bad.
     */
    state = READY;
}

/*
 * UUCP lock file polling support.  When a modem is not
 * monitored by a faxgetty process outbound modem usage
 * is ``discovered'' when we attempt to assign a modem
 * to a job.  At that time we mark the modem BUSY and
 * kick off a polling procedure to watch for when the
 * lock file is removed; at which time we mark the modem
 * READY again and poke the scheduler in case jobs are
 * waiting for a modem to come ready again.
 */
void
Modem::startLockPolling(long sec)
{
    Dispatcher::instance().startTimer(sec, 0, &lockHandler);
}

void
Modem::stopLockPolling()
{
    Dispatcher::instance().stopTimer(&lockHandler);
}

void
Modem::setCapabilities(const char* s)
{
    canpoll = (s[0] == 'P');			// P/p for polling/no polling
    char* tp;
    caps.decodeCaps((u_int) strtoul(s+1, &tp, 16));// fax capabilities
    if (tp && *tp == ':') {			// modem priority
	u_int pri = (u_int) strtoul(tp+1, NULL, 16);
	if (pri != priority) {
	    /*
	     * Priority changed, move modem so that the list remains
	     * sorted by priority (highest priority to lowest priority).
	     */
	    remove();
	    priority = pri;
	    if (!list.isEmpty()) {
		ModemIter iter(list);
		do {
		    if (iter.modem().priority > pri)
			break;
		    iter++;
		} while (iter.notDone());
		insert(iter.modem());
	    } else
		insert(list);
	}
    }
    setState(READY);		// XXX needed for static configuration
}

void Modem::setNumber(const char* cp)		{ number = cp; }
void Modem::setCommID(const char* cp)		{ commid = cp; }
void Modem::setState(ModemState s)		{ state = s; }

/*
 * Return whether or not the modem supports the
 * specified page width.  We perhaps should accept
 * page width when large page sizes are supported
 * (but then the caller would need to know in order
 * to pad the image to the appropriate width).
 */
bool
Modem::supportsPageWidthInMM(u_int w) const
{
    if (w <= 110)		// 864 pixels + slop
	return caps.wd & BIT(WD_864);
    else if (w <= 154)		// 1216 pixels + slop
	return caps.wd & BIT(WD_1216);
    else if (w <= 218)		// 1728 pixels + slop
	return caps.wd & BIT(WD_1728);
    else if (w <= 258)		// 2048 pixels + slop
	return caps.wd & BIT(WD_2048);
    else if (w <= 306)		// 2432 pixels + slop
	return caps.wd & BIT(WD_2432);
    else
	return false;
}

bool
Modem::supportsPageWidthInPixels(u_int w) const
{
    if (w <= 880)		// 864 pixels + slop
	return caps.wd & BIT(WD_864);
    else if (w <= 1232)		// 1216 pixels + slop
	return caps.wd & BIT(WD_1216);
    else if (w <= 1744)		// 1728 pixels + slop
	return caps.wd & BIT(WD_1728);
    else if (w <= 2064)		// 2048 pixels + slop
	return caps.wd & BIT(WD_2048);
    else if (w <= 2448)		// 2432 pixels + slop
	return caps.wd & BIT(WD_2432);
    else
	return false;
}

/*
 * Return whether or not the modem supports the
 * specified vertical resolution.  Note that we're
 * rather tolerant because of potential precision
 * problems and general sloppiness on the part of
 * applications writing TIFF files.
 */
bool
Modem::supportsVRes(float res) const
{
    if (75 <= res && res < 120)
	return (true);		// all fax modems must support vr = 0
    else if (150 <= res && res < 250)
	return (caps.vr & VR_FINE || caps.vr & VR_200X200);
    else if (250 <= res && res < 350)
	return caps.vr & VR_300X300;
    else if (350 <= res && res < 500)
	return (caps.vr & VR_R8 || caps.vr & VR_200X400 || caps.vr & VR_R16);
    else
	return false;
}

/*
 * Return whether or not the modem supports the
 * specified VR setting.
 */
bool
Modem::supportsVR(u_int r) const
{
        return caps.vr & r;
}

/*
 * Return whether or not the modem supports 2DMR.
 */
bool
Modem::supports2D() const
{
    return caps.df & BIT(DF_2DMR);
}

/*
 * Return whether or not the modem supports 2DMMR.
 */
bool
Modem::supportsMMR() const
{
    return caps.df & BIT(DF_2DMMR);
}

/*
 * Return whether or not the modem supports the
 * specified page length.  As above for vertical
 * resolution we're lenient in what we accept.
 */
bool
Modem::supportsPageLengthInMM(u_int l) const
{
    // XXX probably need to be more forgiving with values
    if (270 < l && l <= 330)
	return caps.ln & (BIT(LN_A4)|BIT(LN_INF));
    else if (330 < l && l <= 390)
	return caps.ln & (BIT(LN_B4)|BIT(LN_INF));
    else
	return caps.ln & BIT(LN_INF);
}

/*
 * Broadcast a message to all known modems.
 */
void
Modem::broadcast(const fxStr& msg)
{
    for (ModemIter iter(list); iter.notDone(); iter++) {
	/*
	 * NB: We rarely send msgs, so for now close after each use.
	 *     +1 here is so the \0 is included in the message.
	 */
	iter.modem().send(msg, msg.length()+1, false);
    }
}

/*
 * Send a message to the process managing a modem.
 */
bool
Modem::send(const char* msg, u_int msgLen, bool cacheFd)
{
    bool retry = true;
again:
    if (fd < 0) {
	fd = Sys::open(fifoName, O_WRONLY|O_NDELAY);
	if (fd < 0) {
#ifdef notdef
	    /*
	     * NB: We don't generate a message here because this
	     *     is expected when faxgetty is not running.
	     */
	    logError("MODEM " | devID | ": Cannot open FIFO: %m");
#endif
	    return (false);
	}
    }
    int n = Sys::write(fd, msg, msgLen);
    if (n == -1) {
	if (errno == EBADF && retry) {		// cached descriptor bad, retry
	    retry = false;
	    Sys::close(fd), fd = -1;
	    goto again;
	}
	logError("MODEM " | devID | ": Cannot send msg \"%.*s\"", msgLen, msg);
    }
    if (!cacheFd)
	Sys::close(fd), fd = -1;
    return ((unsigned)n == msgLen);
}

#include "StackBuffer.h"

void
Modem::encode(fxStackBuffer& buf) const
{
    buf.put(devID,  devID.length()+1);
    buf.put(number, number.length()+1);
    buf.put(commid, commid.length()+1);

    switch (state) {
    case DOWN:	buf.put('D'); break;
    case BUSY:	buf.put('B'); break;
    case READY:	buf.put('R'); break;
    }
    buf.put(canpoll ? 'P' : 'p');
    u_int ec = caps.encodeCaps();
    buf.put((const char*) &ec, sizeof (u_int));
    buf.put((const char*) &priority, sizeof (u_short));
}
