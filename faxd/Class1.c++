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
 */
#include "Class1.h"
#include "ModemConfig.h"
#include "HDLCFrame.h"
#include "t.30.h"
#include "tiffio.h"

#include <stdlib.h>
#include <ctype.h>

const char* Class1Modem::modulationNames[6] = {
    "v.21, chan 2",
    "v.27ter fallback mode",
    "v.27ter",
    "v.29",
    "v.17",
    "v.33",
};
/*
 * Modem capabilities are queried at startup and a
 * table based on this is created for each modem.
 * This information is used to negotiate T.30 session
 * parameters (e.g. signalling rate).
 *
 * NB: v.17 w/ long training is the same as v.33, at
 *     least at 12000 and 14400.
 */
const Class1Cap Class1Modem::basicCaps[15] = {
    {  3,  0,	 	0,		     V21,   false }, // v.21
    {  24, BR_2400,	DCSSIGRATE_2400V27,  V27FB, false }, // v.27 ter
    {  48, BR_4800,	DCSSIGRATE_4800V27,  V27,   false }, // v.27 ter
    {  72, BR_7200,	DCSSIGRATE_7200V29,  V29,   false }, // v.29
    {  73, BR_7200,	DCSSIGRATE_7200V17,  V17,   false }, // v.17
    {  74, BR_7200,	DCSSIGRATE_7200V17,  V17,   false }, // v.17 w/st
    {  96, BR_9600,	DCSSIGRATE_9600V29,  V29,   false }, // v.29
    {  97, BR_9600,	DCSSIGRATE_9600V17,  V17,   false }, // v.17
    {  98, BR_9600,	DCSSIGRATE_9600V17,  V17,   false }, // v.17 w/st
    { 121, BR_12000,	DCSSIGRATE_12000V33, V33,   false }, // v.33
    { 121, BR_12000,	DCSSIGRATE_12000V17, V17,   false }, // v.17
    { 122, BR_12000,	DCSSIGRATE_12000V17, V17,   false }, // v.17 w/st
    { 145, BR_14400,	DCSSIGRATE_14400V33, V33,   false }, // v.33
    { 145, BR_14400,	DCSSIGRATE_14400V17, V17,   false }, // v.17
    { 146, BR_14400,	DCSSIGRATE_14400V17, V17,   false }, // v.17 w/st
};
#define	NCAPS	(sizeof (basicCaps) / sizeof (basicCaps[0]))

const char* Class1Modem::tmCmdFmt = "AT+FTM=%u";
const char* Class1Modem::rmCmdFmt = "AT+FRM=%u";

Class1Modem::Class1Modem(FaxServer& s, const ModemConfig& c)
    : FaxModem(s,c)
    , thCmd("AT+FTH=3")
    , rhCmd("AT+FRH=3")
{
    memcpy(xmitCaps, basicCaps, sizeof (basicCaps));
    memcpy(recvCaps, basicCaps, sizeof (basicCaps));
}

Class1Modem::~Class1Modem()
{
}

/*
 * Check if the modem is a Class 1 modem and,
 * if so, configure it for use.
 */
