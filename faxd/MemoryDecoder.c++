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
 * Phase C data correction and page chopping support.
 */

#include "MemoryDecoder.h"
#include "G3Encoder.h"
#include "StackBuffer.h"
#include "config.h"

MemoryDecoder::MemoryDecoder(u_char* data, u_long n)
{
    bp = data;
    cc = n;
    endOfData = NULL;
    nblanks = 0;
    runs = NULL;
    rowBuf = NULL;
    rows = 0;
}

MemoryDecoder::MemoryDecoder(u_char* data, u_int wid, u_long n,
                             u_int order, bool twoDim, bool mmr)
{
    bp         = data;
    width      = wid;
    byteWidth  = howmany(width, 8);
    cc         = n;
    rows       = 0;
    
    fillorder  = order;
    is2D       = twoDim;
    isG4       = mmr;

    runs      = new tiff_runlen_t[2*width];      // run arrays for cur+ref rows
    rowBuf    = new u_char[byteWidth];
    setupDecoder(fillorder, is2D, isG4);
    setRuns(runs, runs+width, width);
}

MemoryDecoder::~MemoryDecoder()
{
    if (rowBuf)
	delete rowBuf;
    if (runs)
	delete runs;
}

int
MemoryDecoder::decodeNextByte()
{
    if (cc == 0)
        raiseRTC();                     // XXX don't need to recognize EOF
    cc--;
    return (*bp++);
}

void
MemoryDecoder::invalidCode(const char* type, int x)
{
    printf("Invalid %s code word, x %d\n", type, x);
}        
void
MemoryDecoder::badPixelCount(const char* type, int got, int expected)  
{
    if (!seenRTC())
	printf("Bad %s pixel count, got %d, expected %d\n",
	    type, got, expected);
}
void
MemoryDecoder::badDecodingState(const char* type, int x)
{
    printf("Panic, bad %s decoding state, x %d\n", type, x);
}

static bool
isBlank(tiff_runlen_t* runs, u_int rowpixels)
{
    u_int x = 0;
    for (;;) {
	if ((x += *runs++) >= rowpixels)
	    break;
	if (runs[0] != 0)
	    return (false);
	if ((x += *runs++) >= rowpixels)
	    break;
    }
    return (true);
}

void
MemoryDecoder::scanPageForBlanks(u_int fillorder, const Class2Params& params)
{
    setupDecoder(fillorder,  params.is2D(), (params.df == DF_2DMMR));
    u_int rowpixels = params.pageWidth();	// NB: assume rowpixels <= 4864
    tiff_runlen_t runs[2*4864];			// run arrays for cur+ref rows
    setRuns(runs, runs+4864, rowpixels);

    if (!RTCraised()) {
	/*
	 * Skip a 1" margin at the top of the page before
	 * scanning for trailing white space.  We do this
	 * to ensure that there is always enough space on
	 * the page to image a tag line and to satisfy a
	 * fax machine that is incapable of imaging to the
	 * full extent of the page.
	 */
	u_int topMargin = 1*98;			// 1" at 98 lpi
	switch (params.vr) {
	    case VR_FINE:
	    case VR_200X200:
		topMargin *= 2;			// 196 lpi =>'s twice as many
		break;
	    case VR_300X300:
		topMargin *= 3;
		break;
	    case VR_R8:
	    case VR_R16:
	    case VR_200X400:
		topMargin *= 4;
		break;
	}
	do {
	    (void) decodeRow(NULL, rowpixels);
	} while (--topMargin);
	/*
	 * Scan the remainder of the page data and calculate
	 * the number of blank lines at the bottom.
	 */
	for (;;) {
	    (void) decodeRow(NULL, rowpixels);
	    if (isBlank(lastRuns(), rowpixels)) {
		endOfData = bp;			// include one blank row
		nblanks = 0;
		do {
		    nblanks++;
		    (void) decodeRow(NULL, rowpixels);
		} while (isBlank(lastRuns(), rowpixels));
	    }
	}
    }
}

