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
#ifndef _t30_
#define	_t30_
/*
 * The Group 3/T.30 protocol defines pre-, in-, and post-
 * message phases.  During the pre-message phase, the modems
 * exchange capability information and do "training" to
 * synchronize transmission.  Once this is done, one or more
 * pages of information may be transmitted, and following this
 * the post-message phase allows for retransmission, line
 * turn-around, and so on.
 *
 * Consult CCITT recommendation T.30, "Procedures for Document
 * Facsimile Transmission in the General Switched Telephone
 * Network" (especially pp. 69-109) for further information.
 */

/*
 * Note: All the FCF codes after the initial identification
 * commands should include FCF_SNDR or FCF_RCVR ``or-ed in''.
 * For example, 
 *    Calling Station	    Called Station
 *    -----------------------------------
 *			<-  FCF_DIS
 *    FCF_DCS|FCF_SNDR  ->
 *    			<-  FCF_CFR|FCF_RCVR
 *	      <<send message data>>
 *    FCF_EOP|FCF_SNDR  ->
 *			<-  FCF_MCF|FCF_RCVR
 *    FCF_DCN|FCF_SNDR	->
 */

// protocol timeouts in milliseconds
#define	TIMER_T1	((35+5)*1000)	// 35 +/- 5 seconds
#define	TIMER_T2	((6+1)*1000)	// 6 +/- 1 seconds
#define	TIMER_T3	((10+5)*1000)	// 10 +/- 5 seconds
#define	TIMER_T4	3100		// 3.1secs

#define	TCF_DURATION	1500		// 1.5 seconds

// this is our defined NSF manufacturer code in reverse bit-order
#define HYLAFAX_NSF	"\255\000\125"

/*
 * Facsimile control field (FCF) values
 */
#define	FCF_SNDR	0x80		// station receiving valid DIS
#define	FCF_RCVR	0x00		// station receiving valid DIS response

// initial identification commands from the called to calling station
#define	FCF_DIS		0x01		// digital identification signal
#define	FCF_CSI		0x02		// called subscriber identification
#define	FCF_NSF		0x04		// non-standard facilities (optional)

// responses from calling station wishing to recv 
#define	FCF_DTC		(FCF_DIS|FCF_SNDR) // digital transmit command
#define	FCF_CIG		(FCF_CSI|FCF_SNDR) // calling subrscriber id (opt)
#define	FCF_NSC		(FCF_NSF|FCF_SNDR) // non-standard facilities cmd (opt)
#define	FCF_PPW		(0x03|FCF_SNDR)	   // password for polling (opt)
#define	FCF_SEP		(0x05|FCF_SNDR)	   // selective polling subaddress (opt)
// responses from transmitter to receiver
#define	FCF_DCS		(0x40|FCF_DIS)	// digital command signal
#define	FCF_TSI		(0x40|FCF_CSI)	// transmitting subscriber id (opt)
#define	FCF_NSS		(0x40|FCF_NSF)	// non-standard facilities setup (opt)
#define	FCF_PWD		(0x40|0x05)	// password for transmission (opt)
#define	FCF_SUB		(0x40|0x03)	// routing information for xmit (opt)

/*
 * Digital Identification Signal (DIS) definitions.
 *
 * The DIS is sent from the called station to the calling station
 * to identify its capabilities.  This information may also appear
 * in a DTC frame if the line is to be turned around.
 *
 * The values given below assume a 24-bit representation for the DIS;
 * i.e. the first 3 bytes of the frame are treated as a single 24-bit
 * value.  Additional bytes of the DIS are optional and indicated by
 * a 1 in the least significant bit of the last byte.  There are currently
 * as many as 6 additional bytes that may follow the required 3-byte
 * minimum DIS frame;  we only process the first 4.
 */