bool
Class1Modem::setupModem()
{
    if (!selectBaudRate(conf.maxRate, conf.flowControl, conf.flowControl))
	return (false);
    // Query service support information
    fxStr s;
    if (doQuery(conf.classQueryCmd, s, 500) && FaxModem::parseRange(s, modemServices))
	traceBits(modemServices & SERVICE_ALL, serviceNames);
    if ((modemServices & SERVICE_CLASS1) == 0)
	return (false);
    atCmd(conf.class1Cmd);

    /*
     * Query manufacturer, model, and firmware revision.
     * We use the manufacturer especially as a key to
     * working around firmware bugs (yech!).
     */
    if (setupManufacturer(modemMfr)) {
	modemCapability("Mfr " | modemMfr);
	modemMfr.raisecase();
    }
    (void) setupModel(modemModel);
    (void) setupRevision(modemRevision);
    if (modemModel != "")
	modemCapability("Model " | modemModel);
    if (modemRevision != "")
	modemCapability("Revision " | modemRevision);

    /*
     * Get modem capabilities and calculate best signalling
     * rate, data formatting capabilities, etc. for use in
     * T.30 negotiations.
     */
    if (!class1Query("AT+FTM=?", xmitCaps)) {
	serverTrace("Error parsing \"+FTM\" query response: \"%s\"", rbuf);
	return (false);
    }
    modemParams.br = 0;
    u_int i;
    for (i = 1; i < NCAPS; i++)
	if (xmitCaps[i].ok)
	    modemParams.br |= BIT(xmitCaps[i].br);
    modemParams.vr = VR_ALL;
    modemParams.wd = BIT(WD_1728) | BIT(WD_2048) | BIT(WD_2432);
    modemParams.ln = LN_ALL;
    modemParams.df = BIT(DF_1DMR) | BIT(DF_2DMR);
    modemParams.ec = EC_DISABLE;
    modemParams.bf = BF_DISABLE;
    modemParams.st = ST_ALL;
    traceModemParams();
    /*
     * Receive capabilities are maintained separately from
     * transmit capabilities because we need to know more
     * than the signalling rate to formulate the DIS.
     */ 
    if (!class1Query("AT+FRM=?", recvCaps)) {
	serverTrace("Error parsing \"+FRM\" query response: \"%s\"", rbuf);
	return (false);
    }
    u_int mods = 0;
    for (i = 1; i < NCAPS; i++)
	if (recvCaps[i].ok)
	    mods |= BIT(recvCaps[i].mod);
    switch (mods) {
    case BIT(V27FB):
	discap = DISSIGRATE_V27FB;
	break;
    case BIT(V27FB)|BIT(V27):
	discap = DISSIGRATE_V27;
	break;
    case BIT(V29):
	discap = DISSIGRATE_V29;
	break;
    case BIT(V27FB)|BIT(V27)|BIT(V29):
	discap = DISSIGRATE_V2729;
	break;
    case BIT(V27FB)|BIT(V27)|BIT(V29)|BIT(V33):
	discap = DISSIGRATE_V33;
	break;
    case BIT(V27FB)|BIT(V27)|BIT(V29)|BIT(V33)|BIT(V17):
	discap = DISSIGRATE_V17;
	break;
    }
    /*
     * T.30 specifies that HDCL frame data are in MSB bit
     * order except for CIG/TSI data which have LSB bit order.
     * We compose and interpret frame data in MSB bit order
     * and pass the frames through frameRev immediately before
     * transmitting and upon receiving to handle modem characteristics.
     *
     * Class1Modem::encodeTSI and Class1Modem::decodeTSI (below)
     * assume this work and process CIG/TSI data accordingly.
     */
    frameRev = TIFFGetBitRevTable(conf.frameFillOrder == FILLORDER_LSB2MSB);

    setupClass1Parameters();
    return (true);
}

/*
 * Send the modem the Class 1 configuration commands.
 */
bool
Class1Modem::setupClass1Parameters()
{
    if (modemServices & SERVICE_CLASS1) {
	atCmd(conf.class1Cmd);
	setupFlowControl(flowControl);
	atCmd(conf.setupAACmd);
    }
    return (true);
}

/*
 * Setup receive-specific parameters.
 */
bool
Class1Modem::setupReceive()
{
    return (true);			// nothing to do
}

/*
 * Send the modem any commands needed to force use of
 * the specified flow control scheme.
 */
bool
Class1Modem::setupFlowControl(FlowControl fc)
{
    switch (fc) {
    case FLOW_NONE:	return atCmd(conf.class1NFLOCmd);
    case FLOW_XONXOFF:	return atCmd(conf.class1SFLOCmd);
    case FLOW_RTSCTS:	return atCmd(conf.class1HFLOCmd);
    }
    return (true);
}

/*
 * Place the modem into the appropriate state
 * for sending/received facsimile.
 */
bool
Class1Modem::faxService()
{
    return (atCmd(conf.class1Cmd) && setupFlowControl(flowControl));
}

/*
 * Set the local subscriber identification.
 */
void
Class1Modem::setLID(const fxStr& number)
{
    encodeTSI(lid, number);
}

bool
Class1Modem::supportsPolling() const
{
    return (true);
}