#ifdef roundup
#undef roundup
#endif
#define roundup(a,b)    ((((a)+((b)-1))/(b))*(b))

/*
 * TIFF Class F specs say:
 *
 * "As illustrated in FIGURE 1/T.4 in Recommendation T.4 (the Red
 * Book, page 20), facsimile documents begin with an EOL (which in
 * Class F is byte-aligned)..."
 *
 * This is wrong! "Byte-aligned" first EOL means extra zero bits
 * which are not allowed by T.4. Reencode first row to fix this
 * "byte-alignment".
 */
void MemoryDecoder::fixFirstEOL()
{
    fxStackBuffer result;
    G3Encoder enc(result);
    enc.setupEncoder(fillorder, is2D, isG4);
    
    memset(rowBuf, 0, byteWidth*sizeof(u_char)); // clear row to white
    if(!RTCraised()) {
        u_char* start = current();
        (void)decodeRow(rowBuf, width);
        /*
         * syncronize to the next EOL and calculate pointer to it
         * (see detailed explanation of look_ahead in encodeTagLine())
         */
        (void)isNextRow1D();
        u_int look_ahead = roundup(getPendingBits(),8) / 8;
        u_int decoded = current() - look_ahead - start;

        enc.encode(rowBuf, width, 1);
	enc.encoderCleanup();
        u_int encoded = result.getLength();
            
        while( encoded < decoded ){
            result.put((char) 0);
            encoded++;
        }
        if( encoded == decoded ){
            memcpy(start, (const char*)result, encoded);
        }
    }
}

/*
 * TIFF Class F specs say:
 *
 * "Aside from EOL's, TIFF Class F files contain only image data. This
 * means that the Return To Control sequence (RTC) is specifically
 * prohibited..."
 *
 * Nethertheless Ghostscript and possibly other TIFF Class F writers
 * append RTC or single EOL to the last encoded line. Remove them.
 */
u_char* MemoryDecoder::cutExtraRTC()
{
    /*
     * We expect RTC near the end of data and thus
     * do not check all image to save processing time.
     * It's safe because we will resync on the first 
     * encountered EOL.
     *
     * NB: We expect G3Decoder::data==0 and
     * G3Decoder::bit==0 (no data in the accumulator).
     * As we cannot explicitly clear the accumulator
     * (bit and data are private), cutExtraRTC()
     * should be called immediately after
     * MemoryDecoder() constructing.
     */
    const u_long CheckArea = 20;
    if( cc > CheckArea ){
        bp += (cc-CheckArea);
        cc = CheckArea;
    }
        
    endOfData = NULL;
    rows = 0;
    if(!RTCraised()) {
        /*
         * syncronize to the next EOL and calculate pointer to it
         * (see detailed explanation of look_ahead in encodeTagLine())
         */
        (void)isNextRow1D();
        u_int look_ahead = roundup(getPendingBits(),8) / 8;
        endOfData = current() - look_ahead;
        for (;;) {
            if( decodeRow(NULL, width) ){
                /*
                 * endOfData is now after last good row. Thus we correctly handle
                 * RTC, single EOL in the end, or no RTC/EOL at all
                 */
                endOfData = current();
            }
            if( seenRTC() )
                break;
	    rows++;
        }
    }
    return endOfData;
}

