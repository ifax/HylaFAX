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

const int NSFData::vendorIdSize = 3; // Country & provider code (T.35)

static const ModelData Canon[] =
{{"\x80\x00\x80\x48\x00", "Faxphone B640"},
 {"\x80\x00\x80\x49\x10", "Fax B100"},
 {"\x80\x00\x8A\x49\x10", "Laser Class 9000 Series"},
 {NULL}};
  

static const ModelData Brother[] =
{{"\x55\x55\x00\x88\x90\x80\x5F\x00\x15\x51", "Intellifax 770"},
 {"\x55\x55\x00\x80\xB0\x80\x00\x00\x59\xD4", "Personal fax 190"},
 {"\x55\x55\x00\x8C\x90\x80\xF0\x02\x20", "MFC-8600"},
 {NULL}};

static const ModelData Panasonic0E[] =
{{"\x00\x00\x00\x96\x0F\x01\x02\x00\x10\x05\x02\x95\xC8\x08\x01\x49\x02\x41\x53\x54\x47", "KX-F90" },
 {"\x00\x00\x00\x96\x0F\x01\x03\x00\x10\x05\x02\x95\xC8\x08\x01\x49\x02                \x03", "KX-F230 or KX-FT21 or ..." },
 {"\x00\x00\x00\x16\x0F\x01\x03\x00\x10\x05\x02\x95\xC8\x08",          "KX-F780" },
 {"\x00\x00\x00\x16\x0F\x01\x03\x00\x10\x00\x02\x95\x80\x08\x75\xB5",  "KX-M260" },
 {"\x00\x00\x00\x16\x0F\x01\x02\x00\x10\x05\x02\x85\xC8\x08\xAD", "KX-F2050BS" },
 {NULL}};

static const ModelData Panasonic79[] =
{{"\x00\x00\x00\x02\x0F\x09\x12\x00\x10\x05\x02\x95\xC8\x88\x80\x80\x01", "UF-S10" },
 {"\x00\x00\x00\x16\x7F\x09\x13\x00\x10\x05\x16\x8D\xC0\xD0\xF8\x80\x01", "/Siemens Fax 940" },
 {"\x00\x00\x00\x16\x0F\x09\x13\x00\x10\x05\x06\x8D\xC0\x50\xCB", "Panafax UF-321" },
 {NULL}};

static const ModelData Ricoh[] =
{{"\x00\x00\x00\x12\x10\x0D\x02\x00\x50\x00\x2A\xB8\x2C", "/Nashuatec P394" },
 {NULL}};


static const ModelData Samsung8C[] =
{{"\x00\x00\x01\x00", "SF-2010" },
 {NULL}};

static const ModelData Sanyo[] =
{{"\x00\x00\x10\xB1\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x41\x26\xFF\xFF\x00\x00\x85\xA1", "SFX-107" },
 {"\x00\x00\x00\xB1\x12\xF2\x62\xB4\x82\x0A\xF2\x2A\x12\xD2\xA2\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x41\x4E\xFF\xFF\x00\x00", "MFP-510" },
 {NULL}};

static const ModelData HP[] =
{{"\x20\x00\x45\x00\x0C\x04\x70\xCD\x4F\x00\x7F\x49", "LaserJet 3150" },
 {"\x40\x80\x84\x01\xF0\x6A", "OfficeJet" },
 {"\xC0\x00\x00\x00\x00", "OfficeJet 500" },
 {"\xC0\x00\x00\x00\x00\x8B", "Fax-920" },
 {NULL}};

