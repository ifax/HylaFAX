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
#include "Trigger.h"
#include "TriggerRef.h"
#include "HylaClient.h"
#include "Sys.h"
#include "Job.h"
#include "Modem.h"
#include "StackBuffer.h"
#include "config.h"

#include <ctype.h>
#include <errno.h>

/*
 * Trigger Support.
 */
#define	EventClass(e)	(e>>4)

u_int	Trigger::tidFree[TRIGGER_MAXWDS];
u_int	Trigger::tidRotor = 0;
Trigger* Trigger::triggers[TRIGGER_MAXTID];
QLink	Trigger::wildcards[TRIGGER_MAXEVENT>>4];

Trigger::Trigger(trid_t t, const fxStr& fifoName) : tid(t)
{
    refs = 0;
    interests[JOB_BASE>>4]   = 0;
    interests[SEND_BASE>>4]  = 0;
    interests[RECV_BASE>>4]  = 0;
    interests[MODEM_BASE>>4] = 0;
    client = &HylaClient::getClient(fifoName);
    client->inc();

    triggers[tid] = this;		// register in master table
}

Trigger::~Trigger()
{
    if (refs > 0)
	purgeWildRefs();
    if (refs != 0)			// NB: should be no other references
	logError("Trigger %u deleted with %u references", tid, refs);
    /*
     * Remove references to this trigger from the master
     * table and clear the tid from the allocation bitmap.
     * We release the reference on the client and if this
     * is the last reference to the client we purge it.
     */
    triggers[tid] = NULL;
    // NB: we mimic the logic below to avoid byte-order issues
    u_int i = tid / TRIGGER_BPW;
    ((u_char*) &tidFree[i])[i>>3] &= ~(1<<(i&7));
    client->dec();
}

/*
 * Remove all references to this that appear
 * on a reference list (used for wildcards).
 */
void
Trigger::purgeWildRefs()
{
    for (u_int i = 0; i < EventClass(TRIGGER_MAXEVENT) && refs > 0; i++) {
	QLink& w = wildcards[i];
	if (!w.isEmpty())
	    TriggerRef::purge(w, this);
    }
}

/*
 * Delete all the triggers associated
 * with the specified client.
 */
void
Trigger::purgeClient(HylaClient* hc)
{
    u_int refs = hc->refs;
    for (u_int w = 0; w < TRIGGER_MAXWDS; w++) {
	if (Trigger::tidFree[w] != 0) {
	    u_int n = TRIGGER_BPW-1; 
	    u_int i = w*TRIGGER_BPW;
	    do {
		Trigger* t = triggers[i++];
		if (t && t->client == hc) {
		    if (t->cancel())
			delete t;
		    if (--refs == 0)		// found all triggers
			return;
		    // check if this block is now empty
		    if (Trigger::tidFree[w] == 0)
			break;
		}
	    } while (--n);
	}
    }
    logError("Lost trigger for %s", (const char*) hc->fifoName);
}

/*
 * Create a trigger for the client process listening on
 * the specified FIFO.  The interests are given by the
 * specification string.
 */
void
Trigger::create(const fxStr& fifoName, const char* spec)
{
    trid_t tid = tidNextFree();
    if (tid == TRIGGER_MAXTID) {
	HylaClient::getClient(fifoName).send("T!", 3);
	logError("TRIGGER: tid table overflow");
    } else {
	Trigger* tr = new Trigger(tid, fifoName);
	if (!tr->parse(spec)) {
	    tr->send("T!");
	    delete tr;
	} else
	    tr->send("T*%u", tid);
    }
}

/*
 * Return a free trigger id if one is available.
 * If the table is full return TRIGGER_MAXTID
 */
