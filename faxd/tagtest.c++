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
 * Program for testing tag line imaging support.
 *
 * Usage: tagtest [-f fontfile] [-m format] [-o output.tif] input.tif
 */
#include "Sys.h"

#include "PCFFont.h"
#include "StackBuffer.h"
#include "FaxFont.h"
#include "tiffio.h"
#include "Class2Params.h"
#if HAS_LOCALE
extern "C" {
#include <locale.h>
}
#endif

u_int	tagLineSlop;
FaxFont* tagLineFont;
u_int	pageNumber = 1;
u_int	totalPages;
fxStr	tagLineFmt("From %%n|%c|Page %%p of %%t");
fxStr	tagLineFontFile("fixed.pcf");
fxStr	jobid("9733");
fxStr	jobtag("sendq/q9733");
fxStr	localid("Sam's Bar&Grill");
fxStr	modemnumber("+15105268781");
fxStr	mailaddr("sam@flake.asd.sgi.com");
fxStr	external("+14159657824");
fxStr	sender("Sam Leffler");
fxStr	tagLine;
u_int	tagLineFields;

static void
insert(fxStr& tag, u_int l, const fxStr& s)
{
    tag.remove(l,2);
    tag.insert(s, l);
}

/*
 * Read in the PCF font to use for imaging the tag line and
 * preformat as much of the tag line as possible.
 */
void
setupTagLine()
{
    if (tagLineFont == NULL)
	tagLineFont = new PCFFont;
    if (!tagLineFont->isReady() && tagLineFontFile != "")
	(void) tagLineFont->read(tagLineFontFile);

    time_t t = Sys::now();
    tm* tm = localtime(&t);
    char line[1024];
    strftime(line, sizeof (line), tagLineFmt, tm);
    tagLine = line;
    u_int l = 0;
    while (l < tagLine.length()) {
	l = tagLine.next(l, '%');
	if (l >= tagLine.length()-1)
	    break;
	switch (tagLine[l+1]) {
	case 'd': insert(tagLine, l, external); break;
	case 'i': insert(tagLine, l, jobid); break;
	case 'j': insert(tagLine, l, jobtag); break;
	case 'l': insert(tagLine, l, localid); break;
	case 'm': insert(tagLine, l, mailaddr); break;
	case 'n': insert(tagLine, l, modemnumber); break;
	case 's': insert(tagLine, l, sender); break;
	/* for the purpose of tagtest %%T is the same of %%t */
	case 't':   
	case 'T': insert(tagLine, l, fxStr((int) totalPages, "%u")); break;
	default:  l += 2; break;
	}
    }
    /*
     * Break the tag into fields.
     */
    tagLineFields = 0;
    for (l = 0; l < tagLine.length(); l = tagLine.next(l+1, '|'))
	tagLineFields++;
}

#define	MARGIN_TOP	2
#define	MARGIN_BOT	2
#define	MARGIN_LEFT	2
#define	MARGIN_RIGHT	2
#define SLOP_LINES	3

bool
setupTagLineSlop(const Class2Params& params)
{
    if (tagLineFont->isReady()) {
	tagLineSlop = (tagLineFont->fontHeight()+MARGIN_TOP+MARGIN_BOT+SLOP_LINES) * 
	    howmany(params.pageWidth(),8);
	return (true);
    } else
	return (false);
}

#include "MemoryDecoder.h"

/*
 * Image the tag line in place of the top few lines of the page
 * data and return the encoded tag line at the front of the
 * data buffer.  The buffer that holds the page data is assumed
 * to have tagLineSlop extra space allocated in front of the
 * page data.  The tag line format string is assumed to be
 * preprocessed by setupTagLine above so that we only need to
 * setup the current page number.
 */
