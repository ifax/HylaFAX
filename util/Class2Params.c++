/*	$Id$ */
/*
 * Copyright (c) 1990-1996 Sam Leffler
 * Copyright (c) 1991-1996 Silicon Graphics, Inc.
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
#include "config.h"
#include "Class2Params.h"
#include "Sys.h"
#include "t.30.h"

Class2Params::Class2Params()
{
    vr = (u_int) -1;
    br = (u_int) -1;
    wd = (u_int) -1;
    ln = (u_int) -1;
    df = (u_int) -1;
    ec = (u_int) -1;
    bf = (u_int) -1;
    st = (u_int) -1;
}

int
Class2Params::operator==(const Class2Params& other) const
{
    return vr == other.vr
	&& br == other.br
	&& wd == other.wd
	&& ln == other.ln
	&& df == other.df
	&& ec == other.ec
	&& bf == other.bf
	&& st == other.st;
}

int
Class2Params::operator!=(const Class2Params& other) const
{
    return !(*this == other);
}

fxStr
Class2Params::cmd(bool class2UseHex) const
{
    u_int unset = (u_int) -1;
    fxStr comma(",");
    fxStr notation;
    if (class2UseHex)
        notation = "%X";
    else
        notation = "%u";
    fxStr s;
    if (vr != unset) s.append(fxStr::format(notation, vr));
    s.append(comma);
    if (br != unset) s.append(fxStr::format(notation, br));
    s.append(comma);
    if (wd != unset) s.append(fxStr::format(notation, wd));
    s.append(comma);
    if (ln != unset) s.append(fxStr::format(notation, ln));
    s.append(comma);
    if (df != unset) s.append(fxStr::format(notation, df));
    s.append(comma);
    if (ec != unset) s.append(fxStr::format(notation, ec));
    s.append(comma);
    if (bf != unset) s.append(fxStr::format(notation, bf));
    s.append(comma);
    if (st != unset) s.append(fxStr::format(notation, st));
    return s;
}

bool
Class2Params::is2D() const
{
    return (DF_2DMR <= df && df <= DF_2DMMR);
}

/*
 * Tables to convert from Class 2
 * subparameter codes to a T.30 DIS.
 */
u_int Class2Params::vrDISTab[2] = {
    0,				// VR_NORMAL
    DIS_7MMVRES			// VR_FINE
};
u_int Class2Params::dfDISTab[4] = {
    0,				// 1-D MR
    DIS_2DENCODE,		// + 2-D MR
    DIS_2DENCODE,		// + Uncompressed data
    DIS_2DENCODE,		// + 2-D MMR
};
u_int Class2Params::brDISTab[8] = {
    DISSIGRATE_V27FB<<10,		// BR_2400
    DISSIGRATE_V27<<10,			// BR_4800
    (DISSIGRATE_V27|DISSIGRATE_V29)<<10,// BR_7200
    (DISSIGRATE_V27|DISSIGRATE_V29)<<10,// BR_9600
    0xD,				// BR_12000 (v.27,v.29,v.17,v.33)
    0xD,				// BR_14400 (v.27,v.29,v.17,v.33)
    (DISSIGRATE_V27|DISSIGRATE_V29)<<10,// 6 ?
    (DISSIGRATE_V27|DISSIGRATE_V29)<<10,// 7 ?
};
u_int Class2Params::wdDISTab[8] = {
    DISWIDTH_1728<<6,		// WD_1728
    DISWIDTH_2048<<6,		// WD_2048
    DISWIDTH_2432<<6,		// WD_2432
    DISWIDTH_1728<<6,		// WD_1216 XXX
    DISWIDTH_1728<<6,		// WD_864 XXX
    DISWIDTH_1728<<6,		// 5
    DISWIDTH_1728<<6,		// 6
    DISWIDTH_1728<<6,		// 7
};
u_int Class2Params::lnDISTab[3] = {
    DISLENGTH_A4<<4,		// LN_A4
    DISLENGTH_A4B4<<4,		// LN_B4
    DISLENGTH_UNLIMITED<<4	// LN_INF
};
u_int Class2Params::stDISTab[8] = {
    DISMINSCAN_0MS<<1,		// ST_0MS
    DISMINSCAN_5MS<<1,		// ST_5MS
    DISMINSCAN_10MS2<<1,	// ST_10MS2
    DISMINSCAN_10MS<<1,		// ST_10MS
    DISMINSCAN_20MS2<<1,	// ST_20MS2
    DISMINSCAN_20MS<<1,		// ST_20MS
    DISMINSCAN_40MS2<<1,	// ST_40MS2
    DISMINSCAN_40MS<<1,		// ST_40MS
};

