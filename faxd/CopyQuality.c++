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
#include "config.h"
#include "Sys.h"
#include "FaxModem.h"
#include "FaxTrace.h"
#include "ModemConfig.h"
#include "StackBuffer.h"
#include "FaxServer.h"

#include <ctype.h>

#define	RCVBUFSIZ	(32*1024)		// XXX

static	void setupCompression(TIFF*, u_int, u_int, uint32);

void
FaxModem::setupStartPage(TIFF* tif, const Class2Params& params)
{
    /*
     * Not doing copy quality checking so compression and
     * Group3Options must reflect the negotiated session
     * parameters for the forthcoming page data.
     */
    setupCompression(tif, params.df, params.jp, group3opts);
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
}

void
FaxModem::recvEndPage(TIFF* tif, const Class2Params& params)
{
    /*
     * FAXRECVPARAMS is limited to a 32-bit value, and as that is quite
     * limited in comparison to all possible fax parameters, FAXDCS is
     * intended to be used to discern most fax parameters.  The DCS
     * signal, however, does not contain bitrate information when V.34-Fax
     * is used, so tiffinfo, for example, will use FAXDCS for all fax
     * parameters except for bitrate which comes from FAXRECVPARAMS.
     *
     * As FAXDCS is more recent in libtiff than is FAXRECVPARAMS some
     * installations may not be able to use FAXDCS, in which case those
     * installations will be limited to the 32-bit restraints.
     */
    // NB: must set these after compression tag
#ifdef TIFFTAG_FAXRECVPARAMS
    TIFFSetField(tif, TIFFTAG_FAXRECVPARAMS,	(uint32) params.encode());
#endif
#ifdef TIFFTAG_FAXDCS
    FaxParams pageparams = FaxParams(params);
    fxStr faxdcs = "";
    pageparams.asciiEncode(faxdcs);
    TIFFSetField(tif, TIFFTAG_FAXDCS, (const char*) faxdcs);
#endif
#ifdef TIFFTAG_FAXSUBADDRESS
    if (sub != "")
	TIFFSetField(tif, TIFFTAG_FAXSUBADDRESS,	(const char*) sub);
#endif
#ifdef TIFFTAG_FAXRECVTIME
    TIFFSetField(tif, TIFFTAG_FAXRECVTIME,
	(uint32) server.setPageTransferTime());
#endif
}

void
FaxModem::resetLineCounts()
{
    recvEOLCount = 0;				// count of EOL codes
    recvBadLineCount = 0;			// rows with a decoding error
    recvConsecutiveBadLineCount = 0;		// max consecutive bad rows
    linesWereA4Width = 0;			// rows that measured 1728 pels
}

void
FaxModem::initializeDecoder(const Class2Params& params)
{
    setupDecoder(recvFillOrder, params.is2D(), (params.df == DF_2DMMR));

    u_int rowpixels = params.pageWidth();	// NB: assume rowpixels <= 4864
    tiff_runlen_t runs[2*4864];			// run arrays for cur+ref rows
    setRuns(runs, runs+4864, rowpixels);
    setIsECM(false);
    resetLineCounts();
}

/*
 * Receive Phase C data with or without copy
 * quality checking and erroneous row fixup.
 */
