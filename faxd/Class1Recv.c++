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

/*
 * EIA/TIA-578 (Class 1) Modem Driver.
 *
 * Receive protocol.
 */
#include <stdio.h>
#include "Class1.h"
#include "ModemConfig.h"
#include "HDLCFrame.h"
#include "StackBuffer.h"		// XXX

#include "t.30.h"
#include "Sys.h"
#include "config.h"

/*
 * Tell the modem to answer the phone.  We override
 * this method so that we can force the terminal's
 * flow control state to be setup to our liking.
 */
CallType
Class1Modem::answerCall(AnswerType type, fxStr& emsg)
{
    if (flowControl == FLOW_XONXOFF)
	setXONXOFF(FLOW_NONE, FLOW_NONE, ACT_FLUSH);
    return FaxModem::answerCall(type, emsg);
}

/*
 * Process an answer response from the modem.
 * Since some Class 1 modems do not give a connect
 * message that distinguishes between DATA and FAX,
 * we override the default handling of "CONNECT"
 * message here to force the high level code to
 * probe further.
 */
const AnswerMsg*
Class1Modem::findAnswer(const char* s)
{
    static const AnswerMsg answer[2] = {
    { "CONNECT ", 8,
      FaxModem::AT_NOTHING, FaxModem::OK, FaxModem::CALLTYPE_DATA },
    { "CONNECT",  7,
      FaxModem::AT_NOTHING, FaxModem::OK, FaxModem::CALLTYPE_UNKNOWN },
    };
    return strneq(s, answer[0].msg, answer[0].len) ? &answer[0] :
	   strneq(s, answer[1].msg, answer[1].len) ? &answer[1] :
	      FaxModem::findAnswer(s);
}

/*
 * Begin the receive protocol.
 */
bool
Class1Modem::recvBegin(fxStr& emsg)
{
    setInputBuffering(false);
    prevPage = false;				// no previous page received
    pageGood = false;				// quality of received page
    messageReceived = false;			// expect message carrier
    recvdDCN = false;				// haven't seen DCN
    lastPPM = FCF_DCN;				// anything will do
    sendCFR = false;				// TCF was not received

    fxStr nsf;
    encodeNSF(nsf, HYLAFAX_VERSION);

    return FaxModem::recvBegin(emsg) && recvIdentification(
	0, fxStr::null,
	0, fxStr::null,
	FCF_NSF|FCF_RCVR, nsf,
	FCF_CSI|FCF_RCVR, lid,
	FCF_DIS|FCF_RCVR, modemDIS(), modemXINFO(),
	conf.class1RecvIdentTimer, emsg);
}

/*
 * Transmit local identification and wait for the
 * remote side to respond with their identification.
 */