/*
 * Construct a binary TSI/CIG string for transmission.  Note
 * that we do not enforce the specification that restricts
 * the alphabet to that of Table 3/T.30 (numeric digits,
 * hyphen, period, and space), but instead permit any
 * printable ASCII string of at most 20 characters to be
 * used.  Note also that the bit reversal is done with the
 * understanding that the encoded string is processed again
 * using frameRev (see above) when forming the transmitted
 * HDLC frame.
 */
void
Class1Modem::encodeTSI(fxStr& binary, const fxStr& ascii)
{
    u_int i, j;
    u_char buf[20];
    u_int n = fxmin(ascii.length(),(u_int) 20);
    for (i = 0, j = 0; i < n; i++) {
	char c = ascii[i];
	if (isprint(c) || c == ' ')
	    buf[j++] = frameRev[c];
    }
    /*
     * Now ``reverse copy'' the string.
     */
    binary.resize(20);
    for (i = 0; j > 0; i++, j--)
	binary[i] = buf[j-1];
    for (; i < 20; i++)
	binary[i] = frameRev[' '];	// blank pad remainder
}

/*
 * Do the inverse of encodeTSI; i.e. convert a binary
 * string of encoded digits into the equivalent ascii.
 * Note that as above (and per the spec) bytes are
 * presumed to be transmitted in LSB2MSB bit order and
 * then reversed on receipt according to the host bit
 * order.
 */
const fxStr&
Class1Modem::decodeTSI(fxStr& ascii, const HDLCFrame& binary)
{
    int n = binary.getFrameDataLength();
    if (n > 20)			// spec says no more than 20 digits
	n = 20;
    ascii.resize(n);
    u_int d = 0;
    bool seenDigit = false;
    for (const u_char* cp = binary.getFrameData() + n-1; n > 0; cp--, n--) {
	/*
	 * Accept any printable ascii.
	 */
	u_char c = frameRev[*cp];
        if (isprint(c) || c == ' ') {
	    if (c != ' ')
		seenDigit = true;
	    if (seenDigit)
		ascii[d++] = c;
	}
    }
    ascii.resize(d);
    return ascii;
}

/*
 * Construct a binary PWD/SUB/SEP string for transmission.
 */
void
Class1Modem::encodePWD(fxStr& binary, const fxStr& ascii)
{
    u_int n = fxmin(ascii.length(), (u_int) 20);
    binary.resize(n);
    for (u_int i = 0, j = n-1; i < n; i++, j--)
	binary[j] = frameRev[ascii[i]];
}

/*
 * Do the inverse of encodePWD; i.e. convert a binary
 * string of encoded digits into the equivalent ascii.
 */
const fxStr&
Class1Modem::decodePWD(fxStr& ascii, const HDLCFrame& binary)
{
    u_int n = fxmin(binary.getFrameDataLength(), (u_int) 20);
    ascii.resize(n);
    u_int d = 0;
    for (const u_char* cp = binary.getFrameData() + n-1; n > 0; cp--, n--) {
	u_char c = frameRev[*cp];
	if (isprint(c) || c == ' ')	// XXX accept only printable ascii
	    ascii[d++] = c;
    }
    return ascii;
}

/*
 * Pass data to modem, filtering DLE's and
 * optionally including the end-of-data
 * marker <DLE><ETX>.
 */
bool
Class1Modem::sendClass1Data(const u_char* data, u_int cc,
    const u_char* bitrev, bool eod)
{
    if (!putModemDLEData(data, cc, bitrev, getDataTimeout()))
	return (false);
    if (eod) {
	u_char buf[2];
	buf[0] = DLE;
	buf[1] = ETX;
	return (putModemData(buf, 2));
    } else
	return (true);
}

/*
 * Receive timed out, abort the operation
 * and resynchronize before returning.
 */
void
Class1Modem::abortReceive()
{
    bool b = wasTimeout();
    char c = CAN;			// anything other than DC1/DC3
    putModem(&c, 1, 1);
    /*
     * If the modem handles abort properly, then just
     * wait for an OK result.  Otherwise, wait a short
     * period of time, flush any input, and then send
     * "AT" and wait for the return "OK".
     */
    if (conf.class1RecvAbortOK == 0) {	// modem doesn't send OK response
	pause(200);
	flushModemInput();
	(void) atCmd("AT", AT_OK, 100);
    } else
	(void) waitFor(AT_OK, conf.class1RecvAbortOK);
    setTimeout(b);			// XXX putModem clobbers timeout state
}