bool
FaxModem::recvPageDLEData(TIFF* tif, bool checkQuality,
    const Class2Params& params, fxStr& emsg)
{
    initializeDecoder(params);
    u_int rowpixels = params.pageWidth();	// NB: assume rowpixels <= 4864
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
    if (checkQuality && params.ec == EC_DISABLE) {
	/*
	 * Receive a page of data w/ copy quality checking.
	 * Note that since we decode and re-encode we can
	 * trivially do transcoding by just changing the
	 * TIFF tag values setup for each received page.  This
	 * may however be too much work for some CPUs.
	 *
	 * Copy quality checking is superfluous when using ECM.
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
	setupCompression(tif, df, 0, 0);		// XXX maybe zero-fill EOLs?
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

	u_char* curGood = (u_char*) malloc((size_t) rowSize);
	memset(curGood, 0, (size_t) rowSize);	// clear to white
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
		decodedPixels = rowpixels;	// assume no error
		bool decodeOK = decodeRow(recvRow, rowpixels);
		if (seenRTC())			// seen RTC, flush everything
		    continue;
		if (decodeOK) {
		    if (lastRowBad) {		// reset run statistics
			lastRowBad = false;
			if (cblc > recvConsecutiveBadLineCount)
			    recvConsecutiveBadLineCount = cblc;
			cblc = 0;
		    }
		} else {
		    /*
		     * Our copy-quality correction mechanism involves decoding
		     * and re-encoding each line (as is being done), and if a
		     * line is decoded with error (short pixel count) then the
		     * remaining pixels are carried-over from the row above.
		     * This is done instead of replacing the corrupt row with
		     * the last good row to avoid severe data loss where the
		     * decoded data is still valid (only incomplete), and this
		     * is done instead of replacing the missing data with white
		     * to avoid visual disconnect of blackened areas.
		     */
		    if ((u_int) decodedPixels < rowpixels) {
			u_int filledchars = (decodedPixels + 7) / 8;
			u_short rembits = decodedPixels % 8;
			memcpy(recvRow + filledchars, curGood + filledchars, rowSize - filledchars);
			if (rembits) {
			    // now deal with the transitional character
			    u_char remmask = 0;
			    for (u_short bit = 0; bit < 8; bit++) {
				remmask<<=1;
				if (bit < rembits) remmask |= 1;
			    }
			    recvRow[filledchars-1] = (recvRow[filledchars-1] & remmask) | (curGood[filledchars-1] & ~remmask);
			}
		    } else if ((u_int) decodedPixels >= rowpixels) {
			/*
			 * If we get a long pixel count, then our correction mechanism
			 * involves trimming horizontal "streaks" at the end of the
			 * scanline and replacing that data with the corresponding data
			 * from the preceding scanline.  If the streak doesn't exceed
			 * rowpixels, then copy quality checking won't catch it, so
			 * streaks can still appear, but this does well at getting most
			 * of them in practice.
			 */
			u_int pos = rowSize - 1;
			u_char streak = recvRow[pos];
			if (streak == 0xFF || streak == 0x00) {
			    while (pos && recvRow[pos] == streak) {
				recvRow[pos] = curGood[pos];
				pos--;
			    }
			}
		    }
		    /*
		     * Some senders signal the wrong page width in DCS, such as A3,
		     * and then proceed to deliver A4 page image data.  The copy
		     * quality correction mechanism will allow the image data to get
		     * through, with white space on the right, but it's better to
		     * avoid sending RTN, as it will only cause more of the same or
		     * will cause the sender to disconnect.  So if the we seem to
		     * be getting A4 page data when something else was negotiated
		     * we don't count those lines as bad.
		     */
		    linesWereA4Width += decodedPixels == 1728 ? 1 : 0;
		    if (decodedPixels != 1728 || linesWereA4Width < ((recvEOLCount + 1) * 95 / 100)) {
			recvBadLineCount++;
			cblc++;
			lastRowBad = true;
		    }
		}
		if (decodedPixels) memcpy(curGood, recvRow, (size_t) rowSize);
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
		if (recvRow + rowSize > &buf[RCVBUFSIZ]) {
		    flushEncodedData(tif, recvStrip++, buf, recvRow - buf);
		    recvRow = buf;
		}
	    }
	}
	free(curGood);
	if (seenRTC()) {			// RTC found in data stream
	    /*
	     * Adjust everything to reflect the location
	     * at which RTC was found in the data stream.
	     */
	    copyQualityTrace("Adjusting for RTC found at row %u", getRTCRow());
						// # EOL's in recognized RTC
	    u_int n = (u_int)(recvEOLCount - getRTCRow());
	    if (cblc - n  > recvConsecutiveBadLineCount)
		recvConsecutiveBadLineCount = cblc - n;
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
	    if (cblc > recvConsecutiveBadLineCount)
		recvConsecutiveBadLineCount = cblc;
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
	setupStartPage(tif, params);
	if (params.df == DF_JBIG || params.jp == JP_GREY || params.jp == JP_COLOR) {
	    if (params.df != DF_JBIG) {
		/*
		 * In the case of JPEG we have to buffer it all, alter SOF
		 * after seeing DNL, and then write it to disk... because
		 * most JPEG parsers won't know how to handle DNL as it's
		 * rather fax-specific.
		 */
		recvEOLCount = 0;
		recvRow = (u_char*) malloc(1024*1000);    // 1M should do it?
		fxAssert(recvRow != NULL, "page buffering error (JPEG page).");
		recvPageStart = recvRow;
	    }
	    parserCount[0] = 0;
	    parserCount[1] = 0;
	    parserCount[2] = 0;
	    memset(parserBuf, 0, 16);
	    int cc = 0, c;
	    bool fin = false;
	    if (params.df == DF_JBIG) {
		for (; cc < 20; cc++) {
		    c = getModemChar(30000);
		    if (c == EOF || wasTimeout()) {
			fin = true;
			break;
		    }
		    if (c == DLE) {
			c = getModemChar(30000);
			if (c == EOF || c == ETX || wasTimeout()) {
			    fin = true;
			    break;
			}
		    }
		    buf[cc] = c;
		}
		parseJBIGBIH(buf);
		flushRawData(tif, 0, (const u_char*) buf, cc);
	    }
	    if (!fin) {
		do {
		    cc = 0;
		    do {
			c = getModemChar(30000);
			if (c == EOF || wasTimeout()) {
			    fin = true;
			    break;
			}
			if (c == DLE) {
			    c = getModemChar(30000);
			    if (c == EOF || c == ETX || wasTimeout()) {
				fin = true;
				break;
			    }
			}
			if (params.df == DF_JBIG) parseJBIGStream(c);
			else parseJPEGStream(c);
			buf[cc++] = c;
		    } while (cc < RCVBUFSIZ && !fin);
		    if (params.df == DF_JBIG) {
			flushRawData(tif, 0, (const u_char*) buf, cc);
		    } else {
			memcpy(recvRow, (const char*) buf, cc);
			recvRow += cc;
		    }
		} while (!fin);
		if (params.df == DF_JBIG) clearSDNORMCount();
		else fixupJPEG(tif);
	    }
	    recvEndPage(tif, params);
	    return (true);
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
		if (n >= RCVBUFSIZ)
		    flushRawData(tif, 0, (const u_char*) raw, n);
		else {
		    memcpy(recvRow, (const char*) raw, n);
		    recvRow += n;
		}
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
	    if (params.df == DF_2DMMR) {
		copyQualityTrace("Adjusting for EOFB at row %u", getRTCRow());
	    } else {
		copyQualityTrace("Adjusting for RTC found at row %u", getRTCRow());
	    }
	    recvEOLCount = getRTCRow();
	}
    }
    recvEndPage(tif, params);
    return (true);
}