#define DIS_V8		0x040000	// supports V.8 training
#define	DIS_T4XMTR	0x008000	// T.4 sender & has docs to poll
#define	DIS_T4RCVR	0x004000	// T.4 receiver
#define	DIS_SIGRATE	0x003C00	// data signalling rate
#define	    DISSIGRATE_V27FB	0x0	// V.27ter fallback mode: 2400 BPS
#define	    DISSIGRATE_V27	0x4	// V.27ter: 4800 + 2400 BPS
#define	    DISSIGRATE_V29	0x8	// V.29: 9600 + 7200 BPS
#define	    DISSIGRATE_V2729	0xC	// V.27ter+V.29
#define	    DISSIGRATE_V33	0xE	// V.27ter+V.29+V.33
#define	    DISSIGRATE_V17	0xD	// V.27ter+V.29+V.33+V.17
#define	DIS_7MMVRES	0x000200	// vertical resolution = 7.7 line/mm
#define	DIS_2DENCODE	0x000100	// 2-d compression supported
#define	DIS_PAGEWIDTH	0x0000C0	// recording width capabilities
#define	    DISWIDTH_1728	0	// only 1728
#define	    DISWIDTH_2432	1	// 2432, 2048, 1728
#define	    DISWIDTH_2048	2	// 2048, 1728
#define	    DISWIDTH_INVALID	3	// invalid, but treat as 2432
#define	DIS_PAGELENGTH	0x000030	// max recording length capabilities
#define	    DISLENGTH_A4	0	// A4 (297 mm)
#define	    DISLENGTH_UNLIMITED	1	// no max length
#define	    DISLENGTH_A4B4	2	// A4 and B4 (364 mm)
#define	    DISLENGTH_INVALID	3
#define	DIS_MINSCAN	0x00000E	// receiver min scan line time
#define	    DISMINSCAN_20MS	0x0
#define	    DISMINSCAN_40MS	0x1
#define	    DISMINSCAN_10MS	0x2
#define	    DISMINSCAN_10MS2	0x3
#define	    DISMINSCAN_5MS	0x4
#define	    DISMINSCAN_40MS2	0x5
#define	    DISMINSCAN_20MS2	0x6
#define	    DISMINSCAN_0MS	0x7
#define	DIS_XTNDFIELD	0x000001	// extended field indicator

// 1st extension byte (alternative mode capabilities)
#define	DIS_2400HS	(0x80<<24)	// 2400 bit/s handshaking
#define	DIS_2DUNCOMP	(0x40<<24)	// uncompressed 2-d data supported
#define	DIS_ECMODE	(0x20<<24)	// error correction mode supported
// NB: bit 0x10 must be zero
#define	DIS_ELMODE	(0x08<<24)	// error limiting mode suported
#define	DIS_G4COMP	(0x02<<24)	// T.6 compression supported
// bit 0x01 indicates an extension byte follows

// The meaning of the 2nd extension byte changed after the 1993 recommendation.
// If DIS_IGNOLD is set, then the 1993 definitions can be understood, otherwise
// the current definitions should be understood.

// 2nd extension byte - 1993 meaning - (alternative paper width capabilities)
#define	DIS_IGNOLD	(0x80<<16)	// ignore old paper widths in byte 3
#define	DIS_1216	(0x40<<16)	// 1216 pixels in 151 mm scanline
#define	DIS_864		(0x20<<16)	// 864 pixels in 107 mm scanline
#define	DIS_1728L	(0x10<<16)	// 1728 pixels in 151 mm scanline
#define	DIS_1728H	(0x08<<16)	// 1728 pixels in 107 mm scanline
// bits 0x04 and 0x02 are reserved
// bit 0x01 indicates an extension byte follows

// 2nd extension byte - current meaning
// bit 0x80 is not valid and must be unset
#define	DIS_MULTSEP	(0x40<<16)	// multiple selective polling capability
#define	DIS_POLLSUB	(0x20<<16)	// polled subaddress
#define	DIS_T43		(0x10<<16)	// T.43 coding
#define	DIS_INTERLV	(0x08<<16)	// plane interleave
#define	DIS_VOICE	(0x04<<16)	// voice coding - G.726
#define	DIS_VOICEXT	(0x02<<16)	// extended voice coding
// bit 0x01 indicates an extension byte follows

