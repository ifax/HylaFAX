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
 * Program for testing copy quality support.
 *
 * cqtest [-m maxbad] [-o output.tif] [-p %good] input.tif
 */
#include "config.h"
#include "Sys.h"
#include "StackBuffer.h"
#include "G3Decoder.h"
#include "Class2Params.h"
#include "Str.h"
#include "FaxTrace.h"
#include "tiffio.h"

struct CQDecoder : public G3Decoder {
    u_int	cblc;		// current count of consecutive bad lines
    bool	lastRowBad;	// last decoded row was bad

    u_long	recvEOLCount;	// EOL count for received page
    u_long	recvBadLineCount;
    u_long	recvConsecutiveBadLineCount;
    tstrip_t	recvStrip;	// current strip number during receive
    u_char*	recvRow;	// current receive row raster
    fxStackBuffer* recvBuf;

    u_char*	cp;
    u_int	cc;
    u_int	percentGoodLines;
    u_int	maxConsecutiveBadLines;
    uint16	recvFillOrder;
    uint32	group3opts;

    void	recvSetupTIFF(TIFF* tif, long group3opts, int fillOrder,
		    const Class2Params& params);
    void	flushEncodedData(TIFF*, tstrip_t, u_char*, u_int);
    void	flushRawData(TIFF*, tstrip_t, u_char*, u_int);
    void	invalidCode(const char* type, int x);
    void	badPixelCount(const char* type, int got, int expected);
    void	badDecodingState(const char* type, int x);

    void	recvTrace(const char* fmt, ...);
    void	serverTrace(const char* fmt ...);
    void	copyQualityTrace(const char* fmt, ...);

    bool	recvPageDLEData(TIFF* tif, bool checkQuality,
		    const Class2Params& params, fxStr& emsg);
    void	abortPageRecv();
    int		nextByte();

    bool	checkQuality();
    bool	isQualityOK(const Class2Params&);

    CQDecoder();
    ~CQDecoder();
};

CQDecoder::CQDecoder()
{
    percentGoodLines = 95;
    maxConsecutiveBadLines = 5;
    group3opts = 0;
}
CQDecoder::~CQDecoder()		{}
void CQDecoder::abortPageRecv()	{}

void
vtraceStatus(int, const char* fmt, va_list ap)
{
    vfprintf(stdout, fmt, ap);
    putchar('\n');
}

#define	RCVBUFSIZ	(32*1024)		// XXX

/*
 * Prepare for the reception of page data by setting the
 * TIFF tags to reflect the data characteristics.
 */
void
CQDecoder::recvSetupTIFF(TIFF* tif, long, int fillOrder, const Class2Params& params)
{
    TIFFSetField(tif, TIFFTAG_SUBFILETYPE,	FILETYPE_PAGE);
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH,	(uint32) params.pageWidth());
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE,	1);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC,	PHOTOMETRIC_MINISWHITE);
    TIFFSetField(tif, TIFFTAG_ORIENTATION,	ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL,	1);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP,	(uint32) -1);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG,	PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_FILLORDER,	(uint16) fillOrder);
    TIFFSetField(tif, TIFFTAG_XRESOLUTION,	(float) params.horizontalRes());
    TIFFSetField(tif, TIFFTAG_YRESOLUTION,	(float) params.verticalRes());
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT,	RESUNIT_INCH);
    TIFFSetField(tif, TIFFTAG_SOFTWARE,		HYLAFAX_VERSION);
#ifdef notdef
    TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION,	(const char*) tsi);
#endif
    char dateTime[24];
    time_t now = Sys::now();
    strftime(dateTime, sizeof (dateTime), "%Y:%m:%d %H:%M:%S",
	localtime(&now));
    TIFFSetField(tif, TIFFTAG_DATETIME,	    dateTime);