/*
 * Receive an HDLC frame. The frame itself must
 * be received within 3 seconds (per the spec).
 * If a carrier is received, but the complete frame
 * is not received before the timeout, the receive
 * is aborted.
 */
bool
Class1Modem::recvRawFrame(HDLCFrame& frame)
{
    /*
     * The spec says that a frame that takes between
     * 2.55 and 3.45 seconds to be received may be
     * discarded; we also add some time for DCE
     * to detect and strip flags. 
     */
    startTimeout(5000);
    /*
     * Strip HDLC frame flags. This is not needed,
     * (according to the standard DCE does the job),
     * be we leave this legacy code unchanged
     * for sure - D.B.
     */
    int c;
    fxStr garbage;

    for (;;) {
	c = getModemChar(0);
	if (c == 0xff || c == EOF)
	    break;

	garbage.append(c);

	if ( garbage.length() >= 2 && garbage.tail(2) == "\r\n") {
	    /*
	     * CR+LF received before address field.
	     * We expect result code (though it's possible
	     * that CR+LF is a part of garbage frame)
	     */
	    garbage = garbage.head(garbage.length() - 2);
	    break;
	}
    }

    if (getHDLCTracing() && garbage.length()) {
	fxStr buf;
	u_int j = 0;
	for (u_int i = 0; i < garbage.length(); i++) {
	    if (j > 0)
		buf.append(' ');
	    buf.append(fxStr(garbage[i] & 0xFF, "%2.2X"));
	    j++;
	    if (j > 19) {
		protoTrace("--> [%u:%.*s]",
		    j, buf.length(), (const char*) buf);
		buf = "";
		j = 0;
	    }
	}
	if (j)
	    protoTrace("--> [%u:%.*s]",
		j, buf.length(), (const char*) buf);
	}

    if (c == 0xff) {			// address field received
	do {
	    if (c == DLE) {
		c = getModemChar(0);
		if (c == ETX || c == EOF)
		    break;
	    }
	    frame.put(frameRev[c]);
	} while ((c = getModemChar(0)) != EOF);
    }
    stopTimeout("receiving HDLC frame data");
    if (frame.getLength() > 0)
	traceHDLCFrame("-->", frame);
    if (wasTimeout()) {
	abortReceive();
	return (false);
    }
    /*
     * Now collect the "OK", "ERROR", or "FCERROR"
     * response telling whether or not the FCS was
     * legitimate.
     */
    if (!waitFor(AT_OK)) {
	if (lastResponse == AT_ERROR)
	    protoTrace("FCS error");
	return (false);
    }
    if (frame.getFrameDataLength() < 1) {
	protoTrace("HDLC frame too short (%u bytes)", frame.getLength());
	return (false);
    }
    if ((frame[1]&0xf7) != 0xc0) {
	protoTrace("HDLC frame with bad control field %#x", frame[1]);
	return (false);
    }
    frame.setOK(true);
    return (true);
}

#include "StackBuffer.h"

/*
 * Log an HLDC frame along with a time stamp (secs.10ms).
 */
void
Class1Modem::traceHDLCFrame(const char* direction, const HDLCFrame& frame)
{
    if (!getHDLCTracing())
	return;
    const char* hexdigits = "0123456789ABCDEF";
    fxStackBuffer buf;
    for (u_int i = 0; i < frame.getLength(); i++) {
	u_char b = frame[i];
	if (i > 0)
	    buf.put(' ');
	buf.put(hexdigits[b>>4]);
	buf.put(hexdigits[b&0xf]);
    }
    protoTrace("%s HDLC<%u:%.*s>", direction,
	frame.getLength(), buf.getLength(), (const char*) buf);
}

/*
 * Send a raw HDLC frame and wait for the modem response.
 * The modem is expected to be an HDLC frame sending mode
 * (i.e. +FTH=3 or similar has already be sent to the modem).
 * The T.30 max frame length is enforced with a 3 second
 * timeout on the send.
 */
