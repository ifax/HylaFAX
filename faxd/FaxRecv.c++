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
FaxServer::recvFax(const CallerID& cid)
{
    traceProtocol("RECV FAX: begin");

    fxStr emsg;
    FaxRecvInfoArray docs;
    FaxRecvInfo info;
    bool faxRecognized = false;
    abortCall = false;

    /*
     * Create the first file ahead of time to avoid timing
     * problems with Class 1 modems.  (Creating the file
     * after recvBegin can cause part of the first page to
     * be lost.)
     */
    TIFF* tif = setupForRecv(info, docs, emsg);
    if (tif) {
	recvPages = 0;			// total count of received pages
	fileStart = Sys::now();		// count initial negotiation on failure
	if (faxRecognized = modem->recvBegin(emsg)) {
	    // NB: partially fill in info for notification call
	    if (!modem->getRecvTSI(info.sender))
		info.sender = "<UNSPECIFIED>";
	    notifyRecvBegun(info);
	    if (!recvDocuments(tif, info, docs, emsg)) {
		traceProtocol("RECV FAX: %s", (const char*) emsg);
		modem->recvAbort();
	    }
	    if (!modem->recvEnd(emsg))
		traceProtocol("RECV FAX: %s", (const char*) emsg);
	} else {
	    modem->trainingFailed();		// wait until after QualifyTSI
	    traceProtocol("RECV FAX: %s", (const char*) emsg);
	    TIFFClose(tif);
	}
    } else
	traceServer("RECV FAX: %s", (const char*) emsg);
    /*
     * Now that the session is completed, do local processing
     * that might otherwise slow down the protocol (and potentially
     * cause timing problems).
     */
    for (u_int i = 0, n = docs.length(); i < n; i++) {
	const FaxRecvInfo& ri = docs[i];
	if (ri.npages == 0)
	    Sys::unlink(ri.qfile);
	else
	    Sys::chmod(ri.qfile, recvFileMode);
	if (faxRecognized)
	    // It would be cleaner and more versatile to include
	    // cid as part of ri now instead of continuing to
	    // pass it along.  This would require alterations to
	    // FaxRecvInfo, though - not as simple an approach.
	    notifyRecvDone(ri, cid);
    }
    traceProtocol("RECV FAX: end");
    return (faxRecognized);
}

#define	MAXSEQNUM	99999		// should be good enough, can be larger
#define	NEXTSEQNUM(x)	((x)+1 >= MAXSEQNUM ? 1 : (x)+1)
#define	FAX_RECVSEQF	FAX_RECVDIR "/" FAX_SEQF

