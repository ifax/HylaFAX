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

const int FaxParams::BITNUM_V8_CAPABLE	=  6;
const int FaxParams::BITNUM_FRAMESIZE_DIS	=  7;
const int FaxParams::BITNUM_T4XMTR		=  9;
const int FaxParams::BITNUM_T4RCVR		= 10;
const int FaxParams::BITNUM_SIGRATE_11	= 11;
const int FaxParams::BITNUM_SIGRATE_12	= 12;
const int FaxParams::BITNUM_SIGRATE_13	= 13;
const int FaxParams::BITNUM_SIGRATE_14	= 14;
const int FaxParams::BITNUM_VR_FINE		= 15;
const int FaxParams::BITNUM_2DMR		= 16;
const int FaxParams::BITNUM_WIDTH_17	= 17;
const int FaxParams::BITNUM_WIDTH_18	= 18;
const int FaxParams::BITNUM_LENGTH_19	= 19;
const int FaxParams::BITNUM_LENGTH_20	= 20;
const int FaxParams::BITNUM_ST_21		= 21;
const int FaxParams::BITNUM_ST_22		= 22;
const int FaxParams::BITNUM_ST_23		= 23;
const int FaxParams::BITNUM_ECM		= 27;
const int FaxParams::BITNUM_FRAMESIZE_DCS	= 28;
const int FaxParams::BITNUM_2DMMR		= 31;
const int FaxParams::BITNUM_JBIG		= 36;
const int FaxParams::BITNUM_VR_R8		= 41;
const int FaxParams::BITNUM_VR_300X300	= 42;
const int FaxParams::BITNUM_VR_R16		= 43;
const int FaxParams::BITNUM_INCH_RES	= 44;
const int FaxParams::BITNUM_METRIC_RES	= 45;
const int FaxParams::BITNUM_SEP		= 47;
const int FaxParams::BITNUM_SUB		= 49;
const int FaxParams::BITNUM_PWD		= 50;
const int FaxParams::BITNUM_JPEG		= 68;
const int FaxParams::BITNUM_FULLCOLOR	= 69;
const int FaxParams::BITNUM_LETTER_SIZE	= 76;
const int FaxParams::BITNUM_LEGAL_SIZE	= 77;
const int FaxParams::BITNUM_JBIG_BASIC	= 78;
const int FaxParams::BITNUM_JBIG_L0		= 79;

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

/*
 * Convert FaxParams and return a formatted ASCII DCS/DIS equivalent.
 */
void FaxParams::asciiEncode(fxStr& response)
{
    u_int byte = 0;
    do {
	if (byte) response.append(" ");
	response.append(fxStr::format("%.2X", getByte(byte)));
    } while (hasNextByte(byte++));
}

/*
 * Reverse of asciiEncode, take ASCII string and make it into FaxParams
 */
void FaxParams::asciiDecode(const char* dcs)
{
    u_int byte = 0;
    while (dcs[0] != '\0' && dcs[1] != '\0') {
	u_char value = 0;
	value = 2;
	m_bits[byte] = ((dcs[0] - (dcs[0] > 64 ? 55 : 48)) << 4) + (dcs[1] - (dcs[1] > 64 ? 55 : 48));
	setExtendBits(byte++);
	dcs += 2;
	if (dcs[0] == ' ') dcs++;
    }
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
    memcpy(m_bits, operand.m_bits, sizeof(m_bits));
    return *this;
}

void FaxParams::update(bool isDIS)
{
}
