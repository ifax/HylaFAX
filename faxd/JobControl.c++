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
#include <ctype.h>

#include "Sys.h"

#include "JobControl.h"
#include "faxQueueApp.h"
#include "FaxTrace.h"

#define	DCI_MAXCONCURRENTCALLS	0x0001
#define	DCI_TIMEOFDAY		0x0002
#define	DCI_MAXSENDPAGES	0x0004
#define	DCI_MAXDIALS		0x0008
#define	DCI_MAXTRIES		0x0010
#define	DCI_USEXVRES		0x0020
#define	DCI_VRES		0x0040

#define	isDefined(b)		(defined & b)
#define	setDefined(b)		(defined |= b)

JobControlInfo::JobControlInfo()		 	{ defined = 0; }
JobControlInfo::JobControlInfo(const JobControlInfo& other)
    : rejectNotice(other.rejectNotice)
    , modem(other.modem)
    , tod(other.tod)
    , args(other.args)
{
    defined = other.defined;
    maxConcurrentCalls = other.maxConcurrentCalls;
    maxSendPages = other.maxSendPages;
    maxDials = other.maxDials;
    maxTries = other.maxTries;
    usexvres = other.usexvres;
    vres = other.vres;
}

JobControlInfo::JobControlInfo (const fxStr& buffer)
{
    defined = 0;
    u_int pos = 0;
    u_int last_pos = 0;
    int loop = 0;
    while ( (pos = buffer.next(last_pos, '\n')) < buffer.length() )
    {
    	// Quick safety-net
	if (loop++ > 100)
	    break;

    	fxStr l(buffer.extract(last_pos, pos - last_pos));
	last_pos = pos+1;

	readConfigItem(l);
    }
}

JobControlInfo::~JobControlInfo() {}

bool
JobControlInfo::isCompatible (const JobControlInfo& other) const
{
    if (args != other.args)
	return false;

    return true;
}

void
JobControlInfo::configError (const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogError(fxStr::format("JobControl: %s", fmt) , ap);
    va_end(ap);
}

void
JobControlInfo::configTrace (const char*, ...)
{
   // We don't trace JobControl parsing...
}

bool
JobControlInfo::setConfigItem (const char* tag, const char* value)
{
    if (streq(tag, "rejectnotice")) {
	rejectNotice = value;
    } else if (streq(tag, "modem")) {
	modem = value;
    } else if (streq(tag, "maxconcurrentjobs")) {	// backwards compatibility
	maxConcurrentCalls = getNumber(value);
	setDefined(DCI_MAXCONCURRENTCALLS);
    } else if (streq(tag, "maxconcurrentcalls")) {
	maxConcurrentCalls = getNumber(value);
	setDefined(DCI_MAXCONCURRENTCALLS);
    } else if (streq(tag, "maxsendpages")) {
	maxSendPages = getNumber(value);
	setDefined(DCI_MAXSENDPAGES);
    } else if (streq(tag, "maxdials")) {
	maxDials = getNumber(value);
	setDefined(DCI_MAXDIALS);
    } else if (streq(tag, "maxtries")) {
	maxTries = getNumber(value);
	setDefined(DCI_MAXTRIES);
    } else if (streq(tag, "timeofday")) {
	tod.parse(value);
	setDefined(DCI_TIMEOFDAY);
    } else if (streq(tag, "usexvres")) {
	usexvres = getNumber(value);
	setDefined(DCI_USEXVRES);
    } else if (streq(tag, "vres")) {
	vres = getNumber(value);
	setDefined(DCI_VRES);
    } else {
	if( args != "" )
	    args.append('\0');
	args.append(fxStr::format("-c%c%s:\"%s\"",
	    '\0', tag, value));
    }
    return true;
}

u_int
JobControlInfo::getMaxConcurrentCalls() const
{
    if (isDefined(DCI_MAXCONCURRENTCALLS))
	return maxConcurrentCalls;
    else
	return faxQueueApp::instance().getMaxConcurrentCalls();
}

u_int
JobControlInfo::getMaxSendPages() const
{
    if (isDefined(DCI_MAXSENDPAGES))
	return maxSendPages;
    else
	return faxQueueApp::instance().getMaxSendPages();
}

u_int
JobControlInfo::getMaxDials() const
{
    if (isDefined(DCI_MAXDIALS))
	return maxDials;
    else
	return faxQueueApp::instance().getMaxDials();
}

u_int
JobControlInfo::getMaxTries() const
{
    if (isDefined(DCI_MAXTRIES))
	return maxTries;
    else
	return faxQueueApp::instance().getMaxTries();
}

const fxStr&
JobControlInfo::getRejectNotice() const
{
    return rejectNotice;
}

const fxStr&
JobControlInfo::getModem() const
{
    return modem;
}

time_t
JobControlInfo::nextTimeToSend(time_t t) const
{
    if (isDefined(DCI_TIMEOFDAY))
	return tod.nextTimeOfDay(t);
    else
	return faxQueueApp::instance().nextTimeToSend(t);
}

int
JobControlInfo::getUseXVRes() const
{
    if (isDefined(DCI_USEXVRES))
	return usexvres;
    else
	return -1;
}

u_int
JobControlInfo::getVRes() const
{
    if (isDefined(DCI_VRES))
	return vres;
    else
	return 0;
}

const fxStr& JobControlInfo::getArgs() const
{
    return args;
}
