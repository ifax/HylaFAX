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
#ifndef _CLASS1_
#define	_CLASS1_
/*
 * EIA/TIA-578 (Class 1) Modem Driver.
 */
#include "FaxModem.h"

class HDLCFrame;

/*
 * Class 1 modem capability (for sending/receiving).
 */
typedef struct {
    int		value;	// Class 1 parameter value (e.g for +FRM)
    u_char	br;	// Class 2 bit rate parameter
    u_short	sr;	// T.30 DCS signalling rate
    u_char	mod;	// modulation technique
    fxBool	ok;	// true if modem is capable
} Class1Cap;
#define	HasShortTraining(c) \
    ((c)->mod == V17 && ((c)->value & 1) && (c)[1].ok)

class Class1Modem : public FaxModem {
protected:
    fxStr	thCmd;			// command for transmitting a frame
    fxStr	rhCmd;			// command for receiving a frame
    u_int	dis;			// current remote DIS
    u_int	xinfo;			// current remote DIS extensions
    const u_char* frameRev;		// HDLC frame bit reversal table
    fxStr	lid;			// encoded local id string
    fxStr	pwd;			// transmit password
    fxStr	sub;			// transmit subaddress
    Class1Cap	xmitCaps[15];		// modem send capabilities
    Class1Cap	recvCaps[15];		// modem recv capabilities
    const Class1Cap* curcap;		// capabilities being used
    u_int	discap;			// DIS signalling rate capabilities
    fxBool	prevPage;		// a previous page was received
    fxBool	pageGood;		// quality of last page received
    fxBool	recvdDCN;		// received DCN frame
    fxBool	messageReceived;	// expect/don't expect message carrier
    u_int	lastPPM;		// last PPM during receive

    static const u_int modemPFMCodes[8];// map T.30 FCF to Class 2 PFM
    static const u_int modemPPMCodes[8];// map T.30 FCF to Class 2 PPM
    static const Class1Cap basicCaps[15];
    static const char* rmCmdFmt;
    static const char* tmCmdFmt;