/*
 * Setup "stock TIFF tags" in preparation for receiving a page of data.
 */
void
FaxModem::recvSetupTIFF(TIFF* tif, long, int fillOrder, const fxStr& id)
{
    TIFFSetField(tif, TIFFTAG_SUBFILETYPE,	FILETYPE_PAGE);
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH,	(uint32) params.pageWidth());
    if (params.jp == JP_COLOR || params.jp == JP_GREY) {
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE,	8);
#ifdef PHOTOMETRIC_ITULAB
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC,		PHOTOMETRIC_ITULAB);
#else
	printf("Attempt to save JPEG Grey/Colour data without PHOTOMETRIC_ITULAB support.  This should not happen.\n");
#endif
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE,	8);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG,		PLANARCONFIG_CONTIG);
	// libtiff requires IMAGELENGTH to be set before SAMPLESPERPIXEL, 
	// or StripOffsets and StripByteCounts will have SAMPLESPERPIXEL values
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH,		2000);	// we adjust this later
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL,	params.jp == JP_GREY ? 1 : 3);
    } else {
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE,	1);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC,		PHOTOMETRIC_MINISWHITE);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL,	1);
	TIFFSetField(tif, TIFFTAG_FILLORDER,		(uint16) fillOrder);
    }
    TIFFSetField(tif, TIFFTAG_ORIENTATION,	ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP,	(uint32) -1);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG,	PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_XRESOLUTION,	(float) params.horizontalRes());
    TIFFSetField(tif, TIFFTAG_YRESOLUTION,	(float) params.verticalRes());
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT,	RESUNIT_INCH);
    TIFFSetField(tif, TIFFTAG_SOFTWARE,		HYLAFAX_VERSION);
    TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION,	(const char*) id);
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
setupCompression(TIFF* tif, u_int df, u_int jp, uint32 opts)
{
    u_int dataform = df + (jp ? jp + 4 : 0);
    switch (dataform) {
	case JP_GREY+4:
	case JP_COLOR+4:
	    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_JPEG);
	    break;
	case DF_JBIG:
	    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_JBIG);
	    break;
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
	case DF_1DMH:
	    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX3);
	    TIFFSetField(tif, TIFFTAG_GROUP3OPTIONS, opts &~ GROUP3OPT_2DENCODING);
	    break;
    }
}

