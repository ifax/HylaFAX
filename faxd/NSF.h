/* $Id$ */
/* 
 * This file does not exist in the original HylaFAX distribution.
 * Created by Dmitry Bely, April 2000
 */
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

#ifndef NSF_H
#define NSF_H

#include "Str.h"

class NSF {
    fxStr nsf;
    fxStr hexNsf;
    fxStr vendor;
    fxStr model;
    fxStr stationId;
    bool  vendorDecoded;
    bool  stationIdDecoded;
public:
    NSF();
    NSF( const char* hexNSF );
    NSF( const u_char* rawData, int size, const u_char* revTab );
    /*
     * We are happy with default copy constructor and copy assignment,
     * so do not explicitly define them (but will use)
     */
    const fxStr& getRawNsf(){ return nsf; }
    const fxStr& getHexNsf(){ return hexNsf; }
    bool  vendorFound(){ return vendorDecoded; }
    bool  stationIdFound(){ return stationIdDecoded; }
    const char* getVendor(){ return (const char*)vendor; }
    const char* getModel(){ return (const char*)model; }
    const char* getStationId(){ return (const char*)stationId; }
private:
    void clear();
    void loadHexData( const char* hexNSF );
    void loadRawData( const u_char* rawData, int size, const u_char* revTab );
    void findStationId( bool reverseOrder, bool reverseBitOrder );
    void decode();
};

#endif /* NSF_H */
