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

#include "Dispatcher.h"
#include "tiffio.h"
#include "FaxServer.h"
#include "FaxMachineInfo.h"
#include "FaxRecvInfo.h"
#include "FaxAcctInfo.h"
#include "faxApp.h"			// XXX
#include "UUCPLock.h"
#include "t.30.h"
#include "config.h"

/*
 * FAX Server Transmission Protocol.
 */
void
FaxServer::sendFax(FaxRequest& fax, FaxMachineInfo& clientInfo, FaxAcctInfo& ai)
{
    u_int prevPages = fax.npages;
    if (lockModem()) {
	beginSession(fax.number);
	fax.commid = getCommID();		// set by beginSession
	traceServer("SEND FAX: JOB %s DEST %s COMMID %s DEVICE '%s'"
	    , (const char*) fax.jobid
	    , (const char*) fax.external
	    , (const char*) fax.commid
	    , (const char*) getModemDevice()
	);
	if (setupModem()) {
	    changeState(SENDING);
	    IOHandler* handler =
		Dispatcher::instance().handler(
		    getModemFd(), Dispatcher::ReadMask);
	    if (handler)
		Dispatcher::instance().unlink(getModemFd());
	    setServerStatus("Sending job " | fax.jobid);
	    /*
	     * Construct the phone number to dial by applying the
	     * dialing rules to the user-specified dialing string.
	     */
	    sendFax(fax, clientInfo, prepareDialString(fax.number));
	    /*
	     * Because some modems are impossible to safely hangup in the
	     * event of a problem, we force a close on the device so that
	     * the modem will see DTR go down and (hopefully) clean up any
	     * bad state its in.  We then wait a couple of seconds before
	     * trying to setup the modem again so that it can have some
	     * time to settle.  We want a handle on the modem so that we
	     * can be prepared to answer incoming phone calls.
	     */
	    discardModem(true);
	    changeState(MODEMWAIT, 5);
	} else {
	    sendFailed(fax, send_retry, "Can not setup modem", 4*pollModemWait);
	    discardModem(true);
	    changeState(MODEMWAIT, pollModemWait);
	}
	endSession();
	unlockModem();
    } else {
	if (state != LOCKWAIT)
	    sendFailed(fax, send_retry,
		"Can not lock modem device", 2*pollLockWait);
	if (state != SENDING && state != ANSWERING && state != RECEIVING)
	    changeState(LOCKWAIT, pollLockWait);
    }
    /*
     * Record transmit accounting information for caller.
     */
    ai.npages = fax.npages - prevPages;		// count of pages transmitted
    ai.params = clientParams.encode();		// negotiated parameters
    fax.sigrate = clientParams.bitRateName();	// (last) signalling rate used
    fax.df = clientParams.dataFormatName();	// negotiated data format
}

void
FaxServer::sendFailed(FaxRequest& fax, FaxSendStatus stat, const char* notice, u_int tts)
{
    fax.status = stat;
    fax.notice = notice;
    /*
     * When requeued for the default interval (requeueOther),
     * don't adjust the time-to-send field so that the spooler
     * will set it according to the default algorithm that 
     * uses the command-line parameter and a random jitter.
     */
    if (tts != requeueOther)
	fax.tts = Sys::now() + tts;
	traceServer("SEND FAILED: JOB %s DEST %s ERR %s"
		, (const char*) fax.jobid
            	, (const char*) fax.external
            	, (const char*) notice
        );

}

/*
 * Send the specified TIFF files to the FAX
 * agent at the given phone number.
 */
