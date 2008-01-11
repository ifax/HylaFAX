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
#ifndef _JobExt_
#define	_JobExt_
/*
 * Externalized Form of a Queue Manager Job.
 */
#include "Str.h"

/*
 * This structure must correspond to that of the Job
 * class.  Note also how members are ordered so that
 * simple items can be block-copied when encoding/decoding.
 */
struct JobExtFixed {
    time_t	tts;		// time to send job
    time_t	killtime;	// time to kill job
    time_t	start;		// time job passed to modem
    int		pri;		// priority
    pid_t	pid;		// pid of current subprocess
    u_short	state;		// scheduling state
};
struct JobExt : public JobExtFixed {
    fxStr	jobid;		// job identifier
    fxStr	dest;		// canonical destination identity
    fxStr	device;		// modem to be used
    fxStr	commid;		// communication identifier

    JobExt();
    ~JobExt();

    const char* decode(const char*);
};
#endif /* _JobExt_ */
