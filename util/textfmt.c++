/*	$Id$ */
/*
 * Copyright (c) 1993-1996 Sam Leffler
 * Copyright (c) 1993-1996 Silicon Graphics, Inc.
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
 * A sort of enscript clone...
 */
#include "TextFormat.h"
#include "Sys.h"
#include "config.h"

#if HAS_LOCALE
extern "C" {
#include <locale.h>
}
#endif

static	const char* prog;

static void
usage()
{
    fprintf(stderr, _("Usage: %s"
	" [-1]"
	" [-2]"
	" [-B]"
	" [-c]"
	" [-D]"
	" [-f fontname]"
	" [-F fontdir(s)]"
	" [-m N]"
	" [-o #]"
	" [-p #]"
	" [-r]"
	" [-U]"
	" [-Ml=#,r=#,t=#,b=#]"
	" [-V #]"
	" files... >out.ps\n"), prog);
    fprintf(stderr, _("Default options:"
	" -f Courier"
	" -1"
	" -p 11bp"
	" -o 0"
	"\n"));
    exit(1);
}

int
main(int argc, char* argv[])
{
    extern int optind;
    extern char* optarg;
    int c;

#ifdef LC_CTYPE
    setlocale(LC_CTYPE, "");			// for <ctype.h> calls
#endif
#ifdef LC_TIME
    setlocale(LC_TIME, "");			// for strftime calls
#endif

    TextFormat fmt;

    fmt.resetConfig();
    fmt.readConfig(FAX_SYSCONF);
    fmt.readConfig(FAX_USERCONF);

    prog = argv[0];
    while ((c = Sys::getopt(argc, argv, "f:F:m:M:o:O:p:s:V:12BcDGrRU")) != -1)
	switch(c) {
	case '1':		// 1-column output
	case '2':		// 2-column output
	    fmt.setNumberOfColumns(c - '0');
	    break;
	case 'B':
	    fmt.setPageHeaders(false);
	    break;
	case 'c':		// clip/cut instead of wrapping lines
	    fmt.setLineWrapping(false);
	    break;
	case 'D':		// don't use ISO 8859-1 encoding
	    fmt.setISO8859(false);
	    break;
	case 'f':		// body font
	    if (!fmt.setTextFont(optarg)) {
		fprintf(stderr,
		    _("%s: No font metric information found for \"%s\".\n"),
		    prog, optarg);
		usage();
	    }
	    break;
	case 'F':		// set Font path
	    fmt.setFontPath(optarg);
	    break;
	case 'G':		// gaudy mode
	    fmt.setGaudyHeaders(true);
	    break;
	case 'm':		// multi-column output
	    fmt.setNumberOfColumns(atoi(optarg));
	    break;
	case 'M':		// margin(s)
	    if (!fmt.setPageMargins(optarg)) {
		fprintf(stderr, _("Bad margin syntax.\n"));
		usage();
	    }
	    break;
	case 'o':		// outline columns
	    fmt.setOutlineMargin(TextFormat::inch(optarg));
	    break;
	case 'O':
	    fmt.readConfigItem(optarg);
	    break;
	case 'p':		// text point size
	    fmt.setTextPointSize(TextFormat::inch(optarg));
	    break;
	case 'r':		// rotate page (landscape)
	    fmt.setPageOrientation(TextFormat::LANDSCAPE);
	    break;
	case 'R':		// don't rotate page (portrait)
	    fmt.setPageOrientation(TextFormat::PORTRAIT);
	    break;
	case 's':		// page size
	    if (!fmt.setPageSize(optarg)) {
		fprintf(stderr, _("Unknown page size %s.\n"), optarg);
		usage();
	    }
	    break;
	case 'U':		// reverse page collation
	    fmt.setPageCollation(TextFormat::REVERSE);
	    break;
	case 'V':		// vertical line height+spacing
	    fmt.setTextLineHeight(TextFormat::inch(optarg));
	    break;
	default:
	    fprintf(stderr, _("Unrecognized option \"%c\".\n"), c);
	    usage();
	}
#ifdef notdef
    fmt.setTitle();
#endif
    fmt.beginFormatting(stdout);
    if (optind < argc) {
	for (; optind < argc; optind++) {
	    struct stat sb;
	    if (Sys::stat(argv[optind], sb) >= 0)
		fmt.setModTimeAndDate((time_t) sb.st_mtime);
	    fmt.formatFile(argv[optind]);
	}
    } else {
	fmt.setFilename("<stdin>");
	fmt.setModTimeAndDate(Sys::now());
	fmt.formatFile(stdin);
    }
    fmt.endFormatting();
    return (0);
}