/*
 * Write a strip of decoded data to the receive file.
 */
void
FaxModem::flushEncodedData(TIFF* tif, tstrip_t strip, const u_char* buf, u_int cc)
{
    // NB: must update ImageLength for each new strip
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, recvEOLCount);
    if (TIFFWriteEncodedStrip(tif, strip, (tdata_t)buf, cc) == -1)
	serverTrace("RECV: %s: write error", TIFFFileName(tif));
}

/*
 * Write a strip of raw data to the receive file.
 */
void
FaxModem::flushRawData(TIFF* tif, tstrip_t strip, const u_char* buf, u_int cc)
{
    recvTrace("%u bytes of data, %lu total lines", cc, recvEOLCount);
    if (TIFFWriteRawStrip(tif, strip, (tdata_t)buf, cc) == -1)
	serverTrace("RECV: %s: write error", TIFFFileName(tif));
}

void
FaxModem::clearSDNORMCount()
{
    if (parserCount[1]) {
	copyQualityTrace("Found %d SDNORM Marker Segments in BID", parserCount[1]);
	parserCount[1] = 0;
    }
}

void
FaxModem::parseJBIGStream(u_char c)
{
    /*
     * parserCount[n]
     *   n = 0, bytes since last marker
     *   n = 1, SDNORM marker counter
     *   n = 2, framelength bypass countdown
     */
    parserCount[0]++;
    if (parserCount[2]) {
	// just skipping bytes
	parserCount[2]--;
	return;
    }
    for (u_short i = 15; i > 0; i--) parserBuf[i] = parserBuf[i-1];
    parserBuf[0] = c;
    u_long framelength = 0;

    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] == 0x04) {
	clearSDNORMCount();
	copyQualityTrace("Found ABORT Marker Segment in BID");
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 8 && parserBuf[7] == 0xFF && parserBuf[6] == 0x06) {
	clearSDNORMCount();
	copyQualityTrace("Found ATMOVE Marker Segment in BID, Yat %d, tx %d, ty %d",
	    256*256*256*parserBuf[5]+256*256*parserBuf[4]+256*parserBuf[3]+parserBuf[2], parserBuf[1], parserBuf[0]);
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 6 && parserBuf[5] == 0xFF && parserBuf[4] == 0x07) {
	clearSDNORMCount();
	parserCount[2] = 256*256*256*parserBuf[3]+256*256*parserBuf[2]+256*parserBuf[1]+parserBuf[0];
	copyQualityTrace("Found COMMENT Marker Segment in BID");
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 6 && parserBuf[5] == 0xFF && parserBuf[4] == 0x05) {
	clearSDNORMCount();
	framelength = 256*256*256*parserBuf[3];
	framelength += 256*256*parserBuf[2];
	framelength += 256*parserBuf[1];
	framelength += parserBuf[0];
	copyQualityTrace("Found NEWLEN Marker Segment in BID, Yd = %d", framelength);
	// T.82: "The new Yd shall never be greater than the original"
	if (framelength < 65535 && (!recvEOLCount || framelength < recvEOLCount)) recvEOLCount = framelength;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] == 0x01) {
	clearSDNORMCount();
	copyQualityTrace("Found RESERVE Marker Segment in BID");
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] == 0x02) {
	parserCount[1]++;	// SDNORM
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] == 0x03) {
	clearSDNORMCount();
	copyQualityTrace("Found SDRST Marker Segment in BID");
	parserCount[0] = 0;
	return;
    }
}