    enum {			// modulation techniques
	V21   = 0,		// v.21, ch 2 300 bits/sec
	V27FB = 1,		// v.27ter fallback mode
	V27   = 2,		// v.27ter, 4800, 2400
	V29   = 3,		// v.29, 9600, 7200
	V17   = 4,		// v.17, 14400, 12000, 9600, 7200
	V33   = 5 		// v.33, 14400, 12000, 9600, 7200
    };
    static const char* modulationNames[6];

// modem setup stuff
    virtual fxBool setupModem();
    virtual fxBool setupClass1Parameters();
    virtual fxBool setupFlowControl(FlowControl fc);
// transmission support
    fxBool	sendPrologue(u_int dcs, u_int xinfo, const fxStr& tsi);
    fxBool	dropToNextBR(Class2Params&);
    fxBool	raiseToNextBR(Class2Params&);
    fxBool	sendTraining(Class2Params&, int, fxStr& emsg);
    fxBool	sendTCF(const Class2Params&, u_int ms);
    fxBool	sendPage(TIFF* tif, const Class2Params&, u_int, fxStr& emsg);
    fxBool	sendPageData(u_char* data, u_int cc, const u_char* bitrev);
    fxBool	sendRTC(fxBool is2D);
    fxBool	sendPPM(u_int ppm, HDLCFrame& mcf, fxStr& emsg);
    fxBool	decodePPM(const fxStr& pph, u_int& ppm, fxStr& emsg);
// reception support
    const AnswerMsg* findAnswer(const char*);
    fxBool	recvIdentification(
		    u_int f1, const fxStr& pwd,
		    u_int f2, const fxStr& addr,
		    u_int f3, const fxStr& id,
		    u_int f4, u_int dics, u_int xinfo,
		    u_int timer, fxStr& emsg);
    fxBool	recvDCSFrames(HDLCFrame& frame);
    fxBool	recvTraining();
    fxBool	recvPPM(int& ppm, fxStr& emsg);
    fxBool	recvPageData(TIFF*, fxStr& emsg);
    void	recvData(TIFF*, u_char* buf, int n);
    void	processDCSFrame(const HDLCFrame& frame);
    void	abortPageRecv();
// miscellaneous
    enum {			// Class 1-specific AT responses
	AT_FCERROR	= 100 	// "+FCERROR"
    };
    virtual ATResponse atResponse(char* buf, long ms = 30*1000);
    virtual fxBool waitFor(ATResponse wanted, long ms = 30*1000);
    void	encodeTSI(fxStr& binary, const fxStr& ascii);
    const fxStr& decodeTSI(fxStr& ascii, const HDLCFrame& binary);
    void	encodePWD(fxStr& binary, const fxStr& ascii);
    const fxStr& decodePWD(fxStr& ascii, const HDLCFrame& binary);
    const Class1Cap* findSRCapability(u_short sr, const Class1Cap[]);
    const Class1Cap* findBRCapability(u_short br, const Class1Cap[]);
// class 1 HDLC frame support
    fxBool	transmitFrame(u_char fcf, fxBool lastFrame = TRUE);
    fxBool	transmitFrame(u_char fcf, u_int, u_int, fxBool lastFrame = TRUE);
    fxBool	transmitFrame(u_char fcf, const fxStr&, fxBool lastFrame=TRUE);
    fxBool	transmitData(int br, u_char* data, u_int cc,
		    const u_char* bitrev, fxBool eod);
    fxBool	sendFrame(u_char fcf, fxBool lastFrame = TRUE);
    fxBool	sendFrame(u_char fcf, u_int, u_int, fxBool lastFrame = TRUE);
    fxBool	sendFrame(u_char fcf, const fxStr&, fxBool lastFrame = TRUE);
    fxBool	sendRawFrame(HDLCFrame& frame);
    fxBool	sendClass1Data(const u_char* data, u_int cc,
		    const u_char* bitrev, fxBool eod);
    fxBool	recvFrame(HDLCFrame& frame, long ms = 10*1000);
    fxBool	recvTCF(int br, HDLCFrame&, const u_char* bitrev, long ms);
    fxBool	recvRawFrame(HDLCFrame& frame);
    void	abortReceive();
    void	traceHDLCFrame(const char* direction, const HDLCFrame& frame);
// class 1 command support routines
    fxBool	class1Query(const char* what, Class1Cap caps[]);
    fxBool	parseQuery(const char*, Class1Cap caps[]);
public:
    Class1Modem(FaxServer&, const ModemConfig&);
    virtual ~Class1Modem();

// send support
    fxBool	sendSetup(FaxRequest&, const Class2Params&, fxStr& emsg);
    CallStatus	dialResponse(fxStr& emsg);
    FaxSendStatus getPrologue(Class2Params&, fxBool&, fxStr&);
    void	sendBegin();
    void	sendSetupPhaseB(const fxStr& pwd, const fxStr& sub);
    FaxSendStatus sendPhaseB(TIFF* tif, Class2Params&, FaxMachineInfo&,
		    fxStr& pph, fxStr& emsg);
    void	sendEnd();
    void	sendAbort();

// receive support
    CallType	answerCall(AnswerType, fxStr& emsg);
    u_int	modemDIS() const;
    u_int	modemXINFO() const;
    fxBool	setupReceive();
    fxBool	recvBegin(fxStr& emsg);
    fxBool	recvPage(TIFF*, int& ppm, fxStr& emsg);
    fxBool	recvEnd(fxStr& emsg);
    void	recvAbort();

// polling support
    fxBool	requestToPoll(fxStr&);
    fxBool	pollBegin(const fxStr& cig, const fxStr& sep, const fxStr& pwd,
		    fxStr& emsg);

// miscellaneous
    fxBool	faxService();			// switch to fax mode
    fxBool	reset(long ms);			// reset modem
    void	setLID(const fxStr& number);	// set local id string
    fxBool	supportsPolling() const;	// modem capability
};
#endif /* _CLASS1_ */
