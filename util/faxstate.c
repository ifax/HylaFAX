/*	$Id$ */
/*
 * Copyright (c) 1990-1996 Sam Leffler
 * Copyright (c) 1991-1996 Silicon Graphics, Inc.
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
#include <string.h>
#include <syslog.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"
#include "port.h"

static void
fatal(char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (isatty(fileno(stderr))) {
        vfprintf(stderr, fmt, ap);
        putc('\n', stderr);
    } else {
        vsyslog(LOG_ERR, fmt, ap);
    }
    va_end(ap);
    exit(-1);
}

extern	int cvtFacility(const char*, int*);

static const char*
modemstate(const char* arg)
{
    static struct {
	const char* tag;
	const char* state;
    } modemstates[] = {
	{ "busy",  "B" },
	{ "ready", "R" }, { "up",      "R" },
	{ "down",  "D" }, { "disable", "D" },
	{ NULL }
    };
    int i;

    for (i = 0; modemstates[i].tag != NULL; i++)
	if (strcmp(modemstates[i].tag, arg) == 0)
	    return (modemstates[i].state);
    fatal("Bad modem ready state `%s'; use one of busy, ready, and down", arg);
    /*NOTREACHED*/
    return (NULL);
}

int
main(int argc, char** argv)
{
    extern int optind;
    extern char* optarg;
    int fifo, c;
    char* spooldir = FAX_SPOOLDIR;
    const char* arg = modemstate("ready");
    char fifoname[256];
    char devid[256];
    int cmdlen;
    char cmd[80];
    char* appname;
    char* usage = "[-s state] [-q queue-dir] [-n]";
    char* cp;
    int facility = LOG_DAEMON;
    int nofaxgetty = 0;

    (void) cvtFacility(LOG_FAX, &facility);
    openlog(argv[0], LOG_PID|LOG_ODELAY, facility);
    appname = strrchr(argv[0], '/');
    if (appname)
	appname++;
    else
	appname = argv[0];
    while ((c = getopt(argc, argv, "s:q:n")) != -1)
	switch (c) {
	case 'n':
	    nofaxgetty = 1;
	    break;
	case 's':
	    arg = modemstate(optarg);
	    break;
	case 'q':
	    spooldir = optarg;
	    break;
	case '?':
	    fatal("usage: %s %s devid", argv[0], usage);
	    /*NOTREACHED*/
	}
    if (optind != argc-1) {
        fatal("usage: %s %s modem", argv[0], usage);
    }
    if (strlen(argv[optind]) < sizeof(devid)) {
        strcpy(devid, argv[optind]);
    } else {
        fatal("Argument is too large: %s", argv[optind]);
    }
    for (cp = devid; (cp = strchr(cp, '/')); *cp++ = '_')
	;
    if (chdir(spooldir) < 0)
	fatal("%s: chdir: %s", spooldir, strerror(errno));
    if (nofaxgetty) {
	/*
	 * No faxgetty process, contact faxq directly and emulate
	 * what faxgetty would send.
	 */
	fifo = open(FAX_FIFO, O_WRONLY|O_NDELAY);
        if (fifo < 0) {
            fatal("%s: open: %s", FAX_FIFO, strerror(errno));
        }
        cmdlen = snprintf(cmd, sizeof(cmd), "+%s:%s", devid, arg);
        if (cmdlen < 0 || cmdlen >= sizeof(cmd) || write(fifo, cmd, cmdlen) != cmdlen) {
            fatal("FIFO write failed for command (%s)", strerror(errno));
        }
    } else {
        snprintf(fifoname, sizeof(fifoname), "%s.%.*s", FAX_FIFO,
            sizeof (fifoname) - sizeof (FAX_FIFO), devid);
        fifo = open(fifoname, O_WRONLY|O_NDELAY);
        if (fifo < 0) {
            fatal("%s: open: %s", fifoname, strerror(errno));
        }
        cmdlen = snprintf(cmd, sizeof(cmd), "S%s", arg);
        if (cmdlen < 0 || cmdlen >= sizeof(cmd) || write(fifo, cmd, cmdlen) != cmdlen) {
            fatal("FIFO write failed for command (%s)", strerror(errno));
        }
    }
    (void) close(fifo);
    return 0;
}