bool
Class1Modem::sendRawFrame(HDLCFrame& frame)
{
    traceHDLCFrame("<--", frame);
    if (frame.getLength() < 3) {
	protoTrace("HDLC frame too short (%u bytes)", frame.getLength());
	return (false);
    }
    if (frame[0] != 0xff) {
	protoTrace("HDLC frame with bad address field %#x", frame[0]);
	return (false);
    }
    if ((frame[1]&0xf7) != 0xc0) {
	protoTrace("HDLC frame with bad control field %#x", frame[1]);
	return (false);
    }
    static u_char buf[2] = { DLE, ETX };
    return (putModemDLEData(frame, frame.getLength(), frameRev, 0) &&
	putModem(buf, 2, 0) &&
	waitFor(frame.moreFrames() ? AT_CONNECT : AT_OK, 0));
}

/*
 * Send a single byte frame.
 */
bool
Class1Modem::sendFrame(u_char fcf, bool lastFrame)
{
    HDLCFrame frame(conf.class1FrameOverhead);
    frame.put(0xff);
    frame.put(lastFrame ? 0xc8 : 0xc0);
    frame.put(fcf);
    return (sendRawFrame(frame));
}

/*
 * Send a frame with DCS/DIS.
 */
bool
Class1Modem::sendFrame(u_char fcf, u_int dcs, u_int xinfo, bool lastFrame)
{
    HDLCFrame frame(conf.class1FrameOverhead);
    frame.put(0xff);
    frame.put(lastFrame ? 0xc8 : 0xc0);
    frame.put(fcf);
    frame.put(dcs>>16);
    frame.put(dcs>>8);
    frame.put(dcs);
    if (dcs&(1<<0)) {			// send any optional bytes
	frame.put(xinfo>>24);
	if (xinfo&(1<<24)) {
	    frame.put(xinfo>>16);
	    if (xinfo&(1<<16)) {
		frame.put(xinfo>>8);
		if (xinfo&(1<<8))
		    frame.put(xinfo);
	    }
	}
    }
    return (sendRawFrame(frame));
}

/*
 * Send a frame with TSI/CSI/PWD/SUB/SEP.
 */
bool
Class1Modem::sendFrame(u_char fcf, const fxStr& tsi, bool lastFrame)
{
    HDLCFrame frame(conf.class1FrameOverhead);
    frame.put(0xff);
    frame.put(lastFrame ? 0xc8 : 0xc0);
    frame.put(fcf);
    frame.put((const u_char*)(const char*)tsi, tsi.length());
    return (sendRawFrame(frame));
}

bool
Class1Modem::transmitFrame(u_char fcf, bool lastFrame)
{
    startTimeout(2550);			// 3.0 - 15% = 2.55 secs
    bool frameSent =
	atCmd(thCmd, AT_NOTHING) &&
	atResponse(rbuf, 0) == AT_CONNECT &&
	sendFrame(fcf, lastFrame);
    stopTimeout("sending HDLC frame");
    return (frameSent);
}

bool
Class1Modem::transmitFrame(u_char fcf, u_int dcs, u_int xinfo, bool lastFrame)
{
    /*
     * The T.30 spec says no frame can take more than 3 seconds
     * (+/- 15%) to transmit.  We take the conservative approach.
     * and guard against the send exceeding the lower bound.
     */
    startTimeout(2550);			// 3.0 - 15% = 2.55 secs
    bool frameSent =
	atCmd(thCmd, AT_NOTHING) &&
	atResponse(rbuf, 0) == AT_CONNECT &&
	sendFrame(fcf, dcs, xinfo, lastFrame);
    stopTimeout("sending HDLC frame");
    return (frameSent);
}

bool
Class1Modem::transmitFrame(u_char fcf, const fxStr& tsi, bool lastFrame)
{
    startTimeout(3000);			// give more time than others
    bool frameSent =
	atCmd(thCmd, AT_NOTHING) &&
	atResponse(rbuf, 0) == AT_CONNECT &&
	sendFrame(fcf, tsi, lastFrame);
    stopTimeout("sending HDLC frame");
    return (frameSent);
}

