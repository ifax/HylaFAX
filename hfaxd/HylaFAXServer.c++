/*	$Id$ */
/*
 * Copyright (c) 1995-1996 Sam Leffler
 * Copyright (c) 1995-1996 Silicon Graphics, Inc.
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
#include "port.h"
#include "Sys.h"
#include "config.h"
#include "tiffio.h"

#include "HylaFAXServer.h"
#include "Dispatcher.h"
#include "StackBuffer.h"
#include "Sequence.h"

#include <ctype.h>
#include <sys/file.h>
#include <pwd.h>
#include <limits.h>

const char* HylaFAXServer::version = HYLAFAX_VERSION;

/*
 * NB: The remainder of the instance state is
 * initialized in initServer which is expected to
 * be called before any other operation is done
 * (through open).  This is required because some
 * initialization work is done by virtual methods
 * that cannot be invoked from the constructor.
 */
HylaFAXServer::HylaFAXServer()
    : defJob("")
{
    state = 0;
    xferfaxlog = -1;

    loginAttempts = 0;		// NB: not reset by REIN command
    adminAttempts = 0;		// NB: not reset by REIN command
    idcache = NULL;

    data = -1;			// current data connection (socket)
    pdata = -1;			// passive mode data connect (socket)
    faxqFd = -1;
    clientFd = -1;

    char buff[64];
    (void) gethostname(buff, sizeof(buff));
    hostname = buff;
    hostaddr = "unknown";	// derived classes should fill-in

    lastModTime = 0;		// shutdown file mod time
    discTime = 0;		// shutdown forced disconnect time
    denyTime = 0;		// shutdown service denial time
    /*
     * Calculate the time differential between
     * the local timezone and GMT for adjusting
     * client-specified time values that are given
     * in GMT.
     */
    time_t now = Sys::now();
    struct tm gmt = *gmtime(&now);
    struct tm tm = *localtime(&now);
    gmt.tm_isdst = tm.tm_isdst;
    gmtoff = mktime(&gmt) - mktime(&tm);
#if HAS_TM_ZONE
    /*
     * BSD/OS doesn't support the global timezone
     * information so setup substitutes here.
     */
    tzname[0] = tm.tm_zone;
    tzname[1] = NULL;
#endif

    cachedTIFF = NULL;
}

HylaFAXServer::~HylaFAXServer()
{
    dologout(0);
}

/*
 * Initialize the state of the server.  Note that this
 * is used to implement the REIN command and for that
 * it doesn't work entirely because state set from the
 * configuration files is not accessible after a login
 * because the sever does a chroot to the top of the
 * spooling area.  The only way to make this work is
 * to keep all the configuration files in the spooling
 * area which is not folks want when things are shared
 * through NFS (i.e. server binaries and config files
 * are NFS-mounted by the spooling area is private).
 */
void
HylaFAXServer::initServer(void)
{
    end_login();		// reset user-related state

    /*
     * Default state:
     *   o send long replies
     *   o use GMT for time values
     *   o auto-detect whether filesystem has BSD or SysV
     *     semantics for setting the GID on created files
     */
    state = S_LREPLIES|S_USEGMT|S_CHECKGID;

    restart_point = 0;		// data-transfer-related state
    mode = MODE_S;
    form = FORM_PS;
    type = TYPE_A;
    stru = STRU_F;

    initDefaultJob();		// reset default job state
    curJob = &defJob;

    if (data != -1)		// data transfer-related state
	Sys::close(data), data = -1;
    if (pdata != -1)
	Sys::close(pdata), pdata = -1;

    if (trigSpec != "") {
	fxStr emsg;
	cancelTrigger(emsg);
    }

    // XXX FIFO state

    pushedToken = T_NIL;
    recvCC = 0;			// no data present in buffer
    recvNext = 0;
    consecutiveBadCmds = 0;

    resetConfig();
    readConfig(FAX_SYSCONF);
    readConfig(FAX_LIBDATA "/hfaxd.conf");
}

