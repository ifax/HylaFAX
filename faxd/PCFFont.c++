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
 * Read and render a PCF font.  This code is VERY distantly
 * related to the X11R5 code found in the file pcfread.c (the
 * original copyright is at the bottom of this file).
 *
 * This code is specifically written to handle just enough
 * of the format to image text for outgoing facsimile.
 */
#include <unistd.h>
#include "PCFFont.h"
#include "tiffio.h"

#define LSBFirst		0
#define MSBFirst		1

struct PCFTableRec {		// on-disk table of contents
    u_long	type;
    u_long	format;
    u_long	size;
    u_long	offset;
};

struct charInfo {
    short	lsb, rsb;	// left+right side bearing from file
    short	ascent, descent;// ascent+descent from file
    u_short	cw;		// character width from file
    u_char*	bits;		// pointer to glyph bitmap
};

#define PCF_FILE_VERSION	(('p'<<24)|('c'<<16)|('f'<<8)|1)
#define	PCF_FORMAT_MASK		0xffffff00

#define PCF_DEFAULT_FORMAT	0x00000000
#define PCF_COMPRESSED_METRICS	0x00000100
#define PCF_ACCEL_W_INKBOUNDS	0x00000100

#define PCF_METRICS		(1<<2)	// font metric information
#define PCF_BITMAPS		(1<<3)	// glyph bitmaps
#define	PCF_BDF_ENCODINGS	(1<<5)	// BDF-based encoding
#define PCF_BDF_ACCELERATORS	(1<<8)	// BDF-derived accelerator information

#define PCF_GLYPH_PAD_MASK	(3<<0)
#define PCF_BYTE_MASK		(1<<2)
#define PCF_BIT_MASK		(1<<3)
#define PCF_SCAN_UNIT_MASK	(3<<4)

#define	CONFIG_BIT	MSBFirst	// required bit order
#define	CONFIG_BYTE	MSBFirst	// required byte order
#define	CONFIG_GLYPH	2		// glyphs must be word-aligned
#define	CONFIG_GLYPH_IX	1		// index for word-aligned glyphs

inline bool PCFFont::isFormat(u_long f) const
    { return ((format&PCF_FORMAT_MASK) == f); }
inline u_int PCFFont::byteOrder() const
    { return ((format&PCF_BYTE_MASK) ? MSBFirst : LSBFirst); }
inline u_int PCFFont::bitOrder() const
    { return ((format&PCF_BIT_MASK) ? MSBFirst : LSBFirst); }
inline u_int PCFFont::glyphPadIndex() const
    { return ((format) & PCF_GLYPH_PAD_MASK); }
inline u_int PCFFont::glyphPad() const
    { return (1<<glyphPadIndex()); }
inline u_int PCFFont::scanUnit() const
    { return (1<<((format) & PCF_SCAN_UNIT_MASK)); }

PCFFont::PCFFont()
{
    file = NULL;
    filename = NULL;
    encoding = NULL;
    bitmaps = NULL;
    metrics = NULL;
    toc = NULL;
    cdef = NULL;
    { union { int32 i; char c[4]; } u; u.i = 1; isBigEndian = u.c[0] == 0; }
}

PCFFont::~PCFFont()
{
    cleanup();
}

void
PCFFont::cleanup()
{
    if (file != NULL)
	fclose(file), file = NULL;
    ready = false;
    delete toc, toc = NULL;
    delete encoding, encoding = NULL;
    delete bitmaps, bitmaps = NULL;
    delete metrics, metrics = NULL;
    cdef = NULL;
}

/*
 * Read only the useful bits from a PCF font file:
 * font metrics, font bitmaps, font encoding, and
 * some of the accelerator info.
 */
