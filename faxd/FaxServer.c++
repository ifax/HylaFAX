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
#include <unistd.h>
#include <sys/param.h>

#include "Sys.h"
#include "FaxServer.h"
#include "Class0.h"
#include "Class1Ersatz.h"
#include "Class10.h"
#include "Class2Ersatz.h"
#include "Class20.h"
#include "Class21.h"

#include "config.h"

#ifndef MAXHOSTNAMELEN
#define	MAXHOSTNAMELEN	64
#endif

fxIMPLEMENT_ObjArray(FaxRecvInfoArray, FaxRecvInfo)

/*
 * HylaFAX Fax Modem Server.
 */

FaxServer::FaxServer(const fxStr& devName, const fxStr& devID)
    : ModemServer(devName, devID)
{
    modem = NULL;
}

FaxServer::~FaxServer()
{
}

void
FaxServer::initialize(int argc, char** argv)
{
    ModemServer::initialize(argc, argv);
    hostname.resize(MAXHOSTNAMELEN);
    char buff[MAXHOSTNAMELEN];
    if (Sys::gethostname(buff, MAXHOSTNAMELEN) == 0) {
        hostname = buff;
        hostname.resize(strlen(hostname));
    }
}

time_t FaxServer::getConnectTime() const
    { return (connTime); }
time_t FaxServer::getFileTransferTime() const
    { return (Sys::now() - fileStart); }
time_t FaxServer::getPageTransferTime() const
    { return (pageTransferTime); }
time_t FaxServer::setPageTransferTime()
{
    pageTransferTime = Sys::now() - pageStart;
    pageStart = Sys::now();	// reset
    return (pageTransferTime);
}
const Class2Params& FaxServer::getClientParams() const
    { return (clientParams); }

void FaxServer::notifyCallPlaced(const FaxRequest&) {}
void FaxServer::notifyConnected(const FaxRequest&) {}

/*
 * Setup the modem; if needed.  Note that we reread
 * the configuration file if it's been changed prior
 * to setting up the modem.  This makes it easy to
 * swap modems that need different configurations
 * just by yanking the cable and then swapping the
 * config file before hooking up the new modem.
 */
bool
FaxServer::setupModem()
{
    modem = NULL;
    if (!ModemServer::setupModem())
	return (false);
    if (getModem()->isFaxModem()) {
	modem = (FaxModem*) ModemServer::getModem();
	modem->setLID(localIdentifier);
    }
    return (true);
}

void
FaxServer::discardModem(bool dropDTR)
{
    ModemServer::discardModem(dropDTR);
    modem = NULL;
}

/*
 * Deduce the type of modem supplied to the server
 * and return an instance of the appropriate modem
 * driver class.
 */
ClassModem*
FaxServer::deduceModem()
{
    fxStr h(type);
    h.raisecase();
    /*
     * Probe for modem using type, if specified; otherwise
     * try Class 2.1, Class 2.0, Class 2, Class 1.0, Class 1, and then Class 0 types.
     */
    u_int modemServices = 0;    // fax classes to try (bitmask)
    ClassModem* modem;
    if (h == "UNKNOWN"){
	modem = new Class0Modem(*this, *this);
	if (modem) {
	    if (modem->setupModem()){
                modemServices = modem->getModemServices();
                fxStr mfr = modem->getManufacturer();
                mfr.raisecase();
                if( mfr.find(0,"ROBOTICS") < mfr.length() ||
                    mfr.find(0,"3COM")     < mfr.length() ){
                    /*
                     * Don't use Class2.0 with USR modems if not explicitly
                     * specified in the config file (it doesn't work reliably) -- dbely
                     */
                    modem->serverTrace("USR/3COM modem: disable Class 2.0");
                    modemServices &= ~SERVICE_CLASS20;
                }
            }
	    delete modem;
	}
    }
    else if (h == "CLASS2.1")
        modemServices |= SERVICE_CLASS21;
    else if (h == "CLASS2.0")
        modemServices |= SERVICE_CLASS20;
    else if (h == "CLASS2")
        modemServices |= SERVICE_CLASS2;
    else if (h == "CLASS1.0")
        modemServices |= SERVICE_CLASS10;
    else if (h == "CLASS1")
        modemServices |= SERVICE_CLASS1;

    if (modemServices & SERVICE_CLASS21) {
	modem = new Class21Modem(*this, *this);
	if (modem) {
	    if (modem->setupModem())
		return modem;
	    delete modem;
	}
    }
    if (modemServices & SERVICE_CLASS20) {
	modem = new Class20Modem(*this, *this);
	if (modem) {
	    if (modem->setupModem())
		return modem;
	    delete modem;
	}
    }
    if (modemServices & SERVICE_CLASS2) {
	modem = new Class2ErsatzModem(*this, *this);
	if (modem) {
	    if (modem->setupModem())
		return modem;
	    delete modem;
	}
    }
    if (modemServices & SERVICE_CLASS10) {
	modem = new Class10Modem(*this, *this);
	if (modem) {
	    if (modem->setupModem())
		return modem;
	    delete modem;
	}
    }
    if (modemServices & SERVICE_CLASS1) {
	modem = new Class1ErsatzModem(*this, *this);
	if (modem) {
	    if (modem->setupModem())
		return modem;
	    delete modem;
	}
    }
    return (NULL);
}

/*
 * Modem support interfaces.  Note that the values
 * returned when we don't have a handle on the modem
 * are selected so that any imaged facsimile should
 * still be sendable.
 */
bool FaxServer::modemSupports2D() const
    { return modem ? modem->supports2D() : false; }
bool FaxServer::modemSupportsMMR() const
    { return modem ? modem->supportsMMR() : false; }
bool FaxServer::modemSupportsEOLPadding() const
    { return modem ? modem->supportsEOLPadding() : false; }
bool FaxServer::modemSupportsVRes(float res) const
    { return modem ? modem->supportsVRes(res) : true; }
bool FaxServer::modemSupportsPageLength(u_int l) const
    { return modem ? modem->supportsPageLength(l) : true; }

bool FaxServer::modemSupportsPolling() const
    { return modem ? modem->supportsPolling() : false; }

fxStr
FaxServer::getModemCapabilities() const
{
    return fxStr::format("%c%08x"
	, modem->supportsPolling() ? 'P' : 'p'
	, modem->getCapabilities()
    );
}

/*
 * Server configuration support.
 */

/*
 * Read a configuration file.  Note that we suppress
 * dial string rules setup while reading so that the
 * order of related parameters is not important.  We
 * also setup the local identifier from the fax number
 * if nothing is specified in the config file (for
 * backwards compatibility).
 */
void
FaxServer::readConfig(const fxStr& filename)
{
    ModemServer::readConfig(filename);
    if (localIdentifier == "")
	setLocalIdentifier(canonicalizePhoneNumber(FAXNumber));
}

/*
 * Set local identifier and if modem is setup
 * pass in so that it can be installed in the
 * modem (e.g. for Class 2-style modems).
 */
void
FaxServer::setLocalIdentifier(const fxStr& lid)
{
    ServerConfig::setLocalIdentifier(lid);
    if (modem)
	modem->setLID(lid);
}
const fxStr& FaxServer::getLocalIdentifier() const { return (localIdentifier); }