static void
tiffErrorHandler(const char* module, const char* fmt0, va_list ap)
{
    fxStr fmt = (module != 0) ?
        fxStr::format("%s: Warning, %s.", module, fmt0)
        : fxStr::format("Warning, %s.", fmt0);
    vlogError(fmt, ap);
}

static void
tiffWarningHandler(const char* module, const char* fmt0, va_list ap)
{
    fxStr fmt = (module != 0) ?
        fxStr::format("%s: Warning, %s.", module, fmt0)
        : fxStr::format("Warning, %s.", fmt0);
    vlogWarning(fmt, ap);
}

void
HylaFAXServer::open(void)
{
    initServer();		// complete state initialization

    fxStr emsg;
    if (!initClientFIFO(emsg)) {
        logInfo("connection refused (%s) from %s [%s]",
	    (const char*) emsg,
	    (const char*) remotehost, (const char*) remoteaddr);
	reply(420, "%s server cannot initialize: %s",
	    (const char*) hostname, (const char*) emsg);
	dologout(-1);
    }
    ctrlFlags = fcntl(STDIN_FILENO, F_GETFL);	// for parser
    if (isShutdown(true))
	reply(220, "%s HylaFAX server shut down; available only for admin use.",
	    (const char*) hostname);
    else
	reply(220, "%s server (%s) ready.", (const char*) hostname, version);

    if (TRACE(TIFF)) {
	TIFFSetErrorHandler(tiffErrorHandler);
	TIFFSetWarningHandler(tiffWarningHandler);
    } else {
	TIFFSetErrorHandler(NULL);
	TIFFSetWarningHandler(NULL);
    }
}

void
HylaFAXServer::close()
{
    dologout(-1);
}

void
HylaFAXServer::setupPermissions(void)
{
    uid_t euid = geteuid();
    struct passwd* pwd = getpwnam(FAX_USER);
    if (!pwd)
	logError("No fax user \"%s\" defined on your system!\n"
	    "This software is not installed properly!", FAX_USER);
    else if (euid == 0) {
	if (setegid(pwd->pw_gid) < 0)
	    logError("Can not setup permissions (gid): %m");
	else if (seteuid(pwd->pw_uid) < 0)
	    logError("Can not setup permissions (uid): %m");
	else {
	    faxuid = pwd->pw_gid;
	    endpwent();
	    return;
	}
    } else {
	faxuid = pwd->pw_gid;
	uid_t faxuid = pwd->pw_uid;
	setpwent();
	pwd = getpwuid(euid);
	if (!pwd)
	    logError("Can not figure out the identity of uid %u", euid);
	else if (pwd->pw_uid != faxuid)
	    logError("Configuration error; "
		"the fax server must run as the fax user \"%s\".", FAX_USER);
	else {
	    endpwent();
	    return;
	}
    }
    exit(-1);
}

/*
 * Close all open descriptors and unlink any
 * Dispatcher i/o handlers.
 */
void
HylaFAXServer::closeAllBut(int fd)
{
    Dispatcher& disp = Dispatcher::instance();
    for (int f = Sys::getOpenMax()-1; f >= 0; f--)
	if (f != fd) {
	    IOHandler* h = disp.handler(f, Dispatcher::ReadMask);
	    if (h)
		disp.unlink(f);
	    Sys::close(f);
	}
}

const char*
HylaFAXServer::fixPathname(const char* file)
{
    return (!IS(LOGGEDIN) && file[0] == '/' ? file+1 : file);
}

