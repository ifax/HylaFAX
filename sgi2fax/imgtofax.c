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
 * Prepare SGI image files for transmission as facsimile.
 * Each image is scaled to fit a standard page, sharpened
 * with a hipass filter, gamma warp'd, and then dithered
 * to a bilevel image.
 *
 * Output is a single TIFF Class F file with each image as
 * a separate page/directory.
 *
 * Derived from Paul Haeberli's imgtofax program.
 */
#include <stdio.h>
#include <math.h>
#include <image.h>
#include "hipass.h"
#include "izoom.h"
#include "lut.h"
#include "lum.h"
#include "tiffio.h"
#include <stdarg.h>
#include "PageSize.h"

#define GAMMAVAL        0.8

#define	DEF_MARGIN	14
#define	DEF_TOPMARGIN	0
#define	DEF_BOTMARGIN	0

static	short sbuf[4096];
static	IMAGE* iimage;
static	highpass *hp;
static	zoom* zm;
static	TIFF* tif;
static	lut* lookup;
static	int npages;

static	int pixelWidth;
static	int pixelHeight;
static	int leftMargin		= DEF_MARGIN;
static	int rightMargin		= DEF_MARGIN;
static	int topMargin		= DEF_TOPMARGIN;
static	int bottomMargin	= DEF_BOTMARGIN;
static	int pageWidth;
static	int pageHeight;
static	float pageres	= 196.;		/* default is medium res */
static	long g3opts	= GROUP3OPT_FILLBITS;

static void
fatal(char* va_alist, ...)
#define	fmt va_alist
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "sgi2fax: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ".\n");
    va_end(ap);
    exit(-1);
}
#undef fmt

static void
getimgrow(buf,y)
short *buf;
int y;
{
    getbwrow(iimage,buf,iimage->ysize-1-y);
}

static void
getzrow(buf,y)
short *buf;
int y;
{
    getzoomrow(zm,buf,y);
}

static void
tofax(short* wp, int n)
{
#define MAXXSIZE	2432
    unsigned char row[(MAXXSIZE+7) &~ 7], *rp = row;
    int bit = 0x80;

    memset(row, 0, sizeof (row));
    while (n-- > 0) {
	if (*wp++ < 128)
	    *rp |= bit;
	if ((bit >>= 1) == 0) {
	    rp++;
	    bit = 0x80;
	}
    }
    (void) TIFFWriteScanline(tif, row,
	TIFFCurrentRow(tif) == (uint32) -1 ? 0 : TIFFCurrentRow(tif), 0);
}

static float transfunc(f)
float f;
{
    return pow(f,GAMMAVAL);
}

/* 
 *      dithering stuff follows
 *
 */
#define MATSIZE88

#define XSIZE   8
#define YSIZE   8

#ifdef notdef
static short Xdithmat[YSIZE][XSIZE] = {         /* 8x8 floyd-steinberg */
        0,      8,      36,     44,     2,      10,     38,     46,
        16,     24,     52,     60,     18,     26,     54,     62,
        32,     40,     4,      12,     34,     42,     6,      14,
        48,     56,     20,     28,     50,     58,     22,     30,
        3,      11,     39,     47,     1,      9,      37,     45,
        19,     27,     55,     63,     17,     25,     53,     61,
        35,     43,     7,      15,     33,     41,     5,      13,
        51,     59,     23,     31,     49,     57,     21,     29,
};
#endif
static short dithmat[YSIZE][XSIZE] = {          /* halftone dots */
        3,      17,     55,     63,     61,     47,     9,      1,
        15,     29,     39,     51,     49,     35,     25,     13,
        40,     32,     26,     20,     22,     30,     36,     42,
        56,     44,     10,     4,      6,      18,     52,     58,
        60,     46,     8,      0,      2,      16,     54,     62,
        48,     34,     24,     12,     14,     28,     38,     50,
        23,     31,     37,     43,     41,     33,     27,     21,
        7,      19,     53,     59,     57,     45,     11,     5,
};

#define TOTAL           (XSIZE*YSIZE)

static ditherrow(buf,y,n)
short *buf;
int y, n;
{
    int r, val;
    int rshades, rmaxbits;
    short *rdith;

    rdith = &dithmat[y%YSIZE][0];
    rshades = TOTAL+1;
    rmaxbits = ((rshades-1)/TOTAL);
    while(n--) {
        r = *buf;
        val = (rshades*r)/255;
        if(val>=TOTAL) 
            *buf++ = 255;
        else if(val>rdith[n%XSIZE])
            *buf++ = 255;
        else
            *buf++ = 0;
    }
}

static void
blankSpace(int nrows)
{
    setrow(sbuf,255,pixelWidth);
    while (nrows-- > 0)
        tofax(sbuf,pixelWidth);
}

