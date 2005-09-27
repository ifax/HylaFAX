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
#ifndef _class2_
#define	_class2_
/*
 * Fax Modem Definitions for:
 *
 * Class 2	(nominally SP-2388-A of August 30, 1991)
 * Class 2.0	(TIA/EIA-592)
 * T.class2	(ITU-T)
 */
#define	BIT(i)	(1<<(i))

// bit ordering directives +fbor=<n>
const u_short BOR_C_DIR	= 0;		// phase C direct
const u_short BOR_C_REV	= 1;		// phase C reversed
const u_short BOR_C		= 0x1;
const u_short BOR_BD_DIR	= (0<<1);	// phase B/D direct
const u_short BOR_BD_REV	= (1<<1);	// phase B/D reversed
const u_short BOR_BD	= 0x2;

// service types returned by +fclass=?
const u_short SERVICE_DATA	 = BIT(0);	// data service
const u_short SERVICE_CLASS1 = BIT(1);	// class 1 interface
const u_short SERVICE_CLASS2 = BIT(2);	// class 2 interface
const u_short SERVICE_CLASS20 = BIT(3);	// class 2.0 interface
const u_short SERVICE_CLASS10 = BIT(4);	// class 1.0 interface
const u_short SERVICE_CLASS21 = BIT(5);	// class 2.1 interface
const u_short SERVICE_VOICE	 = BIT(8);	// voice service (ZyXEL extension)
const u_short SERVICE_ALL	 = BIT(9)-1;

// t.30 session subparameter codes
const u_int VR_NORMAL	= 0x00;		// 98 lpi
const u_int VR_FINE	= 0x01;		// 196 lpi
const u_int VR_R8	= 0x02;		// R8  x 15.4 l/mm
const u_int VR_R16	= 0x04;		// R16 x 15.4 l/mm
const u_int VR_200X100	= 0x08;		// 200 dpi x 100 l/25.4mm
const u_int VR_200X200	= 0x10;		// 200 dpi x 200 l/25.4mm
const u_int VR_200X400	= 0x20;		// 200 dpi x 400 l/25.4mm
const u_int VR_300X300	= 0x40;		// 300 dpi x 300 l/25.4mm
const u_int VR_ALL	= 0x7F;

const u_short BR_2400	= 0;		// 2400 bit/s
const u_short BR_4800	= 1;		// 4800 bit/s
const u_short BR_7200	= 2;		// 7200 bit/s
const u_short BR_9600	= 3;		// 9600 bit/s
const u_short BR_12000	= 4;		// 12000 bit/s
const u_short BR_14400	= 5;		// 14400 bit/s
const u_short BR_16800	= 6;		// 16800 bit/s
const u_short BR_19200	= 7;		// 19200 bit/s
const u_short BR_21600	= 8;		// 21600 bit/s
const u_short BR_24000	= 9;		// 24000 bit/s
const u_short BR_26400	= 10;		// 26400 bit/s
const u_short BR_28800	= 11;		// 28800 bit/s
const u_short BR_31200	= 12;		// 31200 bit/s
const u_short BR_33600	= 13;		// 33600 bit/s
const u_short BR_ALL	= BIT(BR_33600+1)-1;

const u_short WD_A4	= 0;		// 1728 pixels in 215 mm
const u_short WD_B4	= 1;		// 2048 pixels in 255 mm
const u_short WD_A3	= 2;		// 2432 pixels in 303 mm
const u_short WD_ALL	= BIT(WD_A3+1)-1;

const u_short LN_A4		= 0;		// A4, 297 mm
const u_short LN_B4		= 1;		// B4, 364 mm
const u_short LN_INF	= 2;		// Unlimited length
const u_short LN_ALL	= BIT(LN_INF+1)-1;

const u_short LN_LET	= 3;		// XXX US Letter size (used internally)

