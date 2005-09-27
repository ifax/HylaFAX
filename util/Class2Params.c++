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
Class2Params::cmd(bool class2UseHex, bool ecm20) const
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
    if (ec != unset) s.append(fxStr::format(notation, ec - (ecm20 && ec ? 1 : 0)));
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
 * Tables for mapping a T.30 DIS to Class 2
 * subparameter code values.
 */
u_int Class2Params::DISdfTab[2] = {
    DF_1DMH,			// !DIS_2DENCODE
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
    BR_2400,			// 0x0 V27ter fall-back (BR_4800 could be assumed, T.30 Table 2 Note 3)
    BR_14400,			// 0x1 undefined
    BR_9600,			// 0x2 undefined
    BR_14400,			// 0x3 undefined
    BR_4800,			// 0x4 V27ter
    BR_14400,			// 0x5 reserved
    BR_4800,			// 0x6 reserved
    BR_14400,			// 0x7 reserved
    BR_9600,			// 0x8 V29
    BR_14400,			// 0x9 undefined
    BR_9600,			// 0xA undefined
    BR_14400,			// 0xB undefined
    BR_9600,			// 0xC V27ter+V29
    BR_14400,			// 0xD V27ter+V29+V17 (not V.33, post-1994)
    BR_14400,			// 0xE V27ter+V29+V33 (invalid, post-1994, T.30 Table 2 Notes 31, 32)
    BR_14400,			// 0xF undefined
};
u_int Class2Params::DISwdTab[4] = {
    WD_A4,			// DISWIDTH_A4
    WD_A3,			// DISWIDTH_A3
    WD_B4			// DISWIDTH_B4
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
 * Convert a T.30 DIS to a Class 2 parameter block.
 *
 * Note that DIS is a list of *capabilities* and not necessarily maximums.
 * Thus the values here can be set to bitmaps rather than settings.
 */  
void
Class2Params::setFromDIS(FaxParams& dis_caps)
{  
    assign(dis_caps);
    u_int dis = 0;
    u_int xinfo = 0;

    dis |= getByte(0) << 16;
    dis |= getByte(1) << 8;
    dis |= getByte(2) << 0;

    xinfo |= getByte(3) << 24;
    xinfo |= getByte(4) << 16;
    xinfo |= getByte(5) << 8;
    xinfo |= getByte(6) << 0;

    setFromDIS(dis, xinfo);

    if (ec != EC_DISABLE) {
	if (dis_caps.isBitEnabled(FaxParams::BITNUM_JBIG_BASIC)) df |= BIT(DF_JBIG);
	if (dis_caps.isBitEnabled(FaxParams::BITNUM_JPEG)) df |= BIT(DF_JPEG_GREY);
	//if (dis_caps.isBitEnabled(FaxParams::BITNUM_JBIG)) df |= BIT(DF_JBIG_GREY);
	if (dis_caps.isBitEnabled(FaxParams::BITNUM_FULLCOLOR)) {
	    if (df & BIT(DF_JPEG_GREY)) df |= BIT(DF_JPEG_COLOR);
	    //if (df & BIT(DF_JBIG_GREY)) df |= BIT(DF_JBIG_COLOR);
	}
    }
}

/*
 * Convert a T.30 DIS to a Class 2 parameter block.
 */
void
Class2Params::setFromDIS(u_int dis, u_int xinfo)
{
    // VR is a bitmap of available settings, not a maximum
    vr = DISvrTab[(dis & DIS_7MMVRES) >> 9];
    if (xinfo & DIS_METRES) {
	if (xinfo & DIS_200X400) vr |= VR_R8;
	if (xinfo & DIS_400X400) vr |= VR_R16;
    }
    if (xinfo & DIS_INCHRES) {
	vr |= VR_200X100;
	if (dis & DIS_7MMVRES) vr |= VR_200X200;
	if (xinfo & DIS_200X400) vr |= VR_200X400;
    }
    if (xinfo & DIS_300X300) vr |= VR_300X300;
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

    // DF here is a bitmap
    df = BIT(DF_1DMH);		// required support for all G3 facsimile
    if ((xinfo & DIS_G4COMP) && (xinfo & DIS_ECMODE))	// MMR requires ECM
	df |= BIT(DF_2DMMR);
    if (xinfo & DIS_2DUNCOMP)
	df |= BIT(DF_2DMRUNCOMP);
    if (dis & DIS_2DENCODE)
	df |= BIT(DF_2DMR);

    if (xinfo & DIS_ECMODE)
	ec = (dis & DIS_FRAMESIZE) ? EC_ENABLE64 : EC_ENABLE256;
    else
	ec = EC_DISABLE;
    bf = BF_DISABLE;			// XXX from xinfo
    st = DISstTab[(dis & DIS_MINSCAN) >> 1];
}

/*
 * Convert a T.30 DCS to a Class 2 parameter block.
 */
void 
Class2Params::setFromDCS(FaxParams& dcs_caps)
{
    assign(dcs_caps);
    u_int dcs = 0;
    u_int xinfo = 0;
  
    dcs |= getByte(0) << 16;
    dcs |= getByte(1) << 8;
    dcs |= getByte(2) << 0;

    xinfo |= getByte(3) << 24;
    xinfo |= getByte(4) << 16;
    xinfo |= getByte(5) << 8;
    xinfo |= getByte(6) << 0;

    setFromDCS(dcs, xinfo);

    if (dcs_caps.isBitEnabled(FaxParams::BITNUM_LETTER_SIZE) || 
	dcs_caps.isBitEnabled(FaxParams::BITNUM_LEGAL_SIZE)) {
	// we map letter and legal onto WD_A4 + LN_INF for convenience
	wd = WD_A4;
	ln = LN_INF;
    }
    if (dcs_caps.isBitEnabled(FaxParams::BITNUM_JBIG_BASIC)) df = DF_JBIG;
    if (dcs_caps.isBitEnabled(FaxParams::BITNUM_JBIG_L0)) df = DF_JBIG;
    if (dcs_caps.isBitEnabled(FaxParams::BITNUM_JPEG)) df = DF_JPEG_GREY;
    //if (dcs_caps.isBitEnabled(FaxParams::BITNUM_JBIG)) df = DF_JBIG_GREY;
    if (dcs_caps.isBitEnabled(FaxParams::BITNUM_FULLCOLOR)) {
	if (df == DF_JPEG_GREY) df = DF_JPEG_COLOR;
	//if (df == DF_JBIG_GREY) df = DF_JBIG_COLOR;
    }
}

/*
 * Convert a T.30 DCS to a Class 2 parameter block.
 */
void
Class2Params::setFromDCS(u_int dcs, u_int xinfo)
{
    setFromDIS(dcs, xinfo);
    // override DIS setup
    br = DCSbrTab[(dcs & DCS_SIGRATE) >> 10];
    if (xinfo & DCS_INCHRES) {
	if (xinfo & DCS_400X400) vr = VR_R16;	// rather than adding a VR_400X400
	else if (xinfo & DCS_300X300) vr = VR_300X300;
	else if (xinfo & DCS_200X400) vr = VR_200X400;
	else if (dcs & DCS_7MMVRES) vr = VR_200X200;
	else vr = VR_200X100;
    } else {			// bit 44 of DCS is 0
	// some manufacturers don't send DCS_INCHRES with DCS_300X300
	if (xinfo & DCS_300X300) vr = VR_300X300;
	else if (xinfo & DCS_400X400) vr = VR_R16;
	else if (xinfo & DCS_200X400) vr = VR_R8;
	else vr = DISvrTab[(dcs & DCS_7MMVRES) >> 9];
    }

    // DF here is a setting, not a bitmap, max of DF_2DMMR (JPEG, JBIG set later)
    if (df & BIT(DF_2DMMR)) df = DF_2DMMR;
    else if (df & BIT(DF_2DMR)) df = DF_2DMR;
    else df = DF_1DMH;

    if (xinfo & DCS_ECMODE)
	ec = (xinfo & DCSFRAME_64) ? EC_ENABLE64 : EC_ENABLE256;
    else
	ec = EC_DISABLE;
}

#define CHECKPARAM(a, b, c)	(c ? a & BIT(b) : a == b)

/*
 * Set the T.30 DIS/DCS bits that reflects the parameters.
 * isDIS distinguishes between DIS and DCS
 *
 * Class2Params may be capability bitmaps (modemParams) or may be
 * settings (params), we must be capable of handling both.
 */
void
Class2Params::update(bool isDIS)
{
    setupT30(0,0);
    setBit(BITNUM_T4RCVR, true);	// Table 2-T.30 Notes 19 & 20
    /*
     * RESOLUTIONS
     *
     * We set both resolution units preferences in order to
     * allow the sender to choose between them.  If it has
     * prepared an inch-based document, that's how we want it.
     * VR is bitmapped natively in all cases.
     */
    if (vr & VR_FINE || vr & VR_200X200) setBit(BITNUM_VR_FINE, true);	// R8  x  7.7 l/mm
    if (vr & VR_R8 || vr & VR_200X400)   setBit(BITNUM_VR_R8, true);	// R8  x 15.4 l/mm
    if (vr & VR_R16)			 setBit(BITNUM_VR_R16, true);	// R16 x 15.4 l/mm
    if (vr & VR_300X300)		 setBit(BITNUM_VR_300X300, true);// 300 x 300 dpi

    /*
     * SIGNALING RATE
     *
     * Converting br into a meaningful T.30
     * representation is an inexact science because
     *  1) V.17 speeds overlap V.33 and V.29, and
     *  2) br indicates speed, and T.30
     *     indicates modulation also, and
     *  3) V.34 overlaps V.17, V.33, V.29, and V.27.
     *
     * As V.33 is no longer part of T.30 as of 1994,
     * we do not set it here.
     *
     * V.17 support requires both V.29 and V.27 support.
     * V.29 and V.27 support are independent.
     * V.34 requires V.8 support.
     *
     * The imperfect assumptions are that:
     *		BR_2400 -- BR_4800  are V.27
     *		BR_7200 -- BR_9600  are V.29
     *		BR_12000 - BR_14400 are V.17
     *		BR_16800 - BR_33600 are V.34
     *
     * The protocol drivers will certainly need to reproduce
     * this independently.
     */
    if (isDIS) {	// DIS
	if (CHECKPARAM(br, BR_14400, true))	setBit(BITNUM_SIGRATE_14, true);
	if (CHECKPARAM(br, BR_9600, true))	setBit(BITNUM_SIGRATE_11, true);
	if (CHECKPARAM(br, BR_4800, true))	setBit(BITNUM_SIGRATE_12, true);
	if (CHECKPARAM(br, BR_33600, true))	setBit(BITNUM_V8_CAPABLE, true);
    } else {		// DCS
	// no bits set for V.34 rates and V.27ter 2400
	if (CHECKPARAM(br, BR_14400, false))	setBit(BITNUM_SIGRATE_14, true);
	if (CHECKPARAM(br, BR_12000, false)) {
	    setBit(BITNUM_SIGRATE_14, true);
	    setBit(BITNUM_SIGRATE_12, true);
	}
	if (CHECKPARAM(br, BR_9600, false))	setBit(BITNUM_SIGRATE_11, true);
	if (CHECKPARAM(br, BR_7200, false)) {
	    setBit(BITNUM_SIGRATE_11, true);
	    setBit(BITNUM_SIGRATE_12, true);
	}
	if (CHECKPARAM(br, BR_4800, false))	setBit(BITNUM_SIGRATE_12, true);
    }

    /*
     * RECORDING WIDTH
     *
     * Post-1994 T.30 revisions have three page width
     * options: A4 (215 mm), B4 (255 mm), and A3 (303 mm).
     * Earlier revisions also included A5 (151 mm) and
     * A6 (107 mm).
     *
     * Trying to support A5 and A6 is likely to only
     * be counterproductive, thus we do not support them.
     *
     * A3 support requires B4 support.
     * A4 support is required.
     */
    if (CHECKPARAM(wd, WD_A3, isDIS))		setBit(BITNUM_WIDTH_18, true);
    else if (CHECKPARAM(wd, WD_B4, isDIS))	setBit(BITNUM_WIDTH_17, true);

    /*
     * RECORDING LENGTH
     *
     * Three possibilities: A4 (297 mm), B4 (364 mm), and unlimited.
     * A4 page length support is required.
     *
     * Using "unlimited" page length is the only way to get the popular
     * "US-Legal" page size.
     */
    if (CHECKPARAM(ln, LN_INF, isDIS))		setBit(BITNUM_LENGTH_20, true);
    else if (CHECKPARAM(ln, LN_B4, isDIS))	setBit(BITNUM_LENGTH_19, true);

    /*
     * DATA FORMAT
     *
     * Options: MH (required), MR, MMR, JBIG, JPEG.
     *
     * ECM support is required for MMR, JBIG, and JPEG.
     *
     * There are other data format options in T.30
     * such as T.81, and T.43.
     */
    if (CHECKPARAM(df, DF_2DMR, isDIS))  setBit(BITNUM_2DMR, true);
    if (CHECKPARAM(df, DF_2DMMR, isDIS) && (CHECKPARAM(ec, EC_ENABLE64, isDIS) || CHECKPARAM(ec, EC_ENABLE256, isDIS)))
	setBit(BITNUM_2DMMR, true);
    if (CHECKPARAM(df, DF_JBIG, isDIS) && (CHECKPARAM(ec, EC_ENABLE64, isDIS) || CHECKPARAM(ec, EC_ENABLE256, isDIS)))
	setBit(BITNUM_JBIG_BASIC, true);

    /*
     * MINIMUM SCANLINE TIME
     */
    if (CHECKPARAM(st, ST_5MS, isDIS) || 
        CHECKPARAM(st, ST_20MS2, isDIS) ||
        CHECKPARAM(st, ST_40MS2, isDIS) ||
        CHECKPARAM(st, ST_0MS, isDIS)) {
	setBit(BITNUM_ST_21, true);
    }
    if (CHECKPARAM(st, ST_10MS, isDIS) || 
        CHECKPARAM(st, ST_10MS2, isDIS) ||
        CHECKPARAM(st, ST_20MS2, isDIS) ||
        CHECKPARAM(st, ST_0MS, isDIS)) {
	setBit(BITNUM_ST_22, true);
    }
    if (CHECKPARAM(st, ST_40MS, isDIS) || 
        CHECKPARAM(st, ST_10MS2, isDIS) ||
        CHECKPARAM(st, ST_40MS2, isDIS) ||
        CHECKPARAM(st, ST_0MS, isDIS)) {
	setBit(BITNUM_ST_23, true);
    }
    
    /*
     * ECM
     *
     * 64-byte and 256-byte frame-size options.
     * A receiver must support both or neither.
     *
     * A sender must support no ECM.  Senders
     * should honor the preference of the receiver.
     */
    if (CHECKPARAM(ec, EC_ENABLE64, isDIS)) {
	setBit(BITNUM_ECM, true);
	if (isDIS) setBit(BITNUM_FRAMESIZE_DIS, true);
	else setBit(BITNUM_FRAMESIZE_DCS, true);
    }
    if (CHECKPARAM(ec, EC_ENABLE256, isDIS)) {
	setBit(BITNUM_ECM, true);
	if (isDIS) setBit(BITNUM_FRAMESIZE_DIS, false);
	else setBit(BITNUM_FRAMESIZE_DCS, false);
    }
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
    if ((st & 1) == 0 && vr > VR_NORMAL)
	ms /= 2;
    return transferSize(ms);
}

u_int
Class2Params::pageWidth() const
{
    u_int widths[8] = {
	1728,	// 1728 in 215 mm line
	2048,	// 2048 in 255 mm line
	2432,	// 2432 in 303 mm line
	1216,	// 1216 in 151 mm line
	864,	// 864 in 107 mm line
	1728,	// undefined
	1728,	// undefined
	1728,	// undefined
    };
    switch (vr) {
	case VR_300X300: 
	    widths[0] = 2592;
	    widths[1] = 3072;
	    widths[2] = 3648;
	    widths[3] = 1824;
	    widths[4] = 1296;
	    break;  
	case VR_R16:
	    widths[0] = 3456;
	    widths[1] = 4096;
	    widths[2] = 4864;
	    widths[3] = 2432;
	    widths[4] = 1728;
	    break;
	case VR_NORMAL:
	case VR_FINE:
	case VR_R8:
	case VR_200X100:
	case VR_200X200:
	case VR_200X400:
	    // nothing
	    break;
    }
    return (widths[wd&7]);
}

void
Class2Params::setPageWidthInMM(u_int w)
{
    /*
     * We get the width in MM from the q file.  Basically,
     * faxing only deals with three page widths defined by
     * pixel-widths: 1728, 2048, and 2432 (in R8 resolution).
     * The associated page size names are A4, B4, and A3.  
     * However, the calculated size in millimeters differs
     * between pixel-widths and page size.
     * 1728 pixels = 215 mm : A4 = 209.903 mm
     * 2048 pixels = 255 mm : B4 = 250.119 mm
     * 2432 pixels = 303 mm : A3 = 297.039 mm
     * The client program may therefore use a range of
     * pagewidth (mm) values.  We must interpret wisely.
     */
    wd = (w > 270 ? WD_A3 : w > 230 ? WD_B4 : WD_A4);
}

void
Class2Params::setPageWidthInPixels(u_int w)
{
    /*
     * Here we attempt to determine the WD parameter with
     * a pixel width which is impossible to be perfect
     * because there are colliding values.  However,
     * since we don't use > WD_A3, this is fine.
     */
    wd = (w == 1728 ? WD_A4 :
	  w == 2048 ? WD_B4 :
	  w == 2432 ? WD_A3 :
	  w == 3456 ? WD_A4 :
	  w == 4096 ? WD_B4 :
	  w == 4864 ? WD_A3 :
	  w == 2592 ? WD_A4 :
	  w == 3072 ? WD_B4 :
	  w == 3648 ? WD_A3 :
		      WD_A4);
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
	  l <= 380 ?	     LN_B4 :
			     LN_INF);
}

