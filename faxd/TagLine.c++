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
#include "FaxServer.h"
#include "PCFFont.h"
#include "G3Encoder.h"
#include "StackBuffer.h"
#include "FaxFont.h"
#include "FaxRequest.h"

#include "Sys.h"

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
FaxModem::setupTagLine(const FaxRequest& req, const fxStr& tagLineFmt)
{
    if (tagLineFont == NULL)
	tagLineFont = new PCFFont;
    if (!tagLineFont->isReady() && conf.tagLineFontFile != "")
	(void) tagLineFont->read(conf.tagLineFontFile);

    time_t t = Sys::now();
    tm* tm = localtime(&t);
    char line[1024];
    strftime(line, sizeof (line)-1, tagLineFmt, tm);
    tagLine = line;
    u_int l = 0;
    while (l < tagLine.length()) {
	l = tagLine.next(l, '%');
	if (l >= tagLine.length()-1)
	    break;
	switch (tagLine[l+1]) {
	case 'd': insert(tagLine, l, req.external); break;
	case 'i': insert(tagLine, l, req.jobid); break;
	case 'j': insert(tagLine, l, req.jobtag); break;
	case 'l': insert(tagLine, l, server.getLocalIdentifier()); break;
	case 'm': insert(tagLine, l, req.mailaddr); break;
	case 'n': insert(tagLine, l, server.getModemNumber()); break;
	case 'r': insert(tagLine, l, req.receiver); break;
	case 's': insert(tagLine, l, req.sender); break;
	case 't': insert(tagLine, l,
			fxStr((int)(req.totpages-req.npages), "%u")); break;
	case 'T': insert(tagLine, l,
			fxStr((int)(req.totpages), "%u")); break;
	case '%': tagLine.remove(l); break;
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

/*
 * Calculate a ``slop factor'' to use in processing the tag line.
 * This is the amount of space to preallocate for storing the
 * encoded tag line once its been imaged.  We guestimate that
 * the encoded data will be <= the amount of space needed to store
 * the unencoded bitmap raster.  If this number is low then the
 * encoded raster will be truncated resulting in the tag line
 * being cropped at the bottom; probably together with a decoding
 * error of one row at the receiver.
 */
bool
FaxModem::setupTagLineSlop(const Class2Params& params)
{
    if (tagLineFont->isReady() && tagLineFields > 0) {
	tagLineSlop = (tagLineFont->fontHeight()+MARGIN_TOP+MARGIN_BOT+3) * 
	    howmany(params.pageWidth(),8);
	return (true);
    } else {
	tagLineSlop = 0;
	return (false);
    }
}

/*
 * A memory-based G3 decoder--does no checking for end-of-data; it
 * assumes there'll be enough to satisfy the decoding requests.
 */
class TagLineMemoryDecoder : public G3Decoder {
private:
    const u_char* bp;
    int decodeNextByte();
public:
    TagLineMemoryDecoder(const u_char* data);
    ~TagLineMemoryDecoder();
    const u_char* current()		{ return bp; }
};
TagLineMemoryDecoder::TagLineMemoryDecoder(const u_char* data) : bp(data) {}
TagLineMemoryDecoder::~TagLineMemoryDecoder()	{}
int TagLineMemoryDecoder::decodeNextByte()	{ return *bp++; }

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
FaxModem::imageTagLine(u_char* buf, u_int fillorder, const Class2Params& params)
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
	if (tag[l+1] == 'P')
	    insert(tag, l, fxStr((int) pageNumberOfJob, "%d"));
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
     *
     * NB: The +3 below is for the case where we need to re-encode
     *     2D-encoded data.  An extra 3 rows is sufficinet because
     *     the number of consecutive 2D-encoded rows is bounded
     *     by the K parameter in the CCITT spec.
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
    TagLineMemoryDecoder dec(buf);
    dec.setupDecoder(fillorder, params.is2D());
    tiff_runlen_t runs[2*2432];		// run arrays for cur+ref rows
    dec.setRuns(runs, runs+2432, w);

    dec.decode(NULL, w, th);		// discard decoded data
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
    for (n = 0; n < 4 && !dec.isNextRow1D(); n++)
	dec.decodeRow(NULL, w);
    th += n;				// compensate for discarded rows
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
