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
#ifndef _ServerConfig_
#define	_ServerConfig_
/*
 * Fax Modem and Protocol Server Configuration.
 */
#include "ModemConfig.h"

class DialStringRules;
class UUCPLock;
class REArray;
class fxBoolArray;

class ServerConfig : public ModemConfig {
public:
    // NB: the S_ prefixes workaround a bug in the AIX xlC compiler
    struct S_stringtag {
	const char*	 name;
	fxStr ServerConfig::* p;
	const char*	 def;		// NULL is shorthand for ""
    };
    struct S_numbertag {
	const char*	 name;
	u_int ServerConfig::*p;
	u_int		 def;
    };
    struct S_filemodetag {
	const char*	 name;
	mode_t ServerConfig::*p;
	// NB: this should be mode_t but causes alignment problems
	//     on systems where it's 16-bits (e.g. m68k-hp-hpux9).
	u_int		 def;
    };
private:
    fxStr	longDistancePrefix;	// prefix str for long distance dialing
    fxStr	internationalPrefix;	// prefix str for international dialing
    fxStr	areaCode;		// local area code
    fxStr	countryCode;		// local country code
    DialStringRules* dialRules;		// dial string rules
    fxStr	uucpLockType;		// UUCP lock file type
    fxStr	uucpLockDir;		// UUCP lock file directory
    mode_t	uucpLockMode;		// UUCP lock file creation mode
    u_int	uucpLockTimeout;	// UUCP stale lock file timeout
    time_t	lastTSIModTime;		// last mod time of TSI patterns file
    time_t	lastPWDModTime;		// last mod time of PWD patterns file
    REArray*	tsiPats;		// recv tsi patterns
    REArray*	pwdPats;		// recv PWD patterns
    fxBoolArray* acceptTSI;		// accept/reject matched tsi
    fxBoolArray* acceptPWD;		// accept/reject matched PWD
    fxStr	logFacility;		// syslog facility to direct trace msgs

    static S_stringtag strings[];
    static S_numbertag numbers[];
    static S_filemodetag filemodes[];

    SpeakerVolume getVolume(const char* cp);
protected:
    ServerConfig();

    void	setupConfig();
    virtual bool setConfigItem(const char* tag, const char* value);

// pattern access control list support
    void	updatePatterns(const fxStr& file,
		    REArray*& pats, fxBoolArray*& accept,
		    time_t& lastModTime);
    void	readPatterns(FILE*, REArray*&, fxBoolArray*&);
    bool	checkACL(const fxStr& id, REArray*, fxBoolArray&);
// methods for parameters whose assignment may have side effects
    virtual void setDialRules(const char* name);
    virtual void setLocalIdentifier(const fxStr& lid);
    virtual void setModemSpeakerVolume(SpeakerVolume level);
public:
    SpeakerVolume speakerVolume;	// volume control
    fxStr	qualifyTSI;		// if set, no recv w/o acceptable tsi
    fxStr	qualifyPWD;		// if set, no recv w/o acceptable PWD
    u_int	noCarrierRetrys;	// # times to retry on no carrier
    mode_t	recvFileMode;		// protection mode for received files
    mode_t	deviceMode;		// protection mode for modem device
    mode_t	logMode;		// protection mode for log files
    u_int	tracingLevel;		// tracing level w/o session
    u_int	logTracingLevel;	// tracing level during session
    u_int	tracingMask;		// tracing level control mask
    bool	clocalAsRoot;		// set CLOCAL as root
    bool	priorityScheduling;	// change process priority
    u_int	requeueTTS[9];		// requeue intervals[CallStatus code]
    u_int	requeueProto;		// requeue interval after protocol error
    u_int	requeueOther;		// requeue interval after other problem
    u_int	pollModemWait;		// polling interval in modem wait state
    u_int	pollLockWait;		// polling interval in lock wait state
    u_int	maxRecvPages;		// max pages to accept on receive
    u_int	maxConsecutiveBadCalls;	// max consecutive bad phone calls
    fxStr	localIdentifier;	// to use in place of FAXNumber
    fxStr	FAXNumber;		// phone number
    u_int	maxSetupAttempts;	// # times to try initializing modem

    virtual ~ServerConfig();

    virtual void resetConfig();

    fxStr	canonicalizePhoneNumber(const fxStr& ds);
    fxStr	prepareDialString(const fxStr& ds);

    UUCPLock*	getUUCPLock(const fxStr& deviceName);

    bool	isTSIOk(const fxStr& tsi);
    bool	isPWDOk(const fxStr& pwd);

    virtual void vconfigError(const char* fmt, va_list ap) = 0;
    virtual void vconfigTrace(const char* fmt, va_list ap) = 0;
    virtual void vdialrulesTrace(const char* fmt, va_list ap) = 0;
    void configError(const char* fmt, ...);
    void configTrace(const char* fmt, ...);
};
#endif /* _ServerConfig_ */
