/*	$Id$ */
/*
 * Copyright (c) 1994-1996 Sam Leffler
 * Copyright (c) 1994-1996 Silicon Graphics, Inc.
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
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/file.h>
#include <signal.h>
#include <ctype.h>

#include "FaxMachineInfo.h"
#include "FaxAcctInfo.h"
#include "UUCPLock.h"
#include "pageSendApp.h"
#include "FaxRequest.h"
#include "Dispatcher.h"
#include "StackBuffer.h"
#include "Sys.h"
#include "ixo.h"

#include "config.h"

/*
 * Send messages with IXO/TAP protocol.
 */

pageSendApp* pageSendApp::_instance = NULL;

pageSendApp::pageSendApp(const fxStr& devName, const fxStr& devID)
    : ModemServer(devName, devID)
{
    ready = false;
    modemLock = NULL;
    setupConfig();

    fxAssert(_instance == NULL, "Cannot create multiple pageSendApp instances");
    _instance = this;
}

pageSendApp::~pageSendApp()
{
    delete modemLock;
}

pageSendApp& pageSendApp::instance() { return *_instance; }

void
pageSendApp::initialize(int argc, char** argv)
{
    ModemServer::initialize(argc, argv);
    faxApp::initialize(argc, argv);

    // NB: must do last to override config file information
    for (GetoptIter iter(argc, argv, getOpts()); iter.notDone(); iter++)
	switch (iter.option()) {
	case 'l':			// do uucp locking
	    modemLock = getUUCPLock(getModemDevice());
	    break;
	case 'c':			// set configuration parameter
	    readConfigItem(iter.optArg());
	    break;
	}
}

void
pageSendApp::open()
{
    ModemServer::open();
    faxApp::open();
}

void
pageSendApp::close()
{
    if (isRunning()) {
	if (state == ModemServer::SENDING) {
	    /*
	     * Terminate the active job and let the send
	     * operation complete so that the transfer is
	     * logged and the appropriate exit status is
	     * returned to the caller.
	     */
	    ModemServer::abortSession();
	} else {
	    ModemServer::close();
	    faxApp::close();
	}
    }
}

FaxSendStatus
pageSendApp::send(const char* filename)
{
    int fd = Sys::open(filename, O_RDWR);
    if (fd >= 0) {
	if (flock(fd, LOCK_EX) >= 0) {
	    FaxRequest* req = new FaxRequest(filename, fd);
	    bool reject;
	    if (req->readQFile(reject) && !reject) {
		if (req->findRequest(FaxRequest::send_page) != fx_invalidArrayIndex) {
		    FaxMachineInfo info;
		    info.updateConfig(canonicalizePhoneNumber(req->number));
		    FaxAcctInfo ai;

		    ai.start = Sys::now();

		    sendPage(*req, info);

		    ai.jobid = req->jobid;
		    ai.jobtag = req->jobtag;
		    ai.user = req->mailaddr;
		    ai.duration = Sys::now() - ai.start;
		    ai.conntime = getConnectTime();
		    ai.commid = req->commid;
		    ai.device = getModemDeviceID();
		    ai.dest = req->external;
		    ai.csi = "";
		    ai.params = 0;
		    if (req->status == send_done)
			ai.status = "";
		    else
			ai.status = req->notice;
		    if (!ai.record("PAGE"))
			logError("Error writing %s accounting record, dest=%s",
			    "PAGE", (const char*) ai.dest);
		} else
		    sendFailed(*req, send_failed, "Job has no PIN to send to");
		req->writeQFile();		// update on-disk copy
		return (req->status);		// return status for exit
	    } else
		delete req;
	    logError("Could not read request file");
	} else
	    logError("Could not lock request file: %m");
	Sys::close(fd);
    } else
	logError("Could not open request file: %m");
    return (send_failed);
}

void
pageSendApp::sendPage(FaxRequest& req, FaxMachineInfo& info)
{
    if (lockModem()) {
	beginSession(req.number);
	req.commid = getCommID();
	traceServer("SEND PAGE: JOB %s DEST %s COMMID %s"
	    , (const char*) req.jobid
	    , (const char*) req.external
	    , (const char*) req.commid
	);
	/*
	 * Setup tty parity; per-destination information takes
	 * precedence over command-line arguments/config params;
	 * otherwise the IXO/TAP spec is used (set below).
	 */
	if (info.getPagerTTYParity() != "")
	    pagerTTYParity = info.getPagerTTYParity();
	// NB: may need to set tty baud rate here XXX
	if (setupModem()) {
	    changeState(SENDING);
	    setServerStatus("Sending page " | req.jobid);
	    /*
	     * Construct the phone number to dial by applying the
	     * dialing rules to the user-specified dialing string.
	     */
	    fxStr msg;
	    if (prepareMsg(req, info, msg))
		sendPage(req, info, prepareDialString(req.number), msg);
	    changeState(MODEMWAIT);		// ...sort of...
	} else
	    sendFailed(req, send_retry, "Can not setup modem", 4*pollModemWait);
	discardModem(true);
	endSession();
	unlockModem();
    } else {
	sendFailed(req, send_retry, "Can not lock modem device",2*pollLockWait);
    }
}

