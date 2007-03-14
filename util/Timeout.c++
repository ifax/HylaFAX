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
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include "Timeout.h"

bool Timeout::timerExpired = false;

Timeout::Timeout() {}
Timeout::~Timeout() {}
void Timeout::traceTimer(const char* ...) {}

void
Timeout::sigAlarm(int)
{
    Timeout::timerExpired = true;
}

#ifndef SA_INTERRUPT
#define	SA_INTERRUPT	0
#endif

void
Timeout::startTimeout(long ms)
{
    timerExpired = false;
#ifdef SA_NOCLDSTOP			/* POSIX */
    static struct sigaction sa;
    sa.sa_handler = fxSIGACTIONHANDLER(Timeout::sigAlarm);
    sa.sa_flags = SA_INTERRUPT;
    sigaction(SIGALRM, &sa, (struct sigaction*) 0);
#else
#ifdef SV_INTERRUPT			/* BSD-style */
    static struct sigvec sv;
    sv.sv_handler = fxSIGVECHANDLER(Timeout::sigAlarm);
    sv.sv_flags = SV_INTERRUPT;
    sigvec(SIGALRM, &sv, (struct sigvec*) 0);
#else					/* System V-style */
    signal(SIGALRM, fxSIGHANDLER(sigAlarm));
#endif
#endif
#ifdef ITIMER_REAL
    itimerval itv;
    itv.it_value.tv_sec = ms / 1000;
    itv.it_value.tv_usec = (ms % 1000) * 1000;
    timerclear(&itv.it_interval);
    (void) setitimer(ITIMER_REAL, &itv, (itimerval*) 0);
    traceTimer("START %ld.%02ld second timeout",
	itv.it_value.tv_sec, itv.it_value.tv_usec / 10000);
#else
    long secs = howmany(ms, 1000);
    (void) alarm(secs);
    traceTimer("START %ld second timeout", secs);
#endif
}

void
Timeout::stopTimeout()
{
#ifdef ITIMER_REAL
    static itimerval itv = { { 0, 0 }, { 0, 0 } };
    (void) setitimer(ITIMER_REAL, &itv, (itimerval*) 0);
#else
    (void) alarm(0);
#endif
    traceTimer("STOP timeout%s", timerExpired ? ", timer expired" : "");
}
