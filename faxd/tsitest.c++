/*	$Id$ */
/*
 * Copyright (c) 1994-1996 Sam Leffler
 * Copyright (c) 1994-1996 Silicon Graphics, Inc.
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
/*
 * Interactive program for TSI checking support.
 *
 * Usage: tsitest [tsifile]
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "REArray.h"
#include "BoolArray.h"
#include "Str.h"

fxStr		qualifyTSI;
REArray*	tsiPats = NULL;		// recv tsi patterns
fxBoolArray*	acceptTSI = NULL;	// accept/reject matched tsi
time_t		lastPatModTime = 0;	// last mod time of patterns file

/*
 * Read the file of TSI patterns into an array.
 *
 * The order of patterns is preserved and a leading ``!''
 * is interpreted to mean ``reject TSI if matched by this
 * regex''.  Note also that we always allocate an array
 * of patterns.  The TSI pattern matching logic rejects
 * TSI that don't match any patterns.  Thus an empty file
 * causes all incoming facsimile to be rejected.
 */
void
readTSIPatterns(FILE* fd, REArray*& pats, fxBoolArray*& accept)
{
    if (pats)
	pats->resize(0);
    else
	pats = new REArray;
    if (accept)
	accept->resize(0);
    else
	accept = new fxBoolArray;

    char line[256];
    while (fgets(line, sizeof (line)-1, fd)) {
	char* cp = strchr(line, '#');
	if (cp || (cp = strchr(line, '\n')))
	    *cp = '\0';
	/* trim off trailing white space */
	for (cp = strchr(line, '\0'); cp > line; cp--)
	    if (!isspace(cp[-1]))
		break;
	*cp = '\0';
	if (line[0] == '\0')
	    continue;
	RE* re;
	if (line[0] == '!') {
	    accept->append(false);
	    pats->append(re = new RE(line+1));
	} else {
	    accept->append(true);
	    pats->append(re = new RE(line));
	}
	if (re->getErrorCode() > REG_NOMATCH) {
	    fxStr emsg;
	    re->getError(emsg);
	    printf("Bad TSI pattern: %s: " | emsg | ".\n", re->pattern());
	}
    }
}

/*
 * Update the TSI pattern arrays if the file
 * of TSI patterns has been changed since the last
 * time we read it.
 */
void
updateTSIPatterns()
{
    FILE* fd = fopen((const char*) qualifyTSI, "r");
    if (fd != NULL) {
	struct stat sb;
	if (fstat(fileno(fd), &sb) >= 0 && sb.st_mtime >= lastPatModTime) {
	    readTSIPatterns(fd, tsiPats, acceptTSI);
	    lastPatModTime = sb.st_mtime;
	}
	fclose(fd);
    } else if (tsiPats) {
	// file's been removed, delete any existing info
	delete tsiPats, tsiPats = NULL;
	delete acceptTSI, acceptTSI = NULL;
    }
}

extern	void fxFatal(const char* va_alist ...);

static	const char* appName;

static void
usage()
{
    fxFatal("usage: %s [-q] tsifile", appName);
}

static int
prompt()
{
    printf("ready> "); fflush(stdout);
    return (1);
}

int
main(int argc, char* argv[])
{
    bool verbose = true;
    extern int optind, opterr;
    extern char* optarg;
    int c;

    appName = argv[0];
    while ((c = getopt(argc, argv, ":q")) != -1)
	switch (c) {
	case 'q':
	    verbose = false;
	    break;
	case '?':
	    usage();
	    /*NOTREACHED*/
	}
    if (argc - optind != 1)
	usage();
    qualifyTSI = argv[optind];
    updateTSIPatterns();
    char line[1024];
    while (prompt() && fgets(line, sizeof (line), stdin)) {
	char* cp = strchr(line, '\n');
	if (cp)
	    *cp = '\0';
	if (verbose)
	    printf("input = \"%s\"\n", line);
	updateTSIPatterns();
	if (tsiPats != NULL) {
	    u_int i;
	    for (i = 0; i < tsiPats->length(); i++) {
		RE* pat = (*tsiPats)[i];
		if (verbose)
		    printf("[check %s]\n", pat->pattern());
		fxStr tsi(line);
		if (pat->Find(tsi)) {
		    printf("%s (matched by %s)\n",
			(*acceptTSI)[i] ? "accept" : "reject",
			pat->pattern());
		    break;
		}
	    }
	    if (i == tsiPats->length())
		printf("reject (no pattern match)\n");
	} else
	    printf("reject (no patterns)\n");
    }
    return (0);
}
