/*	$Id$ */
/*
 * Copyright (c) 1994-1996 Sam Leffler
 * Copyright (c) 1994-1996 Silicon Graphics, Inc.
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
#ifndef _ClassModem_
#define	_ClassModem_
/*
 * Class-X-style Modem Driver Interface.
 */
#include <stdarg.h>
#include "Str.h"

class ModemServer;
class ModemConfig;
class FaxRequest;
class Class2Params;

// NB: these would be enums in the ClassModem class
//     if there were a portable way to refer to them!
typedef u_int CallStatus;	// return status from dialing op
typedef	u_int CallType;		// type detected for incoming call
typedef	u_int AnswerType;	// type of call to answer for
typedef u_int SpeakerVolume;
typedef	u_int ATResponse;	// response code from AT command
typedef	u_int BaudRate;		// serial line communication rate
typedef	u_int FlowControl;	// serial line flow control scheme
typedef	u_int SetAction;		// how to act when setting line
typedef struct {
    const char*	msg;		// string to match
    u_short	len;		// string length
    ATResponse	expect;		// next AT response to expect
    CallStatus	status;		// resultant call status
    CallType	type;		// resultant call type
} AnswerMsg;

typedef struct {
    fxStr	number;		// caller's phone number
    fxStr	name;		// caller's identity
} CallerID;

/*
 * AT command escape codes.  Command strings specified in the
 * modem configuration file are parsed and embedded commands
 * are converted to one of the escape codes below.
 */ 
#define	ord(e)	((int)(e))

#define	ESC_SETBR	(0x80|0x01)	// set baud rate
#define	ESC_SETFLOW	(0x80|0x02)	// set flow control
#define	ESC_DELAY	(0x80|0x04)	// delay period of time
#define	ESC_WAITFOR	(0x80|0x08)	// wait for modem response
#define	ESC_FLUSH	(0x80|0x10)	// flush input queue

#ifdef OFF
#undef OFF			// workaround for SCO
#endif

/*
 * This is an abstract class that defines the interface to
 * the set of modem drivers.  Real drivers are derived from
 * this and fill in the pure virtual methods to, for example,
 * send specific commands to the modem.  The Class2Params
 * structure defines the session parameters used/supported
 * by this interface.  Class2Params is derived from the
 * set of parameters supported by the Class 2 interface spec.
 */
class ClassModem {
public:
    enum {			// ClassModem::CallStatus
	OK	   = 0,		// phone answered & carrier received
	BUSY	   = 1,		// destination phone busy
	NOCARRIER  = 2,		// no carrier from remote
	NOANSWER   = 3,		// no answer from remote
	NODIALTONE = 4,		// no local dialtone (phone not plugged in?)
	ERROR	   = 5,		// error in dial command
	FAILURE	   = 6,		// other problem (e.g. modem turned off)
	NOFCON	   = 7,		// carrier established, but phase a failure
	DATACONN   = 8,		// data carrier established
	RING	   = 9		// glare - ring detected after dial
    };

    enum {			// ClassModem::CallType
	CALLTYPE_UNKNOWN = 0,	// unknown variety
	CALLTYPE_DATA	= 1,	// data connection
	CALLTYPE_FAX	= 2,	// fax connection
	CALLTYPE_VOICE	= 3,	// voice connection
	CALLTYPE_ERROR	= 4,	// error deducing type of incoming call
	CALLTYPE_DONE	= 5	// subprocess completed call handling
    };
    static const char* callTypes[5];

    enum {			// ClassModem::SpeakerVolume
	OFF	= 0,		// nothing
	QUIET	= 1,		// somewhere near a dull chirp
	LOW	= 2,		// normally acceptable
	MEDIUM	= 3,		// heard above a stereo
	HIGH	= 4		// ear splitting
    };

    enum {			// ClassModem::BaudRate
	BR0	= 0,		// force hangup/drop DTR
	BR300	= 1,		// 300 bits/sec
	BR1200	= 2,		// 1200 bits/sec
	BR2400	= 3,		// 2400 bits/sec
	BR4800	= 4,		// 4800 bits/sec
	BR9600	= 5,		// 9600 bits/sec
	BR19200	= 6,		// 19200 bits/sec
	BR38400	= 7,		// 38400 bits/sec
	BR57600	= 8,		// 57600 bits/sec
	BR76800	= 9,		// 76800 bits/sec
	BR115200= 10		// 115200 bits/sec
    };

