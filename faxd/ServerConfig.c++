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
#include "ServerConfig.h"
#include "FaxTrace.h"
#include "DialRules.h"
#include "UUCPLock.h"
#include "REArray.h"
#include "BoolArray.h"
#include "faxApp.h"

#include <ctype.h>
#include "Sys.h"

#include "config.h"

ServerConfig::ServerConfig()
{
    lastTSIModTime = 0;
    tsiPats = NULL;
    acceptTSI = NULL;
    dialRules = NULL;
    setupConfig();
}

ServerConfig::~ServerConfig()
{
    delete dialRules;
    delete acceptTSI;
    delete tsiPats;
}

void
ServerConfig::configError(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vconfigError(fmt, ap);
    va_end(ap);
}

void
ServerConfig::configTrace(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vconfigTrace(fmt, ap);
    va_end(ap);
}

#define	N(a)	(sizeof (a) / sizeof (a[0]))

ServerConfig::S_stringtag ServerConfig::strings[] = {
{ "logfacility",	&ServerConfig::logFacility,	LOG_FAX },
{ "faxnumber",		&ServerConfig::FAXNumber },
{ "areacode",		&ServerConfig::areaCode	},
{ "countrycode",	&ServerConfig::countryCode },
{ "longdistanceprefix",	&ServerConfig::longDistancePrefix },
{ "internationalprefix",&ServerConfig::internationalPrefix },
{ "qualifytsi",		&ServerConfig::qualifyTSI },
{ "uucplockdir",	&ServerConfig::uucpLockDir,	UUCP_LOCKDIR },
{ "uucplocktype",	&ServerConfig::uucpLockType,	UUCP_LOCKTYPE },
};
ServerConfig::S_numbertag ServerConfig::numbers[] = {
{ "tracingmask",	&ServerConfig::tracingMask,	// NB: must be first
   FAXTRACE_MODEMIO|FAXTRACE_TIMEOUTS },
{ "sessiontracing",	&ServerConfig::logTracingLevel,	FAXTRACE_SERVER },
{ "servertracing",	&ServerConfig::tracingLevel,	FAXTRACE_SERVER },
{ "uucplocktimeout",	&ServerConfig::uucpLockTimeout,	0 },
{ "nocarrierretrys",	&ServerConfig::noCarrierRetrys,	1 },
{ "jobreqproto",	&ServerConfig::requeueProto,	FAX_REQPROTO },
{ "jobreqother",	&ServerConfig::requeueOther,	FAX_REQUEUE },
{ "pollmodemwait",	&ServerConfig::pollModemWait,	30 },
{ "polllockwait",	&ServerConfig::pollLockWait,	30 },
{ "maxrecvpages",	&ServerConfig::maxRecvPages,	(u_int) -1 },
{ "maxbadcalls",	&ServerConfig::maxConsecutiveBadCalls, 25 },
{ "maxsetupattempts",	&ServerConfig::maxSetupAttempts, 2 },
};
ServerConfig::S_filemodetag ServerConfig::filemodes[] = {
{ "recvfilemode",	&ServerConfig::recvFileMode,	0600 },
{ "devicemode",		&ServerConfig::deviceMode,	0600 },
{ "logfilemode",	&ServerConfig::logMode,		0600 },
{ "uucplockmode",	&ServerConfig::uucpLockMode,	UUCP_LOCKMODE },
};

void
ServerConfig::setupConfig()
{
    int i;

    for (i = N(strings)-1; i >= 0; i--)
	(*this).*strings[i].p = (strings[i].def ? strings[i].def : "");
    for (i = N(filemodes)-1; i >= 0; i--)
	(*this).*filemodes[i].p = (mode_t) filemodes[i].def;
    for (i = N(numbers)-1; i >= 0; i--)
	(*this).*numbers[i].p = numbers[i].def;

    speakerVolume = ClassModem::QUIET;	// default speaker volume
#ifdef sgi
    clocalAsRoot = true;		// under IRIX must be root to set CLOCAL
#else
    clocalAsRoot = false;		// everywhere else anyone can do it
#endif

#if HAS_SCHEDCTL || HAS_PRIOCNTL || HAS_RTPRIO
    priorityScheduling = true;		// maintain historic behavior
#else
    priorityScheduling = false;		// for new mechanisms anyone can do it
#endif

    requeueTTS[ClassModem::OK]		= 0;
    requeueTTS[ClassModem::BUSY]	= FAX_REQBUSY;
    requeueTTS[ClassModem::NOCARRIER]	= FAX_REQUEUE;
    requeueTTS[ClassModem::NOANSWER]	= FAX_REQUEUE;
    requeueTTS[ClassModem::NODIALTONE]	= FAX_REQUEUE;
    requeueTTS[ClassModem::ERROR]	= FAX_REQUEUE;
    requeueTTS[ClassModem::FAILURE]	= FAX_REQUEUE;
    requeueTTS[ClassModem::NOFCON]	= FAX_REQUEUE;
    requeueTTS[ClassModem::DATACONN]	= FAX_REQUEUE;

    localIdentifier = "";
    delete dialRules, dialRules = NULL;
}

