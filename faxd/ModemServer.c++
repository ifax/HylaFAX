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
#include "Sys.h"

#include <ctype.h>
#include <termios.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/time.h>
#if HAS_MODEM_H
#include <sys/modem.h>
#else
#include <sys/ioctl.h>
#endif
#if HAS_TERMIOX
#include <sys/termiox.h>
#endif
#include <sys/file.h>

#include "Dispatcher.h"
#include "FaxTrace.h"
#include "FaxMachineLog.h"
#include "ModemServer.h"
#include "UUCPLock.h"
#include "Class0.h"

#include "config.h"

#ifndef O_NOCTTY
#define	O_NOCTTY	0		// no POSIX O_NOCTTY support
#endif

/*
 * HylaFAX Modem Server.
 */

// map ClassModem::BaudRate to numeric value
static const u_int baudRates[] = {
    0,		// BR0
    300,	// BR300
    1200,	// BR1200
    2400,	// BR2400
    4800,	// BR4800
    9600,	// BR9600
    19200,	// BR19200
    38400,	// BR38400
    57600,	// BR57600
    76800,	// BR76800
    115200,	// BR115200
};

ModemServer::ModemServer(const fxStr& devName, const fxStr& devID)
    : modemDevice(devName)
    , modemDevID(devID)
    , configFile(fxStr(FAX_CONFIG) | "." | devID)
{
    state = BASE;
    statusFile = NULL;
    abortCall = false;
    deduceComplain = true;		// first failure causes complaint
    changePriority = true;
    delayConfig = false;
    inputBuffered = true;		// OSes buffer by default

    modemFd = -1;
    modem = NULL;
    curRate = ClassModem::BR0;		// unspecified baud rate
    curParity = NONE;			// default is 8-bit/no parity
    curVMin = 127;			// buffer input by default
    curVTime = 1;			// ditto
    setupAttempts = 0;

    rcvCC = rcvNext = rcvBit = gotByte = 0;
    sawBlockEnd = false;
    timeout = false;
    log = NULL;
}

ModemServer::~ModemServer()
{
    delete log;
    delete modem;
    if (statusFile)
	fclose(statusFile);
}

#include "faxApp.h"
/*
 * Initialize the server from command line arguments.
 */
void
ModemServer::initialize(int argc, char** argv)
{
    for (GetoptIter iter(argc, argv, faxApp::getOpts()); iter.notDone(); iter++)
	switch (iter.option()) {
	case 'p':
	    changePriority = false;
	    break;
	case 'x':
	    tracingMask &= ~(FAXTRACE_MODEMIO|FAXTRACE_TIMEOUTS);
	    break;
	}
    TIFFSetErrorHandler(NULL);
    TIFFSetWarningHandler(NULL);
    // setup server's status file
    statusFile = Sys::fopen(FAX_STATUSDIR "/" | modemDevID, "w");
    if (statusFile != NULL) {
#if HAS_FCHMOD
	fchmod(fileno(statusFile), 0644);
#else
	Sys::chmod(FAX_STATUSDIR "/" | modemDevID, 0644);
#endif
	setServerStatus("Initializing server");
    }
    umask(077);				// keep all temp files private

    updateConfig(configFile);		// read config file
}

/*
 * Startup the server for the first time.
 */
void
ModemServer::open()
{
    if (lockModem()) {
	bool modemReady = setupModem();
	unlockModem();
	if (!modemReady)
	    changeState(MODEMWAIT, pollModemWait);
	else
	    changeState(RUNNING, pollLockWait);
    } else {
	traceServer("%s: Can not lock device.", (const char*) modemDevice);
	changeState(LOCKWAIT, pollLockWait);
    }
}

/*
 * Close down the server.
 */
void
ModemServer::close()
{
    if (lockModem()) {
	if (modem)
	    modem->hangup();
	discardModem(true);
	unlockModem();
    }
}

const char* ModemServer::stateNames[9] = {
    "BASE",
    "RUNNING",
    "MODEMWAIT",
    "LOCKWAIT",
    "GETTYWAIT",
    "SENDING",
    "ANSWERING",
    "RECEIVING",
    "LISTENING"
};
const char* ModemServer::stateStatus[9] = {
    "Initializing server and modem",		// BASE
    "Running and idle",				// RUNNING
    "Waiting for modem to come ready",		// MODEMWAIT
    "Waiting for modem to come free",		// LOCKWAIT
    "Waiting for login session to terminate",	// GETTYWAIT
    "Sending facsimile",			// SENDING
    "Answering the phone",			// ANSWERING
    "Receiving facsimile",			// RECEIVING
    "Listening to rings from modem",		// LISTENING
};

/*
 * Change the server's state and, optionally,
 * start a timer running for timeout seconds.
 */
void
ModemServer::changeState(ModemServerState s, long timeout)
{
    if (s != state) {
	if (timeout)
	    traceStatus(FAXTRACE_STATETRANS,
		"STATE CHANGE: %s -> %s (timeout %ld)",
		stateNames[state], stateNames[s], timeout);
	else
	    traceStatus(FAXTRACE_STATETRANS, "STATE CHANGE: %s -> %s",
		stateNames[state], stateNames[s]);
	state = s;
	if (changePriority)
	    setProcessPriority(state);
	if (modemFd >= 0)
	    setInputBuffering(state != RUNNING && state != SENDING &&
		state != ANSWERING && state != RECEIVING && state != LISTENING);
	setServerStatus(stateStatus[state]);
	switch (state) {
	case RUNNING:
	    notifyModemReady();			// notify surrogate
	    break;
	case MODEMWAIT:
	    setupAttempts = 0;
	    break;
	default:
	    break;
	}
    } else if (s == MODEMWAIT && ++setupAttempts >= maxSetupAttempts) {
	traceStatus(FAXTRACE_SERVER,
	    "Unable to setup modem on %s; giving up after %d attempts",
	    (const char*) modemDevice, setupAttempts);
	notifyModemWedged();
    }
    /*
     * Before we start any timer, make sure we stop the current one
     */
    Dispatcher::instance().stopTimer(this);

    if (timeout)
	Dispatcher::instance().startTimer(timeout, 0, this);
}