static const ModelData Sharp[] =
{{"\x00\xCE\xB8\x80\x80\x11\x85\x0D\xDD\x00\x00\xDD\xDD\x00\x00\xDD\xDD\x00\x00\x00\x00\x00\x00\x00\x00\xED\x22\xB0\x00\x00\x90\x00\x8C", "Sharp UX-460" },
 {"\x00\x4E\xB8\x80\x80\x11\x84\x0D\xDD\x00\x00\xDD\xDD\x00\x00\xDD\xDD\x00\x00\x00\x00\x00\x00\x00\x00\xED\x22\xB0\x00\x00\x90\x00\xAD", "Sharp UX-177" },
 {"\x00\xCE\xB8\x00\x84\x0D\xDD\x00\x00\xDD\xDD\x00\x00\xDD\xDD\xDD\xDD\xDD\x02\x05\x28\x02\x22\x43\x29\xED\x23\x90\x00\x00\x90\x01\x00", "Sharp FO-4810" },
 {NULL}};

static const ModelData Xerox[] =
{{"\x00\x08\x2D\x43\x57\x50\x61\x75\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01\x1A\x02\x02\x10\x01\x82\x01\x30\x34", "635 Workcenter" },
 {NULL}};

static const ModelData PitneyBowes[] = 
{{"\x79\x91\xB1\xB8\x7A\xD8", "9550" },
 {NULL}};

static const ModelData Muratec[] =
{{"\xF4\x91\xFF\xFF\xFF\x42\x2A\xBC\x01\x57", "M4700" },
 {NULL}};

static const NSFData KnownNSF[] =
{
    {"\x00\x00\x0E", "Panasonic", false, 3,10, Panasonic0E },
    {"\x00\x00\x11", "Canon",     false, 3, 5, Canon },
    {"\x00\x00\x19", "Xerox",     true  },
    {"\x00\x00\x09", "Xerox",     true, 3, 10, Xerox },
    {"\x00\x00\x21", "Lanier",    true  },
    {"\x00\x00\x25", "Ricoh",     true,  3,10, Ricoh },
    {"\x00\x00\x26", NULL,        false },
    {"\x00\x00\x45", "Muratec",   false, 3,10, Muratec },
    {"\x00\x00\x51", "Sanyo",     false, 3,10, Sanyo },
    {"\x00\x00\x56", "Brother",   false, 3, 9, Brother },
    {"\x00\x00\x66", "UTAX",      true  },
    {"\x00\x00\x79", "Panasonic", false, 3,10, Panasonic79 },
    {"\x20\x41\x59", "Siemens",   false },
    {"\x59\x59\x01", NULL,        false },
    {"\x86\x00\x8C", "Samsung",   false, 3, 4, Samsung8C },
    {"\x86\x00\x98", "Samsung",   false },
    {"\xAD\x00\x36", "HP",        false, 3, 5, HP },
    {"\xAD\x00\x44", NULL,        true },
    {"\xB5\x00\x2E", "Delrina",   false },
    {"\x00\x00\x31", "Sharp/Olivetti",     false, 3, 10, Sharp },
    {"\xAD\x00\x00", "Pitney Bowes", false, 3, 6, PitneyBowes },
    {"\xB5\x00\x76", "Trust",     false },
    {"\xAD\x00\x42", "FaxTalk",   false },
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
        hexNsf.append(fxStr::format("%02X ", *p));
    }
    // remove trailing space
    hexNsf.resize( hexNsf.length() - 1 );
}

void NSF::decode()
{
    u_int nsfSize = nsf.length();
    for( const NSFData* p = KnownNSF; p->vendorId; p++ ){
        if( nsfSize >= p->vendorIdSize &&
            memcmp( p->vendorId, &nsf[0], p->vendorIdSize )==0 ){
	    if( p->vendorName )
                vendor = p->vendorName;
            if( p->knownModels ){
                for( const ModelData* pp = p->knownModels; pp->modelId; pp++ )
                    if( nsfSize >= p->modelIdPos + p->modelIdSize &&
                        memcmp( pp->modelId, &nsf[p->modelIdPos], p->modelIdSize )==0 )
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
    for( const char *p = (const char*)nsf + NSFData::vendorIdSize,
             *end = p + nsf.length(); p < end; p++ ){
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
