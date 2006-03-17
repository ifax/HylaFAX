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
#ifndef _ModemServer_
#define	_ModemServer_
/*
 * Modem and Protocol Server.
 */
#include <stdarg.h>

#include "ClassModem.h"
#include "FaxRequest.h"
#include "IOHandler.h"
#include "ServerConfig.h"
#include "Timeout.h"

#include <sys/time.h>

#ifdef tcsetattr
#undef tcsetattr			// workaround for SCO
#endif
#ifdef tcgetattr
#undef tcgetattr			// workaround for SCO
#endif

class FaxMachineInfo;
class FaxMachineLog;

/*
 * This class defines the ``server process'' that manages
 * a modem and implements the necessary protocol above the
 * ClassModem driver interface.
 */
class ModemServer : public ServerConfig, public IOHandler {
protected:
    enum Parity {
	NONE            = 0,
	EVEN            = 1,
	ODD             = 2
    };
private:
    FILE*	statusFile;		// server status file
    bool	deduceComplain;		// if true, complain when can't deduce
    bool	changePriority;		// change process priority by state
    bool	delayConfig;		// suppress effects while reading config
    fxStr	dialRulesFile;		// dial string rules filename
    fxStr	commid;			// communication ID
// generic modem-related stuff
    int		modemFd;		// open modem file
    fxStr	modemDevice;		// name of device to open
    fxStr	modemDevID;		// device identifier
    ClassModem*	modem;			// modem driver
    Timeout	timer;			// timeout support class
    bool	timeout;		// timeout during i/o operations
    fxStr	configFile;		// pathname to configuration file
    BaudRate	curRate;		// current termio baud rate
    Parity	curParity;		// current termio parity setting
    u_int	curVMin;		// current termio VMIN setting
    u_int	curVTime;		// current termio VTIME setting
    u_int	setupAttempts;		// consec. failed attempts to init modem
// buffered i/o stuff
    short	rcvCC;			// # bytes pending in rcvBuf
    short	rcvNext;		// next available byte in rcvBuf
    u_short	rcvBit;			// pending bit of rcvNext to send
    int		gotByte;		// byte held for bit destruction
    bool	sawBlockEnd;		// whether DLE+ETX has been seen
    bool	inputBuffered;		// whether or not modem input is buffered
    u_char	rcvBuf[1024];		// receive buffering

    friend class ClassModem;

// configuration stuff
    void	setDialRules(const char* name);
    void	setModemSpeakerVolume(SpeakerVolume);
// state-related stuff
    static const char* stateNames[9];
    static const char* stateStatus[9];

    bool	openDevice(const char* dev);
    bool	reopenDevice();
    bool	tcsetattr(int op, struct termios& term);
    bool	tcgetattr(const char* method, struct termios& term);
    static void setFlow(struct termios&, FlowControl iflow, FlowControl oflow);
    static void setParity(struct termios&, Parity);

// general trace interface
    void	traceModemIO(const char* dir, const u_char* buf, u_int cc);
protected:
    enum ModemServerState {
	BASE		= 0,
	RUNNING		= 1,
	MODEMWAIT	= 2,
	LOCKWAIT	= 3,
	GETTYWAIT	= 4,
	SENDING		= 5,
	ANSWERING	= 6,
	RECEIVING	= 7,
	LISTENING	= 8
    };
    ModemServerState state;
    bool	abortCall;		// abort current send/receive
// logging and tracing
    FaxMachineLog* log;			// current log device