trid_t
Trigger::tidNextFree()
{
    u_int r = tidRotor;
    if (tidFree[r] == (u_int) -1) {
	/*
	 * This word is full, move the rotor
	 * forward looking for a word with space.
	 */
	r = (r+1) % TRIGGER_MAXWDS;
	while (r != tidRotor && tidFree[r] == (u_int) -1)
	    r = (r+1) % TRIGGER_MAXWDS;
	if (r == tidRotor)
	    return (TRIGGER_MAXTID);
	tidRotor = r;
    }
    static u_char ffc[256] = {
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 7,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
        0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 8,
    };
    // locate free id in bitmask
    u_char* bp = (u_char*) &tidFree[r];
    r *= TRIGGER_BPW;
    if (bp[0] != 0xff) { bp += 0; r +=  0; } else
    if (bp[1] != 0xff) { bp += 1; r +=  8; } else
    if (bp[2] != 0xff) { bp += 2; r += 16; } else
    		       { bp += 3, r += 24; }
    u_char b = ffc[bp[0]];
    bp[0] |= 1<<b;
    return (r+b);
}

/*
 * Parse a trigger specification.  Syntax is:
 *
 *   [<class>['<'id'>'][<mask>|'*']]*
 *
 * <class> defines a class of events and is one of:
 *
 *   J	job-related events
 *   S	send-related events
 *   R	receive-related events
 *   M	modem-related events
 *
 * <mask> is a 4-hex-digit mask of trigger events as defined
 * in the Trigger class.  Bit 0 corresponds to <class>_BASE,
 * bit 1 to <class>_BASE+1, etc.  If '*' is specified then any
 * event in the class is matched.
 *
 * An <id> can be used to restrict matches to a specific
 * job or modem.  Eventually this will need to be generalized
 * for job groups.
 *
 * Thus an example specification that would catch any event
 * for the modem on ttyf2 would be ``M<ttyf2>*'', and to be
 * notified when job 1932 is requeued or completes one would
 * use ``J<1932>4c60''.
 */
fxBool
Trigger::parse(const char* spec0)
{
    const char* spec = spec0;
    while (*spec) {
	u_int base;
	switch (spec[0]) {
	case 'J':	base = JOB_BASE; break;
	case 'S':	base = SEND_BASE; break;
	case 'R':	base = RECV_BASE; break;
	case 'M':	base = MODEM_BASE; break;
	default:	spec++; continue;
	}
	const char* cp = spec+1;
	fxStr id;
	if (cp[0] == '<') {
	    const char* tp;
	    for (tp = ++cp; *tp != '>'; tp++)
		if (*tp == '\0') {			// XXX syntax error
		    syntaxError(spec0, "missing '>'");
		    return (FALSE);
		}
	    id = fxStr(cp, tp-cp);
	    cp = tp+1;
	}
	int c = *cp++;
	u_short& m = interests[base>>4];
	if (m != 0) {
	    syntaxError(spec0, "interests conflict");
	    return (FALSE);
	}
	if (c == '*') {
	    m = 0xffff;
	} else if (isxdigit(c)) {
	    u_int v = 0;
	    for (u_int i = 0; i < 4; i++) {
		u_int bits = isdigit(c) ? c-'0' :
		    (islower(c) ? 10+(c-'a') : 10+(c-'A'));
		v = (v<<4) | bits;
		c = *cp++;
	    } 
	    m = v;
	} else {
	     syntaxError(spec0, "non-hex event mask");
	     return (FALSE);
	}
	TriggerRef* tr = new TriggerRef(*this);
	/*
	 * Place the trigger reference on the appropriate list.
	 * If no modem or job is specified then we match any
	 * job/modem by placing the reference on the wildcards list.
	 * Otherwise we locate the job or modem and hook this
	 * reference to the list that hangs off the object.
	 */
	if (id == "") {
	    tr->insert(wildcards[EventClass(base)]);
	} else if (base == MODEM_BASE || base == RECV_BASE) {
	    /*
	     * NB: you can install a trigger on a modem before
	     *     the modem ``exists''; maybe this is bad?
	     */
	    tr->insert(Modem::getModemByID(id).triggers);
	} else if (base == JOB_BASE || base == SEND_BASE) {
	    Job* job = Job::getJobByID(id);
	    if (!job) {
		logError("TRIGGER: job %s does not exist", (const char*) id);
		return (FALSE);
	    }
	    tr->insert(job->triggers);
	}
	spec = cp;
    }
    return (TRUE);
}
void
Trigger::syntaxError(const char* spec, const char* msg)
{
    logError("TRIGGER: syntax error, %s in \"%s\"", msg, spec);
}

