/*	$Id$
/*
 * Copyright (c) 1994-1996 Sam Leffler
 * Copyright (c) 1994-1996 Silicon Graphics, Inc.
 * HylaFAX is a trademark of Silicon Graphics, Inc.
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
#include <stdio.h>
#include <sys/types.h>
#include <stdarg.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

void
vsyslog(int pri, const char* fmt, va_list ap)
{
	char tbuf[2048], fmt_cpy[1024];
	char* cp;
	char c;

	/* substitute error message for %m */
	for (cp = fmt_cpy; c = *fmt; ++fmt) {
	    if (c == '%' && fmt[1] == 'm') {
		const char* dp;
		++fmt;
		for (dp = strerror(errno); *cp = *dp++; ++cp)
		    ;
	    } else
		*cp++ = c;
	    *cp = '\0';
	}
	(void) vsprintf(tbuf, fmt_cpy, ap);
	(void) syslog(pri, "%s", tbuf);
}