bool
Class1Modem::recvIdentification(
    u_int f1, const fxStr& pwd,
    u_int f2, const fxStr& addr,
    u_int f3, const fxStr& nsf,
    u_int f4, const fxStr& id,
    u_int f5, u_int dics, u_int xinfo,
    u_int timer, fxStr& emsg)
{
    u_int t1 = howmany(timer, 1000);		// in seconds
    u_int trecovery = howmany(conf.class1TrainingRecovery, 1000);
    time_t start = Sys::now();
    HDLCFrame frame(conf.class1FrameOverhead);
    bool framesSent;

    emsg = "No answer (T.30 T1 timeout)";
    /*
     * Transmit (PWD) (SUB) (CSI) DIS frames when the receiving
     * station or (PWD) (SEP) (CIG) DTC when initiating a poll.
     */
    if (f1) {
	startTimeout(3000);
	framesSent = sendFrame(f1, pwd, false);
	stopTimeout("sending PWD frame");
    } else if (f2) {
	startTimeout(3000);
	framesSent = sendFrame(f2, addr, false);
	stopTimeout("sending SUB/SEP frame");
    } else if (f3) {
	startTimeout(3000);
	framesSent = sendFrame(f3, (const u_char*)HYLAFAX_NSF, nsf, false);
	stopTimeout("sending NSF frame");
    } else {
	startTimeout(3000);
	framesSent = sendFrame(f4, id, false);
	stopTimeout("sending CSI/CIG frame");
    }
    for (;;) {
	if (framesSent) {
	    if (f1) {
		startTimeout(2550);
		framesSent = sendFrame(f2, addr, false);
		stopTimeout("sending SUB/SEP frame");
	    }
	    if (framesSent && f2) {
		startTimeout(2550);
		framesSent = sendFrame(f3, (const u_char*)HYLAFAX_NSF, nsf, false);
		stopTimeout("sending NSF frame");
	    }
	    if (framesSent && f3) {
		startTimeout(2550);
		framesSent = sendFrame(f4, id, false);
		stopTimeout("sending CSI/CIG frame");
	    }
	    if (framesSent) {
		startTimeout(2550);
		framesSent = sendFrame(f5, dics, xinfo);
		stopTimeout("sending DIS/DCS frame");
	    }
	}
	if (framesSent) {
	    /*
	     * Wait for a response to be received.
	     */
	    if (recvFrame(frame, conf.t4Timer)) {
		do {
		    /*
		     * Verify a DCS command response and, if
		     * all is correct, receive phasing/training.
		     */
		    if (!recvDCSFrames(frame)) {
			if (frame.getFCF() == FCF_DCN) {
			    emsg = "RSPREC error/got DCN";
			    recvdDCN = true;
				return (false);
			} else			// XXX DTC/DIS not handled
			    emsg = "RSPREC invalid response received";
			break;
		    }
		    if (recvTraining()) {
			emsg = "";
			return (true);
		    }
		    emsg = "Failure to train modems";
		    /*
		     * Reset the timeout to insure the T1 timer is
		     * used.  This is done because the adaptive answer
		     * strategy may setup a shorter timeout that's
		     * used to wait for the initial identification
		     * frame.  If we get here then we know the remote
		     * side is a fax machine and so we should wait
		     * the full T1 timeout, as specified by the protocol.
		     */
		    t1 = howmany(conf.t1Timer, 1000);
		} while (recvFrame(frame, conf.t2Timer));
	    }
	}
	/*
	 * We failed to send our frames or failed to receive
	 * DCS from the other side.  First verify there is
	 * time to make another attempt...
	 */
	if (Sys::now()+trecovery-start >= t1)
	    break;
	/*
	 * Delay long enough to miss any training that the
	 * other side might have sent us.  Otherwise the
	 * caller will miss our retransmission since it'll
	 * be in the process of sending training.
	 */
	pause(conf.class1TrainingRecovery);
	/*
	 * Retransmit ident frames.
	 */
	if (f1)
	    framesSent = transmitFrame(f1, pwd, false);
	else if (f2)
	    framesSent = transmitFrame(f2, addr, false);
	else if (f3)
	    framesSent = transmitFrame(f3, (const u_char*)HYLAFAX_NSF, nsf, false);
	else
	    framesSent = transmitFrame(f4, id, false);
    }
    return (false);
}

/*
 * Receive DCS preceded by any optional frames.
 */
bool
Class1Modem::recvDCSFrames(HDLCFrame& frame)
{
    fxStr s;
    do {
	switch (frame.getFCF()) {
	case FCF_PWD:
	    recvPWD(decodePWD(s, frame));
	    break;
	case FCF_SUB:
	    recvSUB(decodePWD(s, frame));
	    break;
	case FCF_TSI:
	    recvTSI(decodeTSI(s, frame));
	    break;
	case FCF_DCS:
	    processDCSFrame(frame);
	    break;
	}
    } while (frame.moreFrames() && recvFrame(frame, conf.t4Timer));
    return (frame.isOK() && frame.getFCF() == FCF_DCS);
}

/*
 * Receive training and analyze TCF.
 */
