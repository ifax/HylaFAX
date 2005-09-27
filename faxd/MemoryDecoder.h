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

#ifndef	_MemoryDecoder_
#define	_MemoryDecoder_

#include "G3Decoder.h"
#include "Class2.h"

class MemoryDecoder : public G3Decoder {
private:
    u_char*	bp;
    u_int	width;
    u_int	byteWidth;
    u_long	cc;
    uint32	rows;
    u_int	fillorder;
    bool	is2D, isG4;
    u_char*	endOfData;	// used by cutExtraRTC
    u_int	nblanks;
    tiff_runlen_t* runs;
    u_char*	rowBuf;

    int		decodeNextByte();
    void	invalidCode(const char* type, int x);
    void	badPixelCount(const char* type, int got, int expected);
    void	badDecodingState(const char* type, int x);
public:
    MemoryDecoder(u_char* data, u_long cc);
    MemoryDecoder(u_char* data, u_int wid, u_long n,
                  u_int fillorder, bool twoDim, bool mmr);
    ~MemoryDecoder();
    u_char* current() { return bp; }
    void fixFirstEOL();
    u_char* cutExtraRTC();
    u_char* cutExtraEOFB();
    u_char* encodeTagLine (u_long* raster, u_int th, u_int slop);
    u_char* convertDataFormat(const Class2Params& params);
    int		getLastByte();

    void scanPageForBlanks(u_int fillorder, const Class2Params& params);
    const u_char* getEndOfPage()			{ return endOfData; }
    u_int getLastBlanks()				{ return nblanks; }
    u_long getCC()					{ return cc; }
    uint32 getRows()					{ return rows; }
};

#endif
