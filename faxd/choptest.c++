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
#include "G3Decoder.h"
#include "Class2Params.h"
#include "tiffio.h"

class MemoryDecoder : public G3Decoder {
private:
    u_long	cc;
    const u_char* bp;
    const u_char* endOfPage;
    u_int nblanks;

    int decodeNextByte();
public:
    MemoryDecoder(const u_char* data, u_long cc);
    ~MemoryDecoder();
    const u_char* current()				{ return bp; }

    void scanPage(u_int fillorder, const Class2Params& params);

    const u_char* getEndOfPage()			{ return endOfPage; }
    u_int getLastBlanks()				{ return nblanks; }
};
MemoryDecoder::MemoryDecoder(const u_char* data, u_long n)
{
    bp = data;
    cc = n;
    endOfPage = NULL;
    nblanks = 0;
}
MemoryDecoder::~MemoryDecoder()				{}

int
MemoryDecoder::decodeNextByte()
{
    if (cc == 0)
	raiseRTC();			// XXX don't need to recognize EOF
    cc--;
    return (*bp++);
}

static bool
isBlank(uint16* runs, u_int rowpixels)
{
    u_int x = 0;
    for (;;) {
	if ((x += *runs++) >= rowpixels)
	    break;
	if (runs[0] != 0)
	    return (false);
	if ((x += *runs++) >= rowpixels)
	    break;
    }
    return (true);
}

void
MemoryDecoder::scanPage(u_int fillorder, const Class2Params& params)
{
    setupDecoder(fillorder,  params.is2D());
    u_int rowpixels = params.pageWidth();	// NB: assume rowpixels <= 2432
    uint16 runs[2*2432];			// run arrays for cur+ref rows
    setRuns(runs, runs+2432, rowpixels);

    if (!RTCraised()) {
	/*
	 * Skip a 1" margin at the top of the page before
	 * scanning for trailing white space.  We do this
	 * to insure that there is always enough space on
	 * the page to image a tag line and to satisfy a
	 * fax machine that is incapable of imaging to the
	 * full extent of the page.
	 */
	u_int topMargin = 1*98;			// 1" at 98 lpi
	if (params.vr == VR_FINE)		// 196 lpi =>'s twice as many
	    topMargin *= 2;
	do {
	    (void) decodeRow(NULL, rowpixels);
	} while (--topMargin);
	/*
	 * Scan the remainder of the page data and calculate
	 * the number of blank lines at the bottom.
	 */
	for (;;) {
	    (void) decodeRow(NULL, rowpixels);
	    if (isBlank(lastRuns(), rowpixels)) {
		endOfPage = bp;			// include one blank row
		nblanks = 0;
		do {
		    nblanks++;
		    (void) decodeRow(NULL, rowpixels);
		} while (isBlank(lastRuns(), rowpixels));
	    }
	}
    }
}

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
    while ((c = getopt(argc, argv, "t:a")) != -1)
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
    if (comp != COMPRESSION_CCITTFAX3)
	fatal("%s: Not a Group 3-encoded TIFF file", argv[optind]);

    Class2Params params;
    params.vr = VR_NORMAL;
    params.wd = WD_1728;
    params.ln = LN_INF;
    params.df = DF_1DMR;

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
	    params.df = (opts & GROUP3OPT_2DENCODING) ? DF_2DMR : DF_1DMR;

	    uint16 fillorder;
	    TIFFGetFieldDefaulted(tif, TIFFTAG_FILLORDER, &fillorder);
	    uint32* stripbytecount;
	    (void) TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &stripbytecount);

	    u_int totbytes = (u_int) stripbytecount[0];
	    if (totbytes > 0) {
		u_char* data = new u_char[totbytes];
		if (TIFFReadRawStrip(tif, 0, data, totbytes) >= 0) {
		    MemoryDecoder dec(data, totbytes);
		    dec.scanPage(fillorder, params);
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