bool
Class1Modem::recvTraining()
{
    /*
     * It is possible (and with some modems likely) that the sending
     * system has not yet dropped its V.21 carrier.  So we follow the
     * reasoning behind Class 1.0's adaptive reception control in T.32 
     * 8.5.1 and the strategy documented in T.31 Appendix II.1: we issue
     * another +FRH and wait for NO CARRIER before looking for the high
     * speed carrier.  Even if the remote dropped its V.21 carrier at the
     * same moment that we received the signal, the remote still has to
     * wait 75 +/- 20 ms before sending us TCF as dictated by T.30
     * Chapter 5, Note 3.  T.31 alerts us to the possibility of an ERROR 
     * result instead of NO CARRIER due to line noise at carrier shut-off 
     * and that we should ignore the ERROR.
     *
     * This approach poses less risk of failure than previous methods
     * which simply ran through +FRM -> +FCERROR -> +FRM loops because
     * with each iteration of said loop we ran the risk of losing our
     * timing due to the DCE being deaf for a short period of time.  
     * Unfortunately, this routine will cause some modems (i.e. Zyxel
     * U336 and USR Courier 3367) to fail TCF reception.
     */
    if (conf.class1TCFRecvHack)
	atCmd(rhCmd, AT_NOCARRIER);

    protoTrace("RECV training at %s %s",
	modulationNames[curcap->mod],
	Class2Params::bitRateNames[curcap->br]);
    HDLCFrame buf(conf.class1FrameOverhead);
    bool ok = recvTCF(curcap->value, buf, frameRev, conf.class1TCFRecvTimeout);
    if (ok) {					// check TCF data
	u_int n = buf.getLength();
	u_int nonzero = 0;
	u_int zerorun = 0;
	u_int i = 0;
	/*
	 * Skip any initial non-zero training noise.
	 */
	while (i < n && buf[i] != 0)
	    i++;
	/*
	 * Determine number of non-zero bytes and
	 * the longest zero-fill run in the data.
	 */
	while (i < n) {
	    u_int j;
	    for (; i < n && buf[i] != 0; i++)
		nonzero++;
	    for (j = i; j < n && buf[j] == 0; j++)
		;
	    if (j-i > zerorun)
		zerorun = j-i;
	    i = j;
	}
	/*
	 * Our criteria for accepting is that there must be
	 * no more than 10% non-zero (bad) data and the longest
	 * zero-run must be at least at least 2/3'rds of the
	 * expected TCF duration.  This is a hack, but seems
	 * to work well enough.  What would be better is to
	 * anaylze the bit error distribution and decide whether
	 * or not we would receive page data with <N% error,
	 * where N is probably ~5.  If we had access to the
	 * modem hardware, the best thing that we could probably
	 * do is read the Eye Quality register (or similar)
	 * and derive an indicator of the real S/N ratio.
	 */
	u_int minrun = params.transferSize(conf.class1TCFMinRun);
	nonzero = (100*nonzero) / (n == 0 ? 1 : n);
	protoTrace("RECV: TCF %u bytes, %u%% non-zero, %u zero-run",
	    n, nonzero, zerorun);
	if (nonzero > conf.class1TCFMaxNonZero) {
	    protoTrace("RECV: reject TCF (too many non-zero, max %u%%)",
		conf.class1TCFMaxNonZero);
	    ok = false;
	}
	if (zerorun < minrun) {
	    protoTrace("RECV: reject TCF (zero run too short, min %u)", minrun);
	    ok = false;
	}
	(void) waitFor(AT_NOCARRIER);	// wait for message carrier to drop
    }
    /*
     * Send training response; we follow the spec
     * by delaying 75ms before switching carriers.
     */
    pause(conf.class1TCFResponseDelay);
    if (ok) {
	/*
	 * Send CFR later so that we can cancel
	 * session by DCN if it's needed. 
	 */
	sendCFR = true;
	protoTrace("TRAINING succeeded");
    } else {
	transmitFrame(FCF_FTT|FCF_RCVR);
	sendCFR = false;
	protoTrace("TRAINING failed");
    }
    return (ok);
}

/*
 * Process a received DCS frame.
 */
void
Class1Modem::processDCSFrame(const HDLCFrame& frame)
{
    u_int dcs = frame.getDIS();			// NB: really DCS
    u_int xinfo = frame.getXINFO();
    if (xinfo & DCSFRAME_64) frameSize = 64;
    else frameSize = 256;
    params.setFromDCS(dcs, xinfo);
    setDataTimeout(60, params.br);
    curcap = findSRCapability(dcs&DCS_SIGRATE, recvCaps);
    recvDCS(params);				// announce session params
}

const u_int Class1Modem::modemPPMCodes[8] = {
    0,			// 0
    PPM_EOM,		// FCF_EOM+FCF_PRI_EOM
    PPM_MPS,		// FCF_MPS+FCF_PRI_MPS
    0,			// 3
    PPM_EOP,		// FCF_EOP+FCF_PRI_EOP
    0,			// 5
    0,			// 6
    0,			// 7
};

/*
 * Receive a page of data.
 *
 * This routine is called after receiving training or after
 * sending a post-page response in a multi-page document.
 */
