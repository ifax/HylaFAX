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
/*
 * Program for interactively using dial string rules.
 *
 * Usage: dialtest [-v] [-a areacode] [-c countrycode]
 *    [-i internationalprefix] [-l longdistanceprefix] dial-rules-file
 */
#include <stdlib.h>
#include <unistd.h>
#include "DialRules.h"

extern	void fxFatal(const char* va_alist ...);

static	const char* appName;

static void
usage()
{
    fxFatal("usage: %s"
	" [-v]"
	" [-a area-code]"
	" [-c country-code]"
	" [-i international-prefix]"
	" [-l long-distance-prefix]"
	" dialrules"
	, appName
	);
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
    char* areaCode = "415";
    char* countryCode = "1";
    char* internationalPrefix = "011";
    char* longDistancePrefix = "1";
    bool verbose = false;
    extern int optind, opterr;
    extern char* optarg;
    int c;

    appName = argv[0];
    while ((c = getopt(argc, argv, "a:c:i:l:v")) != -1)
	switch (c) {
	case 'a':
	    areaCode = optarg;
	    break;
	case 'c':
	    countryCode = optarg;
	    break;
	case 'i':
	    internationalPrefix = optarg;
	    break;
	case 'l':
	    longDistancePrefix = optarg;
	    break;
	case 'v':
	    verbose = true;
	    break;
	case '?':
	    usage();
	    /*NOTREACHED*/
	}
    if (argc - optind != 1)
	usage();
    DialStringRules rules(argv[optind]);
    rules.setVerbose(true);
    rules.def("AreaCode", areaCode);
    rules.def("CountryCode", countryCode);
    rules.def("InternationalPrefix", internationalPrefix);
    rules.def("LongDistancePrefix", longDistancePrefix);
    if (!rules.parse())
	fxFatal("%s: Problem parsing rules in %s", appName, argv[optind]);
    char line[1024];
    while (prompt() && fgets(line, sizeof (line), stdin)) {
	char* cp = strchr(line, '\n');
	if (cp)
	    *cp = '\0';
	if (verbose)
	    printf("input = \"%s\"\n", line);
	if (cp = strchr(line, '(')) {
	    char* ep = strchr(cp, ')');
	    if (ep)
		*ep = '\0';
	    fxStr set(line, cp-line);
	    fxStr result = rules.applyRules(set, cp+1);
	    printf("%s(%s) = \"%s\"\n", (const char*) set, cp+1, (const char*) result);
	} else {
	    fxStr c = rules.canonicalNumber(line);
	    fxStr d = rules.dialString(line);
	    fxStr n = rules.displayNumber(line);
	    printf("canonical = \"%s\"\n", (const char*) c);
	    printf("dial-string = \"%s\"\n", (const char*) d);
	    printf("display = \"%s\"\n", (const char *) n);
	}
    }
    return (0);
}
