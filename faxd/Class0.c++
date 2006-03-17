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

/*
 * Class 0 (data) Modem Driver Interface.
 */
#include "Class0.h"
#include "ModemServer.h"
#include "class2.h"			// XXX for SERVICE_DATA

Class0Modem::Class0Modem(ModemServer& s, const ModemConfig& c)
    : ClassModem(s, c)
{
}

Class0Modem::~Class0Modem()
{
}

bool
Class0Modem::setupModem(bool isSetup)
{
    if (!selectBaudRate(conf.maxRate, conf.flowControl, conf.flowControl))
	return (false);
    // Query service support information
    fxStr s;
    if (doQuery(conf.classQueryCmd, s, 500) && parseRange(s, modemServices))
	traceBits(modemServices & SERVICE_ALL, serviceNames);
    if ((modemServices & SERVICE_DATA) == 0)
	return (false);
    atCmd(conf.class0Cmd);
    setupFlowControl(flowControl);

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

    return (true);
}

/*
 * Send the modem any commands needed to force use of
 * the specified flow control scheme.
 */
bool
Class0Modem::setupFlowControl(FlowControl fc)
{
    switch (fc) {
    case FLOW_NONE:	return atCmd(conf.noFlowCmd);
    case FLOW_XONXOFF:	return atCmd(conf.softFlowCmd);
    case FLOW_RTSCTS:	return atCmd(conf.hardFlowCmd);
    }
    return (true);
}

CallStatus
Class0Modem::dial(const char* number, fxStr& emsg)
{
    return (ClassModem::dial(number, emsg));
}

/*
 * Wait-for and process a dial command response.
 */
CallStatus
Class0Modem::dialResponse(fxStr&)
{
    ATResponse r;
    do {
	r = atResponse(rbuf, conf.dialResponseTimeout);
	switch (r) {
	case AT_ERROR:	    return (ERROR);	// error in dial command
	case AT_BUSY:	    return (BUSY);	// busy signal
	case AT_NOCARRIER:  return (NOCARRIER);	// no carrier detected
	case AT_OK:	    return (NOCARRIER);	// (for AT&T DataPort)
	case AT_NODIALTONE: return (NODIALTONE);// local phone connection hosed
	case AT_NOANSWER:   return (NOANSWER);	// no answer or ring back
	case AT_TIMEOUT:    return (FAILURE);	// timed out w/o response
	case AT_CONNECT:    return (OK);	// data connection
	}
    } while (r == AT_OTHER && isNoise(rbuf));
    return (FAILURE);
}

bool Class0Modem::isFaxModem() const		{ return false; }
