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
#include "port.h"
#include "Sys.h"
#include "config.h"

#include "HylaFAXServer.h"
#include "Dispatcher.h"
#include "ModemExt.h"
#include "JobExt.h"
#include "FaxRecvInfo.h"
#include "FaxSendInfo.h"

/*
 * Setup a trigger to monitor server events and
 * return ascii/binary event information on a
 * data connection.
 */
void
HylaFAXServer::triggerCmd(const char* fmt ...)
{
    /*
     * We setup the connection before registering the
     * trigger to reduce lost event reports.  If we setup
     * the trigger first then events could come in and
     * be discarded while we were setting up the data
     * connection.  Either way we may still lose some
     * events if, for example, the client wants to
     * fully monitor a submitted job using a single
     * control channel.  The only way to reliably do
     * this is to register a trigger before a job is
     * submitted (sigh).
     */
    int code;
    FILE* dout = openDataConn("w", code);
    if (dout != NULL) {
	va_list ap;
	va_start(ap, fmt);
	trigSpec = fxStr::vformat(fmt, ap);
	va_end(ap);
	reply(code, "%s for trigger \"%s\".", dataConnMsg(code),
	    (const char*) trigSpec);
	state |= S_LOGTRIG;			// force events to be reported
	fxStr emsg;
	if (loadTrigger(emsg)) {
	    if (setjmp(urgcatch) == 0) {
		state |= S_TRANSFER;
		/*
		 * The only way out of this is a client
		 * ABORt request or if the control channel
		 * connection is broken.
		 */
		for (;;)
		    Dispatcher::instance().dispatch();
	    }
	    state &= ~S_TRANSFER;
	    (void) cancelTrigger(emsg);
	} else
	    reply(504, "Cannot register trigger: %s.", (const char*) emsg);
	state &= ~S_LOGTRIG;
	closeDataConn(dout);
    }
}

/*
 * Create a trigger for the specified events
 * and return the identifier sent back by the
 * scheduler.
 */
fxBool
HylaFAXServer::newTrigger(fxStr& emsg, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fxBool b = vnewTrigger(emsg, fmt, ap);
    va_end(ap);
    return (b);
}
fxBool
HylaFAXServer::vnewTrigger(fxStr& emsg, const char* fmt, va_list ap)
{
    trigSpec = fxStr::vformat(fmt, ap);
    return (loadTrigger(emsg));
}

/*
 * Send the current trigger specification to the scheduler
 * and stash the returned trigger ID.  This is used to
 * create triggers the first time as well as to re-load an
 * active trigger if the scheduler is restarted.
 */
fxBool
HylaFAXServer::loadTrigger(fxStr& emsg)
{
    if (sendQueuerACK(emsg, "T%s", (const char*) trigSpec) &&
      fifoResponse.length() > 2) {
	tid = atoi(&fifoResponse[2]);
	return (TRUE);
    } else
	return (FALSE);
}

/*
 * Inform the spooler that it should cancel
 * a previously created trigger.
 */
fxBool
HylaFAXServer::cancelTrigger(fxStr& emsg)
{
    trigSpec = "";				// avoid spontaneous reloads
    return sendQueuer(emsg, "D%u", tid);
}

/*
 * Handle a trigger event received while waiting
 * for a job or group of jobs.  The status information
 * is sent back to the client over the data channel
 * if S_LOGTRIG is set in the server state.
 */
