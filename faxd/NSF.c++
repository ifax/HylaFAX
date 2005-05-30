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
    static const u_int   vendorIdSize; // Country & provider code (T.35)
    const char* vendorName;
    bool        inverseStationIdOrder;
    u_int         modelIdPos;
    u_int         modelIdSize;
    const ModelData* knownModels;
    bool        inverseBitOrder;
};

const u_int NSFData::vendorIdSize = 3; // Country & provider code (T.35)

static const ModelData Canon[] =
{{"\x80\x00\x80\x48\x00", "Faxphone B640"},
 {"\x80\x00\x80\x49\x10", "Fax B100"},
 {"\x80\x00\x8A\x49\x10", "Laser Class 9000 Series"},
 {NULL}};
  

static const ModelData Brother[] =
{{"\x55\x55\x00\x88\x90\x80\x5F\x00\x15\x51", "Fax-560/770"},
 {"\x55\x55\x00\x80\xB0\x80\x00\x00\x59\xD4", "Personal fax 190"},
 {"\x55\x55\x00\x8C\x90\x80\xF0\x02\x20", "MFC-8600"},
 {"\x55\x55\x00\x8C\x90\x80\x7A\x06", "MFC-3100C"},
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

static const ModelData Muratec45[] =
{{"\xF4\x91\xFF\xFF\xFF\x42\x2A\xBC\x01\x57", "M4700" },
 {NULL}};

// Muratec uses unregistered Japan code "00 00 48" 
static const ModelData Muratec48[] =
{{"\x53\x53\x61", "M620" },
 {NULL}};

/*
 * Country code first byte, then manufacturer is last two bytes. See T.35.  
 * Japan is x00, USA xB5, UK xB4, Canada x20.
 */

static const NSFData KnownNSF[] =
{
    {"\x00\x00\x00", "unknown - Japan", true },
    {"\x00\x00\x01", "Anjitsu",  false },
    {"\x00\x00\x02", "Nippon Telephone", false },
    {"\x00\x00\x05", "Mitsuba Electric", false },
    {"\x00\x00\x06", "Master Net", false },
    {"\x00\x00\x09", "Xerox/Toshiba", true, 3, 10, Xerox },
    {"\x00\x00\x0A", "Kokusai",   false },
    {"\x00\x00\x0D", "Logic System International", false },
    {"\x00\x00\x0E", "Panasonic", false, 3,10, Panasonic0E },
    {"\x00\x00\x11", "Canon",     false, 3, 5, Canon },
    {"\x00\x00\x15", "Toyotsushen Machinery", false },
    {"\x00\x00\x16", "System House Mind", false },
    {"\x00\x00\x19", "Xerox",     true  },
    {"\x00\x00\x1D", "Hitachi Software", false },
    {"\x00\x00\x21", "Oki Electric/Lanier", true },
    {"\x00\x00\x25", "Ricoh",     true,  3,10, Ricoh },
    {"\x00\x00\x26", "Konica",    false },
    {"\x00\x00\x29", "Japan Wireless", false },
    {"\x00\x00\x2D", "Sony",      false },
    {"\x00\x00\x31", "Sharp/Olivetti", false, 3, 10, Sharp },
    {"\x00\x00\x35", "Kogyu", false },
    {"\x00\x00\x36", "Japan Telecom", false },
    {"\x00\x00\x3D", "IBM Japan", false },
    {"\x00\x00\x39", "Panasonic", false },
    {"\x00\x00\x41", "Swasaki Communication", false },
    {"\x00\x00\x45", "Muratec",   false, 3,10, Muratec45 },
    {"\x00\x00\x46", "Pheonix",   false },
    {"\x00\x00\x48", "Muratec",   false, 3,3, Muratec48 },
    {"\x00\x00\x49", "Japan Electric", false },
    {"\x00\x00\x4D", "Okura Electric", false },
    {"\x00\x00\x51", "Sanyo",     false, 3,10, Sanyo },
    {"\x00\x00\x55", "unknown - Japan", false },
    {"\x00\x00\x56", "Brother",   false, 3, 8, Brother },
    {"\x00\x00\x59", "Fujitsu",   false },
    {"\x00\x00\x5D", "Kuoni",     false },
    {"\x00\x00\x61", "Casio",     false },
    {"\x00\x00\x65", "Tateishi Electric", false },
    {"\x00\x00\x66", "Utax/Mita", true  },
    {"\x00\x00\x69", "Hitachi Production",   false },
    {"\x00\x00\x6D", "Hitachi Telecom", false },
    {"\x00\x00\x71", "Tamura Electric Works", false },
    {"\x00\x00\x75", "Tokyo Electric Corp.", false },
    {"\x00\x00\x76", "Advance",   false },
    {"\x00\x00\x79", "Panasonic", false, 3,10, Panasonic79 },
    {"\x00\x00\x7D", "Seiko",     false },
    {"\x00\x08\x00", "Daiko",     false },
    {"\x00\x10\x00", "Funai Electric", false },
    {"\x00\x20\x00", "Eagle System", false },
    {"\x00\x30\x00", "Nippon Business Systems", false },
    {"\x00\x40\x00", "Comtron",   false },
    {"\x00\x48\x00", "Cosmo Consulting", false },
    {"\x00\x50\x00", "Orion Electric", false },
    {"\x00\x60\x00", "Nagano Nippon", false },
    {"\x00\x70\x00", "Kyocera",   false },
    {"\x00\x80\x00", "Kanda Networks", false },
    {"\x00\x88\x00", "Soft Front", false },
    {"\x00\x90\x00", "Arctic",    false },
    {"\x00\xA0\x00", "Nakushima", false },
    {"\x00\xB0\x00", "Minolta", false },
    {"\x00\xC0\x00", "Tohoku Pioneer", false },
    {"\x00\xD0\x00", "USC",       false },
    {"\x00\xE0\x00", "Hiboshi",   false },
    {"\x00\xF0\x00", "Sumitomo Electric", false },
    {"\x20\x41\x59", "Siemens",   false },
    {"\x59\x59\x01", NULL,        false },
    {"\xB4\x00\xB0", "DCE",       false },
    {"\xB4\x00\xB1", "Hasler",    false },
    {"\xB4\x00\xB2", "Interquad", false },
    {"\xB4\x00\xB3", "Comwave",   false },
    {"\xB4\x00\xB4", "Iconographic", false },
    {"\xB4\x00\xB5", "Wordcraft", false },
    {"\xB4\x00\xB6", "Acorn",     false },
    {"\xB5\x00\x01", "Picturetel", false },
    {"\xB5\x00\x20", "Conexant",  false },
    {"\xB5\x00\x22", "Comsat",    false },
    {"\xB5\x00\x24", "Octel",     false },
    {"\xB5\x00\x26", "ROLM",      false },
    {"\xB5\x00\x28", "SOFNET",    false },
    {"\xB5\x00\x29", "TIA TR-29 Committee", false },
    {"\xB5\x00\x2A", "STF Tech",  false },
    {"\xB5\x00\x2C", "HKB",       false },
    {"\xB5\x00\x2E", "Delrina",   false },
    {"\xB5\x00\x30", "Dialogic",  false },
    {"\xB5\x00\x32", "Applied Synergy", false },
    {"\xB5\x00\x34", "Syncro Development", false },
    {"\xB5\x00\x36", "Genoa",     false },
    {"\xB5\x00\x38", "Texas Instruments", false },
    {"\xB5\x00\x3A", "IBM",       false },
    {"\xB5\x00\x3C", "ViaSat",    false },
    {"\xB5\x00\x3E", "Ericsson",  false },
    {"\xB5\x00\x42", "Bogosian",  false },
    {"\xB5\x00\x44", "Adobe",     false },
    {"\xB5\x00\x46", "Fremont Communications", false },
    {"\xB5\x00\x48", "Hayes",     false },
    {"\xB5\x00\x4A", "Lucent",    false },
    {"\xB5\x00\x4C", "Data Race", false },
    {"\xB5\x00\x4E", "TRW",       false },
    {"\xB5\x00\x52", "Audiofax",  false },
    {"\xB5\x00\x54", "Computer Automation", false },
    {"\xB5\x00\x56", "Serca",     false },
    {"\xB5\x00\x58", "Octocom",   false },
    {"\xB5\x00\x5C", "Power Solutions", false },
    {"\xB5\x00\x5A", "Digital Sound", false },
    {"\xB5\x00\x5E", "Pacific Data", false },
    {"\xB5\x00\x60", "Commetrex", false },
    {"\xB5\x00\x62", "BrookTrout", false },
    {"\xB5\x00\x64", "Gammalink", false },
    {"\xB5\x00\x66", "Castelle",  false },
    {"\xB5\x00\x68", "Hybrid Fax", false },
    {"\xB5\x00\x6A", "Omnifax",   false },
    {"\xB5\x00\x6C", "HP",        false },
    {"\xB5\x00\x6E", "Microsoft", false },
    {"\xB5\x00\x72", "Speaking Devices", false },
    {"\xB5\x00\x74", "Compaq",    false },
/*
    {"\xB5\x00\x76", "Trust - Cryptek", false },	// collision with Microsoft
*/
    {"\xB5\x00\x76", "Microsoft", false },		// uses LSB for country but MSB for manufacturer
    {"\xB5\x00\x78", "Cylink",    false },
    {"\xB5\x00\x7A", "Pitney Bowes", false },
    {"\xB5\x00\x7C", "Digiboard", false },
    {"\xB5\x00\x7E", "Codex",     false },
    {"\xB5\x00\x82", "Wang Labs", false },
    {"\xB5\x00\x84", "Netexpress Communications", false },
    {"\xB5\x00\x86", "Cable-Sat", false },
    {"\xB5\x00\x88", "MFPA",      false },
    {"\xB5\x00\x8A", "Telogy Networks", false },
    {"\xB5\x00\x8E", "Telecom Multimedia Systems", false },
    {"\xB5\x00\x8C", "AT&T",      false },
    {"\xB5\x00\x92", "Nuera",     false },
    {"\xB5\x00\x94", "K56flex",   false },
    {"\xB5\x00\x96", "MiBridge",  false },
    {"\xB5\x00\x98", "Xerox",     false },
    {"\xB5\x00\x9A", "Fujitsu",   false },
    {"\xB5\x00\x9B", "Fujitsu",   false },
    {"\xB5\x00\x9C", "Natural Microsystems",  false },
    {"\xB5\x00\x9E", "CopyTele",  false },
    {"\xB5\x00\xA2", "Murata",    false },
    {"\xB5\x00\xA4", "Lanier",    false },
    {"\xB5\x00\xA6", "Qualcomm",  false },
    {"\xB5\x00\xAA", "HylaFAX",   false },
    /*
     * T.30 states that all HDLC frame data should be in MSB
     * order except as noted.  T.30 5.3.6.2.7 does not explicitly
     * make an exception for NSF, but 5.3.6.2.4-11 do make exceptions
     * for TSI, etc., which should be in LSB order.  Therefore, it is
     * widely accepted that NSF is in LSB order.  However, some
     * manufacturers unfortunately use MSB for NSF.
     *
     * Thus, country code x61 (Korea) turns into x86 (Papua New Guinea),
     * code xB5 (USA) turns into xAD (Tunisia), code x26 (China) turns
     * into x64 (Lebanon), and code x3D (France) turns into xBC (Vietnam).
     * Therefore, we need to convert these to produce a legible station ID.
     */
    {"\x64\x01\x00", "unknown - China", false, 0, 0, NULL, false }, // reverse-ordered code but not station-id
    {"\x64\x01\x01", "unknown - China", false, 0, 0, NULL, true },
    {"\x64\x01\x02", "unknown - China", false, 0, 0, NULL, true },
    {"\x86\x00\x0A", "unknown - Korea", false, 0, 0, NULL, true },
    {"\x86\x00\x0E", "unknown - Korea", false, 0, 0, NULL, true },
    {"\x86\x00\x10", "Samsung",   false, 0, 0, NULL, true },
    {"\x86\x00\x1A", "unknown - Korea", false, 0, 0, NULL, true },
    {"\x86\x00\x52", "unknown - Korea", false, 0, 0, NULL, true },
    {"\x86\x00\x5A", "unknown - Korea", false, 0, 0, NULL, true },
    {"\x86\x00\x8C", "Samsung",   false, 3, 4, Samsung8C, true },
    {"\x86\x00\x98", "Samsung",   false, 0, 0, NULL, true },
    {"\x86\x00\xC9", "unknown - Korea", false, 0, 0, NULL, true },
    {"\x86\x00\xEE", "unknown - Korea", false, 0, 0, NULL, true },
    {"\xAD\x00\x00", "Pitney Bowes", false, 3, 6, PitneyBowes, true },
    {"\xAD\x00\x24", "Octel",     false, 0, 0, NULL, true },
    {"\xAD\x00\x36", "HP",        false, 3, 5, HP, true },
    {"\xAD\x00\x42", "FaxTalk",   false, 0, 0, NULL, true },
    {"\xAD\x00\x44", NULL,        true,  0, 0, NULL, true },
    {"\xAD\x00\x98", "unknown - USA", true, 0, 0, NULL, true },
    {"\xBC\x53\x01", "Minolta",   false, 0, 0, NULL, true },
    {NULL}
};

NSF::NSF()
{
    clear();
}


NSF::NSF( const char* hexNSF, bool useHex )
{
    clear();
    loadHexData( hexNSF, useHex );
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

void NSF::loadHexData( const char* hexNSF, bool useHex )
{
    hexNsf.append( hexNSF );
    const char *p = hexNSF;
    char *pNext = NULL;
    for( ;; ){
        int val = strtol( p, &pNext, (useHex ? 16 : 10) );
        if( pNext == p )
            break;
        p = pNext;
	if (p[0] != '\0') p++;		// skip delimiter
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
            findStationId( p->inverseStationIdOrder, p->inverseBitOrder );
            vendorDecoded = true;
        }
    }
    if( !vendorFound() )
	findStationId( 0, 0 );
}

void NSF::findStationId( bool reverseOrder, bool reverseBitOrder )
{
    const char* id = NULL;
    u_int       idSize = 0;
    const char* maxId = NULL;
    u_int       maxIdSize = 0;
    /*
     * Convert to LSBMSB if it's not.
     */
    if ( reverseBitOrder ) {
	for ( u_int i = 0 ; i < nsf.length(); i++ ) {
	    // a one-byte bit-order converter...
	    nsf[i] =  (((nsf[i]>>0)&1)<<7)|(((nsf[i]>>1)&1)<<6)|
		      (((nsf[i]>>2)&1)<<5)|(((nsf[i]>>3)&1)<<4)|
		      (((nsf[i]>>4)&1)<<3)|(((nsf[i]>>5)&1)<<2)|
		      (((nsf[i]>>6)&1)<<1)|(((nsf[i]>>7)&1)<<0);
	}
    }
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