bool
pageSendApp::prepareMsg(FaxRequest& req, FaxMachineInfo& info, fxStr& msg)
{
    u_int i = req.findRequest(FaxRequest::send_data);
    if (i == fx_invalidArrayIndex)		// page w/o text
	return (true);
    int fd = Sys::open(req.requests[i].item, O_RDONLY);
    if (fd < 0) {
	sendFailed(req, send_failed,
	    "Internal error: unable to open text message file");
	return (false);
    }
    struct stat sb;
    (void) Sys::fstat(fd, sb);
    msg.resize((u_int) sb.st_size);
    if (Sys::read(fd, &msg[0], (u_int) sb.st_size) != sb.st_size) {
	sendFailed(req, send_failed,
	    "Internal error: unable to read text message file");
	return (false);
    }
    Sys::close(fd);

    u_int maxMsgLen = info.getPagerMaxMsgLength();
    if (maxMsgLen == (u_int) -1)		// not set, use default
	maxMsgLen = pagerMaxMsgLength;
    if (msg.length() > maxMsgLen) {
	traceServer("Pager message length %u too large; truncated to %u",
	    msg.length(), maxMsgLen);
	msg.resize(maxMsgLen);
    }
    return (true);
}

void
pageSendApp::sendFailed(FaxRequest& req, FaxSendStatus stat, const char* notice, u_int tts)
{
    req.status = stat;
    req.notice = notice;
    /*
     * When requeued for the default interval (called with 3 args),
     * don't adjust the time-to-send field so that the spooler
     * will set it according to the default algorithm that 
     * uses the command-line parameter or requeueOther and a random jitter.
     */
    if (tts != 0)
	req.tts = Sys::now() + tts;
    traceServer("PAGE FAILED: %s", notice);
}

void
pageSendApp::sendPage(FaxRequest& req, FaxMachineInfo& info, const fxStr& number, const fxStr& msg)
{
    connTime = 0;				// indicate no connection
    if (!getModem()->dataService()) {
	sendFailed(req, send_failed, "Unable to configure modem for data use");
	return;
    }
    req.notice = "";
    fxStr notice;
    time_t pageStart = Sys::now();
    if (info.getPagerSetupCmds() != "")		// use values from info file
	pagerSetupCmds = info.getPagerSetupCmds();
    if (pagerSetupCmds != "")			// configure line speed, etc.
	(void) getModem()->atCmd(pagerSetupCmds);
    CallStatus callstat = getModem()->dial(number, notice);
    if (callstat == ClassModem::OK)
	connTime = Sys::now();			// connection start time
    (void) abortRequested();			// check for user abort
    if (callstat == ClassModem::OK && !abortCall) {
	req.ndials = 0;				// consec. failed dial attempts
	req.tottries++;				// total answered calls
	req.totdials++;				// total attempted calls
	info.setCalledBefore(true);
	info.setDialFailures(0);

	req.status = send_ok;			// be optimistic

	// from this point on, the treatment of the two protocols differs
	if (streq(info.getPagingProtocol(), "ixo")) {
	    sendIxoPage(req, info, msg, notice);
	} else if (streq(info.getPagingProtocol(), "ucp")) {
	    sendUcpPage(req, info, msg, notice);
	} else {
	    notice = req.notice | "; paging protocol unknown ";
	    sendFailed(req, send_failed, notice);
	    req.status = send_failed;
	}

	// here again, we have identical code
	if (req.status == send_ok) {
	    time_t now = Sys::now();
	    traceServer("SEND PAGE: FROM " | req.mailaddr
		| " TO " | req.external | " (sent in %s)",
		fmtTime(now - pageStart));
	    info.setSendFailures(0);
	} else {
	    info.setSendFailures(info.getSendFailures()+1);
	    info.setLastSendFailure(req.notice);
	}
    } else if (!abortCall) {
	/*
	 * Analyze the call status codes and selectively decide if the
	 * job should be retried.  We try to avoid the situations where
	 * we might be calling the wrong number so that we don't end up
	 * harrassing someone w/ repeated calls.
	 */
	req.ndials++;
	req.totdials++;			// total attempted calls
	switch (callstat) {
	case ClassModem::NOCARRIER:	// no carrier detected on remote side
	    /*
	     * Since some modems can not distinguish between ``No Carrier''
	     * and ``No Answer'' we offer this configurable hack whereby
	     * we'll retry the job <n> times in the face of ``No Carrier''
	     * dialing errors; if we've never previously reached a modem
	     * at that number.  This should not be used except if
	     * the modem is incapable of distinguishing between
	     * ``No Carrier'' and ``No Answer''.
	     */
	    if (!info.getCalledBefore() && req.ndials > noCarrierRetrys) {
		sendFailed(req, send_failed, notice);
		break;
	    }
	    /* fall thru... */
	case ClassModem::NODIALTONE:	// no local dialtone, possibly unplugged
	case ClassModem::ERROR:		// modem might just need to be reset
	case ClassModem::FAILURE:	// modem returned something unexpected
	case ClassModem::BUSY:		// busy signal
	case ClassModem::NOANSWER:	// no answer or ring back
	    sendFailed(req, send_retry, notice, requeueTTS[callstat]);
	    /* fall thru... */
	case ClassModem::OK:		// call was aborted by user
	    break;
	}
	if (callstat != ClassModem::OK) {
	    info.setDialFailures(info.getDialFailures()+1);
	    info.setLastDialFailure(req.notice);
	}
    }
    if (abortCall)
	sendFailed(req, send_failed, "Job aborted by user");
    else if (req.status == send_retry) {
	if (req.totdials == req.maxdials) {
	    notice = req.notice | "; too many attempts to dial";
	    sendFailed(req, send_failed, notice);
	} else if (req.tottries == req.maxtries) {
	    notice = req.notice | "; too many attempts to send";
	    sendFailed(req, send_failed, notice);
	}
    }
    /*
     * Cleanup after the call.  If we have new information on
     * the client's remote capabilities, the machine info
     * database will be updated when the instance is destroyed.
     */
    getModem()->hangup();
    /*
     * This may not be exact--the line may already have been
     * dropped--but it should be close enough unless the modem
     * gets wedged and the hangup work times out.  Also be
     * sure to register a non-zero amount of connect time so
     * that folks doing accounting can adjust charge-back costs
     * to reflect any minimum connect time tarrifs imposted by
     * their PTT (e.g. calls < 1 minute are rounded up to 1 min.)
     */
    if (connTime) {
	connTime = Sys::now() - connTime;
	if (connTime == 0)
	    connTime++;
    }
}
/*
 * here comes the IXO specific code for sendPage, search for the
 * string 'BEGIN UCP Support' for UCP specific code
 */
