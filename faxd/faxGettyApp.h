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
#ifndef _faxGettyApp_
#define	_faxGettyApp_
/*
 * HylaFAX Modem Handler.
 */
#include "faxApp.h"
#include "FaxServer.h"

class UUCPLock;
class Getty;

class AnswerTimeoutHandler : public IOHandler {
public:
    AnswerTimeoutHandler();
    ~AnswerTimeoutHandler();
    void timerExpired(long, long);
};

class faxGettyApp : public FaxServer, public faxApp {
public:
    struct stringtag {
	const char*	 name;
	fxStr faxGettyApp::* p;
	const char*	 def;		// NULL is shorthand for ""
    };
    struct numbertag {
	const char*	 name;
	u_int faxGettyApp::*p;
	u_int		 def;
    };
    struct booltag {
	const char*	 name;
	bool faxGettyApp::*p;
	u_int		 def;
    };
private:
// runtime state
    fxStr	readyState;		// modem ready state to send queuer
    int		devfifo;		// fifo device interface
    UUCPLock*	modemLock;		// UUCP interlock
    AnswerTimeoutHandler answerHandler;	// for timing out inbound calls

    u_short	ringsBeforeAnswer;	// # rings to wait
    u_short	ringsHeard;		// # rings received
    fxStr	qualifyCID;		// if set, no answer w/o acceptable cid
    time_t	lastCIDModTime;		// last mod time of CID patterns file
    REArray*	cidPats;		// recv cid patterns
    fxBoolArray* acceptCID;		// accept/reject matched cid
    CallerID	received_cid;		// non-null received CNID
    fxStr	gettyArgs;		// getty arguments
    fxStr	vgettyArgs;		// voice getty arguments
    fxStr	egettyArgs;		// extern getty arguments
    bool	adaptiveAnswer;		// answer as data if fax answer fails
    bool	lockDataCalls;		// hold uucp lock for getty
    bool	lockVoiceCalls;		// hold uucp lock for vgetty
    bool	lockExternCalls;	// hold uucp lock for egetty
    u_int	answerBias;		// rotor bias applied after good calls
    u_short	answerRotor;		// rotor into possible selections
    u_short	answerRotorSize;	// rotor table size
    AnswerType	answerRotary[3];	// rotary selection of answer types
    fxStr	faxRcvdCmd;		// fax received command
    u_int	modemPriority;		// modem priority passed to faxq

    static faxGettyApp* _instance;

    static stringtag strings[];
    static numbertag numbers[];
    static booltag booleans[];

    static const fxStr recvDir;

// configuration support
    void	setupConfig();
    void	resetConfig();
    bool	setConfigItem(const char* tag, const char* value);
    void	setAnswerRotary(const fxStr& value);
// modem handling
    bool	isModemLocked();
    bool	canLockModem();
    bool	lockModem();
    void	unlockModem();
    bool	setupModem();
    void	discardModem(bool dropDTR);
// inbound call handling
    bool	isCIDOk(const fxStr& cid);
    bool	processCall(CallType ctype, fxStr& emsg, const CallerID& cid);
    CallType	runGetty(const char* what,
		    Getty* (*newgetty)(const fxStr&, const fxStr&),
		    const char* args, fxStr &emsg,
		    bool keepLock, bool keepModem = false);
    void	setRingsBeforeAnswer(int rings);
    void	listenBegin();
    void	listenForRing();
    void	answerPhoneCmd(AnswerType, const char* dialnumber = NULL);
    void	answerPhone(AnswerType, CallType, const CallerID& cid, const char* dialnumber = NULL);
    void	answerCleanup();
    bool	answerCall(AnswerType atype, CallType& ctype, fxStr& emsg, const CallerID& cid, const char* dialnumber = NULL);

    friend void AnswerTimeoutHandler::timerExpired(long, long);
// miscellaneous stuff
    bool	sendModemStatus(const char* msg);
// FIFO-related stuff
    void	openFIFOs();
    void	closeFIFOs();
    void	FIFOMessage(const char* mesage);
// Dispatcher hooks
    int		inputReady(int);
// notification interfaces used by FaxServer
    void	notifyModemReady();
    void	notifyModemWedged();
    void	notifyRecvBegun(const FaxRecvInfo&);
    void	notifyPageRecvd(TIFF* tif, const FaxRecvInfo&, int ppm);
    void	notifyDocumentRecvd(const FaxRecvInfo&);
    void	notifyRecvDone(const FaxRecvInfo&);
public:
    faxGettyApp(const fxStr& device, const fxStr& devID);
    ~faxGettyApp();

    static faxGettyApp& instance();

    void	initialize(int argc, char** argv);
    void	open();
    void	close();
};
#endif
