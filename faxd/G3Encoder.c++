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
 * Group 3 Facsimile Writer Support.
 */
#include "G3Encoder.h"
#include "StackBuffer.h"
#include "tiffio.h"
#include "t4.h"

G3Encoder::G3Encoder(fxStackBuffer& b) : buf(b) {}
G3Encoder::~G3Encoder() {}

/*
 * Reset encoding state.
 */
void
G3Encoder::setupEncoder(u_int fillOrder, fxBool is2d)
{
    is2D = is2d;
    /*
     * G3-encoded data is generated in MSB2LSB bit order, so we
     * need to bit reverse only if the desired order is different.
     */
    bitmap = TIFFGetBitRevTable(fillOrder != FILLORDER_MSB2LSB);
    data = 0;
    bit = 8;
}

/*
 * Flush 8-bits of encoded data to the output buffer.
 */
inline void
G3Encoder::flushBits()
{
    buf.put(bitmap[data]);
    data = 0;
    bit = 8;
}

/*
 * Encode a multi-line raster.  We do everything with
 * 1D-data, inserting the appropriate tag bits when
 * 2D-encoding is required.
 */
void
G3Encoder::encode(const void* vp, u_int w, u_int h)
{
    u_int rowbytes = howmany(w, 8);

    while (h-- > 0) {
	if (bit != 4)					// byte-align EOL
	    putBits(0, (bit < 4) ? bit+4 : bit-4);
	if (is2D)
	    putBits((EOL<<1)|1, 12+1);
	else
	    putBits(EOL, 12);
	int bs = 0, span;
	const u_char* bp = (const u_char*) vp;
	for (;;) {
	    span = findspan(&bp, bs, w, zeroruns);	// white span
	    putspan(span, TIFFFaxWhiteCodes);
	    bs += span;
	    if (bs >= w)
		break;
	    span = findspan(&bp, bs, w, oneruns);	// black span
	    putspan(span, TIFFFaxBlackCodes);
	    bs += span;
	    if (bs >= w)
		break;
	}
	vp = (const u_char*)vp + rowbytes;
    }
    if (bit != 8)					// flush partial byte
	flushBits();
}

/*
 * Write a code to the output stream.
 */
inline void
G3Encoder::putcode(const tableentry& te)
{
    putBits(te.code, te.length);
}

/*
 * Write the sequence of codes that describes
 * the specified span of zero's or one's.  The
 * appropriate table that holds the make-up and
 * terminating codes is supplied.
 */
void
G3Encoder::putspan(int span, const tableentry* tab)
{
    while (span >= 2624) {
	const tableentry& te = tab[63 + (2560>>6)];
	putcode(te);
	span -= te.runlen;
    }
    if (span >= 64) {
	const tableentry& te = tab[63 + (span>>6)];
	putcode(te);
	span -= te.runlen;
    }
    putcode(tab[span]);
}

/*
 * Write a variable-length bit-value to the output
 * stream. Values are assumed to be at most 16 bits.
 */
void
G3Encoder::putBits(u_int bits, u_int length)
{
    static const u_int mask[9] =
	{ 0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff };

    while (length > bit) {
	data |= bits >> (length - bit);
	length -= bit;
	flushBits();
    }
    data |= (bits & mask[length]) << (bit - length);
    bit -= length;
    if (bit == 0)
	flushBits();
}

/*
 * Find a span of ones or zeros using the supplied
 * table.  The byte-aligned start of the bit string
 * is supplied along with the start+end bit indices.
 * The table gives the number of consecutive ones or
 * zeros starting from the msb and is indexed by byte
 * value.
 */
int
G3Encoder::findspan(const u_char** bpp, int bs, int be, const u_char* tab)
{
    const u_char *bp = *bpp;
    int bits = be - bs;
    int n, span;

    /*
     * Check partial byte on lhs.
     */
    if (bits > 0 && (n = (bs & 7))) {
	span = tab[(*bp << n) & 0xff];
	if (span > 8-n)        /* table value too generous */
	    span = 8-n;
	if (span > bits)	/* constrain span to bit range */
	    span = bits;
	if (n+span < 8)        /* doesn't extend to edge of byte */
	    goto done;
	bits -= span;
	bp++;
    } else
	span = 0;
    /*
     * Scan full bytes for all 1's or all 0's.
     */
    while (bits >= 8) {
	n = tab[*bp];
	span += n;
	bits -= n;
	if (n < 8)        /* end of run */
	    goto done;
	bp++;
    }
    /*
     * Check partial byte on rhs.
     */
    if (bits > 0) {
	n = tab[*bp];
	span += (n > bits ? bits : n);
    }
done:
    *bpp = bp;
    return (span);
}

const u_char G3Encoder::zeroruns[256] = {
    8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,    /* 0x00 - 0x0f */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,    /* 0x10 - 0x1f */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,    /* 0x20 - 0x2f */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,    /* 0x30 - 0x3f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,    /* 0x40 - 0x4f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,    /* 0x50 - 0x5f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,    /* 0x60 - 0x6f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,    /* 0x70 - 0x7f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0x80 - 0x8f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0x90 - 0x9f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0xa0 - 0xaf */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0xb0 - 0xbf */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0xc0 - 0xcf */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0xd0 - 0xdf */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0xe0 - 0xef */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0xf0 - 0xff */
};
const u_char G3Encoder::oneruns[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0x00 - 0x0f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0x10 - 0x1f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0x20 - 0x2f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0x30 - 0x3f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0x40 - 0x4f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0x50 - 0x5f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0x60 - 0x6f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0x70 - 0x7f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,    /* 0x80 - 0x8f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,    /* 0x90 - 0x9f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,    /* 0xa0 - 0xaf */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,    /* 0xb0 - 0xbf */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,    /* 0xc0 - 0xcf */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,    /* 0xd0 - 0xdf */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,    /* 0xe0 - 0xef */
    4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 7, 8,    /* 0xf0 - 0xff */
};

/*
 * Return the offset of the next bit in the range
 * [bs..be] that is different from bs.  The end,
 * be, is returned if no such bit exists.
 */
int
G3Encoder::finddiff(const u_char* cp, int bs, int be)
{
    cp += bs >> 3;            /* adjust byte offset */
    return (bs + findspan(&cp, bs, be,
	(*cp & (0x80 >> (bs&7))) ? oneruns : zeroruns));
}