void
FaxModem::parseJBIGBIH(u_char* buf)
{
    copyQualityTrace("BIH: Dl %d, D %d, P %d, fill %d", buf[0], buf[1], buf[2], buf[3]);
    /*
     * Parse JBIG BIH to get the image length.
     *
     * See T.82 6.6.2
     * These three integers are coded most significant byte first. In
     * other words, XD is the sum of 256^3 times the fifth byte in BIH, 256^2
     * times the sixth byte, 256 times the seventh byte, and the eighth byte.
     */
    u_long framelength = 0;
    framelength = 256*256*256*buf[8];
    framelength += 256*256*buf[9];
    framelength += 256*buf[10];
    framelength += buf[11];
    /*
     * Senders commonly use 0xFFFF and 0xFFFFFFFF as "maximum Yd".  We ignore such large
     * values because if we use them and no NEWLEN marker is received, then the page
     * length causes problems for viewers (specifically libtiff).
     */
    if (framelength < 65535 && framelength > recvEOLCount) recvEOLCount = framelength;
    copyQualityTrace("BIH: Xd %d, Yd %d, L0 %d, Mx %d, My %d",
	256*256*256*buf[4]+256*256*buf[5]+256*buf[6]+buf[7],
	framelength,
	256*256*256*buf[12]+256*256*buf[13]+256*buf[14]+buf[15],
	buf[16], buf[17]);
    copyQualityTrace("BIH: fill %d, HITOLO %d, SEQ %d, ILEAVE %d, SMID %d",
	(buf[18]&0xF0)>>4, (buf[18]&0x8)>>3, (buf[18]&0x4)>>2, (buf[18]&0x2)>>1, buf[18]&0x1);
    copyQualityTrace("BIH: fill %d, LRLTWO %d, VLENGTH %d, TPDON %d, TPBON %d, DPON %d, DPPRIV %u, DPLAST %u",
	(buf[19]&0x80)>>7, (buf[19]&0x40)>>6, (buf[19]&0x20)>>5, (buf[19]&0x10)>>4, (buf[19]&0x8)>>3,
	(buf[19]&0x4)>>2, (buf[19]&0x2)>>1, buf[19]&0x1);
}

