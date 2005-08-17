/*	$Id$ */
/*
 * Copyright (c) 1995-1996 Sam Leffler
 * Copyright (c) 1995-1996 Silicon Graphics, Inc.
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
#include "Sys.h"
#include "Str.h"
#include "tiffio.h"

static	const char* appName;

extern void fxFatal(const char* fmt, ...);

static void
usage(void)
{
    fxFatal("usage: %s"
	" [-r vertical-res]"
	" [-l page-length]"
	" [-w page-width]"
	" [-1] [-2] [-3]"
	" input.tif"
	, appName
	);
}

#define	OK		0
#define	REJECT		0x1
#define	REFORMAT	0x2
#define	RESIZE		0x4
#define	REVRES		0x8
#define	REIMAGE		0x10

static	uint32 dataFormat = 0;
static  bool useMMR = 0;
static	uint32 pageLength = 297;
static	uint32 pageWidth = 1728;
static	u_long vres = 98;
static	bool useUnlimitedLength = false;

static	int checkPageFormat(TIFF* tif, fxStr& emsg);

int
main(int argc, char* argv[])
{
    int c;

    appName = argv[0];
    while ((c = getopt(argc, argv, "r:l:w:U123")) != -1)
	switch (c) {
	case '1':
	    dataFormat &= ~GROUP3OPT_2DENCODING;
	    break;
	case '2':
	    dataFormat |= GROUP3OPT_2DENCODING;
	    break;
	case '3':
	    useMMR = 1;
	    break;
	case 'U':
	    useUnlimitedLength = true;
	    break;
	case 'l':
	    pageLength = (uint32) strtoul(optarg, NULL, 0);
	    break;
	case 'r':
	    vres = strtoul(optarg, NULL, 0);
	    break;
	case 'w':
	    pageWidth = (uint32) strtoul(optarg, NULL, 0);
	    break;
	case '?':
	    usage();
	    /*NOTREACHED*/
	}
    if (argc - optind != 1)
	usage();
    int status;
    fxStr emsg;
    TIFF* tif = TIFFOpen(argv[optind], "r");

    /*
     * Suppress libtiff warning messages from becoming fatal.
     */
    TIFFSetWarningHandler(NULL);

    if (tif) {
	status = OK;
	do {
	    status |= checkPageFormat(tif, emsg);
	} while ((status & REJECT) == 0 && TIFFReadDirectory(tif));
	TIFFClose(tif);
    } else {
	struct stat sb;
	if (Sys::stat(argv[optind], sb) < 0)
	    emsg = "Document file is unreadable or does not exist";
	else
	    emsg = "Document is not valid TIFF (unspecified format error)";
	status = REJECT;
    }
    if (status != OK) {
	const char* sep = "";
	if (status & REJECT)
	    printf("%sREJECT", sep), sep = "+";
	if (status & REFORMAT)
	    printf("%sREFORMAT", sep), sep = "+";
	if (status & RESIZE)
	    printf("%sRESIZE", sep), sep = "+";
	if (status & REVRES)
	    printf("%sREVRES", sep), sep = "+";
	if (status & REIMAGE)
	    printf("%sREIMAGE", sep), sep = "+";
	printf("\n%s", (const char*) emsg);
    } else
	printf("OK\n");
    return (0);
}

/*
 * Check the format of a page against the capabilities
 * of the modem (or the default capabilities if no modem
 * is currently setup).
 */