#if HAS_SCHEDCTL
#include <sys/schedctl.h>
/*
 * When low latency is required, use a nondegrading process
 * priority; otherwise just remove any nondegrading priority.
 * Note that we assign a high nondegrading priority when sending,
 * answering the telephone, or receiving.  We assume that if the
 * incoming call spawns a getty process that the priority will
 * be reset in the child before the getty is exec'd.
 */
static const int schedCtlParams[9][2] = {
    { NDPRI, 0 },		// BASE
    { NDPRI, 0 },		// RUNNING
    { NDPRI, 0 },		// MODEMWAIT
    { NDPRI, 0 },		// LOCKWAIT
    { NDPRI, 0 },		// GETTYWAIT
    { NDPRI, NDPHIMIN },	// SENDING
    { NDPRI, NDPHIMIN },	// ANSWERING
    { NDPRI, NDPHIMIN },	// RECEIVING
    { NDPRI, 0 },		// LISTENING
};
#elif HAS_PRIOCNTL
extern "C" {
#include <sys/priocntl.h>
#ifdef HAS_FPPRIOCNTL
#include <sys/fppriocntl.h>
#else
#include <sys/rtpriocntl.h>
#endif
#include <sys/tspriocntl.h>
}
static struct SchedInfo {
    const char*	clname;		// scheduling class name
    int		params[3];	// scheduling class parameters
} schedInfo[9] = {
    { "TS", { TS_NOCHANGE, TS_NOCHANGE } },		// BASE
    { "TS", { TS_NOCHANGE, TS_NOCHANGE } },		// RUNNING
    { "TS", { TS_NOCHANGE, TS_NOCHANGE } },		// MODEMWAIT
    { "TS", { TS_NOCHANGE, TS_NOCHANGE } },		// LOCKWAIT
    { "TS", { TS_NOCHANGE, TS_NOCHANGE } },		// GETTYWAIT
#ifdef HAS_FPPRIOCNTL	// if still fails, set prio to 0
    { "FP", { FP_NOCHANGE, FP_NOCHANGE, FP_TQDEF } },// SENDING
    { "FP", { FP_NOCHANGE, FP_NOCHANGE, FP_TQDEF } },// ANSWERING
    { "FP", { FP_NOCHANGE, FP_NOCHANGE, FP_TQDEF } },// RECEIVING
#else
    { "RT", { RT_NOCHANGE, RT_NOCHANGE, RT_NOCHANGE } },// SENDING
    { "RT", { RT_NOCHANGE, RT_NOCHANGE, RT_NOCHANGE } },// ANSWERING
    { "RT", { RT_NOCHANGE, RT_NOCHANGE, RT_NOCHANGE } },// RECEIVING
#endif
    { "TS", { TS_NOCHANGE, TS_NOCHANGE } },		// LISTENING
};
#elif HAS_RTPRIO
/*
 * On HP-UX, a real-time priority is between 0 (high)
 * and 127 (low);  for now we use 120.
 */
#include <sys/rtprio.h>
#ifndef RTPRIO_HIGH
#define RTPRIO_HIGH	120
#endif

static const int rtprioParams[9] = {
    RTPRIO_RTOFF,		// BASE
    RTPRIO_RTOFF,		// RUNNING
    RTPRIO_RTOFF,		// MODEMWAIT
    RTPRIO_RTOFF,		// LOCKWAIT
    RTPRIO_RTOFF,		// GETTYWAIT
    RTPRIO_HIGH,		// SENDING
    RTPRIO_HIGH,		// ANSWERING
    RTPRIO_HIGH,		// RECEIVING
    RTPRIO_RTOFF,		// LISTENING
};
#elif HAS_POSIXSCHED
/*
 * In POSIX (i.e. Linux), a real time priority is
 * between 1 (low) and 99 (high);
 * for now we conservatively use 1.  Priority of 0 
 * is without real-time application.
 */
#include <sched.h>
static const struct SchedInfo {
    int		policy;
    int		priority;
} sched_setschedulerParams[9] = {
    {SCHED_OTHER,	0},	// BASE
    {SCHED_OTHER,	0},	// RUNNING
    {SCHED_OTHER,	0},	// MODEMWAIT
    {SCHED_OTHER,	0},	// LOCKWAIT
    {SCHED_OTHER,	0},	// GETTYWAIT
    {SCHED_FIFO,	RT_PRIORITY},	// SENDING
    {SCHED_FIFO,	RT_PRIORITY},	// ANSWERING
    {SCHED_FIFO,	RT_PRIORITY},	// RECEIVING
    {SCHED_OTHER,	0},	// LISTENING
};

#endif

