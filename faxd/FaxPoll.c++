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
#include <stdio.h>
#include "Sys.h"
#include "faxApp.h"			// XXX
#include "FaxServer.h"
#include "FaxRecvInfo.h"

/*
 * FAX Server Polling Protocol.
 */

/*
 * Initiate a polling receive and invoke the receiving protocol.
 */
fxBool
FaxServer::pollFaxPhaseB(const fxStr& sep, const fxStr& pwd, FaxRecvInfoArray& docs, fxStr& emsg)
{
    fxBool pollOK = FALSE;
    changeState(RECEIVING);
    traceProtocol("POLL FAX: begin (SEP \"%s\", PWD \"%s\")",
	(const char*) sep, (const char*) pwd);
    /*
     * Create the first file ahead of time to avoid timing
     * problems with Class 1 modems.  (Creating the file
     * after recvBegin can cause part of the first page to
     * be lost.)
     */
    FaxRecvInfo info;
    TIFF* tif = setupForRecv(info, docs, emsg);
    if (tif) {
	recvPages = 0;			// count of received pages
	fileStart = Sys::now();		// count initial negotiation on failure
	if (modem->pollBegin(canonicalizePhoneNumber(FAXNumber), sep, pwd, emsg)) {
	    pollOK = recvDocuments(tif, info, docs, emsg);
	    if (!pollOK)
		traceProtocol("POLL FAX: %s", (const char*) emsg);
	    if (!modem->recvEnd(emsg))
		traceProtocol("POLL FAX: %s", (const char*) emsg);
	} else
	    traceProtocol("POLL FAX: %s", (const char*) emsg);
    } else
	traceProtocol("POLL FAX: %s", (const char*) emsg);
    traceProtocol("POLL FAX: end");
    return (pollOK);
}

/*
 * Handle notification of a document received as a
 * result of a poll request.
 */
void
FaxServer::notifyPollRecvd(FaxRequest&, const FaxRecvInfo&)
{
}

/*
 * Handle notification that a poll operation has been
 * successfully completed. 
 */
void
FaxServer::notifyPollDone(FaxRequest& req, u_int pi)
{
    if (req.requests[pi].op == FaxRequest::send_poll) {
	req.requests.remove(pi);
	req.writeQFile();
    } else
	logError("notifyPollDone called for non-poll request");
}