const u_short DF_1DMH	= 0;		// 1-D Modified Huffman
const u_short DF_2DMR	= 1;		// 2-D Modified Read
const u_short DF_2DMRUNCOMP	= 2;		// 2-D Uncompressed Mode
const u_short DF_2DMMR	= 3;		// 2-D Modified Modified Read
const u_short DF_JBIG	= 4;		// Single-progression sequential coding (Rec. T.85)
const u_short DF_JPEG_GREY	= 5;	// Greyscale JPEG (T.4 Annex E and T.81)
const u_short DF_JPEG_COLOR	= 6;	// Full-color JPEG (T.4 Annex E and T.81)
const u_short DF_ALL	= BIT(DF_2DMMR+1)-1;

/*
 * The EC definition varies between the Class 2 and Class 2.0 spec, so
 * this is a merger of both of them.
 */
const u_short EC_DISABLE	= 0;		// disable ECM
const u_short EC_ENABLE64	= 1;		// enable T.30 Annex A, 64-byte ECM
const u_short EC_ENABLE256	= 2;		// enable T.30 Annex A, 256-byte ECM
const u_short EC_ECLHALF	= 3;		// enable T.30 Annex C, half duplex
const u_short EC_ECLFULL	= 4;		// enable T.30 Annex C, full duplex
const u_short EC_ALL		= BIT(EC_ECLFULL+1)-1;

const u_short BF_DISABLE	= 0;		// disable file transfer modes
const u_short BF_ENABLE	= 1;		// select BFT, T.434
const u_short BF_DTM	= 2;		// select Document Transfer Mode
const u_short BF_EDI	= 4;		// select Edifact Mode
const u_short BF_BTM	= 8;		// select Basic Transfer Mode
const u_short BF_CM		= 10;		// select character mode T.4 Annex D
const u_short BF_MM		= 20;		// select Mixed mode, T.4 Annex E
const u_short BF_PM		= 40;		// select Processable mode, T.505
const u_short BF_ALL	= 0x3;

const u_short ST_0MS	= 0;		// scan time/line: 0 ms/0 ms
const u_short ST_5MS	= 1;		// scan time/line: 5 ms/5 ms
const u_short ST_10MS2	= 2;		// scan time/line: 10 ms/5 ms
const u_short ST_10MS	= 3;		// scan time/line: 10 ms/10 ms
const u_short ST_20MS2	= 4;		// scan time/line: 20 ms/10 ms
const u_short ST_20MS	= 5;		// scan time/line: 20 ms/20 ms
const u_short ST_40MS2	= 6;		// scan time/line: 40 ms/20 ms
const u_short ST_40MS	= 7;		// scan time/line: 40 ms/40 ms
const u_short ST_ALL	= BIT(ST_40MS+1)-1;

// post page message codes
const u_short PPM_MPS	= 0;		// another page next, same document
const u_short PPM_EOM	= 1;		// another document next
const u_short PPM_EOP	= 2;		// no more pages or documents
const u_short PPM_PRI_MPS	= 4;		// another page, procedure interrupt
const u_short PPM_PRI_EOM	= 5;		// another doc, procedure interrupt
const u_short PPM_PRI_EOP	= 6;		// all done, procedure interrupt

// post page response codes
const u_short PPR_MCF	= 1;		// page good
const u_short PPR_RTN	= 2;		// page bad, retrain requested
const u_short PPR_RTP	= 3;		// page good, retrain requested
const u_short PPR_PIN	= 4;		// page bad, interrupt requested
const u_short PPR_PIP	= 5;		// page good, interrupt requested

// important stream transfer codes
// These are actual (char) recived, so thes aren't unsigned int
const int DLE = 16;		// transparent character escape
const int SUB = 26;		// <DLE><SUB> => <DLE><DLE> for Class 2.0
const int ETX = 3;		// <DLE><ETX> means end of transfer
const int DC1 = 17;		// start data transfer (Class 2)
const int DC2 = 18;		// start data transfer (Class 2.0 and ZyXEL)
const int CAN = 24;		// abort data transfer
const int EOT = 4;		// end transmission (Class 1.0)		
#endif /* _class2_ */
