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
#ifdef HAVE_IOSTREAM
#include <iostream>
#else
#include "iostream.h"
#endif

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