void
pageSendApp::sendIxoPage(FaxRequest& req, FaxMachineInfo& info, const fxStr& msg,
    fxStr& notice)
{
    if (pagePrologue(req, info, notice)) {
	while (req.requests.length() > 0) {	// messages
	    u_int i = req.findRequest(FaxRequest::send_page);
	    if (i == fx_invalidArrayIndex)
		break;
	    if (req.requests[i].item.length() == 0) {
		sendFailed(req, send_failed, "No PIN specified");
		break;
	    }
	    if (!sendPagerMsg(req, req.requests[i], msg, req.notice)) {
		/*
		 * On protocol errors retry more quickly
		 * (there's no reason to wait is there?).
		 */
		if (req.status == send_retry) {
		    req.tts = time(0) + requeueProto;
		    break;
		}
	    }
	    req.requests.remove(i);
	}
	if (req.status == send_ok)
	    (void) pageEpilogue(req, info, notice);
    } else
	sendFailed(req, req.status, notice, requeueProto);
}

u_int
pageSendApp::getResponse(fxStackBuffer& buf, long secs)
{
    buf.reset();
    if (secs) startTimeout(secs*1000);
    for (;;) {
	int c = getModemChar(0);
	if (c == EOF)
	    break;
	if (c == '\r' || c == '\003') {
	    if (buf.getLength() > 0)		// discard leading \r's or ETX
		break;
	} else if (c != '\n')			// discard all \n's
	    buf.put(c);
    }
    if (secs) stopTimeout("reading line from modem");
    if (buf.getLength() > 0)
	traceIXOCom("-->", (u_char*) (const char*) buf, buf.getLength());
    return (buf.getLength());
}

/*
 * Scan through a buffer looking for a potential
 * code byte return in a protocol response.
 * This is needed because some pager services such
 * as PageNet intersperse protocol messages and
 * verbose text messages.
 */
static bool
scanForCode(const u_char*& cp, u_int& len)
{
    if (len > 0) {
	do {
	    cp++, len--;
	} while (len > 0 &&
	    *cp != ACK && *cp != NAK && *cp != ESC && *cp != RS);
    }
    return (len > 0);
}

bool
pageSendApp::pagePrologue(FaxRequest& req, const FaxMachineInfo& info, fxStr& emsg)
{
    fxStackBuffer buf;
    time_t start;

    /*
     * Send \r and wait for ``ID='' response.
     * Repeat at 2 second intervals until a
     * response is received or ntries have
     * been done.
     */
    traceIXO("EXPECT ID (paging central identification)");
    start = Sys::now();
    bool gotID = false;
    do {
	putModem("\r", 1);
	if (getResponse(buf, ixoIDProbe) >= 3) {
	    // skip leading white space
	    const char* cp;
	    for (cp = buf; *cp && isspace(*cp); cp++)
		;
	    gotID = strneq(cp, "ID=", 3);
	}
	if (gotID) {
	    traceIXO("RECV ID (\"%.*s\")",
		buf.getLength(), (const char*) buf);
	} else
	    traceResponse(buf);
    } while (!gotID && (unsigned) Sys::now() - start < ixoIDTimeout);
    if (!gotID) {
	emsg = "No initial ID response from paging central";
	req.status = send_retry;
	return (false);
    }
    flushModemInput();			// paging central may send multiple ID=
    /*
     * Identify use of automatic protocol (as opposed
     * to manual) and proceed with login procedure:
     *
     *    ESC SST<pwd>.
     *
     * ESC means ``automatic dump mode'' protocol.
     * SS  identifies service:
     *    P = Pager ID
     *    G = Message (?)
     * T identifies type of terminal or device sending:
     *    1 = ``category of entry devices using the same protocol''
     *    	  (PETs and IXO)
     *    7,8,9 = ``wild card terminals or devices which may 
     *         relate to a specific users' system''.
     * <pwd> is a 6-character alpha-numeric password
     *    string (optional)
     */
    const fxStr& pass = info.getPagerPassword();
    fxStr prolog("\033" | ixoService | ixoDeviceID);
    if (pass != "")
	prolog.append(pass);
    prolog.append('\r');

    traceIXO("SEND device identification/login request");
    putModem((const char*) prolog, prolog.length());

    int ntries = ixoLoginRetries;	// retry login up to 3 times
    int unknown = ixoMaxUnknown;	// accept up to 3 unknown messages
    start = Sys::now();
    do {
	u_int len = getResponse(buf, ixoLoginTimeout);
	const u_char* cp = buf;
	while (len > 0) {
	    switch (cp[0]) {
	    case ACK:			// login successful, wait for go-ahead
		traceIXO("RECV ACK (login successful)");
		return (pageGoAhead(req, info, emsg));
	    case NAK:			// login failed, retry
		traceIXO("RECV NAK (login unsuccessful)");
		if (--ntries == 0) {
		    emsg = "Login failed multiple times";
		    req.status = send_retry;
		    return (false);
		}
		/*
		 * Resend the login request.
		 */
		traceIXO("SEND device identification/login request");
		putModem((const char*) prolog, prolog.length());
		start = Sys::now();	// restart timer
		/*
		 * NB: we should just goto the top of the loop,
		 *     but old cfront-based compilers aren't
		 *     smart enough to handle goto's that might
		 *     bypass destructors.
		 */
		unknown++;		// counteract loop iteration
		len = 0;		// don't scan forward in buffer
		break;
	    case ESC:
		if (len > 1) {
		    if (cp[1] == EOT) {
			traceIXO("RECV EOT (forced disconnect)");
			emsg =
			    "Paging central responded with forced disconnect";
			req.status = send_failed;
			return (false);
		    }
		    // check for go-ahead message
		    if (len > 2 && cp[1] == '[' && cp[2] == 'p') {
			traceIXO("RECV ACK (login successful & got go-ahead)");
			return (true);
		    }
		}
		break;
	    }
	    if (!scanForCode(cp, len))
		traceResponse(buf);
	}
    } while ((unsigned)Sys::now()-start < ixoLoginTimeout && --unknown != 0);
    emsg = fxStr::format("Protocol failure: %s from paging central",
	(unknown ?
	    "timeout waiting for response" : "too many unknown responses"));
    req.status = send_retry;
    return (false);
}