void
FaxServer::sendFax(FaxRequest& fax, FaxMachineInfo& clientInfo, const fxStr& number)
{
    connTime = 0;				// indicate no connection
    fxStr notice;
    /*
     * Force the modem into the appropriate class
     * used to send facsimile.  We do this before
     * doing any fax-specific operations such as
     * requesting polling.
     */
    if (!modem->faxService()) {
	sendFailed(fax, send_failed, "Unable to configure modem for fax use");
	return;
    }
    /*
     * Check if this job includes a poll request, and
     * if it does, inform the modem in case it needs to
     * do something to get back status about whether or
     * not documents are available for retrieval.
     */
    if (fax.findRequest(FaxRequest::send_poll) != fx_invalidArrayIndex &&
	!modem->requestToPoll(notice)) {
	sendFailed(fax, send_failed, notice);
	return;
    }
    /*
     * Calculate initial page-related session parameters so
     * that braindead Class 2 modems can constrain the modem
     * before dialing the telephone.
     */
    Class2Params dis;
    dis.decodePage(fax.pagehandling);
    dis.br = fxmin(modem->getBestSignallingRate(), (u_int) fax.desiredbr);
    dis.ec = (modem->supportsECM() ? fax.desiredec : EC_DISABLE);
    dis.st = fxmax(modem->getBestScanlineTime(), (u_int) fax.desiredst);
    dis.bf = BF_DISABLE;
    if (!modem->sendSetup(fax, dis, notice)) {
	sendFailed(fax, send_failed, notice);
	return;
    }
    fax.notice = "";
    notifyCallPlaced(fax);
    CallStatus callstat = modem->dial(number, notice);
    if (callstat == ClassModem::OK)
	connTime = Sys::now();			// connection start time
    (void) abortRequested();			// check for user abort
    if (callstat == ClassModem::OK && !abortCall) {
	/*
	 * Call reached a fax machine.  Check remote's
	 * capabilities against those required by the
	 * job and setup for transmission.
	 */
	fax.ndials = 0;				// consec. failed dial attempts
	fax.tottries++;				// total answered calls
	fax.totdials++;				// total attempted calls
	clientInfo.setCalledBefore(true);
	clientInfo.setDialFailures(0);
	modem->sendBegin();
	bool remoteHasDoc = false;
	notifyConnected(fax);
	FaxSendStatus status = modem->getPrologue(
	    clientCapabilities, remoteHasDoc, notice);
	if (status != send_ok) {
	    sendFailed(fax, status, notice, requeueProto);
	} else {
	    fxStr csi("<UNSPECIFIED>");
	    (void) modem->getSendCSI(csi);
	    clientInfo.setCSI(csi);			// record remote CSI
	    if (!sendClientCapabilitiesOK(fax, clientInfo, notice)) {
		// NB: mark job completed 'cuz there's no way recover
		sendFailed(fax, send_failed, notice);
	    } else {
		modem->sendSetupPhaseB(fax.passwd, fax.subaddr);
		/*
		 * Group 3 protocol forces any sends to precede any polling.
		 */
		fax.status = send_done;			// be optimistic
		while (fax.requests.length() > 0) {	// send operations
		    u_int i = fax.findRequest(FaxRequest::send_fax);
		    if (i == fx_invalidArrayIndex)
			break;
		    faxRequest& freq = fax.requests[i];
		    traceProtocol("SEND file \"%s\"", (const char*) freq.item);
		    fileStart = pageStart = Sys::now();
		    if (!sendFaxPhaseB(fax, freq, clientInfo)) {
			/*
			 * On protocol errors retry more quickly
			 * (there's no reason to wait is there?).
			 */
			if (fax.status == send_retry ||
			  fax.status == send_reformat)
			    fax.tts = time(0) + requeueProto;
			break;
		    }
		    /*
		     * The file was delivered, notify the server.
		     * Note that a side effect of the notification
		     * is that this file is deleted from the set of
		     * files to send (so that it's not sent again
		     * if the job is requeued).  This is why we call
		     * find again at the top of the loop
		     */
		    notifyDocumentSent(fax, i);
		}
		if (fax.status == send_done &&
	      fax.findRequest(FaxRequest::send_poll) != fx_invalidArrayIndex)
		    sendPoll(fax, remoteHasDoc);
	    }
	}
	modem->sendEnd();
	if (fax.status != send_done) {
	    clientInfo.setSendFailures(clientInfo.getSendFailures()+1);
	    clientInfo.setLastSendFailure(fax.notice);
	} else
	    clientInfo.setSendFailures(0);
    } else if (!abortCall) {
	/*
	 * Analyze the call status codes and selectively decide if the
	 * job should be retried.  We try to avoid the situations where
	 * we might be calling the wrong number so that we don't end up
	 * harrassing someone w/ repeated calls.
	 */
	fax.ndials++;			// number of consecutive failed calls
	fax.totdials++;			// total attempted calls
	switch (callstat) {
	case ClassModem::NOCARRIER:	// no carrier detected on remote side
	    /*
	     * Since some modems can not distinguish between ``No Carrier''
	     * and ``No Answer'' we offer this configurable hack whereby
	     * we'll retry the job <n> times in the face of ``No Carrier''
	     * dialing errors; if we've never previously reached a facsimile
	     * machine at that number.  This should not be used except if
	     * the modem is incapable of distinguishing betwee ``No Carrier''
	     * and ``No Answer''.
	     */
	    if (!clientInfo.getCalledBefore() && fax.ndials > noCarrierRetrys)
		sendFailed(fax, send_failed, notice);
	    else if (fax.retrytime != 0)
		sendFailed(fax, send_retry, notice, fax.retrytime);
	    else
		sendFailed(fax, send_retry, notice, requeueTTS[callstat]);
	    break;
	case ClassModem::NODIALTONE:	// no local dialtone, possibly unplugged
	case ClassModem::ERROR:		// modem might just need to be reset
	case ClassModem::FAILURE:	// modem returned something unexpected
	    sendFailed(fax, send_retry, notice, requeueTTS[callstat]);
	    break;
	case ClassModem::NOFCON:	// carrier seen, but handshake failed
	case ClassModem::DATACONN:	// data connection established
	    clientInfo.setCalledBefore(true);
	    /* fall thru... */
	case ClassModem::BUSY:		// busy signal
	case ClassModem::NOANSWER:	// no answer or ring back
	    if (fax.retrytime != 0)
		sendFailed(fax, send_retry, notice, fax.retrytime);
	    else
		sendFailed(fax, send_retry, notice, requeueTTS[callstat]);
	    /* fall thru... */
	case ClassModem::OK:		// call was aborted by user
	    break;
	}
	if (callstat != ClassModem::OK) {
	    clientInfo.setDialFailures(clientInfo.getDialFailures()+1);
	    clientInfo.setLastDialFailure(fax.notice);
	}
    }
    if (abortCall)
	sendFailed(fax, send_failed, "Job aborted by user");
    else if (fax.status == send_retry) {
	if (fax.totdials == fax.maxdials) {
	    notice = fax.notice | "; too many attempts to dial";
	    sendFailed(fax, send_failed, notice);
	} else if (fax.tottries == fax.maxtries) {
	    notice = fax.notice | "; too many attempts to send";
	    sendFailed(fax, send_failed, notice);
	}
    }
    /*
     * Cleanup after the call.  If we have new information on
     * the client's remote capabilities, the machine info
     * database will be updated when the instance is destroyed.
     */
    modem->hangup();
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
 * Process a polling request.
 */
void
FaxServer::sendPoll(FaxRequest& fax, bool remoteHasDoc)
{
    u_int ix = fax.findRequest(FaxRequest::send_poll);
    if (ix == fx_invalidArrayIndex) {
	fax.notice = "polling operation not done because of internal failure";
	traceServer("internal muckup, lost polling request");
	// NB: job is marked done
    } else if (!remoteHasDoc) {
	fax.notice = "remote has no document to poll";
	traceServer("REJECT: " | fax.notice);
	// override to force status about polling failure
	if (fax.notify == FaxRequest::no_notice)
	    fax.notify = FaxRequest::when_done;
    } else {
	faxRequest& freq = fax.requests[ix];
	FaxRecvInfoArray docs;
	fax.status = (pollFaxPhaseB(freq.addr, freq.item, docs, fax.notice) ?
	    send_done : send_retry);
	for (u_int j = 0; j < docs.length(); j++) {
	    const FaxRecvInfo& ri = docs[j];
	    if (ri.npages > 0) {
		Sys::chmod(ri.qfile, recvFileMode);
		notifyPollRecvd(fax, ri);
	    } else {
		traceServer("POLL: empty file \"%s\" deleted",
		    (const char*) ri.qfile);
		Sys::unlink(ri.qfile);
	    }
	}
	if (fax.status == send_done)
	    notifyPollDone(fax, ix);
    }
}

/*
 * Phase B of Group 3 protocol.
 */
bool
FaxServer::sendFaxPhaseB(FaxRequest& fax, faxRequest& freq, FaxMachineInfo& clientInfo)
{
    fax.status = send_failed;			// assume failure

    TIFF* tif = TIFFOpen(freq.item, "r");
    if (tif && (freq.dirnum == 0 || TIFFSetDirectory(tif, freq.dirnum))) {
	// set up DCS according to file characteristics
	fax.status = sendSetupParams(tif, clientParams, clientInfo, fax.notice);
	if (fax.status == send_ok) {
	    /*
	     * Count pages sent and advance dirnum so that if we
	     * terminate prematurely we'll only transmit what's left
	     * in the current document/file.  Also, if nothing is
	     * sent, bump the counter on the number of times we've
	     * attempted to send the current page.  We don't try
	     * more than 3 times--to avoid looping.
	     */
	    u_int prevPages = fax.npages;
	    fax.status = modem->sendPhaseB(tif, clientParams, clientInfo,
		fax.pagehandling, fax.notice);
	    if (fax.npages == prevPages) {
		fax.ntries++;
		if (fax.ntries > 2) {
		    if (fax.notice != "")
			fax.notice.append("; ");
		    fax.notice.append(
			"Giving up after 3 attempts to send same page");
		    traceServer("SEND: %s \"%s\", dirnum %d",
			(const char*) fax.notice, (const char*) freq.item, freq.dirnum);
		    fax.status = send_failed;
		}
	    } else {
		freq.dirnum += fax.npages - prevPages;
		fax.ntries = 0;
	    }
	}
    } else {
	fax.notice = tif ? "Can not set directory in document file" :
			   "Can not open document file";
	traceServer("SEND: %s \"%s\", dirnum %d",
	    (const char*) fax.notice, (const char*) freq.item, freq.dirnum);
    }
    if (tif)
	TIFFClose(tif);
    return (fax.status == send_ok);
}

/*
 * Check client's capabilities (DIS) against those of the
 * modem and select the parameters that are best for us.
 */
bool
FaxServer::sendClientCapabilitiesOK(FaxRequest& fax, FaxMachineInfo& clientInfo, fxStr& emsg)
{
    /*
     * Select signalling rate and minimum scanline time
     * for the duration of the session.  These are not
     * changed once they are set here.
     */
    clientInfo.setMaxSignallingRate(clientCapabilities.br);
    int signallingRate =
	modem->selectSignallingRate(
	    fxmin(clientInfo.getMaxSignallingRate(), fax.desiredbr));
    if (signallingRate == -1) {
	emsg = "Modem does not support negotiated signalling rate";
	return (false);
    }
    clientParams.br = signallingRate;

    clientInfo.setMinScanlineTime(clientCapabilities.st);
    int minScanlineTime =
	modem->selectScanlineTime(
	    fxmax(clientInfo.getMinScanlineTime(), fax.desiredst));
    if (minScanlineTime == -1) {
	emsg = "Modem does not support negotiated min scanline time";
	return (false);
    }
    clientParams.st = minScanlineTime;

    /*
     * Use optional Error Correction Mode (ECM) if the
     * peer implements and our modem is also capable.
     */
    if (clientCapabilities.ec == EC_ENABLE && modem->supportsECM())
	clientParams.ec = fax.desiredec;
    else
	clientParams.ec = EC_DISABLE;
    clientParams.bf = BF_DISABLE;
    /*
     * Record the remote machine's capabilities for use below in
     * selecting tranfer parameters for each page sent.  The info
     * constructed here is also recorded in a private database for
     * use in pre-formatting documents sent in future conversations.
     */
    clientInfo.setSupportsHighRes(clientCapabilities.vr == VR_FINE);
    clientInfo.setSupports2DEncoding(clientCapabilities.df >= DF_2DMR);
    clientInfo.setMaxPageWidthInPixels(clientCapabilities.pageWidth());
    clientInfo.setMaxPageLengthInMM(clientCapabilities.pageLength());
    traceProtocol("REMOTE best rate %s", clientCapabilities.bitRateName());
    traceProtocol("REMOTE max %s", clientCapabilities.pageWidthName());
    traceProtocol("REMOTE max %s", clientCapabilities.pageLengthName());
    traceProtocol("REMOTE best vres %s", clientCapabilities.verticalResName());
    traceProtocol("REMOTE best format %s", clientCapabilities.dataFormatName());
    if (clientCapabilities.ec != EC_DISABLE)
	traceProtocol("REMOTE supports %s", clientCapabilities.ecmName());
    traceProtocol("REMOTE best %s", clientCapabilities.scanlineTimeName());
#ifdef notdef
    // NB: don't say anything since it confuses the naive
    traceProtocol("REMOTE %s PostScript transfer",
	clientInfo.getSupportsPostScript() ? "supports" : "does not support");
#endif

    traceProtocol("USE %s", clientParams.bitRateName());
    traceProtocol("USE %s", clientParams.scanlineTimeName());
    if (clientParams.ec == EC_ENABLE)
	traceProtocol("USE error correction mode");
    return (true);
}

/*
 * Select session parameters according to the info
 * in the TIFF file.  We setup the encoding scheme,
 * page width & length, and vertical-resolution
 * parameters.  If the remote machine is incapable
 * of handling the image, we bail out.
 *
 * Note that we shouldn't be rejecting too many files
 * because we cache the capabilities of the remote machine
 * and use this to image the facsimile.  This work is
 * mainly done to optimize transmission and to reject
 * anything that might sneak by.
 */
FaxSendStatus
FaxServer::sendSetupParams1(TIFF* tif,
    Class2Params& params, const FaxMachineInfo& clientInfo, fxStr& emsg)
{
    uint16 compression;
    (void) TIFFGetField(tif, TIFFTAG_COMPRESSION, &compression);
    if (compression != COMPRESSION_CCITTFAX3) {
	emsg = fxStr::format("Document is not in a Group 3-compatible"
	    " format (compression %u)", compression);
	return (send_failed);
    }

    // XXX perhaps should verify samples and bits/sample???
    uint32 g3opts;
    if (!TIFFGetField(tif, TIFFTAG_GROUP3OPTIONS, &g3opts))
	g3opts = 0;
    if (g3opts & GROUP3OPT_2DENCODING) {
	if (!clientInfo.getSupports2DEncoding()) {
	    emsg = "Document was encoded with 2DMR,"
		   " but client does not support this data format";
	    return (send_reformat);
	}
	if (!modem->supports2D()) {
	    emsg = "Document was encoded with 2DMR,"
		   " but modem does not support this data format";
	    return (send_reformat);
	}
	params.df = DF_2DMR;
    } else
	params.df = DF_1DMR;

    uint32 w;
    (void) TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    if (w > clientInfo.getMaxPageWidthInPixels()) {
	emsg = fxStr::format("Client does not support document page width"
		", max remote page width %u pixels, image width %lu pixels",
		clientInfo.getMaxPageWidthInPixels(), w);
	return (send_reformat);
    }
    if (!modem->supportsPageWidth((u_int) w)) {
	static const char* widths[8] = {
	    "1728",	// 1728 in 215 mm line
	    "2048",	// 2048 in 255 mm line
	    "2432",	// 2432 in 303 mm line
	    "1216",	// 1216 in 151 mm line
	    "864",	// 864 in 107 mm line
	    "<undefined>",
	    "<undefined>",
	    "<undefined>",
	};
	emsg = fxStr::format("Modem does not support document page width"
		", max page width %s pixels, image width %lu pixels",
		widths[modem->getBestPageWidth()&7], w);
	return (send_reformat);
    }
    // NB: only common values
    params.wd = (w <= 1728 ? WD_1728 : w <= 2048 ? WD_2048 : WD_2432);

    /*
     * Try to deduce the vertical resolution of the image
     * image.  This can be problematical for arbitrary TIFF
     * images 'cuz vendors sometimes don't give the units.
     * We, however, can depend on the info in images that
     * we generate 'cuz we're careful to include valid info.
     */
    float yres;
    if (TIFFGetField(tif, TIFFTAG_YRESOLUTION, &yres)) {
	short resunit = RESUNIT_NONE;
	(void) TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &resunit);
	if (resunit == RESUNIT_INCH)
	    yres /= 25.4;
    } else {
	/*
	 * No vertical resolution is specified, try
	 * to deduce one from the image length.
	 */
	u_long l;
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &l);
	yres = (l < 1450 ? 3.85 : 7.7);		// B4 at 98 lpi is ~1400 lines
    }
    if (yres >= 7.) {
	if (!clientInfo.getSupportsHighRes()) {
	    emsg = fxStr::format("High resolution document is not supported"
		          " by client, image resolution %g lines/mm", yres);
	    return (send_reformat);
	}
	if (!modem->supportsVRes(yres)) {
	    emsg = fxStr::format("High resolution document is not supported"
		          " by modem, image resolution %g lines/mm", yres);
	    return (send_reformat);
	}
	params.vr = VR_FINE;
    } else
	params.vr = VR_NORMAL;

    /*
     * Select page length according to the image size and
     * vertical resolution.  Note that if the resolution
     * info is bogus, we may select the wrong page size.
     * Note also that we're a bit lenient in places here
     * to take into account sloppy coding practice (e.g.
     * using 200 dpi for high-res facsimile.
     */
    if (clientInfo.getMaxPageLengthInMM() != -1) {
	u_long h = 0;
	(void) TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
	float len = h / yres;			// page length in mm
	if ((int) len > clientInfo.getMaxPageLengthInMM()) {
	    emsg = fxStr::format("Client does not support document page length"
			  ", max remote page length %d mm"
			  ", image length %lu rows (%.2f mm)",
		clientInfo.getMaxPageLengthInMM(), h, len);
	    return (send_reformat);
	}
	if (!modem->supportsPageLength((u_int) len)) {
	    static const char* lengths[4] = {
		"297",		// A4 paper
		"364",		// B4 paper
		"<unlimited>",	// unlimited
		"<undefined>",	// US letter (used internally)
	    };
	    emsg = fxStr::format("Modem does not support document page length"
			  ", max page length %s mm"
			  ", image length %lu rows (%.2f mm)",
		lengths[modem->getBestPageLength()&3], h, len);
	    return (send_reformat);
	}
	// 330 is chosen 'cuz it's half way between A4 & B4 lengths
	params.ln = (len < 330 ? LN_A4 : LN_B4);
    } else
	params.ln = LN_INF;
    return (send_ok);
}