/*
 * Send data using the specified signalling rate.
 */
bool
Class1Modem::transmitData(int br, u_char* data, u_int cc,
    const u_char* bitrev, bool eod)
{
    if (flowControl == FLOW_XONXOFF)
	setXONXOFF(FLOW_XONXOFF, FLOW_NONE, ACT_FLUSH);
    fxStr tmCmd(br, tmCmdFmt);
    bool ok = (atCmd(tmCmd, AT_CONNECT) &&
	sendClass1Data(data, cc, bitrev, eod) &&
	(eod ? waitFor(AT_OK) : true));
    if (flowControl == FLOW_XONXOFF)
	setXONXOFF(FLOW_NONE, FLOW_NONE, ACT_DRAIN);
    return (ok);
}

/*
 * Receive an HDLC frame.  The timeout is against
 * the receipt of the HDLC flags; the frame itself must
 * be received within 3 seconds (per the spec).
 * If a carrier is received, but the complete frame
 * is not received before the timeout, the receive
 * is aborted.
 */
bool
Class1Modem::recvFrame(HDLCFrame& frame, long ms)
{
    frame.reset();
    startTimeout(ms);
    bool readPending = atCmd(rhCmd, AT_NOTHING);
    if (readPending && waitFor(AT_CONNECT,0)){
        stopTimeout("waiting for HDLC flags");
        if (wasTimeout()){
            abortReceive();
            return (false);
        }
        return recvRawFrame(frame);
    }
    stopTimeout("waiting for v.21 carrier");
    if (readPending && wasTimeout())
	abortReceive();
    return (false);
}

/*
 * Receive TCF data using the specified signalling rate.
 */
bool
Class1Modem::recvTCF(int br, HDLCFrame& buf, const u_char* bitrev, long ms)
{
    buf.reset();
    if (flowControl == FLOW_XONXOFF)
	setXONXOFF(FLOW_NONE, FLOW_XONXOFF, ACT_DRAIN);
    startTimeout(ms);
    /*
     * Loop waiting for carrier or timeout.
     */
    bool readPending, gotCarrier;
    fxStr rmCmd(br, rmCmdFmt);
    do {
	readPending = atCmd(rmCmd, AT_NOTHING);
	gotCarrier = readPending && waitFor(AT_CONNECT, 0);
    } while (readPending && !gotCarrier && lastResponse == AT_FCERROR);
    /*
     * If carrier was recognized, collect the data.
     */
    bool gotData = false;
    if (gotCarrier) {
	int c = getModemChar(0);		// NB: timeout is to first byte
	stopTimeout("receiving TCF");
	if (c != EOF) {
	    buf.reset();
	    /*
	     * Use a 2 second timer to receive the 1.5
	     * second TCF--perhaps this is too long to
	     * permit us to send the nak in time?
	     */
	    startTimeout(2000);
	    do {
		if (c == DLE) {
		    c = getModemChar(0);
		    if (c == ETX) {
			gotData = true;
			break;
		    }
		    if (c == EOF) {
			break;
		    }
		}
		buf.put(bitrev[c]);
		if (buf.getLength() > 10000) {
		    setTimeout(true);
		    break;
		}

	    } while ((c = getModemChar(0)) != EOF);
	}
    }
    stopTimeout("receiving TCF");
    /*
     * If the +FRM is still pending, abort it.
     */
    if (readPending && wasTimeout())
	abortReceive();
    if (flowControl == FLOW_XONXOFF)
	setXONXOFF(FLOW_NONE, FLOW_NONE, ACT_DRAIN);
    return (gotData);
}

/* 
 * Modem manipulation support.
 */

/*
 * Reset a Class 1 modem.
 */
bool
Class1Modem::reset(long ms)
{
    return (FaxModem::reset(ms) && setupClass1Parameters());
}

ATResponse
Class1Modem::atResponse(char* buf, long ms)
{
    if (FaxModem::atResponse(buf, ms) == AT_OTHER && strneq(buf, "+FCERROR", 8))
	lastResponse = AT_FCERROR;
    return (lastResponse);
}

