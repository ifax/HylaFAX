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

#define	RCVBUFSIZ	(32*1024)		// XXX

static	void setupCompression(TIFF*, u_int, uint32);

void
FaxModem::setupStartPage(TIFF* tif, const Class2Params& params)
{
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
		if (recvRow + rowSize > &buf[RCVBUFSIZ]) {
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
	setupStartPage(tif, params);
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
    if (params.df == DF_JPEG_COLOR || params.df == DF_JPEG_GREY) {
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE,	8);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC,		PHOTOMETRIC_CIELAB);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG,		PLANARCONFIG_CONTIG);
	// libtiff requires IMAGELENGTH to be set before SAMPLESPERPIXEL, 
	// or StripOffsets and StripByteCounts will have SAMPLESPERPIXEL values
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH,		2000);	// we adjust this later
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL,	params.df == DF_JPEG_GREY ? 1 : 3);
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
setupCompression(TIFF* tif, u_int df, uint32 opts)
{
    switch (df) {
    case DF_JPEG_GREY:
    case DF_JPEG_COLOR:
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_JPEG);
	break;
    case DF_JBIG_BASIC:
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

    char cbuf[4];		// size of the page count signal
    if (seq & 1) {		// first block
	switch (params.df) {
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
				exit(0);
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

	    case DF_JBIG_BASIC:
		{
		    setupStartPage(tif, params);
		    /*
		     * Parse JBIG BIH to get the image length.
		     *
		     * See T.82 6.6.2
		     * These three integers are coded most significant byte first. In
		     * other words, XD is the sum of 256^3 times the fifth byte in BIH, 256^2
		     * times the sixth byte, 256 times the seventh byte, and the eighth byte.
		     */
		    u_long framelength = 0;
		    //Yd is byte 9, 10, 11, and 12
		    framelength = 256*256*256*buf[9-1];
		    framelength += 256*256*buf[10-1];
		    framelength += 256*buf[11-1];
		    framelength += buf[12-1];

		    protoTrace("RECV: Yd field in BIH is %d", framelength);
		    /*
		     * Senders commonly use 0xFFFF and 0xFFFFFFFF as empty fill.  We ignore such large 
		     * values because if we use them and no NEWLEN marker is received, then the page
		     * length causes problems for viewers (specifically libtiff).
		     */
		    if (framelength < 65535 && framelength > recvEOLCount) recvEOLCount = framelength;
		}
		break;

	    case DF_JPEG_GREY:
	    case DF_JPEG_COLOR:
		recvEOLCount = 0;
		recvRow = (u_char*) malloc(1024*1000);    // 1M should do it?
		fxAssert(recvRow != NULL, "page buffering error (JPEG page).");
		recvPageStart = recvRow;
		setupStartPage(tif, params);
		break;
	}
    }
    switch (params.df) {
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

	case DF_JBIG_BASIC:
	    //search for NEWLEN Marker Segment in JBIG Bi-Level Image Data
	    {
		u_long framelength = 0;
		for (u_int i = 0; i < cc-2; i++) {
		    if (buf[i] == 0xFF && buf[i+1] == 0x05) {
			framelength = 256*256*256*buf[i+2];
			framelength += 256*256*buf[i+3];   
			framelength += 256*buf[i+4];
			framelength += buf[i+5];

			protoTrace("RECV: Found NEWLEN Marker Segment in BID, Yd = %d", framelength);
			if (framelength < 65535) recvEOLCount = framelength;
		    }
		}
	    }
	    break;

	case DF_JPEG_GREY:
	case DF_JPEG_COLOR:
	    {
		u_long framelength = 0;
		u_long framewidth = 0;
		for (u_int i = 0; i < cc-2; i++) {
		    if (buf[i] == 0xFF && buf[i+1] == 0xC0) {
			framelength = 256*buf[i+5];
			framelength += buf[i+6];
			framewidth = 256*buf[i+7];
			framewidth += buf[i+8];
			protoTrace("RECV: Found Start of Frame (SOF) Marker, size: %lu x %lu", framewidth, framelength);
			if (framelength < 65535 && framelength > recvEOLCount) recvEOLCount = framelength;
		    }
		    if (buf[i] == 0xFF && buf[i+1] == 0xDC) {
			framelength = 256*buf[i+4];
			framelength += buf[i+5];
			protoTrace("RECV: Found Define Number of Lines (DNL) Marker, lines: %lu", framelength);
			if (framelength < 65535) recvEOLCount = framelength;
		    }
		}
	    }
	    break;
    }
    if (params.df != DF_JPEG_GREY && params.df != DF_JPEG_COLOR) {
	flushRawData(tif, 0, (const u_char*) buf, cc);
    } else {
	memcpy(recvRow, (const char*) buf, cc);
	recvRow += cc;
    }
    if (seq & 2 && !recvEOLCount && (params.df ==  DF_JPEG_GREY || params.df == DF_JPEG_COLOR)) {
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
	    case VR_FINE:	res = 770;  break;
	    case VR_200X100:	res = 394; break;
	    case VR_200X200:	res = 787; break;
	    case VR_200X400:    res = 1575; break;
	    case VR_300X300:    res = 1181; break;
	    default:		res = 1540; break;	// VR_R16, VR_R8
	}
	recvEOLCount = (u_long) ((len * res) / 100);
	protoTrace("RECV: assumed image length of %lu lines", recvEOLCount);
    }
    if (seq & 2 && (params.df == DF_JPEG_GREY || params.df == DF_JPEG_COLOR)) {
	/*
	 * DNL markers generally are not usable in TIFF files.  Furthermore,
	 * many TIFF viewers cannot use them, either.  So, we go back 
	 * through the strip and replace any "zero" image length attributes
	 * of any SOF markers with recvEOLCount.  Perhaps we should strip
         * out the DNL marker entirely, but leaving it in seems harmless.
	 */
	u_long pagesize = recvRow - recvPageStart;
	recvRow = recvPageStart;
	for (uint32 i = 0; i < pagesize; i++) {
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
