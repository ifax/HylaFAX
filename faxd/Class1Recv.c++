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
Class1Modem::answerCall(AnswerType type, fxStr& emsg, const char* number)
{
    // Reset modemParams.br to non-V.34 settings.  If V.8 handshaking
    // succeeds, then it will be changed again.
    modemParams.br = nonV34br;

    if (flowControl == FLOW_XONXOFF)
	setXONXOFF(FLOW_NONE, FLOW_NONE, ACT_FLUSH);
    return ClassModem::answerCall(type, emsg, number);
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

    if (useV34 && !gotCTRL) waitForDCEChannel(true);	// expect control channel

    return FaxModem::recvBegin(emsg) && recvIdentification(
	0, fxStr::null,
	0, fxStr::null,
	FCF_NSF|FCF_RCVR, nsf,
	FCF_CSI|FCF_RCVR, lid,
	FCF_DIS|FCF_RCVR, modemDIS(), modemXINFO(),
	conf.class1RecvIdentTimer, emsg);
}

/*
 * Begin the receive protocol after an EOM signal.
 */
bool
Class1Modem::recvEOMBegin(fxStr& emsg)
{
    /*
     * We must raise the transmission carrier to mimic the state following ATA.
     */
    if (!useV34) {
	pause(conf.t2Timer);	// T.30 Fig 5.2B requires T2 to elapse
	if (!(atCmd(thCmd, AT_NOTHING) && atResponse(rbuf, 0) == AT_CONNECT))
	    return (false);
    }
    return Class1Modem::recvBegin(emsg);
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
    if (useV34) {
	sendCFR = true;
	return (true);
    }
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
	/*
	 * We expect the message carrier to drop.  However, some senders will
	 * transmit garbage after we see <DLE><ETX> but before we see NO CARRIER.
	 */
	time_t nocarrierstart = Sys::now();
	bool gotnocarrier = false;
	do {
	    gotnocarrier = waitFor(AT_NOCARRIER, 2*1000);
	} while (!gotnocarrier && Sys::now() < (nocarrierstart + 5));
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
    if (useV34) params.br = primaryV34Rate-1;
    else curcap = findSRCapability(dcs&DCS_SIGRATE, recvCaps);
    setDataTimeout(60, params.br);
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
Class1Modem::recvPage(TIFF* tif, u_int& ppm, fxStr& emsg, const fxStr& id)
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

    time_t t2end = 0;
    signalRcvd = 0;
    sentERR = false;
    prevPage = false;

    do {
	u_int timer = conf.t2Timer;
	if (!messageReceived) {
	    if (sendCFR ) {
		transmitFrame(FCF_CFR|FCF_RCVR);
		sendCFR = false;
	    }
	    pageGood = false;
	    recvSetupTIFF(tif, group3opts, FILLORDER_LSB2MSB, id);
	    ATResponse rmResponse = AT_NOTHING;
	    if (params.ec != EC_DISABLE) {
		pageGood = recvPageData(tif, emsg);
		messageReceived = true;
		prevPage = true;
	    } else {
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
		 *
		 * Timing here is very critical.  It is more "tricky" than timing
		 * for AT+FRM for TCF because unlike with TCF, where the direction
		 * of communication doesn't change, here it does change because 
		 * we just sent CFR but now have to do AT+FRM.  In practice, if we 
		 * issue AT+FRM after the sender does AT+FTM then we'll get +FCERROR.
		 * Using Class1MsgRecvHackCmd often only complicates the problem.
		 * If the modem doesn't drop its transmission carrier (OK response
		 * following CFR) quickly enough, then we'll see more +FCERROR.
		 */
		fxStr rmCmd(curcap[HasShortTraining(curcap)].value, rmCmdFmt);
		u_short attempts = 0;
		while ((rmResponse == AT_NOTHING || rmResponse == AT_FCERROR) && attempts++ < 20) {
		    (void) atCmd(rmCmd, AT_NOTHING);
		    rmResponse = atResponse(rbuf, conf.t2Timer);
		}
		if (rmResponse == AT_CONNECT) {
		    /*
		     * The message carrier was recognized;
		     * receive the Phase C data.
		     */
		    protoTrace("RECV: begin page");
		    pageGood = recvPageData(tif, emsg);
		    protoTrace("RECV: end page");
		    if (!wasTimeout()) {
			/*
			 * The data was received correctly, wait
			 * for the modem to signal carrier drop.
			 */
			time_t nocarrierstart = Sys::now();
			do {
			    messageReceived = waitFor(AT_NOCARRIER, 2*1000);
			} while (!messageReceived && Sys::now() < (nocarrierstart + 5));
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
		     *   for the carrier to drop.
		     */
		    if (!wasTimeout()) {
			/*
			 * We found the wrong carrier, which means that there
			 * is an HDLC frame waiting for us--in which case it
			 * should get picked up below.
			 */
			break;
		    }
		    /*
		     * The timeout expired - thus we missed the carrier either
		     * raising or dropping.
		     */
		    abortReceive();		// return to command state
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
			    if (!useV34) atCmd(thCmd, AT_CONNECT);
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
    } while (!wasTimeout() && lastResponse != AT_EMPTYLINE);
    emsg = "T.30 T2 timeout, expected page not received";
    if (prevPage && conf.saveUnconfirmedPages) {
	TIFFWriteDirectory(tif);
	protoTrace("RECV keeping unconfirmed page");
	return (true);
    }
    return (false);
}

void
Class1Modem::abortPageRecv()
{
    if (useV34) return;				// nothing to do in V.34
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
    bool pagedataseen = false;
    u_short seq = 1;					// sequence code for the first block

    do {
	u_int fnum = 0;
	char ppr[32];					// 256 bits
	for (u_int i = 0; i < 32; i++) ppr[i] = 0xff;	// ppr defaults to all 1's, T.4 A.4.4
	u_short rcpcnt = 0;
	u_short pprcnt = 0;
	u_int fcount = 0;
	u_short syncattempts = 0;
	bool blockgood = false;
	do {
	    sentERR = false;
	    resetBlock();
	    signalRcvd = 0;
	    rcpcnt = 0;
	    bool dataseen = false;
	    setInputBuffering(true);
	    if (flowControl == FLOW_XONXOFF)
		(void) setXONXOFF(FLOW_NONE, FLOW_XONXOFF, ACT_FLUSH);
	    if (!useV34) {
		if (!atCmd(conf.class1MsgRecvHackCmd, AT_OK)) {
		    emsg = "Failure to receive silence.";
		    return (false);
		}
		fxStr rmCmd(curcap[HasShortTraining(curcap)].value, rmCmdFmt);
		u_short attempts = 0;
		ATResponse response = AT_NOTHING;
		while ((response == AT_NOTHING || response == AT_FCERROR) && attempts++ < 20) {
		    (void) atCmd(rmCmd, AT_NOTHING);
		    response = atResponse(rbuf, conf.t2Timer);
		}
		if (response != AT_CONNECT) {
		    emsg = "Failed to properly detect high-speed data carrier.";
		    if (conf.saveUnconfirmedPages && pagedataseen) {
			protoTrace("RECV keeping unconfirmed page");
			writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
			prevPage = true;
		    }
		    free(block);
		    if (wasTimeout()) abortReceive();	// return to command mode
		    return (false);
		}
	    } else {
		if (!gotEOT) {
		    bool gotprimary = waitForDCEChannel(false);
		    u_short rtnccnt = 0;
		    while (!gotEOT && gotRTNC && rtnccnt++ < 3) {
			/*
			 * Remote requested control channel retrain; the remote
			 * didn't properly hear our last signal.  So now we have to
			 * wait for a signal from the remote and then respond appropriately
			 * to get us back in sync. DCS::CFR - PPS::PPR/MCF - EOR::ERR
			 */
			if (flowControl == FLOW_XONXOFF)
			    (void) setXONXOFF(FLOW_NONE, FLOW_NONE, ACT_DRAIN);
			setInputBuffering(false);
			HDLCFrame rtncframe(conf.class1FrameOverhead);
			if (recvFrame(rtncframe, conf.t2Timer)) {
			    switch (rtncframe.getFCF()) {
				case FCF_DCS:
				    // hopefully it didn't change on us!
				    transmitFrame(FCF_CFR|FCF_RCVR);
				    break;
				case FCF_PPS:
				    tracePPM("RECV recv", rtncframe.getFCF());
				    if (rtncframe.getLength() > 5) {
					tracePPM("RECV recv", rtncframe.getFCF2());
					switch (rtncframe.getFCF2()) {
					    case 0: 	// PPS-NULL
					    case FCF_EOM:
					    case FCF_MPS:
					    case FCF_EOP:
					    case FCF_PRI_EOM:
					    case FCF_PRI_MPS:
					    case FCF_PRI_EOP:
						if (pprcnt) {
						    sendFrame(FCF_PPR, fxStr(ppr, 32));
						    tracePPR("RECV send", FCF_PPR);
						} else {
						    (void) transmitFrame(FCF_MCF|FCF_RCVR);
						    tracePPR("RECV send", FCF_MCF);
						}
						break;
					}
				    }
				    break;
				case FCF_EOR:
				    tracePPM("RECV recv", rtncframe.getFCF());
				    if (rtncframe.getLength() > 5) {
					tracePPM("RECV recv", rtncframe.getFCF2());
					switch (rtncframe.getFCF2()) {
					    case 0: 	// PPS-NULL
					    case FCF_EOM:
					    case FCF_MPS:
					    case FCF_EOP:
					    case FCF_PRI_EOM:
					    case FCF_PRI_MPS:
					    case FCF_PRI_EOP:
						(void) transmitFrame(FCF_ERR|FCF_RCVR);
						tracePPR("RECV send", FCF_ERR);
						break;
					}
				    }
				    break;
			    }
			    setInputBuffering(true);
			    if (flowControl == FLOW_XONXOFF)
				(void) setXONXOFF(FLOW_NONE, FLOW_XONXOFF, ACT_FLUSH);
			    gotprimary = waitForDCEChannel(false);
			} else
			    gotprimary = false;
		    }
		    if (!gotprimary) {
			emsg = "Failed to properly open V.34 primary channel.";
			protoTrace(emsg);
			if (conf.saveUnconfirmedPages && pagedataseen) {
			    protoTrace("RECV keeping unconfirmed page");
			    writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
			    prevPage = true;
			}
			free(block);
			return (false);
		    }
		} else {
		    emsg = "Received premature V.34 termination.";
		    protoTrace(emsg);
		    if (conf.saveUnconfirmedPages && pagedataseen) {
			protoTrace("RECV keeping unconfirmed page");
			writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
			prevPage = true;
		    }
		    free(block);
		    return (false);
		}
	    }
	    if (useV34 || syncECMFrame()) {		// no synchronization needed w/V.34-fax
		time_t start = Sys::now();
		do {
		    frame.reset();
		    if (recvECMFrame(frame)) {
			if (frame[2] == 0x60) {		// FCF is FCD
			    dataseen = true;
			    pagedataseen = true;
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
			if (!useV34) syncECMFrame();
			if (useV34 && (gotEOT || gotCTRL)) rcpcnt = 3;
		    }
		    // some senders don't send the requisite three RCP signals
		} while (rcpcnt == 0 && (unsigned) Sys::now()-start < 5*60);	// can't expect 50 ms of flags, some violate T.4 A.3.8
		if (useV34) {
		    if (!gotEOT && !waitForDCEChannel(true)) {
			emsg = "Failed to properly open V.34 control channel.";
			protoTrace(emsg);
			if (conf.saveUnconfirmedPages && pagedataseen) {
			    protoTrace("RECV keeping unconfirmed page");
			    writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
			    prevPage = true;
			}
			free(block);
			return (false);
		    }
		    if (gotEOT) {
			emsg = "Received premature V.34 termination.";
			protoTrace(emsg);
			if (conf.saveUnconfirmedPages && pagedataseen) {
			    protoTrace("RECV keeping unconfirmed page");
			    writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
			    prevPage = true;
			}
			free(block);
			return (false);
		    }
		} else {
		    endECMBlock();				// wait for <DLE><ETX>
		}
		if (!useV34) {
		    // wait for message carrier to drop
		    time_t nocarrierstart = Sys::now();
		    bool gotnocarrier = false;
		    do {
			gotnocarrier = waitFor(AT_NOCARRIER, 2*1000);
		    } while (!gotnocarrier && Sys::now() < (nocarrierstart + 5));
		}
		if (flowControl == FLOW_XONXOFF)
		    (void) setXONXOFF(FLOW_NONE, FLOW_NONE, ACT_DRAIN);
		setInputBuffering(false);
		bool gotpps = false;
		HDLCFrame ppsframe(conf.class1FrameOverhead);
		u_short recvFrameCount = 0;
		do {
		    gotpps = recvFrame(ppsframe, conf.t2Timer);
		} while (!gotpps && !wasTimeout() && ++recvFrameCount < 20);
		if (gotpps) {
		    tracePPM("RECV recv", ppsframe.getFCF());
		    if (ppsframe.getLength() > 5) {
			// sender may violate T.30-A.4.3 and send another signal (i.e. DCN)
			tracePPM("RECV recv", ppsframe.getFCF2());
		    }
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
			if (!useV34 && !atCmd(conf.class1SwitchingCmd, AT_OK)) {
			    emsg = "Failure to receive silence.";
			    if (conf.saveUnconfirmedPages && pagedataseen) {
				protoTrace("RECV keeping unconfirmed page");
				writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
				prevPage = true;
			    }
			    free(block);
			    return (false);
			}
			if (! blockgood) {
			    // inform the remote that one or more frames were invalid

			    if (!useV34) atCmd(thCmd, AT_CONNECT);
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
					    if (useV34) {
						// T.30 F.3.4.5 Note 1 does not permit CTC in V.34-fax
						emsg = "Received invalid CTC signal in V.34-Fax.";
						if (conf.saveUnconfirmedPages && pagedataseen) {
						    protoTrace("RECV keeping unconfirmed page");
						    writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
						    prevPage = true;
						}
						free(block);
						return (false);
					    }
					    // use 16-bit FIF to alter speed, curcap
					    dcs = rtnframe[3] | (rtnframe[4]<<8);
					    curcap = findSRCapability(dcs&DCS_SIGRATE, recvCaps);
					    // requisite pause before sending response (CTR)
					    if (!atCmd(conf.class1SwitchingCmd, AT_OK)) {
						emsg = "Failure to receive silence.";
						if (conf.saveUnconfirmedPages && pagedataseen) {
						    protoTrace("RECV keeping unconfirmed page");
						    writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
						    prevPage = true;
						}
						free(block);
						return (false);
					    }
					    (void) transmitFrame(FCF_CTR|FCF_RCVR);
					    tracePPR("RECV send", FCF_CTR);
					    break;
					case FCF_EOR:
					    tracePPM("RECV recv", rtnframe.getFCF2());
					    /*
					     * It may be wise to disconnect here if MMR is being
					     * used because there will surely be image data loss.
					     * However, since the sender knows what the extent of
					     * the data loss will be, we'll naively assume that
					     * the sender knows what it's doing, and we'll
					     * proceed as instructed by it.
					     */
					    blockgood = true;
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
						    if (conf.saveUnconfirmedPages && pagedataseen) {
							protoTrace("RECV keeping unconfirmed page");
							writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
							prevPage = true;
						    }
						    free(block);
						    return (false);
					    }
					    // requisite pause before sending response (ERR)
					    if (!useV34 && !atCmd(conf.class1SwitchingCmd, AT_OK)) {
						emsg = "Failure to receive silence.";
						if (conf.saveUnconfirmedPages && pagedataseen) {
						    protoTrace("RECV keeping unconfirmed page");
						    writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
						    prevPage = true;
						}
						free(block);
						return (false);
					    }
					    (void) transmitFrame(FCF_ERR|FCF_RCVR);
					    tracePPR("RECV send", FCF_ERR);
					    sentERR = true;
					    break;
					default:
					    emsg = "COMREC invalid response to repeated PPR received";
					    if (conf.saveUnconfirmedPages && pagedataseen) {
						protoTrace("RECV keeping unconfirmed page");
						writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
						prevPage = true;
					    }
					    free(block);
					    return (false);
				    }
				} else {
				    emsg = "T.30 T2 timeout, expected signal not received";
				    if (conf.saveUnconfirmedPages && pagedataseen) {
					protoTrace("RECV keeping unconfirmed page");
					writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
					prevPage = true;
				    }
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
				    if (conf.saveUnconfirmedPages && pagedataseen) {
					protoTrace("RECV keeping unconfirmed page");
					writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
					prevPage = true;
				    }
				    free(block);
				    return (false);
			    }
			}
		    } else {
			emsg = "COMREC invalid response received (expected PPS)";
			if (conf.saveUnconfirmedPages && pagedataseen) {
			    protoTrace("RECV keeping unconfirmed page");
			    writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
			    prevPage = true;
			}
			free(block);
			return (false);
		    }
		} else {
		    emsg = "T.30 T2 timeout, expected signal not received";
		    if (conf.saveUnconfirmedPages && pagedataseen) {
			protoTrace("RECV keeping unconfirmed page");
			writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
			prevPage = true;
		    }
		    free(block);
		    return (false);
		}
	    } else {
		if (syncattempts++ > 20) {
		    emsg = "Cannot synchronize ECM frame reception.";
		    if (conf.saveUnconfirmedPages) {
			protoTrace("RECV keeping unconfirmed page");
			writeECMData(tif, block, (fcount * frameSize), params, (seq |= 2));
			prevPage = true;
		    }
		    free(block);
		    return(false);
		}
	    }
	} while (! blockgood);

	u_int cc = fcount * frameSize;
	if (lastblock) {
	    // trim zero padding
	    while (cc > 0 && block[cc - 1] == 0) cc--;
	}
	// write the block to file
	if (lastblock) seq |= 2;			// seq code for the last block
	writeECMData(tif, block, cc, params, seq);
	seq = 0;					// seq code for in-between blocks

	if (!lastblock && !sentERR) {
	    // confirm block received as good
	    (void) transmitFrame(FCF_MCF|FCF_RCVR);
	    tracePPR("RECV send", FCF_MCF);
	}
    } while (! lastblock);

    free(block);
    recvEndPage(tif, params);

    if (getRecvEOLCount() == 0) {
	// Just because image data blocks are received properly doesn't guarantee that
	// those blocks actually contain image data.  If the decoder finds no image
	// data at all we send DCN instead of MCF in hopes of a retransmission.
	emsg = "ECM page received containing no image data.";
	return (false);
    }
    return (true);   		// signalRcvd is set, full page is received...
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
    if (params.ec != EC_DISABLE) {
	if (!recvPageECMData(tif, params, emsg)) {
	    /*
	     * The previous page experienced some kind of error.  Falsify
	     * some event settings in order to cope with the error gracefully.
	     */
	    signalRcvd = FCF_EOP;
	    messageReceived = true;
	    sentERR = true;
	    if (prevPage)
		recvEndPage(tif, params);
	}
    } else (void) recvPageDLEData(tif, checkQuality(), params, emsg);

    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, getRecvEOLCount());
    TIFFSetField(tif, TIFFTAG_CLEANFAXDATA, getRecvBadLineCount() ?
	CLEANFAXDATA_REGENERATED : CLEANFAXDATA_CLEAN);
    if (getRecvBadLineCount()) {
	TIFFSetField(tif, TIFFTAG_BADFAXLINES, getRecvBadLineCount());
	TIFFSetField(tif, TIFFTAG_CONSECUTIVEBADFAXLINES,
	    getRecvConsecutiveBadLineCount());
    }
    if (params.ec != EC_DISABLE) return (true);	// no RTN with ECM
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
    if (useV34) {
	// terminate V.34 channel
	u_char buf[2];
	buf[0] = DLE; buf[1] = EOT;		// <DLE><EOT>
	putModemData(buf, 2);
	// T.31-A1 samples indicate an OK response, but anything is acceptable
	waitFor(AT_OK, 60000);
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
