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
#include "faxApp.h"
#include "config.h"

#include <errno.h>
#include <pwd.h>
#include <limits.h>
extern "C" {
#if HAS_LOCALE
#include <locale.h>
#endif
}

#include "Sys.h"

/*
 * getopt Iterator Interface.
 */
extern int opterr, optind;
extern char* optarg;

GetoptIter::GetoptIter(int ac, char** av, const fxStr& s) : opts(s)
{
    argc = ac;
    argv = av;
    optind = 1;
    opterr = 0;
    c = Sys::getopt(argc, argv, opts);
}
GetoptIter::~GetoptIter() {}

void GetoptIter::operator++()		{ c = Sys::getopt(argc, argv, opts); }
void GetoptIter::operator++(int)	{ c = Sys::getopt(argc, argv, opts); }
const char* GetoptIter::optArg() const	{ return optarg; }
const char* GetoptIter::getArg()
   { return optind < argc ? argv[optind] : ""; }
const char* GetoptIter::nextArg()
   { return optind < argc ? argv[optind++] : ""; }

faxApp::faxApp()
{
    running = false;
    faxqfifo = -1;
    setLogFacility(LOG_FAX);			// default
#ifdef LC_CTYPE
    setlocale(LC_CTYPE, "");			// for <ctype.h> calls
#endif
#ifdef LC_TIME
    setlocale(LC_TIME, "");			// for strftime calls
#endif
    signal(SIGPIPE, fxSIGHANDLER(SIG_IGN));	// for FIFO writes
}
faxApp::~faxApp() {}

void
faxApp::initialize(int, char**)
{
    openFIFOs();
}
void faxApp::open(void) { running = true; }
void
faxApp::close(void)
{
    running = false;
    if (faxqfifo != -1)
	Sys::close(faxqfifo);
}

fxStr faxApp::getopts;
void faxApp::setOpts(const char* s) { getopts = s; }
const fxStr& faxApp::getOpts() { return getopts; }

void
faxApp::fatal(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogError(fmt, ap);
    va_end(ap);
    exit(-1);
}

/*
 * FIFO-related support.
 */
const fxStr faxApp::fifoName = FAX_FIFO;

/*
 * Open the requisite FIFO special files.
 */
void
faxApp::openFIFOs(void)
{
}

void
faxApp::closeFIFOs(void)
{
}

/*
 * Open the specified FIFO file.
 */
int
faxApp::openFIFO(const char* fifoName, int mode, bool okToExist)
{
    if (Sys::mkfifo(fifoName, mode & 0777) < 0) {
	if (errno != EEXIST || !okToExist)
	    faxApp::fatal("Could not create %s: %m.", fifoName);
    }
    int fd = Sys::open(fifoName, CONFIG_OPENFIFO|O_NDELAY, 0);
    if (fd == -1)
	faxApp::fatal("Could not open FIFO file %s: %m.", fifoName);
    if (!Sys::isFIFOFile(fd))
	faxApp::fatal("%s is not a FIFO special file", fifoName);
    // open should set O_NDELAY, but just to be sure...
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NDELAY) < 0)
	logError("openFIFO %s: fcntl: %m", fifoName);
    return (fd);
}

/*
 * Respond to input on a FIFO file descriptor.
 */
int
faxApp::FIFOInput(int fd)
{
    char buf[2048];
    int n;
    while ((n = Sys::read(fd, buf, sizeof (buf)-1)) > 0) {
	buf[n] = '\0';
	/*
	 * Break up '\0'-separated records and strip
	 * any trailing '\n' so that "echo mumble>FIFO"
	 * works (i.e. echo appends a '\n' character).
	 */
	char* bp = &buf[0];
	do {
	    char* cp = strchr(bp, '\0');
	    if (cp > bp) {
		if (cp[-1] == '\n')
		    cp[-1] = '\0';
		FIFOMessage(bp);
	    }
	    bp = cp+1;
	} while (bp < &buf[n]);
    }
#ifdef FIFOSELECTBUG
    /*
     * Solaris 2.x botch (and some versions of IRIX 5.x).
     *
     * A client close of an open FIFO causes an M_HANGUP to be
     * sent and results in the receiver's file descriptor being
     * marked ``hung up''.  This in turn causes select to
     * perpetually return true and if we're running as a realtime
     * process, brings the system to a halt.  The workaround for
     * Solaris 2.1 was to do a parallel reopen of the appropriate
     * FIFO so that the original descriptor is recycled.  This
     * apparently no longer works in Solaris 2.2 or later and we
     * are forced to close and reopen both FIFO descriptors (noone
     * appears capable of answering why this this is necessary and
     * I personally don't care...)
     */
    closeFIFOs(); openFIFOs();
#endif
    return (0);
}