void
ServerConfig::resetConfig()
{
    ModemConfig::resetConfig();
    setupConfig();
}

#define	valeq(a,b)	(strcasecmp(a,b)==0)

SpeakerVolume
ServerConfig::getVolume(const char* cp)
{
    if (valeq(cp, "off"))
	return ClassModem::OFF;
    else if (valeq(cp, "quiet"))
	return ClassModem::QUIET;
    else if (valeq(cp, "low"))
	return ClassModem::LOW;
    else if (valeq(cp, "medium"))
	return ClassModem::MEDIUM;
    else if (valeq(cp, "high"))
	return ClassModem::HIGH;
    else {
	configError("Unknown speaker volume \"%s\"; using \"quiet\"", cp);
	return ClassModem::QUIET;
    }
}

void
ServerConfig::setLocalIdentifier(const fxStr& lid)
{
    localIdentifier = lid;
}

void
ServerConfig::setModemSpeakerVolume(SpeakerVolume level)
{
    speakerVolume = level;
}

/*
 * Subclass DialStringRules so that we can redirect the
 * diagnostic and tracing interfaces through the server.
 */
class ServerConfigDialStringRules : public DialStringRules {
private:
    ServerConfig& config;	// XXX should be const, but requires other mods

    virtual void parseError(const char* fmt ...);
    virtual void traceParse(const char* fmt ...);
    virtual void traceRules(const char* fmt ...);
public:
    ServerConfigDialStringRules(ServerConfig& config, const char* filename);
    ~ServerConfigDialStringRules();
};
ServerConfigDialStringRules::ServerConfigDialStringRules(ServerConfig& c, const char* f)
    : DialStringRules(f), config(c)
{}
ServerConfigDialStringRules::~ServerConfigDialStringRules() {}

void
ServerConfigDialStringRules::parseError(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    config.vconfigError(fmt, ap);
    va_end(ap);
}
void
ServerConfigDialStringRules::traceParse(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    config.vdialrulesTrace(fmt, ap);
    va_end(ap);
}
void
ServerConfigDialStringRules::traceRules(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    config.vdialrulesTrace(fmt, ap);
    va_end(ap);
}

void
ServerConfig::setDialRules(const char* name)
{
    delete dialRules;
    dialRules = new ServerConfigDialStringRules(*this, name);
    dialRules->setVerbose(
	((tracingLevel|logTracingLevel) & FAXTRACE_DIALRULES) != 0);
    /*
     * Setup configuration environment.
     */
    dialRules->def("AreaCode", areaCode);
    dialRules->def("CountryCode", countryCode);
    dialRules->def("LongDistancePrefix", longDistancePrefix);
    dialRules->def("InternationalPrefix", internationalPrefix);
    if (!dialRules->parse()) {
	configError("Parse error in dial string rules \"%s\"", name);
	delete dialRules, dialRules = NULL;
    }
}

/*
 * Convert a dialing string to a canonical format.
 */
fxStr
ServerConfig::canonicalizePhoneNumber(const fxStr& ds)
{
    if (dialRules)
	return dialRules->canonicalNumber(ds);
    else
	return ds;
}

/*
 * Prepare a dialing string for use.
 */
fxStr
ServerConfig::prepareDialString(const fxStr& ds)
{
    if (dialRules)
	return dialRules->dialString(ds);
    else
	return ds;
}

/*
 * Create an appropriate UUCP lock instance.
 */
UUCPLock*
ServerConfig::getUUCPLock(const fxStr& deviceName)
{
    return UUCPLock::newLock(uucpLockType,
	uucpLockDir, deviceName, uucpLockMode);
}

bool
ServerConfig::checkACL(const fxStr& id, REArray* pats, fxBoolArray& accept)
{
    if (pats != NULL) {
	for (u_int i = 0; i < pats->length(); i++)
	    if ((*pats)[i]->Find(id))
		return (accept[i]);
    }
    return (false);			// NB: reject if no patterns!
}

bool
ServerConfig::isTSIOk(const fxStr& tsi)
{
    updatePatterns(qualifyTSI, tsiPats, acceptTSI, lastTSIModTime);
    return (qualifyTSI == "" ? true : checkACL(tsi, tsiPats, *acceptTSI));
}

/*
 * Update pattern arrays if a file patterns has
 * been changed since the last time we read it.
 */
void
ServerConfig::updatePatterns(const fxStr& file,
    REArray*& pats, fxBoolArray*& accept,
    time_t& lastModTime)
{
    struct stat sb;
    if (file != "" && Sys::stat(file, sb) >= 0 && sb.st_mtime >= lastModTime) {
	FILE* fp = Sys::fopen(file, "r");
	if (fp != NULL) {
	    readPatterns(fp, pats, accept);
	    lastModTime = sb.st_mtime;
	    fclose(fp);
	}
    } else if (pats) {
	// file's been removed, delete any existing info
	delete pats, pats = NULL;
	delete accept, accept = NULL;
    }
}