bool
Class1Modem::recvPage(TIFF* tif, u_int& ppm, fxStr& emsg)
{
    if (/* sendingHDLC */ lastPPM == FCF_MPS && prevPage && pageGood && !sentERR) {
	// sendingHDLC = false
	/*
	 * Resume sending HDLC frame (send data)
	 */
	startTimeout(2550);
	(void) sendFrame(FCF_MCF|FCF_RCVR);
	stopTimeout("sending HDLC frame");
    }

top:
    time_t t2end = 0;
    signalRcvd = 0;
    sentERR = false;

    do {
	u_int timer = conf.t2Timer;
	if (!messageReceived) {
	    if (sendCFR ) {
		transmitFrame(FCF_CFR|FCF_RCVR);
		sendCFR = false;
	    }
	    /*
	     * Look for message carrier and receive Phase C data.
	     */
	    setInputBuffering(true);
	    if (flowControl == FLOW_XONXOFF)
		(void) setXONXOFF(FLOW_NONE, FLOW_XONXOFF, ACT_FLUSH);

	    /*
	     * Same reasoning here as before receiving TCF.  In practice,
	     * however, we can't follow Class1TCFRecvHack because it
	     * apparently takes too much time to drop the V.21 carrier.  
	     * So, our approach is much like Class1SwitchingCmd.
	     */
	    if (!atCmd(conf.class1MsgRecvHackCmd, AT_OK)) {
		emsg = "Failure to receive silence.";
		return (false);
	    }

	    /*
	     * Set high speed carrier & start receive.  If the
	     * negotiated modulation technique includes short
	     * training, then we use it here (it's used for all
	     * high speed carrier traffic other than the TCF).
	     */
	    fxStr rmCmd(curcap[HasShortTraining(curcap)].value, rmCmdFmt);
	    ATResponse rmResponse = AT_NOTHING;
	    while (rmResponse != AT_CONNECT && rmResponse != AT_TIMEOUT && rmResponse != AT_ERROR) {
		(void) atCmd(rmCmd, AT_NOTHING);
		rmResponse = atResponse(rbuf, conf.t2Timer);
	    }
	    if (rmResponse == AT_CONNECT) {
		/*
		 * The message carrier was recognized;
		 * receive the Phase C data.
		 */
		protoTrace("RECV: begin page");
		recvSetupTIFF(tif, group3opts, FILLORDER_LSB2MSB);
		pageGood = recvPageData(tif, emsg);
		protoTrace("RECV: end page");
		if (!wasTimeout()) {
		    /*
		     * The data was received correctly, wait
		     * for the modem to signal carrier drop.
		     */
		    if (signalRcvd != 0) {
			messageReceived = true;
			prevPage = true;
		    } else {
			messageReceived = waitFor(AT_NOCARRIER, 2*1000);
			if (messageReceived)
			    prevPage = true;
			timer = conf.t1Timer;		// wait longer for PPM
		    }
		}
	    }
	    if (signalRcvd != 0) {
		if (flowControl == FLOW_XONXOFF)
		    (void) setXONXOFF(FLOW_NONE, FLOW_NONE, ACT_DRAIN);
		setInputBuffering(false);
	    }
	    if (!messageReceived && rmResponse != AT_FCERROR) {
		if (rmResponse != AT_ERROR) {
		    /*
		     * One of many things may have happened:
		     * o if we lost carrier, then some modems will return
		     *   AT_NOCARRIER or AT_EMPTYLINE in response to the
		     *   AT+FRM request.
		     * o otherwise, there may have been a timeout receiving
		     *   the message data, or there was a timeout waiting
		     *   for the carrier to drop.  Anything unexpected causes
		     *   us abort the receive to avoid looping.
		     * The only case that we don't abort on is that we found
		     * the wrong carrier, which means that there is an HDLC
		     * frame waiting for us--in which case it should get
		     * picked up below.
		     */
		    if (wasTimeout()) {
			abortReceive();		// return to command state
		    }
		    break;
		} else {
		    /*
		     * Some modems respond ERROR instead +FCERROR on wrong carrier
		     * and not return to command state.
		     */
		    abortReceive();		// return to command state
		}
	    }
	}
	/*
	 * T.30 says to process operator intervention requests
	 * here rather than before the page data is received.
	 * This has the benefit of not recording the page as
	 * received when the post-page response might need to
	 * be retransmited.
	 */
	if (abortRequested()) {
	    // XXX no way to purge TIFF directory
	    emsg = "Receive aborted due to operator intervention";
	    return (false);
	}

	/*
	 * Acknowledge PPM from ECM protocol.
	 */
	HDLCFrame frame(conf.class1FrameOverhead);
	bool ppmrcvd;
	if (signalRcvd != 0) {
	    ppmrcvd = true;
	    lastPPM = signalRcvd;
	} else {
	    ppmrcvd = recvFrame(frame, timer);
	    if (ppmrcvd) lastPPM = frame.getFCF();
	}
	/*
	 * Do command received logic.
	 */
	if (ppmrcvd) {
	    switch (lastPPM) {
	    case FCF_DIS:			// XXX no support
		if (prevPage && !pageGood) recvResetPage(tif);
		protoTrace("RECV DIS/DTC");
		emsg = "Can not continue after DIS/DTC";
		return (false);
	    case FCF_PWD:
	    case FCF_SUB:
	    case FCF_NSS:
	    case FCF_TSI:
	    case FCF_DCS:
		if (prevPage && !pageGood) recvResetPage(tif);
		// look for high speed carrier only if training successful
		messageReceived = !(
		       FaxModem::recvBegin(emsg)
		    && recvDCSFrames(frame)
		    && recvTraining()
		);
		break;
	    case FCF_MPS:			// MPS
	    case FCF_EOM:			// EOM
	    case FCF_EOP:			// EOP
	    case FCF_PRI_MPS:			// PRI-MPS
	    case FCF_PRI_EOM:			// PRI-EOM
	    case FCF_PRI_EOP:			// PRI-EOP
		if (prevPage && !pageGood) recvResetPage(tif);
		if (signalRcvd == 0) tracePPM("RECV recv", lastPPM);
		if (!prevPage) {
		    /*
		     * Post page message, but no previous page
		     * was received--this violates the protocol.
		     */
		    emsg = "COMREC invalid response received";
		    return (false);
		}

		/*
		 * As recommended in T.31 Appendix II.1, we try to
		 * prevent the rapid switching of the direction of 
		 * transmission by using +FRS.  Theoretically, "OK"
		 * is the only response, but if the sender has not
		 * gone silent, then we cannot continue anyway,
		 * and aborting here will give better information.
		 *
		 * Using +FRS is better than a software pause, which
		 * could not ensure loss of carrier.  +FRS is easier
		 * to implement than using +FRH and more reliable than
		 * using +FTS
		 */
		if (signalRcvd == 0 && !atCmd(conf.class1SwitchingCmd, AT_OK)) {
		    emsg = "Failure to receive silence.";
		    return (false);
		}

		/*
		 * [Re]transmit post page response.
		 */
		if (pageGood) {
		    if (!sentERR) {
			if (lastPPM == FCF_MPS && messageReceived) {
			    /*
			     * Start sending HDLC frame.
			     * The modem will report CONNECT and transmit training
			     * followed by flags until we begin sending data or
			     * 5 seconds elapse.
			     */
			    // sendingHDLC =
			    atCmd(thCmd, AT_CONNECT);
			} else {
			    (void) transmitFrame(FCF_MCF|FCF_RCVR);
			}
			tracePPR("RECV send", FCF_MCF);
		    }
		    /*
		     * If post page message confirms the page
		     * that we just received, write it to disk.
		     */
		    if (messageReceived) {
			TIFFWriteDirectory(tif);
			/*
			 * Reset state so that the next call looks
			 * first for page carrier or frame according
			 * to what's expected.  (Grr, where's the
			 * state machine...)
			 */
			messageReceived = (lastPPM == FCF_EOM);
			ppm = modemPPMCodes[lastPPM&7];
			return (true);
		    }
		} else {
		    /*
		     * Page not received, or unacceptable; tell
		     * other side to retransmit after retrain.
		     */
		    (void) transmitFrame(FCF_RTN|FCF_RCVR);
		    tracePPR("RECV send", FCF_RTN);
		    /*
		     * Reset the TIFF-related state so that subsequent
		     * writes will overwrite the previous data.
		     */
		    messageReceived = true;	// expect DCS next
		}
		break;
	    case FCF_DCN:			// DCN
		protoTrace("RECV recv DCN");
		emsg = "COMREC received DCN";
		recvdDCN = true;
		if (prevPage && conf.saveUnconfirmedPages) {
		    TIFFWriteDirectory(tif);
		    protoTrace("RECV keeping unconfirmed page");
		    return (true);
		}
		return (false);
	    default:
		if (prevPage && !pageGood) recvResetPage(tif);
		emsg = "COMREC invalid response received";
		return (false);
	    }
	    t2end = 0;
	} else {
	    /*
	     * If remote is on hook, then modem responces [+FC]ERROR
	     * or NO CARRIER. I only try to prevent looping (V.F.)
	     */
	    if (t2end) {
		if (Sys::now() > t2end)
		    break;
	    } else {
		t2end = Sys::now() + howmany(conf.t2Timer, 1000);
	    }
	}
    /*
     * We need to provide an escape from the do {...} while loop for EOM
     * because we were looking in the wrong place for the timeout, which
     * occurred at "Set high speed carrier & start receive."  (L.H.)
     */
    } while (!wasTimeout() && lastResponse != AT_EMPTYLINE && lastPPM != FCF_EOM);
    if (lastPPM == FCF_EOM) {
	/*
	 * Sigh, no state machine, have to do this the hard
	 * way.  After receipt of EOM if a subsequent frame
	 * receive times out then we must restart Phase B
	 * and redo training et. al.  However, because of the
	 * timeout, we need to achieve CONNECT first, just
	 * as we did following ATA back in the beginning.
	 */
	if (atCmd(thCmd, AT_NOTHING) && atResponse(rbuf, 0) == AT_CONNECT && recvBegin(emsg))
	    goto top;
    } else {
	emsg = "T.30 T2 timeout, expected page not received";
	if (prevPage && conf.saveUnconfirmedPages) {
	    TIFFWriteDirectory(tif);
	    protoTrace("RECV keeping unconfirmed page");
	    return (true);
	}
    }
    return (false);
}