bool
PCFFont::read(const char* name)
{
    cleanup();
    filename = name;				// for error diagnostics
    file = fopen(filename, "r");
    if (file == NULL) {
	error("Can not open file");
	return (false);
    }
    if (!readTOC())
	return (false);
    if (seekToTable(PCF_METRICS)) {
	format = getLSB32();
	if (isFormat(PCF_DEFAULT_FORMAT))
	    numGlyphs = getINT32();
	else if (isFormat(PCF_COMPRESSED_METRICS))
	    numGlyphs = getINT16();
	else {
	    error("Bad font metric format 0x%lx", format);
	    return (false);
	}
	metrics = new charInfo[numGlyphs];
	if (!metrics) {
	    error("No space for font metric information");
	    return (false);
	}
	for (u_int i = 0; i < numGlyphs; i++) {
	    if (isFormat(PCF_DEFAULT_FORMAT))
		getMetric(metrics[i]);
	    else
		getCompressedMetric(metrics[i]);
	}
    } else {
	error("Can not seek to font metric information");
	return (false);
    }

    if (seekToTable(PCF_BITMAPS)) {
	format = getLSB32();
	if (!isFormat(PCF_DEFAULT_FORMAT)) {
	    error("Bad bitmap data format 0x%lx", format);
	    return (false);
	}
	u_long nbitmaps = getINT32();
	u_long* offsets = new u_long[nbitmaps];
	if (!offsets) {
	    error("No space for bitmap offsets array");
	    return (false);
	}
	for (u_int i = 0; i < nbitmaps; i++)
	    offsets[i] = getINT32();
	u_long bitmapSizes[4];
	bitmapSizes[0] = getINT32();
	bitmapSizes[1] = getINT32();
	bitmapSizes[2] = getINT32();
	bitmapSizes[3] = getINT32();
	u_long sizebitmaps = bitmapSizes[glyphPadIndex()];
	bitmaps = new u_char[sizebitmaps];
	if (!bitmaps) {
	    error("No space for bitmap data array");
	    delete offsets;
	    return (false);
	}
	if (fread(bitmaps, (u_int) sizebitmaps, 1, file) != 1) {
	    error("Error reading bitmap data");
	    delete offsets;
	    return (false);
	}
	if (bitOrder() != CONFIG_BIT)
	    TIFFReverseBits(bitmaps, sizebitmaps);
	if (byteOrder() != bitOrder()) {
	    switch (scanUnit()) {
	    case 2:
		TIFFSwabArrayOfShort((uint16*) bitmaps, sizebitmaps/2);
		break;
	    case 4:
		TIFFSwabArrayOfLong((uint32*) bitmaps, sizebitmaps/4);
		break;
	    default:
		error("Unknown scan unit format %d\n", scanUnit());
		break;
	    }
	}
	if (!isBigEndian)		// NB: rasterizer requires BE byte order
	    TIFFSwabArrayOfShort((u_short*) bitmaps, sizebitmaps/2);
	if (glyphPad() != CONFIG_GLYPH) {
	    u_long sizepadbitmaps = bitmapSizes[CONFIG_GLYPH_IX];
	    u_char* padbitmaps = new u_char[sizepadbitmaps];
	    if (!padbitmaps) {
		error("No space for padded bitmap data array");
		delete offsets;
		return (false);
	    }
	    int newoff = 0;
	    for (u_int i = 0; i < nbitmaps; i++) {
		off_t old = offsets[i];
		offsets[i] = newoff;
		const charInfo& metric = metrics[i];
		newoff += repadBitmap(bitmaps + old, padbitmaps + newoff,
			  glyphPad(), CONFIG_GLYPH,
			  metric.rsb - metric.lsb,
			  metric.ascent + metric.descent);
	    }
	    delete bitmaps;
	    bitmaps = padbitmaps;
	}
	for (u_int i = 0; i < nbitmaps; i++) {
	    metrics[i].bits = bitmaps + offsets[i];
	    if ((unsigned long) metrics[i].bits & 1) {
		error("Internal error, bitmap data not word-aligned");
		delete offsets;
		return (false);
	    }
	}
	delete offsets;
    } else {
	error("Can not seek to bitmap data");
	return (false);
    }

    if (seekToTable(PCF_BDF_ENCODINGS)) {
	format = getLSB32();
	if (!isFormat(PCF_DEFAULT_FORMAT)) {
	    error("Bad encodings format 0x%lx", format);
	    return (false);
	}
	firstCol = getINT16();
	lastCol = getINT16();
	u_short firstRow = getINT16();
	u_short lastRow = getINT16();
	u_short defaultCh = getINT16();

	u_int nencoding = (lastCol-firstCol+1) * (lastRow-firstRow+1);
	encoding = new charInfo*[nencoding];
	if (!encoding) {
	    error("No space for character encoding vector");
	    return (false);
	}
	for (u_int i = 0; i < nencoding; i++) {
	    int encodingOffset = getINT16();
	    encoding[i] = (encodingOffset == 0xffff) ?
		0 : metrics + encodingOffset;
	}
	if (defaultCh != (u_short)-1) {
	    int r = defaultCh >> 8;
	    int c = defaultCh & 0xff;
	    if (firstRow <= r && r <= lastRow && firstCol <= c && c <= lastCol) {
		int cols = lastCol - firstCol + 1;
		r = r - firstRow;
		c = c - firstCol;
		cdef = encoding[r * cols + c];
	    }
	}
    } else {
	error("Can not seek to encoding data");
	return (false);
    }

    if (seekToTable(PCF_BDF_ACCELERATORS)) {
	format = getLSB32();
	if (!isFormat(PCF_DEFAULT_FORMAT) &&
	    !isFormat(PCF_ACCEL_W_INKBOUNDS)) {
	    error("Bad BDF accelerator format 0x%lx", format);
	    return (false);
	}
	fseek(file, 8, SEEK_CUR);	// skip a bunch of junk
	fontAscent = (short) getINT32();
	fontDescent = (short) getINT32();
	// more stuff...
    } else {
	error("Can not seek to BDF accelerator information");
	return (false);
    }
    fclose(file), file = NULL;
    filename = NULL;
    return (ready = true);
}

