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

#include "FaxParams.h"
#include "Class2Params.h"
#include "SystemLog.h"
#include <iostream>

const int FaxParams::BITNUM_V8_CAPABLE =  6;
const int FaxParams::BITNUM_FRAMESIZE  =  7;
const int FaxParams::BITNUM_T4XMTR     =  9;
const int FaxParams::BITNUM_T4RCVR     = 10;
const int FaxParams::BITNUM_SIGRATE_11 = 11;
const int FaxParams::BITNUM_SIGRATE_12 = 12;
const int FaxParams::BITNUM_SIGRATE_13 = 13;
const int FaxParams::BITNUM_SIGRATE_14 = 14;
const int FaxParams::BITNUM_VR_FINE    = 15;
const int FaxParams::BITNUM_2DMR       = 16;
const int FaxParams::BITNUM_WIDTH_17   = 17;
const int FaxParams::BITNUM_WIDTH_18   = 18;
const int FaxParams::BITNUM_LENGTH_19  = 19;
const int FaxParams::BITNUM_LENGTH_20  = 20;
const int FaxParams::BITNUM_ST_21      = 21;
const int FaxParams::BITNUM_ST_22      = 22;
const int FaxParams::BITNUM_ST_23      = 23;
const int FaxParams::BITNUM_ECM        = 27;
const int FaxParams::BITNUM_2DMMR      = 31;
const int FaxParams::BITNUM_VR_R8      = 41;
const int FaxParams::BITNUM_VR_300X300 = 42;
const int FaxParams::BITNUM_VR_R16     = 43;
const int FaxParams::BITNUM_INCH_RES   = 44;
const int FaxParams::BITNUM_METRIC_RES = 45;
const int FaxParams::BITNUM_SEP        = 47;
const int FaxParams::BITNUM_SUB        = 49;
const int FaxParams::BITNUM_PWD        = 50;
const int FaxParams::BITNUM_JBIG_BASIC = 78;
const int FaxParams::BITNUM_JBIG_L0    = 79;

FaxParams::FaxParams()
{
    initializeBitString();
}

FaxParams::FaxParams(u_char* pBits, int length)
{
    setupT30(pBits, length);
}

FaxParams::~FaxParams (void)
{
}

/*
 * Convert old dis+xinfo into FaxParams.  This should go away eventually.
 */
FaxParams::FaxParams(u_int disDcs, u_int xinfo)
{
    setupT30(disDcs, xinfo);
}

/*
 * Convert Class2Params (Table 21/T.32)
 * into FaxParams (Table 2/T.30).
 *
 * As Table 21/T.32 cannot completely represent Table 2/T.30
 * (in fact, the two are somewhat disjointed), this result
 * should only be used unmodified in the Class 2 driver.
 */