#ifdef notdef
    TIFFSetField(tif, TIFFTAG_MAKE,	    (const char*) getManufacturer());
    TIFFSetField(tif, TIFFTAG_MODEL,	    (const char*) getModel());
    TIFFSetField(tif, TIFFTAG_HOSTCOMPUTER, (const char*) server.hostname);
#endif
}

/*
 * Receive Phase C data with or without copy
 * quality checking and erroneous row fixup.
 */
bool
CQDecoder::recvPageDLEData(TIFF* tif, bool checkQuality,
    const Class2Params& params, fxStr& emsg)
{
    setupDecoder(recvFillOrder, params.is2D());

    u_int rowpixels = params.pageWidth();	// NB: assume rowpixels <= 4864
    tiff_runlen_t runs[2*4864];			// run arrays for cur+ref rows
    setRuns(runs, runs+4864, rowpixels);

    recvEOLCount = 0;				// count of EOL codes
    recvBadLineCount = 0;			// rows with a decoding error
    recvConsecutiveBadLineCount = 0;		// max consecutive bad rows
    /*
     * Data destined for the TIFF file is buffered in buf.
     * recvRow points to the next place in buf where data
     * (decoded or undecoded) is to be placed.  Beware
     * that this is not an automatic variable because our
     * use of setjmp/longjmp to deal with EOF and RTC detection
     * does not guarantee correct values being restored to
     * automatic variables when the stack is unwound.
     */
    u_char buf[RCVBUFSIZ];			// output buffer
    recvRow = buf;				// current decoded row
    recvStrip = 0;				// TIFF strip number
    if (EOFraised()) {
	abortPageRecv();
	emsg = "Missing EOL after 5 seconds";
	recvTrace("%s", (const char*) emsg);
	return (false);
    }
    if (checkQuality) {
	/*
	 * Receive a page of data w/ copy quality checking.
	 * Note that since we decode and re-encode we can
	 * trivially do transcoding by just changing the
	 * TIFF tag values setup for each received page.  This
	 * may however be too much work for some CPUs.
	 */
	tsize_t rowSize = TIFFScanlineSize(tif);// scanline buffer size
	/*
	 * Doing copy quality checking; since we decode
	 * and re-encode we can choose any compression
	 * we want for the data that's eventually written
	 * to the file.
	 */
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX4);
	/*
	 * When copy quality checking is done we generate multi-strip
	 * images where the strip size is dependent on the size of the
	 * receive buffer used to buffer decoded image data.  There is
	 * a balance here between reducing i/o, minimizing encoding
	 * overhead, and minimizing delays that occur in the middle of
	 * page data receive operations.  If the receive buffer is made
	 * too large then a modem with little buffering may have a
	 * data underrun occur.  On the other hand encoding each row of
	 * data adds to the overhead both in setup time for the encoder
	 * and in various accounting work done by the TIFF library when
	 * images are grown row-by-row.  All this is also dependent on
	 * the speed of the host CPU and i/o performance.
	 */
	TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, RCVBUFSIZ / rowSize);
	u_char* curGood = buf;			// last good row
	memset(curGood, 0, (size_t) rowSize);	// initialize to all white
	recvBuf = NULL;				// don't need raw data

	lastRowBad = false;			// no previous row
	cblc = 0;				// current bad line run
	if (!RTCraised()) {
	    for (;;) {
		/*
		 * Decode the next row of data into the raster.  If
		 * an error is encountered, replicate the last good
		 * row and continue.  We track statistics on bad
		 * lines and consecutive bad lines; these are used
		 * later for deciding whether or not the page quality
		 * is acceptable.
		 */
		bool decodeOK = decodeRow(recvRow, rowpixels);
		if (seenRTC())			// seen RTC, flush everything
		    continue;
		if (decodeOK) {
		    curGood = recvRow;		// reset last good
		    if (lastRowBad) {		// reset run statistics
			lastRowBad = false;
			if (cblc > recvConsecutiveBadLineCount)
			    recvConsecutiveBadLineCount = cblc;
			cblc = 0;
		    }
		} else {
		    memcpy(recvRow, curGood, (size_t) rowSize);
			// replicate last good
		    recvBadLineCount++;
		    cblc++;
		    lastRowBad = true;
		}
		/*
		 * Advance forward a scanline and if necessary
		 * flush the buffer.  Note that we leave the
		 * pointer to the last good row of data at the
		 * last decoded line near the end of the buffer.
		 * We assume the buffer is large enough that we
		 * don't have to worry about overlapping with the
		 * next decoded scanline.
		 */
		recvRow += rowSize;
		recvEOLCount++;
		if (recvRow + rowSize >= &buf[RCVBUFSIZ]) {
		    flushEncodedData(tif, recvStrip++, buf, recvRow - buf);
		    recvRow = buf;
		}
	    }
	}
	if (seenRTC()) {			// RTC found in data stream
	    /*
	     * Adjust everything to reflect the location
	     * at which RTC was found in the data stream.
	     */
	    copyQualityTrace("Adjusting for RTC found at row %u", getRTCRow());
						// # EOL's in recognized RTC
	    u_int n = (u_int) (recvEOLCount - getRTCRow());
	    if ((recvRow -= n*rowSize) < buf)
		recvRow = buf;
	    recvBadLineCount -= n;		// deduct RTC
	    recvEOLCount = getRTCRow();		// adjust row count
	} else if (lastRowBad) {
	    /*
	     * Adjust the received line count to deduce the last
	     * consecutive bad line run since the RTC is often not
	     * readable and/or is followed by line noise or random
	     * junk from the sender.
	     */
	    copyQualityTrace("adjusting for trailing noise (%lu run)", cblc);
	    recvEOLCount -= cblc;
	    recvBadLineCount -= cblc;
	    if ((recvRow -= cblc*rowSize) < buf)
		recvRow = buf;
	}
	recvTrace("%lu total lines, %lu bad lines, %lu consecutive bad lines"
	    , recvEOLCount
	    , recvBadLineCount
	    , recvConsecutiveBadLineCount
	);
	if (recvRow > &buf[0])
	    flushEncodedData(tif, recvStrip, buf, recvRow - buf);
    } else {
	/*
	 * Receive a page of data w/o doing copy quality analysis.
	 *
	 * NB: the image is written as a single strip of data.
	 */
	/*
	 * Not doing copy quality checking so compression and
	 * Group3Options must reflect the negotiated session
	 * parameters for the forthcoming page data.
	 */
	switch (params.df) {
	case DF_2DMMR:
	    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX4);
	    break;
	case DF_2DMRUNCOMP:
	    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX3);
	    TIFFSetField(tif, TIFFTAG_GROUP3OPTIONS,
		group3opts | GROUP3OPT_2DENCODING|GROUP3OPT_UNCOMPRESSED);
	    break;
	case DF_2DMR:
	    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX3);
	    TIFFSetField(tif, TIFFTAG_GROUP3OPTIONS,
		group3opts | GROUP3OPT_2DENCODING);
	    break;
	case DF_1DMH:
	    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX3);
	    TIFFSetField(tif, TIFFTAG_GROUP3OPTIONS,
		group3opts &~ GROUP3OPT_2DENCODING);
	    break;
	}
	/*
	 * NB: There is a potential memory leak here if the
	 * stack buffer gets expanded, such that memory gets
	 * malloc'd, and EOF is raised.  In that case the destructor
	 * may not be invoked (in fact it almost certainly won't).
	 * This is an unlikely scenario so for now we'll leave
	 * the code the way it is.
	 */
	fxStackBuffer raw;			// XXX may leak
	recvBuf = &raw;
	if (!RTCraised()) {
	    for (;;) {
		raw.reset();
		(void) decodeRow(NULL, rowpixels);
		if (seenRTC())
		    continue;
		u_int n = raw.getLength();
		if (recvRow + n >= &buf[RCVBUFSIZ]) {
		    flushRawData(tif, 0, buf, recvRow - buf);
		    recvRow = buf;
		}
		memcpy(recvRow, (const char*) raw, n);
		recvRow += n;
		recvEOLCount++;
	    }
	}
	if (recvRow > &buf[0])
	    flushRawData(tif, 0, buf, recvRow - buf);
	if (seenRTC()) {			// RTC found in data stream
	    /*
	     * Adjust the received line count to reflect the
	     * location at which RTC was found in the data stream.
	     */
	    copyQualityTrace("Adjusting for RTC found at row %u", getRTCRow());
	    recvEOLCount = getRTCRow();
	}
    }
    return (true);
}