u_long
PCFFont::getLSB32()
{
    u_long c = getc(file);
    c |= getc(file) << 8;
    c |= getc(file) << 16;
    c |= getc(file) << 24;
    return (c);
}

u_long
PCFFont::getINT32()
{
    u_long c;
    if (byteOrder() == MSBFirst) {
	c = getc(file) << 24;
	c |= getc(file) << 16;
	c |= getc(file) << 8;
	c |= getc(file);
    } else {
	c = getc(file);
	c |= getc(file) << 8;
	c |= getc(file) << 16;
	c |= getc(file) << 24;
    }
    return (c);
}

int
PCFFont::getINT16()
{
    int c;
    if (byteOrder() == MSBFirst) {
	c = getc(file) << 8;
	c |= getc(file);
    } else {
	c = getc(file);
	c |= getc(file) << 8;
    }
    return (c);
}

int PCFFont::getINT8() { return getc(file); }

/*
 * PCF supports two formats for metrics, both the regular
 * jumbo size, and 'lite' metrics, which are useful
 * for most fonts which have even vaguely reasonable
 * metrics
 */
void
PCFFont::getMetric(charInfo& metric)
{
    metric.lsb = getINT16();
    metric.rsb = getINT16();
    metric.cw = getINT16();
    metric.ascent = getINT16();
    metric.descent = getINT16();
    (void) getINT16();			// attributes
}

void
PCFFont::getCompressedMetric(charInfo& metric)
{
    metric.lsb = getINT8() - 0x80;
    metric.rsb = getINT8() - 0x80;
    metric.cw = getINT8() - 0x80;
    metric.ascent = getINT8() - 0x80;
    metric.descent = getINT8() - 0x80;
}

/*
 * Position to the begining of the specified table.
 */
bool
PCFFont::seekToTable(u_long type)
{
    for (u_int i = 0; i < tocSize; i++)
	if (toc[i].type == type) {
	    if (fseek(file, toc[i].offset, SEEK_SET) == -1) {
		error("Can not seek; fseek failed");
		return (false);
	    }
	    format = toc[i].format;
	    return (true);
	}
    error("Can not seek; no such entry in the TOC");
    return (false);
}

/*
 * Read the table-of-contents for the font file.
 */
bool
PCFFont::readTOC()
{
    u_long version = getLSB32();
    if (version != PCF_FILE_VERSION) {
	error("Cannot read TOC; bad version number %lu", version);
	return (false);
    }
    tocSize = getLSB32();
    toc = new PCFTableRec[tocSize];
    if (!toc) {
	error("Cannot read TOC; no space for %lu records", tocSize);
	return (false);
    }
    for (u_int i = 0; i < tocSize; i++) {
	toc[i].type = getLSB32();
	toc[i].format = getLSB32();
	toc[i].size = getLSB32();
	toc[i].offset = getLSB32();
    }
    return (true);
}

