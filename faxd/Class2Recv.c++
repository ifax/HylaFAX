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
#include "Class2.h"
#include "ModemConfig.h"
#include "Sys.h"

#include "t.30.h"

/*
 * Recv Protocol for Class-2-style modems.
 */

static const AnswerMsg answerMsgs[] = {
{ "+FCO",	4,
  FaxModem::AT_NOTHING, FaxModem::OK,	    FaxModem::CALLTYPE_FAX },
{ "+FDM",	4,
  FaxModem::AT_NOTHING, FaxModem::OK,	    FaxModem::CALLTYPE_DATA },
{ "VCON",	4,
  FaxModem::AT_NOTHING, FaxModem::OK,	    FaxModem::CALLTYPE_VOICE },
};
#define	NANSWERS	(sizeof (answerMsgs) / sizeof (answerMsgs[0]))

const AnswerMsg*
Class2Modem::findAnswer(const char* s)
{
    for (u_int i = 0; i < NANSWERS; i++)
	if (strneq(s, answerMsgs[i].msg, answerMsgs[i].len))
	    return (&answerMsgs[i]);
    return FaxModem::findAnswer(s);
}

/*
 * Begin a fax receive session.
 */
bool
Class2Modem::recvBegin(Status& eresult)
{
    bool status = false;
    hangupCode[0] = '\0';
    hadHangup = false;
    ATResponse r;
    do {
	switch (r = atResponse(rbuf, 3*60*1000)) {
	case AT_NOANSWER:
	case AT_NOCARRIER:
	case AT_NODIALTONE:
	case AT_ERROR:
	case AT_TIMEOUT:
	case AT_EMPTYLINE:
	    processHangup("70");
	    eresult = hangupStatus(hangupCode);
	    return (false);
	case AT_FNSS:
	    // XXX parse and pass on to server
	    break;
	case AT_FSA:
	    recvSUB(stripQuotes(skipStatus(rbuf)));
	    break;
#ifdef notdef
	case AT_FPA:
	    recvSEP(stripQuotes(skipStatus(rbuf)));
	    break;
#endif
	case AT_FPW:
	    recvPWD(stripQuotes(skipStatus(rbuf)));
	    break;
	case AT_FTSI:
	    recvTSI(stripQuotes(skipStatus(rbuf)));
	    break;
	case AT_FDCS:
	    status = recvDCS(rbuf);
	    break;
	case AT_FHNG:
	    status = false;
	    break;
	}
    } while (r != AT_OK);
    if (!status)
	eresult = hangupStatus(hangupCode);
    return (status);
}

/*
 * Begin a fax receive session after EOM.
 */
bool
Class2Modem::recvEOMBegin(Status&)
{
    /*
     * There's nothing to do because the modem
     * does all of the protocol work.
     */
    return (true);
}

/*
 * Process a received DCS.
 */
bool
Class2Modem::recvDCS(const char* cp)
{
    if (parseClass2Capabilities(skipStatus(cp), params, false)) {
	params.update(false);
	setDataTimeout(60, params.br);
	FaxModem::recvDCS(params);	// announce session params
	return (true);
    } else {				// protocol botch
	processHangup("72");		// XXX "COMREC error"
	return (false);
    }
}

/*
 * Signal that we're ready to receive a page
 * and then collect the data.  Return the
 * received post-page-message.
 */
