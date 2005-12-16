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
#include <errno.h>

#include "FaxMachineInfo.h"
#include "StackBuffer.h"
#include "class2.h"
#include "config.h"

const fxStr FaxMachineInfo::infoDir(FAX_INFODIR);

FaxMachineInfo::FaxMachineInfo()
{
    changed = false;
    resetConfig();
}
FaxMachineInfo::FaxMachineInfo(const FaxMachineInfo& other)
    : FaxConfig(other)
    , file(other.file)
    , csi(other.csi)
    , nsf(other.nsf)
    , dis(other.dis)
    , lastSendFailure(other.lastSendFailure)
    , lastDialFailure(other.lastDialFailure)
    , pagerPassword(other.pagerPassword)
    , pagerTTYParity(other.pagerTTYParity)
    , pagingProtocol(other.pagingProtocol)
    , pageSource(other.pageSource)
    , pagerSetupCmds(other.pagerSetupCmds)
{
    locked = other.locked;

    supportsVRes = other.supportsVRes;
    supports2DEncoding = other.supports2DEncoding;
    supportsMMR = other.supportsMMR;
    hasV34Trouble = other.hasV34Trouble;
    hasV17Trouble = other.hasV17Trouble;
    supportsPostScript = other.supportsPostScript;
    supportsBatching = other.supportsBatching;
    calledBefore = other.calledBefore;
    maxPageWidth = other.maxPageWidth;
    maxPageLength = other.maxPageLength;
    maxSignallingRate = other.maxSignallingRate;
    minScanlineTime = other.minScanlineTime;
    sendFailures = other.sendFailures;
    dialFailures = other.dialFailures;

    pagerMaxMsgLength = other.pagerMaxMsgLength;

    changed = other.changed;
}
FaxMachineInfo::~FaxMachineInfo() { writeConfig(); }

u_short
FaxMachineInfo::getMaxPageWidthInMM() const
{
    return (u_short)(maxPageWidth/(204.0f/25.4f));
}

#include <ctype.h>

bool
FaxMachineInfo::updateConfig(const fxStr& number)
{
    fxStr canon(number);
    u_int i = 0;
    while (i < canon.length()) {
	if (!isdigit(canon[i]))
	    canon.remove(i);
	else
	    i++;
    }
    if (file == "")
	file = infoDir | "/" | canon;
    return FaxConfig::updateConfig(file);
}

void
FaxMachineInfo::resetConfig()
{
    supportsVRes = VR_FINE;		// normal and high res support
    supports2DEncoding = true;		// assume 2D-encoding support
    supportsMMR = true;			// assume MMR support
    hasV34Trouble = false;		// assume no problems
    hasV17Trouble = false;		// assume no problems
    supportsPostScript = false;		// no support for Adobe protocol
    supportsBatching = true;		// assume batching (EOM) support
    calledBefore = false;		// never called before
    maxPageWidth = 2432;		// max required width
    maxPageLength = (u_short) -1;	// infinite page length
    maxSignallingRate = BR_14400;	// T.17 14.4KB
    minScanlineTime = ST_0MS;		// 0ms/0ms
    sendFailures = 0;
    dialFailures = 0;

    pagerMaxMsgLength = (u_int) -1;	// unlimited length
    pagerPassword = "";			// no password string
    pagerTTYParity = "";		// unspecified
    pagingProtocol = "ixo";		// ixo/tap or ucp
    pageSource = "";			// source unknown
    pagerSetupCmds = "";		// use values from config file

    locked = 0;
}

extern void vlogError(const char* fmt, va_list ap);

void
FaxMachineInfo::error(const char* fmt0 ...)
{
    va_list ap;
    va_start(ap, fmt0);
    vlogError(file | ": " | fmt0, ap);
    va_end(ap);
}

/*
 * Report an error encountered while parsing the info file.
 */
void
FaxMachineInfo::vconfigError(const char* fmt0, va_list ap)
{
    vlogError(file |
	fxStr::format(": line %u: %s", getConfigLineNumber(), fmt0), ap);
}
void
FaxMachineInfo::configError(const char* fmt0 ...)
{
    va_list ap;
    va_start(ap, fmt0);
    vconfigError(fmt0, ap);
    va_end(ap);
}
void FaxMachineInfo::configTrace(const char* ...) {}

#define	N(a)		(sizeof (a) / sizeof (a[0]))

static const char* brnames[] =
   { "2400", "4800", "7200", "9600", "12000", "14400",
     "16800", "19200", "21600", "24000", "26400", "28800", "31200", "33600" };
#define	NBR	N(brnames)
static const char* stnames[] =
   { "0ms", "5ms", "10ms/5ms", "10ms",
     "20ms/10ms", "20ms", "40ms/20ms", "40ms" };
#define	NST	N(stnames)

