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
#ifndef _PCFFont_
#define _PCFFont_
/*
 * Portable Compiled Format (PCF) Font Support.
 *
 * This class is specifically designed to read a PCF font and use
 * it to image text into a raster bitmap.  It does just enough to
 * satisfy the needs of the fax software (for imaging tag lines).
 */
#include "FaxFont.h"

struct PCFTableRec;
struct charInfo;

class PCFFont : public FaxFont {
private:
    u_short	firstCol;	// index of first encoded glyph
    u_short	lastCol;	// index of last encoded glyph
    u_long	numGlyphs;	// count of glyphs with metrics+bitmaps
    charInfo*	metrics;	// font metrics, including glyph pointers
    u_char*	bitmaps;	// base of bitmaps, useful only to free
    charInfo**	encoding;	// array of char info pointers
    charInfo*	cdef;		// default character
				// input-specific state
    FILE*	file;		// open file
    const char*	filename;	// filename for error messages
    u_long	format;		// format for current portion being read
    PCFTableRec* toc;		// table of contents
    u_long	tocSize;	// number of entries in TOC
    fxBool	isBigEndian;	// host byte order

    void	cleanup();
    fxBool	readTOC();
    fxBool	seekToTable(u_long type);
    void	getMetric(charInfo& metric);
    void	getCompressedMetric(charInfo& metric);
    int		repadBitmap(u_char* src, u_char* dst,
		    u_long srcPad, u_long dstPad, int width, int height);
    u_long	getLSB32();
    u_long	getINT32();
    int		getINT16();
    int		getINT8();

    fxBool	isFormat(u_long f) const;
    u_int	byteOrder() const;
    u_int	bitOrder() const;
    u_int	glyphPadIndex() const;
    u_int	glyphPad() const;
    u_int	scanUnit() const;

    virtual void error(const char* fmt, ...);
public:
    PCFFont();
    ~PCFFont();

    fxBool	read(const char* filename);

    u_int	charWidth(u_int) const;
    void	strWidth(const char* text, u_int& w, u_int& h) const;
    u_int	imageText(const char* text,
		    u_short* bitmap, u_int w, u_int h,
		    u_int lm, u_int rm, u_int tm, u_int bm) const;
    void	print(FILE*) const;
};
#endif /* _PCFFont_ */
