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
    changed = FALSE;
    resetConfig();
}
FaxMachineInfo::FaxMachineInfo(const FaxMachineInfo& other)
    : FaxConfig(other)
    , file(other.file)
    , csi(other.csi)
    , lastSendFailure(other.lastSendFailure)
    , lastDialFailure(other.lastDialFailure)
    , pagerPassword(other.pagerPassword)
    , pagerTTYParity(other.pagerTTYParity)
    , pagingProtocol(other.pagingProtocol)
    , pageSource(other.pageSource)
    , pagerSetupCmds(other.pagerSetupCmds)
{
    locked = other.locked;

    supportsHighRes = other.supportsHighRes;
    supports2DEncoding = other.supports2DEncoding;
    supportsPostScript = other.supportsPostScript;
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

int
FaxMachineInfo::getMaxPageWidthInMM() const
{
    return (int)(maxPageWidth/(204.0f/25.4f));
}

#include <ctype.h>

fxBool
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
    supportsHighRes = TRUE;		// assume 196 lpi support
    supports2DEncoding = TRUE;		// assume 2D-encoding support
    supportsPostScript = FALSE;		// no support for Adobe protocol
    calledBefore = FALSE;		// never called before
    maxPageWidth = 2432;		// max required width
    maxPageLength = -1;			// infinite page length
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
   { "2400", "4800", "7200", "9600", "12000", "14400" };
#define	NBR	N(brnames)
static const char* stnames[] =
   { "0ms", "5ms", "10ms/5ms", "10ms",
     "20ms/10ms", "20ms", "40ms/20ms", "40ms" };
#define	NST	N(stnames)

#define	HIRES	0
#define	G32D	1
#define	PS	2
#define	WD	3
#define	LN	4
#define	BR	5
#define	ST	6

#define	setLocked(b,ix)	locked |= b<<ix

fxBool
FaxMachineInfo::setConfigItem(const char* tag, const char* value)
{
    int b = (tag[0] == '&' ? 1 : 0);	// locked down indicator
    if (b) tag++;
    if (streq(tag, "supportshighres")) {
	supportsHighRes = getBoolean(value);
	setLocked(b, HIRES);
    } else if (streq(tag, "supports2dencoding")) {
	supports2DEncoding = getBoolean(value);
	setLocked(b, G32D);
    } else if (streq(tag, "supportspostscript")) {
	supportsPostScript = getBoolean(value);
	setLocked(b, PS);
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
    } else if (streq(tag, "pagesource")) {
	pageSource = value;
    } else if (streq(tag, "pagersetupcmds")) {
	pagerSetupCmds = value;
    } else
	return (FALSE);
    return (TRUE);
}

#define	isLocked(b)	(locked & (1<<b))

#define	checkLock(ix, member, value)	\
    if (!isLocked(ix)) {		\
	member = value;			\
	changed = TRUE;			\
    }

void FaxMachineInfo::setSupportsHighRes(fxBool b)
    { checkLock(HIRES, supportsHighRes, b); }
void FaxMachineInfo::setSupports2DEncoding(fxBool b)
    { checkLock(G32D, supports2DEncoding, b); }
void FaxMachineInfo::setSupportsPostScript(fxBool b)
    { checkLock(PS, supportsPostScript, b); }
void FaxMachineInfo::setMaxPageWidthInPixels(int v)
    { checkLock(WD, maxPageWidth, v); }
void FaxMachineInfo::setMaxPageLengthInMM(int v)
    { checkLock(LN, maxPageLength, v); }
void FaxMachineInfo::setMaxSignallingRate(int v)
    { checkLock(BR, maxSignallingRate, v); }
void FaxMachineInfo::setMinScanlineTime(int v)
    { checkLock(ST, minScanlineTime, v); }

void
FaxMachineInfo::setCalledBefore(fxBool b)
{
    calledBefore = b;
    changed = TRUE;
}

#define	checkChanged(member, value)	\
    if (member != value) {		\
	member = value;			\
	changed = TRUE;			\
    }

void FaxMachineInfo::setCSI(const fxStr& v)
    { checkChanged(csi, v); }
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
	    if (Sys::write(fd, buf, cc) != cc) {
		error("write error: %s", strerror(errno));
		Sys::close(fd);
		return;
	    }
	    ftruncate(fd, cc);
	    Sys::close(fd);
	} else
	    error("open: %m");
	changed = FALSE;
    }
}

static void
putBoolean(fxStackBuffer& buf, const char* tag, fxBool locked, fxBool b)
{
    buf.fput("%s%s:%s\n", locked ? "&" : "", tag, b ? "yes" : "no");
}

static void
putDecimal(fxStackBuffer& buf, const char* tag, fxBool locked, int v)
{
    buf.fput("%s%s:%d\n", locked ? "&" : "", tag, v);
}

static void
putString(fxStackBuffer& buf, const char* tag, fxBool locked, const char* v)
{
    buf.fput("%s%s:\"%s\"\n", locked ? "&" : "", tag, v);
}

static void
putIfString(fxStackBuffer& buf, const char* tag, fxBool locked, const char* v)
{
    if (*v != '\0')
	buf.fput("%s%s:\"%s\"\n", locked ? "&" : "", tag, v);
}

void
FaxMachineInfo::writeConfig(fxStackBuffer& buf)
{
    putBoolean(buf, "supportsHighRes", isLocked(HIRES), supportsHighRes);
    putBoolean(buf, "supports2DEncoding", isLocked(G32D),supports2DEncoding);
    putBoolean(buf, "supportsPostScript", isLocked(PS), supportsPostScript);
    putBoolean(buf, "calledBefore", FALSE, calledBefore);
    putDecimal(buf, "maxPageWidth", isLocked(WD), maxPageWidth);
    putDecimal(buf, "maxPageLength", isLocked(LN), maxPageLength);
    putString(buf, "maxSignallingRate", isLocked(BR),
	brnames[fxmin(maxSignallingRate, BR_14400)]);
    putString(buf, "minScanlineTime", isLocked(ST),
	stnames[fxmin(minScanlineTime, ST_40MS)]);
    putString(buf, "remoteCSI", FALSE, csi);
    putDecimal(buf, "sendFailures", FALSE, sendFailures);
    putIfString(buf, "lastSendFailure", FALSE, lastSendFailure);
    putDecimal(buf, "dialFailures", FALSE, dialFailures);
    putIfString(buf, "lastDialFailure", FALSE, lastDialFailure);
    if (pagerMaxMsgLength != (u_int) -1)
	putDecimal(buf, "pagerMaxMsgLength", TRUE, pagerMaxMsgLength);
    putIfString(buf, "pagerPassword", TRUE, pagerPassword);
    putIfString(buf, "pagerTTYParity", TRUE, pagerTTYParity);
    putIfString(buf, "pagingProtocol", TRUE, pagingProtocol);
    putIfString(buf, "pageSource", TRUE, pageSource);
    putIfString(buf, "pagerSetupCmds", TRUE, pagerSetupCmds);
}
