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

#ifndef fax_capabilities
#define	fax_capabilities

#include "Types.h"
#include "Str.h"

struct Class2Params;

#define MAX_BITSTRING_BYTES 16

/* The FaxParams class stores the DIS/DCS bitstring according to Table 2 
 * T.30.  Before this class, dis/dcs were limited to 24 bits and 32 bits of
 * optional data.  Merging them into this one class removes that limitation.
 * Table 2 T.30 starts the index at one.  This class follows that same
 * convention.  Only bit numbers start at index one.  Byte numbers in this class
 * follow the c convention of the index starting at zero.
 *
 */
class FaxParams
{
    public:
	FaxParams();
	FaxParams(u_int disDcs, u_int xinfo);
	FaxParams(u_char* pBits, int length);

	virtual ~FaxParams(void);

	virtual void update(bool isDIS);

	void setBit(int bitNum, bool val);
	bool isBitEnabled(int bitNum);

	u_char getByte(int byteNum) const;
	bool hasNextByte(int byteNum) const;
	void asciiEncode(fxStr&) const;
	void asciiDecode(const char*);

	bool operator==(const FaxParams& operand) const;
	bool operator!=(const FaxParams& operand) const;
	FaxParams& operator=(const FaxParams& operand);

    protected:
	void setupT30(u_char* pBits, int length);
	void setupT30(u_int dcs_dis, u_int xinfo);

	static const int BITNUM_V8_CAPABLE;	//  6;
	static const int BITNUM_FRAMESIZE_DIS;	//  7;
	static const int BITNUM_T4XMTR;		//  9;
	static const int BITNUM_T4RCVR;		// 10;
	static const int BITNUM_SIGRATE_11;	// 11;
	static const int BITNUM_SIGRATE_12;	// 12;
	static const int BITNUM_SIGRATE_13;	// 13;
	static const int BITNUM_SIGRATE_14;	// 14;
	static const int BITNUM_VR_FINE;	// 15;
	static const int BITNUM_2DMR;		// 16;
	static const int BITNUM_WIDTH_17;	// 17;
	static const int BITNUM_WIDTH_18;	// 18;
	static const int BITNUM_LENGTH_19;	// 19;
	static const int BITNUM_LENGTH_20;	// 20;
	static const int BITNUM_ST_21;		// 21;
	static const int BITNUM_ST_22;		// 22;
	static const int BITNUM_ST_23;		// 23;
	static const int BITNUM_ECM;		// 27;
	static const int BITNUM_FRAMESIZE_DCS;	// 28;
	static const int BITNUM_2DMMR;		// 31;
	static const int BITNUM_JBIG;		// 36;
	static const int BITNUM_VR_R8;		// 41;
	static const int BITNUM_VR_300X300;	// 42;
	static const int BITNUM_VR_R16;		// 43;
	static const int BITNUM_INCH_RES;	// 44;
	static const int BITNUM_METRIC_RES;	// 45;
	static const int BITNUM_SEP;		// 47;
	static const int BITNUM_SUB;		// 49;
	static const int BITNUM_PWD;		// 50;
	static const int BITNUM_JPEG;		// 68;
	static const int BITNUM_FULLCOLOR;	// 69;
	static const int BITNUM_LETTER_SIZE;	// 76;
	static const int BITNUM_LEGAL_SIZE;	// 77;
	static const int BITNUM_JBIG_BASIC;	// 78;
	static const int BITNUM_JBIG_L0;	// 79;

	FaxParams& assign (const FaxParams&);

	// Class1Modem wants intimate knowledge of the BITS&BYTES
	friend class Class1Modem;

    private:
	void initializeBitString();
	int calculateByteNumber(int bitNum);
	u_char calculateMask(int bitNum);
	void setExtendBits(int byteNum);
	void unsetExtendBits();
	bool validBitNumber(int bitNum);
    
	u_char m_bits[MAX_BITSTRING_BYTES];
};

inline FaxParams& FaxParams::operator= (const FaxParams& p)
{
    return assign(p);
}

#endif