/*
 * Wait (carefully) for some response from the modem.
 */
bool
Class1Modem::waitFor(ATResponse wanted, long ms)
{
    for (;;) {
	ATResponse response = atResponse(rbuf, ms);
	if (response == wanted)
	    return (true);
	switch (response) {
	case AT_TIMEOUT:
	case AT_EMPTYLINE:
	case AT_ERROR:
	case AT_NOCARRIER:
	case AT_NODIALTONE:
	case AT_NOANSWER:
	case AT_OFFHOOK:
	case AT_RING:
	    modemTrace("MODEM %s", ATresponses[response]);
	    /* fall thru... */
	case AT_FCERROR:
	    return (false);
	}
    }
}

/*
 * Send <what>=? and get a range response.
 */
bool
Class1Modem::class1Query(const char* what, Class1Cap caps[])
{
    char response[1024];
    if (atCmd(what, AT_NOTHING) && atResponse(response) == AT_OTHER) {
	sync(5000);
	return (parseQuery(response, caps));
    }
    return (false);
}

/*
 * Map the DCS signalling rate to the appropriate
 * Class 1 capability entry.
 */
const Class1Cap*
Class1Modem::findSRCapability(u_short sr, const Class1Cap caps[])
{
    for (u_int i = NCAPS-1; i > 0; i--) {
	const Class1Cap* cap = &caps[i];
	if (cap->sr == sr) {
	    if (cap->mod == V17 && HasShortTraining(cap-1))
		cap--;
	    return (cap);
	}
    }
    // XXX should not happen...
    protoTrace("MODEM: unknown signalling rate %#x, using 9600 v.29", sr);
    return findSRCapability(DCSSIGRATE_9600V29, caps);
}

/*
 * Map the Class 2 bit rate to the best
 * signalling rate capability of the modem.
 */
const Class1Cap*
Class1Modem::findBRCapability(u_short br, const Class1Cap caps[])
{
    for (u_int i = NCAPS-1; i > 0; i--) {
	const Class1Cap* cap = &caps[i];
	if (cap->br == br && cap->ok) {
	    if (cap->mod == V17 && HasShortTraining(cap-1))
		cap--;
	    return (cap);
	}
    }
    protoTrace("MODEM: unsupported baud rate %#x", br);
    return NULL;
}

/*
 * Override the DIS signalling rate capabilities because
 * they are defined from the Class 2 parameter information
 * and do not include the modulation technique.
 */
u_int
Class1Modem::modemDIS() const
{
    // NB: DIS is in 24-bit format
    return (FaxModem::modemDIS() &~ DIS_SIGRATE) | (discap<<10) | DIS_XTNDFIELD;
}

/*
 * Return the 32-bit extended capabilities for the
 * modem for setting up the initial T.30 DIS when
 * receiving data.
 */
u_int
Class1Modem::modemXINFO() const
{
    return FaxModem::modemXINFO()
	| (1<<24) | (1<<16) | (1<<8)	// extension flags for 3 more bytes
	| DIS_SEP			// support for selected polling frame
	| DIS_SUB			// support for subaddressing frame
	| DIS_PWD			// support for pwd frames
	| DIS_1216			// additional page widths
	| DIS_864
	| DIS_1728H
	| DIS_1728L
#ifdef notdef
	| DIS_200X400			// additional resolutions
	| DIS_300X300
	| DIS_400X400
#endif
	;
}

const char COMMA = ',';
const char SPACE = ' ';

/*
 * Parse a Class 1 parameter string.
 */
bool
Class1Modem::parseQuery(const char* cp, Class1Cap caps[])
{
    while (cp[0]) {
	if (cp[0] == SPACE) {		// ignore white space
	    cp++;
	    continue;
	}
	if (!isdigit(cp[0]))
	    return (false);
	int v = 0;
	do {
	    v = v*10 + (cp[0] - '0');
	} while (isdigit((++cp)[0]));
	for (u_int i = 0; i < NCAPS; i++)
	    if (caps[i].value == v) {
		caps[i].ok = true;
		break;
	    }
	if (cp[0] == COMMA)		// <item>,<item>...
	    cp++;
    }
    return (true);
}
