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
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "tiffio.h"

#include "PageSize.h"
#include "Class2Params.h"

#include "port.h"

extern	const char* fmtTime(time_t t);

static bool
isFAXImage(TIFF* tif)
{
    uint16 w;
    if (TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &w) && w != 1)
	return (false);
    if (TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &w) && w != 1)
	return (false);
    if (!TIFFGetField(tif, TIFFTAG_COMPRESSION, &w) ||
      (w != COMPRESSION_CCITTFAX3 && w != COMPRESSION_CCITTFAX4))
	return (false);
    if (!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &w) ||
      (w != PHOTOMETRIC_MINISWHITE && w != PHOTOMETRIC_MINISBLACK))
	return (false);
    return (true);
}

static void
sanitize(fxStr& s)
{
    for(u_int i = 0; i < s.length(); i++) {
        if (!isascii(s[i]) || !isprint(s[i])) s[i] = '?';
    }
}

int
main(int argc, char** argv)
{
    bool showFilename = true;
    const char* appName = argv[0];

    if (argc > 2 && streq(argv[1], "-n")) {
	showFilename = false;
	argc--, argv++;
    }
    if (argc != 2) {
	fprintf(stderr, "usage: %s [-n] file.tif\n", appName);
	return (-1);
    }
    if (showFilename)
	printf("%s:\n", argv[1]);
    TIFFSetErrorHandler(NULL);
    TIFFSetWarningHandler(NULL);
    TIFF* tif = TIFFOpen(argv[1], "r");
    if (tif == NULL) {
	printf("Could not open %s; either not TIFF or corrupted.\n", argv[1]);
	return (0);
    }
    bool ok = isFAXImage(tif);
    if (!ok) {
	printf("Does not look like a facsimile?\n");
	return (0);
    }

    Class2Params params;
    uint32 v;
#ifdef TIFFTAG_FAXRECVPARAMS
    if (TIFFGetField(tif, TIFFTAG_FAXRECVPARAMS, &v))
	params.decode((u_int) v);			// page transfer params
    else {
#endif
    float vres = 3.85;					// XXX default
    if (TIFFGetField(tif, TIFFTAG_YRESOLUTION, &vres)) {
	uint16 resunit = RESUNIT_INCH;			// TIFF spec default
	TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &resunit);
	if (resunit == RESUNIT_INCH)
	    vres /= 25.4;
	if (resunit == RESUNIT_NONE)
	    vres /= 720.0;				// postscript units ?
    }
    params.setVerticalRes((u_int) vres);		// resolution
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &v);
    params.setPageWidthInPixels((u_int) v);		// page width
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &v);
    params.setPageLengthInMM((u_int)(v / vres));	// page length
#ifdef TIFFTAG_FAXRECVPARAMS
    }
#endif
    fxStr sender;
    char* cp;
    if (TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &cp)) {
	sender = cp;
	sanitize(sender);
    } else
	sender = "<unknown>";
    printf("%11s %s\n", "Sender:", (const char*) sender);
#ifdef TIFFTAG_FAXSUBADDRESS
    if (TIFFGetField(tif, TIFFTAG_FAXSUBADDRESS, &cp)) {
	fxStr subaddr(cp);
	sanitize(subaddr);
	printf("%11s %s\n", "SubAddr:", (const char*) subaddr);
    }
#endif
    fxStr date;
    if (TIFFGetField(tif, TIFFTAG_DATETIME, &cp)) {	// time received
	date = cp;
	sanitize(date);
    } else {
	struct stat sb;
	fstat(TIFFFileno(tif), &sb);
	char buf[80];
	strftime(buf, sizeof (buf),
	    "%Y:%m:%d %H:%M:%S %Z", localtime(&sb.st_mtime));
	date = buf;
    }
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &v);
    float h = v / (params.verticalRes() < 100 ? 3.85 : 7.7);
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &v);
    float w = (v / 204.) * 25.4;
    time_t time = 0;
    u_int npages = 0;					// page count
    do {
	npages++;
#ifdef TIFFTAG_FAXRECVTIME
	if (TIFFGetField(tif, TIFFTAG_FAXRECVTIME, &v))
	    time += v;
#endif
    } while (TIFFReadDirectory(tif));
    TIFFClose(tif);

    printf("%11s %u\n", "Pages:", npages);
    if (params.verticalRes() == 98)
	printf("%11s Normal\n", "Quality:");
    else if (params.verticalRes() == 196)
	printf("%11s Fine\n", "Quality:");
    else
	printf("%11s %u lines/inch\n", "Quality:", params.verticalRes());
    PageSizeInfo* info = PageSizeInfo::getPageSizeBySize(w, h);
    if (info)
	printf("%11s %s\n", "Page:", info->name());
    else
	printf("%11s %u by %u\n", "Page:", params.pageWidth(), (u_int) h);
    delete info;
    printf("%11s %s\n", "Received:", (const char*) date);
    printf("%11s %s\n", "TimeToRecv:", time == 0 ? "<unknown>" : fmtTime(time));
    printf("%11s %s\n", "SignalRate:", params.bitRateName());
    printf("%11s %s\n", "DataFormat:", params.dataFormatName());
    return (0);
}
