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
#ifndef _Font_
#define _Font_
/*
 * Fax Server Font Support.
 */
#include "Types.h"

class FaxFont {
protected:
    fxBool	ready;		// font ready to use
    short	fontAscent;	// font ascent metric
    short	fontDescent;	// font descent metric

    FaxFont();
public:
    virtual ~FaxFont();

    virtual fxBool read(const char* filename) = 0;

    fxBool	isReady() const;
    u_int	fontHeight() const;
    virtual u_int charWidth(u_int) const = 0;
    virtual void strWidth(const char* text, u_int& w, u_int& h) const = 0;
    virtual u_int imageText(const char* text,
		    u_short* bitmap, u_int w, u_int h,
		    u_int lm, u_int rm, u_int tm, u_int bm) const = 0;
    virtual void print(FILE*) const = 0;
};

inline fxBool FaxFont::isReady() const	 { return ready; }
inline u_int FaxFont::fontHeight() const { return fontAscent+fontDescent; }
#endif /* _Font_ */