void
HylaFAXServer::triggerEvent(const TriggerMsgHeader& h, const char* cp)
{
#define	EventType(e)	((e)>>4)
    int evt = EventType(h.event);
    if (evt == EventType(JOB_BASE) || evt == EventType(SEND_BASE)) {
	JobExt job;
	cp = job.decode(cp);
	/*
	 * Propagate job state for clients that are doing
	 * things like waiting for a job to complete.  This
	 * may be fairly costly if someone traces all jobs
	 * as it'll cause the in-memory job database to grow.
	 */
	fxStr emsg;
	Job* jp = findJob(job.jobid, emsg);
	if (jp) {
	    jp->tts = job.tts;
	    jp->killtime = job.killtime;
	    jp->state = job.state;
	    jp->pri = job.pri;
	    jp->commid = job.commid;
	}
	if (IS(LOGTRIG) && type == TYPE_A) {
	    if (evt == EventType(JOB_BASE))
		logJobEventMsg(h, job);
	    else
		logSendEventMsg(h, job, cp);
	}
    } else if (evt == EventType(RECV_BASE)) {
	FaxRecvInfo ri;
	ri.decode(cp);
	/*
	 * Update/record receive queue status.
	 */
	RecvInfo* rip = recvq[ri.qfile];
	if (!rip)
	    recvq[ri.qfile] = rip = new RecvInfo(ri.qfile);
	if (rip->recvTime == 0)
	    rip->recvTime = h.tstamp;
	if (h.event == Trigger::RECV_END) {
	    rip->beingReceived = FALSE;
	    rip->reason = ri.reason;
	} else
	    rip->beingReceived = TRUE;
	rip->sender = ri.sender;
	rip->subaddr = ri.subaddr;
	rip->npages = ri.npages;
	rip->time = h.tstamp - rip->recvTime;
	rip->commid = ri.commid;
	if (IS(LOGTRIG) && type == TYPE_A)
	    logRecvEventMsg(h, ri, cp);
    } else if (evt == EventType(MODEM_BASE)) {
	ModemExt modem;
	cp = modem.decode(cp);
	if (IS(LOGTRIG) && type == TYPE_A)
	    logModemEventMsg(h, modem, cp);
    } else {
	logError("Unrecognized trigger event message %u.", h.event);
	return;
    }
    if (IS(LOGTRIG) && type == TYPE_I) {
	/*
	 * Binary trigger logging causes the raw event
	 * stream to pass through to the user.  This is
	 * probably not a great idea as it causes clients
	 * to be aware of the server's internal message
	 * formats but for some applications it may be
	 * appropriate.  We support it now to better
	 * understand the implications.
	 */
	(void) Sys::write(data, cp - sizeof (h), h.length);
    }
#undef EventType
}

/*
 * Ascii Trigger Event Logging Support.
 */
#define	unknownEvent	"Unknown event"		// XXX for HP CC

void
HylaFAXServer::logEventMsg(const TriggerMsgHeader& h, fxStr& msg)
{
    struct tm& tm = *cvtTime(h.tstamp);
    msg.insert(fxStr::format("%02d%02d%02d %d "
	, tm.tm_hour
	, tm.tm_min
	, tm.tm_sec
	, 100+h.event
    ));
    msg.append("\r\n");
    (void) Sys::write(data, msg, msg.length());
}

/*
 * Format and log a job event.
 */
void
HylaFAXServer::logJobEventMsg(const TriggerMsgHeader& h, const JobExt& job)
{
    static const char* jobNames[16] = {
	"Created",
	"Suspended",
	"Ready to send",
	"Sleeping awaiting time-to-send",
	"Marked dead",
	"Being processed by scheduler",
	"Corpus reaped",
	"Activated",
	"Rejected",
	"Killed",
	"Blocked by another job",
	"Delayed by time-of-day restriction or similar",
	"Parameters altered",
	"Timed out",
	"Preparation started",
	"Preparation finished",
    };
    fxStr msg = fxStr::format("JOB %s (dest %s pri %u com %s): %s"
	, (const char*) job.jobid
	, (const char*) job.dest
	, job.pri
	, (const char*) job.commid
	, jobNames[h.event&0xf]
    );
    logEventMsg(h, msg);
}

static void
addParams(fxStr& msg, const Class2Params& params)
{
    msg.append(fxStr::format("<%s, %s, %s, %s, %s>"
	, (params.ln == LN_A4 ? "A4" : params.ln == LN_B4 ? "B4" : "INF")
	, params.verticalResName()
	, params.dataFormatName()
	, params.bitRateName()
	, params.scanlineTimeName()
    ));
}

/*
 * Format and log a send event.
 */