u_int
Class2Params::horizontalRes() const
{
    /*
     * Technically horizontal resolution depends upon the
     * the number of pixels across the page and the page width.
     * But, these are just used for writing TIFF tags for 
     * received faxes, so we do this to accomodate the session
     * parameters, even though it may be slightly off.
     */
    return (vr == VR_NORMAL ? 204 :
	    vr == VR_FINE ? 204 :
	    vr == VR_R8 ? 204 :
	    vr == VR_R16 ? 408 :
	    vr == VR_200X100 ? 200 :
	    vr == VR_200X200 ? 200 :
	    vr == VR_200X400 ? 200 :
	    vr == VR_300X300 ? 300 :
	    (u_int) -1);
}

u_int
Class2Params::verticalRes() const
{
    return (vr == VR_NORMAL ? 98 :
	    vr == VR_FINE ? 196 :
	    vr == VR_R8 ? 391 :
	    vr == VR_R16 ? 391 :
	    vr == VR_200X100 ? 100 :
	    vr == VR_200X200 ? 200 :
	    vr == VR_200X400 ? 400 :
	    vr == VR_300X300 ? 300 :
	    (u_int) -1);
}

void
Class2Params::setRes(u_int xres, u_int yres)
{
    vr = (xres > 300 && yres > 391 ? VR_R16 : 
	xres > 204 && yres > 250 ? VR_300X300 :
	yres > 391 ? VR_200X400 : 
	yres > 250 ? VR_R8 : 
	yres > 196 ? VR_200X200 :
	yres > 150 ? VR_FINE : 
	yres > 98 ? VR_200X100 : VR_NORMAL);
}