void
Class1Modem::abortPageRecv()
{
    char c = CAN;				// anything other than DC1/DC3
    putModem(&c, 1, 1);
}

/*
 * Receive Phase C data in T.30-A ECM mode.
 */
bool
Class1Modem::recvPageECMData(TIFF* tif, const Class2Params& params, fxStr& emsg)
{
    HDLCFrame frame(5);					// A+C+FCF+FCS=5 bytes
    u_char* block = (u_char*) malloc(frameSize*256);	// 256 frames per block - totalling 16/64KB
    fxAssert(block != NULL, "ECM procedure error (receive block).");
    bool lastblock = false;
    u_short seq = 1;					// sequence code for the first block

    initializeDecoder(params);
    setupStartPage(tif, params);

    do {
	u_int fnum = 0;
	char ppr[32];					// 256 bits
	for (u_int i = 0; i < 32; i++) ppr[i] = 0xff;	// ppr defaults to all 1's, T.4 A.4.4
	u_short rcpcnt = 0;
	u_short pprcnt = 0;
	u_int fcount = 0;
	bool blockgood = false;
	do {
	    sentERR = false;
	    resetBlock();
	    signalRcvd = 0;
	    rcpcnt = 0;
	    bool dataseen = false;
	    if (syncECMFrame()) {
		time_t start = Sys::now();
		do {
		    frame.reset();
		    if (recvECMFrame(frame)) {
			if (frame[2] == 0x60) {		// FCF is FCD
			    dataseen = true;
			    rcpcnt = 0;			// reset RCP counter
			    fnum = frameRev[frame[3]];	// T.4 A.3.6.1 says LSB2MSB
			    protoTrace("RECV received frame number %u", fnum);
			    if (fcount < (fnum + 1)) fcount = fnum + 1;
			    // store received frame in block at position fnum (A+C+FCF+Frame No.=4 bytes)
			    for (u_int i = 0; i < frameSize; i++) {
				if (frame.getLength() - 6 > i)	// (A+C+FCF+Frame No.+FCS=6 bytes)
				    block[fnum*frameSize+i] = frameRev[frame[i+4]];	// LSB2MSB
			    }
			    if (frame.checkCRC()) {
				// valid frame, set the corresponding bit in ppr to 0
				u_int pprpos, pprval;
				for (pprpos = 0, pprval = fnum; pprval >= 8; pprval -= 8) pprpos++;
				if (ppr[pprpos] & frameRev[1 << pprval]) ppr[pprpos] ^= frameRev[1 << pprval];
			    } else {
				protoTrace("RECV frame FCS check failed");
			    }
			} else if (frame[2] == 0x61 && frame.checkCRC()) {	// FCF is RCP
			    rcpcnt++;
			} else {
			    dataseen = true;
			    protoTrace("HDLC frame with bad FCF %#x", frame[2]);
			}
		    } else {
			dataseen = true;	// assume that garbage was meant to be data
			syncECMFrame();
		    }
		    // some senders don't send the requisite three RCP signals
		} while (rcpcnt == 0 && (unsigned) Sys::now()-start < 5*60);	// can't expect 50 ms of flags, some violate T.4 A.3.8
		endECMBlock();				// wait for <DLE><ETX>
		(void) waitFor(AT_NOCARRIER);		// wait for message carrier to drop
		if (flowControl == FLOW_XONXOFF)
		    (void) setXONXOFF(FLOW_NONE, FLOW_NONE, ACT_DRAIN);
		setInputBuffering(false);
		bool gotpps = false;
		HDLCFrame ppsframe(conf.class1FrameOverhead);
		int recvFrameCount = 20;
		do {
		    gotpps = recvFrame(ppsframe, conf.t2Timer);
		} while (!wasTimeout() && !gotpps && recvFrameCount--);
		if (gotpps) {
		    tracePPM("RECV recv", ppsframe.getFCF());
		    tracePPM("RECV recv", ppsframe.getFCF2());
		    if (ppsframe.getFCF() == FCF_PPS) {
			// PPS is the only valid signal, Figure A.8/T.30
			u_int fc = frameRev[ppsframe[6]] + 1;
			if (fc == 256 && !dataseen) fc = 0;    // distinguish between 0 and 256
			if (fcount < fc) fcount = fc;
			protoTrace("RECV received %u frames of block %u of page %u", \
			    fc, frameRev[ppsframe[5]]+1, frameRev[ppsframe[4]]+1);
			blockgood = true;
			if (fc > 0) {	// assume that 0 frames means that sender is done
			    for (u_int i = 0; i <= (fcount - 1); i++) {
				u_int pprpos, pprval;
				for (pprpos = 0, pprval = i; pprval >= 8; pprval -= 8) pprpos++;
				if (ppr[pprpos] & frameRev[1 << pprval]) blockgood = false;
			    }
			}

			// requisite pause before sending response (PPR/MCF)
			if (!atCmd(conf.class1SwitchingCmd, AT_OK)) {
			    emsg = "Failure to receive silence.";
			    free(block);
			    return (false);
			}
			if (! blockgood) {
			    // inform the remote that one or more frames were invalid

			    atCmd(thCmd, AT_CONNECT);
			    startTimeout(3000);
			    sendFrame(FCF_PPR, fxStr(ppr, 32));
			    stopTimeout("sending PPR frame");
			    tracePPR("RECV send", FCF_PPR);

			    pprcnt++;
			    if (pprcnt == 4) {
				// expect sender to send CTC/EOR after every fourth PPR, not just the fourth
				protoTrace("RECV sent fourth PPR");
				pprcnt = 0;
				HDLCFrame rtnframe(conf.class1FrameOverhead);
				if (recvFrame(rtnframe, conf.t2Timer)) {
				tracePPM("RECV recv", rtnframe.getFCF());
				    u_int dcs;			// possible bits 1-16 of DCS in FIF
				    switch (rtnframe.getFCF()) {
					case FCF_CTC:
					    // use 16-bit FIF to alter speed, curcap
					    dcs = rtnframe[3] | (rtnframe[4]<<8);
					    curcap = findSRCapability(dcs&DCS_SIGRATE, recvCaps);
					    // requisite pause before sending response (CTR)
					    if (!atCmd(conf.class1SwitchingCmd, AT_OK)) {
						emsg = "Failure to receive silence.";
						free(block);
						return (false);
					    }
					    (void) transmitFrame(FCF_CTR|FCF_RCVR);
					    tracePPR("RECV send", FCF_CTR);
					    break;
					case FCF_EOR:
					    blockgood = true;
					    tracePPM("RECV recv", rtnframe.getFCF2());
					    switch (rtnframe.getFCF2()) {
						case 0:
						    // EOR-NULL partial page boundary
						    break;
						case FCF_EOM:
						case FCF_MPS:
						case FCF_EOP:
						case FCF_PRI_EOM:
						case FCF_PRI_MPS:
						case FCF_PRI_EOP:
						    lastblock = true;
						    signalRcvd = rtnframe.getFCF2();
						    break;
						default:
						    emsg = "COMREC invalid response to repeated PPR received";
						    free(block);
						    return (false);
					    }
					    // requisite pause before sending response (ERR)
					    if (!atCmd(conf.class1SwitchingCmd, AT_OK)) {
						emsg = "Failure to receive silence.";
						free(block);
						return (false);
					    }
					    (void) transmitFrame(FCF_ERR|FCF_RCVR);
					    tracePPR("RECV send", FCF_ERR);
					    sentERR = true;
					    break;
					default:
					    emsg = "COMREC invalid response to repeated PPR received";
					    free(block);
					    return (false);
				    }
				} else {
				    emsg = "T.30 T2 timeout, expected signal not received";
				    free(block);
				    return (false);
				}
			    }
			}
			if (signalRcvd == 0) {		// don't overwrite EOR settings
			    switch (ppsframe.getFCF2()) {
				case 0:
				    // PPS-NULL partial page boundary
				    break;
				case FCF_EOM:
				case FCF_MPS:
				case FCF_EOP:
				case FCF_PRI_EOM:
				case FCF_PRI_MPS:
				case FCF_PRI_EOP:
				    lastblock = true;
				    signalRcvd = ppsframe.getFCF2();
				    break;
				default:
				    emsg = "COMREC invalid post-page signal received";
				    free(block);
				    return (false);
			    }
			}
		    } else {
			emsg = "COMREC invalid response received (expected PPS)";
			free(block);
			return (false);
		    }
		} else {
		    emsg = "T.30 T2 timeout, expected signal not received";
		    free(block);
		    return (false);
		}
	    }
	    if (! blockgood) {		// back to high-speed carrier
		setInputBuffering(true);
		if (flowControl == FLOW_XONXOFF)
		    (void) setXONXOFF(FLOW_NONE, FLOW_XONXOFF, ACT_FLUSH);
		fxStr rmCmd(curcap[HasShortTraining(curcap)].value, rmCmdFmt);
		ATResponse response = AT_NOTHING;
		while (response != AT_CONNECT && response != AT_TIMEOUT && response != AT_ERROR) {
		    (void) atCmd(rmCmd, AT_NOTHING);
		    response = atResponse(rbuf, conf.t2Timer);
		}
		if (response == AT_TIMEOUT || response == AT_ERROR) {
		    free(block);
		    return (false);
		}
	    }
	} while (! blockgood);

	u_int cc = fcount * frameSize;
	if (lastblock) {
	    // trim zero padding
	    while (block[cc - 1] == 0) cc--;
	}
	// write the block to file
	if (lastblock) seq |= 2;			// seq code for the last block
	writeECMData(tif, block, cc, params, seq);
	seq = 0;					// seq code for in-between blocks

	if (! lastblock) {		// back to high-speed carrier
	    if (!sentERR) {
		// confirm block received as good
		(void) transmitFrame(FCF_MCF|FCF_RCVR);
		tracePPR("RECV send", FCF_MCF);
	    }
	    setInputBuffering(true);
	    if (flowControl == FLOW_XONXOFF)
		(void) setXONXOFF(FLOW_NONE, FLOW_XONXOFF, ACT_FLUSH);
	    fxStr rmCmd(curcap[HasShortTraining(curcap)].value, rmCmdFmt);
	    ATResponse response = AT_NOTHING;
	    while (response != AT_CONNECT && response != AT_TIMEOUT && response != AT_ERROR) {
		(void) atCmd(rmCmd, AT_NOTHING);
		response = atResponse(rbuf, conf.t2Timer);
	    }
	    if (response == AT_TIMEOUT || response == AT_ERROR) {
		free(block);
		return (false);
	    }
	}
    } while (! lastblock);

    free(block);
    recvEndPage(tif, params);

    return (true);    		// signalRcvd is set, full page is received...
}