u_char* MemoryDecoder::cutExtraEOFB()
{
    /*
     * MMR requires us to decode the entire image...
     */
    endOfData = NULL;
    rows = 0;
    if(!RTCraised()) {
	endOfData = current();
        for (;;) {
            if( decodeRow(NULL, width) ){
                endOfData = current();
            }
            if( seenRTC() )
                break;
	    rows++;
        }
    }
    /*
     * The loop above will leave the endOfData pointer somewhere inside of EOFB.
     * Make sure that endOfData points to the last byte containing real image data.
     * So trim any whole bytes containing EOFB data.
     */
    if (seenRTC()) {
	bool trimmed;
	u_int searcharea;
	u_short i;
	do {
	    while (*(endOfData - 1) == 0x00) endOfData--;
	    searcharea =  (*(endOfData - 1) << 16) | (*(endOfData - 2) << 8) | *(endOfData - 3);
	    trimmed = false;
	    for (i = 0; i < 13; i++) {
		if (((searcharea >> i) & 0xFFF) == 0x800) {
		    endOfData--;
		    trimmed = true;
		    break;
		}
	    }
	} while (trimmed);
    }
    return endOfData;
}

u_char* MemoryDecoder::encodeTagLine(u_long* raster, u_int th, u_int slop)
{
    /*
     * Decode (and discard) the top part of the page where
     * the tag line is to be imaged.  Note that we assume
     * the strip of raw data has enough scanlines in it
     * to satisfy our needs (caller is responsible).
     *
     * ... and then...
     *
     * Encode the result according to the parameters of
     * the outgoing page.  Note that the encoded data is
     * written in the bit order of the page data since
     * it must be merged back with it below.
     */
    fxStackBuffer result;
    G3Encoder enc(result);
    enc.setupEncoder(fillorder, is2D, isG4);

    decode(NULL, width, th);		// discard decoded data
    if (!isG4) {
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
	u_int n;
	for (n = 0; n < 4 && !isNextRow1D(); n++)
	    decodeRow(NULL, width);
	th += n;			// compensate for discarded rows
	/*
	 * Things get tricky trying to identify the last byte in
	 * the decoded data that we want to replace.  The decoder
	 * must potentially look ahead to see the zeros that
	 * makeup the EOL that marks the end of the data we want
	 * to skip over.  Consequently current() must be
	 * adjusted by the look ahead, a factor of the number of
	 * bits pending in the G3 decoder's bit accumulator.
	 */
	u_int look_ahead = roundup(getPendingBits(),8) / 8;
	u_int decoded = current() - look_ahead - bp;
	enc.encode(raster, width, th);
	enc.encoderCleanup();
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
	if (encoded > slop + decoded)
	    encoded = slop + decoded;
	u_char* dst = bp + (int)(decoded-encoded);
	memcpy(dst, (const unsigned char*) result, encoded);
	return (dst);
    } else {
	u_char* refrow = new u_char[byteWidth*sizeof(u_char)];	// reference row
	memset(refrow, 0, byteWidth*sizeof(u_char));		// clear to white
	enc.encode(raster, width, th, (unsigned char*) refrow);
	/*
	 * refrow does not need to be altered now to match the 
	 * last line of raster because the raster contains MARGIN_BOT
	 * blank lines.
	 */
	delete raster;
	if (!RTCraised()) {
	    for (;;) {
		(void) decodeRow(rowBuf, width);
		if(seenRTC())
		    break;
		enc.encode(rowBuf, width, 1, (unsigned char*) refrow);
		memcpy(refrow, rowBuf, byteWidth*sizeof(u_char));
	    }
	}
	enc.encoderCleanup();
	cc = result.getLength();
	u_char* dst = new u_char[cc];
	memcpy(dst, (const unsigned char*) result, cc);
	return (dst);
    }
}

#ifdef HAVE_JBIG
extern "C" {
#include "jbig.h"
}
fxStackBuffer resultBuffer;

void bufferJBIGData(unsigned char *start, size_t len, void *file)
{
    resultBuffer.put((const char*) start, len);
}
#endif /* HAVE_JBIG */