bool
pageSendApp::pageGoAhead(FaxRequest& req, const FaxMachineInfo&, fxStr& emsg)
{
    fxStackBuffer buf;
    time_t start = Sys::now();
    u_int unknown = ixoMaxUnknown;
    do {
	u_int len = getResponse(buf, ixoGATimeout);
	const u_char* cp = buf;
	while (len > 0) {
	    if (len > 2 && cp[0] == ESC && cp[1] == '[' && cp[2] == 'p') {
		traceIXO("RECV go-ahead (prologue done)");
		return (true);
	    }
	    (void) scanForCode(cp, len);
	}
	traceResponse(buf);
    } while ((unsigned) Sys::now()-start < ixoGATimeout && --unknown != 0);
    emsg = fxStr::format("Protocol failure: %s waiting for go-ahead message",
	unknown ? "timeout" : "too many unknown responses");
    req.status = send_retry;
    return (false);
}

/*
 * Calculate packet checksum and append to buffer.
 */
static void
addChecksum(fxStackBuffer& buf)
{
    int sum = 0;
    for (u_int i = 0; i < buf.getLength(); i++)
	sum += buf[i];

    char check[3];
    check[2] = '0' + (sum & 15); sum = sum >> 4;
    check[1] = '0' + (sum & 15); sum = sum >> 4;
    check[0] = '0' + (sum & 15);
    buf.put(check, 3);
}

bool
pageSendApp::sendPagerMsg(FaxRequest& req, faxRequest& preq, const fxStr& msg, fxStr& emsg)
{
    /*
     * Build page packet:
     *
     *    STX pin CR line1 CR ... linen CR EEE checksum CR
     *
     * where pin is the destination Pager ID and line<n>
     * are the lines of the message to send.  The trailing
     * EEE depends on whether or not the message is continued
     * on into the next block and/or whether this is the last
     * block in the transaction.
     */
    fxStackBuffer buf;
    buf.put(STX);
    buf.put(preq.item);				// copy PIN to packet
    buf.put('\r');
    buf.put(msg);				// copy text message
    buf.put('\r');
    buf.put(ETX);				// XXX
    addChecksum(buf);				// append packet checksum
    buf.put('\r');

    /*
     * Send the packet to paging central.
     */
    traceIXO("SEND message block");
    putModem((const char*) buf, buf.getLength());

    /*
     * Process replies, possibly retransmitting the packet.
     */
    fxStackBuffer resp;				// paging central response
    u_int ntries = ixoXmitRetries;		// up to 3 xmits of message
    u_int unknown = 0;				// count of unknown responses
    time_t start = Sys::now();
    do {
	u_int len = getResponse(resp, ixoXmitTimeout);
	const u_char* cp = resp;
	while (len > 0) {
	    switch (cp[0]) {
	    case ACK:
		traceIXO("RECV ACK (message block accepted)");
		return (true);
	    case NAK:
		traceIXO("RECV NAK (message block rejected)");
		if (--ntries == 0) {
		    req.status = send_retry;
		    emsg = "Message block not acknowledged by paging central "
			"after multiple tries";
		    return (false);
		}
		/*
		 * Retransmit the packet to paging central.
		 */
		traceIXO("SEND message block (retransmit)");
		putModem((const char*) buf, buf.getLength());
		start = Sys::now();		// restart timer
		unknown = 0;			// reset unknown response count
		/*
		 * NB: we should just goto the top of the loop,
		 *     but old cfront-based compilers aren't
		 *     smart enough to handle goto's that might
		 *     bypass destructors.
		 */
		len = 0;			// flush response buffer
		flushModemInput();		// flush pending data
		break;
	    case RS:
		traceIXO("RECV RS (message block rejected; skip to next)");
		/*
		 * This actually means to abandon the current transaction
		 * and proceed to the next.  However we treat it as a
		 * total failure since it's not clear within the present
		 * design whether proceeding to the next transaction is
		 * the right thing to do.
		 */
		req.status = send_failed;
		emsg = "Message block transmit failed; "
		    "paging central rejected it";
		return (false);
	    case ESC:
		if (len > 1 && cp[1] == EOT) {
		    traceIXO("RECV EOT (forced disconnect)");
		    req.status = send_failed;
		    emsg = "Protocol failure: paging central responded to "
			"message block transmit with forced disconnect";
		    return (false);
		}
		/* fall thru... */
	    default:				// unrecognized response
		unknown++;
		break;
	    }
	    if (!scanForCode(cp, len))
		traceResponse(resp);
	}
    } while ((unsigned)Sys::now()-start < ixoXmitTimeout && unknown < ixoMaxUnknown);
    emsg = fxStr::format("Protocol failure: %s to message block transmit",
	(unknown ?
	    "timeout waiting for response" : "too many unknown responses"));
    req.status = send_retry;
    return (false);
}