FaxParams::FaxParams(Class2Params modemParams)
{
    initializeBitString();

    /*
     * RESOLUTIONS
     *
     * We set both resolution units preferences in order to
     * allow the sender to choose between them.  If it has
     * prepared an inch-based document, that's how we want it.
     */
    if (modemParams.vr & VR_FINE || modemParams.vr & VR_200X200) setBit(BITNUM_VR_FINE, true);	// R8  x  7.7 l/mm
    if (modemParams.vr & VR_R8 || modemParams.vr & VR_200X400)   setBit(BITNUM_VR_R8, true);	// R8  x 15.4 l/mm
    if (modemParams.vr & VR_R16) 				 setBit(BITNUM_VR_R16, true);	// R16 x 15.4 l/mm
    if (modemParams.vr & VR_300X300)				 setBit(BITNUM_VR_300X300, true);// 300 x 300 dpi

    /*
     * SIGNALING RATE
     *
     * Converting modemParams.br into a meaningful T.30
     * representation is an inexact science because
     *  1) V.17 speeds overlap V.33 and V.29, and
     *  2) modemParams.br indicates speed, and T.30
     *     indicates modulation also, and
     *  3) V.34 overlaps V.17, V.33, V.29, and V.27.
     *
     * As V.33 is no longer part of T.30 as of 1994,
     * we do not set it here.
     *
     * V.17 support requires both V.29 and V.27 support.
     * V.29 and V.27 support are independent.
     * V.34 requires V.8 support.
     */
    if (modemParams.br & BIT(BR_14400))	setBit(BITNUM_SIGRATE_14, true);
    if (modemParams.br & BIT(BR_9600))	setBit(BITNUM_SIGRATE_11, true);
    if (modemParams.br & BIT(BR_4800))	setBit(BITNUM_SIGRATE_12, true);
    if (modemParams.br & BIT(BR_33600)) setBit(BITNUM_V8_CAPABLE, true);

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
    if (modemParams.wd & BIT(WD_2432))      setBit(BITNUM_WIDTH_18, true);
    else if (modemParams.wd & BIT(WD_2048)) setBit(BITNUM_WIDTH_17, true);

    /*
     * RECORDING LENGTH
     *
     * Three possibilities: A4 (297 mm), B4 (364 mm), and unlimited.
     * A4 page length support is required.
     *
     * Using "unlimited" page length is the only way to get the popular
     * "US-Legal" page size.
     */
    if (modemParams.ln & BIT(LN_INF))	  setBit(BITNUM_LENGTH_20, true);
    else if (modemParams.ln & BIT(LN_B4)) setBit(BITNUM_LENGTH_19, true);

    /*
     * DATA FORMAT
     *
     * Options: MH (required), MR, and MMR.
     *
     * ECM support is required for MMR.
     *
     * There are other data format options in T.30
     * such as JPEG, JBIG, T.81, and T.43, but they're
     * not discernable from Class2Params.
     */
    if (modemParams.df & BIT(DF_2DMR))  setBit(BITNUM_2DMR, true);
    if (modemParams.df & BIT(DF_2DMMR) && modemParams.ec != EC_DISABLE) setBit(BITNUM_2DMMR, true);

    /*
     * MINIMUM SCANLINE TIME
     */
    if (modemParams.st & BIT(ST_5MS) || 
        modemParams.st & BIT(ST_20MS2) ||
        modemParams.st & BIT(ST_40MS2) ||
        modemParams.st & BIT(ST_0MS)) {
        setBit(BITNUM_ST_21, true);
    }
    if (modemParams.st & BIT(ST_10MS) || 
        modemParams.st & BIT(ST_10MS2) ||
        modemParams.st & BIT(ST_20MS2) ||
        modemParams.st & BIT(ST_0MS)) {
        setBit(BITNUM_ST_22, true);
    }
    if (modemParams.st & BIT(ST_40MS) || 
        modemParams.st & BIT(ST_10MS2) ||
        modemParams.st & BIT(ST_40MS2) ||
        modemParams.st & BIT(ST_0MS)) {
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
    if (modemParams.ec != EC_DISABLE)	   setBit(BITNUM_ECM, true);
    if (modemParams.ec & BIT(EC_ENABLE64) && !(modemParams.ec & BIT(EC_ENABLE256))) setBit(BITNUM_FRAMESIZE, true);
}

/*
 * Set all bits to zero.  According to Table 2 T.30 NOTE 1, reserved
 * bits should also be set to zero.
 */
void FaxParams::initializeBitString()
{
    for (int i = 0; i < MAX_BITSTRING_BYTES; i++) m_bits[i] = 0;
}

void FaxParams::setupT30(u_char* pBits, int length)
{
    initializeBitString();
    bool lastbyte = false;

    for (int byte = 0; byte < MAX_BITSTRING_BYTES && byte < length; byte++) {
	if (!lastbyte) m_bits[byte] = pBits[byte];
	else m_bits[byte] = 0;		// clear bits when they have no meaning

	if (byte > 2 && !(m_bits[byte] & 0x01)) lastbyte = true;
    }

    // Don't allow the last byte to have the extend bit set.
    m_bits[MAX_BITSTRING_BYTES-1] = m_bits[MAX_BITSTRING_BYTES-1] & 0xFE;
}

void FaxParams::setupT30 (u_int disDcs, u_int xinfo)
{
    initializeBitString();
    m_bits[0] = (disDcs&0xFF0000) >> 16;
    m_bits[1] = (disDcs&0x00FF00) >>  8;
    m_bits[2] = (disDcs&0x0000FF) >>  0;

    m_bits[3] = (xinfo&0xFF000000) >> 24;
    m_bits[4] = (xinfo&0x00FF0000) >> 16;
    m_bits[5] = (xinfo&0x0000FF00) >>  8;
    m_bits[6] = (xinfo&0x000000FF) >>  0;
}


/*
 * Table 2 T.30  defines bit numbers 1 ... 127.  
 * Anything else is invalid and should not be used.
 */
bool FaxParams::validBitNumber(int bitNum)
{
    return ((bitNum >= 1) && (bitNum <= 127));
}

bool FaxParams::isBitEnabled(int bitNum)
{
    if (!validBitNumber(bitNum)) return false;
    return m_bits[calculateByteNumber(bitNum)] & calculateMask(bitNum);
}

void FaxParams::setExtendBits(int byteNum)
{
    if (byteNum >= 3) {
	for (int byte = byteNum-1; byte >= 2; byte--)
	    m_bits[byte] = m_bits[byte] | 0x01;
    }
}

void FaxParams::unsetExtendBits()
{
    for (int byte = MAX_BITSTRING_BYTES-1; byte >= 2; byte--) {
	if (m_bits[byte] & 0xFF) {
	    //extend field is needed in previous byte
	    break;
	} else {
	    //extend field NOT needed in previous byte
	    //so unset it
	    m_bits[byte-1] = m_bits[byte-1] & 0xFE;
	}
    }
}

void FaxParams::setBit(int bitNum, bool val)
{
    if (!validBitNumber(bitNum)) return;

    int byteNum = calculateByteNumber(bitNum);
    u_char mask = calculateMask(bitNum);

    if (val) {
	m_bits[byteNum] = m_bits[byteNum] | mask;
	setExtendBits(byteNum);
    } else {
	m_bits[byteNum] = m_bits[byteNum] & ~mask;
	unsetExtendBits();
    }
}

int FaxParams::calculateByteNumber(int bitNum)
{
    //Subtract 1 from bitNum because Table 2 T.30 indexes 
    //bit numbers from 1 and C indexes everything from 0.

    return (bitNum-1)/8;
}

u_char FaxParams::calculateMask(int bitNum)
{
    //Subtract 1 from bitNum because Table 2 T.30 indexes 
    //bit numbers from 1 and C indexes everything from 0.

    int shiftLeft = 7-((bitNum-1)%8);
    u_char mask = 0x01 << shiftLeft;
    return mask;
}

bool FaxParams::hasNextByte(int byteNum)
{
    return (byteNum <= 1 || (m_bits[byteNum] & 0x01));
}

u_char FaxParams::getByte(int byteNum)
{
    return m_bits[byteNum];
}

bool FaxParams::operator==(FaxParams& operand) const
{
    bool equals = true;
    u_short byte = 0;

    // We do not compare the next bytes if the extend-bit is not set.
    while (equals && byte < MAX_BITSTRING_BYTES && (byte < 3 || m_bits[byte] & 0x01)) {
	if (m_bits[byte] != operand.m_bits[byte]) equals = false;
	byte++;
    }
    return equals;
}

bool FaxParams::operator!=(FaxParams& operand) const
{
    return !operator==(operand);
}

FaxParams& FaxParams::assign(const FaxParams& operand)
{
    bool lastbyte = false;

    for (int byte = 0; byte < MAX_BITSTRING_BYTES; byte++) {
	if (!lastbyte) m_bits[byte] = operand.m_bits[byte];
	else m_bits[byte] = 0;		// clear bits when they have no meaning

	if (byte > 2 && !(m_bits[byte] & 0x01)) lastbyte = true;
    }

    // Don't allow the last byte to have the extend bit set.
    m_bits[MAX_BITSTRING_BYTES-1] = m_bits[MAX_BITSTRING_BYTES-1] & 0xFE;

    return *this;
}

void FaxParams::update (void)
{
}
