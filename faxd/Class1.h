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
#include "FaxParams.h"

class HDLCFrame;

/*
 * Class 1 modem capability (for sending/receiving).
 */
typedef struct {
    int		value;	// Class 1 parameter value (e.g for +FRM)
    u_char	br;	// Class 2 bit rate parameter
    u_short	sr;	// T.30 DCS signalling rate
    u_char	mod;	// modulation technique
    bool	ok;	// true if modem is capable
} Class1Cap;
#define	HasShortTraining(c) \
    ((c)->mod == V17 && ((c)->value & 1) && (c)[1].ok)

class Class1Modem : public FaxModem {
protected:
    fxStr	thCmd;			// command for transmitting a frame
    fxStr	rhCmd;			// command for receiving a frame
    fxStr	classCmd;		// set class command
    u_int	serviceType;		// modem service required
    FaxParams	dis_caps;	// current remote DIS
    u_int	frameSize;		// size of image frames
    u_int	signalRcvd;		// last signal received in ECM protocol
    u_int	nonV34br;		// modemParams.br without V.34
    bool	sendERR;		// T.30-A send ERR instead of MCF
    bool	hadV34Trouble;		// indicates failure due to V.34 restrictions
    bool	batchingError;		// indicates failure due to batching protocol
    const u_char* frameRev;		// HDLC frame bit reversal table
    fxStr	lid;			// encoded local id string
    fxStr	pwd;			// transmit password
    fxStr	sub;			// transmit subaddress
    Class1Cap	xmitCaps[15];		// modem send capabilities
    Class1Cap	recvCaps[15];		// modem recv capabilities
    const Class1Cap* curcap;		// capabilities being used
    u_int	discap;			// DIS signalling rate capabilities
    bool	prevPage;		// a previous page was received
    bool	pageGood;		// quality of last page received
    bool	recvdDCN;		// received DCN frame
    bool	messageReceived;	// expect/don't expect message carrier
    u_int	lastPPM;		// last PPM used
    bool	sendCFR;		// received TCF was not confirmed
    u_short	ecmBitPos;		// bit position to populate on ecmByte
    u_int	ecmByte;		// pending byte to add to ecmBlock
    u_short	ecmOnes;		// count of consecutive ones for adding zero bits
    u_char*	ecmFrame;		// to hold outgoing frames as they are read from the file
    u_int	ecmFramePos;		// fill pointer for ecmFrame
    u_char*	ecmBlock;		// to hold 256 raw ecmFrames to send before MCF
    u_long	ecmBlockPos;		// fill pointer for ecmBlock
    u_char*	ecmStuffedBlock;	// to hold image block after adding transparent zeros and FCS bytes
    u_long	ecmStuffedBlockPos;	// fill pointer for ecmStuffedBlockPos
    u_short	frameNumber;		// frame sequence number of ecmFrame in ecmBlock
    u_short	blockNumber;		// block sequence number of ecmBlock in page

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

// V.34 indicators
    bool	useV34;		// whether or not V.8 handhaking was used
    bool	gotEOT;		// V.34-fax heard EOT signal
    bool	gotCTRL;	// current channel indicator
    bool	gotRTNC;	// retrain control channel
    u_short	primaryV34Rate;	// rate indication for primary channel
    u_short	controlV34Rate;	// rate indication for control channel
    fxStr	ctrlFrameRcvd;	// unexpected control channel frame received

// modem setup stuff
    virtual bool setupModem();
    virtual bool setupClass1Parameters();
    virtual bool setupFlowControl(FlowControl fc);
// transmission support
    bool	sendPrologue(FaxParams& dcs_caps, const fxStr& tsi);
    bool	dropToNextBR(Class2Params&);
    bool	raiseToNextBR(Class2Params&);
    bool	sendTraining(Class2Params&, int, fxStr& emsg);
    bool	sendTCF(const Class2Params&, u_int ms);
    bool	sendPage(TIFF* tif, Class2Params&, u_int, u_int, fxStr& emsg);
    bool	sendPageData(u_char* data, u_int cc, const u_char* bitrev, bool ecm, fxStr& emsg);
    bool	sendRTC(Class2Params params, u_int ppmcmd, int lastbyte, fxStr& emsg);
    bool	sendPPM(u_int ppm, HDLCFrame& mcf, fxStr& emsg);
    bool	decodePPM(const fxStr& pph, u_int& ppm, fxStr& emsg);
// reception support
    const AnswerMsg* findAnswer(const char*);
    bool	recvIdentification(
		    u_int f1, const fxStr& pwd,
		    u_int f2, const fxStr& addr,
		    u_int f3, const fxStr& nsf,
		    u_int f4, const fxStr& id,
		    u_int f5, FaxParams& dics,
		    u_int timer, fxStr& emsg);
    bool	recvDCSFrames(HDLCFrame& frame);
    bool	recvTraining();
    bool	recvPPM(int& ppm, fxStr& emsg);
    bool	recvPageData(TIFF*, fxStr& emsg);
    bool	raiseRecvCarrier(bool& dolongtrain, fxStr& emsg);
    void	recvData(TIFF*, u_char* buf, int n);
    void	processDCSFrame(const HDLCFrame& frame);
    void	abortPageRecv();
// miscellaneous
    enum {			// Class 1-specific AT responses
	AT_FCERROR	= 100, 	// "+FCERROR"
	AT_FRH3		= 101,	// "+FRH:3"
    };
    virtual ATResponse atResponse(char* buf, long ms = 30*1000);
    virtual bool waitFor(ATResponse wanted, long ms = 30*1000);
    void	encodeTSI(fxStr& binary, const fxStr& ascii);
    void	encodeNSF(fxStr& binary, const fxStr& ascii);
    const fxStr& decodeTSI(fxStr& ascii, const HDLCFrame& binary);
    void	encodePWD(fxStr& binary, const fxStr& ascii);
    const fxStr& decodePWD(fxStr& ascii, const HDLCFrame& binary);
    const Class1Cap* findSRCapability(u_short sr, const Class1Cap[]);
    const Class1Cap* findBRCapability(u_short br, const Class1Cap[]);
    static bool isCapable(u_int sr, FaxParams& dis);
// class 1 HDLC frame support
    bool	transmitFrame(u_char fcf, bool lastFrame = true);
    bool	transmitFrame(u_char fcf, FaxParams& dcs_caps, bool lastFrame = true);
    bool	transmitFrame(u_char fcf, const fxStr&, bool lastFrame=true);
    bool	transmitFrame(u_char fcf, const u_char* code, const fxStr&, bool lastFrame=true);
    bool	transmitData(int br, u_char* data, u_int cc,
		    const u_char* bitrev, bool eod);
    bool	sendFrame(u_char fcf, bool lastFrame = true);
    bool	sendFrame(u_char fcf, FaxParams& dcs_caps, bool lastFrame = true);
    bool	sendFrame(u_char fcf, const fxStr&, bool lastFrame = true);
    bool	sendFrame(u_char fcf, const u_char* code, const fxStr&, bool lastFrame = true);
    bool	sendRawFrame(HDLCFrame& frame);
    bool	sendClass1Data(const u_char* data, u_int cc,
		    const u_char* bitrev, bool eod);
    bool	sendClass1ECMData(const u_char* data, u_int cc,
		     const u_char* bitrev, bool eod, u_int ppmcmd, fxStr& emsg);
    bool	recvFrame(HDLCFrame& frame, long ms = 10*1000);
    bool	recvTCF(int br, HDLCFrame&, const u_char* bitrev, long ms);
    bool	recvRawFrame(HDLCFrame& frame);
    bool	recvECMFrame(HDLCFrame& frame);
    bool        waitForDCEChannel(bool awaitctrl);
    bool        renegotiatePrimary(bool constrain);
    bool	syncECMFrame();
    bool	recvPageECMData(TIFF* tif, const Class2Params& params, fxStr& emsg);
    void	blockData(u_int byte, bool flag);
    bool	blockFrame(const u_char* bitrev, bool lastframe, u_int ppmcmd, fxStr& emsg);
    bool	endECMBlock();
    void	abortReceive();
    void	traceHDLCFrame(const char* direction, const HDLCFrame& frame);
// class 1 command support routines
    bool	class1Query(const fxStr& queryCmd, Class1Cap caps[]);
    bool	parseQuery(const char*, Class1Cap caps[]);
public:
    Class1Modem(FaxServer&, const ModemConfig&);
    virtual ~Class1Modem();
    void	hangup();

// send support
    bool	sendSetup(FaxRequest&, const Class2Params&, fxStr& emsg);
    CallStatus	dialResponse(fxStr& emsg);
    FaxSendStatus getPrologue(Class2Params&, bool&, fxStr&, u_int&);
    void	sendBegin();
    void	sendSetupPhaseB(const fxStr& pwd, const fxStr& sub);
    FaxSendStatus sendPhaseB(TIFF* tif, Class2Params&, FaxMachineInfo&,
		    fxStr& pph, fxStr& emsg, u_int& batched);
    void	sendEnd();
    void	sendAbort();

// receive support
    CallType	answerCall(AnswerType, fxStr& emsg, const char* number);
    FaxParams	modemDIS() const;
    bool	setupReceive();
    bool	recvBegin(fxStr& emsg);
    bool	recvEOMBegin(fxStr& emsg);
    bool	recvPage(TIFF*, u_int& ppm, fxStr& emsg, const fxStr& id);
    bool	recvEnd(fxStr& emsg);
    void	recvAbort();
    void	pokeConfig();

// polling support
    bool	requestToPoll(fxStr&);
    bool	pollBegin(const fxStr& cig, const fxStr& sep, const fxStr& pwd,
		    fxStr& emsg);

// miscellaneous
    bool	faxService(bool enableV34);	// switch to fax mode
    bool	reset(long ms);			// reset modem
    bool	ready(long ms);			// ready modem
    void	setLID(const fxStr& number);	// set local id string
    bool	supportsPolling() const;	// modem capability
};
#endif /* _CLASS1_ */