bool
HylaFAXServer::readShutdownFile(void)
{
    bool ok = false;
    FILE* fd = fopen(fixPathname(shutdownFile), "r");
    if (fd != NULL) {
	struct tm tm;
	int deny, disc;
	memset(&tm, 0, sizeof (tm));
	int n = fscanf(fd, "%d %d %d %d %d %d %d\n",
	    &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
	    &deny, &disc);
	if (n == 7) {
	    tm.tm_year -= 1900;
	    tm.tm_isdst = -1;
	    time_t shut = mktime(&tm);
	    if (shut != (time_t) -1) {
		denyTime = shut - 3600 * (deny / 100) + 60 * (deny % 100);
		discTime = shut - 3600 * (disc / 100) + 60 * (disc % 100);

		shutdownMsg = "";
		char buf[1024];
		while (fgets(buf, sizeof (buf), fd))
		    shutdownMsg.append(buf);
		ok = true;
	    } else
		logError("%s: Invalid shutdown time, mktime conversion failed;"
		    "Year=%d Mon=%d Day=%d Hour=%d Min=%d"
		    , (const char*) shutdownFile
		    , tm.tm_year+1900
		    , tm.tm_mon
		    , tm.tm_mday
		    , tm.tm_hour
		    , tm.tm_min
		);
	} else
	    logError("%s: shutdown file format error",
		(const char*) shutdownFile);
	(void) fclose(fd);
    } else
	logError("%s: Cannot open shutdown file: %s",
	    (const char*) shutdownFile, strerror(errno));
    return (ok);
}

bool
HylaFAXServer::isShutdown(bool quiet)
{
    struct stat sb;
    if (shutdownFile == "" || Sys::stat(fixPathname(shutdownFile), sb) < 0)
	return (false);
    if (sb.st_mtime != lastModTime) {
	if (!readShutdownFile())
	    return (false);
	lastModTime = sb.st_mtime;
    }
    time_t now = Sys::now();
    if (!quiet) {			// possibly send client shutdown msg
	time_t timeToDisconnect = discTime - now;
	time_t lastMsg = now-lastTime;
	bool sendShutDownMsg =
	       (lastTime == 0)		// first time
	    || (timeToDisconnect < 60)	// <60 seconds, warn continuously
					// <15 minutes, warn ever 5 minutes
	    || (timeToDisconnect < 15*60 && lastMsg > 5*60)
					// <24 hours, warn every 30 minutes
    	    || (timeToDisconnect < 24*60*60 && lastMsg > 30*60)
					// >24 hours, warn ever day
    	    || (timeToDisconnect < 24*60*60 && lastMsg >= 24*60*60)
	    ;
	if (sendShutDownMsg) {
	    autospout = shutdownMsg;	// XXX append?
	    lastTime = now;
	}
    }
    return (now > discTime);
}

void
HylaFAXServer::statusCmd(void)
{
    lreply(211, "%s HylaFAX server status:", (const char*) hostname);
    printf("    %s\r\n", version);
    printf("    Connected to %s", (const char*) remotehost);
    if (!isdigit(remotehost[0]))
	printf(" (%s)", (const char*) remoteaddr);
    printf("\r\n");
    if (IS(LOGGEDIN)) {
	printf("    Logged in as user %s (uid %u)%s\r\n"
	    , (const char*) the_user
	    , uid
	    , IS(PRIVILEGED) ? " (with administrative privileges)" : ""
	);
	u_int len = strlen(cwd->pathname)-1;	// strip trailing "/"
	printf("    \"%.*s\" is the current directory\r\n",
	    len ? len : len+1, cwd->pathname);
	printf("    Current job: ");
	if (curJob->jobid == "default")
	    printf("(default)\r\n");
	else
	    printf("jobid %s groupid %s\r\n",
		(const char*) curJob->jobid, (const char*) curJob->groupid);
    } else if (IS(WAITPASS))
	printf("    Waiting for password\r\n");
    else
	printf("    Waiting for user name\r\n");
    printf("    Time values are handled in %s\r\n",
	IS(USEGMT) ? "GMT" : tzname[0]);
    printf("    Idle timeout set to %d seconds\r\n", idleTimeout);
    printf("    %s long replies\r\n", IS(LREPLIES) ? "Using" : "Not using");
    if (discTime > 0)
	printf("    Server scheduled to be unavailable at %.24s\r\n",
	    asctime(cvtTime(discTime)));
    else
	printf("    No server down time currently scheduled\r\n");
    printf("    HylaFAX scheduler reached at %s (%sconnected)\r\n"
	, (const char*) faxqFIFOName
	, faxqFd == -1 ? "not " : ""
    );
    if (clientFd != -1)
	printf("    Server FIFO is /%s (%sopen)\r\n"
	    , (const char*) clientFIFOName
	    , clientFd == -1 ? "not " : ""
	);
    if (IS(WAITFIFO))
	printf("    Waiting for response from HylaFAX scheduler\r\n");
    FileCache::printStats(stdout);
    printTransferStatus(stdout);

    netStatus(stdout);		// transport-dependent status
    reply(211, "End of status");
}