void
HylaFAXServer::logSendEventMsg(const TriggerMsgHeader& h, const JobExt& job, const char* cp)
{
    fxStr msg = fxStr::format("JOB %s (dest %s pri %u com %s): SEND FAX: "
	, (const char*) job.jobid
	, (const char*) job.dest
	, job.pri
	, (const char*) job.commid
    );
    if (h.event != Trigger::SEND_POLLRCVD) {
	static const char* sendNames[16] = {
	    "Begin attempt",			// SEND_BEGIN
	    "Call placed (off-hook)",		// SEND_CALL
	    "Connected to remote device",	// SEND_CONNECTED
	    unknownEvent,			// SEND_PAGE
	    unknownEvent,			// SEND_DOC
	    unknownEvent,			// SEND_POLLRCVD
	    unknownEvent,			// SEND_POLLDONE
	    "Finished attempt",			// SEND_END
	    "Reformat documents because of capabilities mismatch",
	    "Requeue job",			// SEND_REQUEUE
	    "Job completed successfully",	// SEND_DONE
	    unknownEvent,
	    unknownEvent,
	    unknownEvent,
	    unknownEvent,
	    unknownEvent
	};
	FaxSendInfo si;
	switch (h.event) {
	case Trigger::SEND_PAGE:		// page sent
	    si.decode(cp);
	    msg.append(fxStr::format("Page %u sent in %s (file %s) "
		, si.npages
		, fmtTime(si.time)
		, (const char*) si.qfile
	    ));
	    addParams(msg, si.params);
	    break;
	case Trigger::SEND_DOC:			// document sent
	    si.decode(cp);
	    msg.append(fxStr::format("Document sent in %s (file %s)"
		, fmtTime(si.time)
		, (const char*) si.qfile
	    ));
	    break;
	case Trigger::SEND_POLLDONE:		// polling operation done
	    si.decode(cp);
	    msg.append(fxStr::format("Poll completed in %s (file %s)"
		, fmtTime(si.time)
		, (const char*) si.qfile
	    ));
	    break;
	default:
	    msg.append(sendNames[h.event&15]);
	    break;
	}
    } else {
	FaxRecvInfo ri;
	ri.decode(cp);
	msg.append(fxStr::format("Recv polled document from %s, %u pages in %s, file %s"
	    , (const char*) ri.sender
	    , ri.npages
	    , fmtTime((time_t) ri.time)
	    , (const char*) ri.qfile
	));
    }
    logEventMsg(h, msg);
}

/*
 * Format and log a modem event.
 */
void
HylaFAXServer::logModemEventMsg(const TriggerMsgHeader& h, const ModemExt& modem, const char* cp)
{
    static const char* modemNames[16] = {
	"Assigned to job",
	"Released by job",
	"Marked down",
	"Marked ready",
	"Marked busy",
	"Considered wedged",
	"In-use by an outbound job",
	"Inbound data call begin",
	"Inbound data call completed",
	"Inbound voice call begin",
	"Inbound voice call completed",
	"Caller-id information: ",
	unknownEvent,
	unknownEvent,
	unknownEvent,
	unknownEvent
    };
    fxStr msg = fxStr::format("MODEM %s: %s"
	, (const char*) modem.devID
	, modemNames[h.event&15]
    );
    if (h.event == Trigger::MODEM_CID)
	msg.append(cp);
    logEventMsg(h, msg);
}

/*
 * Format and log a facsimile receive event.
 */
void
HylaFAXServer::logRecvEventMsg(const TriggerMsgHeader& h, const FaxRecvInfo& ri, const char*)
{
    fxStr msg = fxStr::format("RECV FAX: ");
    switch (h.event) {
    case Trigger::RECV_BEGIN:
	msg.append("Call started");
	break;
    case Trigger::RECV_END:
	msg.append("Call ended");
	break;
    case Trigger::RECV_START:
	msg.append(fxStr::format("Session started (com %s), TSI \"%s\" "
	    , (const char*) ri.commid
	    , (const char*) ri.sender
	));
	addParams(msg, ri.params);
	break;
    case Trigger::RECV_PAGE:
	msg.append(fxStr::format("From %s (com %s), page %u in %s "
	    , (const char*) ri.sender
	    , (const char*) ri.commid
	    , ri.npages
	    , fmtTime((time_t) ri.time)
	));
	addParams(msg, ri.params);
	break;
    case Trigger::RECV_DOC:
	msg.append(fxStr::format("From %s (com %s), %u pages in %s, file %s"
	    , (const char*) ri.sender
	    , (const char*) ri.commid
	    , ri.npages
	    , fmtTime((time_t) ri.time)
	    , (const char*) ri.qfile
	));
	break;
    default:
	msg.append(unknownEvent);
	break;
    }
    logEventMsg(h, msg);
}