void
CQDecoder::flushEncodedData(TIFF* tif, tstrip_t strip, u_char* buf, u_int cc)
{
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, recvEOLCount);
    if (TIFFWriteEncodedStrip(tif, strip, buf, cc) == -1)
	serverTrace("RECV: %s: write error", TIFFFileName(tif));
}

/*
 * Write a strip of raw data to the receive file.
 */
void
CQDecoder::flushRawData(TIFF* tif, tstrip_t strip, u_char* buf, u_int cc)
{
    recvTrace("%u bytes of data, %lu total lines", cc, recvEOLCount);
    if (TIFFWriteRawStrip(tif, strip, buf, cc) == -1)
	serverTrace("RECV: %s: write error", TIFFFileName(tif));
}

/*
 * Check if the configuration parameters indicate if
 * copy quality checking should be done on recvd pages.
 */
bool
CQDecoder::checkQuality()
{
    return (percentGoodLines != 0 && maxConsecutiveBadLines != 0);
}

/*
 * Check the statistics accumulated during a page recived
 * against the configuration parameters and return an
 * indication of whether or not the page quality is acceptable.
 */
bool
CQDecoder::isQualityOK(const Class2Params& params)
{
    if (percentGoodLines != 0 && recvEOLCount != 0) {
	u_long percent = 100 * (recvEOLCount - recvBadLineCount) / recvEOLCount;
	if (percent < percentGoodLines) {
	    serverTrace("RECV: REJECT page quality, %u%% good lines (%u%% required)",
		percent, percentGoodLines);
	    return (false);
	}
    }
    u_int cblc = maxConsecutiveBadLines;
    if (cblc != 0) {
	if (params.vr == VR_FINE)
	    cblc *= 2;
	if (recvConsecutiveBadLineCount > cblc) {
	    serverTrace("RECV: REJECT page quality, %u-line run (max %u)",
		recvConsecutiveBadLineCount, cblc);
	    return (false);
	}
    }
    return (true);
}

