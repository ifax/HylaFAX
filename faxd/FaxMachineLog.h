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
#ifndef _FaxMachineLog_
#define	_FaxMachineLog_
/*
 * Fax Machine Session Logging Support.
 */
#include "Str.h"
#include <stdarg.h>

/*
 * Each fax session is logged to a separate file.  Sends
 * are logged to files named according to the destination
 * fax machine's number (in a canonical format), while
 * receptions are logged to the number of the receiver
 * (i.e. the local fax number).  The contents of the log
 * is controlled by the SessionTracing configuration
 * parameter maintained by the server.
 */
class FaxMachineLog {
private:
    int		fd;
    pid_t	pid;
public:
    FaxMachineLog(int fd, const fxStr& number, const fxStr& commid);
    ~FaxMachineLog();

    void log(const char* fmt, ...);
    void vlog(const char* fmt, va_list ap);
};
#endif /* _FaxMachineLog_ */