FaxSendStatus
FaxServer::sendSetupParams(TIFF* tif, Class2Params& params, const FaxMachineInfo& clientInfo, fxStr& emsg)
{
    FaxSendStatus status = sendSetupParams1(tif, params, clientInfo, emsg);
    if (status == send_ok) {
	traceProtocol("USE %s", params.pageWidthName());
	traceProtocol("USE %s", params.pageLengthName());
	traceProtocol("USE %s", params.verticalResName());
	traceProtocol("USE %s", params.dataFormatName());
    } else if (status == send_reformat) {
	traceServer(emsg);
    } else if (status == send_failed) {
	traceServer("REJECT: " | emsg);
    }
    return (status);
}

/*
 * Send Notification Support.
 */

void
FaxServer::notifyPageSent(FaxRequest& req, const char*)
{
    time_t now = Sys::now();
    req.npages++;			// count transmitted page
    req.writeQFile();			// update q file for clients
    traceProtocol("SEND FAX (%s): FROM %s TO %s (page %u of %u sent in %s)"
	, (const char*) req.commid
	, (const char*) req.mailaddr
	, (const char*) req.external
	, req.npages
	, req.totpages
	, fmtTime(now - pageStart)
    );
    pageStart = now;			// for next page
}

/*
 * Handle notification that a document has been successfully
 * transmitted.  We remove the file from the request array so
 * that it's not resent if the job is requeued.
 *
 * NB: Proper operation of the reference counting scheme used
 *     to handle delayed-removal of the imaged documents requires
 *     that the central scheduler be notified when a document
 *     is transmitted (so that it can update its global table
 *     of document uses); this is normally done in the derived
 *     class by overriding this method.
 */
void
FaxServer::notifyDocumentSent(FaxRequest& req, u_int fi)
{
    const faxRequest& freq = req.requests[fi];
    if (freq.op != FaxRequest::send_fax) {
	logError("notifyDocumentSent called for non-TIFF file");
	return;
    }
    traceProtocol("SEND FAX (%s): FROM %s TO %s (%s sent in %s)"
	, (const char*) req.commid
	, (const char*) req.mailaddr
	, (const char*) req.external
	, (const char*) freq.item
	, fmtTime(getFileTransferTime())
    );
    logInfo("SEND FAX: JOB %s SENT in %s"
	, (const char*) req.jobid
	, fmtTime(getFileTransferTime())
    );
    if (freq.op == FaxRequest::send_fax)
	req.renameSaved(fi);
    req.requests.remove(fi);
    req.writeQFile();
}