int
HylaFAXServer::inputReady(int fd)
{
    if (fd == STDIN_FILENO)
	return parse();
    else if (fd == clientFd)
	return FIFOInput(fd);

    fatal("Input ready on unknown file descriptor %d", fd);
    return (0);				// to shutup compilers
}

void
HylaFAXServer::fatal(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vreply(451, fxStr::format("Error in server: %s", fmt), ap);
    va_end(ap);
    reply(221, "Closing connection due to server error.");
    dologout(0);
    /*NOTREACHED*/
}

void
HylaFAXServer::reply(int code, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vreply(code, fmt, ap);
    va_end(ap);
}

void
HylaFAXServer::vreply(int code, const char* fmt, va_list ap)
{
    if (autospout != "") {
        printf("%d-", code);
        int i = 0;
        while (autospout[i] != '\0') {
            if (autospout[i] == '\n') {
                fputs("\r\n", stdout);
                if (autospout[++i])
                    printf("%d-", code);
            } else
                putchar(autospout[i++]);
        }
        if (autospout[--i] != '\n')
            printf("\r\n");
        autospout = "";
    }
    fxStackBuffer buf;
    buf.vput(fmt, ap);
    fprintf(stdout, "%d %.*s\r\n", code, buf.getLength(), (const char*) buf);
    fflush(stdout);

    if (TRACE(PROTOCOL))
        logDebug("<--- %d %.*s", code, buf.getLength(), (const char*) buf);
}

void
HylaFAXServer::lreply(int code, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlreply(code, fmt, ap);
    va_end(ap);
}

void
HylaFAXServer::vlreply(int code, const char* fmt, va_list ap)
{
    if (IS(LREPLIES)) {
	fxStackBuffer buf;
	buf.vput(fmt, ap);
	printf("%d-%.*s\r\n", code, buf.getLength(), (const char*) buf);
	fflush(stdout);

	if (TRACE(PROTOCOL))
	    logDebug("<--- %d-%.*s", code, buf.getLength(), (const char*) buf);
    }
}

/* Format and send reply containing system error number. */
void
HylaFAXServer::perror_reply(int code, const char* string, int errnum)
{
    reply(code, "%s: %s.", string, strerror(errnum));
}

void
HylaFAXServer::ack(int code, const char* s)
{
    reply(code, "%s command successful.", s);
}

struct tm*
HylaFAXServer::cvtTime(const time_t& t) const
{
    return IS(USEGMT) ? gmtime(&t) : localtime(&t);
}

u_int HylaFAXServer::getJobNumber(fxStr& emsg)
    { return (Sequence::getNext(FAX_SENDDIR "/" FAX_SEQF, emsg)); }
u_int HylaFAXServer::getDocumentNumber(fxStr& emsg)
    { return (Sequence::getNext(FAX_DOCDIR "/" FAX_SEQF, emsg)); }

void
HylaFAXServer::sanitize(fxStr& s)
{
    for (u_int i = 0; i < s.length(); i++)
	if (!isascii(s[i]) || !isprint(s[i]))
	    s[i] = '?';
}

/*
 * Convert modem name from canonical format back
 * to a pathname by replacing '_'s with '/'s.
 */