u_char* MemoryDecoder::convertDataFormat(const Class2Params& params)
{
    /*
     * Convert data to the format specified in params.  The decoder has already
     * been set up, and we don't need to worry about decoder operation here.  
     * These params are for the encoder to use.
     */
    rows = 0;
    if (params.df <= DF_2DMMR) {
	fxStackBuffer result;
	G3Encoder enc(result);
	enc.setupEncoder(fillorder, params.is2D(), (params.df == DF_2DMMR));

	u_char* refrow = new u_char[byteWidth*sizeof(u_char)];	// reference row
	memset(refrow, 0, byteWidth*sizeof(u_char));		// clear to white

	/*
	 * For MR we encode a 1-D or 2-D line according to T.4 4.2.1.
	 * We understand that "standard" resolution means VR_NORMAL.
	 */
	u_short k = 0;

	if (!RTCraised()) {
	    for (;;) {
		(void) decodeRow(rowBuf, width);
		if(seenRTC())
		    break;
		rows++;
		// encode the line specific to the desired format
		if (params.df == DF_2DMMR) {
		    enc.encode(rowBuf, width, 1, (unsigned char*) refrow);
		} else if (params.df == DF_2DMR) {
		    if (k) {
			enc.encode(rowBuf, width, 1, (unsigned char*) refrow);	// 2-D
		    } else {
			enc.encode(rowBuf, width, 1);				// 1-D
			k = (params.vr == VR_NORMAL || params.vr == VR_200X100) ? 2 : 4;
		    }
		    k--;
		} else {	// DF_1DMH
		    enc.encode(rowBuf, width, 1);
		}
		memcpy(refrow, rowBuf, byteWidth*sizeof(u_char));
	    }
	}
	enc.encoderCleanup();
	cc = result.getLength();
	u_char* dst = new u_char[cc];
	memcpy(dst, (const unsigned char*) result, cc);
	return (dst);
    } else if (params.df == DF_JBIG) {
#ifdef HAVE_JBIG
	char* decodedrow = new char[byteWidth];
	fxStackBuffer raster;
	resultBuffer = raster;		// initialize resultBuffer
	if (!RTCraised()) {
	    for (;;) {
		(void) decodeRow(decodedrow, width);
		if(seenRTC())
		    break;
		raster.put(decodedrow, byteWidth*sizeof(u_char));
		rows++;
	    }
	}
	delete decodedrow;
	// bitmap raster is prepared, pass through JBIG encoding...
	cc = raster.getLength();
	u_char* rasterdst = new u_char[cc];
	memcpy(rasterdst, (const unsigned char*) raster, cc);
	unsigned char *pmap[1];
	pmap[0] = rasterdst;
	struct jbg_enc_state jbigstate;
	jbg_enc_init(&jbigstate, width, rows, 1, pmap, bufferJBIGData, NULL);
	/*
	 * T.85 requires "single-progressive sequential coding" and thus:
	 *
	 * Dl = 0, D = 0, P = 1 are required
	 * L0 = 128 is suggested (and appears to be standard among senders)
	 * Mx = 0 to 127 (and 0 appears to be standard)
	 * My = 0, HITOLO = 0, SEQ = 0, ILEAVE = 0, SMID = 0
	 * TPDON = 0, DPON = 0, DPPRIV = 0, DPLAST = 0
	 *
	 * As these settings vary from the library's defaults, we carefully
	 * specify all of them.
	 */
	jbg_enc_options(&jbigstate, 0, 0, 128, 0, 0);
	jbg_enc_out(&jbigstate);
	jbg_enc_free(&jbigstate);
	delete rasterdst;
	// image is now encoded into JBIG
	//resultBuffer[19] |= 0x20;	// set VLENGTH = 1, if someday we want to transmit NEWLEN
	cc = resultBuffer.getLength();
	u_char* dst = new u_char[cc];
	memcpy(dst, (const unsigned char*) resultBuffer, cc);
	return (dst);
#else
	printf("Attempt to convert Phase C data to JBIG without JBIG support.  This should not happen.\n");
	return (NULL);
#endif /* HAVE_JBIG */
    } else {
	printf("Attempt to convert Phase C data to an unsupported format.  This should not happen.\n");
	return (NULL);
    }
}