void
FaxModem::parseJPEGStream(u_char c)
{
    /*
     * parserCount[n]
     *   n = 0, bytes since last marker
     *   n = 1, framelength bypass countdown
     */
    parserCount[0]++;
    if (parserCount[1]) {
	// just skipping bytes
	parserCount[1]--;
	return;
    }
    for (u_short i = 15; i > 0; i--) parserBuf[i] = parserBuf[i-1];
    parserBuf[0] = c;
    u_long framelength = 0, framewidth = 0, fsize = 0;

    if (parserCount[0] >= 9 && parserBuf[8] == 0xFF && parserBuf[7] >= 0xC0 && parserBuf[7] <= 0xCF
	&& parserBuf[7] != 0xC4 && parserBuf[7] != 0xC8 && parserBuf[7] != 0xCC) {
	u_short type = parserBuf[7] - 0xC0;
	fsize = 256*parserBuf[6];
	fsize += parserBuf[5];
	framelength = 256*parserBuf[3];
	framelength += parserBuf[2];
	framewidth = 256*parserBuf[1];
	framewidth += parserBuf[0];
	copyQualityTrace("Found Start of Frame (SOF%u) Marker, size: %lu x %lu", type, framewidth, framelength);
	if (framelength < 65535 && framelength > recvEOLCount) recvEOLCount = framelength;
	parserCount[1] = fsize - 9;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] == 0xC8) {
	copyQualityTrace("Found JPEG Extensions (JPG) Marker");
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 4 && parserBuf[3] == 0xFF && parserBuf[2] == 0xC4) {
	fsize = 256*parserBuf[1];
	fsize += parserBuf[0];
	copyQualityTrace("Found Define Huffman Tables (DHT) Marker");
	parserCount[1] = fsize - 4;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 4 && parserBuf[3] == 0xFF && parserBuf[2] == 0xCC) {
	fsize = 256*parserBuf[1];
	fsize += parserBuf[0];
	copyQualityTrace("Found Define Arithmatic Coding Conditionings (DAC) Marker");
	parserCount[1] = fsize - 4;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] >= 0xD0 && parserBuf[0] <= 0xD7) {
	copyQualityTrace("Found Restart (RST) Marker");
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] == 0xD8) {
	copyQualityTrace("Found Start of Image (SOI) Marker");
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] == 0xD9) {
	copyQualityTrace("Found End of Image (EOI) Marker");
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 4 && parserBuf[3] == 0xFF && parserBuf[2] == 0xDA) {
	fsize = 256*parserBuf[1];
	fsize += parserBuf[0];
	copyQualityTrace("Found Start of Scan (SOS) Marker");
	parserCount[1] = fsize - 4;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 4 && parserBuf[3] == 0xFF && parserBuf[2] == 0xDB) {
	fsize = 256*parserBuf[1];
	fsize += parserBuf[0];
	copyQualityTrace("Found Define Quantization Tables (DQT) Marker");
	parserCount[1] = fsize - 4;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 6 && parserBuf[5] == 0xFF && parserBuf[4] == 0xDC) {
	fsize = 256*parserBuf[3];
	fsize += parserBuf[2];
	framelength = 256*parserBuf[1];
	framelength += parserBuf[0];
	copyQualityTrace("Found Define Number of Lines (DNL) Marker, lines: %lu", framelength);
	if (framelength < 65535) recvEOLCount = framelength;
	parserCount[1] = fsize - 6;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 4 && parserBuf[3] == 0xFF && parserBuf[2] == 0xDD) {
	fsize = 256*parserBuf[1];
	fsize += parserBuf[0];
	copyQualityTrace("Found Define Restart Interval (DRI) Marker");
	parserCount[1] = fsize - 4;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] == 0xDE) {
	copyQualityTrace("Found Define Hierarchial Progression (DHP) Marker");
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 4 && parserBuf[3] == 0xFF && parserBuf[2] == 0xDF) {
	fsize = 256*parserBuf[1];
	fsize += parserBuf[0];
	copyQualityTrace("Found Expand Reference Components (EXP) Marker");
	parserCount[1] = fsize - 4;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 4 && parserBuf[3] == 0xFF && parserBuf[2] >= 0xE0 && parserBuf[2] <= 0xEF) {
	u_short type = parserBuf[2] - 0xE0;
	fsize = 256*parserBuf[1];
	fsize += parserBuf[0];
	copyQualityTrace("Found Application Segment (APP%u) Marker", type);
	parserCount[1] = fsize - 4;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] >= 0xF0 && parserBuf[0] <= 0xFD) {
	u_short type = parserBuf[0] - 0xF0;
	copyQualityTrace("Found JPEG Extension (JPG%u) Marker", type);
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 4 && parserBuf[3] == 0xFF && parserBuf[2] == 0xFE) {
	fsize = 256*parserBuf[1];
	fsize += parserBuf[0];
	copyQualityTrace("Found Comment (COM) Marker");
	parserCount[1] = fsize - 4;
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] == 0x01) {
	copyQualityTrace("Found Temporary Private Use (TEM) Marker");
	parserCount[0] = 0;
	return;
    }
    if (parserCount[0] >= 2 && parserBuf[1] == 0xFF && parserBuf[0] >= 0x02 && parserBuf[0] <= 0xBF) {
	copyQualityTrace("Found Reserved (RES) Marker 0x%X", parserBuf[0]);
	parserCount[0] = 0;
	return;
    }
}