int
PCFFont::repadBitmap(u_char* src, u_char* dst, u_long spad, u_long dpad, int w, int h)
{
    int srcWidthBytes;
    switch (spad) {
    case 1:	srcWidthBytes = (w+7)>>3; break;
    case 2:	srcWidthBytes = ((w+15)>>4)<<1; break;
    case 4:	srcWidthBytes = ((w+31)>>5)<<2; break;
    case 8:	srcWidthBytes = ((w+63)>>6)<<3; break;
    default:	return 0;
    }
    int dstWidthBytes;
    switch (dpad) {
    case 1:	dstWidthBytes = (w+7)>>3; break;
    case 2:	dstWidthBytes = ((w+15)>>4)<<1; break;
    case 4:	dstWidthBytes = ((w+31)>>5)<<2; break;
    case 8:	dstWidthBytes = ((w+63)>>6)<<3; break;
    default:	return 0;
    }
    w = srcWidthBytes;
    if (w > dstWidthBytes)
	w = dstWidthBytes;
    for (int row = 0; row < h; row++) {
	int col;
	for (col = 0; col < w; col++)
	    *dst++ = *src++;
	while (col < dstWidthBytes) {
	    *dst++ = '\0';
	    col++;
	}
	src += srcWidthBytes - w;
    }
    return (dstWidthBytes * h);
}

u_int
PCFFont::charWidth(u_int c) const
{
    if (ready) {
	charInfo* ci = (firstCol <= c && c <= lastCol) ?
	    encoding[c - firstCol] : cdef;
	return (ci ? ci->cw : 0);
    } else
	return (0);
}

void
PCFFont::strWidth(const char* text, u_int &sw, u_int& sh) const
{
    sh = fontHeight();
    sw = 0;
    if (ready) {
	for (const char* cp = text; *cp; cp++) {
	    u_int g = *cp;
	    charInfo* ci = (firstCol <= g && g <= lastCol) ?
		encoding[g - firstCol] : cdef;
	    if (ci)
		sw += ci->cw;
	}
    }
}

/* merge Left/Right bits from a word in a glyph bitmap */
#define	MERGEL(r,g,dx,dm) \
    (r) = ((r) &~ (dm)) | (((g)>>dx) & (dm))
#define	MERGER(r,g,dx,dm) \
    (r) = ((r) &~ (dm)) | (((g)<<(dx)) & (dm))
/*
 * Image text into a raster.  The raster origin (0,0) is assumed to
 * be in the upper left.  Text is imaged from top-to-bottom and
 * left-to-right.  The height of the rendered text is returned.
 */
