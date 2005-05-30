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

#include <sys/file.h>
#include <ctype.h>
#include <errno.h>

#include "Dispatcher.h"
#include "tiffio.h"
#include "FaxServer.h"
#include "FaxRecvInfo.h"
#include "faxApp.h"			// XXX
#include "t.30.h"
#include "config.h"

/*
 * FAX Server Reception Protocol.
 */

bool
FaxServer::recvFax(const CallID& callid, fxStr& emsg)
{
    traceProtocol("RECV FAX: begin");

    FaxRecvInfoArray docs;
    FaxRecvInfo info;
    bool faxRecognized = false;
    emsg = "";
    abortCall = false;
    waitNotifyPid = 0;

    /*
     * Create the first file ahead of time to avoid timing
     * problems with Class 1 modems.  (Creating the file
     * after recvBegin can cause part of the first page to
     * be lost.)
     */
    info.callid = callid;
    TIFF* tif = setupForRecv(info, docs, emsg);
    if (tif) {
	recvPages = 0;			// total count of received pages
	fileStart = pageStart = Sys::now();
	if (faxRecognized = modem->recvBegin(emsg)) {
	    /*
	     * If the system is busy then notifyRecvBegun may not return
	     * quickly.  Thus we run it in a child process and move on.
	     */
	    waitNotifyPid = fork();
	    switch (waitNotifyPid) {
		case 0:
		    // NB: partially fill in info for notification call
		    notifyRecvBegun(info);
		    sleep(1);		// XXX give parent time
		    exit(0);
		case -1:
		    logError("Can not fork for non-priority processing.");
		    notifyRecvBegun(info);
		    break;
		default:
		    Dispatcher::instance().startChild(waitNotifyPid, this);
		    break;
	    }
	    if (!recvDocuments(tif, info, docs, emsg)) {
		traceProtocol("RECV FAX: %s", (const char*) emsg);
		modem->recvAbort();
	    }
	    if (!modem->recvEnd(emsg))
		traceProtocol("RECV FAX: %s", (const char*) emsg);
	} else {
	    traceProtocol("RECV FAX: %s", (const char*) emsg);
	    TIFFClose(tif);
	}
    } else
	traceServer("RECV FAX: %s", (const char*) emsg);

    /*
     * Possibly issue a command upon successful reception.
     */
    if (info.npages > 0 && info.reason == "")
	    modem->recvSucceeded();

    /*
     * Now that the session is completed, do local processing
     * that might otherwise slow down the protocol (and potentially
     * cause timing problems).
     */
    for (u_int i = 0, n = docs.length(); i < n; i++) {
	FaxRecvInfo& ri = docs[i];
	if (ri.npages == 0)
	    Sys::unlink(ri.qfile);
	else
	    Sys::chmod(ri.qfile, recvFileMode);
	if (faxRecognized)
	    notifyRecvDone(ri);
    }
    traceProtocol("RECV FAX: end");
    return (faxRecognized);
}

int
FaxServer::getRecvFile(fxStr& qfile, fxStr& emsg)
{
    u_long seqnum = Sequence::getNext(FAX_RECVDIR "/" FAX_SEQF, emsg);

    if (seqnum == -1)
	return -1;

    qfile = fxStr::format(FAX_RECVDIR "/fax" | Sequence::format | ".tif", seqnum);
    int ftmp = Sys::open(qfile, O_RDWR|O_CREAT|O_EXCL, recvFileMode);

    if (ftmp < 0)
        emsg = "Failed to find unused filename";

    return ftmp;
}

/*
 * Create and lock a temp file for receiving data.
 */
TIFF*
FaxServer::setupForRecv(FaxRecvInfo& ri, FaxRecvInfoArray& docs, fxStr& emsg)
{
    int ftmp = getRecvFile(ri.qfile, emsg);
    if (ftmp >= 0) {
	ri.commid = getCommID();	// should be set at this point
	ri.npages = 0;			// mark it to be deleted...
	docs.append(ri);		// ...add it in to the set
	TIFF* tif = TIFFFdOpen(ftmp, ri.qfile, "w");
	if (tif != NULL)
	    return (tif);
	Sys::close(ftmp);
	emsg = fxStr::format("Unable to open TIFF file %s for writing",
	    (const char*) ri.qfile);
	ri.reason = emsg;		// for notifyRecvDone
    } else
	emsg.insert("Unable to create temp file for received data: ");
    return (NULL);
}

/*
 * Receive one or more documents.
 */
