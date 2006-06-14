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
#include "CallID.h"
#include "Sys.h"

#include "port.h"

extern	const char* fmtTime(time_t t);

/*
 * This tests whether the tif file is a "fax" image.
 * Traditional fax images are MH, MR, MMR, but some can
 * be JPEG, JBIG, and possibly others.  So we use
 * TIFFTAG_FAXRECVPARAMS as a "fax" image identifier,
 * and if it's not there, then we resort to traditional
 * tactics.
 */
static bool
isFAXImage(TIFF* tif)
{
#ifdef TIFFTAG_FAXRECVPARAMS
    uint32 v;
    if (TIFFGetField(tif, TIFFTAG_FAXRECVPARAMS, &v) && v != 0)
	return (true);
#endif
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

static void
usage (const char* app)
{
    printf("usage: %s [-n] [-S fmt] [-s fmt] [-e fmt] [-E fmt]\n\n", app);
}

static const char*
escapedString (const char*src)
{
    char* res;
    int len;
    len = strlen(src);
    res = (char*)malloc(len);
    if (res)
    {
	char* dst = res;
	for (int i = 0; i < len; i++)
	{
	    if (src[i] == '\\')
	    {
		switch (src[++i])
		{
		    case 'n':	*dst++ = '\n';	break;
		    case 'r':	*dst++ = '\r';	break;
		    case 't':	*dst++ = '\t';	break;
		    default:
			*dst++ = src[i];
		}
	    } else
	    {
		*dst++ = src[i];
	    }
	}
    }
    return res;
}

static const char* faxStart = "";
static const char* fieldStart = "%10s: ";
static const char* fieldEnd = "\n";
static const char* faxEnd = "";

static void 
printStart (const char* filename)
{
    printf(faxStart, filename);
}

static void
printField (const char* val_fmt, ...)
{
    char fmt[256];
    snprintf(fmt, sizeof(fmt), "%s%s%s", fieldStart, val_fmt, fieldEnd);
    va_list ap;
    va_start(ap,val_fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static void
printEnd (const char* filename)
{
    printf(faxEnd, filename);
}

int
main(int argc, char** argv)
{
    const char* appName = argv[0];
    int c;

    while ((c = getopt(argc, argv, "nS:s:e:E:")) != -1)
	switch (c) {
	    case '?':
	    	usage(appName);
		return 0;
	    case 'n':
		faxStart = "%s:\n";
		break;
	    case 'S':
		faxStart = escapedString(optarg);
	    	break;
	    case 's':
		fieldStart = escapedString(optarg);
	    	break;
	    case 'e':
		fieldEnd = escapedString(optarg);
	    	break;
	    case 'E':
		faxEnd = escapedString(optarg);
	    	break;
	}

    while (optind < argc) {
	printStart(argv[optind]);
	TIFFSetErrorHandler(NULL);
	TIFFSetWarningHandler(NULL);
	TIFF* tif = TIFFOpen(argv[optind], "r");
	if (tif == NULL) {
	    printf("Could not open %s; either not TIFF or corrupted.\n",
		    argv[optind]);
	    return (0);
	}
	bool ok = isFAXImage(tif);
	if (!ok) {
	    printf("Does not look like a facsimile?\n");
	    return (0);
	}

	Class2Params params;
	uint32 v;
	float vres = 3.85;					// XXX default
	float hres = 8.03;
#ifdef TIFFTAG_FAXRECVPARAMS
	if (TIFFGetField(tif, TIFFTAG_FAXRECVPARAMS, &v)) {
	    params.decode((u_int) v);			// page transfer params
	    // inch & metric resolutions overlap and are distinguished by yres
	    TIFFGetField(tif, TIFFTAG_YRESOLUTION, &vres);
	    switch ((u_int) vres) {
		case 100:
		    params.vr = VR_200X100;
		    break;
		case 200:
		    params.vr = VR_200X200;
		    break;
		case 400:
		    params.vr = VR_200X400;
		    break;
		case 300:
		    params.vr = VR_300X300;
		    break;
	    }
	} else {
#endif
	if (TIFFGetField(tif, TIFFTAG_YRESOLUTION, &vres)) {
	    uint16 resunit = RESUNIT_INCH;			// TIFF spec default
	    TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &resunit);
	    if (resunit == RESUNIT_INCH)
		vres /= 25.4;
	    if (resunit == RESUNIT_NONE)
		vres /= 720.0;				// postscript units ?
	    if (TIFFGetField(tif, TIFFTAG_XRESOLUTION, &hres)) {
		if (resunit == RESUNIT_INCH)
		    hres /= 25.4;
		if (resunit == RESUNIT_NONE)
		    hres /= 720.0;				// postscript units ?
	    }
	}
	params.setRes((u_int) hres, (u_int) vres);		// resolution
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &v);
	params.setPageWidthInPixels((u_int) v);		// page width
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &v);
	params.setPageLengthInMM((u_int)(v / vres));	// page length
#ifdef TIFFTAG_FAXRECVPARAMS
	}
#endif
	char* cp;
#ifdef TIFFTAG_FAXDCS
	if (TIFFGetField(tif, TIFFTAG_FAXDCS, &cp) && strncmp(cp, "00 00 00", 8) != 0) {
	    // cannot trust br from faxdcs as V.34-Fax does not provide it there
	    u_int brhold = params.br;
	    fxStr faxdcs(cp);
	    sanitize(faxdcs);
	    params.asciiDecode((const char*) faxdcs);	// params per Table 2/T.30
	    params.setFromDCS(params);
	    params.br = brhold;
	}
#endif
	fxStr sender = "";
	CallID callid;
	if (TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &cp)) {
	    while (cp[0] != '\0' && cp[0] != '\n') {	// sender
		sender.append(cp[0]);
		cp++;
	    }
	    sanitize(sender);
	    u_int i = 0;
	    while (cp[0] == '\n') {
		cp++;
		callid.resize(i+1);
		while (cp[0] != '\0' && cp[0] != '\n') {
		    callid[i].append(cp[0]);
		    cp++;
		}
		sanitize(callid[i]);
		i++;
	    }
	} else
	    sender = "<unknown>";
	printField("%s", "Sender", (const char*) sender);
#ifdef TIFFTAG_FAXSUBADDRESS
	if (TIFFGetField(tif, TIFFTAG_FAXSUBADDRESS, &cp)) {
	    fxStr subaddr(cp);
	    sanitize(subaddr);
	    printField("%s", "SubAddr", (const char*) subaddr);
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
	float h = v / (params.vr == VR_NORMAL ? 3.85 : 
	    params.vr == VR_200X100 ? 3.94 : 
	    params.vr == VR_FINE ? 7.7 :
	    params.vr == VR_200X200 ? 7.87 : 
	    params.vr == VR_R8 ? 15.4 : 
	    params.vr == VR_200X400 ? 15.75 : 
	    params.vr == VR_300X300 ? 11.81 : 15.4);
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &v);
	float w = v / (params.vr == VR_NORMAL ? 8.0 : 
	    params.vr == VR_200X100 ? 8.00 : 
	    params.vr == VR_FINE ? 8.00 :
	    params.vr == VR_200X200 ? 8.00 : 
	    params.vr == VR_R8 ? 8.00 : 
	    params.vr == VR_200X400 ? 8.00 : 
	    params.vr == VR_300X300 ? 12.01 : 16.01);
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

	printField("%u", "Pages", npages);
	if (params.vr == VR_NORMAL)
	    printField("Normal", "Quality");
	else if (params.vr == VR_FINE)
	    printField("Fine", "Quality");
	else if (params.vr == VR_R8)
	    printField("Superfine", "Quality");
	else if (params.vr == VR_R16)
	    printField("Hyperfine", "Quality");
	else
	    printField("%u lines/inch", "Quality", params.verticalRes());
	PageSizeInfo* info = PageSizeInfo::getPageSizeBySize(w, h);
	if (info)
	    printField("%s", "Page", info->name());
	else
	    printField("%u by %u", "Page", params.pageWidth(), (u_int) h);
	delete info;
	printField("%s", "Received", (const char*) date);
	printField("%s", "TimeToRecv", time == 0 ? "<unknown>" : fmtTime(time));
	printField("%s", "SignalRate", params.bitRateName());
	printField("%s", "DataFormat", params.dataFormatName());
	printField("%s", "ErrCorrect", params.ec == EC_DISABLE ? "No" : "Yes");
	for (u_int i = 0; i < callid.size(); i++) {
	    // formatting will mess up if i gets bigger than one digit
	    printf("%9s%u: %s", "CallID", i+1, (const char*) callid.id(i));
	}
	printEnd(argv[optind]);
	optind++;
    }
    return (0);
}
