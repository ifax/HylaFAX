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
 * Program for testing page chopping support.
 *
 * Usage: choptest input.tif
 */
#include <unistd.h>
#include "Class2.h"
#include "MemoryDecoder.h"
#include "tiffio.h"
#include "Sys.h"

const char* appName;

void
usage()
{
    fprintf(stderr, "usage: %s [-a] [-t threshold] input.tif\n", appName);
    exit(-1);
}

void
fatal(const char* fmt ...)
{
    fprintf(stderr, "%s: ", appName);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs(".\n", stderr);
    exit(-1);
}

int
main(int argc, char* argv[])
{
    extern int optind, opterr;
    extern char* optarg;
    float minChop = 3.0;		// chop if >= 3" white space at bottom
    u_int minRows;
    bool doAll = false;
    int c;

    appName = argv[0];
    while ((c = Sys::getopt(argc, argv, "t:a")) != -1)
	switch (c) {
	case 'a':
	    doAll = true;
	    break;
	case 't':
	    minChop = atof(optarg);
	    break;
	case '?':
	    usage();
	    /*NOTREACHED*/
	}
    if (argc - optind != 1)
	usage();
    TIFF* tif = TIFFOpen(argv[optind], "r");
    if (!tif)
	fatal("%s: Cannot open, or not a TIFF file", argv[optind]);
    uint16 comp;
    TIFFGetField(tif, TIFFTAG_COMPRESSION, &comp);
    if (comp != COMPRESSION_CCITTFAX3 && comp != COMPRESSION_CCITTFAX4)
	fatal("%s: Not a Group 3 or Group 4-encoded TIFF file", argv[optind]);

    Class2Params params;
    params.vr = VR_NORMAL;
    params.wd = WD_A4;
    params.ln = LN_INF;
    params.df = DF_1DMH;

    printf("Chop %s >=%.2g\" of white space at the bottom.\n"
	, doAll ? "all pages with" : "last page if"
	, minChop
    );

    do {
	if (doAll || TIFFLastDirectory(tif)) {
	    uint32 l;
	    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &l);
	    if (l < 1500) {
		params.vr = VR_NORMAL;
		minRows = (u_int)(98*minChop);
	    } else {
		params.vr = VR_FINE;
		minRows = (u_int)(196*minChop);
	    }

	    uint32 opts = 0;
	    TIFFGetField(tif, TIFFTAG_GROUP3OPTIONS, &opts);
	    params.df = (opts & GROUP3OPT_2DENCODING) ? DF_2DMR : DF_1DMH;

	    uint16 fillorder;
	    TIFFGetFieldDefaulted(tif, TIFFTAG_FILLORDER, &fillorder);
	    uint32* stripbytecount;
	    (void) TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &stripbytecount);

	    u_int totbytes = (u_int) stripbytecount[0];
	    if (totbytes > 0) {
		u_char* data = new u_char[totbytes];
		if (TIFFReadRawStrip(tif, 0, data, totbytes) >= 0) {
		    MemoryDecoder dec(data, totbytes);
		    dec.scanPageForBlanks(fillorder, params);
		    if (dec.getLastBlanks() > minRows) {
			printf(
			    "Chop %u rows, strip was %lu bytes, need only %lu\n"
			    , dec.getLastBlanks()
			    , totbytes
			    , dec.getEndOfPage() - data
			);
		    } else {
			printf("Don't chop, found %u rows, need %u rows\n"
			    , dec.getLastBlanks()
			    , minRows
			);
		    }
		}
		delete data;
	    }
	}
    } while (TIFFReadDirectory(tif));
    return (0);
}
