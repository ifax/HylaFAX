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
#include "G3Decoder.h"
#include "G3Encoder.h"
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
	case 't': insert(tagLine, l, fxStr((int) totalPages, "%u")); break;
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

bool
setupTagLineSlop(const Class2Params& params)
{
    if (tagLineFont->isReady()) {
	tagLineSlop = (tagLineFont->fontHeight()+MARGIN_TOP+MARGIN_BOT+3) * 
	    howmany(params.pageWidth(),8);
	return (true);
    } else
	return (false);
}

class MemoryDecoder : public G3Decoder {
private:
    const u_char* bp;
    int		row;

    int		decodeNextByte();

    void	invalidCode(const char* type, int x);
    void	badPixelCount(const char* type, int got, int expected);
    void	badDecodingState(const char* type, int x);
public:
    MemoryDecoder(const u_char* data);
    ~MemoryDecoder();
    const u_char* current()				{ return bp; }

    void setRowNum(int r)				{ row = r; }
};
MemoryDecoder::MemoryDecoder(const u_char* data)	{ bp = data; }
MemoryDecoder::~MemoryDecoder()				{}
int MemoryDecoder::decodeNextByte()			{ return *bp++; }
void
MemoryDecoder::invalidCode(const char* type, int x)
{
    printf("Invalid %s code word, row %lu, x %d\n", type, row, x);
}
void
MemoryDecoder::badPixelCount(const char* type, int got, int expected)
{
    printf("Bad %s pixel count, row %lu, got %d, expected %d\n",
	type, row, got, expected);
}
void
MemoryDecoder::badDecodingState(const char* type, int x)
{
    printf("Panic, bad %s decoding state, row %lu, x %d\n", type, row, x);
}