fxStr
Class2Params::encodePage() const
{
    int v = (vr&3) | ((wd&7)<<1) | ((ln&3)<<4) | ((df&3)<<6);
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
    return  (vr > 4 ? ((vr>>4)&7)<<0 : (vr&7)<<0)	// push inch resolutions
	  | ((br&15)<<3)
	  | ((wd&7)<<9)
	  | ((ln&3)<<12)
	  | ((df&3)<<14)
	  | ((ec == EC_DISABLE ? 0 : 1)<<16)		// boolean value
	  | ((bf&1)<<17)
	  | ((st&7)<<18)
	  | (1<<21)		// this is the version identifier
	  ;
}

void
Class2Params::decode(u_int v)
{
    if (v>>21 == 1) {		// check version
	vr = ((v>>0) & 7);	// VR is a bitmap
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
Class2Params::getMinSpeed() const
{
    u_int minspeed = BR_14400;
    for (int speed = BR_14400; speed >= BR_2400; speed--) {
	if (br & BIT(speed)) minspeed = speed;
    }
    return (minspeed);
}

u_int
Class2Params::encodeCaps() const
{
    // we can't use all of BR_ALL because we're limited to 32 bits
    // all ECM support is reduced to "1" (EC_ENABLE64) if any ECM is available
    return (vr&VR_ALL)
	 | ((br&(BR_2400|BR_4800|BR_7200|BR_9600|BR_12000|BR_14400))<<8)
	 | ((wd&WD_ALL)<<14)
	 | ((ln&LN_ALL)<<19)
	 | ((df&DF_ALL)<<22)
	 | ((ec&EC_DISABLE)<<26)
	 | ((ec&EC_ALL != 0 ? 1 : 0)<<27)
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
    ec = (v>>26) & (EC_DISABLE|EC_ENABLE64);				// see encoding
    bf = (v>>28) & BF_ALL;
    st = (v>>30) & ST_ALL;
}

/*
 * Routines for printing some Class 2 capabilities.
 */
const char* Class2Params::verticalResNames[65] = {
    "3.85 line/mm",
    "7.7 line/mm",
    "15.4 line/mm", "",
    "R16 x 15.4 line/mm", "", "", "",
    "200 x 100 dpi", "", "", "", "", "", "", "",
    "200 x 200 dpi", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "200 x 400 dpi", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "300 x 300 dpi"
};

const char* Class2Params::verticalResName() const
    { return (verticalResNames[vr & VR_ALL]); }

const char* Class2Params::bestVerticalResName() const
{
    /*
     * "Best" is determined by comparing pixels per square
     * area of image.  Thus 300x300 is "better" than 200x400.
     */
    u_int res = VR_NORMAL;	// required default
    if (vr & VR_200X100)	res = VR_200X100;
    if (vr & VR_FINE)		res = VR_FINE;
    if (vr & VR_200X200)	res = VR_200X200;
    if (vr & VR_R8)		res = VR_R8;
    if (vr & VR_200X400)	res = VR_200X400;
    if (vr & VR_300X300)	res = VR_300X300;
    if (vr & VR_R16)		res = VR_R16;
    return(verticalResNames[res]);
}


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

const char* Class2Params::dataFormatNames[7] = {
    "1-D MH",			// DF_1DMH
    "2-D MR",			// DF_2DMR
    "2-D Uncompressed Mode",	// DF_2DMRUNCOMP
    "2-D MMR",			// DF_2DMMR
    "JBIG",			// DF_JBIG
    "JPEG Greyscale",		// DF_JPEG_GREY
    "JPEG Full-Color"		// DF_JPEG_COLOR
};
const char* Class2Params::dataFormatName() const
     { return (dataFormatNames[df]); }

fxStr
Class2Params::dataFormatsName()
{
    fxStr formats = "MH";
    if (df & BIT(DF_2DMR)) formats.append(", MR");
    if (df & BIT(DF_2DMMR)) formats.append(", MMR");
    if (df & BIT(DF_JBIG)) formats.append(", JBIG");
    // since color requires greyscale, just say one or the other
    if (df & BIT(DF_JPEG_COLOR)) formats.append(", JPEG Full-Color");
    else if (df & BIT(DF_JPEG_GREY))  formats.append(", JPEG Greyscale");
    return (formats);
}

const char* Class2Params::pageWidthNames[8] = {
    "A4 page width (215 mm)",
    "B4 page width (255 mm)",
    "A3 page width (303 mm)",
    "page width 151 mm",
    "page width 107 mm",
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
const char* Class2Params::ecmNames[8] = {
    "no ECM",
    "T.30 Annex A, 64-byte ECM",
    "T.30 Annex A, 256-byte ECM",
    "T.30 Annex C, half duplex ECM",
    "T.30 Annex C, full duplex ECM",
    "undefined ECM",
    "undefined ECM",
    "undefined ECM"
};
const char* Class2Params::ecmName() const
    { return (ecmNames[ec&7]); }