static void
imgtofax(char* input, int pn)
{
    int ixsize, iysize;
    int oxsize, oysize;
    int ymargin;
    int y;

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (long) pixelWidth);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (long) pixelHeight);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, -1L);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 1);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX3);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISWHITE);
    TIFFSetField(tif, TIFFTAG_FILLORDER, FILLORDER_LSB2MSB);
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
    TIFFSetField(tif, TIFFTAG_XRESOLUTION, 204.);
    TIFFSetField(tif, TIFFTAG_YRESOLUTION, pageres);
    TIFFSetField(tif, TIFFTAG_PAGENUMBER, pn, npages);
    TIFFSetField(tif, TIFFTAG_CLEANFAXDATA, CLEANFAXDATA_CLEAN);
    { char buf[1024];
      snprintf(buf, sizeof(buf) - 1, "Ditherered B&W version of %s", input);
      TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, buf);
    }
    TIFFSetField(tif, TIFFTAG_SOFTWARE, "sgi2fax");
    TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_BOTLEFT);
    TIFFSetField(tif, TIFFTAG_GROUP3OPTIONS, g3opts);

/* calculate the zoom factor */
    ixsize = iimage->xsize;
    iysize = iimage->ysize;
    oxsize = pageWidth;
    oysize = (iysize*pageWidth)/ixsize;		/* maintain aspect ratio */
    if (pageres == 98.)
	oysize /= 2;

/* set up the filters */
    zm = newzoom(getimgrow, ixsize, iysize, oxsize, oysize, TRIANGLE, 1.0);
    hp = newhp(getzrow, oxsize, oysize, 2.0);

    ymargin = (pageHeight-oysize)/2;
    if(ymargin<0)
        ymargin = 0;
    blankSpace(topMargin + ymargin);
    for(y=0; y<oysize; y++) {
        hpgetrow(hp, sbuf, y); 
        applylut(lookup, sbuf, oxsize);
        ditherrow(sbuf, y, oxsize);
        tofax(sbuf, pixelWidth);
    }
    blankSpace(bottomMargin + ymargin);

    freezoom(zm);
    freehp(hp);

    TIFFWriteDirectory(tif);
}

static void
usage()
{
    fprintf(stderr, "usage: sgi2fax %s %s %s %s %s %s %s [-12] file ...\n",
	"[-h height]",
	"[-w width]",
	"[-v vres]",
	"[-o output]",
	"[-r %red]",
	"[-g %green]",
	"[-b %blue]",
	""
    );
    exit(-1);
}

#define	CVT(x)	(((x)*255)/100)

main(argc, argv)
    int argc;
    char **argv;
{
    extern int optind;
    extern char* optarg;
    char* output = "sgi.fax";
    int c;
    float w, h;
    struct pageSizeInfo* info;

    info = getPageSize("default");
    w = getPageWidth(info);
    h = getPageHeight(info);
    delPageSize(info);
    while ((c = getopt(argc, argv, "o:r:g:b:h:s:v:w:12")) != -1)
	switch (c) {
	case '1':
	    g3opts &= ~GROUP3OPT_2DENCODING;
	    break;
	case '2':
	    g3opts |= GROUP3OPT_2DENCODING;
	    break;
	case 'r':			/* %red illumination */
	    _RILUM = CVT(atoi(optarg));
	    break;
	case 'g':			/* %green illumination */
	    _GILUM = CVT(atoi(optarg));
	    break;
	case 'b':			/* %blue illumination */
	    _BILUM = CVT(atoi(optarg));
	    break;
	case 'o':			/* output file */
	    output = optarg;
	    break;
	case 's':			/* page size */
	    info = getPageSize(optarg);
	    if (!info) {
		fprintf(stderr, "%s: Unknown page size \"%s\".\n",
		    argv[0], optarg);
		exit(-1);
	    }
	    w = getPageWidth(info);
	    h = getPageHeight(info);
	    delPageSize(info);
	    break;
	case 'v':			/* vertical resolution (lines/inch) */
	    pageres = atof(optarg);
	    /* XXX force acceptable resolutions */
	    if (pageres < 120.)
		pageres = 98.;
	    else
		pageres = 196.;
	    break;
	case 'w':			/* page width (mm) */
	    w = atof(optarg);
	    break;
	case 'h':			/* page height (mm) */
	    h = atof(optarg);
	    break;
	case '?':
	    usage();
	}
    if (argc - optind < 1)
	usage();

    /* XXX force known sizes */
    if (w > 280)
	pixelWidth = 2432;
    else if (w > 230)
	pixelWidth = 2048;
    else
	pixelWidth = 1728;
    if (h > 350)
	pixelHeight = 2810;
    else if (h > 280)
	pixelHeight = 2292;
    else
	pixelHeight = 2166;

    if (pageres == 98.) {
	pixelHeight /= 2;
	topMargin /= 2;
	bottomMargin /= 2;
    }
    pageWidth = pixelWidth - (leftMargin + rightMargin);
    pageHeight = pixelHeight - (topMargin + bottomMargin);

/* open the output file */
    tif = TIFFOpen(output, "w");
    if (!tif)
	fatal("%s: Can not create output file", output);

    lookup = makelut(transfunc,256,256,0);

    npages = argc - optind;
    for (c = 0; optind < argc; c++, optind++) {
	iimage = iopen(argv[optind], "r");
	if (!iimage) {
	    fprintf(stderr, "sgi2fax: %s: Can not open\n", argv[optind]);
	    continue;
	}
	imgtofax(argv[optind], c);
    }
    TIFFClose(tif);
    freelut(lookup);
    exit(0);
}