void
FaxModem::fixupJPEG(TIFF* tif)
{
    if (!recvEOLCount) {
	/*
	 * We didn't detect an image length marker (DNL/NEWLEN).  So
	 * we use the session parameters to guess at one, and we hope that
	 * the eventual viewing decoder can cope with things if the data
	 * is short.
	 *
	 * This approach doesn't seem to work with JBIG, so for now we only do it with JPEG.
	 */
	u_int len, res;
	switch (params.ln) {
	    case LN_A4:	len = 297; break;
	    default:	len = 364; break;	// LN_INF, LN_B4
	}
	switch (params.vr) {	// values in mm/100 to avoid floats
	    case VR_NORMAL:	res = 385; break;
	    case VR_FINE:	res = 770; break;
	    case VR_200X100:	res = 394; break;
	    case VR_200X200:	res = 787; break;
	    case VR_200X400:	res = 1575; break;
	    case VR_300X300:	res = 1181; break;
	    default:		res = 1540; break;	// VR_R16, VR_R8
	}
	recvEOLCount = (u_long) ((len * res) / 100);
	protoTrace("RECV: assumed image length of %lu lines", recvEOLCount);
    }
    /*
     * DNL markers generally are not usable in TIFF files.  Furthermore,
     * many TIFF viewers cannot use them, either.  So, we go back
     * through the strip and replace any "zero" image length attributes
     * of any SOF markers with recvEOLCount.  Perhaps we should strip
     * out the DNL marker entirely, but leaving it in seems harmless.
     */
    u_long pagesize = recvRow - recvPageStart;
    recvRow = recvPageStart;
    for (uint32 i = 0; i < (pagesize - 6); i++) {
	if (recvRow[i] == 0xFF && recvRow[i+1] == 0xC0 && recvRow[i+5] == 0x00 && recvRow[i+6] == 0x00) {
	    recvRow[i+5] = recvEOLCount >> 8;
	    recvRow[i+6] = recvEOLCount & 0xFF;
	    protoTrace("RECV: fixing zero image frame length in SOF marker at byte %lu to %lu", i, recvEOLCount);
	}
    }
    if (TIFFWriteRawStrip(tif, 0, (tdata_t) recvRow, pagesize) == -1)
	serverTrace("RECV: %s: write error", TIFFFileName(tif));
    free(recvRow);
}

/*
 * In ECM mode the ECM module provides the line-counter.
 */