u_char*
imageTagLine(u_char* buf, u_int fillorder, const Class2Params& params, u_long& totdata)
{
    u_int l;
    /*
     * Fill in any per-page variables used in the tag line.
     */
    fxStr tag = tagLine;
    l = 0;
    while (l < tag.length()) {
	l = tag.next(l, '%');
	if (l >= tag.length()-1)
	    break;
	/* for the purpose of tagtest %%P is the same of %%p */
	if (tag[l+1] == 'p' || tag[l+1] == 'P')
	    insert(tag, l, fxStr((int) pageNumber, "%d"));
	else
	    l += 2;
    }
    /*
     * Setup the raster in which the tag line is imaged.
     *
     * The font size information received from the font functions
     * are suitable for VR_FINE.  Thus VR_FINE is used as the reference
     * resolution, and all other resolutions must be scaled.
     */
    u_int w = params.pageWidth();
    u_int h = (tagLineFont->fontHeight()*2)+MARGIN_TOP+MARGIN_BOT;	// max height - double VR_FINE
    u_int th = 0;							// actual tagline height
    switch(params.vr) {
	case VR_NORMAL:
	case VR_200X100:
	    th = (tagLineFont->fontHeight()/2)+MARGIN_TOP+MARGIN_BOT;	// half VR_FINE
	    break;
	case VR_FINE:
	case VR_200X200:
	    th = tagLineFont->fontHeight()+MARGIN_TOP+MARGIN_BOT;	// reference resolution
	    break;
	case VR_R8:
	case VR_R16:
	case VR_200X400:
	case VR_300X300:        // not proportionate but legible
	    th = (tagLineFont->fontHeight()*2)+MARGIN_TOP+MARGIN_BOT;	// double VR_FINE
	    break;
    }
    /*
     * imageText assumes that raster is word-aligned; we use
     * longs here to optimize the scaling done below for the
     * low res case.  This should satisfy the word-alignment.
     */
    u_int lpr = howmany(w,sizeof(u_long)*8);		// longs/raster row
    u_long* raster = new u_long[(h+SLOP_LINES)*lpr];	// decoded raster
    memset(raster,0,(h+SLOP_LINES)*lpr*sizeof (u_long));// clear raster to white
    /*
     * Break the tag into fields and render each piece of
     * text centered in its field.  Experiments indicate
     * that rendering the text over white is better than,
     * say, rendering it over the original page.
     */
    l = 0;
    /*  
     * imageText produces good dimensioned fonts at 1728 pixels/row. At VR_R16
     * and VR_300X300, both being wider than 1728, text appears shrinked
     * horizontally; while VR_300 is still ok, VR_R16 is too small. To help
     * streching the text horizontally we force text imaging to still use
     * 1728 instead of VR_R16's 3456 (3456 / 2 = 1728); text will be
     * imaged the 1st (left) half of the line, it will be stretched after
     * imaging takes place.
     */
    u_int fieldWidth = params.pageWidth() / (params.vr == VR_R16 ? 2 : 1) / tagLineFields;
    for (u_int f = 0; f < tagLineFields; f++) {
	fxStr tagField = tag.token(l, '|');
	u_int fw, fh;
	tagLineFont->strWidth(tagField, fw, fh);
	u_int xoff = f*fieldWidth;
	if (fw < fieldWidth)
	    xoff += (fieldWidth-fw)/2;
	else
	    xoff += MARGIN_LEFT;
	(void) tagLineFont->imageText(tagField, (u_short*) raster, w, h,
	    xoff, MARGIN_RIGHT, MARGIN_TOP, MARGIN_BOT);
    }

    /*
     * Scale image data as needed (see notes above).
     */

    if (params.vr == VR_NORMAL || params.vr == VR_200X100) {
	/*
	 * These resolutions require vertical "shrinking" of the
	 * tagline image.  We make 1 line out of 2.
	 * (Note the ``or'' used to generate the final samples.)
	 *
	 * Details:  
	 * - image is in lines 1 through y
	 * - available lines are 1 through y/2
	 * - start at the top of the image
	 * - line 1 is ORed with line 2 to get new line 1
	 * - line 3 is ORed with line 4 to get new line 2
	 * - ...
	 * - line y is ORed with line y+1 to get new line (y+1)/2
	 */
	u_long* l1 = raster+MARGIN_TOP*lpr;
	u_long* l2 = l1+lpr;
	u_long* l3 = raster+MARGIN_TOP*lpr;
	for (u_int nr = th-(MARGIN_TOP+MARGIN_BOT); nr; nr--) {
	    for (u_int nl = lpr; nl; nl--)
		*l3++ = *l1++ | *l2++;
	    l1 += lpr;
	    l2 += lpr;
	}
	memset(l3, 0, MARGIN_BOT*lpr*sizeof (u_long));
    }
    if (params.vr == VR_R8 || params.vr == VR_R16 || params.vr == VR_200X400 || params.vr == VR_300X300) {
	/*
	 * These resolutions require vertical "stretching" of the
	 * tagline image.  We make 2 lines out of 1.
	 * Go bottom-to-top since the image resides in the top half and the
	 * bottom data can be overwritten since it is unset.
	 * 
	 * Details:
	 * - image is in lines 1 through y/2
	 * - available lines are 1 through y
	 * - we use 2 pointers, 1st starting at line y/2 the other at line y
	 * - line y/2   copied in line y   and y-1
	 * - line y/2-1 copied in line y-2 and y-2-1
	 * - ...
	 */
	// bottom of actual image
	u_long* l1 = raster - 1 + lpr * (MARGIN_TOP + (th-MARGIN_TOP-MARGIN_BOT)/2 + 2);
	// bottom of available image
	u_long* l2 = raster - 1 + lpr * (MARGIN_TOP + (th-MARGIN_TOP-MARGIN_BOT) + 1);

	/* stretch vertically (going backwards, R->L, B->T) */
	for (u_int nr = (th-(MARGIN_TOP+MARGIN_BOT))/2; nr; nr--) {
	    for (u_int nl = lpr; nl; nl--) { 
		*(l2 - lpr) = *l1;	/* y/2 copied into y-1 */
		*l2 = *l1;		/* y/2 copied into y */ 
		l2--;			/* left 1 long */
		l1--;			/* left 1 long */
	    }
	    /* after previous loop, l1 and l2 are up 1 line; l2 needs 1 more */
	    l2 -= lpr;			/* 2nd ptr up 1 line */
	}
	if (params.vr == VR_R16) {
	    /*
	     * hr is twice the hr in which data is imaged.
	     * We need to strech the image horizontally:
	     * 1234567890ABCDEFGHIJ -> 11223344556677889900
	     * (ABCDEFGHIJ is whitespace)
	     */

	    /* Reset ptr to begin of image */
	    l1 = raster + MARGIN_TOP*lpr;               // begin of 1st line
	    l2 = raster + MARGIN_TOP*lpr + lpr - 1;     // end of 1st line
	    for (u_int nr = th-(MARGIN_TOP+MARGIN_BOT); nr; nr--) {
		/*
		 * 0      lpr/2      lpr
		 * |        |         |
		 * 1234567890__________
		 * 1234567890________00  x/2   copied into x   and x-1
		 * 1234567890______9900  x/2-1 copied into x-2 and x-3
		 * ...
		 * 11223344556677889900
		 */
		u_int bpl = sizeof(u_long) * 8;		// bits per u_long
		for (u_int nl = lpr/2 - 1; nl ; nl--) {
		    // make 2 longs out of 1 (ABCD -> AABB CCDD)
		    int pos = 0;
		    for (u_int i = 0; i < (bpl/8); i++) {
			if (i == 0 || i == bpl/8/2) {
			    *l2 = (u_long) 0;
			    pos = bpl - 2;
			}
			// put pairs of bits from l1 into the right places within l2
			*l2 |= (u_long)((*(l1+nl) & (1<<(bpl-8*i-5))) >> (bpl-8*i-5) << pos);
			*l2 |= ((u_long)((*(l1+nl) & (1<<(bpl-8*i-5))) >> (bpl-8*i-5) << pos)) << 1;
			pos -= 2;

			*l2 |= (u_long)((*(l1+nl) & (1<<(bpl-8*i-6))) >> (bpl-8*i-6) << pos);
			*l2 |= ((u_long)((*(l1+nl) & (1<<(bpl-8*i-6))) >> (bpl-8*i-6) << pos)) << 1;
			pos -= 2;

			*l2 |= (u_long)((*(l1+nl) & (1<<(bpl-8*i-7))) >> (bpl-8*i-7) << pos);
			*l2 |= ((u_long)((*(l1+nl) & (1<<(bpl-8*i-7))) >> (bpl-8*i-7) << pos)) << 1;
			pos -= 2;

			*l2 |= (u_long)((*(l1+nl) & (1<<(bpl-8*i-8))) >> (bpl-8*i-8) << pos);
			*l2 |= ((u_long)((*(l1+nl) & (1<<(bpl-8*i-8))) >> (bpl-8*i-8) << pos)) << 1;
			pos -= 2;

			*l2 |= (u_long)((*(l1+nl) & (1<<(bpl-8*i-1))) >> (bpl-8*i-1) << pos);
			*l2 |= ((u_long)((*(l1+nl) & (1<<(bpl-8*i-1))) >> (bpl-8*i-1) << pos)) << 1;
			pos -= 2;

			*l2 |= (u_long)((*(l1+nl) & (1<<(bpl-8*i-2))) >> (bpl-8*i-2) << pos);
			*l2 |= ((u_long)((*(l1+nl) & (1<<(bpl-8*i-2))) >> (bpl-8*i-2) << pos)) << 1;
			pos -= 2;

			*l2 |= (u_long)((*(l1+nl) & (1<<(bpl-8*i-3))) >> (bpl-8*i-3) << pos);
			*l2 |= ((u_long)((*(l1+nl) & (1<<(bpl-8*i-3))) >> (bpl-8*i-3) << pos)) << 1;
			pos -= 2;

			*l2 |= (u_long)((*(l1+nl) & (1<<(bpl-8*i-4))) >> (bpl-8*i-4) << pos);
			*l2 |= ((u_long)((*(l1+nl) & (1<<(bpl-8*i-4))) >> (bpl-8*i-4) << pos)) << 1;
			pos -= 2;
			if (pos < 0) *l2--;
		    }
		}
		l1 += lpr;              // begin of next line
		l2 = l1 + lpr - 1;      // end of next line
	    }
	}
	memset(l2, 0, MARGIN_BOT*lpr*sizeof (u_long));
    }
    MemoryDecoder dec(buf, w, totdata, fillorder, params.is2D(), (params.df == DF_2DMMR));
    u_char* encbuf = dec.encodeTagLine(raster, th, tagLineSlop);
    totdata = dec.getCC();
    return (encbuf);
}

