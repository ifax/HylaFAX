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

MemoryDecoder::MemoryDecoder(u_char* data, u_long n)
{
    bp = data;
    cc = n;
    endOfData = NULL;
    nblanks = 0;
    runs = NULL;
    rowBuf = NULL;
}

MemoryDecoder::MemoryDecoder(u_char* data, u_int wid, u_long n,
                             u_int order, bool twoDim, bool mmr)
{
    bp         = data;
    width      = wid;
    byteWidth  = howmany(width, 8);
    cc         = n;
    
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

int
MemoryDecoder::getLastByte()
{
    return (*(endOfData - 1));
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
         * (see detailed explanation of look_ahead in TagLine.c++)
         */
        (void)isNextRow1D();
        u_int look_ahead = roundup(getPendingBits(),8) / 8;
        u_int decoded = current() - look_ahead - start;

        enc.encode(rowBuf, width, 1);
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
    u_char* start = current();
    
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
    if(!RTCraised()) {
        /*
         * syncronize to the next EOL and calculate pointer to it
         * (see detailed explanation of look_ahead in TagLine.c++)
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
    if(!RTCraised()) {
	endOfData = current();
        for (;;) {
            if( decodeRow(NULL, width) ){
                endOfData = current();
            }
            if( seenRTC() )
                break;
        }
    }
    if (seenRTC())
	endOfData--;	// step back over the first byte of EOFB
    return endOfData;
}
