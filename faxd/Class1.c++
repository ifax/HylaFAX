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
#include "Sys.h"
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
    messageReceived = false;
    memcpy(xmitCaps, basicCaps, sizeof (basicCaps));
    memcpy(recvCaps, basicCaps, sizeof (basicCaps));

    /*
     * Because the sending routines deliver data to the transmit functions
     * in segments, these must be globally available to spool outgoing data
     * until a complete ECM block can be assembled.
     *
     * Besides the contents of ecmBlock, ecmStuffedBlock must be able to
     * hold all sync flag bytes, stuffed bits, and RCP frames.
     */
    u_int fs = 256;
    if (conf.class1ECMFrameSize == 64) fs = 64;
    ecmFrame = (u_char*) malloc(fs + 4);
    fxAssert(ecmFrame != NULL, "ECM procedure error (frame).");
    ecmBlock = (u_char*) malloc((fs + 4) * 256);
    fxAssert(ecmFrame != NULL, "ECM procedure error (block).");
    ecmStuffedBlock = (u_char*) malloc(fs == 256 ? 83000 : 33000);
    fxAssert(ecmFrame != NULL, "ECM procedure error (stuffed block).");
    gotCTRL = false;
}

Class1Modem::~Class1Modem()
{
    free(ecmFrame);
    free(ecmBlock);
    free(ecmStuffedBlock);
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
    atCmd(classCmd);

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
    if (!class1Query(conf.class1TMQueryCmd, xmitCaps)) {
	serverTrace("Error parsing \"+FTM\" query response: \"%s\"", rbuf);
	return (false);
    }
    modemParams.br = 0;
    u_int i;
    for (i = 1; i < NCAPS; i++)
	if (xmitCaps[i].ok)
	    modemParams.br |= BIT(xmitCaps[i].br);
    nonV34br = modemParams.br;
    if (conf.class1EnableV34Cmd != "" && conf.class1ECMSupport) {
	// This is cosmetic, mostly, to state the modem supports V.34.
	// We could query the modem but that would require another
	// config option, so we just trust the enable command.
	u_short pos = 0;
	primaryV34Rate = 0;
	const char* buf = conf.class1EnableV34Cmd;
	while (buf[0] != '=') buf++;		// move to assignment
	while (!isdigit(buf[0])) buf++;		// move to digits
	do {
	    primaryV34Rate = primaryV34Rate*10 + (buf[0] - '0');
	} while (isdigit((++buf)[0]));
	modemParams.br |= BIT(primaryV34Rate) - 1;
    }
    if (conf.class1ExtendedRes) {
	modemParams.vr = VR_ALL;
    } else {
	modemParams.vr = VR_NORMAL | VR_FINE;
    }
    modemParams.wd = BIT(WD_1728) | BIT(WD_2048) | BIT(WD_2432);
    modemParams.ln = LN_ALL;
    modemParams.df = BIT(DF_1DMH) | BIT(DF_2DMR);
    if (conf.class1ECMSupport) {
	modemParams.ec = BIT(EC_DISABLE) | BIT(EC_ENABLE64) | BIT(EC_ENABLE256);
 	modemParams.df |= BIT(DF_2DMMR);
    } else
	modemParams.ec = BIT(EC_DISABLE);
    modemParams.bf = BF_DISABLE;
    modemParams.st = ST_ALL;
    traceModemParams();
    /*
     * Receive capabilities are maintained separately from
     * transmit capabilities because we need to know more
     * than the signalling rate to formulate the DIS.
     */ 
    if (!class1Query(conf.class1RMQueryCmd, recvCaps)) {
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
    case BIT(V27FB)|BIT(V27)|BIT(V29)|BIT(V17):
    case BIT(V27FB)|BIT(V27)|BIT(V29)|BIT(V33)|BIT(V17):
	discap = DISSIGRATE_V17;
	break;
    }
    /*
     * T.30 specifies that HDLC frame data are in MSB bit
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
    if (conf.class1EnableV34Cmd != "" && conf.class1ECMSupport) {
	atCmd(conf.class1EnableV34Cmd);
	gotEOT = false;
    }
    useV34 = false;	// only when V.8 handshaking is used
    return (true);
}

/*
 * Send the modem the Class 1 configuration commands.
 */
bool
Class1Modem::setupClass1Parameters()
{
    if (modemServices & SERVICE_CLASS1) {
	atCmd(classCmd);
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
Class1Modem::faxService(bool enableV34)
{
    if (!atCmd(classCmd)) return (false);
    if (conf.class1EnableV34Cmd != "" && enableV34)
	atCmd(conf.class1EnableV34Cmd);
    useV34 = false;	// only when V.8 handshaking is used
    gotEOT = false;
    return (setupFlowControl(flowControl));
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
 * Encode an NSF string for transmission.
 */
void
Class1Modem::encodeNSF(fxStr& binary, const fxStr& ascii)
{
    u_int i, j;
    u_int n = ascii.length();
    binary.resize(n);
    for (i = 0, j = 0; i < n; i++) {
	char c = ascii[i];
	if (isprint(c) || c == ' ')
	    binary[j++] = frameRev[c];
    }
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
    encodeTSI(binary, ascii);
}

/*
 * Do the inverse of encodePWD; i.e. convert a binary
 * string of encoded digits into the equivalent ascii.
 */
const fxStr&
Class1Modem::decodePWD(fxStr& ascii, const HDLCFrame& binary)
{
    return decodeTSI(ascii, binary);
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
    if (useV34) return;			// nothing to do in V.34
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
 * Request a primary rate renegotiation.
 */
bool
Class1Modem::renegotiatePrimary(bool constrain)
{
    u_char buf[4];
    u_short size = 0;
    buf[size++] = DLE;
    if (constrain) {
	// don't neotiate a faster rate
	if (primaryV34Rate == 1 || primaryV34Rate == 2) buf[size++] = 0x70;	// 2400 bit/s
	else buf[size++] = primaryV34Rate + 0x6D;	// drop 4800 bit/s
	buf[size++] = DLE;
    }
    buf[size++] = 0x6C;					// <DLE><pph>
    if (!putModemData(buf, size)) return (false);
    if (constrain)
	protoTrace("Request primary rate renegotiation (limit %u bit/s).", (primaryV34Rate-1)*2400);
    else
	protoTrace("Request primary rate renegotiation.");
    return (true);
}

/*
 * Wait for a <DLE><ctrl> response per T.31-A1 B.8.4.
 */
bool
Class1Modem::waitForDCEChannel(bool awaitctrl)
{
    time_t start = Sys::now();
    int c;
    fxStr garbage;
    bool gotresponse = false;
    gotRTNC = false;
    do {
	c = getModemChar(60000);
	if (c == DLE) {
	    /*
	     * With V.34-faxing we expect <DLE><command>
	     * Refer to T.31-A1 Table B.1.  Except for EOT
	     * these are merely indicators and do not require
	     * action.
	     */
	    c = getModemChar(60000);
	    switch (c) {
		case EOT:
		    protoTrace("EOT received (end of transmission)");
		    gotEOT = true;
		    return (false);
		    break;
		case 0x69:
		    protoTrace("Control channel retrain");
		    // wait for the control channel to reappear
		    // should we reset the timeout setting?
		    waitForDCEChannel(true);
		    gotRTNC = true;
		    return (false);
		    break;
		case 0x6B:
		    protoTrace("Primary channel selected");
		    gotCTRL = false;
		    continue;
		    break;
		case 0x6D:
		    protoTrace("Control channel selected");
		    gotCTRL = true;
		    continue;
		    break;
		case 0x6E:			// 1200 bit/s
		case 0x6F:			// 2400 bit/s
		    // control rate indication
		    if (controlV34Rate != (c - 0x6D)) {
			controlV34Rate = (c - 0x6D);
			protoTrace("Control channel rate now %u bit/s", controlV34Rate*1200);
		    }
		    if (awaitctrl) gotresponse = true;
		    continue;
		    break;
		case 0x70:			//  2400 bit/s
		case 0x71:			//  4800 bit/s
		case 0x72:			//  7200 bit/s
		case 0x73:			//  9600 bit/s
		case 0x74:			// 12000 bit/s
		case 0x75:			// 14400 bit/s
		case 0x76:			// 16800 bit/s
		case 0x77:			// 19200 bit/s
		case 0x78:			// 21600 bit/s
		case 0x79:			// 24000 bit/s
		case 0x7A:			// 26400 bit/s
		case 0x7B:			// 28800 bit/s
		case 0x7C:			// 31200 bit/s
		case 0x7D:			// 33600 bit/s
		    // primary rate indication
		    if (primaryV34Rate != (c - 0x6F)) {
			primaryV34Rate = (c - 0x6F);
			protoTrace("Primary channel rate now %u bit/s", primaryV34Rate*2400);
		    }
		    if (!awaitctrl) gotresponse = true;
		    continue;
		    break;
		default:
		    // unexpected <DLE><command>, deem as garbage
		    garbage.append(DLE);
		    garbage.append(c);
		    break;
	    }
	} else garbage.append(c);
	fxStr rcpsignal;
	rcpsignal.append(0xFF);	rcpsignal.append(0x03);	rcpsignal.append(0x86);	rcpsignal.append(0x69);
	rcpsignal.append(0xCB);	rcpsignal.append(0x10);	rcpsignal.append(0x03);
	if (!gotCTRL && garbage == rcpsignal) {
	    // We anticipate getting "extra" RCP frames since we
	    // only look for one but usually we will see three.
	    garbage.cut(0, 7);
	}
    } while (!gotresponse && Sys::now()-start < 60);
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
    return (gotresponse);
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
	if (useV34 && c == DLE) {
	    c = getModemChar(0);
	    switch (c) {
		case EOT:
		    protoTrace("EOT received (end of transmission)");
		    gotEOT = true;
		    return (false);
		    break;
		case 0x69:
		    protoTrace("Control channel retrain");
		    // wait for the control channel to reappear
		    // should we reset the timeout setting?
		    waitForDCEChannel(true);
		    return (false);
		    break;
		default:
		    // unexpected <DLE><command>, deem as garbage
		    garbage.append(DLE);
		    break;
	    }
	}
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
		if (useV34) {
		    /*
		     * T.31-A1 Table B.1
		     * These indicate transparancy, shielding, or delimiting.
		     */
		    if (c == 0x07) {	// end of HDLC frame w/FCS error
			break;
		    }
		    switch (c) {
			case EOT:
			    protoTrace("EOT received (end of transmission)");
			    gotEOT = true;
			    return (false);
			    break;
			case DLE:	// <DLE><DLE> => <DLE>
			    break;
			case SUB:	// <DLE><SUB> => <DLE><DLE>
			    frame.put(frameRev[DLE]);
			    break;
			case 0x51:	// <DLE><0x51> => <DC1>
			    c = DC1;
			    break;
			case 0x53:	// <DLE><0x53> => <DC3>
			    c = 0x13;
			    break;
		    }
		}
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
    if (!useV34 && !waitFor(AT_OK)) {
	if (lastResponse == AT_ERROR)
	    protoTrace("FCS error");
	return (false);
    }
    if (useV34 && c == 0x07) {
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
    if (conf.class1ValidateV21Frames && !frame.checkCRC()) {
	protoTrace("FCS error (calculated)");
	return (false);
    }
    frame.setOK(true);
    return (true);
}

bool
Class1Modem::syncECMFrame()
{
    /*
     * We explicitly look for the first sync flag so as to
     * not get confused by any initial garbage or training.
     */
    int bit = 0;
    u_short ones = 0;

    // look for the first sync flag

    time_t start = Sys::now();
    startTimeout(900);		// triple T.4 A.3.1
    do {
	if ((unsigned) Sys::now()-start >= 3) {
	    protoTrace("Timeout awaiting synchronization sequence");
	    return (false);
	}
	bit = getModemBit(0);
    } while (bit != 0 && !didBlockEnd());
    do {
	if ((unsigned) Sys::now()-start >= 3) {
	    protoTrace("Timeout awaiting synchronization sequence");
	    return (false);
	}
	if (bit == 0 || ones > 0xFF) ones = 0;
	bit = getModemBit(0);
	if (bit == 1) ones++;
    } while (!(ones == 6 && bit == 0 && bit != EOF) && !didBlockEnd());
    stopTimeout("awaiting synchronization sequence");
    if (wasTimeout()) {
	return (false);
    }
    return (true);
}

/*
 * Receive an HDLC frame with ECM.  We must always be cautious
 * of "stuffed" zero-bits after five sequential one-bits between
 * flag sequences.  We assume this is called only after a
 * successfuly received complete flag sequence.
 *
 * Things are significantly more simple with V.34-fax ECM since
 * there is no synchronization or RCP frames.  In this case we
 * simply have to handle byte transparancy and shielding, looking
 * for byte-aligned frame delimiters.
 */
bool
Class1Modem::recvECMFrame(HDLCFrame& frame)
{
    if (useV34) {
	int c;
	for (;;) {
	    c = getModemChar(60000);
	    if (wasTimeout()) {
		return (false);
	    }
	    if (c == DLE) {
		c = getModemChar(60000);
		if (wasTimeout()) {
		    return (false);
		}
		switch (c) {
		    case DLE:
			break;
		    case SUB:
			frame.put(frameRev[DLE]);
			break;
		    case 0x51:
			c = 0x11;
			break;
		    case 0x53:
			c = 0x13;
			break;
		    case ETX:
			if (frame.getLength() > 0)
			    traceHDLCFrame("-->", frame);
			if (frame.getLength() < 5) {		// RCP frame size
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
			return (true);
		    case 0x07:
			protoTrace("FCS error");
			return (false);
		    case 0x04:
			protoTrace("EOT received (end of transmission)");
			gotEOT = true;
			return (false);
		    case 0x6D:
			protoTrace("Control channel selected");
			gotCTRL = true;
			return (false);
		    default:
			protoTrace("got <DLE><%X>", c);
			break;
		}
	    }
	    frame.put(frameRev[c]);
	}
    }

    int bit = getModemBit(0);
    u_short ones = 0;

    // look for the last sync flag (possibly the previous one)

    // some senders use this as the time to do framing so we must wait longer than T.4 A.3.1 implies
    startTimeout(60000);				// just to prevent hanging
    while (bit != 1 && bit != EOF && !didBlockEnd()) {	// flag begins with zero, address begins with one
	do {
	    if (bit == 0 || ones > 6) ones = 0;
	    bit = getModemBit(0);
	    if (bit == 1) ones++;
	} while (!(ones == 6 && bit == 0 && bit != EOF) && !didBlockEnd());
	ones = 0;
	bit = getModemBit(0);
    }
    stopTimeout("waiting for the last synchronization flag");

    // receive the frame, strip stuffed zero-bits, and look for end flag

    ones = 1;
    u_short bitpos = 7;
    u_int byte = (bit << bitpos);
    bool rcpframe = false;
    time_t start = Sys::now();
    do {
	if ((unsigned) Sys::now()-start >= 3) {
	    protoTrace("Timeout receiving HDLC frame");
	    return (false);
	}
	bit = getModemBit(0);
	if (bit == 1) {
	    ones++;
	}
	if (!(ones == 5 && bit == 0 && bit != EOF)) {	// not transparent stuffed zero-bits
	    bitpos--;
	    byte |= (bit << bitpos);
	    if (bitpos == 0) {			// fully populated byte
		frame.put(byte);
		bitpos = 8;
		byte = 0;
		/*
		 * Ensure that a corrupt frame doesn't overflow the frame buffer.
		 */
		if (frame.getLength() > ((frameSize+6)*4)) {	//  4 times valid size
		    protoTrace("HDLC frame length invalid.");
		    return (false);
		}
	    }
	}
	if (bit == 0) ones = 0;
	// don't wait for the terminating flag on an RCP frame
	if (frame[0] == 0xff && frame[1] == 0xc0 && frame[2] == 0x61 && frame.getLength() == 5 && frame.checkCRC()) {
	    protoTrace("RECV received RCP frame");
	    rcpframe = true;
	} else if (didBlockEnd()) {
	    // sometimes RCP frames are truncated by or are missing due to premature DLE+ETX characters, sadly
	    protoTrace("RECV assumed RCP frame with block end");
	    // force-feed a valid RCP frame with FCS
	    frame.reset();
	    frame.put(0xff); frame.put(0xc0); frame.put(0x61); frame.put(0x96); frame.put(0xd3);
	    rcpframe = true;
	}
    } while (ones != 6 && bit != EOF && !rcpframe);
    bit = getModemBit(0);			// trailing bit on flag
    if (!rcpframe) {
	if (frame.getLength() > 0)
	    traceHDLCFrame("-->", frame);
	if (bit) {				// should have been zero
	    protoTrace("Bad HDLC terminating flag received.");
	    return (false);
	}
	if (byte != 0x7e) {			// trailing byte should be flag
	    protoTrace("HDLC frame not byte-oriented.  Trailing byte: %#x", byte);
	    return (false);
	}
    }
    if (bit == EOF) {
	protoTrace("EOF received.");
	return (false);
    }
    if (frame.getLength() < 5) {		// RCP frame size
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
    return (true);
}

bool
Class1Modem::endECMBlock()
{
    if (didBlockEnd()) return (true);	// some erroniously re-use bytes

    int c = getLastByte();		// some erroniously re-use bits	
    startTimeout(2500);			// just to prevent hanging
    do {
	if (c == DLE) {
	    c = getModemChar(0);
	    if (c == ETX || c == EOF)
		break;
	}
    } while ((c = getModemChar(0)) != EOF);
    stopTimeout("waiting for DLE+ETX");
    if (c == EOF) return (false);
    else return (true);
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
    return (putModemDLEData(frame, frame.getLength(), frameRev, 60*1000) &&
	putModem(buf, 2, 60*1000) &&
	(useV34 ? true : waitFor(frame.moreFrames() ? AT_CONNECT : AT_OK, 0)));
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
 * Send a frame with TSI/CSI/PWD/SUB/SEP/PPR.
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

/*
 * Send a frame with NSF.
 */
bool
Class1Modem::sendFrame(u_char fcf, const u_char* code, const fxStr& nsf, bool lastFrame)
{
    HDLCFrame frame(conf.class1FrameOverhead);
    frame.put(0xff);
    frame.put(lastFrame ? 0xc8 : 0xc0);
    frame.put(fcf);
    frame.put(code, 3);		// should be in LSBMSB already
    frame.put((const u_char*)(const char*)nsf, nsf.length());
    return (sendRawFrame(frame));
}

bool
Class1Modem::transmitFrame(u_char fcf, bool lastFrame)
{
    startTimeout(2550);			// 3.0 - 15% = 2.55 secs
    bool frameSent =
	(useV34 ? true : atCmd(thCmd, AT_NOTHING)) &&
	(useV34 ? true : atResponse(rbuf, 0) == AT_CONNECT) &&
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
	(useV34 ? true : atCmd(thCmd, AT_NOTHING)) &&
	(useV34 ? true : atResponse(rbuf, 0) == AT_CONNECT) &&
	sendFrame(fcf, dcs, xinfo, lastFrame);
    stopTimeout("sending HDLC frame");
    return (frameSent);
}

bool
Class1Modem::transmitFrame(u_char fcf, const fxStr& tsi, bool lastFrame)
{
    startTimeout(3000);			// give more time than others
    bool frameSent =
	(useV34 ? true : atCmd(thCmd, AT_NOTHING)) &&
	(useV34 ? true : atResponse(rbuf, 0) == AT_CONNECT) &&
	sendFrame(fcf, tsi, lastFrame);
    stopTimeout("sending HDLC frame");
    return (frameSent);
}

bool
Class1Modem::transmitFrame(u_char fcf, const u_char* code, const fxStr& nsf, bool lastFrame)
{
    startTimeout(3000);			// give more time than others
    bool frameSent =
	(useV34 ? true : atCmd(thCmd, AT_NOTHING)) &&
	(useV34 ? true : atResponse(rbuf, 0) == AT_CONNECT) &&
	sendFrame(fcf, code, nsf, lastFrame);
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
    bool ok = atCmd(tmCmd, AT_CONNECT);
    if (ok) {

	// T.31 8.3.3 requires the DCE to report CONNECT at the beginning
	// of transmission of the training pattern rather than at the end.
	// We pause here to allow the remote's +FRM to result in CONNECT.
	// This delay will vary depending on the modem's adherence to T.31.
	pause(conf.class1TMConnectDelay);

	ok = (sendClass1Data(data, cc, bitrev, eod) &&
	    (eod ? waitFor(AT_OK) : true));
    }
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
    if (useV34) {
	return recvRawFrame(frame);
    }
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
    if (lastResponse == AT_OTHER && strneq(buf, "+F34:", 5)) {
	/*
	 * V.8 handshaking was successful.  The rest of the
	 * session is governed by T.31 Amendment 1 Annex B.
	 * (This should only happen after ATA or ATD.)
	 *
	 * The +F34: response is interpreted according to T.31-A1 B.6.2.
	 */
	buf += 5;					// skip "+F34:" prefix
	primaryV34Rate = 0;
	while (!isdigit(buf[0])) buf++;		// move to digits
	do {
	    primaryV34Rate = primaryV34Rate*10 + (buf[0] - '0');
        } while (isdigit((++buf)[0]));
	controlV34Rate = 0;
	while (!isdigit(buf[0])) buf++;		// move to digits
	do {
	    controlV34Rate = controlV34Rate*10 + (buf[0] - '0');
        } while (isdigit((++buf)[0]));
	useV34 = true;
	protoTrace("V.8 handshaking succeeded, V.34-Fax (SuperG3) capability enabled.");
	protoTrace("Primary channel rate: %u bit/s, Control channel rate: %u bit/s.", primaryV34Rate*2400, controlV34Rate*1200);
	modemParams.br |= BIT(primaryV34Rate) - 1;
    }
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
	case AT_OTHER:
	case AT_FCERROR:
	case AT_OK:
	    return (false);
	}
    }
}

/*
 * Send queryCmd and get a range response.
 */
bool
Class1Modem::class1Query(const fxStr& queryCmd, Class1Cap caps[])
{
    char response[1024];
    if (queryCmd[0] == '!') {
	return (parseQuery(queryCmd.tail(queryCmd.length()-1), caps));
    }
    if (atCmd(queryCmd, AT_NOTHING) && atResponse(response) == AT_OTHER) {
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
    u_int fs = conf.class1ECMFrameSize == 64 ? DIS_FRAMESIZE : 0;
    u_int v8 = conf.class1ECMSupport && conf.class1EnableV34Cmd != "" ? DIS_V8 : 0;
    return (FaxModem::modemDIS() &~ DIS_SIGRATE) | (discap<<10) | DIS_XTNDFIELD | fs | v8;
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
    bool bracket = false, first = true;
    
    while (cp[0]) {
	if (cp[0] == SPACE) {		// ignore white space
	    cp++;
	    continue;
	}

        /* by a.pogoda@web.de, jan 21st 2002
         * workaround for modems sending (<item>,<item>,...), i.e. 
         * enclosed in brackets rather than just <item>,<item>,...
         * e.g. elsa microlink 56k internet II and maybo others
         */
        if (cp[0]=='(' && first && !bracket) {
          /* check whether the first non-space char is an 
           * opening bracket and skip it if true
           */ 
          bracket = true;
          cp++;
          continue;
        }
        else if (cp[0]==')' && !first && bracket) {
          /* if an opening bracket was scanned before and 
           * the current char is a closing one, skip it
           */
          bracket = false;
          cp++;
          continue;
        }
        else if (!isdigit(cp[0]))
	  return (false);

        /* state that we already scanned past the first char */
        first = false;
	int v = 0;
	do {
	    v = v*10 + (cp[0] - '0');
	} while (isdigit((++cp)[0]));
	int r = v;
	if (cp[0] == '-') {				// <low>-<high>
	    cp++;
	    if (!isdigit(cp[0])) {
		return (false);
	    }
	    r = 0;
	    do {
		r = r*10 + (cp[0] - '0');
	    } while (isdigit((++cp)[0]));
	}
	for (u_int i = 0; i < NCAPS; i++)
	    if (caps[i].value >= v && caps[i].value <= r) {
		caps[i].ok = true;
		break;
	    }
	if (cp[0] == COMMA)		// <item>,<item>...
	    cp++;
    }
    return (true);
}

