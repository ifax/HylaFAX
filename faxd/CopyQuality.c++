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
 * Page Data Receive and Copy Quality Support for Modem Drivers.
 */
#include "Sys.h"
#include "FaxModem.h"
#include "FaxTrace.h"
#include "ModemConfig.h"
#include "StackBuffer.h"
#include "FaxServer.h"

#include "version.h"

#define	RCVBUFSIZ	(32*1024)		// XXX

static	void setupCompression(TIFF*, u_int, uint32);

/*
 * Receive Phase C data with or without copy
 * quality checking and erroneous row fixup.
 */
bool
FaxModem::recvPageDLEData(TIFF* tif, bool checkQuality,
    const Class2Params& params, fxStr& emsg)
{
    setupDecoder(conf.recvFillOrder, params.is2D());

    u_int rowpixels = params.pageWidth();	// NB: assume rowpixels <= 2432
    uint16 runs[2*2432];			// run arrays for cur+ref rows
    setRuns(runs, runs+2432, rowpixels);

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
    bytePending = 0;
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
	/*
	 * Since we decode and re-encode we can choose any
	 * compression scheme we want for the data that's
	 * eventually written to the file.
	 */
	u_int df = (conf.recvDataFormat == DF_ALL ?
	    params.df : conf.recvDataFormat);
	setupCompression(tif, df, 0);		// XXX maybe zero-fill EOLs?
	/*
	 * Do magic at page start to collect the file offset
	 * to the start of the page data--this is used in case
	 * of an error to overwrite unacceptable page data.
	 *
	 * NB: This must be done *after* setting the compression
	 *     scheme, otherwise the TIFF library will disallow
	 *     setting the Compression tag.
	 */
	recvStartPage(tif);

	u_char* curGood = buf;			// last good row
	memset(curGood, 0, rowSize);		// initialize to all white
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
		    memcpy(recvRow, curGood, rowSize);// replicate last good
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
	    u_int n = (u_int)(recvEOLCount - getRTCRow());
	    if ((recvRow -= n*rowSize) < buf)
		recvRow = buf;
	    if (n > recvBadLineCount)		// deduct RTC
		recvBadLineCount = 0;
	    else
		recvBadLineCount -= n;
	    recvEOLCount = getRTCRow();		// adjust row count
	} else if (lastRowBad) {
	    /*
	     * Adjust the received line count to deduce the last
	     * consecutive bad line run since the RTC is often not
	     * readable and/or is followed by line noise or random
	     * junk from the sender.
	     */
	    copyQualityTrace("Adjusting for trailing noise (%lu run)", cblc);
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
	setupCompression(tif, params.df, group3opts);
	/*
	 * Do magic at page start to collect the file offset
	 * to the start of the page data--this is used in case
	 * of an error to overwrite unacceptable page data.
	 *
	 * NB: This must be done *after* setting the compression
	 *     scheme, otherwise the TIFF library will disallow
	 *     setting the Compression tag.
	 */
	recvStartPage(tif);
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
		memcpy(recvRow, (char*) raw, n);
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
    // NB: must set these after compression tag
#ifdef TIFFTAG_FAXRECVPARAMS
    TIFFSetField(tif, TIFFTAG_FAXRECVPARAMS,	(uint32) params.encode());
#endif
#ifdef TIFFTAG_FAXSUBADDRESS
    if (sub != "")
	TIFFSetField(tif, TIFFTAG_FAXSUBADDRESS,	(const char*) sub);
#endif
#ifdef TIFFTAG_FAXRECVTIME
    TIFFSetField(tif, TIFFTAG_FAXRECVTIME,
	(uint32) server.getPageTransferTime());
#endif
    return (true);
}

/*
 * Setup "stock TIFF tags" in preparation for receiving a page of data.
 */
void
FaxModem::recvSetupTIFF(TIFF* tif, long, int fillOrder)
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
    TIFFSetField(tif, TIFFTAG_XRESOLUTION,	204.);
    TIFFSetField(tif, TIFFTAG_YRESOLUTION,	(float) params.verticalRes());
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT,	RESUNIT_INCH);
    TIFFSetField(tif, TIFFTAG_SOFTWARE,		VERSION);
    TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION,	(const char*) tsi);
    char dateTime[24];
    time_t now = Sys::now();
    strftime(dateTime, sizeof (dateTime), "%Y:%m:%d %H:%M:%S", localtime(&now));
    TIFFSetField(tif, TIFFTAG_DATETIME,	    dateTime);
    TIFFSetField(tif, TIFFTAG_MAKE,	    (const char*) getManufacturer());
    TIFFSetField(tif, TIFFTAG_MODEL,	    (const char*) getModel());
    TIFFSetField(tif, TIFFTAG_HOSTCOMPUTER, (const char*) server.hostname);
}

