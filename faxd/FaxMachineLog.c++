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
#include <ctype.h>
extern "C" {
#include <sys/time.h>
}
#include <errno.h>

#include "Sys.h"

#include "config.h"
#include "FaxMachineLog.h"
#include "StackBuffer.h"

extern void logError(const char* fmt ...);

FaxMachineLog::FaxMachineLog(int f, const fxStr& number, const fxStr& commid)
{
    fxStr canon(number);
    for (int i = canon.length()-1; i >= 0; i--)
	if (!isdigit(canon[i]))
	    canon.remove(i,1);
    fd = f;
    pid = getpid();
    log("SESSION BEGIN %s %s", (const char*) commid, (const char*) canon);
    log("%s", HYLAFAX_VERSION);
}

FaxMachineLog::~FaxMachineLog()
{
    if (fd != -1) {
	log("SESSION END");
	Sys::close(fd);
    }
}

void
FaxMachineLog::log(const char* fmt, ...)
{
   if (fd != -1) {
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
   }
}

void
FaxMachineLog::vlog(const char* fmt0, va_list ap)
{
   if (fd == -1)
	return;
    int oerrno = errno;			// save errno on entry
    char buf[1024];
    timeval tv;
    (void) gettimeofday(&tv, 0);
    strftime(buf, sizeof (buf), "%h %d %T", localtime((time_t*) &tv.tv_sec));
    fxStr s = buf | fxStr::format(".%02u: [%5d]: ", tv.tv_usec / 10000, pid);
    /*
     * Copy format string into a local buffer so
     * that we can substitute for %m, a la syslog.
     */
    fxStackBuffer fmt;
    for (const char* fp = fmt0; *fp; fp++) {
	if (fp[0] == '%')
	    switch (fp[1]) {
	    case '%':
		fmt.put("%%"); fp++;
		continue;
	    case 'm':			// substitute errno string
		fmt.put(strerror(oerrno)), fp++;
		continue;
	    }
	fmt.put(fp[0]);
    }
    fmt.put('\n'); fmt.put('\0');
    s.append(fxStr::vformat((const char*) fmt, ap));
    (void) Sys::write(fd, (const char*)s, s.length());
}