void
FaxModem::writeECMData(TIFF* tif, u_char* buf, u_int cc, const Class2Params& params, u_short seq)
{
    /*
     * Start a decoding child process to which the parent pipes the image data 
     * through a decoding pipe.  The child will return the line count back 
     * through a second counting pipe.  The fork is only executed on the first 
     * block, and the child exits after returning the line count after the last
     * block.  In-between blocks merely dump data into the pipe.
     */

    u_int dataform = params.df + (params.jp ? params.jp + 4 : 0);

    char cbuf[4];		// size of the page count signal
    if (seq & 1) {		// first block
	switch (dataform) {
	    case DF_1DMH:
	    case DF_2DMR:
	    case DF_2DMMR:
		{
		    decoderFd[1] = -1;
		    initializeDecoder(params);
		    setupStartPage(tif, params);
		    u_int rowpixels = params.pageWidth();	// NB: assume rowpixels <= 4864
		    recvBuf = NULL;				// just count lines, don't save it
		    if (pipe(decoderFd) >= 0 && pipe(counterFd) >= 0) {
			setDecoderFd(decoderFd[0]);
			decoderPid = fork();
			switch (decoderPid) {
			    case -1:	// error
				recvTrace("Could not fork decoding.");
				Sys::close(decoderFd[0]);
				Sys::close(decoderFd[1]);
				Sys::close(counterFd[0]);
				Sys::close(counterFd[1]);
				break;
			    case 0:		// child
				Sys::close(decoderFd[1]);
				Sys::close(counterFd[0]);
				setIsECM(true);	// point decoder to the pipe rather than the modem for data
				if (!EOFraised() && !RTCraised()) {
				    for (;;) {
					(void) decodeRow(NULL, rowpixels);
					if (seenRTC()) {
					    break;
					}
					recvEOLCount++;
				    }
				}
				if (seenRTC()) {		// RTC found in data stream
				    if (params.df == DF_2DMMR) {
					/*
					 * In the case of MMR, it's not really RTC, but EOFB.
					 * However, we don't actually *find* EOFB, but rather we
					 * wait for the first line decoding error and assume it
					 * to be EOFB.  This is safe because MMR data after a
					 * corrupted line is useless anyway.
					 */
					copyQualityTrace("Adjusting for EOFB at row %u", getRTCRow());
				    } else
					copyQualityTrace("Adjusting for RTC found at row %u", getRTCRow());
				    recvEOLCount = getRTCRow();
				}
				// write the line count to the pipe
				Sys::write(counterFd[1], (const char*) &recvEOLCount, sizeof(recvEOLCount));
				_exit(0);
			    default:	// parent
				Sys::close(decoderFd[0]);
				Sys::close(counterFd[1]);
				break;
			}
		    } else {
			recvTrace("Could not open decoding pipe.");
		    }
		}
		break;

	    case DF_JBIG:
		{
		    setupStartPage(tif, params);
		    parseJBIGBIH(buf);
		    parserCount[0] = 0;
		    parserCount[1] = 0;
		    parserCount[2] = 0;
		    memset(parserBuf, 0, 16);
		}
		break;

	    case JP_GREY+4:
	    case JP_COLOR+4:
		recvEOLCount = 0;
		recvRow = (u_char*) malloc(1024*1000);    // 1M should do it?
		fxAssert(recvRow != NULL, "page buffering error (JPEG page).");
		recvPageStart = recvRow;
		setupStartPage(tif, params);
		parserCount[0] = 0;
		parserCount[1] = 0;
		parserCount[2] = 0;
		memset(parserBuf, 0, 16);
		break;
	}
    }
    switch (dataform) {
	case DF_1DMH:
	case DF_2DMR:
	case DF_2DMMR:
	    {
		if (decoderFd[1] != -1) {	// only if pipe succeeded
		    for (u_int i = 0; i < cc; i++) {
			cbuf[0] = 0x00;		// data marker
			cbuf[1] = buf[i];
			Sys::write(decoderFd[1], cbuf, 2);
		    }
		}
		if (decoderFd[1] != -1 && seq & 2) {	// last block
		    cbuf[0] = 0xFF;		// this signals...
		    cbuf[1] = 0xFF;		// ... end of data
		    Sys::write(decoderFd[1], cbuf, 2);

		    // read the page count from the counter pipe
		    Sys::read(counterFd[0], (char*) &recvEOLCount, sizeof(recvEOLCount));
		    (void) Sys::waitpid(decoderPid);
		    Sys::close(decoderFd[1]);
		    Sys::close(counterFd[0]);
		}
	    }
	    break;

	case DF_JBIG:
	case JP_GREY+4:
	case JP_COLOR+4:
	    // search for Marker Segments in Image Data
	    {
		u_int i = 0;
		if ((dataform == DF_JBIG) && (seq & 1)) i += 20;		// skip BIH
		for (; i < cc; i++) {
		    if (dataform == DF_JBIG) parseJBIGStream(buf[i]);
		    else parseJPEGStream(buf[i]);
		}
		if (dataform == DF_JBIG) clearSDNORMCount();
	    }
	    break;
    }
    if (params.jp != JP_GREY && params.jp != JP_COLOR) {
	flushRawData(tif, 0, (const u_char*) buf, cc);
    } else {
	memcpy(recvRow, (const char*) buf, cc);
	recvRow += cc;
    }
    if (seq & 2 && (params.jp == JP_GREY || params.jp == JP_COLOR)) {
	fixupJPEG(tif);
    }
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
    if (recvEOLCount == 0) return (false);	// don't label null data as "good"
    return (true);
}

/*
 * Return the next decoded byte of page data.
 */
int
FaxModem::nextByte()
{
    int b;
    if (getIsECM()) {
	char buf[2];
	decoderFd[0] = getDecoderFd();
	while (Sys::read(decoderFd[0], buf, 2) < 1);
	if (((unsigned) buf[0] & 0xFF) == 0xFF) raiseEOF();
	b = (unsigned) buf[1] & 0xFF;
    } else {
	if (bytePending & 0x100) {
	    b = bytePending & 0xff;
	    bytePending = 0;
	} else {
	    b = getModemDataChar();
	    if (b == EOF) {
		raiseEOF();
	    }
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
    if (!seenRTC()) {
	copyQualityTrace("Bad %s pixel count, row %u, got %d, expected %d",
	    type, getReferenceRow(), got, expected);
	decodedPixels = got;
    }
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