/*
 * Convert a Class 2 bit rate code to a T.30
 * DCS code.  Beware that certain entries map
 * speeds and protocols (e.g. v.17 vs. v.33).
 */
u_int Class2Params::brDCSTab[8] = {
    DCSSIGRATE_2400V27,		// BR_2400
    DCSSIGRATE_4800V27,		// BR_4800
    DCSSIGRATE_7200V29,		// BR_7200
    DCSSIGRATE_9600V29,		// BR_9600
    DCSSIGRATE_12000V17,	// BR_12000
    DCSSIGRATE_14400V17,	// BR_14400
    DCSSIGRATE_9600V29,		// 6 ?
    DCSSIGRATE_9600V29,		// 7 ?
};
u_int Class2Params::stDCSTab[8] = {
    DISMINSCAN_0MS<<1,		// ST_0MS
    DISMINSCAN_5MS<<1,		// ST_5MS
    DISMINSCAN_10MS<<1,		// ST_10MS2
    DISMINSCAN_10MS<<1,		// ST_10MS
    DISMINSCAN_20MS<<1,		// ST_20MS2
    DISMINSCAN_20MS<<1,		// ST_20MS
    DISMINSCAN_40MS<<1,		// ST_40MS2
    DISMINSCAN_40MS<<1,		// ST_40MS
};

/*
 * Tables for mapping a T.30 DIS to Class 2
 * subparameter code values.
 */
u_int Class2Params::DISdfTab[2] = {
    DF_1DMR,			// !DIS_2DENCODE
    DF_2DMR			// DIS_2DENCODE
};
u_int Class2Params::DISvrTab[2] = {
    VR_NORMAL,			// !DIS_7MMVRES
    VR_FINE			// DIS_7MMVRES
};
/*
 * Beware that this table returns the ``best speed''
 * based on the signalling capabilities of the DIS.
 */
u_int Class2Params::DISbrTab[16] = {
    BR_2400,			// 0x0/V27
    BR_14400,			// 0x1/V17
    BR_9600,			// 0x2/undefined
    BR_14400,			// 0x3/V17+undefined
    BR_4800,			// 0x4/V27
    BR_14400,			// 0x5/V17+V27
    BR_4800,			// 0x6/V27+undefined
    BR_14400,			// 0x7/V17+V27+undefined
    BR_9600,			// 0x8/V29
    BR_14400,			// 0x9/V17+V29
    BR_9600,			// 0xA/V29+undefined
    BR_14400,			// 0xB/V17+V29+undefined
    BR_9600,			// 0xC/V29+V27
    BR_14400,			// 0xD/V17+V29+V29
    BR_9600,			// 0xE/V29+V27+undefined
    BR_14400,			// 0xF/V17+V29+V29+undefined
};
u_int Class2Params::DISwdTab[4] = {
    WD_1728,			// DISWIDTH_1728
    WD_2432,			// DISWIDTH_2432
    WD_2048,			// DISWIDTH_2048
    WD_2432			// invalid, but treated as 2432
};
u_int Class2Params::DISlnTab[4] = {
    LN_A4,			// DISLENGTH_A4
    LN_INF,			// DISLENGTH_UNLIMITED
    LN_B4,			// DISLENGTH_B4
    LN_A4			// undefined
};
u_int Class2Params::DISstTab[8] = {
    ST_20MS,			// DISMINSCAN_20MS
    ST_40MS,			// DISMINSCAN_40MS
    ST_10MS,			// DISMINSCAN_10MS
    ST_10MS2,			// DISMINSCAN_10MS2
    ST_5MS,			// DISMINSCAN_5MS
    ST_40MS2,			// DISMINSCAN_40MS2
    ST_20MS2,			// DISMINSCAN_20MS2
    ST_0MS			// DISMINSCAN_0MS
};