int
CQDecoder::nextByte()
{
    if (cc == 0)
	raiseRTC();
    cc--;
    int b = getBitmap()[*cp++];
    if (recvBuf)
	recvBuf->put(b);
    return (b);
}

/*
 * Trace a protocol-receive related activity.
 */
void
CQDecoder::recvTrace(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    static const fxStr recv("RECV: ");
    vtraceStatus(FAXTRACE_PROTOCOL, recv | fmt, ap);
    va_end(ap);
}

/*
 * Trace a protocol-receive related activity.
 */
void
CQDecoder::serverTrace(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vtraceStatus(FAXTRACE_SERVER, fmt, ap);
    va_end(ap);
}

/*
 * Note an invalid G3 code word.
 */
void
CQDecoder::invalidCode(const char* type, int x)
{
    if (!seenRTC())
	copyQualityTrace("Invalid %s code word, row %u, x %d",
	    type, getReferenceRow(), x);
}

/*
 * Note a row decode that gives the wrong pixel count.
 */
void
CQDecoder::badPixelCount(const char* type, int got, int expected)
{
    if (!seenRTC())
	copyQualityTrace("Bad %s pixel count, row %u, got %d, expected %d",
	    type, getReferenceRow(), got, expected);
}

void
CQDecoder::badDecodingState(const char* type, int x)
{
    copyQualityTrace("Panic, bad %s decoding state, row %u, x %d",
	type, getReferenceRow(), x);
}

