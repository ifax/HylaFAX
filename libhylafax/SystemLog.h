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
#ifndef _SystemLog_
#define	_SystemLog_
/*
 * SystemLog Support.
 */
#include <stdarg.h>
extern "C" {
#include <syslog.h>
}

class SystemLog {
public:
    static int	facility;		// syslog facility
    static const char* appName;		// app name for syslog messages

    static void setupLogging(const char* appName);
    static void setupLogging(void);
    static void closeLogging(void);
    static void setLogFacility(const char* facility);
    static int getLogFacility(void);
};
inline int SystemLog::getLogFacility(void)		{ return facility; }

extern	void logError(const char* fmt ...);
extern	void logInfo(const char* fmt ...);
extern	void logNotice(const char* fmt ...);
extern	void logDebug(const char* fmt ...);
extern	void logWarning(const char* fmt ...);
extern	void vlogError(const char* fmt, va_list ap);
extern	void vlogInfo(const char* fmt, va_list ap);
extern	void vlogNotice(const char* fmt, va_list ap);
extern	void vlogDebug(const char* fmt, va_list ap);
extern	void vlogWarning(const char* fmt, va_list ap);
#endif /* _SystemLog_ */