/*
 * Process a message received through a FIFO.
 */
void
faxApp::FIFOMessage(const char* cp)
{
    logError("Bad fifo message \"%s\"", cp);
}

/*
 * Send a message to the central queuer process.
 */
bool
faxApp::vsendQueuer(const char* fmt, va_list ap)
{
    if (faxqfifo == -1) {
#ifdef FIFOSELECTBUG
	/*
	 * We try multiple times to open the appropriate FIFO
	 * file because the system has a kernel bug that forces
	 * the server to close+reopen the FIFO file descriptors
	 * for each message received on the FIFO (yech!).
	 */
	int tries = 0;
	do {
	    if (tries > 0)
		sleep(1);
	    faxqfifo = Sys::open(fifoName, O_WRONLY|O_NDELAY);
	} while (faxqfifo == -1 && errno == ENXIO && ++tries < 5);
#else
	faxqfifo = Sys::open(fifoName, O_WRONLY|O_NDELAY);
#endif
	if (faxqfifo == -1)
	    return (false);
	/*
	 * Turn off O_NDELAY so that write will block if FIFO is full.
	 */
	if (fcntl(faxqfifo, F_SETFL, fcntl(faxqfifo, F_GETFL, 0) &~ O_NDELAY) < 0)
	    logError("fcntl: %m");
    }
    fxStr msg = fxStr::vformat(fmt, ap);
    u_int len = msg.length() + 1;
    if (Sys::write(faxqfifo, (const char*)msg, len) != len) {
	if (errno == EBADF || errno == EPIPE)		// reader expired
	    Sys::close(faxqfifo), faxqfifo = -1;
	else
	    logError("FIFO write failed: %m"); 
	return (false);
    } else
	return (true);
}

/*
 * Send a message to the central queuer process.
 */
bool
faxApp::sendQueuer(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    bool ok = vsendQueuer(fmt, ap);
    va_end(ap);
    return (ok);
}

/*
 * Send a modem status message to the central queuer process.
 */
bool
faxApp::sendModemStatus(const char* devid, const char* fmt0 ...)
{
    fxStr fmt = fxStr::format("+%s:%s", devid, fmt0);
    va_list ap;
    va_start(ap, fmt0);
    bool ok = vsendQueuer(fmt, ap);
    va_end(ap);
    return (ok);
}

/*
 * Send a job status message to the central queuer process.
 */
bool
faxApp::sendJobStatus(const char* jobid, const char* fmt0 ...)
{
    fxStr fmt = fxStr::format("*%s:%s", jobid, fmt0);
    va_list ap;
    va_start(ap, fmt0);
    bool ok = vsendQueuer(fmt, ap);
    va_end(ap);
    return (ok);
}

/*
 * Send a receive status message to the central queuer process.
 */
bool
faxApp::sendRecvStatus(const char* devid, const char* fmt0 ...)
{
    fxStr fmt = fxStr::format("@%s:%s", devid, fmt0);
    va_list ap;
    va_start(ap, fmt0);
    bool ok = vsendQueuer(fmt, ap);
    va_end(ap);
    return (ok);
}

/*
 * Miscellaneous stuff.
 */

/*
 * Convert an identifier to the pathname for the
 * device (required by the UUCP lock code).  This
 * is done converting '_'s to '/'s and then prepending
 * _PATH_DEV.  This is required for SVR4 systems
 * which have their devices in subdirectories!
 */
fxStr
faxApp::idToDev(const fxStr& id)
{
    fxStr dev(id);
    u_int l;
    while ((l = dev.next(0, '_')) < dev.length())
	dev[l] = '/';
    return (_PATH_DEV | dev);
}

fxStr
faxApp::devToID(const fxStr& id)
{
    fxStr devID(id);
    fxStr prefix(_PATH_DEV);
    u_int l = prefix.length();
    if (devID.length() > l && devID.head(l) == prefix)
	devID.remove(0, l);
    while ((l = devID.next(0, '/')) < devID.length())
	devID[l] = '_';
    return (devID);
}

/*
 * Force the real uid+gid to be the same as
 * the effective ones.  Must temporarily
 * make the effective uid root in order to
 * do the real id manipulations.
 */
void
faxApp::setRealIDs(void)
{
    uid_t euid = geteuid();
    if (seteuid(0) < 0)
	logError("seteuid(root): %m");
    if (setgid(getegid()) < 0)
	logError("setgid: %m");
    if (setuid(euid) < 0)
	logError("setuid: %m");
}