/*
 * Trace a copy quality-reated activity.
 */
void
CQDecoder::copyQualityTrace(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    static const fxStr cq("RECV/CQ: ");
    vtraceStatus(FAXTRACE_COPYQUALITY, cq | fmt, ap);
    va_end(ap);
}

const char* appName;

void
usage()
{
    fprintf(stderr, "usage: %s [-m maxbad] [-p %%good] [-o output.tif] input.tif\n", appName);
    _exit(-1);
}

int
main(int argc, char* argv[])
{
    const char* outFile = "cq.tif";
    extern int optind, opterr;
    extern char* optarg;
    int c;

    CQDecoder cq;
    appName = argv[0];
    while ((c = getopt(argc, argv, "m:o:p:")) != -1)
	switch (c) {
	case 'm':
	    cq.maxConsecutiveBadLines = (u_int) strtoul(optarg, NULL, 0);
	    break;
	case 'o':
	    outFile = optarg;
	    break;
	case 'p':
	    cq.percentGoodLines = (u_int) strtoul(optarg, NULL, 0);
	    break;
	case '?':
	    usage();
	    /*NOTREACHED*/
	}
    if (argc - optind != 1)
	usage();
    TIFF* tif = TIFFOpen(argv[optind], "r");
    if (!tif) {
	fprintf(stderr, "%s: Cannot open, or not a TIFF file\n", argv[optind]);
	return (-1);
    }
    TIFF* tifout = TIFFOpen(outFile, "w");
    if (!tifout) {
	fprintf(stderr, "%s: Cannot create TIFF file\n", outFile);
	return (-1);
    }
    Class2Params params;
    do {
	uint32 w;
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
	params.setPageWidthInPixels((u_int) w);
	uint32 l;
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &l);
	params.ln = LN_A4;
	if (l < 1500)
	    params.vr = VR_NORMAL;
	else
	    params.vr = VR_FINE;
	uint32 opts = 0;
	TIFFGetField(tif, TIFFTAG_GROUP3OPTIONS, &opts);
	params.df = (opts & GROUP3OPT_2DENCODING) ? DF_2DMR : DF_1DMH;
	TIFFGetFieldDefaulted(tif, TIFFTAG_FILLORDER, &cq.recvFillOrder);

	printf("%u x %u, %s, %s, %s\n"
	    , params.pageWidth()
	    , params.pageLength()
	    , params.verticalResName()
	    , params.dataFormatName()
	    , cq.recvFillOrder == FILLORDER_LSB2MSB ? "lsb-to-msb" : "msb-to-lsb"
	);

	cq.recvSetupTIFF(tifout, 0, cq.recvFillOrder, params);

	uint32* stripbytecount;
	(void) TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &stripbytecount);
	for (u_int strip = 0; strip < TIFFNumberOfStrips(tif); strip++) {
	    u_int totbytes = (u_int) stripbytecount[strip];
	    if (totbytes > 0) {
		u_char* data = new u_char[totbytes];
		if (TIFFReadRawStrip(tif, strip, data, totbytes) >= 0) {
		    fxStr emsg;
		    cq.cp = data;
		    cq.cc = totbytes;
		    (void) cq.recvPageDLEData(tifout,
			cq.checkQuality(), params, emsg);
		} else
		    printf("Read error on strip %u\n", strip);
		delete data;
	    }
	}
	TIFFSetField(tifout, TIFFTAG_IMAGELENGTH, cq.recvEOLCount);
    } while (TIFFReadDirectory(tif) && TIFFWriteDirectory(tifout));
    TIFFClose(tifout);
    TIFFClose(tif);
    return (0);
}