bool
pageSendApp::pageEpilogue(FaxRequest& req, const FaxMachineInfo&, fxStr& emsg)
{
    putModem("\4\r", 2);		// EOT then <CR>

    fxStackBuffer buf;
    time_t start = Sys::now();
    do {
	u_int len = getResponse(buf, ixoAckTimeout);
	const u_char* cp = buf;
	while (len > 0) {
	    switch (cp[0]) {
	    case ESC:
		if (len > 1 && cp[1] == EOT) {
		    traceIXO("RECV EOT (disconnect)");
		    return (true);
		}
		break;
	    case RS:
		traceIXO("RECV RS (message content rejected)");
		emsg = "Paging central rejected content; check PIN";
		req.status = send_failed;
		return (false);
	    }
	    (void) scanForCode(cp, len);
	}
	traceResponse(buf);
	// NB: ignore unknown responses
    } while ((unsigned)Sys::now() - start < ixoAckTimeout);
    req.status = send_retry;
    emsg = "Protocol failure: timeout waiting for transaction ACK/NAK "
	"from paging central";
    return (false);
}

void
pageSendApp::traceResponse(const fxStackBuffer& buf)
{
    const char* extra = "";
    u_int len = buf.getLength();
    if (len > 0) {
	const char* cp = buf;
	do {
	    if (!isprint(*cp++)) {
		extra = "unknown paging central response";
		break;
	    }
	} while (--len);
	/*
	 * No unprintable characters, just log the string w/o
	 * the alarming "Unknown paging central response".
	 */
	traceIXO("RECV%s: %.*s", extra, buf.getLength(), (const char*) buf);
    }
}

void
pageSendApp::traceIXOCom(const char* dir, const u_char* data, u_int cc)
{
    if (log) {
	if ((logTracingLevel& FAXTRACE_IXO) == 0)
	    return;
    } else if ((tracingLevel & FAXTRACE_IXO) == 0)
	return;

    fxStackBuffer buf;
    for (u_int i = 0; i < cc; i++) {
	u_char b = data[i];
	if (!isprint(b)) {
	    const char* octdigits = "01234567";
	    char s[4];
	    s[0] = '\\';
	    s[1] = octdigits[b>>6];
	    s[2] = octdigits[(b>>3)&07];
	    s[3] = octdigits[b&07];
	    buf.put(s, 4);
	} else
	    buf.put(b);
    }
    traceStatus(FAXTRACE_IXO, "%s <%u:%.*s>",
	dir, cc, buf.getLength(), (const char*) buf);
}

void
pageSendApp::traceIXO(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vtraceStatus(FAXTRACE_PROTOCOL, fmt, ap);
    va_end(ap);
}

/*
 * BEGIN UCP Support
 */
void
pageSendApp::sendUcpPage(FaxRequest& req, FaxMachineInfo& info,
    const fxStr& msg, fxStr& notice)
{

    while (req.requests.length() > 0) {	// messages
        u_int i = req.findRequest(FaxRequest::send_page);
	if (i == fx_invalidArrayIndex)
	    break;
	if (req.requests[i].item.length() == 0) {
	    sendFailed(req, send_failed, "No PIN specified");
	    break;
	}
	if (!sendUcpMsg(req, req.requests[i], msg, req.notice, info)) {
	    /*
	     * On protocol errors retry more quickly
	     * (there's no reason to wait is there?).
	     */
	    if (req.status == send_retry) {
	        req.tts = time(0) + requeueProto;
		break;
	    }
	}
	req.requests.remove(i);
    }
//    if (req.status == send_ok) 
//        (void) pageEpilogue(req, info, notice);
}
// the (simplistic) UCP checksum algorithm
static void
addUcpChecksum(fxStackBuffer& buf)
{
    int sum = 0;
    for (u_int i = 1; i < buf.getLength(); i++)
	sum += buf[i];
    sum&=0xff;

    buf.put(fxStr::format("%2.2X",sum&0xff));
}