static int
checkPageFormat(TIFF* tif, fxStr& emsg)
{
    int status = OK;

    uint16 bps;
    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
    if (bps != 1) {
	emsg.append(fxStr::format(
	    "Document is not a bilevel image (bits/sample %u).\n", bps));
	status |= REIMAGE;
    }
    uint16 spp;
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
    if (spp != 1) {
	emsg.append(fxStr::format(
	    "Document is a multi-sample image (samples/pixel %u).\n", spp));
	status |= REIMAGE;
    }
    uint16 compression = 0;
    (void) TIFFGetField(tif, TIFFTAG_COMPRESSION, &compression);
    if (useMMR) {
	if (compression != COMPRESSION_CCITTFAX4) {
	    emsg.append("Document requires reformatting, not in Group 4 format.\n");
	    status |= REFORMAT;
	}
    } else {
	if (compression != COMPRESSION_CCITTFAX3) {
	    emsg.append("Document requires reformatting, not in Group 3 format.\n");
	    status |= REFORMAT;
	}
	uint32 g3opts = 0;
	(void) TIFFGetField(tif, TIFFTAG_GROUP3OPTIONS, &g3opts);
	if ((g3opts ^ dataFormat) & GROUP3OPT_2DENCODING) {
	    emsg.append("Document requires reformatting, not in 2DMR format.\n");
	    status |= REFORMAT;
	}
    }
    /*
     * FaxSend can handle multistrip MH and MR images (not MMR),
     * but not if the strips are not in sequential order.
     */
    if (TIFFNumberOfStrips(tif) != 1) {
	emsg.append("Document should be reformatted as a single strip.\n");
	status |= REFORMAT;
    }
#ifdef notdef
    /*
     * The server can handle images that have an MSB2LSB fill order.
     */
    uint16 fill;
    (void) TIFFGetFieldDefaulted(tif, TIFFTAG_FILLORDER, &fill);
    if (fill != FILLORDER_LSB2MSB) {
	emsg.append("Document should be reformatted with "
	    "LSB-to-MSB bit order.\n");
	status |= REFORMAT;
    }
#endif
    /*
     * Try to deduce the vertical resolution of the image
     * image.  This can be problematical for arbitrary TIFF
     * images because vendors sometimes do not give the units.
     * We, however, can depend on the info in images that
     * we generate because we are careful to include valid info.
     */
    float yres, yresinch = .0F;
    if (TIFFGetField(tif, TIFFTAG_YRESOLUTION, &yres)) {
	short resunit = RESUNIT_INCH;		// TIFF spec default
	(void) TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &resunit);
	if (resunit == RESUNIT_INCH) {
	    yresinch = yres;
	    yres /= 25.4;
	} else
	if (resunit == RESUNIT_CENTIMETER) {
	    yresinch = yres * 25.4;
	    yres /= 10;
	} else
	if (resunit == RESUNIT_NONE) {		// postscript units ?
	    yresinch = yres * 720.0;
	    yres /= 28.35;
	}
    } else {
	/*
	 * No vertical resolution is specified, try
	 * to deduce one from the image length.
	 */
	uint32 l;
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &l);
	yres = (l < 1450 ? 98 : 196);		// B4 at 98 lpi is ~1400 lines
    }
    // vres is in inches, compare inches to inches, allow 15 lpi variation
    if ((u_long) yresinch > vres ? (u_long) yresinch - vres > 15 : vres - (u_long) yresinch > 15) {
	emsg.append(fxStr::format("Document requires reformatting to adjust"
	    " vertical resolution (convert to %lu, document is %lu).\n",
	    vres, (u_long) yresinch));
	status |= REVRES;
    }

    /*
     * Select page width according to the image width
     * and vertical resolution.
     */
    uint32 w;
    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w)) {
	emsg = "Document is not valid TIFF (missing ImageWidth tag).\n";
	return (REJECT);
    }
    if (w != pageWidth) {
	emsg.append(fxStr::format("Document requires resizing to adjust"
	    " page width (convert to %lu, document is %lu).\n",
	    (u_long) pageWidth, (u_long) w));
	status |= RESIZE;
    }

    /*
     * Select page length according to the image size and
     * vertical resolution.  Note that if the resolution
     * info is bogus, we may select the wrong page size.
     * Note also that we're a bit lenient in places here
     * to take into account sloppy coding practice (e.g.
     * using 200 lpi for high-res facsimile.)
     */
    uint32 h = 0;
    if (!TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h)) {
	emsg = "Document is not valid TIFF (missing ImageLength tag).\n";
	return (REJECT);
    }
    /*
     * Ignore length check when unlimited length is used.
     */
    if (!useUnlimitedLength) {
	float len = h / (yres == 0 ? 1. : yres);		// page length in mm
	if (pageLength != (uint32) -1 && len > pageLength+30) {
	    emsg.append(fxStr::format("Document requires resizing to adjust"
		" page length (convert to %lu, document is %lu).\n",
		(u_long) pageLength, (u_long) len));
	    status |= RESIZE;
	}
    }
    return (status);
}