#define VR	0
#define	G32D	1
#define	G4	2
#define	PS	3
#define	WD	4
#define	LN	5
#define	BR	6
#define	ST	7
#define V34	8
#define V17	9
#define BATCH	10
#define PAGING	11

#define	setLocked(b,ix)	locked |= b<<ix

bool
FaxMachineInfo::setConfigItem(const char* tag, const char* value)
{
    int b = (tag[0] == '&' ? 1 : 0);	// locked down indicator
    if (b) tag++;
    if (streq(tag, "supportshighres")) {	// obsolete tag
	supportsVRes = VR_FINE;
	setLocked(b, VR);
    } else if (streq(tag, "supportsvres")) {
	supportsVRes = getNumber(value);
	setLocked(b, VR);
    } else if (streq(tag, "supports2dencoding")) {
	supports2DEncoding = getBoolean(value);
	setLocked(b, G32D);
    } else if (streq(tag, "supportsmmr")) {
	supportsMMR = getBoolean(value);
	setLocked(b, G4);
    } else if (streq(tag, "hasv34trouble")) {
	hasV34Trouble = getBoolean(value);
	setLocked(b, V34);
    } else if (streq(tag, "hasv17trouble")) {
	hasV17Trouble = getBoolean(value);
	setLocked(b, V17);
    } else if (streq(tag, "supportspostscript")) {
	supportsPostScript = getBoolean(value);
	setLocked(b, PS);
    } else if (streq(tag, "supportsbatching")) {
	supportsBatching = getBoolean(value);
	setLocked(b, BATCH);
    } else if (streq(tag, "calledbefore")) {
	calledBefore = getBoolean(value);
    } else if (streq(tag, "maxpagewidth")) {
	maxPageWidth = getNumber(value);
	setLocked(b, WD);
    } else if (streq(tag, "maxpagelength")) {
	maxPageLength = getNumber(value);
	setLocked(b, LN);
    } else if (streq(tag, "sendfailures")) {
	sendFailures = getNumber(value);
    } else if (streq(tag, "dialfailures")) {
	dialFailures = getNumber(value);
    } else if (streq(tag, "remotecsi")) {
	csi = value;
    } else if (streq(tag, "remotensf")) {
	nsf = value;
    } else if (streq(tag, "remotedis")) {
	dis = value;
    } else if (streq(tag, "lastsendfailure")) {
	lastSendFailure = value;
    } else if (streq(tag, "lastdialfailure")) {
	lastDialFailure = value;
    } else if (streq(tag, "maxsignallingrate")) {
	u_int ix;
	if (findValue(value, brnames, N(brnames), ix)) {
	    maxSignallingRate = ix;
	    setLocked(b, BR);
	}
    } else if (streq(tag, "minscanlinetime")) {
	u_int ix;
	if (findValue(value, stnames, N(stnames), ix)) {
	    minScanlineTime = ix;
	    setLocked(b, ST);
	}
    } else if (streq(tag, "pagermaxmsglength")) {
	pagerMaxMsgLength = getNumber(value);
    } else if (streq(tag, "pagerpassword")) {
	pagerPassword = value;
    } else if (streq(tag, "pagerttyparity")) {
	pagerTTYParity = value;
    } else if (streq(tag, "pagingprotocol")) {
	pagingProtocol = value;
	setLocked(b, PAGING);
    } else if (streq(tag, "pagesource")) {
	pageSource = value;
    } else if (streq(tag, "pagersetupcmds")) {
	pagerSetupCmds = value;
    } else
	return (false);
    return (true);
}

#define	isLocked(b)	(locked & (1<<b))

#define	checkLock(ix, member, value)	\
    if (!isLocked(ix)) {		\
	member = value;			\
	changed = true;			\
    }

void FaxMachineInfo::setSupportsVRes(int v)
    { checkLock(VR, supportsVRes, v); }
void FaxMachineInfo::setSupports2DEncoding(bool b)
    { checkLock(G32D, supports2DEncoding, b); }
void FaxMachineInfo::setSupportsMMR(bool b)
    { checkLock(G4, supportsMMR, b); }
void FaxMachineInfo::setHasV34Trouble(bool b)
    { checkLock(V34, hasV34Trouble, b); }
void FaxMachineInfo::setHasV17Trouble(bool b)
    { checkLock(V17, hasV17Trouble, b); }
void FaxMachineInfo::setSupportsPostScript(bool b)
    { checkLock(PS, supportsPostScript, b); }
void FaxMachineInfo::setSupportsBatching(bool b)
    { checkLock(BATCH, supportsBatching, b); }
void FaxMachineInfo::setMaxPageWidthInPixels(int v)
    { checkLock(WD, maxPageWidth, v); }
void FaxMachineInfo::setMaxPageLengthInMM(int v)
    { checkLock(LN, maxPageLength, v); }
void FaxMachineInfo::setMaxSignallingRate(int v)
    { checkLock(BR, maxSignallingRate, v); }