#ifdef roundup
#undef roundup
#endif
#define	roundup(a,b)	((((a)+((b)-1))/(b))*(b))

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
imageTagLine(u_char* buf, u_int fillorder, const Class2Params& params)
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
	if (tag[l+1] == 'p')
	    insert(tag, l, fxStr((int) pageNumber, "%d"));
	else
	    l += 2;
    }
    /* 
     * Setup the raster in which the tag line is imaged.
     */
    u_int w = params.pageWidth();
    u_int h = tagLineFont->fontHeight()+MARGIN_TOP+MARGIN_BOT;
    u_int th = (params.vr == VR_FINE) ?
	h : (tagLineFont->fontHeight()/2)+MARGIN_TOP+MARGIN_BOT;
    /*
     * imageText assumes that raster is word-aligned; we use
     * longs here to optimize the scaling done below for the
     * low res case.  This should satisfy the word-alignment.
     */
    u_int lpr = howmany(w,32);			// longs/raster row
    u_long* raster = new u_long[(h+3)*lpr];	// decoded raster
    memset(raster, 0, (h+3)*lpr*sizeof (u_long));// clear raster to white
    /*
     * Break the tag into fields and render each piece of
     * text centered in its field.  Experiments indicate
     * that rendering the text over white is better than,
     * say, rendering it over the original page.
     */
    l = 0;
    u_int fieldWidth = params.pageWidth() / tagLineFields;
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
     * Decode (and discard) the top part of the page where
     * the tag line is to be imaged.  Note that we assume
     * the strip of raw data has enough scanlines in it
     * to satisfy our needs (caller is responsible).
     */
    MemoryDecoder dec(buf);
    dec.setupDecoder(fillorder,  params.is2D());
    tiff_runlen_t runs[2*2432];		// run arrays for cur+ref rows
    dec.setRuns(runs, runs+2432, w);

    u_int row;
    for (row = 0; row < th; row++) {
	dec.setRowNum(row);
	dec.decodeRow(NULL, w);
    }
    /*
     * If the source is 2D-encoded and the decoding done
     * above leaves us at a row that is 2D-encoded, then
     * our re-encoding below will generate a decoding
     * error if we don't fix things up.  Thus we discard
     * up to the next 1D-encoded scanline.  (We could
     * instead decode the rows and re-encoded them below
     * but to do that would require decoding above instead
     * of skipping so that the reference line for the
     * 2D-encoded rows is available.)
     */
    for (; row < th+4 && !dec.isNextRow1D(); row++) {
	dec.setRowNum(row);
	dec.decodeRow(NULL, w);
    }
    th = row;				// add in discarded rows
    /*
     * Things get tricky trying to identify the last byte in
     * the decoded data that we want to replace.  The decoder
     * must potentially look ahead to see the zeros that
     * makeup the EOL that marks the end of the data we want
     * to skip over.  Consequently dec.current() must be
     * adjusted by the look ahead, a factor of the number of
     * bits pending in the G3 decoder's bit accumulator.
     */
    u_int look_ahead = roundup(dec.getPendingBits(),8) / 8;
    u_int decoded = dec.current() - look_ahead - buf;
    if (params.vr == VR_NORMAL) {
	/*
	 * Scale text vertically before encoding.  Note the
	 * ``or'' used to generate the final samples. 
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
    /*
     * Encode the result according to the parameters of
     * the outgoing page.  Note that the encoded data is
     * written in the bit order of the page data since
     * it must be merged back with it below.
     */
    fxStackBuffer result;
    G3Encoder enc(result);
    enc.setupEncoder(fillorder, params.is2D());
    enc.encode(raster, w, th);
    delete raster;
    /*
     * To properly join the newly encoded data and the previous
     * data we need to insert two bytes of zero-fill prior to
     * the start of the old data to ensure 11 bits of zero exist
     * prior to the EOL code in the first line of data that
     * follows what we skipped over above.  Note that this
     * assumes the G3 decoder always stops decoding prior to
     * an EOL code and that we've adjusted the byte count to the
     * start of the old data so that the leading bitstring is
     * some number of zeros followed by a 1.
     */
    result.put((char) 0);
    result.put((char) 0);
    /*
     * Copy the encoded raster with the tag line back to
     * the front of the buffer that was passed in.  The
     * caller has preallocated a hunk of space for us to
     * do this and we also reuse space occupied by the
     * original encoded raster image.  If insufficient space
     * exists for the newly encoded tag line, then we jam
     * as much as will fit w/o concern for EOL markers;
     * this will cause at most one bad row to be received
     * at the receiver (got a better idea?).
     */
    u_int encoded = result.getLength();
    if (encoded > tagLineSlop + decoded)
	encoded = tagLineSlop + decoded;
    u_char* dst = buf + (int)(decoded-encoded);
    memcpy(dst, (const unsigned char*)result, encoded);
    return (dst);
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
	"usage: %s [-m format] [-o t.tif] [-f font.pcf] input.tif\n",
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
    extern int optind, opterr;
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
    while ((c = getopt(argc, argv, "f:m:o:")) != -1)
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
	fatal("%s: Cannot open, or not a TIFF file", argv[optind]);
    uint16 comp;
    TIFFGetField(tif, TIFFTAG_COMPRESSION, &comp);
    if (comp != COMPRESSION_CCITTFAX3)
	fatal("%s: Not a Group 3-encoded TIFF file", argv[optind]);
    setupTagLine();
    if (!tagLineFont->isReady())
	fatal("%s: Problem reading font", (const char*) tagLineFontFile);

    TIFF* otif = TIFFOpen(output, "w");
    if (!otif)
	fatal("%s: Cannot create output file", output);
    for (totalPages = 1; TIFFReadDirectory(tif); totalPages++)
	;
    TIFFSetDirectory(tif, 0);

    Class2Params params;
    params.vr = VR_NORMAL;
    params.wd = WD_1728;
    params.ln = LN_INF;
    params.df = DF_1DMR;

    pageNumber = 1;

    setupTagLine();
    do {
	TIFFSetField(otif, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
	TIFFSetField(otif, TIFFTAG_IMAGEWIDTH, 1728);
	uint32 l;
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &l);
	params.vr = (l < 1500 ? VR_NORMAL : VR_FINE);
	TIFFSetField(otif, TIFFTAG_XRESOLUTION, 204.);
	TIFFSetField(otif, TIFFTAG_YRESOLUTION, (float) params.verticalRes());
	TIFFSetField(otif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
	TIFFSetField(otif, TIFFTAG_IMAGELENGTH, l);
	TIFFSetField(otif, TIFFTAG_BITSPERSAMPLE, 1);
	TIFFSetField(otif, TIFFTAG_SAMPLESPERPIXEL, 1);
	TIFFSetField(otif, TIFFTAG_FILLORDER, FILLORDER_LSB2MSB);
	TIFFSetField(otif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX3);
	TIFFSetField(otif, TIFFTAG_FILLORDER, FILLORDER_LSB2MSB);
	uint32 r;
	TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &r);
	TIFFSetField(otif, TIFFTAG_ROWSPERSTRIP, r);
	TIFFSetField(otif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(otif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISWHITE);
	uint32 opts = 0;
	TIFFGetField(tif, TIFFTAG_GROUP3OPTIONS, &opts);
	params.df = (opts & GROUP3OPT_2DENCODING) ? DF_2DMR : DF_1DMR;
	TIFFSetField(otif, TIFFTAG_GROUP3OPTIONS, opts);
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
	    u_int totbytes = (u_int) stripbytecount[strip];
	    if (totbytes > 0) {
		u_char* data = new u_char[totbytes+ts];
		if (TIFFReadRawStrip(tif, strip, data+ts, totbytes) >= 0) {
		    u_char* dp;
		    if (firstStrip) {
			/*
			 * Generate tag line at the top of the page.
			 */
			dp = imageTagLine(data+ts, fillorder, params);
			totbytes = totbytes+ts - (dp-data);
			firstStrip = false;
		    } else
			dp = data;
		    if (fillorder != FILLORDER_LSB2MSB)
			TIFFReverseBits(dp, totbytes);
		    if (TIFFWriteRawStrip(otif, strip, dp, totbytes) == -1)
			fatal("%s: Write error at strip %u, writing %lu bytes", 
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