/*
 * Receive Phase C data w/ or w/o copy quality checking.
 */
bool
Class1Modem::recvPageData(TIFF* tif, fxStr& emsg)
{
    /*
     * T.30-A ECM mode requires a substantially different protocol than non-ECM faxes.
     */
    if (params.ec & EC_ENABLE) (void) recvPageECMData(tif, params, emsg);
    else (void) recvPageDLEData(tif, checkQuality(), params, emsg);

    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, getRecvEOLCount());
    TIFFSetField(tif, TIFFTAG_CLEANFAXDATA, getRecvBadLineCount() ?
	CLEANFAXDATA_REGENERATED : CLEANFAXDATA_CLEAN);
    if (getRecvBadLineCount()) {
	TIFFSetField(tif, TIFFTAG_BADFAXLINES, getRecvBadLineCount());
	TIFFSetField(tif, TIFFTAG_CONSECUTIVEBADFAXLINES,
	    getRecvConsecutiveBadLineCount());
    }
    if (params.ec & EC_ENABLE) return (true);	// no RTN with ECM
    else return (isQualityOK(params));
}

/*
 * Complete a receive session.
 */
bool
Class1Modem::recvEnd(fxStr&)
{
    if (!recvdDCN) {
	u_int t1 = howmany(conf.t1Timer, 1000);	// T1 timer in seconds
	time_t start = Sys::now();
	/*
	 * Wait for DCN and retransmit ack of EOP if needed.
	 */
	HDLCFrame frame(conf.class1FrameOverhead);
	do {
	    if (recvFrame(frame, conf.t2Timer)) {
		switch (frame.getFCF()) {
		case FCF_EOP:
		    (void) transmitFrame(FCF_MCF|FCF_RCVR);
		    tracePPM("RECV recv", FCF_EOP);
		    tracePPR("RECV send", FCF_MCF);
		    break;
		case FCF_DCN:
		    break;
		default:
		    transmitFrame(FCF_DCN|FCF_RCVR);
		    break;
		}
	    } else if (!wasTimeout() && lastResponse != AT_FCERROR) {
		/*
		 * Beware of unexpected responses from the modem.  If
		 * we lose carrier, then we can loop here if we accept
		 * null responses, or the like.
		 */
		break;
	    }
	} while ((unsigned) Sys::now()-start < t1 &&
	    (!frame.isOK() || frame.getFCF() == FCF_EOP));
    }
    setInputBuffering(true);
    return (true);
}

/*
 * Abort an active receive session.
 */
void
Class1Modem::recvAbort()
{
    transmitFrame(FCF_DCN|FCF_RCVR);
    recvdDCN = true;				// don't hang around in recvEnd
}