// 3rd extension byte (alternative resolution capabilities)
#define	DIS_200X400	(0x80<<8)	// 200 x 400 pixels/inch resolution
#define	DIS_300X300	(0x40<<8)	// 300 x 300 pixels/inch resolution
#define	DIS_400X400	(0x20<<8)	// 400 x 400 pixels/inch resolution
#define	DIS_INCHRES	(0x10<<8)	// inch-based resolution preferred
#define	DIS_METRES	(0x08<<8)	// metric-based resolution preferred
#define	DIS_400MST2	(0x04<<8)	// mst for 400 l/inch = 1/ 200 l/inch
#define	DIS_SEP		(0x02<<8)	// selective polling supported
// bit 0x01 indicates an extension byte follows

// 4th extension byte (enhanced features capabilities)
#define	DIS_SUB		(0x80<<0)	// sub-address supported (SUB frames)
#define	DIS_PWD		(0x40<<0)	// password supported (PWD frames)
#define	DIS_DATAFILE	(0x20<<0)	// can emit data file
// bit 0x10 is reserved for facsimile service information
#define	DIS_BFT		(0x08<<0)	// supports Binary File Transfer (BFT)
#define	DIS_DTM		(0x04<<0)	// supports Document Transfer Mode (DTM)
#define	DIS_EDI		(0x02<<0)	// supports Edifact Transfer (EDI)
// bit 0x01 indicates an extension byte follows

/*
 * Digital Command Signal (DCS) definitions.
 *
 * The DCS is sent from the calling station to the called station
 * prior to the training procedure; it identifies the capabilities 
 * to use for session operation.
 *
 * The values given below assume a 24-bit representation for the DCS;
 * i.e. the first 3 bytes of the frame are treated as a single 24-bit
 * value.  Additional bytes of the DCS are optional and indicated by
 * a 1 in the least significant bit of the last byte.  There are currently
 * as many as 6 additional bytes that may follow the required 3-byte
 * minimum DCS frame; we only process the first 4.
 */
#define	DCS_T4RCVR	0x004000	// receiver honors T.4
#define	DCS_SIGRATE	0x003C00	// data signalling rate
#define	    DCSSIGRATE_2400V27	(0x0<<10)
#define	    DCSSIGRATE_4800V27	(0x4<<10)
#define	    DCSSIGRATE_9600V29	(0x8<<10)
#define	    DCSSIGRATE_7200V29	(0xC<<10)
#define	    DCSSIGRATE_14400V33	(0x2<<10)
#define	    DCSSIGRATE_12000V33	(0x6<<10)
#define	    DCSSIGRATE_14400V17	(0x1<<10)
#define	    DCSSIGRATE_12000V17	(0x5<<10)
#define	    DCSSIGRATE_9600V17	(0x9<<10)
#define	    DCSSIGRATE_7200V17	(0xD<<10)
#define	DCS_7MMVRES	0x000200	// vertical resolution = 7.7 line/mm
#define	DCS_2DENCODE	0x000100	// use 2-d encoding
#define	DCS_PAGEWIDTH	0x0000C0	// recording width
#define	    DCSWIDTH_1728	(0<<6)
#define	    DCSWIDTH_2432	(1<<6)
#define	    DCSWIDTH_2048	(2<<6)
#define	DCS_PAGELENGTH	0x000030	// max recording length
#define	    DCSLENGTH_A4	(0<<4)
#define	    DCSLENGTH_UNLIMITED	(1<<4)
#define	    DCSLENGTH_B4	(2<<4)
#define	DCS_MINSCAN	0x00000E	// receiver min scan line time
#define	    DCSMINSCAN_20MS	(0x0<<1)
#define	    DCSMINSCAN_40MS	(0x1<<1)
#define	    DCSMINSCAN_10MS	(0x2<<1)
#define	    DCSMINSCAN_5MS	(0x4<<1)
#define	    DCSMINSCAN_0MS	(0x7<<1)
#define	DCS_XTNDFIELD	0x000001	// extended field indicator