/*
 * Convert a T.30 DIS to a Class 2 parameter block.
 */
void
Class2Params::setFromDIS(u_int dis, u_int xinfo)
{
    vr = DISvrTab[(dis & DIS_7MMVRES) >> 9];
    /*
     * Beware that some modems (e.g. the Supra) indicate they
     * support the V.17 bit rates, but not the normal V.27+V.29
     * signalling rates.  The DISbrTab is NOT setup to mark the
     * V.27 and V.29 if V.17 is set.  Instead we let the upper
     * layers select appropriate signalling rate knowing that
     * we'll fall back to something that the modem will support.
     */
    if (dis & DIS_V8)
	br = BR_33600;	// Is V.8 only used by V.34 (SuperG3) faxes?
    else
	br = DISbrTab[(dis & DIS_SIGRATE) >> 10];
    wd = DISwdTab[(dis & DIS_PAGEWIDTH) >> 6];
    ln = DISlnTab[(dis & DIS_PAGELENGTH) >> 4];
    if (xinfo & DIS_G4COMP)
	df = DF_2DMMR;
    else if (xinfo & DIS_2DUNCOMP)
	df = DF_2DMRUNCOMP;
    else
	df = DISdfTab[(dis & DIS_2DENCODE) >> 8];
    ec = (xinfo & DIS_ECMODE) ? EC_ENABLE : EC_DISABLE;
    bf = BF_DISABLE;			// XXX from xinfo
    st = DISstTab[(dis & DIS_MINSCAN) >> 1];
}

u_int Class2Params::DCSbrTab[16] = {
    BR_2400,			// 0x0/2400 V27
    BR_14400,			// 0x1/14400 V17
    BR_14400,			// 0x2/14400 V33
    0,				// 0x3/undefined 
    BR_4800,			// 0x4/4800 V27
    BR_12000,			// 0x5/12000 V17
    BR_12000,			// 0x6/12000 V33
    0,				// 0x7/undefined 
    BR_9600,			// 0x8/9600 V29
    BR_9600,			// 0x9/9600 V17
    BR_9600,			// 0xA/9600 V33
    0,				// 0xB/undefined
    BR_7200,			// 0xC/7200 V29
    BR_7200,			// 0xD/7200 V17
    BR_7200,			// 0xE/7200 V33
    0,				// 0xF/undefined 
};

/*
 * Convert a T.30 DCS to a Class 2 parameter block.
 */
void
Class2Params::setFromDCS(u_int dcs, u_int xinfo)
{
    setFromDIS(dcs, xinfo);
    br = DCSbrTab[(dcs & DCS_SIGRATE) >> 10];	// override DIS setup
}

/*
 * Return a 24-bit T.30 DCS frame that reflects the parameters.
 */
u_int
Class2Params::getDCS() const
{
    u_int dcs = DCS_T4RCVR
	    | vrDISTab[vr&1]
	    | brDCSTab[br&15]
	    | wdDISTab[wd&7]
	    | lnDISTab[ln&3]
	    | dfDISTab[df&3]
	    | stDCSTab[st&7]
	    ;
    return (dcs);
}

/*
 * Return a 32-bit DCS frame extension.
 */
u_int
Class2Params::getXINFO() const
{
    return (0);					// for now
}

/*
 * Return the number of bytes that can be
 * transferred at the selected signalling
 * rate in <ms> milliseconds.
 */
u_int
Class2Params::transferSize(u_int ms) const
{
    return (bitRate()/8 * ms) / 1000;
}

/*
 * Return the minimum number of bytes in a
 * scanline as determined by the signalling
 * rate, vertical resolution, and min-scanline
 * time parameters.
 */
u_int
Class2Params::minScanlineSize() const
{
    static const u_int stTimes[8] =
	{ 0, 5, 10, 10, 20, 20, 40, 40 };
    u_int ms = stTimes[st&7];
    if ((st & 1) == 0 && vr == VR_FINE)
	ms /= 2;
    return transferSize(ms);
}

