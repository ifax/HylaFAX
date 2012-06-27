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

int
main(int argc, char** argv)
{
    extern int optind;
    extern char* optarg;
    int fifo, c;
    char* spooldir = FAX_SPOOLDIR;
    const char* arg = NULL;
    char fifoname[256];
    int cmdlen, fnlen;
    char cmd[80];
    char* appname;
    const char* opts = NULL;
    const char* usage = NULL;
    const char* cmdfmt = NULL;
    char* cp;
    int facility = LOG_DAEMON;
    int modemRequired = 0;

    (void) cvtFacility(LOG_FAX, &facility);
    openlog(argv[0], LOG_PID|LOG_ODELAY, facility);
    appname = strrchr(argv[0], '/');
    if (appname)
	appname++;
    else
	appname = argv[0];
    if (strcmp(appname, "faxanswer") == 0) {
	opts = "h:q:";
	usage = "[-h how] [-q queue-dir] modem";
	cmdfmt = "A%s";
	arg = "";			/* default how */
	modemRequired = 1;
    } else if (strcmp(appname, "faxquit") == 0) {
	opts = "q:";
	usage = "[-q queue-dir] [modem]";
	cmdfmt = "Q";
	modemRequired = 0;
    } else if (strcmp(appname, "faxabort") == 0) {
	opts = "q:";
	usage = "[-q queue-dir] modem";
	cmdfmt = "Z";
	modemRequired = 1;
    } else if (strcmp(appname, "faxlock") == 0) {
	opts = "q:";
	usage = "[-q queue-dir] modem";
	cmdfmt = "L";
	modemRequired = 1;
    } else {
	fatal("Unrecognized command name %s", appname);
    }
    while ((c = getopt(argc, argv, opts)) != -1)
	switch (c) {
	case 'h':
	    arg = optarg;
            if (strlen(arg) > sizeof(cmd)-2) {
                /* The "how" value won't fit in cmd with the command tag
                 * and terminating NUL character. */
                fatal("How value too long: %s", arg);
            }
	    break;
	case 'q':
	    spooldir = optarg;
	    break;
	case '?':
	    fatal("usage: %s %s", argv[0], usage);
	    /*NOTREACHED*/
	}
    if (optind == argc-1) {
        if (argv[optind][0] == FAX_FIFO[0]) {
            if (strlen(argv[optind]) < sizeof(fifoname)) {
                strcpy(fifoname, argv[optind]);
            } else {
                fatal("Argument name too long: %s", argv[optind]);
            }
        } else {
            fnlen = snprintf(fifoname, sizeof(fifoname), "%s.%.*s", FAX_FIFO,
                (int) (sizeof(fifoname) - sizeof(FAX_FIFO)), argv[optind]);
            if (fnlen < 0 || fnlen >= sizeof(fifoname)) {
                fatal("Argument name too long: %s", argv[optind]);
            }
        }
    } else if (!modemRequired) {
        strcpy(fifoname, FAX_FIFO);
    } else {
        fatal("usage: %s %s", argv[0], usage);
    }
    for (cp = fifoname; (cp = strchr(cp, '/')); *cp++ = '_')
	;
    if (chdir(spooldir) < 0) {
        fatal("%s: chdir: %s", spooldir, strerror(errno));
    }
    fifo = open(fifoname, O_WRONLY|O_NDELAY);
    if (fifo < 0) {
        fatal("%s: open: %s", fifoname, strerror(errno));
    }
    cmdlen = snprintf(cmd, sizeof(cmd), cmdfmt, arg);
    if (write(fifo, cmd, cmdlen) != cmdlen) {
        fatal("FIFO write failed for command (%s)", strerror(errno));
    }
    (void) close(fifo);
    return 0;
}