// Encode the message text in HEX, and perform the character set
// translation while doing so.
// Note that SMS uses a very strange character encoding. The switch
// statement below translates the ISO 8859-1 characters that can
// be mapped to there SMS counterpart, the others to something
// recognizable. The characterset RFC was used as a guideline.
static void
addUcpCodedMsg(fxStackBuffer& buf, const fxStr& msg)
{
    u_int	c;
    for (u_int i = 0; i < msg.length(); i++) {
	c = msg[i];
	switch (c) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 11:
		case 13:
		case 14:
		case 15:
		case 16:
		case 17:
		case 18:
		case 19:
		case 20:
		case 21:
		case 22:
		case 23:
		case 24:
		case 25:
		case 26:
		case 27:
		case 28:
		case 29:
		case 30:
		case 31:	break;

		case 10:	buf.put("0A"); break;
		case 12:	buf.put("0D"); break;
		case 36:	buf.put("02"); break;
		case 64:	buf.put("00"); break;
		case 91:	buf.put("3C28"); break;
		case 92:	buf.put("2F2F"); break;
		case 93:	buf.put("293E"); break;
		case 94:	buf.put("3E"); break;
		case 95:	buf.put("11"); break;
		case 96:	buf.put("2C21"); break;
		case 123:	buf.put("2821"); break;
		case 124:	buf.put("2121"); break;
		case 125:	buf.put("2129"); break;
		case 126:	buf.put("213F"); break;
		case 160:	buf.put("20"); break;
		case 161:	buf.put("40"); break;
		case 162:	buf.put("4374"); break;
		case 163:	buf.put("01"); break;
		case 164:	buf.put("24"); break;
		case 165:	buf.put("03"); break;
		case 166:	buf.put("4242"); break;
		case 167:	buf.put("5F"); break;
		case 168:	buf.put("22"); break;
		case 169:	buf.put("436F"); break;
		case 170:	buf.put("61"); break;
		case 171:	buf.put("3C"); break;
		case 172:	buf.put("2D"); break;
		case 173:	buf.put("2D"); break;
		case 174:	buf.put("52"); break;
		case 175:	buf.put("2D"); break;

		case 176:	buf.put("4447"); break;
		case 177:	buf.put("2B2D"); break;
		case 178:	buf.put("2A2A32"); break;
		case 179:	buf.put("2A2A33"); break;
		case 180:	buf.put("2C"); break;
		case 181:	buf.put("6D75"); break;
		case 182:	buf.put("5049"); break;
		case 183:	buf.put("2E"); break;
		case 184:	buf.put("2C"); break;
		case 185:	buf.put("2A2A31"); break;
		case 186:	buf.put("6F"); break;
		case 187:	buf.put("3E"); break;
		case 188:	buf.put("312F34"); break;
		case 189:	buf.put("312F32"); break;
		case 190:	buf.put("332F34"); break;
		case 191:	buf.put("60"); break;

		case 192:	buf.put("41"); break;
		case 193:	buf.put("41"); break;
		case 194:	buf.put("41"); break;
		case 195:	buf.put("41"); break;
		case 196:	buf.put("5B"); break;
		case 197:	buf.put("0E"); break;
		case 198:	buf.put("1C"); break;
		case 199:	buf.put("09"); break;
		case 200:	buf.put("45"); break;
		case 201:	buf.put("1F"); break;
		case 202:	buf.put("45"); break;
		case 203:	buf.put("45"); break;
		case 204:	buf.put("49"); break;
		case 205:	buf.put("49"); break;
		case 206:	buf.put("49"); break;
		case 207:	buf.put("49"); break;

		case 208:	buf.put("442D"); break;
		case 209:	buf.put("5D"); break;
		case 210:	buf.put("4F"); break;
		case 211:	buf.put("4F"); break;
		case 212:	buf.put("4F"); break;
		case 213:	buf.put("4F"); break;
		case 214:	buf.put("4F"); break;
		case 215:	buf.put("78"); break;
		case 216:	buf.put("0B"); break;
		case 217:	buf.put("55"); break;
		case 218:	buf.put("55"); break;
		case 219:	buf.put("55"); break;
		case 220:	buf.put("55"); break;
		case 221:	buf.put("59"); break;
		case 222:	buf.put("5448"); break;
		case 223:	buf.put("1E"); break;

		case 224:	buf.put("7F"); break;
		case 225:	buf.put("61"); break;
		case 226:	buf.put("61"); break;
		case 227:	buf.put("61"); break;
		case 228:	buf.put("7B"); break;
		case 229:	buf.put("0F"); break;
		case 230:	buf.put("6165"); break;
		case 231:	buf.put("09"); break;
		case 232:	buf.put("04"); break;
		case 233:	buf.put("05"); break;
		case 234:	buf.put("65"); break;
		case 235:	buf.put("65"); break;
		case 236:	buf.put("07"); break;
		case 237:	buf.put("69"); break;
		case 238:	buf.put("69"); break;
		case 239:	buf.put("69"); break;

		case 240:	buf.put("642D"); break;
		case 241:	buf.put("7D"); break;
		case 242:	buf.put("08"); break;
		case 243:	buf.put("6F"); break;
		case 244:	buf.put("6F"); break;
		case 245:	buf.put("6F"); break;
		case 246:	buf.put("7C"); break;
		case 247:	buf.put("2F"); break;
		case 248:	buf.put("0C"); break;
		case 249:	buf.put("06"); break;
		case 250:	buf.put("75"); break;
		case 251:	buf.put("75"); break;
		case 252:	buf.put("7E"); break;
		case 253:	buf.put("79"); break;
		case 254:	buf.put("7468"); break;
		case 255:	buf.put("79"); break;
		default:
			buf.put(fxStr::format("%2.2X",msg[i]&0xff));
			break;
	}
    }
    if (msg.length() > 160) {
	msg[320] = '\0';
    }
}