u_int
PCFFont::imageText(const char* text,
    u_short* raster, u_int w, u_int h,
    u_int lm, u_int rm, u_int tm, u_int bm) const
{
    if (!ready)
	return (0);
    u_int rowwords = howmany(w,16);
    u_int y = tm + fontAscent;
    u_int x = lm;
    /*
     * The rasterize assumes words have a big-endian
     * byte order.  For now (rather than fix it) we
     * byte swap the data coming in and going out.
     */
    if (!isBigEndian)				// XXX
	TIFFSwabArrayOfShort((u_short*) raster, h*rowwords);
    for (const char* cp = text; *cp; cp++) {
	u_int g = (u_char)*cp;
	charInfo* ci = (firstCol <= g && g <= lastCol) ?
	    encoding[g - firstCol] : cdef;
	if (!ci)
	    continue;
	if (x + ci->cw > w - rm) {		// no space on line, move down
	    if (y+fontHeight() >= h-bm)
		break;				// raster completely full
	    y += fontHeight();
	    x = lm;
	}
	/*
	 * Blit glyph bitmap into raster.  The work done here is
	 * not designed for speed.  We break the work into two cases;
	 * where the destination location in the raster is word-aligned
	 * and where it's not word-aligned.  Glyph bitmaps are assumed
	 * to be word-padded and to have the bits ``left adjusted''
	 * within words.  Note that we handle glyphs that are at
	 * most 47 bits wide; this should be sufficient for our needs.
	 */
	u_short cw = ci->rsb - ci->lsb;		// bitmap glyph width
	u_short cwords = cw>>4;			// full words in glyph
	if (cwords > 2)				// skip too wide glyph
	    continue;
	int cx = x + ci->lsb;			// left edge of glyph
	int ch = ci->ascent + ci->descent;	// glyph height
	u_short* rp = raster + (y-ci->ascent)*rowwords + (cx>>4);
	u_short* gp = (u_short*) ci->bits;
	u_short dx0 = cx&15;			// raster word offset
	u_short rowdelta = rowwords - cwords;	// raster row adjust factor
	u_short cbits = cw&15;			// partial glyph word
	if (dx0 != 0) {				// hard case, raster unaligned
	    u_short dm0 = 0xffff>>dx0;
	    u_short dx1 = 16-dx0;
	    u_short dm1 = ~dm0;
	    u_short dm2, dm3;
	    if (cbits > dx1) {			// spills into 2nd word
		dm2 = dm0;
		dm3 = ~((1<<dx1)-1);
	    } else {				// fits entirely into 1st word
		dm2 = dm0 &~ ((1<<(dx1-cbits))-1);
		dm3 = 0;
	    }
	    for (short r = 0; r < ch; r++) {
		switch (cwords) {		// merge complete glyph words
		case 2: MERGEL(rp[0], gp[0], dx0, dm0);
			MERGER(rp[1], gp[0], dx1, dm1);
			rp++, gp++;
		case 1: MERGEL(rp[0], gp[0], dx0, dm0);
			MERGER(rp[1], gp[0], dx1, dm1);
			rp++, gp++;
		}
		if (cbits) {
		    MERGEL(rp[0], gp[0], dx0, dm2);
		    MERGER(rp[1], gp[0], dx1, dm3);
		    gp++;
		}
		rp += rowdelta;
	    }
	} else {				// raster word-aligned
	    u_short dm = 0xffff<<(16-cbits);
	    for (short r = 0; r < ch; r++) {
		switch (cwords) {
		case 2: *rp++ = *gp++;
		case 1: *rp++ = *gp++;
		}
		if (cbits)
		    MERGEL(*rp, *gp++, 0, dm);
		rp += rowdelta;
	    }
	}
	x += ci->cw;
    }
    if (!isBigEndian)				// XXX
	TIFFSwabArrayOfShort((u_short*) raster, h*rowwords);
    return (y+fontDescent+bm);
}
#undef MERGEL
#undef MERGER

#include <ctype.h>

void
PCFFont::print(FILE* fd) const
{
    if (ready) {
	fprintf(fd, "Font Ascent: %d Descent: %d\n", fontAscent, fontDescent);
	fprintf(fd, "FirstCol: %u LastCol: %u\n", firstCol, lastCol);
	fprintf(fd, "%lu glyphs:\n", numGlyphs);
	for (u_int c = firstCol; c <= lastCol; c++) {
	    charInfo* ci = encoding[c - firstCol];
	    if (!ci)
		continue;
	    if (isprint(c))
		fprintf(fd,
		    "'%c': lsb %2d rsb %2d cw %2d ascent %2d descent %d\n",
		    c, ci->lsb, ci->rsb, ci->cw, ci->ascent, ci->descent);
	    else
		fprintf(fd,
		    "%3d: lsb %2d rsb %2d cw %2d ascent %2d descent %d\n",
		    c, ci->lsb, ci->rsb, ci->cw, ci->ascent, ci->descent);
	}
    }
}

#include "Str.h"

extern void vlogError(const char* fmt, va_list ap);

void
PCFFont::error(const char* fmt0 ...)
{
    va_list ap;
    va_start(ap, fmt0);
    fxStr fmt = fxStr::format("PCFFont: %s: %s",
	filename ? filename : "<unknown file>", fmt0);
    vlogError(fmt, ap);
    va_end(ap);
}

/*
 * $XConsortium: pcfread.c,v 1.10 92/05/12 18:07:47 gildea Exp $
 *
 * Copyright 1990 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of M.I.T. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  M.I.T. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * M.I.T. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL M.I.T.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Keith Packard, MIT X Consortium
 */