    enum {			// ClassModem::FlowControl
	FLOW_NONE	= 0,	// no flow control
	FLOW_XONXOFF	= 1,	// XON/XOFF software flow control
	FLOW_RTSCTS	= 2	// RTS/CTS hardware flow control
    };

    enum {			// ClassModem::SetAction
	ACT_NOW		= 0,	// set terminal parameters now
	ACT_DRAIN	= 1,	// set parameters after draining output queue
	ACT_FLUSH	= 2	// set parameters after flush both queues
    };

    enum {			// ClassModem::AnswerType
	ANSTYPE_ANY	= 0,	// any kind of call
	ANSTYPE_DATA	= 1,	// data call
	ANSTYPE_FAX	= 2,	// fax call
	ANSTYPE_VOICE	= 3,	// voice call
	ANSTYPE_EXTERN	= 3	// any kind of call, but answered externally
    };
    static const char* answerTypes[4];

    enum {			// ClassModem::ATResponse
	AT_NOTHING	= 0,	// for passing as a parameter
	AT_OK		= 1,	// "OK" response
	AT_CONNECT	= 2,	// "CONNECT" response
	AT_NOANSWER	= 3,	// "NO ANSWER" response
	AT_NOCARRIER	= 4,	// "NO CARRIER" response
	AT_NODIALTONE	= 5,	// "NO DIALTONE" response
	AT_BUSY		= 6,	// "BUSY" response
	AT_OFFHOOK	= 7,	// "PHONE OFF-HOOK" response
	AT_RING		= 8,	// "RING" response
	AT_ERROR	= 9,	// "ERROR" response
	AT_EMPTYLINE	= 10,	// empty line (0 characters received)
	AT_TIMEOUT	= 11,	// timeout waiting for response
	AT_DLEETX	= 12,	// dle/etx characters
	AT_XON		= 13,	// xon character
	AT_OTHER	= 14	// unknown response (not one of above)
    };
private:
    ModemServer& server;	// server for getting to device
    long	dataTimeout;	// baud rate-dependent data timeout
    BaudRate	rate;		// selected DTE-DCE communication rate
    FlowControl	iFlow;		// input flow control scheme
    FlowControl	oFlow;		// output flow control scheme
protected:
// NB: these are defined protected for convenience (XXX)
    const ModemConfig& conf;	// configuration parameters
    FlowControl	flowControl;	// current DTE-DCE flow control scheme
    u_int	modemServices;	// services modem supports
    fxStr	modemMfr;	// manufacturer identification
    fxStr	modemModel;	// model identification
    fxStr	modemRevision;	// product revision identification
    char	rbuf[1024];	// last input line
    ATResponse	lastResponse;	// last atResponse code
    fxStr	mfrQueryCmd;	// manufacturer identification command
    fxStr	modelQueryCmd;	// model identification command
    fxStr	revQueryCmd;	// product revision identification command

    static const char* serviceNames[9];	 // class 2 services
    static const char* callStatus[10];	 // printable call status
    static const char* ATresponses[15];

    ClassModem(ModemServer&, const ModemConfig&);

// setup and configuration
    static void setupDefault(fxStr&, const fxStr&, const char*);
    virtual bool selectBaudRate(BaudRate max, FlowControl i, FlowControl o);
    virtual bool setupManufacturer(fxStr& mfr);
    virtual bool setupModel(fxStr& model);
    virtual bool setupRevision(fxStr& rev);

    bool doQuery(const fxStr& queryCmd, fxStr& result, long ms = 30*1000);
// dial/answer interactions with derived classes
    virtual const AnswerMsg* findAnswer(const char* s);
    virtual CallType answerResponse(fxStr& emsg);
    virtual CallStatus dialResponse(fxStr& emsg) = 0;
    virtual bool isNoise(const char*);
// miscellaneous
    void	modemSupports(const char* fmt, ...);
    void	modemCapability(const char* fmt, ...);
    void	traceBits(u_int bits, const char* bitNames[]);
    void	traceBitMask(u_int bits, const char* bitNames[]);
public:
    virtual ~ClassModem();

    virtual bool setupModem() = 0;
    virtual bool isFaxModem() const = 0;	// XXX safe to cast