// 1st extension byte (alternative mode capabilities)
#define	DCS_2400HS	(0x80<<24)	// 2400 bit/s handshaking
#define	DCS_2DUNCOMP	(0x40<<24)	// use uncompressed 2-d data
#define	DCS_ECMODE	(0x20<<24)	// use error correction mode
#define	DCS_FRAMESIZE	(0x10<<24)	// EC frame size
#define	    DCSFRAME_256	(0<<28)	// 256 octets
#define	    DCSFRAME_64		(1<<28)	// 64 octets
#define	DCS_ELMODE	(0x08<<24)	// use error limiting mode
// bit 0x04 is reserved for Group 4
#define	DCS_G4COMP	(0x02<<24)	// use T.6 compression
// bit 0x01 indicates another information byte follows

// 2nd extension byte (alternative paper width capabilities)
#define	DCS_IGNOLD	(0x80<<16)	// ignore old paper widths in byte 3
#define	DCS_1216	(0x40<<16)	// use 1216 pixels in 151 mm scanline
#define	DCS_864		(0x20<<16)	// use 864 pixels in 107 mm scanline
// bits 0x10 and 0x08 are invalid
// bits 0x04 and 0x02 are not used
// bit 0x01 indicates another information byte follows

// 3rd extension byte (alternative resolution capabilities)
#define	DCS_200X400	(0x80<<8)	// use 200 x 400 pixels/inch resolution
#define	DCS_300X300	(0x40<<8)	// use 300 x 300 pixels/inch resolution
#define	DCS_400X400	(0x20<<8)	// use 400 x 400 pixels/inch resolution
#define	DCS_INCHRES	(0x10<<8)	// use inch-based resolution
// bits 0x08 and 0x04 are ``don't care''
// bit 0x02 should be zero
// bit 0x01 indicates another information byte follows

// 4th extension byte (enhanced features capabilities)
// bits 0x80 and 0x40 should be zero
// bit 0x20 is not used
// bit 0x10 is reserved for facsimile service information
#define	DCS_BFT		(0x08<<0)	// use Binary File Transfer (BFT)
#define	DCS_DTM		(0x04<<0)	// use Document Transfer Mode (DTM)
#define	DCS_EDI		(0x02<<0)	// use Edifact Transfer (EDI)
// bit 0x01 indicates another information byte follows

// pre-message responses
#define	FCF_CFR		0x21		// confirmation to receive
#define	FCF_FTT		0x22		// failure to train
#define FCF_CTR		0x23		// response to CTC (Annex A)

// post-message commands (from transmitter to receiver)
#define	FCF_EOM		0x71		// end-of-page, restart phase B on ack
#define	FCF_MPS		0x72		// end-of-page, restart phase C on ack
#define	FCF_EOP		0x74		// end-of-procedures, hangup after ack
#define	FCF_PRI_EOM	0x79		// EOM, but allow operator intervention
#define	FCF_PRI_MPS	0x7A		// MPS, but allow operator intervention
#define	FCF_PRI_EOP	0x7C		// MPS, but allow operator intervention
#define FCF_PPS		0x7D		// partial page signal (Annex A)
#define FCF_EOR		0x73		// end of retransmission (Annex A)
#define FCF_RR		0x76		// receive ready (Annex A), was erroniously 0x37 3/93

// post-message responses (from receiver to transmitter)
#define	FCF_MCF		0x31		// message confirmation (ack MPS/EOM)
#define	FCF_RTP		0x33		// ack, continue after retraining
#define	FCF_RTN		0x32		// nak, retry after retraining
#define	FCF_PIP		0x35		// ack, continue after operating interv.
#define	FCF_PIN		0x34		// nak, retry after operation interv.
#define FCF_PPR		0x3D		// partial page request (Annex A)
#define FCF_RNR		0x37		// receive not ready (Annex A)
#define FCF_ERR		0x38		// response for EOR (Annex A)

// other line control signals
#define	FCF_DCN		0x5F		// disconnect - initiate call release
#define	FCF_CRP		0x58		// command repeat - resend last command

// command to receive
#define FCF_CTC		0x48		// continue to correct (Annex A)
#endif /* _t30_ */