/*
 * Cancel (delete) the trigger with the specified id.
 */
fxBool
Trigger::cancel(const char* cp)
{
    trid_t tid = (trid_t) strtoul(cp, NULL, 10);
    if (tid < TRIGGER_MAXTID) {
	Trigger* t = triggers[tid];
	if (t) {
	    if (t->cancel())
		delete t;
	    return (TRUE);
	}
    }
    return (FALSE);
}

/*
 * Cancel a trigger.  Because a trigger may have references
 * scattered many places this can be hard.  If all the
 * references are on the wildcard lists then we can just
 * remove those and delete it.  Otherwise have to just
 * clear all the interests (so no future events will be
 * posted to the client) and wait for the object holding
 * the reference to go away.
 */
fxBool
Trigger::cancel()
{
    purgeWildRefs();			// wildcard references
    if (refs == 0)
	return (TRUE);
    if (interests[MODEM_BASE>>4] != 0) {
	/*
	 * Must explicitly search and purge references
	 * associated with a modem since modems are too
	 * long-lived to wait for them to be deleted.
	 */
	for (ModemIter iter(Modem::list); iter.notDone(); iter++) {
	    TriggerRef::purge(iter.modem().triggers, this);
	    if (refs == 0)
		return (TRUE);
	}
    }
    // clear interests so no more messages are sent
    memset(interests, 0, sizeof (interests));
    return (FALSE);
}

/*
 * Printf-like interface to send a trigger message.
 */
void
Trigger::send(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    fxStr msg(fxStr::vformat(fmt, ap));
    (void) client->send(msg, msg.length()+1);
    va_end(ap);
}

/*
 * Post an event to interested parties.
 */
void
Trigger::post(TriggerEvent e, const QLink& tr, const QLink& any, fxStackBuffer& msg)
{
    TriggerMsgHeader& hdr = *((TriggerMsgHeader*) &msg[0]);
    hdr.length = (u_short) msg.getLength();
    hdr.event = (u_short) e;
    hdr.tstamp = Sys::now();

    u_short mask = 1<<(e&15);
    const QLink* ql;
    for (ql = tr.next; ql != &tr; ql = ql->next) {
	TriggerRef* tr = (TriggerRef*) ql;
	if (tr->ref.interests[e>>4] & mask) {
	    HylaClient& client = *tr->ref.client;
	    hdr.seqnum = client.getSeqnum();
	    client.send(msg, hdr.length);
	}
    }
    for (ql = any.next; ql != &any; ql = ql->next) {
	TriggerRef* tr = (TriggerRef*) ql;
	if (tr->ref.interests[e>>4] & mask) {
	    HylaClient& client = *tr->ref.client;
	    hdr.seqnum = client.getSeqnum();
	    client.send(msg, hdr.length);
	}
    }
}

static TriggerMsgHeader hdr = { '!', ' ' };

void
Trigger::post(TriggerEvent e, const Job& job, const char* info)
{
    const QLink& any = wildcards[EventClass(e)];
    if (!job.triggers.isEmpty() || !any.isEmpty()) {
	fxStackBuffer msg;
	msg.put((const char*) &hdr, sizeof (hdr));
	job.encode(msg);
	if (info)
	    msg.put(info);
	post(e, job.triggers, any, msg);
    }
}
void
Trigger::post(TriggerEvent e, const Modem& modem, const char* info)
{
    const QLink& any = wildcards[EventClass(e)];
    if (!modem.triggers.isEmpty() || !any.isEmpty()) {
	fxStackBuffer msg;
	msg.put((const char*) &hdr, sizeof (hdr));
	if (info)
	    msg.put(info);
	else
	    modem.encode(msg);
	post(e, modem.triggers, any, msg);
    }
}