void
HylaFAXServer::canonModem(fxStr& s)
{
    u_int l = 0;
    while ((l = s.next(l, '_')) < s.length())
	s[l] = '/';
}

/*
 * Convert pathname to a device ID by
 * replacing '/'s with '_'s.
 */
void
HylaFAXServer::canonDevID(fxStr& s)
{
    u_int l = 0;
    while ((l = s.next(l, '/')) < s.length())
	s[l] = '_';
}

/*
 * Configuration support.
 */

void
HylaFAXServer::resetConfig()
{
    FaxConfig::resetConfig();
    setupConfig();
}

#define	N(a)	(sizeof (a) / sizeof (a[0]))

HylaFAXServer::stringtag HylaFAXServer::strings[] = {
{ "logfacility",	&HylaFAXServer::logFacility,	LOG_FAX },
{ "faxcontact",		&HylaFAXServer::faxContact,	"FaxMaster" },
{ "useraccessfile",	&HylaFAXServer::userAccessFile,	"/" FAX_PERMFILE },
{ "shutdownfile",	&HylaFAXServer::shutdownFile,	"/etc/shutdown" },
{ "xferfaxlogfile",	&HylaFAXServer::xferfaxLogFile,	"/etc/clientlog" },
{ "jobfmt",		&HylaFAXServer::jobFormat,
  "%-4j %3i %1a %6.6o %-12.12e %5P %5D %7z %.25s" },
{ "rcvfmt",		&HylaFAXServer::recvFormat,
  "%-7m %4p%1z %-8.8o %14.14s %7t %f" },
{ "modemfmt",		&HylaFAXServer::modemFormat,	"Modem %m (%n): %s" },
{ "filefmt",		&HylaFAXServer::fileFormat,
  "%-7p %3l %8o %8s %-12.12m %.48f" },
{ "faxqfifoname",	&HylaFAXServer::faxqFIFOName,	"/" FAX_FIFO },
{ "systemtype",		&HylaFAXServer::systemType,
  "UNIX Type: L8 Version: SVR4" },
};
HylaFAXServer::numbertag HylaFAXServer::numbers[] = {
{ "servertracing",	&HylaFAXServer::tracingLevel,		TRACE_SERVER },
{ "idletimeout",	&HylaFAXServer::idleTimeout,		900 },
{ "maxidletimeout",	&HylaFAXServer::maxIdleTimeout,		7200 },
{ "maxloginattempts",	&HylaFAXServer::maxLoginAttempts,	5 },
{ "maxadminattempts",	&HylaFAXServer::maxAdminAttempts,	5 },
{ "maxconsecutivebadcmds",&HylaFAXServer::maxConsecutiveBadCmds,10 },
};

void
HylaFAXServer::setupConfig()
{
    int i;

    for (i = N(strings)-1; i >= 0; i--)
	(*this).*strings[i].p = (strings[i].def ? strings[i].def : "");
    for (i = N(numbers)-1; i >= 0; i--)
	(*this).*numbers[i].p = numbers[i].def;
    faxContact.append("@" | hostname);
}

void
HylaFAXServer::configError(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogError(fmt, ap);
    va_end(ap);
}

void
HylaFAXServer::configTrace(const char* fmt, ...)
{
    if (TRACE(CONFIG)) {
	va_list ap;
	va_start(ap, fmt);
	vlogError(fmt, ap);
	va_end(ap);
    }
}

bool
HylaFAXServer::setConfigItem(const char* tag, const char* value)
{
    u_int ix;
    if (findTag(tag, (const tags*) strings, N(strings), ix)) {
	(*this).*strings[ix].p = value;
	switch (ix) {
	case 0:	setLogFacility(logFacility); break;
	}
    } else if (findTag(tag, (const tags*) numbers, N(numbers), ix)) {
	(*this).*numbers[ix].p = getNumber(value);
    } else
	return (false);
    return (true);
}
#undef N