u_int
Class2Params::pageWidth() const
{
    static const u_int widths[8] = {
	1728,	// 1728 in 215 mm line
	2048,	// 2048 in 255 mm line
	2432,	// 2432 in 303 mm line
	1216,	// 1216 in 151 mm line
	864,	// 864 in 107 mm line
	1728,	// undefined
	1728,	// undefined
	1728,	// undefined
    };
    return (widths[wd&7]);
}

void
Class2Params::setPageWidthInMM(u_int w)
{
    wd = (w == 255 ? WD_2048 : w == 303 ? WD_2432 : WD_1728);
}

void
Class2Params::setPageWidthInPixels(u_int w)
{
    wd = (w == 1728 ? WD_1728 :
	  w == 2048 ? WD_2048 :
	  w == 2432 ? WD_2432 :
	  w == 1216 ? WD_1216 :
	  w ==  864 ? WD_864 :
		      WD_1728);
}

u_int
Class2Params::pageLength() const
{
    static const u_int lengths[4] = {
	297,		// A4 paper
	364,		// B4 paper
	(u_int) -1,	// unlimited
	280,		// US letter (used internally)
    };
    return (lengths[ln&3]);
}

void
Class2Params::setPageLengthInMM(u_int l)
{
    ln = (l == (u_int) -1 ?  LN_INF :
	  l <= 280 ?	     LN_LET :
	  l <= 300 ?	     LN_A4 :
			     LN_B4);
}

u_int
Class2Params::verticalRes() const
{
    return (vr == VR_NORMAL ? 98 : vr == VR_FINE ? 196 : (u_int) -1);
}

void
Class2Params::setVerticalRes(u_int res)
{
    vr = (res > 150 ? VR_FINE : VR_NORMAL);
}

fxStr
Class2Params::encodePage() const
{
    int v = (vr&1) | ((wd&7)<<1) | ((ln&3)<<4) | ((df&3)<<6);
    return fxStr(v, "%02x");
}

void
Class2Params::decodePage(const char* s)
{
    u_int v = (u_int) strtoul(s, NULL, 16);
    vr = (v>>0) & 1;
    wd = (v>>1) & 7;
    ln = (v>>4) & 3;
    if (ln == LN_LET)			// force protocol value
	ln = LN_A4;
    df = (v>>6) & 3;
}

u_int
Class2Params::encode() const
{
    return  ((vr&7)<<0)
	  | ((br&15)<<3)
	  | ((wd&7)<<9)
	  | ((ln&3)<<12)
	  | ((df&3)<<14)
	  | ((ec&1)<<16)
	  | ((bf&1)<<17)
	  | ((st&7)<<18)
	  | (1<<21)		// this is the version identifier
	  ;
}

void
Class2Params::decode(u_int v)
{
    if (v>>21 == 1) {		// check version
	vr = (v>>0) & 7;
	br = (v>>3) & 15;
	wd = (v>>9) & 7;
	ln = (v>>12) & 3;
	if (ln == LN_LET)	// force protocol value
	    ln = LN_A4;
	df = (v>>14) & 3;
	ec = (v>>16) & 1;
	bf = (v>>17) & 1;
	st = (v>>18) & 7;
    } else {			// original version
	vr = (v>>0) & 1;
	br = (v>>1) & 7;
	wd = (v>>4) & 7;
	ln = (v>>7) & 3;
	if (ln == LN_LET)	// force protocol value
	    ln = LN_A4;
	df = (v>>9) & 3;
	ec = (v>>11) & 1;
	bf = (v>>12) & 1;
	st = (v>>13) & 7;
    }
}

u_int
Class2Params::encodeCaps() const
{
    // we can't use all of BR_ALL because we're limited to 32 bits
    return (vr&VR_ALL)
	 | ((br&(BR_2400|BR_4800|BR_7200|BR_9600|BR_12000|BR_14400))<<8)
	 | ((wd&WD_ALL)<<14)
	 | ((ln&LN_ALL)<<19)
	 | ((df&DF_ALL)<<22)
	 | ((ec&EC_ALL)<<26)
	 | ((bf&BF_ALL)<<28)
	 | ((st&ST_ALL)<<30)
	 ;
}

