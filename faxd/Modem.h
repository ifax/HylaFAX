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
#ifndef _Modem_
#define	_Modem_
/*
 * HylaFAX Queue Manager Abstract Modem.
 */
#include "Class2Params.h"
#include "Str.h"
#include "faxQueueApp.h"

typedef	unsigned int ModemState;

class UUCPLock;
class RegEx;
class RegExDict;
class fxStackBuffer;
class Modem;

class ModemGroup {
private:
    static RegExDict* classes;	// registered modem classes
public:
    static void reset();
    static void set(const fxStr& name, RegEx* re);
    static RegEx* find(const char* name);
};

/*
 * NB: This should be a private nested class but various
 *     C++ compilers cannot grok it.
 */
class ModemLockWaitHandler : public IOHandler {
private:
    Modem& modem;
public:
    ModemLockWaitHandler(Modem&);
    ~ModemLockWaitHandler();
    void timerExpired(long, long);
};

/*
 * Each modem server process that has identified itself has
 * an instance of this class.  It contains all the information
 * needed by the queuer to select and prepare outbound jobs.
 * Modem processes communicate with the queuer through FIFO
 * files to exchange server and job state changes and to export
 * modem/server capabilities to the queuer.
 */
class Modem : public QLink {
public:
    enum {
	DOWN  = 0,		// modem identified, but offline
	READY = 1,		// modem ready for use
	BUSY  = 2		// modem in use
    };
private:
    int		fd;		// cached open FIFO file
    fxStr	fifoName;	// modem FIFO filename
    fxStr	devID;		// modem device identifier
    fxStr	number;		// modem phone number
    fxStr	commid;		// communication identifier
    ModemState	state;		// modem state
    bool	canpoll;	// modem is capable of polling
    u_short	priority;	// modem priority
    Class2Params caps;		// modem capabilities
    UUCPLock*	lock;		// UUCP lockfile support
    QLink	triggers;	// waiting specifically on this modem
				// Dispatcher handler for lock wait thread
    ModemLockWaitHandler lockHandler;

    static QLink list;		// list of all modems

    void setCapabilities(const char*);	// specify modem capabilities
    void setNumber(const char*);	// specify modem phone number
    void setCommID(const char*);	// specify modem commid
    void setState(ModemState);		// specify modem state

    friend class faxQueueApp;
    friend class Trigger;		// for triggers
public:
    Modem(const fxStr& devid);
    virtual ~Modem();

    static Modem& getModemByID(const fxStr& id);
    static Modem* modemExists(const fxStr& id);
    static Modem* findModem(const Job& job, const DestControlInfo& dci);

    bool assign(Job&);		// assign modem
    void release();			// release modem

    void startLockPolling(long sec);	// initiate polling thread
    void stopLockPolling();		// terminate any active thread

    const fxStr& getDeviceID() const;	// return modem device ID
    const fxStr& getNumber() const;	// return modem phone number
    ModemState getState() const;	// return modem state
    const Class2Params& getCapabilities() const;
    u_int getPriority() const;		// return modem scheduling priority
    const fxStr& getCommID() const;	// return communication ID

    bool isCapable(const Job& job) const;
    bool supports2D() const;		// modem supports 2D-encoded fax
    bool supportsVRes(float) const;	// modem supports vertical resolution
    // modem support fax page width
    bool supportsPageWidthInMM(u_int) const;
    bool supportsPageWidthInPixels(u_int) const;
    // modem supports fax page length
    bool supportsPageLengthInMM(u_int) const;
    bool supportsPolling() const;	// modem supports fax polling

    // send message to modem FIFO
    bool send(const char* msg, u_int len, bool cacheFd = true);
    static void broadcast(const fxStr&);	// broadcast msg to all FIFOs

    void encode(fxStackBuffer&) const;	// encode for ModemExt
};
inline bool Modem::supportsPolling() const	{ return canpoll; }
inline const fxStr& Modem::getDeviceID() const	{ return devID; }
inline const fxStr& Modem::getNumber() const	{ return number; }
inline ModemState Modem::getState() const	{ return state; }
inline const Class2Params& Modem::getCapabilities() const { return caps; }
inline u_int Modem::getPriority() const		{ return priority; }
inline const fxStr& Modem::getCommID() const	{ return commid; }

/*
 * Modem iterator class; for iterating
 * over the set of known modems.
 */
class ModemIter {
private:
    const QLink* head;
    QLink*	ql;
public:
    ModemIter(QLink& q)		{ head = &q; ql = q.next; }
    ~ModemIter() {}

    void operator=(QLink& q)	{ head = &q; ql = q.next; }
    void operator++()		{ ql = ql->next; }
    void operator++(int)	{ ql = ql->next; }
    operator Modem&() const	{ return *(Modem*)ql; }
    operator Modem*() const	{ return (Modem*) ql; }
    Modem& modem() const	{ return *(Modem*)ql; }
    bool notDone()		{ return ql != head; }
};
#endif /* _Modem_ */
