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
 *
 * The 2-D encoding functionality was taken from the libtiff 3.5.7 distribution.
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
G3Encoder::setupEncoder(u_int fillOrder, bool is2d, bool isg4)
{
    is2D = is2d;
    isG4 = isg4;
    /*
     * G3-encoded data is generated in MSB2LSB bit order, so we
     * need to bit reverse only if the desired order is different.
     */
    bitmap = TIFFGetBitRevTable(fillOrder != FILLORDER_MSB2LSB);
    data = 0;
    bit = 8;
    firstEOL = true;
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

static const tableentry horizcode =
    { 3, 0x1 };		/* 001 */
static const tableentry passcode =
    { 4, 0x1 };		/* 0001 */
static const tableentry vcodes[7] = {   
    { 7, 0x03 },	/* 0000 011 */
    { 6, 0x03 },	/* 0000 11 */
    { 3, 0x03 },	/* 011 */
    { 1, 0x1 },		/* 1 */
    { 3, 0x2 },		/* 010 */
    { 6, 0x02 },	/* 0000 10 */
    { 7, 0x02 }		/* 0000 010 */  
};

#define isAligned(p,t)  ((((u_long)(p)) & (sizeof (t)-1)) == 0)

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

/*
 * Find a span of ones or zeros using the supplied
 * table.  The ``base'' of the bit string is supplied
 * along with the start+end bit indices.
 */
int
G3Encoder::find0span(const u_char* bp, int bs, int be)
{
	int32 bits = be - bs;
	int32 n, span;

	bp += bs>>3;
	/*
	 * Check partial byte on lhs.
	 */
	if (bits > 0 && (n = (bs & 7))) {
		span = zeroruns[(*bp << n) & 0xff];
		if (span > 8-n)		/* table value too generous */
			span = 8-n;
		if (span > bits)	/* constrain span to bit range */
			span = bits;
		if (n+span < 8)		/* doesn't extend to edge of byte */
			return (span);
		bits -= span;
		bp++;
	} else
		span = 0;
	if (bits >= 2*8*sizeof (long)) {
		long* lp;
		/*
		 * Align to longword boundary and check longwords.
		 */
		while (!isAligned(bp, long)) {
			if (*bp != 0x00)
				return (span + zeroruns[*bp]);
			span += 8, bits -= 8;
			bp++;
		}
		lp = (long*) bp;
		while (bits >= 8*sizeof (long) && *lp == 0) {
			span += 8*sizeof (long), bits -= 8*sizeof (long);
			lp++;
		}
		bp = (u_char*) lp;
	}
	/*
	 * Scan full bytes for all 0's.
	 */
	while (bits >= 8) {
		if (*bp != 0x00)	/* end of run */
			return (span + zeroruns[*bp]);
		span += 8, bits -= 8;
		bp++;
	}
	/*
	 * Check partial byte on rhs.
	 */
	if (bits > 0) {
		n = zeroruns[*bp];
		span += (n > bits ? bits : n);
	}
	return (span);
}

int
G3Encoder::find1span(const u_char* bp, int bs, int be)
{
	int32 bits = be - bs;
	int32 n, span;

	bp += bs>>3;
	/*
	 * Check partial byte on lhs.
	 */
	if (bits > 0 && (n = (bs & 7))) {
		span = oneruns[(*bp << n) & 0xff];
		if (span > 8-n)		/* table value too generous */
			span = 8-n;
		if (span > bits)	/* constrain span to bit range */
			span = bits;
		if (n+span < 8)		/* doesn't extend to edge of byte */
			return (span);
		bits -= span;
		bp++;
	} else
		span = 0;
	if (bits >= 2*8*sizeof (long)) {
		long* lp;
		/*
		 * Align to longword boundary and check longwords.
		 */
		while (!isAligned(bp, long)) {
			if (*bp != 0xff)
				return (span + oneruns[*bp]);
			span += 8, bits -= 8;
			bp++;
		}
		lp = (long*) bp;
		while (bits >= 8*sizeof (long) && *lp == ~0) {
			span += 8*sizeof (long), bits -= 8*sizeof (long);
			lp++;
		}
		bp = (u_char*) lp;
	}
	/*
	 * Scan full bytes for all 1's.
	 */
	while (bits >= 8) {
		if (*bp != 0xff)	/* end of run */
			return (span + oneruns[*bp]);
		span += 8, bits -= 8;
		bp++;
	}
	/*
	 * Check partial byte on rhs.
	 */
	if (bits > 0) {
		n = oneruns[*bp];
		span += (n > bits ? bits : n);
	}
	return (span);
}

/*
 * Return the offset of the next bit in the range
 * [bs..be] that is different from the specified
 * color.  The end, be, is returned if no such bit
 * exists.
 */
#define finddiff(_cp, _bs, _be, _color) \
	(_bs + (_color ? find1span(_cp,_bs,_be) : find0span(_cp,_bs,_be)))

/*
 * Like finddiff, but also check the starting bit
 * against the end in case start > end. 
 */
#define finddiff2(_cp, _bs, _be, _color) \
	(_bs < _be ? finddiff(_cp,_bs,_be,_color) : _be)

/*
 * Encode a multi-line raster.  For MH and MR we can do everything with
 * 1D-data, if desired, inserting the appropriate tag bits in MR.  For 
 * MMR we must do everything with 2D-data, thus when coding 2D-data a 
 * reference line, rp, is required.
 */
void
G3Encoder::encode(const void* vp, u_int w, u_int h, u_char* rp)
{
#define PIXEL(buf,ix)   ((((buf)[(ix)>>3]) >> (7-((ix)&7))) & 1)
    u_int rowbytes = howmany(w, 8);
    const u_char* bp = (const unsigned char*) vp;

    while (h-- > 0) {
	if (!isG4) {						// put the EOL
	    if( firstEOL )					// according to T.4 first EOL 
		firstEOL = false;				// should not be aligned
	    else if (bit != 4)
		putBits(0, (bit < 4) ? bit+4 : bit-4);		// byte-align other EOLs
	    if (is2D)
		if (rp)
		    putBits((EOL<<1)|0, 12+1);			// T.4 4.2.2
		else
		    putBits((EOL<<1)|1, 12+1);
	    else
		putBits(EOL, 12);
	}
	if (rp) {						// 2-D line
	    uint32 a0 = 0;
	    uint32 a1 = (PIXEL(bp, 0) != 0 ? 0 : finddiff(bp, 0, w, 0));
	    uint32 b1 = (PIXEL(rp, 0) != 0 ? 0 : finddiff(rp, 0, w, 0));
	    uint32 a2, b2;
	    for (;;) {
		b2 = finddiff2(rp, b1, w, PIXEL(rp,b1));
		if (b2 >= a1) {
		    int32 d = b1 - a1;
		    if (!(-3 <= d && d <= 3)) {		/* horizontal mode */
			a2 = finddiff2(bp, a1, w, PIXEL(bp,a1));
			putcode(horizcode);
			if (a0+a1 == 0 || PIXEL(bp, a0) == 0) {
			    putspan(a1-a0, TIFFFaxWhiteCodes);
			    putspan(a2-a1, TIFFFaxBlackCodes);
			} else {
			    putspan(a1-a0, TIFFFaxBlackCodes);
			    putspan(a2-a1, TIFFFaxWhiteCodes);
			}
			a0 = a2;
		    } else {				/* vertical mode */
			putcode(vcodes[d+3]);
			a0 = a1;
		    }
		} else {				/* pass mode */
		    putcode(passcode);
		    a0 = b2;
		}
		if (a0 >= w)
		    break;
		a1 = finddiff(bp, a0, w, PIXEL(bp,a0));
		b1 = finddiff(rp, a0, w, !PIXEL(bp,a0));
		b1 = finddiff(rp, b1, w, PIXEL(bp,a0));
	    }
	    memcpy(rp, bp, rowbytes);
	    bp += rowbytes;					// advance raster row
	} else {						// 1-D line
	    int bs = 0, span;
	    for (;;) {
		span = findspan(&bp, bs, w, zeroruns);		// white span
		putspan(span, TIFFFaxWhiteCodes);
		bs += span;
		if (bs >= w)
		    break;
		span = findspan(&bp, bs, w, oneruns);		// black span
		putspan(span, TIFFFaxBlackCodes);
		bs += span;
		if (bs >= w)
		    break;
	    }
	}
    }
#undef PIXEL
}

void
G3Encoder::encoderCleanup()
{
    if (isG4) {
	putBits(EOL, 12);
	putBits(EOL, 12);
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