bool
pageSendApp::sendUcpMsg(FaxRequest& req, faxRequest& preq, const fxStr& msg, fxStr& emsg, FaxMachineInfo& info)
{
    /*
     * Build page packet:
     *
     *    STX trn '/' len '/O/30/' recipient '/' originator '/' password \
     *		'///////' message '/' checksum ETX
     *
     *	trn		a transaction number
     *	len		computed at the end, when the character set
     *			translation is complete
     *	recipient	who is to receive the message
     *	originator	who sent the message, may be different from the
     *			the number of the modem sending the message
     *	password	a number, untested
     *	message		the hex encoded message (see addUcpCodedMsg)
     *	checksum	the UCP checksum
     *
     */
    fxStackBuffer tmp;

    // first compose the things we know before hand
    fxStackBuffer buf;

    buf.put("/O/30/");
    buf.put(preq.item);		// recipient
    buf.put("/");
    buf.put(info.getPageSource()); // originator
    buf.put("/");
    buf.put(info.getPagerPassword()); // authenticator
    buf.put("///////");
    addUcpCodedMsg(buf,msg);
    buf.put("/");

    // now we know the length, and we can compute the first field
    int len = buf.getLength() + 2 /* trn */ + 1 /* slash */ + 5 /* length */
	+ 2 /* check sum */;
    tmp.put(STX);
    tmp.put("01/");
    tmp.put(fxStr::format("%5.5d", len));
    tmp.put(buf, buf.getLength());

    // know we still have to add the check sum and the trailer
    addUcpChecksum(tmp);    	// append packet checksum
    tmp.put("\x03");

    buf = tmp;

    /*
     * Send the packet to paging central.
     */
    traceIXO("SEND message block: [%.*s]", buf.getLength(), (const char*) buf);
    putModem((const char*) buf, buf.getLength());

    /*
     * Process replies, possibly retransmitting the packet.
     */
    fxStackBuffer resp;				// paging central response
    u_int ntries = ixoXmitRetries;		// up to 3 xmits of message
    u_int unknown = 0;				// count of unknown responses
    time_t start = Sys::now();
    do {
	u_int len = getResponse(resp, ixoXmitTimeout);
	const fxStr str = (const char*)resp;
	//readUcpResponse(str);
	fxStr tmp;
	u_int pos=1,pos2;
	while(pos<str.length()) {
	    tmp=str.token(pos,"\003");

	    // Verify checksum
	    int sum = 0;
	    for (u_int i = 0; i < tmp.length()-2; i++)
 	        sum += tmp[i];
	    sum&=0xff;
	    pos2=tmp.length();
	    fxStr CS=tmp.tokenR(pos2,"/");

	    pos2=0;
	    fxStr TRN=tmp.token(pos2,"/");
	    fxStr LEN=tmp.token(pos2,"/");
	    fxStr OR=tmp.token(pos2,"/");
	    fxStr OT=tmp.token(pos2,"/");
	    if(OR=="R") {  // Response
	        if(OT=="30") { 
		    fxStr  N_ACK=tmp.token(pos2,"/");
		    switch( N_ACK[0]) { 
		    case 'A':  // positive result (ACK)
		        traceIXO("RECV ACK (message block accepted)");
			return(true);
		    case 'N': // Negative result (NACK)
		        traceIXO("RECV NACK (message block rejected)");
		        if(--ntries==0) {
			    req.status = send_retry;
			    emsg="Message block not acknowledged by paging central"
			      "after multiple tries";
			    return(false);
			}
			traceIXO("SEND message block (retransmit)");
			putModem((const char*) buf, buf.getLength());
			start = Sys::now();		// restart timer
			unknown = 0;			// reset unknown response count
		    }
		}
	    }
	}
    } while ((unsigned)Sys::now()-start < ixoXmitTimeout && unknown < ixoMaxUnknown);
    return false;
}

/*
 * END UCP Support
 */

bool
pageSendApp::putModem(const void* data, int n, long ms)
{
    traceIXOCom("<--",  (const u_char*) data, n);
    return (putModem1(data, n, ms));
}

time_t pageSendApp::getConnectTime() const	{ return (connTime); }

/*
 * Configuration support.
 */

void
pageSendApp::resetConfig()
{
    ModemServer::resetConfig();
    setupConfig();
}

#define	N(a)	(sizeof (a) / sizeof (a[0]))

