/* 
 * This file does not exist in the original Hylafax distribution.
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

#include <ctype.h>
#include "NSF.h"

struct ModelData 
{
    const char* modelId;
    const char* modelName;
};

struct NSFData {
    const char* vendorId;
    static
    const int   vendorIdSize; // Country & provider code (T.35)
    const char* vendorName;
    bool        inverseStationIdOrder;
    int         modelIdPos;
    int         modelIdSize;
    const ModelData* knownModels;
};

NSFData::vendorIdSize = 3; // Country & provider code (T.35)

static const ModelData Panasonic0E[] =
{{"\x00\x00\x00\x96", "KX-F90" },
 {"\x00\x00\x00\x16", "KX-F780" },
 {NULL}};

static const ModelData Panasonic79[] =
{{"\x00\x00\x00\x02", "UF-S10" },
 {NULL}};

static const ModelData Samsung8C[] =
{{"\x00\x00\x01\x00", "SF-2010" },
 {NULL}};

static const NSFData KnownNSF[] =
{
    {"\x00\x00\x09", NULL,        false },
    {"\x00\x00\x0E", "Panasonic", false, 3, 4, Panasonic0E },
    {"\x00\x00\x11", "Canon",     false },
    {"\x00\x00\x19", "Xerox",     true  },
    {"\x00\x00\x21", "Lanier?",   true  },
    {"\x00\x00\x25", "Ricoh",     true  },
    {"\x00\x00\x26", NULL,        false },
    {"\x00\x00\x45", "Muratec",   false },
    {"\x00\x00\x51", "Sanyo",     false },
    {"\x00\x00\x66", "UTAX",      true  },
    {"\x00\x00\x79", "Panasonic", false, 3, 4, Panasonic79 },
    {"\x20\x41\x59", "Siemens",   false },
    {"\x59\x59\x01", NULL,        false },
    {"\x86\x00\x8C", "Samsung",   false, 3, 4, Samsung8C },
    {"\x86\x00\x98", "Samsung",   false },
    {"\xAD\x00\x36", "HP",        false },
    {"\xB5\x00\x2E", "Delrina",   false },
    {NULL}
};

NSF::NSF()
{
    clear();
}


NSF::NSF( const char* hexNSF )
{
    clear();
    loadHexData( hexNSF );
    decode();
}

NSF::NSF( const u_char* rawData, int size, const u_char* revTab )
{
    clear();
    loadRawData( rawData, size, revTab );
    decode();
}

void NSF::clear()
{
    nsf.resize(0);
    hexNsf.resize(0);
    vendor = "unknown";
    model = "";
    stationId = "";
    vendorDecoded = false;
    stationIdDecoded = false;
}

void NSF::loadHexData( const char* hexNSF )
{
    hexNsf.append( hexNSF );
    const char *p = hexNSF;
    char *pNext = NULL;
    for( ;; ){
        int val = strtol( p, &pNext, 16 );
        if( pNext == p )
            break;
        p = pNext;
        nsf.append( (unsigned char)val );
    }
}

void NSF::loadRawData( const u_char* rawData, int size, const u_char* revTab )
{
    nsf.append( (const char*)rawData, size );
    u_char *p   = (u_char*)(const char*)nsf;
    u_char *end = p+size;
    for( ; p < end; p++ ){
        *p = revTab[*p];
        char buf[10];
        sprintf( buf, "%02X ", *p );
        hexNsf.append(buf);
    }
    // remove trailing space
    hexNsf.resize( hexNsf.length() - 1 );
}

void NSF::decode()
{
    if ( nsf.length() == 0 )
	return;
    for( const NSFData* p = KnownNSF; p->vendorId; p++ ){
        if( !memcmp( p->vendorId, &nsf[0], p->vendorIdSize ) ){
            if (p->vendorName != 0) {
                vendor = p->vendorName;
            }
            if( p->knownModels ){
                for( const ModelData* pp = p->knownModels; pp->modelId; pp++ )
                    if( !memcmp( pp->modelId, &nsf[p->modelIdPos], p->modelIdSize ) )
                        model = pp->modelName;
            }
            findStationId( p->inverseStationIdOrder );
            vendorDecoded = true;
        }
    }
    if( !vendorFound() )
       findStationId( 0 );
}

void NSF::findStationId( bool reverseOrder )
{
    const char* id = NULL;
    u_int       idSize = 0;
    const char* maxId = NULL;
    u_int       maxIdSize = 0;
    /*
     * Trying to find the longest printable ASCII sequence
     */
    for( const char *p = (const char*)nsf, *end = p + nsf.length(); p < end; p ++ ){
        if( isprint(*p) ){
            if( !idSize++ ) 
                id = p;
            if( idSize > maxIdSize ){
                maxId = id;
                maxIdSize = idSize;
            }
        }
        else {
            id = NULL;
            idSize = 0;
        }
    }
    
    /*
     * Minimum acceptable id length
     */
    const u_int MinIdSize = 4;
    if( maxIdSize >= MinIdSize ){
        stationId.resize(0);
        const char* p;
        int dir;
        if( reverseOrder ){
            p   = maxId + maxIdSize - 1;
            dir = -1;
        }
        else {
            p   = maxId;
            dir = 1;
        }
        for( u_int i = 0; i < maxIdSize; i++ ){
            stationId.append( *p );
            p += dir;
        }
        stationIdDecoded = true;
    }
}
