/*	$Id$ */
/*
 * Copyright (c) 1995-1996 Sam Leffler
 * Copyright (c) 1995-1996 Silicon Graphics, Inc.
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
#include "port.h"
#include "config.h"
#include "SystemLog.h"

/*
 * Logging and error routines.
 */

extern	"C" int cvtFacility(const char*, int*);

int	SystemLog::facility = LOG_DAEMON;
const char* SystemLog::appName = "FaxApp";		// generic, override

void
SystemLog::setupLogging(const char* name)
{
    appName = name;
    openlog(name, LOG_PID|LOG_ODELAY, facility);
}
void
SystemLog::setupLogging(void)
{
    // NB: LOG_NDELAY 'cuz of chroot done by hfaxd
    openlog(appName, LOG_PID|LOG_NDELAY, facility);
}
void SystemLog::closeLogging(void) { closelog(); }

void
SystemLog::setLogFacility(const char* fac)
{
    if (!cvtFacility(fac, &facility))
	logError("Unknown syslog facility name \"%s\"", fac);
}

void vlogInfo(const char* fmt, va_list ap)
    { vsyslog(LOG_INFO|SystemLog::getLogFacility(), fmt, ap); }
void
logInfo(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogInfo(fmt, ap);
    va_end(ap);
}

void vlogError(const char* fmt, va_list ap)
    { vsyslog(LOG_ERR|SystemLog::getLogFacility(), fmt, ap); }
void
logError(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogError(fmt, ap);
    va_end(ap);
}

void vlogNotice(const char* fmt, va_list ap)
    { vsyslog(LOG_NOTICE|SystemLog::getLogFacility(), fmt, ap); }
void
logNotice(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogNotice(fmt, ap);
    va_end(ap);
}

void vlogDebug(const char* fmt, va_list ap)
    { vsyslog(LOG_DEBUG|SystemLog::getLogFacility(), fmt, ap); }
void
logDebug(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogDebug(fmt, ap);
    va_end(ap);
}

void vlogWarning(const char* fmt, va_list ap)
    { vsyslog(LOG_WARNING|SystemLog::getLogFacility(), fmt, ap); }
void
logWarning(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogWarning(fmt, ap);
    va_end(ap);
}
