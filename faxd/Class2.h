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
#ifndef _CLASS2_
#define	_CLASS2_
/*
 * Base class for a Class 2 and Class 2.0 Modem Drivers.
 */
#include "FaxModem.h"
#include <stdarg.h>

class Class2Modem : public FaxModem {
protected:
    fxStr	classCmd;		// set class command
    fxStr	cqCmds;			// copy quality setup commands
    fxStr	tbcCmd;			// modem-host communication mode command
    fxStr	crCmd;			// enable receiving command
    fxStr	phctoCmd;		// set Phase C timeout command
    fxStr	bugCmd;			// enable HDLC tracing command
    fxStr	lidCmd;			// set local ID command
    fxStr	dccCmd;			// set configuration parameters command
    fxStr	dccQueryCmd;		// modem capabilities query command
    fxStr	disCmd;			// set session parameters command
    fxStr	cigCmd;			// set polling ID string command
    fxStr	splCmd;			// set polling request command
    fxStr	nrCmd;			// negotiation message reporting control
    fxStr	pieCmd;			// procedure interrupt enable control
    fxStr	borCmd;			// set bit order command
    fxStr	abortCmd;		// abort session command
    fxStr	ptsCmd;			// set page status command
    fxStr	ptsQueryCmd;		// query page status command
    fxStr	minspCmd;		// set minimum transmit speed command
    fxStr	apCmd;			// set address&polling caps. command
    fxStr	saCmd;			// set subaddress command
    fxStr	paCmd;			// set selective polling address command
    fxStr	pwCmd;			// set password command
    fxStr	noFlowCmd;		// no flow control command
    fxStr	softFlowCmd;		// software flow control command
    fxStr	hardFlowCmd;		// hardware flow control command
    u_int	serviceType;		// modem service required
    u_int	modemCQ;		// copy quality capabilities mask
    u_int	sendCQ;			// sending copy quality capabilities mask

    bool	xmitWaitForXON;		// if true, wait for XON when sending
    bool	hostDidCQ;		// if true, copy quality done on host
    bool	hasPolling;		// if true, modem does polled recv
    bool	useExtendedDF;		// if true, modem has Agere data format extension
    char	recvDataTrigger;	// char to send to start recv'ing data
    char	hangupCode[4];		// hangup reason (from modem)
    bool	hadHangup;		// true if +FHNG:/+FHS: received
    fxStr	lid;			// prepared local identifier string

// modem setup stuff
    virtual bool setupModem(bool isSend = true);
    virtual bool setupModel(fxStr& model);
    virtual bool setupRevision(fxStr& rev);
    virtual bool setupDCC();
    virtual bool setupClass2Parameters();
    virtual bool setupFlowControl(FlowControl fc);
// transmission support
    bool	dataTransfer();
    bool	sendRTC(Class2Params params);
    bool	sendPageData(TIFF* tif, u_int pageChop);

    virtual bool sendPage(TIFF* tif, u_int pageChop) = 0;
    virtual bool pageDone(u_int ppm, u_int& ppr) = 0;
// reception support
    const AnswerMsg* findAnswer(const char*);
    bool	recvDCS(const char*);
    bool	recvPageData(TIFF*, fxStr& emsg);
    bool	recvPPM(TIFF*, int& ppr);
    bool	parseFPTS(TIFF*, const char* cp, int& ppr);
    void	abortPageRecv();
// miscellaneous
    enum {			// Class 2-specific AT responses
	AT_FHNG		= 100,	// remote hangup
	AT_FCON		= 101,	// fax connection status
	AT_FPOLL	= 102,	// document available for polling status
	AT_FDIS		= 103,	// DIS received status
	AT_FNSF		= 104,	// NSF received status
	AT_FCSI		= 105,	// CSI received status
	AT_FPTS		= 106,	// post-page status
	AT_FDCS		= 107,	// DCS received status
	AT_FNSS		= 108,	// NSS received status
	AT_FTSI		= 109,	// TSI received status
	AT_FET		= 110,	// post-page-response status
	AT_FVO		= 111,	// voice transition status
	AT_FSA		= 112,	// subaddress status
	AT_FPA		= 113,  // polling address status
	AT_FPW		= 114	// password status
    };
    virtual ATResponse atResponse(char* buf, long ms = 30*1000) = 0;
    bool	waitFor(ATResponse wanted, long ms = 30*1000);
    fxStr	stripQuotes(const char*);
// hangup processing
    void	processHangup(const char*);
    bool	isNormalHangup();
    const char*	hangupCause(const char* code);
    void	tracePPR(const char* dir, u_int ppr);
    void	tracePPM(const char* dir, u_int ppm);
// class 2 command support routines
    bool	class2Cmd(const fxStr& cmd, int a0,
		    ATResponse = AT_OK, long ms = 30*1000);
    bool	class2Cmd(const fxStr& cmd, const fxStr& a0,
		    ATResponse = AT_OK, long ms = 30*1000);
    bool	class2Cmd(const fxStr& cmd, const Class2Params&, bool isDCC,
		    ATResponse =AT_OK, long ms = 30*1000);
// parsing routines for capability&parameter strings
    bool	parseClass2Capabilities(const char* cap, Class2Params&, bool isDIS);
    bool	parseRange(const char*, Class2Params&);
    const char* skipStatus(const char*);

    Class2Modem(FaxServer&, const ModemConfig&);
public:
    virtual ~Class2Modem();

// send support
    bool	sendSetup(FaxRequest&, const Class2Params&, fxStr& emsg);
    CallStatus	dialResponse(fxStr& emsg);
    FaxSendStatus getPrologue(Class2Params&, bool&, fxStr&, u_int&);
    FaxSendStatus sendPhaseB(TIFF* tif, Class2Params&, FaxMachineInfo&,
		    fxStr& pph, fxStr& emsg, u_int& batched);
    void	sendAbort();

// receive support
    bool	setupReceive();
    bool	recvBegin(fxStr& emsg);
    bool	recvEOMBegin(fxStr& emsg);
    bool	recvPage(TIFF*, u_int& ppm, fxStr& emsg, const fxStr& id);
    bool	recvEnd(fxStr& emsg);
    void	recvAbort();
    void	pokeConfig(bool isSend);

// polling support
    bool	requestToPoll(fxStr& emsg);
    bool	pollBegin(const fxStr& cig, const fxStr& sep, const fxStr& pwd,
		    fxStr& emsg);

// miscellaneous
    bool	faxService(bool enableV34);	// switch to fax mode (send)
    bool	reset(long ms);			// reset modem
    void	setLID(const fxStr& number);	// set local id string
    bool	supportsPolling() const;	// modem capability
    int		lastByte;
};
#endif /* _CLASS2_ */