void FaxMachineInfo::setMinScanlineTime(int v)
    { checkLock(ST, minScanlineTime, v); }

void
FaxMachineInfo::setCalledBefore(bool b)
{
    calledBefore = b;
    changed = true;
}

#define	checkChanged(member, value)	\
    if (member != value) {		\
	member = value;			\
	changed = true;			\
    }

void FaxMachineInfo::setCSI(const fxStr& v)
    { checkChanged(csi, v); }
void FaxMachineInfo::setNSF(const fxStr& v)
    { checkChanged(nsf, v); }
void FaxMachineInfo::setDIS(const fxStr& v)
    { checkChanged(dis, v); }
void FaxMachineInfo::setLastSendFailure(const fxStr& v)
    { checkChanged(lastSendFailure, v); }
void FaxMachineInfo::setLastDialFailure(const fxStr& v)
    { checkChanged(lastDialFailure, v); }
void FaxMachineInfo::setSendFailures(int v)
    { checkChanged(sendFailures, v); }
void FaxMachineInfo::setDialFailures(int v)
    { checkChanged(dialFailures, v); }

/*
 * Rewrite the file if the contents have changed.
 */
void
FaxMachineInfo::writeConfig()
{
    if (changed) {
	mode_t omask = umask(022);
	int fd = Sys::open(file, O_WRONLY|O_CREAT, 0644);
	(void) umask(omask);
	if (fd >= 0) {
	    fxStackBuffer buf;
	    writeConfig(buf);
	    u_int cc = buf.getLength();
	    if (Sys::write(fd, buf, cc) != (ssize_t)cc) {
		error("write error: %s", strerror(errno));
		Sys::close(fd);
		return;
	    }
	    ftruncate(fd, cc);
	    Sys::close(fd);
	} else
	    error("open: %m");
	changed = false;
    }
}

static void
putBoolean(fxStackBuffer& buf, const char* tag, bool locked, bool b)
{
    buf.fput("%s%s:%s\n", locked ? "&" : "", tag, b ? "yes" : "no");
}

static void
putDecimal(fxStackBuffer& buf, const char* tag, bool locked, int v)
{
    buf.fput("%s%s:%d\n", locked ? "&" : "", tag, v);
}

static void
putString(fxStackBuffer& buf, const char* tag, bool locked, const char* v)
{
    buf.fput("%s%s:\"%s\"\n", locked ? "&" : "", tag, v);
}

static void
putIfString(fxStackBuffer& buf, const char* tag, bool locked, const char* v)
{
    if (*v != '\0')
	buf.fput("%s%s:\"%s\"\n", locked ? "&" : "", tag, v);
}

void
FaxMachineInfo::writeConfig(fxStackBuffer& buf)
{
    putDecimal(buf, "supportsVRes", isLocked(VR), supportsVRes);
    putBoolean(buf, "supports2DEncoding", isLocked(G32D),supports2DEncoding);
    putBoolean(buf, "supportsMMR", isLocked(G4),supportsMMR);
    putBoolean(buf, "hasV34Trouble", isLocked(V34),hasV34Trouble);
    putBoolean(buf, "hasV17Trouble", isLocked(V17),hasV17Trouble);
    putBoolean(buf, "supportsPostScript", isLocked(PS), supportsPostScript);
    putBoolean(buf, "supportsBatching", isLocked(BATCH), supportsBatching);
    putBoolean(buf, "calledBefore", false, calledBefore);
    putDecimal(buf, "maxPageWidth", isLocked(WD), maxPageWidth);
    putDecimal(buf, "maxPageLength", isLocked(LN), maxPageLength);
    putString(buf, "maxSignallingRate", isLocked(BR),
	brnames[fxmin(maxSignallingRate, BR_33600)]);
    putString(buf, "minScanlineTime", isLocked(ST),
	stnames[fxmin(minScanlineTime, ST_40MS)]);
    putString(buf, "remoteCSI", false, csi);
    putString(buf, "remoteNSF", false, nsf);
    putString(buf, "remoteDIS", false, dis);
    putDecimal(buf, "sendFailures", false, sendFailures);
    putIfString(buf, "lastSendFailure", false, lastSendFailure);
    putDecimal(buf, "dialFailures", false, dialFailures);
    putIfString(buf, "lastDialFailure", false, lastDialFailure);
    if (pagerMaxMsgLength != (u_int) -1)
	putDecimal(buf, "pagerMaxMsgLength", true, pagerMaxMsgLength);
    putIfString(buf, "pagerPassword", true, pagerPassword);
    putIfString(buf, "pagerTTYParity", true, pagerTTYParity);
    putIfString(buf, "pagingProtocol", isLocked(PAGING), pagingProtocol);
    putIfString(buf, "pageSource", true, pageSource);
    putIfString(buf, "pagerSetupCmds", true, pagerSetupCmds);
}