/*
 * Read the file of patterns into an array.
 *
 * The order of patterns is preserved and a leading ``!''
 * is interpreted to mean ``reject if matched by this
 * regex''.  Note also that we always allocate an array
 * of patterns.  The pattern matching logic rejects
 * things that don't match any patterns.  Thus an empty file
 * causes all incoming facsimile to be rejected.
 */
void
ServerConfig::readPatterns(FILE* fp, REArray*& pats, fxBoolArray*& accept)
{
    if (pats)
	pats->resize(0);
    else
	pats = new REArray;
    if (accept)
	accept->resize(0);
    else
	accept = new fxBoolArray;

    char line[256];
    while (fgets(line, sizeof (line)-1, fp)) {
	char* cp = strchr(line, '#');
	if (cp || (cp = strchr(line, '\n')))
	    *cp = '\0';
	/* trim off trailing white space */
	for (cp = strchr(line, '\0'); cp > line; cp--)
	    if (!isspace(cp[-1]))
		break;
	*cp = '\0';
	if (line[0] == '\0')
	    continue;
	RE* re;
	if (line[0] == '!') {
	    accept->append(false);
	    pats->append(re = new RE(line+1));
	} else {
	    accept->append(true);
	    pats->append(re = new RE(line));
	}
	if (re->getErrorCode() > REG_NOMATCH) {
	    fxStr emsg;
	    re->getError(emsg);
	    configError("Bad TSI/CID pattern: %s: " | emsg, re->pattern());
	}
    }
}

static void
tiffErrorHandler(const char* module, const char* fmt0, va_list ap)
{
    fxStr fmt = (module != NULL) ?
        fxStr::format("%s: Warnings, %s.", module, fmt0)
        : fxStr::format("Warnings, %s.", fmt0);
    vlogError(fmt, ap);
}

static void
tiffWarningHandler(const char* module, const char* fmt0, va_list ap)
{
    fxStr fmt = (module != NULL) ?
        fxStr::format("%s: Warnings, %s.", module, fmt0)
        : fxStr::format("Warnings, %s.", fmt0);
    vlogWarning(fmt, ap);
}

bool
ServerConfig::setConfigItem(const char* tag, const char* value)
{
    u_int ix;
    if (findTag(tag, (const tags*)strings, N(strings), ix)) {
	(*this).*strings[ix].p = value;
	switch (ix) {
	case 0:	faxApp::setLogFacility(logFacility); break;
	}
    } else if (findTag(tag, (const tags*)numbers, N(numbers), ix)) {
	(*this).*numbers[ix].p = getNumber(value);
	switch (ix) {
	case 1: tracingLevel &= ~tracingMask;
	case 2: logTracingLevel &= ~tracingMask;
	    if (dialRules)
		dialRules->setVerbose(
		    (tracingLevel|logTracingLevel) & FAXTRACE_DIALRULES);
	    if ((tracingLevel|logTracingLevel) & FAXTRACE_TIFF) {
		TIFFSetErrorHandler(tiffErrorHandler);
		TIFFSetWarningHandler(tiffWarningHandler);
	    } else {
		TIFFSetErrorHandler(NULL);
		TIFFSetWarningHandler(NULL);
	    }
	    break;
	case 3: UUCPLock::setLockTimeout(uucpLockTimeout); break;
	}
    } else if (findTag(tag, (const tags*)filemodes, N(filemodes), ix))
	(*this).*filemodes[ix].p = strtol(value, 0, 8);

    else if (streq(tag, "speakervolume"))
	setModemSpeakerVolume(getVolume(value));
    else if (streq(tag, "localidentifier"))
	setLocalIdentifier(value);
    else if (streq(tag, "dialstringrules"))
	setDialRules(value);
    else if (streq(tag, "clocalasroot"))
	clocalAsRoot = getBoolean(value);
    else if (streq(tag, "priorityscheduling"))
	priorityScheduling = getBoolean(value);
    else if (streq(tag, "jobreqbusy"))
	requeueTTS[ClassModem::BUSY] = getNumber(value);
    else if (streq(tag, "jobreqnocarrier"))
	requeueTTS[ClassModem::NOCARRIER] = getNumber(value);
    else if (streq(tag, "jobreqnoanswer"))
	requeueTTS[ClassModem::NOANSWER] = getNumber(value);
    else if (streq(tag, "jobreqnofcon"))
	requeueTTS[ClassModem::NOFCON] = getNumber(value);
    else if (streq(tag, "jobreqdataconn"))
	requeueTTS[ClassModem::DATACONN] = getNumber(value);
    else
	return ModemConfig::setConfigItem(tag, value);
    return (true);
}
#undef N