/*
 * Setup the compression scheme and related tags.
 */
static void
setupCompression(TIFF* tif, u_int df, uint32 opts)
{
    switch (df) {
    case DF_2DMMR:
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX4);
	TIFFSetField(tif, TIFFTAG_GROUP4OPTIONS, opts);
	break;
    case DF_2DMRUNCOMP:
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX3);
	TIFFSetField(tif, TIFFTAG_GROUP3OPTIONS,
	    opts | GROUP3OPT_2DENCODING|GROUP3OPT_UNCOMPRESSED);
	break;
    case DF_2DMR:
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX3);
	TIFFSetField(tif, TIFFTAG_GROUP3OPTIONS, opts | GROUP3OPT_2DENCODING);
	break;
    case DF_1DMR:
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX3);
	TIFFSetField(tif, TIFFTAG_GROUP3OPTIONS, opts &~ GROUP3OPT_2DENCODING);
	break;
    }
}

/*
 * Write a strip of decoded data to the receive file.
 */
void
FaxModem::flushEncodedData(TIFF* tif, tstrip_t strip, u_char* buf, u_int cc)
{
    // NB: must update ImageLength for each new strip
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, recvEOLCount);
    if (TIFFWriteEncodedStrip(tif, strip, buf, cc) == -1)
	serverTrace("RECV: %s: write error", TIFFFileName(tif));
}

/*
 * Write a strip of raw data to the receive file.
 */
void
FaxModem::flushRawData(TIFF* tif, tstrip_t strip, u_char* buf, u_int cc)
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
FaxModem::checkQuality()
{
    return (conf.percentGoodLines != 0 && conf.maxConsecutiveBadLines != 0);
}

/*
 * Check the statistics accumulated during a page recived
 * against the configuration parameters and return an
 * indication of whether or not the page quality is acceptable.
 */
bool
FaxModem::isQualityOK(const Class2Params& params)
{
    if (conf.percentGoodLines != 0 && recvEOLCount != 0) {
	u_long percent = 100 * (recvEOLCount - recvBadLineCount) / recvEOLCount;
	if (percent < conf.percentGoodLines) {
	    serverTrace("RECV: REJECT page quality, %u%% good lines (%u%% required)",
		percent, conf.percentGoodLines);
	    return (false);
	}
    }
    u_int cblc = conf.maxConsecutiveBadLines;
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

/*
 * Return the next decoded byte of page data.
 */
int
FaxModem::nextByte()
{
    int b;
    if (bytePending & 0x100) {
	b = bytePending & 0xff;
	bytePending = 0;
    } else {
	b = getModemDataChar();
	if (b == EOF)
	    raiseEOF();
    }
    if (b == DLE) {
	switch (b = getModemDataChar()) {
	case EOF: raiseEOF();
	case ETX: raiseRTC();
	case DLE: break;		// <DLE><DLE> -> <DLE>
	default:
	    bytePending = b | 0x100;
	    b = DLE;
	    break;
	}
    }
    b = getBitmap()[b];
    if (recvBuf)			// record raw data
	recvBuf->put(b);
    return (b);
}

u_long FaxModem::getRecvEOLCount() const
    { return recvEOLCount; }
u_long FaxModem::getRecvBadLineCount() const
    { return recvBadLineCount; }
u_long FaxModem::getRecvConsecutiveBadLineCount() const
    { return recvConsecutiveBadLineCount; }

/*
 * Trace a protocol-receive related activity.
 */
void
FaxModem::recvTrace(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    static const fxStr recv("RECV: ");
    server.vtraceStatus(FAXTRACE_PROTOCOL, recv | fmt, ap);
    va_end(ap);
}

/*
 * Note an invalid G3 code word.
 */
void
FaxModem::invalidCode(const char* type, int x)
{
    if (!seenRTC())
	copyQualityTrace("Invalid %s code word, row %u, x %d",
	    type, getReferenceRow(), x);
}

/*
 * Note a row decode that gives the wrong pixel count.
 */
void
FaxModem::badPixelCount(const char* type, int got, int expected)
{
    if (!seenRTC())
	copyQualityTrace("Bad %s pixel count, row %u, got %d, expected %d",
	    type, getReferenceRow(), got, expected);
}

void
FaxModem::badDecodingState(const char* type, int x)
{
    copyQualityTrace("Panic, bad %s decoding state, row %u, x %d",
	type, getReferenceRow(), x);
}

/*
 * Trace a copy quality-reated activity.
 */
void
FaxModem::copyQualityTrace(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    static const fxStr cq("RECV/CQ: ");
    server.vtraceStatus(FAXTRACE_COPYQUALITY, cq | fmt, ap);
    va_end(ap);
}