void
Class2Params::decodeCaps(u_int v)
{
    vr = (v>>0)  & VR_ALL;
    br = (v>>8)  & (BR_2400|BR_4800|BR_7200|BR_9600|BR_12000|BR_14400);	// see encoding
    wd = (v>>14) & WD_ALL;
    ln = (v>>19) & LN_ALL;
    df = (v>>22) & DF_ALL;
    ec = (v>>26) & EC_ALL;
    bf = (v>>28) & BF_ALL;
    st = (v>>30) & ST_ALL;
}

/*
 * Routines for printing some Class 2 capabilities.
 */
const char* Class2Params::verticalResNames[2] = {
    "3.85 line/mm",
    "7.7 line/mm",
};
const char* Class2Params::verticalResName() const
    { return (verticalResNames[vr&1]); }

const char* Class2Params::bitRateNames[16] = {
    "2400 bit/s",		// BR_2400
    "4800 bit/s",		// BR_4800
    "7200 bit/s",		// BR_7200
    "9600 bit/s",		// BR_9600
    "12000 bit/s",		// BR_12000
    "14400 bit/s",		// BR_14400
    "16800 bit/s",		// BR_16800
    "19200 bit/s",		// BR_19200
    "21600 bit/s",		// BR_21600
    "24000 bit/s",		// BR_24000
    "26400 bit/s",		// BR_26400
    "28800 bit/s",		// BR_28800
    "31200 bit/s",		// BR_31200
    "33600 bit/s",		// BR_33600
    "0 bit/s",			// 14 ???
    "0 bit/s",			// 15 ???
};
const char* Class2Params::bitRateName() const
    { return (bitRateNames[br&15]); }
u_int
Class2Params::bitRate() const
{
    static const u_int brRates[16] = {
	2400,	// BR_2400
	4800,	// BR_4800
	7200,	// BR_7200
	9600,	// BR_9600
	12000,	// BR_12000
	14400,	// BR_14400
	16800,	// BR_16800
	19200,	// BR_19200
	21600,	// BR_21600
	24000,	// BR_24000
	26400,	// BR_26400
	28800,	// BR_28800
	31200,	// BR_31200
	33600,	// BR_33600
	14400,	// 14? XXX
	14400,	// 15? XXX
    };
    return (brRates[br & 15]);
}

const char* Class2Params::dataFormatNames[4] = {
    "1-D MR",			// DF_1DMR
    "2-D MR",			// DF_2DMR
    "2-D Uncompressed Mode",	// DF_2DMRUNCOMP
    "2-D MMR"			// DF_2DMMR
};
const char* Class2Params::dataFormatName() const
     { return (dataFormatNames[df&3]); }

const char* Class2Params::pageWidthNames[8] = {
    "page width 1728 pixels in 215 mm",
    "page width 2048 pixels in 255 mm",
    "page width 2432 pixels in 303 mm",
    "page width 1216 pixels in 151 mm",
    "page width 864 pixels in 107 mm",
    "undefined page width (wd=5)",
    "undefined page width (wd=6)",
    "undefined page width (wd=7)",
};
const char* Class2Params::pageWidthName() const
    { return (pageWidthNames[wd&7]); }

const char* Class2Params::pageLengthNames[4] = {
    "A4 page length (297 mm)",
    "B4 page length (364 mm)",
    "unlimited page length",
    "invalid page length (ln=3)",
};
const char* Class2Params::pageLengthName() const
    { return (pageLengthNames[ln&3]); }

const char* Class2Params::scanlineTimeNames[8] = {
    "0 ms/scanline",
    "5 ms/scanline",
    "10 ms, 5 ms/scanline",
    "10 ms/scanline",
    "20 ms, 10 ms/scanline",
    "20 ms/scanline",
    "40 ms, 20 ms/scanline",
    "40 ms/scanline",
};
const char* Class2Params::scanlineTimeName() const
    { return (scanlineTimeNames[st&7]); }
const char* Class2Params::ecmNames[4] = {
    "no ECM",
    "T.30 Annex A, ECM",
    "T.30 Annex C, half duplex ECM",
    "T.30 Annex C, full duplex ECM",
};
const char* Class2Params::ecmName() const
    { return (ecmNames[ec&3]); }