bool
Class2Modem::recvPage(TIFF* tif, u_int& ppm, Status& eresult, const fxStr& id)
{
    int ppr;
    bool prevPage = false;
    bool pageGood = false; 
    pageStarted = false;

    do {
	ppm = PPM_EOP;
	hangupCode[0] = 0;
	if (!atCmd("AT+FDR", AT_NOTHING))
	    goto bad;
	/*
	 * The spec says the modem is supposed to return CONNECT
	 * in response, but some modems such as the PPI PM14400FXMT
	 * PM28800FXMT return OK instead in between documents
	 * (i.e. when the previous page was punctuated with EOM).
	 */
	ATResponse r;
	do {
	    switch (r = atResponse(rbuf, conf.pageStartTimeout)) {
	    case AT_FDCS:			// inter-page DCS
		if (!pageGood) recvResetPage(tif);
		(void) recvDCS(rbuf);
		break;
	    case AT_FTSI:
		if (!pageGood) recvResetPage(tif);
		recvTSI(stripQuotes(skipStatus(rbuf)));
		break;
	case AT_FSA:
		if (!pageGood) recvResetPage(tif);
		recvSUB(stripQuotes(skipStatus(rbuf)));
		break;
#ifdef notdef
	case AT_FPA:
		if (!pageGood) recvResetPage(tif);
		recvSEP(stripQuotes(skipStatus(rbuf)));
		break;
#endif
	case AT_FPW:
		if (!pageGood) recvResetPage(tif);
		recvPWD(stripQuotes(skipStatus(rbuf)));
		break;
	    case AT_TIMEOUT:
	    case AT_EMPTYLINE:
	    case AT_ERROR:
	    case AT_NOCARRIER:
	    case AT_NODIALTONE:
	    case AT_NOANSWER:
		goto bad;
	    case AT_FHNG:			// remote hangup
		waitFor(AT_OK);
		goto bad;
	    }
	} while (r != AT_CONNECT && r != AT_OK);
	protoTrace("RECV: begin page");
	/*
	 * NB: always write data in LSB->MSB for folks that
	 *     don't understand the FillOrder tag!
	 */
	recvSetupTIFF(tif, group3opts, FILLORDER_LSB2MSB, id);
	if (!recvPageData(tif, eresult)) {
	    prevPage = false;
	    goto bad;
	}
	else {
	    prevPage = true;
	    if (!recvPPM(tif, ppr))
		goto bad;
	}
	if (!waitFor(AT_FET))		// post-page message status
	    goto bad;
	ppm = atoi(skipStatus(rbuf));
	tracePPM("RECV recv", ppm);
	// synchronization from modem
	/*
	 * Class 2 modems should always respond with OK.
	 * Class 2.0 modems may respond with ERROR if copy
	 * quality checking indicates the page is bad.
	 */
	if (!(waitFor(AT_OK) || lastResponse == AT_ERROR))
	    goto bad;
	/*
	 * T.30 says to process operator intervention requests
	 * here rather than before the page data is received.
	 * This has the benefit of not recording the page as
	 * received when the post-page response might need to
	 * be retransmited.
	 */
	if (abortRequested()) {
	    // XXX no way to purge TIFF directory
	    eresult = abortReason();
	    return (false);
	}
	// XXX deal with PRI interrupts
	/*
	 * If the host did the copy quality checking,
	 * then override the modem-specified post-page
	 * response according to the quality of the
	 * received page.
	 */
	if (hostDidCQ)
	    ppr = isQualityOK(params) ? PPR_MCF : PPR_RTN;
#if 0
        /*
         * RTN debug code: always respond with RTN to sending facsimile
         */
        ppr = PPR_RTN;
#endif
	if (ppr & 1) {
	    pageGood = true;
	    TIFFWriteDirectory(tif);	// complete page write
	} else
	    pageGood = false;
	tracePPR("RECV send", ppr);
	if (ppr & 1)			// page good, work complete
	    return (true);
    } while (!hostDidCQ || class2Cmd(ptsCmd, ppr));
bad:
    if (hangupCode[0] == 0)
	processHangup("90");			// "Unspecified Phase C error"
    eresult = hangupStatus(hangupCode);
    if (prevPage && conf.saveUnconfirmedPages) {
	TIFFWriteDirectory(tif);
	protoTrace("RECV keeping unconfirmed page");
	return (true);
    }
    return (false);
}

void
Class2Modem::abortPageRecv()
{
    char c = CAN;
    putModem(&c, 1, 1);
}

/*
 * Receive Phase C data using the Class 2 ``stream interface''.
 */