bool
FaxServer::recvDocuments(TIFF* tif, FaxRecvInfo& info, FaxRecvInfoArray& docs, fxStr& emsg)
{
    bool recvOK;
    u_int ppm = PPM_EOP;
    batchid = getCommID();
    for (;;) {
	bool okToRecv = true;
	fxStr reason;
	modem->getRecvSUB(info.subaddr);		// optional subaddress
	/*
	 * Check a received TSI/PWD against the list of acceptable
	 * patterns defined for the server.  This form of access
	 * control depends on the sender passing a valid TSI/PWD.
	 * Note that to accept/reject unspecified values one
	 * should match "<UNSPECIFIED>".
	 *
	 * NB: Caller-ID access control is done elsewhere; prior
	 *     to answering a call.
	 */
	if (!modem->getRecvTSI(info.sender))		// optional TSI
	    info.sender = "<UNSPECIFIED>";
	if (qualifyTSI != "") {
	    okToRecv = isTSIOk(info.sender);
	    reason = "Permission denied (unnacceptable client TSI)";
	    traceServer("%s TSI \"%s\"", okToRecv ? "ACCEPT" : "REJECT",
		(const char*) info.sender);
	}
	if (!modem->getRecvPWD(info.passwd))		// optional PWD
	    info.passwd = "<UNSPECIFIED>";
	if (qualifyPWD != "") {
	    okToRecv = isPWDOk(info.passwd);
	    reason = "Permission denied (unnacceptable client PWD)";
	    traceServer("%s PWD \"%s\"", okToRecv ? "ACCEPT" : "REJECT",
		(const char*) info.passwd);
	}
	if (!okToRecv) {
	    emsg = reason;
	    info.time = (u_int) getFileTransferTime();
	    info.reason = emsg;
	    docs[docs.length()-1] = info;
	    notifyDocumentRecvd(info);
	    TIFFClose(tif);
	    return (false);
	}
	setServerStatus("Receiving from \"%s\"", (const char*) info.sender);
	recvOK = recvFaxPhaseD(tif, info, ppm, emsg);
	TIFFClose(tif);
	info.time = (u_int) getFileTransferTime();
	info.reason = emsg;
	docs[docs.length()-1] = info;
	/*
	 * If syslog is busy then notifyDocumentRecvd may not return
	 * quickly.  Thus we run it in a child process and move on.
	 */
	pid_t pid = waitNotifyPid;
	waitNotifyPid = fork();
	switch (waitNotifyPid) {
	    case 0:
		if (pid > 0) (void) Sys::waitpid(pid);
		notifyDocumentRecvd(info);
		sleep(1);		// XXX give parent time
		exit(0);
	    case -1:
		logError("Can not fork for non-priority logging.");
		notifyDocumentRecvd(info);
		break;
	    default:
		Dispatcher::instance().startChild(waitNotifyPid, this);
		break;
	}
	if (!recvOK || ppm == PPM_EOP)
	    return (recvOK);
	/*
	 * Setup state for another file.
	 */
	if (! batchLogs)
	{
	    traceServer("SESSION BATCH CONTINUING");
	    endSession();
	    beginSession(FAXNumber);
	    batchid.append(","|getCommID());
	    traceServer("SESSION BATCH %s", (const char*)batchid);
	}
	tif = setupForRecv(info, docs, emsg);
	if (tif == NULL)
	    return (false);
	fileStart = pageStart = Sys::now();
	if (!modem->recvEOMBegin(emsg))
	    return (false);
    }
    /*NOTREACHED*/
}

/*
 * Receive Phase B protocol processing.
 */
bool
FaxServer::recvFaxPhaseD(TIFF* tif, FaxRecvInfo& info, u_int& ppm, fxStr& emsg)
{
    fxStr id = info.sender;
    for (u_int i = 0; i < info.callid.size(); i++) {
	id.append('\n');
	id.append(info.callid[i]);
    }
    do {
	if (++recvPages > maxRecvPages) {
	    emsg = "Maximum receive page count exceeded, job terminated";
	    return (false);
	}
	if (!modem->recvPage(tif, ppm, emsg, id))
	    return (false);
	info.npages++;
	info.time = (u_int) getPageTransferTime();
	info.params = modem->getRecvParams();
	/*
	 * If syslog is busy then notifyPageRecvd may not return quickly.
	 * Thus we run it in a child process and move on.  Timestamps
	 * in syslog cannot be expected to have exact precision anyway.
	 */
	pid_t pid = waitNotifyPid;
	waitNotifyPid = fork();
	switch (waitNotifyPid) {
	    case 0:
		if (pid > 0) (void) Sys::waitpid(pid);
		notifyPageRecvd(tif, info, ppm);
		sleep(1);		// XXX give parent time
		exit(0);
	    case -1:
		logError("Can not fork for non-priority logging.");
		notifyPageRecvd(tif, info, ppm);
		break;
	    default:
		Dispatcher::instance().startChild(waitNotifyPid, this);
		break;
	}
	if (emsg != "") return (false);		// got page with fatal error
	if (PPM_PRI_MPS <= ppm && ppm <= PPM_PRI_EOP) {
	    emsg = "Procedure interrupt received, job terminated";
	    return (false);
	}
    } while (ppm == PPM_MPS || ppm == PPM_PRI_MPS);
    return (true);
}

void
FaxServer::notifyRecvBegun(FaxRecvInfo&)
{
}

/*
 * Handle notification that a page has been received.
 */
void
FaxServer::notifyPageRecvd(TIFF*, FaxRecvInfo& ri, int)
{
    traceServer("RECV FAX (%s): from %s, page %u in %s, %s, %s, %s, %s"
	, (const char*) ri.commid
	, (const char*) ri.sender
	, ri.npages
	, fmtTime((time_t) ri.time)
	, (ri.params.ln == LN_A4 ? "A4" : ri.params.ln == LN_B4 ? "B4" : "INF")
	, ri.params.verticalResName()
	, ri.params.dataFormatName()
	, ri.params.bitRateName()
    );
}

/*
 * Handle notification that a document has been received.
 */
void
FaxServer::notifyDocumentRecvd(FaxRecvInfo& ri)
{
    traceServer("RECV FAX (%s): %s from %s, route to %s, %u pages in %s"
	, (const char*) ri.commid
	, (const char*) ri.qfile
	, (const char*) ri.sender
	, ri.subaddr != "" ? (const char*) ri.subaddr : "<unspecified>"
	, ri.npages
	, fmtTime((time_t) ri.time)
    );
}

/*
 * Handle final actions associated with a document being received.
 */
void
FaxServer::notifyRecvDone(FaxRecvInfo& ri)
{
    if (ri.reason != "")
	traceServer("RECV FAX (%s): session with %s terminated abnormally: %s"
	    , (const char*) ri.commid
	    , (const char*) ri.sender
	    , (const char*) ri.reason
	);
}