void
vlogError(const char* fmt, va_list ap)
{
    vfprintf(stderr, fmt, ap);
    fputs(".\n", stderr);
}

// NB: must duplicate this to avoid pulling faxApp&co.

extern "C" void
_fxassert(const char* msg, const char* file, int line)
{
    fprintf(stderr, "Assertion failed \"%s\", file \"%s\" line %d.\n", 
	msg, file, line);
    abort();
    /*NOTREACHED*/
}

const char* appName;

void
usage()
{
    fprintf(stderr,
	_("usage: %s [-m format] [-o t.tif] [-f font.pcf] input.tif\n"),
	appName);
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
    extern int optind;
    extern char* optarg;
    int c;
    const char* output = "t.tif";

#ifdef LC_CTYPE
    setlocale(LC_CTYPE, "");			// for <ctype.h> calls
#endif
#ifdef LC_TIME
    setlocale(LC_TIME, "");			// for strftime calls
#endif
    appName = argv[0];
    while ((c = Sys::getopt(argc, argv, "f:m:o:")) != -1)
	switch (c) {
	case 'f':
	    tagLineFontFile = optarg;
	    break;
	case 'm':
	    tagLineFmt = optarg;
	    break;
	case 'o':
	    output = optarg;
	    break;
	case '?':
	    usage();
	    /*NOTREACHED*/
	}
    if (argc - optind != 1)
	usage();
    TIFF* tif = TIFFOpen(argv[optind], "r");
    if (!tif)
	fatal(_("%s: Cannot open, or not a TIFF file"), argv[optind]);
    uint16 comp;
    TIFFGetField(tif, TIFFTAG_COMPRESSION, &comp);
    if (comp != COMPRESSION_CCITTFAX3 && comp != COMPRESSION_CCITTFAX4)
	fatal(_("%s: Not a Group 3 or Group 4-encoded TIFF file"), argv[optind]);
    setupTagLine();
    if (!tagLineFont->isReady())
	fatal(_("%s: Problem reading font"), (const char*) tagLineFontFile);

    TIFF* otif = TIFFOpen(output, "w");
    if (!otif)
	fatal(_("%s: Cannot create output file"), output);
    for (totalPages = 1; TIFFReadDirectory(tif); totalPages++)
	;
    TIFFSetDirectory(tif, 0);

    Class2Params params;
    params.vr = VR_NORMAL;
    params.wd = WD_A4;
    params.ln = LN_INF;
    params.df = DF_1DMH;

    pageNumber = 1;

    setupTagLine();
    do {
	uint32 l, w;
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &l);
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);

	TIFFSetField(otif, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
	TIFFSetField(otif, TIFFTAG_IMAGEWIDTH, w);
	/* 
	 * Values sampled using tiffinfo on tif generated by ps2fax
	 * VR_NORMAL     w=1728 l=1169
	 * VR_100X200    w=1728 l=?
	 * VR_FINE       w=1728 l=2292
	 * VR_200X200    w=1728 l=2339
	 * VR_R8         w=1728 l=4573
	 * VR_200X400    w=1728 l=4578
	 * VR_300X300    w=2592 l=3508
	 * VR_R16        w=3456 l=4573
	 *
	 * To simplify the setting of params.vr we use:
	 * VR_NORMAL in place of VR_100X200
	 * VR_FINE   in place of VR_200X200
	 * VR_R8     in place of VR_200X400
	 * for the purpose of imaging tagline this shouldn't make a difference 
	 *
	 */
	if (w <= 2000)
	    params.vr = (l < 1500 ? VR_NORMAL : l < 3000 ? VR_FINE : VR_R8);
	else if (w <= 3000)
	    params.vr = VR_300X300;
	else
	    params.vr = VR_R16;
	TIFFSetField(otif, TIFFTAG_XRESOLUTION, (float) params.horizontalRes());
	TIFFSetField(otif, TIFFTAG_YRESOLUTION, (float) params.verticalRes());
	TIFFSetField(otif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
	TIFFSetField(otif, TIFFTAG_IMAGELENGTH, l);
	TIFFSetField(otif, TIFFTAG_BITSPERSAMPLE, 1);
	TIFFSetField(otif, TIFFTAG_SAMPLESPERPIXEL, 1);
	TIFFSetField(otif, TIFFTAG_FILLORDER, FILLORDER_LSB2MSB);
	TIFFSetField(otif, TIFFTAG_FILLORDER, FILLORDER_LSB2MSB);
	uint32 r;
	TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &r);
	TIFFSetField(otif, TIFFTAG_ROWSPERSTRIP, r);
	TIFFSetField(otif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(otif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISWHITE);

	TIFFSetField(otif, TIFFTAG_COMPRESSION, comp);
	uint32 opts = 0;
	if (comp != COMPRESSION_CCITTFAX4) {
	    TIFFGetField(tif, TIFFTAG_GROUP3OPTIONS, &opts);
	    params.df = (opts & GROUP3OPT_2DENCODING) ? DF_2DMR : DF_1DMH;
	    TIFFSetField(otif, TIFFTAG_GROUP3OPTIONS, opts);
	} else {
	    TIFFGetField(tif, TIFFTAG_GROUP4OPTIONS, &opts);
            params.df = DF_2DMMR;
	    TIFFSetField(tif, TIFFTAG_GROUP4OPTIONS, opts);
	}
	uint16 o;
	if (TIFFGetField(otif, TIFFTAG_ORIENTATION, &o))
	    TIFFSetField(tif, TIFFTAG_ORIENTATION, o);

	uint16 fillorder;
	TIFFGetFieldDefaulted(tif, TIFFTAG_FILLORDER, &fillorder);
	uint32* stripbytecount;
	(void) TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &stripbytecount);

	bool firstStrip = setupTagLineSlop(params);
	u_int ts = tagLineSlop;
	for (u_int strip = 0; strip < TIFFNumberOfStrips(tif); strip++) {
	    u_long totbytes = (u_long) stripbytecount[strip];
	    if (totbytes > 0) {
		u_char* data = new u_char[totbytes+ts];
		if (TIFFReadRawStrip(tif, strip, data+ts, totbytes) >= 0) {
		    u_char* dp;
		    if (firstStrip) {
			uint32 rowsperstrip;
			TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rowsperstrip);  
			if (rowsperstrip == (uint32) -1)
			    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &rowsperstrip);
			/*
			 * Generate tag line at the top of the page.
			 */
			u_long totdata = totbytes;
			dp = imageTagLine(data+ts, fillorder, params, totdata);
			totbytes = (params.df == DF_2DMMR) ? totdata : totbytes+ts - (dp-data);
			firstStrip = false;
		    } else
			dp = data;
		    if (fillorder != FILLORDER_LSB2MSB)
			TIFFReverseBits(dp, totbytes);
		    if (TIFFWriteRawStrip(otif, strip, dp, totbytes) == -1)
			fatal(_("%s: Write error at strip %u, writing %lu bytes"), 
			    output, strip, (u_long) totbytes);
		}
		delete data;
	    }
	}
	pageNumber++;
    } while (TIFFReadDirectory(tif) && TIFFWriteDirectory(otif));
    TIFFClose(otif);
    return (0);
}