pageSendApp::stringtag pageSendApp::strings[] = {
{ "ixoservice",		&pageSendApp::ixoService,	IXO_SERVICE },
{ "ixodeviceid",	&pageSendApp::ixoDeviceID,	IXO_DEVICEID },
{ "pagerttyparity",	&pageSendApp::pagerTTYParity,	"even" },
};
pageSendApp::stringtag pageSendApp::atcmds[] = {
{ "pagersetupcmds",	&pageSendApp::pagerSetupCmds },
};
pageSendApp::numbertag pageSendApp::numbers[] = {
{ "pagermaxmsglength",	&pageSendApp::pagerMaxMsgLength,128 },
{ "ixomaxunknown",	&pageSendApp::ixoMaxUnknown,	IXO_MAXUNKNOWN },
{ "ixoidprobe",		&pageSendApp::ixoIDProbe,	IXO_IDPROBE },
{ "ixoidtimeout",	&pageSendApp::ixoIDTimeout,	IXO_IDTIMEOUT },
{ "ixologinretries",	&pageSendApp::ixoLoginRetries,	IXO_LOGINRETRIES },
{ "ixologintimeout",	&pageSendApp::ixoLoginTimeout,	IXO_LOGINTIMEOUT },
{ "ixogatimeout",	&pageSendApp::ixoGATimeout,	IXO_GATIMEOUT },
{ "ixoxmitretries",	&pageSendApp::ixoXmitRetries,	IXO_XMITRETRIES },
{ "ixoxmittimeout",	&pageSendApp::ixoXmitTimeout,	IXO_XMITTIMEOUT },
{ "ixoacktimeout",	&pageSendApp::ixoAckTimeout,	IXO_ACKTIMEOUT },
};

void
pageSendApp::setupConfig()
{
    int i;
    for (i = N(strings)-1; i >= 0; i--)
	(*this).*strings[i].p = (strings[i].def ? strings[i].def : "");
    for (i = N(atcmds)-1; i >= 0; i--)
	(*this).*atcmds[i].p = (atcmds[i].def ? atcmds[i].def : "");
    for (i = N(numbers)-1; i >= 0; i--)
	(*this).*numbers[i].p = numbers[i].def;
}

bool
pageSendApp::setConfigItem(const char* tag, const char* value)
{
    u_int ix;
    if (findTag(tag, (const tags*)atcmds, N(atcmds), ix)) {
	(*this).*atcmds[ix].p = parseATCmd(value);
    } else if (findTag(tag, (const tags*) strings, N(strings), ix)) {
	(*this).*strings[ix].p = value;
    } else if (findTag(tag, (const tags*)numbers, N(numbers), ix)) {
	(*this).*numbers[ix].p = getNumber(value);
    } else
	return (ModemServer::setConfigItem(tag, value));
    return (true);
}
#undef	N

u_int
pageSendApp::getConfigParity(const char* value) const
{
    if (streq(value, "even"))
	return (EVEN);
    else if (streq(value, "odd"))
	return (ODD);
    else if (streq(value, "none"))
	return (NONE);
    else {
	logError("Unknown pager tty parity %s ignored; using EVEN", value);
	return (EVEN);				// per IXO/TAP spec
    }
}

/*
 * Modem and TTY setup
 */
bool 
pageSendApp::setupModem()
{
    return (ModemServer::setupModem() &&
	setParity((Parity) getConfigParity(pagerTTYParity)));
}

/*
 * Modem locking support.
 */

bool
pageSendApp::canLockModem()
{
    return (modemLock ? modemLock->check() : true);
}

bool
pageSendApp::lockModem()
{
    return (modemLock ? modemLock->lock() : true);
}

void
pageSendApp::unlockModem()
{
    if (modemLock)
	modemLock->unlock();
}

/*
 * Notification handlers.
 */

/*
 * Handle notification that the modem device has become
 * available again after a period of being unavailable.
 */
void
pageSendApp::notifyModemReady()
{
    ready = true;
}

/*
 * Handle notification that the modem device looks to
 * be in a state that requires operator intervention.
 */
void
pageSendApp::notifyModemWedged()
{
    if (!sendModemStatus(getModemDeviceID(), "W"))
	logError("MODEM %s appears to be wedged",
	    (const char*) getModemDevice());
    close();
}

/*
 * Miscellaneous stuff.
 */

static void
usage(const char* appName)
{
    faxApp::fatal("usage: %s -m deviceID [-t tracelevel] [-l] qfile ...",
	appName);
}

static void
sigCleanup(int s)
{
    signal(s, fxSIGHANDLER(sigCleanup));
    logError("CAUGHT SIGNAL %d", s);
    pageSendApp::instance().close();
    if (!pageSendApp::instance().isRunning())
	_exit(send_failed);
}

int
main(int argc, char** argv)
{
    faxApp::setupLogging("PageSend");

    fxStr appName = argv[0];
    u_int l = appName.length();
    appName = appName.tokenR(l, '/');

    faxApp::setOpts("c:m:l");

    fxStr devID;
    for (GetoptIter iter(argc, argv, faxApp::getOpts()); iter.notDone(); iter++)
	switch (iter.option()) {
	case 'm': devID = iter.optArg(); break;
	case '?': usage(appName);
	}
    if (devID == "")
	usage(appName);

    pageSendApp* app = new pageSendApp(faxApp::idToDev(devID), devID);

    signal(SIGTERM, fxSIGHANDLER(sigCleanup));
    signal(SIGINT, fxSIGHANDLER(sigCleanup));

    app->initialize(argc, argv);
    app->open();
    while (app->isRunning() && !app->isReady())
	Dispatcher::instance().dispatch();
    FaxSendStatus status;
    if (app->isReady())
	status = app->send(argv[optind]);
    else
	status = send_retry;
    app->close();
    return (status);
}
