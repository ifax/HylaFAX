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
#ifndef _G3Decoder_
#define _G3Decoder_
/*
 * Group 3 Facsimile Decoder Support.
 */
#include "Types.h"
extern "C" {
#include <setjmp.h>
}
#include "tiffio.h"

class G3Decoder {
private:
    fxBool	is2D;		// whether or not data is 2d-encoded
    uint32	data;		// current input/output byte
    int		bit;		// current bit in input/output byte
    int		EOLcnt;		// EOL code recognized during decoding (1/0)
    int		RTCrun;		// count of consecutive zero-length rows
    int		rowref;		// reference count of rows decoded
    int		RTCrow;		// row number of start of RTC
    uint16*	refruns;	// runs for reference line
    uint16*	curruns;	// runs for current line
    const u_char* bitmap;	// bit reversal table
protected:
    G3Decoder();

    void	raiseEOF();
    void	raiseRTC();

    const u_char* getBitmap();

    virtual int nextByte();
    virtual int decodeNextByte();

    virtual void invalidCode(const char* type, int x);
    virtual void badPixelCount(const char* type, int got, int expected);
    virtual void badDecodingState(const char* type, int x);
public:
    // XXX these should be private; see below for why they're public
    sigjmp_buf	jmpEOF;		// non-local goto on EOF
    sigjmp_buf	jmpRTC;		// non-local goto on RTC

    virtual ~G3Decoder();

    void	setupDecoder(u_int fillorder, fxBool is2D);
    void	setRuns(uint16*, uint16*, int);
    uint16*	lastRuns();

    void	decode(void* raster, u_int w, u_int h);
    fxBool	decodeRow(void* scanline, u_int w);
    fxBool	isNextRow1D();

    int		getPendingBits() const;

    fxBool	seenRTC() const;
    int		getRTCRow() const;
    int		getReferenceRow() const;
};

/*
 * NB: These should be inline public methods but because we
 *     cannot depend on the compiler actually doing the inline
 *     we use #defines instead--if the sigsetjmp is done in
 *     the context of an out-of-line routine, then the saved
 *     frame pointer, pc, etc. will be wrong.
 */
#define	EOFraised()		(sigsetjmp(jmpEOF, 0) != 0)
#define	RTCraised()		(sigsetjmp(jmpRTC, 0) != 0)

inline uint16* G3Decoder::lastRuns()	{ return is2D ? refruns : curruns; }
inline const u_char* G3Decoder::getBitmap()	{ return bitmap; }
inline int G3Decoder::getPendingBits() const	{ return bit; }
inline fxBool G3Decoder::seenRTC() const	{ return (RTCrow != -1); }
inline int G3Decoder::getRTCRow() const		{ return RTCrow; }
inline int G3Decoder::getReferenceRow() const	{ return rowref; }
#endif /* _G3Decoder_ */