    ModemServer(const fxStr& deviceName, const fxStr& devID);

// modem driver setup+teardown interfaces
    virtual bool setupModem(bool isSend = true);
    virtual bool readyModem();
    virtual void discardModem(bool dropDTR);
    virtual ClassModem* deduceModem(bool isSend = true);
// low-level modem interfaces
    int		getModemFd();				// XXX
    BaudRate	getModemRate() const;
    virtual fxStr getModemCapabilities() const;
// modem i/o support
    void	timerExpired(long, long);
    int		getModemLine(char buf[], u_int bufSize, long ms = 0);
    int		getModemChar(long ms = 0);
    int		getModemBit(long ms = 0);
    int		getLastByte();
    bool	didBlockEnd();
    void	resetBlock();
    bool	isModemInput() const;
    void	flushModemInput();
    bool	putModem(const void* data, int n, long ms = 0);
    bool	putModem1(const void* data, int n, long ms = 0);
    void	startTimeout(long ms);
    void	stopTimeout(const char* whichdir);
// modem line control
    bool	sendBreak(bool pause);
    bool	setBaudRate(BaudRate rate);
    bool	setBaudRate(BaudRate rate, FlowControl i, FlowControl o);
    bool	setParity(Parity parity);
    bool	setXONXOFF(FlowControl i, FlowControl o, SetAction act);
    bool	setDTR(bool on);
    bool	setInputBuffering(bool on);
    bool	modemStopOutput();
// modem driver interfaces
    bool	modemWaitForRings(u_short rings, CallType&, CallID&);
    CallType	modemAnswerCall(AnswerType, fxStr&, const char* dialnumber = NULL);
    void	modemAnswerCallCmd(CallType);
    void	modemFlushInput();
    void	modemHangup();
// server state and related control interfaces
    void	changeState(ModemServerState, long timeout = 0);
    void	setServerStatus(const char* fmt, ...);
    void	setProcessPriority(ModemServerState s);
// system logging interfaces
    void	traceServer(const char* fmt ...);
    void	traceProtocol(const char* fmt ...);
    void	traceModemOp(const char* fmt ...);
    void	traceStatus(int kind, const char* fmt ...);
// modem locking interfaces implemented in derived class
    virtual bool canLockModem() = 0;
    virtual bool lockModem() = 0;
    virtual void unlockModem() = 0;
// notification interfaces implemented in derived class
    virtual void notifyModemReady() = 0;
    virtual void notifyModemWedged() = 0;
// configuration stuff
    virtual void readConfig(const fxStr& filename);
    virtual void resetConfig();
    virtual void vconfigError(const char* fmt, va_list ap);
    virtual void vconfigTrace(const char* fmt, va_list ap);
    virtual void vdialrulesTrace(const char* fmt, va_list ap);
    const fxStr& getConfigFile() const;
// call/receive session start+end for controlling logging
    void	beginSession(const fxStr& number);
    void	endSession();
// abort send/receive session
    virtual void abortSession();
    bool	abortRequested();
public:
    virtual ~ModemServer();

    virtual void open();
    virtual void close();
    virtual void initialize(int argc, char** argv);

    ClassModem*	getModem();
    const fxStr& getModemDevice() const;
    const fxStr& getModemDeviceID() const;
    const fxStr& getModemNumber() const;
    const fxStr& getCommID() const;

    int		getSessionTracing() const;
    int		getServerTracing() const;
    void	vtraceStatus(int kind, const char* fmt, va_list ap);

    bool	modemReady() const;
    bool	serverBusy() const;
};
inline ClassModem* ModemServer::getModem()		{ return modem; }
inline int ModemServer::getModemFd()			{ return modemFd; }
inline int ModemServer::getServerTracing() const
    { return (tracingLevel); }
inline int ModemServer::getSessionTracing() const
    { return (logTracingLevel); }
inline const fxStr& ModemServer::getModemDevice() const
    { return modemDevice; }
inline const fxStr& ModemServer::getModemDeviceID() const
    { return modemDevID; }
inline const fxStr& ModemServer::getModemNumber() const
    { return FAXNumber; }
inline const fxStr& ModemServer::getCommID() const	{ return commid; }
inline bool ModemServer::isModemInput() const
    { return rcvNext < rcvCC; }

#endif /* _ModemServer_ */