    virtual bool sync(long ms = 0);		// synchronize (wait for "OK")
    virtual bool reset(long ms = 5*1000);	// reset modem state
    virtual void hangup();			// hangup the phone

// configuration controls
    virtual void setSpeakerVolume(SpeakerVolume);
// configuration query
    const fxStr& getModel() const;
    const fxStr& getManufacturer() const;
    const fxStr& getRevision() const;
    fxStr	getCapabilities() const;
    u_int	getModemServices() const;
// data transfer timeout controls
    void	setDataTimeout(long secs, u_int br);
    long	getDataTimeout() const;
// miscellaneous
    void	pause(u_int ms);
    void	modemTrace(const char* fmt, ...);
    void	protoTrace(const char* fmt, ...);
    void	serverTrace(const char* fmt, ...);
// modem i/o support
    void	trimModemLine(char buf[], int& cc);
    int		getModemLine(char buf[], u_int bufSize, long ms = 0);
// support to write to modem w/ timeout
    void	beginTimedTransfer();
    void	endTimedTransfer();
    bool	wasTimeout();
    void	setTimeout(bool);
    void	flushModemInput();
    bool	putModem(void* data, int n, long ms = 0);
    bool	putModemData(void* data, int n);
    bool	putModemDLEData(const u_char* data, u_int,
		    const u_char* brev, long ms);
    bool	putModemLine(const char* cp);
    int		getModemBit(long ms = 0);
    int		getModemChar(long ms = 0);
    int		getModemDataChar();
    int		getLastByte();
    bool	didBlockEnd();
    void	resetBlock();
    void	startTimeout(long ms);
    void	stopTimeout(const char* whichdir);
// host-modem protocol parsing support
    virtual ATResponse atResponse(char* buf, long ms = 30*1000);
    bool	waitFor(ATResponse wanted, long ms = 30*1000);
    bool	atCmd(const fxStr& cmd, ATResponse = AT_OK, long ms = 30*1000);
    bool	atQuery(const char* what, fxStr& v, long ms = 30*1000);
    bool	atQuery(const char* what, u_int& v, long ms = 30*1000);
    bool	parseRange(const char*, u_int&);
    bool	vparseRange(const char*, int masked, int nargs ...);
// modem line control
    bool	sendBreak(bool pause);
    bool	setBaudRate(BaudRate rate);
    bool	setBaudRate(BaudRate rate, FlowControl i, FlowControl o);
    bool	setXONXOFF(FlowControl i, FlowControl o, SetAction);
    bool	setDTR(bool on);
    bool	setInputBuffering(bool on);
    bool	modemStopOutput();
    FlowControl	getInputFlow();
    FlowControl	getOutputFlow();
// server-related stuff
    bool	abortRequested();

    /*
     * Send support:
     *
     * if (dial(number, params, emsg) == OK) {
     *	  ...do stuff...
     * }
     * hangup();
     */
    virtual bool dataService();
    virtual CallStatus dial(const char* number, fxStr& emsg);

    /*
     * Receive support:
     *
     * if (waitForRings(nrings, ctype, cid)) {	// wait before answering phone
     *    case (answerCall(type, emsg)) {
     *    CALLTYPE_FAX:
     *	        ...do fax kinds of things...
     *      break;
     *	  CALLTYPE_DATA:
     *	        ...do data kinds of things...
     *      break;
     *	  CALLTYPE_VOICE:
     *	        ...do voice kinds of things...
     *      break;
     *    }
     * }
     *
     * with recvAbort available to abort a receive
     * at any time in this procedure.
     */
    virtual bool waitForRings(u_int rings, CallType&, CallerID&);
    virtual CallType answerCall(AnswerType, fxStr& emsg);
    virtual void answerCallCmd(CallType);
};
inline long ClassModem::getDataTimeout() const		{ return dataTimeout; }
inline const fxStr& ClassModem::getModel() const	{ return modemModel; }
inline const fxStr& ClassModem::getManufacturer() const	{ return modemMfr; }
inline const fxStr& ClassModem::getRevision() const	{ return modemRevision; }
inline u_int ClassModem::getModemServices() const       { return modemServices; }
inline FlowControl ClassModem::getInputFlow()		{ return iFlow; }
inline FlowControl ClassModem::getOutputFlow()		{ return oFlow; }
#endif /* _ClassModem_ */