bool
Class2Modem::recvPageData(TIFF* tif, Status& eresult)
{
    // be careful about flushing here -- otherwise we can lose +FDB messages
    if (flowControl == FLOW_XONXOFF)
	(void) setXONXOFF(FLOW_NONE, FLOW_XONXOFF, ACT_DRAIN);
    protoTrace("RECV: send trigger 0%o", recvDataTrigger&0377);
    (void) putModem(&recvDataTrigger, 1);	// initiate data transfer

    /*
     * Have host do copy quality checking if the modem does not
     * support checking for this data format and if the configuration
     * parameters indicate CQ checking is to be done.
     *
     * If the modem is performing copy quality correction then
     * the host cannot perform copy quality checking.
     */
    if (serviceType == SERVICE_CLASS2)
	hostDidCQ = (modemCQ & BIT(params.df)) == 0 && checkQuality();
    else
	hostDidCQ = modemCQ == 0 && checkQuality();
    protoTrace("Copy quality checking performed by %s", hostDidCQ ? "host" : "modem");

    bool pageRecvd = recvPageDLEData(tif, hostDidCQ, params, eresult);

    // be careful about flushing here -- otherwise we lose +FPTS codes
    if (flowControl == FLOW_XONXOFF)
	(void) setXONXOFF(FLOW_XONXOFF, getInputFlow(), ACT_DRAIN);

    if (!pageRecvd)
	processHangup("91");			// "Missing EOL after 5 seconds"
    return (pageRecvd);
}

bool
Class2Modem::recvPPM(TIFF* tif, int& ppr)
{
    for (;;) {
	switch (atResponse(rbuf, conf.pageDoneTimeout)) {
	case AT_OK:
	    protoTrace("MODEM protocol botch: OK without +FPTS:");
	    /* fall thru... */
	case AT_TIMEOUT:
	case AT_EMPTYLINE:
	case AT_NOCARRIER:
	case AT_NODIALTONE:
	case AT_NOANSWER:
	case AT_ERROR:
	    processHangup("50");
	    return (false);
	case AT_FPTS:
	    return parseFPTS(tif, skipStatus(rbuf), ppr);
	case AT_FET:
	    protoTrace("MODEM protocol botch: +FET: without +FPTS:");
	    processHangup("100");		// "Unspecified Phase D error"
	    return (false);
	case AT_FHNG:
	    waitFor(AT_OK);			// resynchronize modem
	    return (false);
	}
    }
}

bool
Class2Modem::parseFPTS(TIFF* tif, const char* cp, int& ppr)
{
    u_long lc = 0;
    int blc = 0;
    int cblc = 0;
    ppr = 0;
    if (sscanf(cp, "%d,%ld,%d,%d", &ppr, &lc, &blc, &cblc) > 0) {

	/*
	 * In practice we cannot trust the modem line count when we're 
	 * not using ECM due to transmission errors and also due to
	 * bugs in the modems' own decoders (like MMR).
	 *
	 * Furthermore, there exists a discrepancy between many modem's
	 * behaviors and the specification.  Some give lc in hex and 
	 * others in decimal, and so this would further complicate things.
	 */
	if (!conf.class2UseLineCount) {
	    lc = getRecvEOLCount();
	}
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, lc);

	TIFFSetField(tif, TIFFTAG_CLEANFAXDATA, blc ?
	    CLEANFAXDATA_REGENERATED : CLEANFAXDATA_CLEAN);
	if (blc) {
	    TIFFSetField(tif, TIFFTAG_BADFAXLINES, (u_long) blc);
	    TIFFSetField(tif, TIFFTAG_CONSECUTIVEBADFAXLINES, cblc);
	}
	return (true);
    } else {
	protoTrace("MODEM protocol botch: \"%s\"; can not parse line count",
	    cp);
	processHangup("100");		// "Unspecified Phase D error"
	return (false);
    }
}

/*
 * Complete a receive session.
 */
bool
Class2Modem::recvEnd(Status&)
{
    if (!hadHangup) {
	if (isNormalHangup()) {
	    if (atCmd("AT+FDR", AT_FHNG))	// wait for DCN
		waitFor(AT_OK);
	} else
	    (void) atCmd(abortCmd);		// abort session
    }
    return (true);
}

/*
 * Abort an active receive session.
 */
void
Class2Modem::recvAbort()
{
    strcpy(hangupCode, "50");			// force abort in recvEnd
}