static void
detachIO(void)
{
    endpwent();				// XXX some systems hold descriptors
    closelog();				// XXX in case syslog has descriptor
    int fd = Sys::open(_PATH_DEVNULL, O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    for (fd = Sys::getOpenMax()-1; fd >= 0; fd--)
	if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO)
	    (void) Sys::close(fd);
}

const fxStr faxApp::quote	= " \"";
const fxStr faxApp::enquote	= "\"";

/*
 * Run the specified shell command.  If changeIDs is
 * true, we set the real uid+gid to the effective; this
 * is so that programs like sendmail show an informative
 * from address.
 */
bool
faxApp::runCmd(const char* cmd, bool changeIDs)
{
    pid_t pid = fork();
    switch (pid) {
    case 0:
	if (changeIDs)
	    setRealIDs();
	detachIO();
	execl("/bin/sh", "sh", "-c", cmd, (char*) NULL);
	sleep(1);			// XXX give parent time
	_exit(127);
    case -1:
	logError("Can not fork for \"%s\"", cmd);
	return (false);
    default:
	{ int status = 0;
	  Sys::waitpid(pid, status);
	  if (status != 0) {
	    logError("Bad exit status %#o for \"%s\"", status, cmd);
	    return (false);
	  }
	}
	return (true);
    }
}

/*
 * Setup server uid+gid.  Normally the server is started up
 * by root and then sets its effective uid+gid to that of
 * the ``fax'' user (the gid is used by hfaxd and should be
 * the uid of the fax user).  This permits the server to
 * switch to ``root'' whenever it's necessary (in order to
 * gain access to a root-specific function such as starting
 * a getty process).  Alternatively the server may be run
 * setuid ``fax'' with the real uid of ``root'' (in order to
 * do privileged operations).
 */ 
void
faxApp::setupPermissions(void)
{
    if (getuid() != 0)
	faxApp::fatal("The fax server must run with real uid root.\n");
    uid_t euid = geteuid();
    const passwd* pwd = getpwnam(FAX_USER);
    if (!pwd)
	faxApp::fatal("No fax user \"%s\" defined on your system!\n"
	    "This software is not installed properly!", FAX_USER);
    if (euid == 0) {
	if (setegid(pwd->pw_uid) < 0)
	    faxApp::fatal("Can not setup permissions (gid)");
	if (seteuid(pwd->pw_uid) < 0)
	    faxApp::fatal("Can not setup permissions (uid)");
    } else {
	uid_t faxuid = pwd->pw_uid;
	setpwent();
	pwd = getpwuid(euid);
	if (!pwd)
	    faxApp::fatal("Can not figure out the identity of uid %u", euid);
	if (pwd->pw_uid != faxuid)
	    faxApp::fatal("Configuration error; "
		"the fax server must run as the fax user \"%s\".", FAX_USER);
	 (void) setegid(faxuid);
    }
    endpwent();
}

/*
 * Break the association with the controlling tty if we can
 * preserve it later with the POSIX O_NOCTTY mechanism.  Note
 * that we do not use detachIO to close all the open file
 * descriptors because many systems cache open descriptors within
 * libraries for performance reasons and do not react well when
 * you close them w/o telling them about it (and some don't react
 * well even when you *DO* tell them).  Since we know we're called
 * very early on from main in all our apps we just assume that
 * we only need to remove the stdin+stdout+stderr before forking
 * and starting a new session.
 */
void
faxApp::detachFromTTY(void)
{
#ifdef O_NOCTTY
    int fd = Sys::open(_PATH_DEVNULL, O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    switch (fork()) {
    case 0:	break;			// child, continue
    case -1:	_exit(1);		// error
    default:	_exit(0);		// parent, terminate
    }
    (void) setsid();
#endif
}

/*
 * Private version of fxassert for server processes.
 * The default library routine sends the message to
 * stderr and then calls abort().  This typically
 * does not work for a server because stderr is not
 * attached to anything (so the message is lost) and
 * abort will not generate a core dump because the
 * process has an effective uid and/or gid different
 * from the real uid/gid.
 *
 * If (the undocumented configuration parameter)
 * CONFIG_WAITONASSERT is set to a non-zero value
 * then instead of dumping core the process will
 * pause indefinitely so that a debugger can be
 * attached.
 */
extern "C" void
_fxassert(const char* msg, const char* file, int line)
{
    fprintf(stderr, "Assertion failed \"%s\", file \"%s\" line %d.\n", 
	msg, file, line);
    logError("Assertion failed \"%s\", file \"%s\" line %d.\n", 
	msg, file, line);
#if CONFIG_WAITONASSERT
    for (;;)				// wait for a debugger to attach
	pause();
#else
    faxApp::setRealIDs();		// reset so we get a core dump
    abort();
#endif
    /*NOTREACHED*/
}