void
ModemServer::setProcessPriority(ModemServerState s)
{
    if (priorityScheduling) {
#if HAS_SCHEDCTL
    uid_t euid = geteuid();
    if (seteuid(0) >= 0) {		// must be done as root
	if (schedctl(schedCtlParams[s][0], 0, schedCtlParams[s][1]) < 0)
	    traceServer("schedctl: %m");
	if (seteuid(euid) < 0)		// restore previous effective uid
	    traceServer("seteuid(%d): %m", euid);
    } else
	traceServer("seteuid(root): %m");
#elif HAS_PRIOCNTL
    uid_t euid = geteuid();
    if (seteuid(0) >= 0) {		// must be done as root
	const SchedInfo& si = schedInfo[s];
	pcinfo_t pcinfo;
	strcpy(pcinfo.pc_clname, si.clname);
	if (priocntl((idtype_t)0, 0, PC_GETCID, (caddr_t)&pcinfo) >= 0) {
	    pcparms_t pcparms;
	    pcparms.pc_cid = pcinfo.pc_cid;
#ifdef HAS_FPPRIOCNTL
	    if (streq(si.clname, "FP")) {
		fpparms_t* fpp = (fpparms_t*) pcparms.pc_clparms;
		fpp->fp_pri	= si.params[0];
		fpp->fp_tqsecs	= (ulong) si.params[1];
		fpp->fp_tqnsecs	= si.params[2];
#else
	    if (streq(si.clname, "RT")) {
		rtparms_t* rtp = (rtparms_t*) pcparms.pc_clparms;
		rtp->rt_pri	= si.params[0];
		rtp->rt_tqsecs	= (ulong) si.params[1];
		rtp->rt_tqnsecs	= si.params[2];
#endif
	    } else {
		tsparms_t* tsp = (tsparms_t*) pcparms.pc_clparms;
		tsp->ts_uprilim	= si.params[0];
		tsp->ts_upri	= si.params[1];
	    }
	    if (priocntl(P_PID, P_MYID, PC_SETPARMS, (caddr_t)&pcparms) < 0)
		traceServer("Unable to set %s scheduling parameters: %m",
		    si.clname);
	} else
	    traceServer("priocntl(%s): %m", si.clname);
	if (seteuid(euid) < 0)		// restore previous effective uid
	    traceServer("setreuid(%d): %m", euid);
    } else
	traceServer("setreuid(root): %m");
#elif HAS_RTPRIO
    uid_t euid = geteuid();
    if (seteuid(0) >= 0) {		// must be done as root
        if(rtprio((pid_t) 0, rtprioParams[s]) < 0)
           traceServer("rtprio: %m");
	if (seteuid(euid) < 0)		// restore previous effective uid
	    traceServer("seteuid(%d): %m", euid);
    } else
	traceServer("seteuid(root): %m");
#elif HAS_POSIXSCHED
    uid_t euid = geteuid();
    if (seteuid(0) >= 0) {		// must be done as root
	struct sched_param sp;
	sp.sched_priority = sched_setschedulerParams[s].priority;
	if (sched_setscheduler(0, sched_setschedulerParams[s].policy, &sp))
	    traceServer("sched_setscheduler: %m");
	if (sched_getparam(0, &sp))
	    traceServer("sched_getparam: %m");
	traceServer("sched policy=%d, priority=%d", sched_getscheduler(0), sp.sched_priority);
	if (seteuid(euid) < 0)		// restore previous effective uid
	    traceServer("seteuid(%d): %m", euid);
    } else
	traceServer("seteuid(root): %m");
#endif
    }
}

/*
 * Record the server status in the status file.
 */
void
ModemServer::setServerStatus(const char* fmt, ...)
{
    if (statusFile == NULL)
	return;
    flock(fileno(statusFile), LOCK_EX);
    rewind(statusFile);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(statusFile, fmt, ap);
    va_end(ap);
    fprintf(statusFile, "\n");
    fflush(statusFile);
    (void) ftruncate(fileno(statusFile), ftell(statusFile));
    flock(fileno(statusFile), LOCK_UN);
}

void
ModemServer::resetConfig()
{
    if (modem)
	discardModem(true);
    ServerConfig::resetConfig();
}

const fxStr& ModemServer::getConfigFile() const { return configFile; }

/*
 * Setup the modem; if needed.
 */
bool
ModemServer::setupModem(bool isSend)
{
    if (!modem) {
	const char* dev = modemDevice;
	if (!openDevice(dev))
	    return (false);
	/*
	 * Deduce modem type and setup configuration info.
	 * The deduceComplain cruft is just to reduce the
	 * noise in the log file when probing for a modem.
	 */
	modem = deduceModem(isSend);
	if (!modem) {
	    discardModem(true);
	    if (deduceComplain) {
		traceServer("%s: Can not initialize modem.", dev);
		deduceComplain = false;
	    }
	    return (false);
	} else {
	    deduceComplain = true;
	    traceServer("MODEM "
		| modem->getManufacturer() | " "
		| modem->getModel() | "/"
		| modem->getRevision());
	}
    } else {
	/*
	 * Reset the modem in case some other program
	 * went in and messed with the configuration.
	 *
	 * Sometimes a modem may get interrupted while in a
	 * "transmit" state such as AT+VTX (voice mode) or
	 * AT+FTM=146 (fax mode) or similar.  Now, the modem
	 * should be smart enough to return to command-mode
	 * after a short period of inactivity, but it's
	 * conceivable that some don't (and we've seen some
	 * that are this way).  Furthermore, it is likely
	 * possible to configure a modem in such a way so as
	 * to never provide for that short period of inactivity.
	 * Further complicating matters, some modems are not
	 * sensitive to DTR.
	 *
	 * So if our first reset attempt fails we send DLE+ETX 
	 * to the modem just in case we happen to have a modem 
	 * in this kind of state, waiting for DLE+ETX before 
	 * returning to command mode.  Then we retry the reset.
	 */
        if (!(modem->reset())) {
	    sendDLEETX();
	    if (!(modem->reset()))
		return (false);
	}
    }
    /*
     * Most modem-related parameters are dealt with
     * in the modem driver.  The speaker volume is
     * kept in the fax server because it often gets
     * changed on the fly.  The phone number has no
     * business in the modem class.
     */
    modem->setSpeakerVolume(speakerVolume);
    return (true);
}

/*
 * Ready the modem; if needed.
 */
bool
ModemServer::readyModem()
{
    return (modem->ready());
}

/*
 * Deduce the type of modem supplied to the server
 * and return an instance of the appropriate modem
 * driver class.
 */
ClassModem*
ModemServer::deduceModem(bool isSend)
{
    ClassModem* modem = new Class0Modem(*this, *this);
    if (modem) {
	if (modem->setupModem(isSend))
	    return modem;
	delete modem;
    }
    return (NULL);
}

fxStr
ModemServer::getModemCapabilities() const
{
    return modem->getCapabilities();
}

/*
 * Open the tty device associated with the modem
 * and change the device file to be owned by us
 * and with a protected file mode.
 */
bool
ModemServer::openDevice(const char* dev)
{
    /*
     * Temporarily become root to open the device.
     * Routines that call setupModem *must* first
     * lock the device with the usual effective uid.
     */
    uid_t euid = geteuid();
    if (seteuid(0) < 0) {
	 traceServer("%s: seteuid root failed (%m)", dev);
	 return (false);
    }
#ifdef O_NDELAY
    /*
     * Open device w/ O_NDELAY to bypass modem
     * control signals, then turn off the flag bit.
     */
    modemFd = Sys::open(dev, O_RDWR|O_NDELAY|O_NOCTTY);
    if (modemFd < 0) {
	seteuid(euid);
	traceServer("%s: Can not open modem (%m)", dev);
	return (false);
    }

    /*
     * Wait a second for "slower" modems
     * such as the Nokia 6210 mobile.
     */
    (void) sleep(1);

    int flags = fcntl(modemFd, F_GETFL, 0);
    if (fcntl(modemFd, F_SETFL, flags &~ O_NDELAY) < 0) {
	 traceServer("%s: fcntl: %m", dev);
	 Sys::close(modemFd), modemFd = -1;
	 return (false);
    }
#else
    startTimeout(3*1000);
    modemFd = Sys::open(dev, O_RDWR);
    stopTimeout("opening modem");
    if (modemFd < 0) {
	seteuid(euid);
	traceServer((timeout ?
		"%s: Can not open modem (timed out)." :
		"%s: Can not open modem (%m)."),
	    dev, errno);
	return (false);
    }
#endif
    /*
     * NB: we stat and use the gid because passing -1
     *     through the gid_t parameter in the prototype
     *	   causes it to get truncated to 65535.
     */
    struct stat sb;
    (void) Sys::fstat(modemFd, sb);
#if HAS_FCHOWN
    if (fchown(modemFd, UUCPLock::getUUCPUid(), sb.st_gid) < 0)
#else
    if (Sys::chown(dev, UUCPLock::getUUCPUid(), sb.st_gid) < 0)
#endif
	traceServer("%s: chown: %m", dev);
#if HAS_FCHMOD
    if (fchmod(modemFd, deviceMode) < 0)
#else
    if (Sys::chmod(dev, deviceMode) < 0)
#endif
	traceServer("%s: chmod: %m", dev);
    seteuid(euid);
    return (true);
}

bool
ModemServer::reopenDevice()
{
    if (modemFd >= 0)
	Sys::close(modemFd), modemFd = -1;
    return openDevice(modemDevice);
}

/*
 * Discard any handle on the modem.
 */
void
ModemServer::discardModem(bool dropDTR)
{
    if (modemFd >= 0) {
	if (dropDTR)
	    (void) setDTR(false);			// force hangup
	Sys::close(modemFd), modemFd = -1;		// discard open file
#ifdef sco5
	// do it again so DTR is really off (SCO Open Server 5 wierdness)
	modemFd = Sys::open(modemDevice, O_RDWR|O_NDELAY|O_NOCTTY);
	Sys::close(modemFd), modemFd = -1;
#endif
    }
    delete modem, modem = NULL;
}

/*
 * Start a session: a period of time during which
 * carrier is raised and a peer is engaged.
 */
void
ModemServer::beginSession(const fxStr& number)
{
    /*
     * Obtain the next communication identifier by reading
     * and updating the sequence number file.  If a problem
     * occurs then session logging will not be done.
     */
    fxStr emsg;
    u_long seqnum = Sequence::getNext(FAX_LOGDIR "/" FAX_SEQF, emsg);

    if (seqnum == (u_long)-1)
    {
	logError("Couldn't get next seqnum for session log: %s",
		 (const char*) emsg);
	return;
    }
    commid = fxStr::format(Sequence::format, seqnum);
    fxStr file = FAX_LOGDIR "/c" | commid;

    mode_t omask = umask(022);
    int ftmp = Sys::open(file, O_RDWR|O_CREAT|O_EXCL, logMode);
    umask(omask);

    if (ftmp < 0)
    {
	logError("Failed to open free sessionlog (seqnum=%u)", seqnum);
    } else
    {
	log =
	    new FaxMachineLog(ftmp, canonicalizePhoneNumber(number), commid);
    }
}

/*
 * Terminate a session.
 */
void
ModemServer::endSession()
{
    delete log, log = NULL;
}

/*
 * Return true if a request has been made to abort
 * the current session.  This is true if a previous
 * abort request was made or if an external abort
 * message is dispatched during our processing.
 */
bool
ModemServer::abortRequested()
{
#ifndef SERVERABORTBUG
    if (!abortCall) {
	// poll for input so abort commands get processed
	long sec = 0;
	long usec = 0;
	while (Dispatcher::instance().dispatch(sec,usec) && !abortCall)
	    ;
    }
#endif
    return (abortCall);
}

/*
 * Request that a current session be aborted.
 */
void
ModemServer::abortSession()
{
    abortCall = true;
    traceServer("ABORT: job abort requested");
}

/*
 * Dispatcher timer expired routine.  Perform the action
 * associated with the server's state and, possible, transition
 * to a new state.
 */
void
ModemServer::timerExpired(long, long)
{
    switch (state) {
    case RUNNING:
	/*
	 * Poll the lock file, see if it's lockable.
	 * If it's lockable, then no lock file exists.  Rinse. Repeat.
	 * If a lockfile exists, go to LOCKWAIT
	 */
	if (canLockModem()) {
	    bool ok = true;
	    if (pollLockPokeModem) {
		/*
		 * Poke the modem to make sure it's still there.
		 * If not, then mark it to be reset.
		 */
		lockModem();
		ok = modem->poke();
		unlockModem();
	    }
	    if (ok)
		Dispatcher::instance().startTimer(pollLockWait, 0, this);
	    else
		changeState(MODEMWAIT, pollModemWait);
	} else {
	    changeState(LOCKWAIT, pollLockWait);
	}
	break;
    case MODEMWAIT:
    case LOCKWAIT:
	/*
	 * Waiting for modem to start working.  Retry setup
	 * and either change state or restart the timer.
	 * Note that we unlock the modem before we change
	 * our state to RUNNING after a modem setup so that
	 * any callback doesn't find the modem locked (and
	 * so cause jobs to be requeued).
	 */
	if (lockModem()) {
	    bool modemReady = setupModem();
	    unlockModem();
	    if (modemReady)
		changeState(RUNNING, pollLockWait);
	    else
		changeState(MODEMWAIT, pollModemWait);
	} else
	    changeState(LOCKWAIT, pollLockWait);
	break;
    default:
	traceServer("ModemServer::timerExpired() in an unexpected "
	    "state %d", state);
	break;
    }
}

/*
 * Modem support interfaces.  Note that the values
 * returned when we don't have a handle on the modem
 * are selected so that any imaged facsimile should
 * still be sendable.
 */
bool ModemServer::modemReady() const
    { return modem != NULL; }

bool ModemServer::serverBusy() const
    { return state != RUNNING; }

bool ModemServer::modemWaitForRings(u_short rings, CallType& type, CallID& callid)
    { return modem->waitForRings(rings, type, callid); }
CallType ModemServer::modemAnswerCall(AnswerType atype, Status& eresult, const char* dialnumber)
    { return modem->answerCall(atype, eresult, dialnumber); }
void ModemServer::modemAnswerCallCmd(CallType ctype)
    { modem->answerCallCmd(ctype); }
void ModemServer::modemHangup()			{ modem->hangup(); }

BaudRate ModemServer::getModemRate() const	{ return baudRates[curRate]; }

/*
 * Server configuration support.
 */

/*
 * Read a configuration file.  Note that we suppress
 * dial string rules setup while reading so that the
 * order of related parameters is not important.  We
 * also setup the local identifier from the fax number
 * if nothing is specified in the config file (for
 * backwards compatibility).
 */
void
ModemServer::readConfig(const fxStr& filename)
{
    dialRulesFile = "";
    delayConfig = true;
    ServerConfig::readConfig(filename);
    delayConfig = false;
    if (dialRulesFile != "")
	setDialRules(dialRulesFile);
    if (localIdentifier == "")
	setLocalIdentifier(canonicalizePhoneNumber(FAXNumber));
}

void ModemServer::vconfigError(const char* fmt, va_list ap)
    { vtraceStatus(FAXTRACE_SERVER, fmt, ap); }
void ModemServer::vconfigTrace(const char* fmt, va_list ap)
    { vtraceStatus(FAXTRACE_CONFIG, fmt, ap); }
void ModemServer::vdialrulesTrace(const char* fmt, va_list ap)
    { vtraceStatus(FAXTRACE_DIALRULES, fmt, ap); }

/*
 * Setup the dial string rules.  Note that if we're
 * reading the configuration file (as opposed to
 * reconfiguring based on a FIFO message), then we
 * suppress the actual setup so that other parameters
 * such as the area code can be specified out of
 * order in the configuration file.
 */
void
ModemServer::setDialRules(const char* name)
{
    if (delayConfig)				// delay during config setup
	dialRulesFile = name;
    else
	ServerConfig::setDialRules(name);
}

/*
 * Set the modem speaker volume and if a modem
 * is setup, pass it into the modem driver to
 * pass to the modem.
 */
void
ModemServer::setModemSpeakerVolume(SpeakerVolume level)
{
    ServerConfig::setModemSpeakerVolume(level);
    if (modem)
	modem->setSpeakerVolume(level);
}

/*
 * Tracing support.
 */

void
ModemServer::traceServer(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vtraceStatus(FAXTRACE_SERVER, fmt, ap);
    va_end(ap);
}

void
ModemServer::traceProtocol(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vtraceStatus(FAXTRACE_PROTOCOL, fmt, ap);
    va_end(ap);
}

void
ModemServer::traceModemOp(const char* fmt0 ...)
{
    va_list ap;
    va_start(ap, fmt0);
    fxStr fmt = fxStr::format("MODEM %s", fmt0);
    vtraceStatus(FAXTRACE_MODEMOPS, fmt, ap);
    va_end(ap);
}

void
ModemServer::traceStatus(int kind, const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vtraceStatus(kind, fmt, ap);
    va_end(ap);
}

extern void vlogInfo(const char* fmt, va_list ap);

void
ModemServer::vtraceStatus(int kind, const char* fmt, va_list ap)
{
    if (log) {
        fxStr s = fxStr::vformat(fmt, ap);
        if (kind == FAXTRACE_SERVER) { // always log server stuff
            logInfo("%s", (const char*)s);
        }
        if (logTracingLevel & kind) {
	        log->log("%s", (const char*)s);
        }
    } else if (tracingLevel & kind) {
	    logInfo("%s", (const char*)fxStr::vformat(fmt, ap));
    }
}

#include "StackBuffer.h"

void
ModemServer::traceModemIO(const char* dir, const u_char* data, u_int cc)
{
    if (log) {
	if ((logTracingLevel& FAXTRACE_MODEMIO) == 0)
	    return;
    } else if ((tracingLevel & FAXTRACE_MODEMIO) == 0)
	return;

    const char* hexdigits = "0123456789ABCDEF";
    fxStackBuffer buf;
    for (u_int i = 0; i < cc; i++) {
	u_char b = data[i];
	if (i > 0)
	    buf.put(' ');
	buf.put(hexdigits[b>>4]);
	buf.put(hexdigits[b&0xf]);
    }
    traceStatus(FAXTRACE_MODEMIO, "%s <%u:%.*s>",
	dir, cc, buf.getLength(), (const char*) buf);
}

/*
 * Device manipulation.
 */

#ifndef B38400
#define	B38400	B19200
#endif
#ifndef B57600
#define	B57600	B38400
#endif
#ifndef B76800
#define	B76800	B57600
#endif
#ifndef B115200
#define	B115200	B76800
#endif
static speed_t termioBaud[] = {
    B0,		// BR0
    B300,	// BR300
    B1200,	// BR1200
    B2400,	// BR2400
    B4800,	// BR4800
    B9600,	// BR9600
    B19200,	// BR19200
    B38400,	// BR38400
    B57600,	// BR57600
    B76800,	// BR76800
    B115200,	// BR115200
};
#define	NBAUDS	(sizeof (termioBaud) / sizeof (termioBaud[0]))
static const char* flowNames[] = { "NONE", "XON/XOFF", "RTS/CTS", };
static const char* parityNames[] = {
    "8 bits, no parity",	// NONE
    "7 bits, even parity",	// EVEN
    "7 bits, odd parity",	// ODD
};

#if defined(CCTS_OFLOW) && defined(CRTS_IFLOW) && !defined(__NetBSD__) && !defined(__OpenBSD__)
#undef CRTSCTS				/* BSDi */
#define	CRTSCTS	(CCTS_OFLOW|CRTS_IFLOW)
#endif
#if defined(CTSFLOW) && defined(RTSFLOW)
#define	CRTSCTS	(CTSFLOW|RTSFLOW)	/* SCO */
#endif
#if defined(CNEW_RTSCTS)
#define	CRTSCTS	CNEW_RTSCTS		/* IRIX 5.x */
#endif
#ifndef CRTSCTS
#define	CRTSCTS	0
#endif

void
ModemServer::setFlow(termios& term, FlowControl iflow, FlowControl oflow)
{
    switch (iflow) {
    case ClassModem::FLOW_NONE:
	term.c_iflag &= ~IXON;
	term.c_cflag &= ~CRTSCTS;
	break;
    case ClassModem::FLOW_XONXOFF:
	term.c_iflag |= IXON;
	term.c_cflag &= ~CRTSCTS;
	break;
    case ClassModem::FLOW_RTSCTS:
	term.c_iflag &= ~IXON;
	term.c_cflag |= CRTSCTS;
	break;
    }
    switch (oflow) {
    case ClassModem::FLOW_NONE:
	term.c_iflag &= ~IXOFF;
	term.c_cflag &= ~CRTSCTS;
	break;
    case ClassModem::FLOW_XONXOFF:
	term.c_iflag |= IXOFF;
	term.c_cflag &= ~CRTSCTS;
	break;
    case ClassModem::FLOW_RTSCTS:
	term.c_iflag &= ~IXOFF;
	term.c_cflag |= CRTSCTS;
	break;
    }
}

void
ModemServer::setParity(termios& term, Parity parity)
{
    switch (parity) {
    case NONE: 
	term.c_cflag &= ~(CSIZE | PARENB);
	term.c_cflag |= CS8;
	term.c_iflag &= ~(IGNPAR | ISTRIP);
	break;
    case EVEN:
	term.c_cflag &= ~(CSIZE | PARODD);
	term.c_cflag |= CS7 | PARENB;
	term.c_iflag |= IGNPAR | ISTRIP;
	break;
    case ODD:
	term.c_cflag &= ~CSIZE;
	term.c_cflag |= CS7 | PARENB | PARODD;
	term.c_iflag |= IGNPAR | ISTRIP;
	break;
    }
}

bool
ModemServer::tcsetattr(int op, struct termios& term)
{
    bool ok;
    if (clocalAsRoot) {
	/*
	 * Gag, IRIX 5.2 and beyond permit only the super-user to
	 * change the CLOCAL bit on a tty device with modem control.
	 * However, since some versions have a bug in the UART driver
	 * that causes RTS/CTS flow control to be silently turned off
	 * when CLOCAL is set, for now we enable this from the
	 * configuration file so that IRIX users can still disable it.
	 */
	uid_t euid = geteuid();
	(void) seteuid(0);
	ok = (::tcsetattr(modemFd, op, &term) == 0);
	seteuid(euid);
    } else
	ok = (::tcsetattr(modemFd, op, &term) == 0);
    if (!ok)
	traceModemOp("tcsetattr: %m");
    return ok;
}

bool
ModemServer::tcgetattr(const char* method, struct termios& term)
{
    if (::tcgetattr(modemFd, &term) != 0) {
	traceModemOp("%s::tcgetattr: %m", method);
	return (false);
    } else
	return (true);
}

/*
 * Set tty port baud rate and flow control.
 */
bool
ModemServer::setBaudRate(BaudRate rate, FlowControl iFlow, FlowControl oFlow)
{
    if (rate >= NBAUDS)
	rate = NBAUDS-1;
    traceModemOp("set baud rate: %d baud, input flow %s, output flow %s",
	baudRates[rate], flowNames[iFlow], flowNames[oFlow]);
    struct termios term;
    if (!tcgetattr("setBaudRate", term))
	return (false);
    curRate = rate;				// NB: for use elsewhere
    term.c_oflag = 0;
    term.c_lflag = 0;
    term.c_iflag &= IXON|IXOFF;			// keep these bits
    term.c_cflag &= CRTSCTS;			// and these bits
    setParity(term, curParity);
    term.c_cflag |= CLOCAL | CREAD;
    setFlow(term, iFlow, oFlow);
    cfsetospeed(&term, termioBaud[rate]);
    cfsetispeed(&term, termioBaud[rate]);
    term.c_cc[VMIN] = (cc_t) curVMin;
    term.c_cc[VTIME] = (cc_t) curVTime;
    flushModemInput();
#if HAS_TXCD
    /* 
     * From: Steve Williams <steve@geniers.cuug.ab.ca>
     *
     * Under AIX there is no easy way to determine if hardware
     * handshaking is already on the terminal Control Discipline
     * stack.  This is necessary to properly condition the tty
     * device for RTS/CTS flow control.  Rather than determine
     * the state of the device we just add/remove the Control
     * Discipline and ignore any errors.  Note that this is the
     * only place this must be done because it is the only
     * method through which the modem drivers enable/disable
     * RTS/CTS flow control.
     */
    if (iFlow == FaxModem::FLOW_RTSCTS) {
	traceModemOp("add rts control discipline");
	(void) ioctl(modemFd, TXADDCD, "rts");	// XXX check return
    } else {
	traceModemOp("remove rts control discipline");
	(void) ioctl(modemFd, TXDELCD, "rts");	// XXX check return
    }
#endif
#if HAS_TERMIOX
    /*
     * Some SVR4.2 systems require use of termiox
     * to setup hardware handshaking on a port.
     */
    struct termiox termx;
    if (ioctl(modemFd, TCGETX, &termx) >= 0) {
	if (iFlow == FaxModem::FLOW_RTSCTS)
	    termx.x_hflag |= RTSXOFF|CTSXON;
	else
	    termx.x_hflag &= ~(RTSXOFF|CTSXON);
	if (ioctl(modemFd, TCSETX, &termx) < 0)
	    traceModemOp("ioctl(TCSETX): %m");
    }
#endif
    return (tcsetattr(TCSANOW, term));
}

/*
 * Set tty port baud rate and leave flow control state unchanged.
 */
bool
ModemServer::setBaudRate(BaudRate rate)
{
    if (rate >= NBAUDS)
	rate = NBAUDS-1;
    traceModemOp("set baud rate: %d baud (flow control unchanged)",
	baudRates[rate]);
    struct termios term;
    if (!tcgetattr("setBaudRate", term))
	return (false);
    curRate = rate;				// NB: for use elsewhere
    term.c_oflag = 0;
    term.c_lflag = 0;
    term.c_iflag &= IXON|IXOFF;			// keep these bits
    term.c_cflag &= CRTSCTS;			// and these bits
    setParity(term, curParity);
    term.c_cflag |= CLOCAL | CREAD;
    cfsetospeed(&term, termioBaud[rate]);
    cfsetispeed(&term, termioBaud[rate]);
    term.c_cc[VMIN] = (cc_t) curVMin;
    term.c_cc[VTIME] = (cc_t) curVTime;
    flushModemInput();
    return (tcsetattr(TCSANOW, term));
}

/*
 * Set tty port parity and number of data bits
 */
bool
ModemServer::setParity(Parity parity)
{
    traceModemOp("set parity: %s", parityNames[parity]);
    struct termios term;
    if (!tcgetattr("setParity", term))
	return (false);
    setParity(term, parity);
    curParity = parity;				// used above
    flushModemInput();
    return (tcsetattr(TCSANOW, term));
}

/*
 * Manipulate DTR on tty port.
 *
 * On systems that support explicit DTR control this is done
 * with an ioctl.  Otherwise we assume that setting the baud
 * rate to zero causes DTR to be dropped (asserting DTR is
 * assumed to be implicit in setting a non-zero baud rate).
 *
 * NB: we use the explicit DTR manipulation ioctls because
 *     setting the baud rate to zero on some systems can cause
 *     strange side effects.
 */
bool
ModemServer::setDTR(bool onoff)
{
    traceModemOp("set DTR %s", onoff ? "ON" : "OFF");
#if defined(MCGETA) && defined(MCSETAF) && defined(MDTR)
    /*
     * HP-UX has a special way to manipulate DTR.
     */
    int mstat;
    if (ioctl(modemFd, MCGETA, &mstat) >= 0) {
	if (onoff)
	    mstat |= MDTR;
	else
	    mstat &= ~MDTR;
	if (ioctl(modemFd, MCSETAF, &mstat) >= 0)
	    return (true);
    }
#elif defined(TIOCMBIS)
    int mctl = TIOCM_DTR;
    /*
     * Happy days! Some systems passes the arg by value, while
     * others passes it by reference; is this progress?
     */
#ifdef CONFIG_TIOCMBISBYREF
    if (ioctl(modemFd, onoff ? TIOCMBIS : TIOCMBIC, (char *)&mctl) >= 0)
#else
    if (ioctl(modemFd, onoff ? TIOCMBIS : TIOCMBIC, (char *)mctl) >= 0)
#endif
	return (true);
    /*
     * Sigh, Sun seems to support this ioctl only on *some*
     * devices (e.g. on-board duarts, but not the ALM-2 card);
     * so if the ioctl that should work fails, we fallback
     * on the usual way of doing things...
     */
#endif /* TIOCMBIS */
    return (onoff ? true : setBaudRate(ClassModem::BR0));
}

static const char* actNames[] = { "NOW", "DRAIN", "FLUSH" };
static u_int actCode[] = { TCSANOW, TCSADRAIN, TCSAFLUSH };

/*
 * Set tty modes so that the specified handling
 * is done on data being sent and received.  When
 * transmitting binary data, oFlow is FLOW_NONE to
 * disable the transmission of XON/XOFF by the host
 * to the modem.  When receiving binary data, iFlow
 * is FLOW_NONE to cause XON/XOFF from the modem
 * to not be interpreted.  In each case the opposite
 * XON/XOFF handling should be enabled so that any
 * XON/XOFF from/to the modem will be interpreted.
 */
bool
ModemServer::setXONXOFF(FlowControl iFlow, FlowControl oFlow, SetAction act)
{
    traceModemOp("set XON/XOFF/%s: input %s, output %s",
	actNames[act],
	iFlow == ClassModem::FLOW_NONE ? "ignored" : "interpreted",
	oFlow == ClassModem::FLOW_NONE ? "disabled" : "generated"
    );
    struct termios term;
    if (!tcgetattr("setXONXOFF", term))
	return (false);
    setFlow(term, iFlow, oFlow);
    if (act == ClassModem::ACT_FLUSH)
	flushModemInput();
    return (tcsetattr(actCode[act], term));
}

#ifdef sgi
#include <sys/stropts.h>
#include <sys/z8530.h>
#endif
#ifdef sun
#include <sys/stropts.h>
#endif

/*
 * Setup process state either for minimum latency (no buffering)
 * or reduced latency (input may be buffered).  We fiddle with
 * the termio structure and, if required, the streams timer
 * that delays the delivery of input data from the UART module
 * upstream to the tty module.
 */
bool
ModemServer::setInputBuffering(bool on)
{
    if (on != inputBuffered) traceModemOp("input buffering %s", on ? "enabled" : "disabled");
    inputBuffered = on;
#ifdef SIOC_ITIMER
    /*
     * Silicon Graphics systems have a settable timer
     * that causes the UART driver to delay passing
     * data upstream to the tty module.  This can cause
     * anywhere from 20-30ms delay between input characters.
     * We set it to zero when input latency is critical.
     */
    strioctl str;
    str.ic_cmd = SIOC_ITIMER;
    str.ic_timout = (on ? 2 : 0);	// 2 ticks = 20ms (usually)
    str.ic_len = 4;
    int arg = 0;
    str.ic_dp = (char*)&arg;
    if (ioctl(modemFd, I_STR, &str) < 0)
	traceModemOp("setInputBuffer::ioctl(SIOC_ITIMER): %m");
#endif
#ifdef sun
    /*
     * SunOS has a timer similar to the SIOC_ITIMER described
     * above for input on the on-board serial ports, but it is
     * not generally accessible because it is controlled by a
     * stream control message (M_CTL w/ either MC_SERVICEDEF or
     * MC_SERVICEIMM) and you can not do a putmsg directly to
     * the UART module and the tty driver does not provide an
     * interface.  Also, the ALM-2 driver apparently also has
     * a timer, but does not provide the M_CTL interface that's
     * provided for the on-board ports.  All in all this means
     * that the only way to defeat the timer for the on-board
     * serial ports (and thereby provide enough control for the
     * fax server to work with Class 1 modems) is to implement
     * a streams module in the kernel that provides an interface
     * to the timer--which is what has been done.  In the case of
     * the ALM-2, however, you are just plain out of luck unless
     * you have source code.
     */
    static bool zsunbuf_push_tried = false;
    static bool zsunbuf_push_ok = false;
    if (on) {			// pop zsunbuf if present to turn on buffering
	char topmodule[FMNAMESZ+1];
        if (zsunbuf_push_ok && ioctl(modemFd, I_LOOK, topmodule) >= 0 &&
	  streq(topmodule, "zsunbuf")) {
	    if (ioctl(modemFd, I_POP, 0) < 0)
		traceModemOp("pop zsunbuf failed: %m");
	}
    } else {			// push zsunbuf to turn off buffering
        if (!zsunbuf_push_tried) {
            zsunbuf_push_ok = (ioctl(modemFd, I_PUSH, "zsunbuf") >= 0);
            traceModemOp("initial push zsunbuf %s",
                zsunbuf_push_ok ? "succeeded" : "failed");
            zsunbuf_push_tried = true;
        } else if (zsunbuf_push_ok) {
            if (ioctl(modemFd, I_PUSH, "zsunbuf") < 0)
                traceModemOp("push zsunbuf failed: %m");
        }
    }
#endif
    struct termios term;
    (void) tcgetattr("setInputBuffering", term);
    if (on) {
	curVMin = 127;
	curVTime = 1;
    } else {
	curVMin = 1;
	curVTime = 0;
    }
    term.c_cc[VMIN] = (cc_t) curVMin;
    term.c_cc[VTIME] = (cc_t) curVTime;
    return (tcsetattr(TCSANOW, term));
}

bool
ModemServer::sendBreak(bool pause)
{
    traceModemOp("send break%s", pause ? " (pause)" : "");
    flushModemInput();
    if (pause) {
	/*
	 * NB: TCSBRK is supposed to wait for output to drain,
	 * but some modems appear to lose data if we don't do this.
	 */
	(void) tcdrain(modemFd);
    }
    if (tcsendbreak(modemFd, 0) != 0) {
	traceModemOp("tcsendbreak: %m");
	return (false);
    } else
	return (true);
}

void
ModemServer::startTimeout(long ms)
{
    timer.startTimeout(ms);
    timeout = false;
}

void
ModemServer::stopTimeout(const char* whichdir)
{
    timer.stopTimeout();
    if (timeout = timer.wasTimeout())
	traceModemOp("TIMEOUT: %s", whichdir);
}

void
ModemServer::sendDLEETX()
{
    u_char buf[2];
    buf[0] = DLE;
    buf[1] = ETX;
    (void) putModem(buf, 2);
}

int
ModemServer::getModemLine(char rbuf[], u_int bufSize, long ms)
{
    int c;
    u_int cc = 0;
    if (ms) startTimeout(ms);
    do {
	while ((c = getModemChar(0)) != EOF && c != '\n' && !timer.wasTimeout())
	    if (c != '\0' && c != '\r' && cc < bufSize)
		rbuf[cc++] = c;
    } while (!timer.wasTimeout() && cc == 0 && c != EOF);
    rbuf[cc] = '\0';
    if (ms) stopTimeout("reading line from modem");
    if (!timeout)
	traceStatus(FAXTRACE_MODEMCOM, "--> [%d:%s]", cc, rbuf);
    return (cc);
}

int
ModemServer::getModemChar(long ms, bool isquery)
{
    if (rcvNext >= rcvCC) {
	int n = 0;
	if (isquery) {
	    if (fcntl(modemFd, F_SETFL, fcntl(modemFd, F_GETFL, 0) | O_NONBLOCK)) {
		traceStatus(FAXTRACE_MODEMCOM, "Can not set O_NONBLOCK: errno %u", errno);
		return (EOF);
	    }
	    n = 5;		// only read once
	}
	if (ms) startTimeout(ms);
	do
	    rcvCC = Sys::read(modemFd, (char*) rcvBuf, sizeof (rcvBuf));
	while (n++ < 5 && rcvCC == 0);
	if (ms) stopTimeout("reading from modem");
	if (isquery) {
	    if (fcntl(modemFd, F_SETFL, fcntl(modemFd, F_GETFL, 0) &~ O_NONBLOCK))
		traceStatus(FAXTRACE_MODEMCOM, "Can not reset O_NONBLOCK: errno %u", errno);
	}
	if (rcvCC <= 0) {
	    if (rcvCC < 0) {
		if (errno != EINTR)
		    if (!isquery || errno != EAGAIN)
			traceStatus(FAXTRACE_MODEMCOM,
			    "MODEM READ ERROR: errno %u", errno);
	    }
	    return (EOF);
	} else
	    traceModemIO("-->", rcvBuf, rcvCC);
	rcvNext = 0;
    }
    return (rcvBuf[rcvNext++]);
}

int
ModemServer::getModemBit(long ms)
{
    /*
     * Return bytes bit-by-bit in MSB2LSB order.
     * getModemChar() returns them in LSB2MSB.
     */
    if (rcvBit < 1) {
	rcvBit = 8;
	gotByte = getModemChar(ms);
	if (gotByte == 0x10) {		// strip stuffed DLE
	    gotByte = getModemChar(ms);
	    if (gotByte == 0x03) sawBlockEnd = true;	// DLE+ETX
	}
    }
    // enable this to simulate a VERY noisy connection
    // if (((int) Sys::now() & 1) && ((random() % 10000)/10000.0) > 0.95) return (1);
    if (gotByte == EOF) return (EOF);
    else if (gotByte & (0x80 >> --rcvBit)) return (1);
    else return (0);
}

int
ModemServer::getLastByte()
{
    return gotByte;
}

bool
ModemServer::didBlockEnd()
{
    return sawBlockEnd;
}

void
ModemServer::resetBlock()
{
    sawBlockEnd = false;
}

void
ModemServer::modemFlushInput()
{
    traceModemOp("flush i/o");
    flushModemInput();
    if (tcflush(modemFd, TCIFLUSH) != 0)
	traceModemOp("tcflush: %m");
}

bool
ModemServer::modemStopOutput()
{
    if (tcflow(modemFd, TCOOFF) != 0) {
	traceModemOp("tcflow: %m");
	return (false);
    } else
	return (true);
}

void
ModemServer::flushModemInput()
{
    rcvCC = rcvNext = rcvBit = gotByte = 0;
    sawBlockEnd = false;
}

bool
ModemServer::putModem(const void* data, int n, long ms)
{
    traceStatus(FAXTRACE_MODEMCOM, "<-- data [%d]", n);
    return (putModem1(data, n, ms));
}

bool
ModemServer::putModem1(const void* data, int n, long ms)
{
    if (ms)
	startTimeout(ms);
    else
	timeout = false;
    int cc = Sys::write(modemFd, (const char*) data, n);
    if (ms)
	stopTimeout("writing to modem");
    if (cc > 0) {
	traceModemIO("<--", (const u_char*) data, cc);
	n -= cc;
    }
    if (cc == -1) {
	if (errno != EINTR)
	    traceStatus(FAXTRACE_MODEMCOM, "MODEM WRITE ERROR: errno %u",
		errno);
    } else if (n != 0)
	traceStatus(FAXTRACE_MODEMCOM, "MODEM WRITE SHORT: sent %u, wrote %u",
	    cc+n, cc);
    return (!timeout && n == 0);
}