int
FaxServer::getRecvFile(fxStr& qfile, fxStr& emsg)
{
    int fseqf = Sys::open(FAX_RECVSEQF, O_CREAT|O_RDWR, 0644);
    if (fseqf < 0) {
	emsg = fxStr::format("cannot open %s: %s",
	    FAX_RECVSEQF, strerror(errno));
	return (-1);
    }
    flock(fseqf, LOCK_EX);
    u_int seqnum = 1;			// avoid 0 'cuz atoi returns it on error
    char line[16];
    int n = read(fseqf, line, sizeof (line));
    line[n < 0 ? 0 : n] = '\0';
    if (n > 0)
	seqnum = atoi(line);
    if (seqnum < 1 || seqnum >= MAXSEQNUM) {
	traceServer("RECV: Bad sequence number \"%s\", reset to 1", line);
	seqnum = 1;
    }
    /*
     * Probe to find a valid filename.
     */
    int ftmp;
    int ntry = 1000;			// that should be a lot!
    do {
	qfile = fxStr::format(FAX_RECVDIR "/fax%05u.tif", seqnum);
	ftmp = Sys::open(qfile, O_RDWR|O_CREAT|O_EXCL, recvFileMode);
	seqnum = NEXTSEQNUM(seqnum);    // Increment _after_ attempting to open
    } while (ftmp < 0 && errno == EEXIST && --ntry >= 0);
    if (ftmp >= 0) {
	/*
	 * Got a file to store received data in,
	 * lock it so clients can see the receive
	 * is active; then update the sequence
	 * number file to reflect the allocation.
	 */
        (void) flock(ftmp, LOCK_EX|LOCK_NB);
        fxStr snum = fxStr::format("%u", seqnum);
        int len = snum.length();
        (void) lseek(fseqf, 0, SEEK_SET);
        if (Sys::write(fseqf, (const char*)snum, len) != len ||
                ftruncate(fseqf, len)) {
            emsg = fxStr::format("error updating %s: %s",
                FAX_RECVSEQF, strerror(errno));
            Sys::unlink(qfile);
            Sys::close(ftmp), ftmp = -1;
        }
    } else {
        emsg = "failed to find unused filename";
    }
    Sys::close(fseqf);			// NB: implicit unlock
    return (ftmp);
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
    int ppm;
    pageStart = Sys::now();
    for (;;) {
	modem->getRecvSUB(info.subaddr);		// optional subaddress
	if (!modem->getRecvTSI(info.sender))		// optional TSI
	    info.sender = "<UNSPECIFIED>";
	if (qualifyTSI != "") {
	    /*
	     * Check a received TSI against the list of acceptable
	     * TSI patterns defined for the server.  This form of
	     * access control depends on the sender passing a valid
	     * TSI.  Note that to accept/reject an unspecified TSI
	     * one should match "<UNSPECIFIED>".
	     *
	     * NB: Caller-ID access control is done elsewhere; prior
	     *     to answering a call.
	     */
	    bool okToRecv = isTSIOk(info.sender);
	    traceServer("%s TSI \"%s\"", okToRecv ? "ACCEPT" : "REJECT",
		(const char*) info.sender);
	    if (!okToRecv) {
		emsg = "Permission denied (unacceptable client TSI)";
		info.time = (u_int) getFileTransferTime();
		info.reason = emsg;
		notifyDocumentRecvd(info);
		TIFFClose(tif);
		return (false);
	    }
	}
	modem->trainingSucceeded();		// wait until after QualifyTSI
	setServerStatus("Receiving from \"%s\"", (const char*) info.sender);
	recvOK = recvFaxPhaseD(tif, info, ppm, emsg);
	TIFFClose(tif);
	info.time = (u_int) getFileTransferTime();
	info.reason = emsg;
	docs[docs.length()-1] = info;
	notifyDocumentRecvd(info);
	if (!recvOK || ppm == PPM_EOP)
	    return (recvOK);
	/*
	 * Setup state for another file.
	 */
	tif = setupForRecv(info, docs, emsg);
	if (tif == NULL)
	    return (false);
	fileStart = pageStart = Sys::now();
    }
    /*NOTREACHED*/
}

/*
 * Receive Phase B protocol processing.
 */
bool
FaxServer::recvFaxPhaseD(TIFF* tif, FaxRecvInfo& info, int& ppm, fxStr& emsg)
{
    ppm = PPM_EOP;
    do {
	if (++recvPages > maxRecvPages) {
	    emsg = "Maximum receive page count exceeded, job terminated";
	    return (false);
	}
	if (!modem->recvPage(tif, ppm, emsg))
	    return (false);
	info.npages++;
	info.time = (u_int) getPageTransferTime();
	info.params = modem->getRecvParams();
	notifyPageRecvd(tif, info, ppm);
	if (PPM_PRI_MPS <= ppm && ppm <= PPM_PRI_EOP) {
	    emsg = "Procedure interrupt received, job terminated";
	    return (false);
	}
	pageStart = Sys::now();			// reset for next page
    } while (ppm == PPM_MPS || ppm == PPM_PRI_MPS);
    return (true);
}

void
FaxServer::notifyRecvBegun(const FaxRecvInfo&)
{
}

/*
 * Handle notification that a page has been received.
 */
void
FaxServer::notifyPageRecvd(TIFF*, const FaxRecvInfo& ri, int)
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
FaxServer::notifyDocumentRecvd(const FaxRecvInfo& ri)
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
FaxServer::notifyRecvDone(const FaxRecvInfo& ri, const CallerID& cid)
{
    if (ri.reason != "")
	traceServer("RECV FAX (%s): session with %s terminated abnormally: %s"
	    , (const char*) ri.commid
	    , (const char*) ri.sender
	    , (const char*) ri.reason
	);
}
