/*	$Id$ */
/*
 * Copyright (c) 1994-1996 Sam Leffler
 * Copyright (c) 1994-1996 Silicon Graphics, Inc.
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
#include <stdio.h>
#include <syslog.h>
#include <string.h>

static struct {
	char	*c_name;
	int	c_val;
} facilitynames[] = {
#ifdef LOG_AUDIT
	"audit",	LOG_AUDIT,
#endif
#ifdef LOG_AUTH
	"auth",		LOG_AUTH,
#endif
#ifdef LOG_CRON
	"cron", 	LOG_CRON,
#endif
#ifdef LOG_DAEMON
	"daemon",	LOG_DAEMON,
#endif
#ifdef LOG_MAIL
	"mail",		LOG_MAIL,
#endif
#ifdef LOG_NEWS
	"news",		LOG_NEWS,
#endif
#ifdef LOG_SAT
	"sat",		LOG_AUDIT,
#endif
#ifdef LOG_SYSLOG
	"syslog",	LOG_SYSLOG,
#endif
#ifdef LOG_USER
	"user",		LOG_USER,
#endif
#ifdef LOG_UUCP
	"uucp",		LOG_UUCP,
#endif
#ifdef LOG_LOCAL0
	"local0",	LOG_LOCAL0,
#endif
#ifdef LOG_LOCAL1
	"local1",	LOG_LOCAL1,
#endif
#ifdef LOG_LOCAL2
	"local2",	LOG_LOCAL2,
#endif
#ifdef LOG_LOCAL3
	"local3",	LOG_LOCAL3,
#endif
#ifdef LOG_LOCAL4
	"local4",	LOG_LOCAL4,
#endif
#ifdef LOG_LOCAL5
	"local5",	LOG_LOCAL5,
#endif
#ifdef LOG_LOCAL6
	"local6",	LOG_LOCAL6,
#endif
#ifdef LOG_LOCAL7
	"local7",	LOG_LOCAL7,
#endif
	NULL,		-1,
};

int
cvtFacility(const char* name, int* facility)
{
    int i;

    for (i = 0; facilitynames[i].c_name != NULL; i++)
	if (strcasecmp(facilitynames[i].c_name, name) == 0) {
	    *facility = facilitynames[i].c_val;
	    return (1);
	}
    return (0);
}
