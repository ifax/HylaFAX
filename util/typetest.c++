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
 * Program for testing out typerules.
 *
 * Usage: typetest [-f rulesfile] files
 */
#include "Sys.h"

#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>

#include "TypeRules.h"
#include "config.h"

TypeRules* typeRules;

/*
 * Return a TypeRule for the specified file.
 */
const TypeRule*
fileType(const char* filename)
{
    struct stat sb;
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
	fprintf(stderr, "%s: Can not open file\n", filename);
	return (NULL);
    }
    if (fstat(fd, &sb) < 0) {
	fprintf(stderr, "%s: Can not stat file\n", filename);
	close(fd);
	return (NULL);
    }
    if ((sb.st_mode & S_IFMT) != S_IFREG) {
	fprintf(stderr, "%s: Not a regular file\n", filename);
	close(fd);
	return (NULL);
    }
    char buf[512];
    int cc = read(fd, buf, sizeof (buf));
    close(fd);
    if (cc == 0) {
	fprintf(stderr, "%s: Empty file\n", filename);
	return (NULL);
    }
    const TypeRule* tr = typeRules->match(buf, cc);
    if (!tr) {
	fprintf(stderr, "%s: Can not determine file type\n", filename);
	return (NULL);
    }
    if (tr->getResult() == TypeRule::ERROR) {
	fxStr emsg(tr->getErrMsg());
	fprintf(stderr, "%s: %s\n", filename, (char*) emsg);
	return (NULL);
    }
    return tr;   
}

char*	appName;

void
usage()
{
    fprintf(stderr, "usage: %s [-f rulesfile] files\n", appName);
    exit(-1);
}

int
main(int argc, char* argv[])
{
    extern int optind, opterr;
    extern char* optarg;
    int c;
    fxStr file;

    appName = argv[0];
    file = FAX_LIBDATA "/" FAX_TYPERULES;
    while ((c = getopt(argc, argv, "f:")) != -1)
	switch (c) {
	case 'f':
	    file = optarg;
	    break;
	case '?':
	    usage();
	    /*NOTREACHED*/
	}
    if (argc - optind < 1)
	usage();
    typeRules = TypeRules::read(file);
    if (!typeRules) {
	fprintf(stderr, "Unable to setup file typing and conversion rules\n");
	return (-1);
    }
    typeRules->setVerbose(TRUE);
    for (; optind < argc; optind++)
	(void) fileType(argv[optind]);
    return (0);
}
