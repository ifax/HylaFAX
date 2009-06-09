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
#include "config.h"
#include "Sys.h"
#include "FaxClient.h"
#include "Transport.h"
#include "zlib.h"
#include <pwd.h>
#include <ctype.h>
#include <sys/types.h>
#include <arpa/telnet.h>
#include <errno.h>
#if HAS_MMAP
#include <sys/mman.h>
#endif

#include "NLS.h"

#define	N(a)	(sizeof (a) / sizeof (a[0]))

FaxClient::FaxClient()
{
    init();
}

FaxClient::FaxClient(const fxStr& hostarg)
{
    init();
    setupHostModem(hostarg);
}

FaxClient::FaxClient(const char* hostarg)
{
    init();
    setupHostModem(hostarg);
}

void
FaxClient::init()
{
    transport = NULL;
    fdIn = NULL;
    fdOut = NULL;
    fdData = -1;
    state = 0;
    pasv = false;

    setupConfig();
}

void
FaxClient::initServerState(void)
{
    type = TYPE_A;
    mode = MODE_S;
    stru = STRU_F;
    format = FORM_UNKNOWN;
    curjob = "DEFAULT";
    tzone = TZ_GMT;
    jobFmt = "";
    recvFmt = "";
    state &= ~(FS_TZPEND|FS_JFMTPEND|FS_RFMTPEND|FS_MFMTPEND|FS_FFMTPEND);
}

FaxClient::~FaxClient()
{
    (void) hangupServer();
}

void
FaxClient::printError(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintError(fmt, ap);
    va_end(ap);
}
void
FaxClient::vprintError(const char* fmt, va_list ap)
{
    vfprintf(stderr, fmt, ap);
    fputs("\n", stderr);
}

void
FaxClient::printWarning(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintWarning(fmt, ap);
    va_end(ap);
}
void
FaxClient::vprintWarning(const char* fmt, va_list ap)
{
    fprintf(stderr, NLS::TEXT("Warning, "));
    vfprintf(stderr, fmt, ap);
    fputs("\n", stderr);
}

void
FaxClient::traceServer(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vtraceServer(fmt, ap);
    va_end(ap);
}
void
FaxClient::vtraceServer(const char* fmt, va_list ap)
{
    vfprintf(stdout, fmt, ap);
    fputs("\n", stdout);
}

/*
 * Host, port, and modem can be specified using
 *
 *     modem@host:port
 *
 * e.g. ttyf2@flake.asd:9999.  Alternate forms
 * are: modem@, modem@host, host, host:port.
 * IPv6 IP addresses (many :) are supported in [xx:xx::x]:port
 */
void
FaxClient::setupHostModem(const fxStr& s)
{
    u_int pos = s.next(0, '@');
    if (pos != s.length()) {
	modem = s.head(pos);
	host = s.tail(s.length() - (pos+1));
    } else
	host = s;

    if (host.length() && host[0] == '[')
    {
	host.remove(0,1);
	pos = host.next(0,']');
	if (pos == host.length())
	    printWarning(NLS::TEXT("Couldn't parse IPv6 ip address string: \"%s\""), (const char*)s);
	else
	host.remove(pos,1);
	    pos = host.next(pos, ':');
    } else
	pos = host.next(0, ':');
    if (pos != host.length()) {
	port = atoi(host.tail(host.length() - (pos+1)));
	host.resize(pos);
    }
}
void FaxClient::setupHostModem(const char* cp) { setupHostModem(fxStr(cp)); }

void FaxClient::setHost(const fxStr& hostarg)	{ setupHostModem(hostarg); }
void FaxClient::setHost(const char* hostarg)	{ setupHostModem(hostarg); }
void FaxClient::setPort(int p)			{ port = p; }
void FaxClient::setProtoName(const char* s)	{ proto = s; }

void FaxClient::setModem(const fxStr& modemarg)	{ modem = modemarg; }
void FaxClient::setModem(const char* modemarg)	{ modem = modemarg; }

void
FaxClient::setVerbose(bool v)
{
    if (v)
	state |= FS_VERBOSE;
    else
	state &= ~FS_VERBOSE;
}

bool
FaxClient::setupUserIdentity(fxStr& emsg)
{
    struct passwd* pwd = NULL;
    const char* name = getenv("FAXUSER");
    if (name)
	pwd = getpwnam(name);
    else
	pwd = getpwuid(getuid());
    if (!pwd) {
	if (name)
	    userName = name;
	else {
	    emsg = fxStr::format(NLS::TEXT("Can not locate your password entry "
		"(uid %lu): %s"), (u_long) getuid(), strerror(errno));
	    return (false);
	}
    }
    else
	userName = pwd->pw_name;
    if (pwd && pwd->pw_gecos && pwd->pw_gecos[0] != '\0') {
	senderName = pwd->pw_gecos;
	senderName.resize(senderName.next(0, '('));	// strip SysV junk
	u_int l = senderName.next(0, '&');
	if (l < senderName.length()) {
	    /*
	     * Do the '&' substitution and raise the
	     * case of the first letter of the inserted
	     * string (the usual convention...)
	     */
	    senderName.remove(l);
	    senderName.insert(userName, l);
	    if (islower(senderName[l]))
		senderName[l] = toupper(senderName[l]);
	}
	senderName.resize(senderName.next(0,','));
    } else
	senderName = userName;
    if (senderName.length() == 0) {
	emsg = NLS::TEXT("Bad (null) user name; your password file entry"
	    " probably has bogus GECOS field information.");
	return (false);
    } else
	return (true);
}

/*
 * Configuration file support.
 */

FaxClient::F_stringtag FaxClient::strings[] = {
{ "protocol",			&FaxClient::proto,		FAX_PROTONAME },
{ "host",			&FaxClient::host,		NULL },
{ "modem",			&FaxClient::modem,		NULL },
{ "jobsortfmt", 		&FaxClient::jobSFmt },
{ "rcvsortfmt", 		&FaxClient::recvSFmt },
{ "modemsortfmt", 		&FaxClient::modemSFmt },
{ "filesortfmt", 		&FaxClient::fileSFmt },
};
FaxClient::F_numbertag FaxClient::numbers[] = {
{ "port",			&FaxClient::port,		(u_int) -1 },
};

void
FaxClient::setupConfig()
{
    int i;

    for (i = N(strings)-1; i >= 0; i--)
	(*this).*strings[i].p = (strings[i].def ? strings[i].def : "");
    for (i = N(numbers)-1; i >= 0; i--)
	(*this).*numbers[i].p = numbers[i].def;
    initServerState();
}

void
FaxClient::resetConfig()
{
    setupConfig();
}

void
FaxClient::configError(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintError(fmt, ap);
    va_end(ap);
}

void
FaxClient::configTrace(const char* fmt ...)
{
    if (getVerbose()) {
	va_list ap;
	va_start(ap, fmt);
	vprintWarning(fmt, ap);
	va_end(ap);
    }
}

bool
FaxClient::setConfigItem(const char* tag, const char* value)
{
    u_int ix;
    if (findTag(tag, (const tags*)strings, N(strings), ix)) {
	(*this).*strings[ix].p = value;
    } else if (findTag(tag, (const tags*)numbers, N(numbers), ix)) {
	(*this).*numbers[ix].p = atoi(value);
    } else if (streq(tag, "verbose")) {
	if (getBoolean(value))
	    state |= FS_VERBOSE;
	else
	    state &= ~FS_VERBOSE;
    } else if (streq(tag, "timezone") || streq(tag, "tzone")) {
	setTimeZone(streq(value, "local") ? TZ_LOCAL : TZ_GMT);
    } else if (streq(tag, "jobfmt")) {
	setJobStatusFormat(value);
    } else if (streq(tag, "rcvfmt")) {
	setRecvStatusFormat(value);
    } else if (streq(tag, "modemfmt")) {
	setModemStatusFormat(value);
    } else if (streq(tag, "filefmt")) {
	setFileStatusFormat(value);
    } else if (streq(tag, "passivemode")) {
	pasv = getBoolean(value);
    } else
	return (false);
    return (true);
}

bool
FaxClient::callServer(fxStr& emsg)
{
    if (host.length() == 0) {		// if host not specified by -h
	const char* cp = getenv("FAXSERVER");
	if (cp && *cp != '\0') {
	    if (modem != "") {		// don't clobber specified modem
		fxStr m(modem);
		setupHostModem(cp);
		modem = m;
	    } else
		setupHostModem(cp);
	}
    }
    transport = &Transport::getTransport(*this, host);
    if (transport->callServer(emsg)) {
	signal(SIGPIPE, fxSIGHANDLER(SIG_IGN));
	/*
	 * Transport code is expected to call back through
	 * setCtrlFds so fdIn should be properly setup...
	 */
	if (fdIn == NULL)
	    return (false);
	int rep = PRELIM;
	for (int i = 0; rep == PRELIM && i < 100; i++)
	    rep = getReply(false);
	return (rep == COMPLETE);
    } else
	return (false);
}

bool
FaxClient::hangupServer(void)
{
    if (fdIn != NULL) {
	if (transport) {
	    closeDataConn();
	    (void) transport->hangupServer();
	}
	fclose(fdIn), fdIn = NULL;
    }
    if (fdOut != NULL)
	fclose(fdOut), fdOut = NULL;
    /*
     * Reset state in case another call is placed.
     */
    delete transport, transport = NULL;
    initServerState();
    return (true);
}

void
FaxClient::setCtrlFds(int in, int out)
{
    if (fdIn != NULL)
	fclose(fdIn);
    fdIn = fdopen(in, "r");
    if (fdOut != NULL)
	fclose(fdOut);
    fdOut = fdopen(out, "w");
}

void
FaxClient::setDataFd(int fd)
{
    if (fdData >= 0)
	Sys::close(fdData);
    fdData = fd;
}

/*
 * Do login procedure.
 */
bool
FaxClient::login(const char* user, fxStr& emsg)
{
    if (user == NULL) {
	setupUserIdentity(emsg);
	user = userName;
    }
    if (*user == '\0') {
	emsg = NLS::TEXT("Malformed (null) username");
	return (false);
    }
    int n = command("USER %s", user);
    if (n == CONTINUE)
	n = command("PASS %s", getPasswd("Password:"));
    if (n == CONTINUE)				// XXX not used
	n = command("ACCT %s", getPasswd("Account:"));
    if (n == COMPLETE)
	state |= FS_LOGGEDIN;
    else
	state &= ~FS_LOGGEDIN;
    if (isLoggedIn()) {
	if (state&FS_TZPEND) {
	    u_int tz = tzone;
	    tzone = 0;
	    (void) setTimeZone(tz);
	    state &= ~FS_TZPEND;
	}
	return (true);
    } else {
	emsg = NLS::TEXT("Login failed: ") | lastResponse;
	return (false);
    }
}

/*
 * Prompt for a password.
 */
const char*
FaxClient::getPasswd(const char* prompt)
{
    return (getpass(prompt));
}

/*
 * Do admin login procedure.
 */
bool
FaxClient::admin(const char* pass, fxStr& emsg)
{
    if (command("ADMIN %s", pass ? pass : getpass("Password:")) != COMPLETE) {
	emsg = NLS::TEXT("Admin failed: ") | lastResponse;
	return (false);
    } else
	return (true);
}

bool
FaxClient::setCommon(FaxParam& parm, u_int v)
{
    if (v != this->*parm.pv) {
	if (0 < v && v < parm.NparmNames) {
	    if (command("%s %s", parm.cmd, parm.parmNames[v]) != COMPLETE) {
		printError("%s", (const char*) lastResponse);
		return (false);
	    }
	} else {
	    printError(NLS::TEXT("Bad %s parameter value %u."), parm.cmd, v);
	    return (false);
	}
	this->*parm.pv = v;
    }
    return (true);
}

static const char* typeNames[] = { "", "A", "E", "I", "L" };
FaxClient::FaxParam FaxClient::typeParam =
    { "TYPE", typeNames, N(typeNames), &FaxClient::type };
bool FaxClient::setType(u_int v)	{ return setCommon(typeParam, v); }

static const char* modeNames[] = { "", "S", "B", "C", "Z" };
FaxClient::FaxParam FaxClient::modeParam =
    { "MODE", modeNames, N(modeNames), &FaxClient::mode };
bool FaxClient::setMode(u_int v)	{ return setCommon(modeParam, v); }

static const char* struNames[] = { "", "F", "R", "P", "T" };
FaxClient::FaxParam FaxClient::struParam =
    { "STRU", struNames, N(struNames), &FaxClient::stru };
bool FaxClient::setStruct(u_int v)	{ return setCommon(struParam, v); }

static const char* formNames[] = { "", "PS", "PS2", "TIFF", "PCL", "PDF" };
FaxClient::FaxParam FaxClient::formParam =
    { "FORM", formNames, N(formNames), &FaxClient::format };
bool FaxClient::setFormat(u_int v)	{ return setCommon(formParam, v); }

static const char* tzoneNames[] = { "", "GMT", "LOCAL" };
FaxClient::FaxParam FaxClient::tzoneParam =
    { "TZONE", tzoneNames, N(tzoneNames), &FaxClient::tzone };
bool
FaxClient::setTimeZone(u_int v)
{
    if (!isLoggedIn()) {		// set and mark pending accordingly
        if (0 < v && v < N(tzoneNames)) {
            tzone = v;
            if (v == TZ_GMT) state &= ~FS_TZPEND;
            else state |= FS_TZPEND;
        } else {
            printError(NLS::TEXT("Bad time zone parameter value %u."), v);
            return (false);
        }
        return (true);
    } else {				// pass directly to server
        return setCommon(tzoneParam, v);
    }
}

/*
 * Data connection support.
 *
 * Separate connections are used for data transfers.
 * The transport classes handle the work since it is
 * inherently transport-specific.  Connections are
 * setup in a 2-step process because of the need (in
 * the TCP world) to setup a listening socket prior to
 * issuing a server command that causes the data connection
 * to be established.  Thus the expected protocol is to
 * initialize (initDataConn), issue a server command,
 * then open (openDataConn); after which data can be
 * transfered over the connection.  When completed the
 * connection should be closed (closeDataConn).
 *
 * Outbound connections can be terminated simply by
 * closing the data connection.  Inbound connections
 * must be aborted with ABOR command that is sent in
 * a transport-specific way (e.g. for TCP the message
 * is sent as urgent data).  The abortDataConn interface
 * is provided for this use.
 */

bool
FaxClient::initDataConn(fxStr& emsg)
{
    closeDataConn();
    if (transport) {
        if (!transport->initDataConn(emsg)) {
            if (emsg == "") {
                emsg = NLS::TEXT("Unable to initialize data connection to server");
            }
            return (false);
        }
    }
    return (true);
}

bool
FaxClient::openDataConn(fxStr& emsg)
{
    if (transport) {
        if (!transport->openDataConn(emsg)) {
            if (emsg == "") {
		emsg = NLS::TEXT("Unable to open data connection to server");
            }
	        return (false);
        }
    }
    return (true);
}

void
FaxClient::closeDataConn(void)
{
    if (fdData >= 0) {
        transport->closeDataConn(fdData);
        fdData = -1;
    }
}

bool
FaxClient::abortDataConn(fxStr& emsg)
{
    if (fdData >= 0 && transport) {
        fflush(fdOut);
        if (!transport->abortCmd(emsg)) {
            if (emsg == "") {
		emsg = NLS::TEXT("Unable to abort data connection to server");
            }
	        return (false);
        }
#ifdef notdef
        /*
         * Flush data from data connection.
         */
        int flags = fcntl(fdData, F_GETFL, 0);
        fcntl(fdData, F_SETFL, flags | FNONBLK);
        while (Sys::read(fdData, buf, sizeof (buf)) > 0);
        fcntl(fdData, F_SETFL, flags);
#endif
        /*
         * Server should send a reply that acknowledges the
         * existing operation is aborted followed by an ack
         * of the ABOR command itself.
         */
        if (getReply(false) != TRANSIENT ||	// 4xx operation aborted
                getReply(false) != COMPLETE) {	// 2xx abort successful
            unexpectedResponse(emsg);
            return (false);
        }
    }
    return (true);
}

void
FaxClient::lostServer(void)
{
    printError(NLS::TEXT("Service not available, remote server closed connection"));
    hangupServer();
}

void
FaxClient::unexpectedResponse(fxStr& emsg)
{
    emsg = NLS::TEXT("Unexpected server response: ") | lastResponse;
}

void
FaxClient::protocolBotch(fxStr& emsg, const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    emsg = NLS::TEXT("Protocol botch") | fxStr::vformat(fmt, ap);
    va_end(ap);
}

/*
 * Send a command and wait for a response.
 * The primary response code is returned.
 */
int
FaxClient::command(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vcommand(fmt, ap);
    va_end(ap);
    return (r);
}

/*
 * Send a command and wait for a response.
 * The primary response code is returned.
 */
int
FaxClient::vcommand(const char* fmt, va_list ap)
{
    fxStr line = fxStr::vformat(fmt, ap);

    if (getVerbose()) {
        if (strncasecmp("PASS ", fmt, 5) == 0) {
            traceServer("-> PASS XXXX");
        } else if (strncasecmp("ADMIN ", fmt, 6) == 0) {
            traceServer("-> ADMIN XXXX");
        } else {
	    traceServer("-> %s", (const char*)line);
        }
    }
    if (fdOut == NULL) {
        printError(NLS::TEXT("No control connection for command"));
        code = -1;
        return (0);
    }
    fputs(line, fdOut);
    fputs("\r\n", fdOut);
    (void) fflush(fdOut);
    return (getReply(strncmp(fmt, "QUIT", 4) == 0));
}

/*
 * Extract a valid reply code from a string.
 * The code must be 3 numeric digits followed
 * by a space or ``-'' (the latter indicates
 * the server reponse is to be continued with
 * one or more lines).  If no valid reply code
 * is recognized zero is returned--this is
 * assumed to be an invalid reply code.
 */
static int
getReplyCode(const char* cp)
{
    if (!isdigit(cp[0])) return (0);
    int c = (cp[0] - '0');
    if (!isdigit(cp[1])) return (0);
    c = 10 * c + (cp[1] - '0');
    if (!isdigit(cp[2])) return (0);
    c = 10 * c + (cp[2] - '0');
    return ((cp[3] == ' ' || cp[3] == '-') ? c : 0);
}

/*
 * Read from the control channel until a valid reply is
 * received or the connection is dropped.  The last line
 * of the received response is left in FaxClient::lastResponse
 * and the reply code is left in FaxClient::code.  The
 * primary response (the first digit of the reply code)
 * is returned to the caller.  Continuation lines are
 * collected separately.
 */
int
FaxClient::getReply(bool expecteof)
{
    int firstCode = 0;
    bool continuation = false;
    lastContinuation.resize(0);
    do {
        lastResponse.resize(0);
        int c;
        while ((c = getc(fdIn)) != '\n') {
            if (c == IAC) {     // handle telnet commands
		switch (c = getc(fdIn)) {
		case WILL:
                case WONT:
		    c = getc(fdIn);
		    fprintf(fdOut, "%c%c%c", IAC, DONT, c);
                    (void) fflush(fdOut);
                    break;
                case DO:
                case DONT:
                    c = getc(fdIn);
                    fprintf(fdOut, "%c%c%c", IAC, WONT, c);
                    (void) fflush(fdOut);
                    break;
                default:
                    break;
                }
                continue;
            }
            if (c == EOF) {
		if (expecteof) {
                    code = 221;
                    return (0);
                } else {
                    lostServer();
                    code = 421;
                    return (4);
                }
            }
            if (c != '\r') lastResponse.append(c);
        }
        if (getVerbose()) {
            traceServer("%s", (const char*) lastResponse);
        }
        code = getReplyCode(lastResponse);
        if (code != 0) {			// found valid reply code
            if (lastResponse[3] == '-') {	// continuation line
                if (firstCode == 0)		// first line of reponse
                    firstCode = code;
                continuation = true;
            } else if (code == firstCode)	// end of continued reply
                continuation = false;
        }
	if (continuation) {
	    lastContinuation.append(&lastResponse[4]);
	    lastContinuation.append("\n");
	}

    } while (continuation || code == 0);

    if (code == 421) { // server closed connection
        lostServer();
    }
    return code / 100;
}

/*
 * Extract a string from a reply message.  The
 * string that is extracted is expected to follow
 * a pattern string.  The pattern is tried both
 * in the initial case and then the inverse case
 * (upper or lower depending on what the original
 * case was).  The resulting string is checked to
 * make sure that it is not null.
 */
bool
FaxClient::extract(u_int& pos, const char* pattern, fxStr& result,
    const char* cmd, fxStr& emsg)
{
    fxStr pat(pattern);
    u_int l = lastResponse.find(pos, pat);
    if (l == lastResponse.length()) {		// try inverse-case version
        if (isupper(pattern[0])) {
            pat.lowercase();
        } else {
            pat.raisecase();
        }
        l = lastResponse.find(pos, pat);
    }
    if (l == lastResponse.length()) {
        protocolBotch(emsg, NLS::TEXT(": No \"%s\" in %s response: %s"),
            pattern, cmd, (const char*) lastResponse);
        return false;
    }
    l = lastResponse.skip(l+pat.length(), ' ');// skip white space
    result = lastResponse.extract(l, lastResponse.next(l, ' ')-l);
    if (result == "") {
        protocolBotch(emsg, NLS::TEXT(": Null %s in %s response: %s"),
            pattern, cmd, (const char*) lastResponse);
        return false;
    }
    pos = l;					// update position
    return true;
}

/*
 * Create a new job and return its job-id
 * and group-id, parsed from the reply.
 */
bool
FaxClient::newJob(fxStr& jobid, fxStr& groupid, fxStr& emsg)
{
    if (command("JNEW") == COMPLETE) {
        if (code == 200) {
            /*
             * The response should be of the form:
             *
             * 200 ... jobid: xxxx groupid: yyyy.
             *
             * where xxxx is the ID for the new job and yyyy is the
             * ID of the new job's group.
             */
            u_int l = 0;
            if (extract(l, "jobid:", jobid, "JNEW", emsg) &&
                    extract(l, "groupid:", groupid, "JNEW", emsg)) {
                /*
                 * Force job and groupd IDs to be numeric;
                 * this deals with servers that want to append
                 * punctuation such as ``,'' or ``.''.
                 */
                jobid.resize(jobid.skip(0, "0123456789"));
                groupid.resize(groupid.skip(0, "0123456789"));
                curjob = jobid;
                return true;
            }
        } else {
            unexpectedResponse(emsg);
        }
    } else {
        emsg = lastResponse;
    }
    return false;
}

/*
 * Set the current job on the server.
 */
bool
FaxClient::setCurrentJob(const char* jobid)
{
    if (strcasecmp(jobid, curjob) != 0) {
        if (command("JOB %s", jobid) != COMPLETE) {
            return false;
        }
        curjob = jobid;
    }
    return true;
}

bool
FaxClient::jobParm(const char* name, const fxStr& value)
{
    /*
     * We need to quote any " marks in the string before
     * we pass it on to the raw jobParm(... const char*)
     */
    if (value.next(0,'"'))
    {
	fxStr tmp(value);
	int r = tmp.length();
	while (r > 0)
	{
	    if ( (r = tmp.nextR(r-1, '"') ) > 0 )
		tmp.insert('\\', r-1);
	}
	return jobParm(name, (const char*)tmp);
    }
    return jobParm(name, (const char*) value);
}

bool
FaxClient::jobParm(const char* name, const char* value)
{
    /*
     * if they're passing us a wrong char*, we expect
     * them to have handled any quoting requried.
     */
    return (command("JPARM %s \"%s\"", name, value) == COMPLETE);
}

bool
FaxClient::jobParm(const char* name, bool b)
{
    return (command("JPARM %s %s", name, b ? "YES" : "NO") == COMPLETE);
}

bool
FaxClient::jobParm(const char* name, u_int v)
{
    return (command("JPARM %s %u", name, v) == COMPLETE);
}

bool
FaxClient::jobParm(const char* name, float v)
{
    return (command("JPARM %s %g", name, v) == COMPLETE);
}

bool
FaxClient::jobSendTime(const struct tm tm)
{
    return (command("JPARM SENDTIME %d%02d%02d%02d%02d"
        , tm.tm_year+1900
        , tm.tm_mon+1
        , tm.tm_mday
        , tm.tm_hour
        , tm.tm_min
        ) == COMPLETE);
}

bool
FaxClient::jobLastTime(u_long tv)
{
    return (command("JPARM LASTTIME %02d%02d%02d",
        tv/(24*60*60), (tv/(60*60))%24, (tv/60)%60) == COMPLETE);
}

bool
FaxClient::jobRetryTime(u_long tv)
{
    return (command("JPARM RETRYTIME %02d%02d", tv/60, tv%60) == COMPLETE);
}

bool
FaxClient::jobCover(const char* docname)
{
    return (command("JPARM COVER %s", docname) == COMPLETE);
}

bool
FaxClient::jobDocument(const char* docname)
{
    return (command("JPARM DOCUMENT %s", docname) == COMPLETE);
}

bool
FaxClient::jobPollRequest(const char* sep, const char* pwd)
{
    return (command("JPARM POLL \"%s\" \"%s\"", sep, pwd) == COMPLETE);
}

bool
FaxClient::jobOp(const char* op, const char* jobid)
{
    return (command(jobid == curjob ? "%s" : "%s %s", op, jobid) == COMPLETE);
}
bool FaxClient::jobSubmit(const char* jobid)	{ return jobOp("JSUBM",jobid); }
bool FaxClient::jobSuspend(const char* jobid)	{ return jobOp("JSUSP",jobid); }
bool FaxClient::jobKill(const char* jobid)	{ return jobOp("JKILL",jobid); }
bool FaxClient::jobDelete(const char* jobid)	{ return jobOp("JDELE",jobid); }
bool FaxClient::jobWait(const char* jobid)	{ return jobOp("JWAIT",jobid); }

bool FaxClient::jgrpSubmit(const char* jgrpid)
    { return (command("JGSUBM %s", jgrpid) == COMPLETE); }
bool FaxClient::jgrpSuspend(const char* jgrpid)
    { return (command("JGSUSP %s", jgrpid) == COMPLETE); }
bool FaxClient::jgrpKill(const char* jgrpid)
    { return (command("JGKILL %s", jgrpid) == COMPLETE); }
bool FaxClient::jgrpWait(const char* jgrpid)
    { return (command("JGWAIT %s", jgrpid) == COMPLETE); }

bool
FaxClient::runScript(const char* filename, fxStr& emsg)
{
    bool ok = false;
    FILE* fd = fopen(filename, "r");
    if (fd != NULL) {
	ok = runScript(fd, filename, emsg);
	fclose(fd);
    } else
	emsg = fxStr::format(NLS::TEXT("Unable to open script file \"%s\"."), filename);
    return (ok);
}

bool
FaxClient::runScript(FILE* fp, const char* filename, fxStr& emsg)
{
    bool ok = false;
    struct stat sb;
    (void) Sys::fstat(fileno(fp), sb);
    char* addr;
#if HAS_MMAP
    addr = (char*) mmap(NULL, (size_t) sb.st_size, PROT_READ, MAP_SHARED, fileno(fp), 0);
    if (addr == (char*) -1) {		// revert to file reads
#endif
	addr = new char[sb.st_size];
	if (Sys::read(fileno(fp), addr, (u_int) sb.st_size) == sb.st_size)
	    ok = runScript(addr, sb.st_size, filename, emsg);
	else
	    emsg = fxStr::format(NLS::TEXT("%s: Read error: %s"),
		filename, strerror(errno));
	delete [] addr;
#if HAS_MMAP
    } else {				// send mmap'd file data
	ok = runScript(addr, sb.st_size, filename, emsg);
	munmap(addr, (size_t) sb.st_size);
    }
#endif
    return (ok);
}

bool
FaxClient::runScript(const char* script, u_long scriptLen,
    const char* filename, fxStr& emsg)
{
    u_int lineno = 0;
    while (scriptLen > 0) {
	lineno++;
	const char* ep = strchr(script, '\n');
	if (!ep)
	    ep = script+scriptLen;
	u_int cmdLen = ep-script;
	if (cmdLen > 1) {
	    if (command("%.*s", cmdLen, script) != COMPLETE) {
		emsg = fxStr::format(NLS::TEXT("%s: line %u: %s"),
		    filename, lineno, (const char*) lastResponse);
		return (false);
	    }
	}
	if (*ep == '\n')
	    ep++;
	scriptLen -= ep - script;
	script = ep;
    }
    return (true);
}

/*
 * Create a uniquely named document on the server
 * that is not removed when the server exits.
 */
bool
FaxClient::storeUnique(fxStr& docname, fxStr& emsg)
{
    return storeUnique("STOU", docname, emsg);
}
/*
 * Create a uniquely named document on the server
 * that is automatically removed when the server exits.
 */
bool
FaxClient::storeTemp(fxStr& docname, fxStr& emsg)
{
    return storeUnique("STOT", docname, emsg);
}

/*
 * Send a STOU/STOT command and parse the
 * response to get the resulting filename.
 */
bool
FaxClient::storeUnique(const char* cmd, fxStr& docname, fxStr& emsg)
{
    if (command(cmd) == PRELIM) {
	if (code == 150) {
	    /*
	     * According to RFC 1123, the response must be of the form:
	     *
	     * 150 FILE: pppp[ anything]
	     *
	     * where pppp is the unique document name chosen by the server.
	     */
	    u_int l = 0;
	    return (extract(l, "FILE:", docname, cmd, emsg));
	} else
	    unexpectedResponse(emsg);
    } else
	emsg = lastResponse;
    return (false);
}

/*
 * Create/overwrite a file on the server.
 */
bool
FaxClient::storeFile(fxStr& docname, fxStr& emsg)
{
    if (command("STOR " | docname) != PRELIM) {
	emsg = lastResponse;
	return (false);
    }
    if (code != 150) {
	unexpectedResponse(emsg);
	return (false);
    }
    return (true);
}

/*
 * Send a block of raw data on the data
 * conenction, interpreting write errors.
 */
bool
FaxClient::sendRawData(void* buf, int cc, fxStr& emsg)
{
#ifdef __linux__
    /*
     * Linux kernel bug: can get short writes on
     * stream sockets when setup for blocking i/o.
     */
    u_char* bp = (u_char*) buf;
    for (int cnt, sent = 0; cc; sent += cnt, cc -= cnt)
	if ((cnt = write(fdData, bp + sent, cc)) <= 0) {
	    protocolBotch(emsg, errno == EPIPE ?
		NLS::TEXT(" (server closed connection)") : NLS::TEXT(" (server write error: %s)."),
		strerror(errno));
	    return (false);
	}
#else
    if (write(fdData, buf, cc) != cc) {
	protocolBotch(emsg, errno == EPIPE ?
	    NLS::TEXT(" (server closed connection)") : NLS::TEXT(" (server write error: %s)."),
	    strerror(errno));
	return (false);
    }
#endif
    return (true);
}

/*
 * Send a document file using stream (uncompressed) mode
 * and the current transfer parameters.  The server-side
 * document name where data gets placed is returned.
 */
bool
FaxClient::sendData(int fd,
    bool (FaxClient::*store)(fxStr&, fxStr&), fxStr& docname, fxStr& emsg)
{
    char* addr = (char*) -1;
    struct stat sb;
    size_t cc;
    (void) Sys::fstat(fd, sb);
    if (getVerbose())
	traceServer(NLS::TEXT("SEND data, %lu bytes"), (u_long) sb.st_size);
    if (!initDataConn(emsg))
	goto bad;
    if (!setMode(MODE_S))
	goto bad;
    if (!(this->*store)(docname, emsg))
	goto bad;
    if (!openDataConn(emsg))
	goto bad;
#if HAS_MMAP
    addr = (char*) mmap(NULL, (size_t) sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == (char*) -1) {			// revert to file reads
#endif
	cc = (size_t) sb.st_size;
	while (cc > 0) {
	    char buf[32*1024];			// XXX better if page-aligned
	    size_t n = fxmin(cc, sizeof (buf));
	    if (read(fd, buf, n) != (ssize_t)n) {
		protocolBotch(emsg, NLS::TEXT(" (data read: %s)."), strerror(errno));
		goto bad;
	    }
	    if (!sendRawData(buf, n, emsg))
		goto bad;
	    cc -= n;
	}
#if HAS_MMAP
    } else if (!sendRawData(addr, (int) sb.st_size, emsg)) // send mmap'd file data
	goto bad;
#endif
    closeDataConn();
#if HAS_MMAP
    if (addr != (char*) -1)
	munmap(addr, (size_t) sb.st_size);
#endif
    return (getReply(false) == 2);
bad:
    closeDataConn();
#if HAS_MMAP
    if (addr != (char*) -1)
	munmap(addr, (size_t) sb.st_size);
#endif
    return (false);
}

/*
 * Send a document file using zip-compressed mode
 * and the current transfer parameters.  The server-side
 * document name where data gets placed is returned.
 */
bool
FaxClient::sendZData(int fd,
    bool (FaxClient::*store)(fxStr&, fxStr&), fxStr& docname, fxStr& emsg)
{
    z_stream zstream;
    zstream.zalloc = NULL;
    zstream.zfree = NULL;
    zstream.opaque = NULL;
    zstream.data_type = Z_BINARY;
    if (deflateInit(&zstream, Z_DEFAULT_COMPRESSION) == Z_OK) {
#if HAS_MMAP
	char* addr = (char*) -1;		// mmap'd file
#endif
	char obuf[32*1024];			// XXX better if page-aligned
	zstream.next_out = (Bytef*) obuf;
	zstream.avail_out = sizeof (obuf);
	struct stat sb;
	size_t cc;
	Sys::fstat(fd, sb);
	if (getVerbose())
	    traceServer(NLS::TEXT("SEND compressed data, %lu bytes"), (u_long) sb.st_size);
	if (!initDataConn(emsg))
	    goto bad;
	if (!setMode(MODE_Z))
	    goto bad;
	if (!(this->*store)(docname, emsg))
	    goto bad;
	if (!openDataConn(emsg))
	    goto bad;
#if HAS_MMAP
	addr = (char*) mmap(NULL, (size_t) sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (addr == (char*) -1) {		// revert to file reads
#endif
	    cc = (size_t) sb.st_size;
	    while (cc > 0) {
		char buf[32*1024];
		int n = fxmin((size_t) cc, sizeof (buf));
		if (read(fd, buf, n) != n) {
		    protocolBotch(emsg, NLS::TEXT(" (data read: %s)"), strerror(errno));
		    goto bad;
		}
		zstream.next_in = (Bytef*) buf;
		zstream.avail_in = n;
		do {
		    if (deflate(&zstream, Z_NO_FLUSH) != Z_OK) {
			emsg = fxStr::format(NLS::TEXT("zlib compressor error: %s"),
			    zstream.msg);
			goto bad;
		    }
		    if (zstream.avail_out == 0) {
			if (!sendRawData(obuf, sizeof (obuf), emsg))
			    goto bad2;
			zstream.next_out = (Bytef*) obuf;
			zstream.avail_out = sizeof (obuf);
		    }
		} while (zstream.avail_in > 0);
		cc -= n;
	    }
	    zstream.avail_in = 0;
#if HAS_MMAP
	} else {
	    zstream.next_in = (Bytef*) addr;
	    zstream.avail_in = (u_int) sb.st_size;
	    do {
		if (deflate(&zstream, Z_NO_FLUSH) != Z_OK) {
		    emsg = fxStr::format(NLS::TEXT("zlib compressor error: %s"),
			zstream.msg);
		    goto bad;
		}
		if (zstream.avail_out == 0) {
		    if (!sendRawData(obuf, sizeof (obuf), emsg))
			goto bad2;
		    zstream.next_out = (Bytef*) obuf;
		    zstream.avail_out = sizeof (obuf);
		}
	    } while (zstream.avail_in > 0);
	}
#endif
	int dstate;
	do {
	    switch (dstate = deflate(&zstream, Z_FINISH)) {
	    case Z_STREAM_END:
	    case Z_OK:
		if (zstream.avail_out != sizeof (obuf)) {
		    if (!sendRawData(obuf, sizeof (obuf) - zstream.avail_out, emsg))
			goto bad2;
		    zstream.next_out = (Bytef*) obuf;
		    zstream.avail_out = sizeof (obuf);
		}
		break;
	    default:
		emsg = fxStr::format(NLS::TEXT("zlib compressor error: %s"),
		    zstream.msg);
		goto bad;
	    }
	} while (dstate != Z_STREAM_END);
	if (getVerbose())
	    traceServer(NLS::TEXT("SEND %lu bytes transmitted (%.1fx compression)"),
#define	NZ(x)	((x)?(x):1)
		zstream.total_out, float(sb.st_size) / NZ(zstream.total_out));
	closeDataConn();
#if HAS_MMAP
	if (addr != (char*) -1)
	    munmap(addr, (size_t) sb.st_size);
#endif
	deflateEnd(&zstream);
	return (getReply(false) == COMPLETE);
bad2:
	(void) getReply(false);
	/* fall thru... */
bad:
	closeDataConn();
#if HAS_MMAP
	if (addr != (char*) -1)
	    munmap(addr, (size_t) sb.st_size);
#endif
	deflateEnd(&zstream);
    } else
	emsg = fxStr::format(NLS::TEXT("Can not initialize compression library: %s"),
	    zstream.msg);
    return (false);
}

/*
 * Receive data using stream mode and the current
 * transfer parameters.  The supplied arguments are
 * passed to command to initiate the transfers once
 * a data connection has been setup.  These commands
 * can intiate a file retrieval (RETR), directory
 * listing (LIST), trigger event trace log (SITE TRIGGER)
 * or other data connection-based transfer.
 */
bool
FaxClient::recvData(bool (*f)(void*, const char*, int, fxStr&),
    void* arg, fxStr& emsg, u_long restart, const char* fmt, ...)
{
    if ((!setMode(MODE_S)) ||
	(!initDataConn(emsg)) ||
	(restart && command("REST %lu", restart) != CONTINUE)) {
	// cannot "goto bad" because it is outside the scope of a va_arg
	closeDataConn();
	return (false);
    }
    va_list ap;
    va_start(ap, fmt);
    int r; r = vcommand(fmt, ap);
    va_end(ap);
    if (r != PRELIM)
	goto bad;
    if (!openDataConn(emsg))
	goto bad;
    u_long byte_count; byte_count = 0;		// XXX for __GNUC__
    for (;;) {
	char buf[16*1024];
	int cc = read(fdData, buf, sizeof (buf));
	if (cc == 0) {
	    closeDataConn();
	    return (getReply(false) == COMPLETE);
	}
	if (cc < 0) {
	    emsg = fxStr::format(NLS::TEXT("Data Connection: %s"), strerror(errno));
	    (void) getReply(false);
	    break;
	}
	byte_count += cc;
	if (!(*f)(arg, buf, cc, emsg))
	    break;
    }
bad:
    closeDataConn();
    return (false);
}

/*
 * Receive data using zip-compressed mode and the current
 * transfer parameters.  The supplied arguments are
 * passed to command to initiate the transfers once
 * a data connection has been setup.  These commands
 * can intiate a file retrieval (RETR), directory
 * listing (LIST), trigger event trace log (SITE TRIGGER)
 * or other data connection-based transfer.
 */
bool
FaxClient::recvZData(bool (*f)(void*, const char*, int, fxStr&),
    void* arg, fxStr& emsg, u_long restart, const char* fmt, ...)
{
    z_stream zstream;
    zstream.zalloc = NULL;
    zstream.zfree = NULL;
    zstream.opaque = NULL;
    zstream.data_type = Z_BINARY;
    if (inflateInit(&zstream) == Z_OK) {
	if ((!setMode(MODE_Z)) ||
	    (!initDataConn(emsg)) ||
	    (restart && command("REST %lu", restart) != CONTINUE)) {
	    // cannot "goto bad" because it is outside the scope of a va_arg
	    closeDataConn();
	    inflateEnd(&zstream);
	    return (false);
	}
	va_list ap;
	va_start(ap, fmt);
	int r; r = vcommand(fmt, ap);		// XXX for __GNUC__
	va_end(ap);
	if (r != PRELIM)
	    goto bad;
	if (!openDataConn(emsg))
	    goto bad;
	char obuf[16*1024];
	zstream.next_out = (Bytef*) obuf;
	zstream.avail_out = sizeof (obuf);
	for (;;) {
	    char buf[16*1024];
	    int cc = read(fdData, buf, sizeof (buf));
	    if (cc == 0) {
		size_t occ = sizeof (obuf) - zstream.avail_out;
		if (occ > 0 && !(*f)(arg, obuf, occ, emsg))
		    goto bad;
		closeDataConn();
		(void) inflateEnd(&zstream);
		return (getReply(false) == COMPLETE);
	    }
	    if (cc < 0) {
		emsg = fxStr::format(NLS::TEXT("Data Connection: %s"), strerror(errno));
		(void) getReply(false);
		goto bad;
	    }
	    zstream.next_in = (Bytef*) buf;
	    zstream.avail_in = cc;
	    do {
		int dstate = inflate(&zstream, Z_PARTIAL_FLUSH);
		if (dstate == Z_STREAM_END)
		    break;
		if (dstate != Z_OK) {
		    emsg = fxStr::format(NLS::TEXT("Decoding error: %s"), zstream.msg);
		    goto bad;
		}
		size_t occ = sizeof (obuf) - zstream.avail_out;
		if (!(*f)(arg, obuf, occ, emsg))
		    goto bad;
		zstream.next_out = (Bytef*) obuf;
		zstream.avail_out = sizeof (obuf);
	    } while (zstream.avail_in > 0);
	}
bad:
	closeDataConn();
	inflateEnd(&zstream);
    } else
	emsg = fxStr::format(NLS::TEXT("Can not initialize decoder: %s"), zstream.msg);
    return (false);
}

/*
 * Return the current value for the specified
 * status format string.  If we have not set a
 * value locally, ask the server for the default
 * setting.
 */
const fxStr&
FaxClient::getStatusFormat(u_int flag, const char* cmd, fxStr& fmt)
{
    if (isLoggedIn()) {
	if (state&flag) {		// set pending; do it
	    if (command("%s \"%s\"", cmd, (const char*) fmt) == COMPLETE)
		state &= ~flag;
	    else
		printError("%s", (const char*) lastResponse);
	} else if (fmt == "") {		// must query server
	    if (command(cmd) == COMPLETE)
		fmt = lastResponse.tail(lastResponse.length()-4);
	    else
		printError("%s", (const char*) lastResponse);
	}
    }
    return (fmt);
}

/*
 * Set the specified status format string
 *  in the client and the server.
 */
bool
FaxClient::setStatusFormat(const char* cmd, u_int flag,
    fxStr& fmt, const char* value)
{
    if (isLoggedIn()) {
	if (command("%s \"%s\"", cmd, value) != COMPLETE) {
	    printError("%s", (const char*) lastResponse);
	    return (false);
	}
	state &= ~flag;
    } else
	state |= flag;
    fmt = value;
    return (true);
}

/*
 * Set the job status format string in the
 * client and the server.
 */
bool
FaxClient::setJobStatusFormat(const char* cp)
{
    return setStatusFormat("JOBFMT", FS_JFMTPEND, jobFmt, cp);
}

/*
 * Return the current job status format string.
 * If we have not set a value locally, ask the
 * server for the default setting.
 */
const fxStr&
FaxClient::getJobStatusFormat(void)
{
    return getStatusFormat(FS_JFMTPEND, "JOBFMT", jobFmt);
}

/*
 * Set the receive queue status format
 * string in the client and the server.
 */
bool
FaxClient::setRecvStatusFormat(const char* cp)
{
    return setStatusFormat("RCVFMT", FS_RFMTPEND, recvFmt, cp);
}

/*
 * Return the current recv queue status format
 * string.  If we have not set a value locally,
 * ask the server for the default setting.
 */
const fxStr&
FaxClient::getRecvStatusFormat(void)
{
    return getStatusFormat(FS_RFMTPEND, "RCVFMT", recvFmt);
}


/*
 * Set the modem status format
 * string in the client and the server.
 */
bool
FaxClient::setModemStatusFormat(const char* cp)
{
    return setStatusFormat("MDMFMT", FS_MFMTPEND, modemFmt, cp);
}

/*
 * Return the current modem status format
 * string.  If we have not set a value locally,
 * ask the server for the default setting.
 */
const fxStr&
FaxClient::getModemStatusFormat(void)
{
    return getStatusFormat(FS_MFMTPEND, "MDMFMT", modemFmt);
}

/*
 * Set the file status format
 * string in the client and the server.
 */
bool
FaxClient::setFileStatusFormat(const char* cp)
{
    return setStatusFormat("FILEFMT", FS_FFMTPEND, fileFmt, cp);
}

/*
 * Return the current file status format
 * string.  If we have not set a value locally,
 * ask the server for the default setting.
 */
const fxStr&
FaxClient::getFileStatusFormat(void)
{
    return getStatusFormat(FS_FFMTPEND, "FILEFMT", fileFmt);
}

/*
 * Convert a format string to a header using a table
 * that maps format character to field header.
 */
void
FaxClient::makeHeader(const char* fmt, const FaxFmtHeader fmts[], fxStr& header)
{
    for (const char* cp = fmt; *cp; cp++) {
	if (*cp == '%') {
	    u_int width = 0;		// field width
	    u_int prec = 0;		// field precision
#define	MAXSPEC	20
	    char fspec[MAXSPEC];
	    char* fp = fspec;
	    *fp++ = '%';
	    char c = *++cp;
	    if (c == '\0')
		break;
	    if (c == '-')
		*fp++ = c, c = *++cp;
	    if (isdigit(c)) {
		do {
		    *fp++ = c;
		    width = 10*width + (c-'0');
		} while (isdigit(c = *++cp) && fp < &fspec[MAXSPEC-3]);
	    }
	    if (c == '.') {
		do {
		    *fp++ = c;
		    prec = 10*prec + (c-'0');
		} while (isdigit(c = *++cp) && fp < &fspec[MAXSPEC-2]);
	    }
	    if (c == '%') {		// %% -> %
		header.append(c);
		continue;
	    }
	    const FaxFmtHeader* hp;
	    for (hp = fmts; hp->fmt != '\0' && hp->fmt != c; hp++)
		;
	    if (hp->fmt == c) {
		if (prec == 0)		// constrain header to field width
		    prec = width;
		if (fspec[1] == '-')	// left justify
		    width = -width;
		if (width == 0 && prec == 0)
		    header.append(NLS::TEXT(hp->title));
		else
		    header.append(fxStr::format("%*.*s", width, prec, NLS::TEXT(hp->title)));
	    } else {
		*fp++ = c;
		header.append(fxStr(fspec, fp-fspec));
	    }
	} else
	    header.append(*cp);
    }
}

/*
 * Table of known format strings for the job
 * queue status listings returned by the server.
 */
const FaxClient::FaxFmtHeader FaxClient::jobFormats[] = {
    /* translator: Column header for Recipient sub-address */				{ 'A',	N_("SUB") },
    /* translator: Column header for Recipient password (of sub-address) */		{ 'B',	N_("PWD") },
    /* translator: Column header for Recipient company name */				{ 'C',	N_("Company") },
    /* translator: Column header for (total dials):(maximum dials) */			{ 'D',	N_("Dials") },
    /* translator: Column header for signalling rate */					{ 'E',	N_("BR") },
    /* translator: Column header for job tagline */					{ 'F',	N_("Tagline") },
    /* translator: Column header for min-scanline-time */				{ 'G',	N_("ST") },
    /* translator: Column header for data format */					{ 'H',	N_("DF") },
    /* translator: Column header for user-requested scheduling priority */		{ 'I',	N_("UsrPri") },
    /* translator: Column header for user specified job tag */				{ 'J',	N_("JobTag") },
    /* translator: Column header for ECM (symbol) */					{ 'K',	N_("EC") },
    /* translator: Column header for recipient location */				{ 'L',	N_("Location") },
    /* translator: Column header for mail address */					{ 'M',	N_("MailAddr") },
    /* translator: Column header for tagline handling (symbol) */			{ 'N',	N_("DT") },
    /* translator: Column header for continuation cover page use (symbol) */		{ 'O',	N_("CC") },
    /* translator: Column header for (number of pages):(total pages) */			{ 'P',	N_("Pages") },
    /* translator: Column header for minimum speed */					{ 'Q',	N_("MinSP") },
    /* translator: Column header for receiver name */					{ 'R',	N_("Receiver") },
    /* translator: Column header for sender name */					{ 'S',	N_("Sender") },
    /* translator: Column header for (total tries):(maximum tries) */			{ 'T',	N_("Tries") },
    /* translator: Column header for minimum white space before chopping */		{ 'U',	N_("ChopThreshold") },
    /* translator: Column header for operation to do when job completes */		{ 'V',	N_("DoneOp") },
    /* translator: Column header for communication ID */				{ 'W',	N_("CommID") },
    /* translator: Column header for job type */					{ 'X',	N_("JobType") },
    /* translator: Column header for Date & Time */					{ 'Y',	N_("Date       Time") },
    /* translator: Column header for seconds since the UNIX epoch */			{ 'Z',	N_("UNIX Time") },
    /* translator: Column header for job state (symbol) */				{ 'a',	N_("State") },
    /* translator: Column header for number of tries */					{ 'b',	N_("NTries") },
    /* translator: Column header for identity of machine that submitted job */		{ 'c',	N_("Client") },
    /* translator: Column header for total dials */					{ 'd',	N_("TotDials") },
    /* translator: Column header for recipient fax number */				{ 'e',	N_("Number") },
    /* translator: Column header for number of dials */					{ 'f',	N_("NDials") },
    /* translator: Column header for group identifier */				{ 'g',	N_("GID") },
    /* translator: Column header for page chopping (symbol) */				{ 'h',	N_("Chop") },
    /* translator: Column header for current scheduling priority */			{ 'i',	N_("Priority") },
    /* translator: Column header for job identifier */					{ 'j',	N_("JID") },
    /* translator: Column header for time to kill job */				{ 'k',	N_("LastTime") },
    /* translator: Column header for page length */					{ 'l',	N_("PageLength") },
    /* translator: Column header for modem group */					{ 'm',	N_("Modem") },
    /* translator: Column header for email notification (symbol) */			{ 'n',	N_("Notify") },
    /* translator: Column header for owner name */					{ 'o',	N_("Owner") },
    /* translator: Column header for number of pages */					{ 'p',	N_("Pages") },
    /* translator: Column header for retry time */					{ 'q',	N_("RetryTime") },
    /* translator: Column header for fax image vertical resolution */			{ 'r',	N_("Resolution") },
    /* translator: Column header for request status indicator */			{ 's',	N_("Status") },
    /* translator: Column header for total tries */					{ 't',	N_("TotTries") },
    /* translator: Column header for maximum tries */					{ 'u',	N_("MaxTries") },
    /* translator: Column header for dial string */					{ 'v',	N_("DialString") },
    /* translator: Column header for page width */					{ 'w',	N_("PageWidth") },
    /* translator: Column header for maximum dials */					{ 'x',	N_("MaxDials") },
    /* translator: Column header for total of pages */					{ 'y',	N_("TotPages") },
    /* translator: Column header for time to send */					{ 'z',	N_("TTS") },
    /* translator: Column header for extended vertical resolution use (symbol) */	{ '0',	N_("UseXVres") },
											{ '\0' },
};
void FaxClient::getJobStatusHeader(fxStr& header)
{
    makeHeader(getJobStatusFormat(), jobFormats, header);
   if (jobSFmt.length())
	command("JOBSORTFMT \"%s\"", (const char*)jobSFmt);
}

/*
 * Table of known format strings for the receive
 * queue status listings returned by the server.
 */
const FaxClient::FaxFmtHeader FaxClient::recvFormats[] = {
    /* translator: Column header for Date & Time */					{ 'Y',	N_("Date       Time") },
    /* translator: Column header for seconds since the UNIX epoch */			{ 'Z',	N_("UNIX Time") },
    /* translator: Column header for Recipient sub-address */				{ 'a',	N_("SUB") },
    /* translator: Column header for signalling rate */					{ 'b',	N_("BR") },
    /* translator: Column header for data format */					{ 'd',	N_("DF") },
    /* translator: Column header for error description */				{ 'e',	N_("Error") },
    /* translator: Column header for file name */					{ 'f',	N_("Filename") },
    /* translator: Column header for time spent receiving */				{ 'h',	N_("Time") },
    /* translator: Column header for callerid name */					{ 'i',	N_("CIDName") },
    /* translator: Column header for caller id number */				{ 'j',	N_("CIDNumber") },
    /* translator: Column header for page length */					{ 'l',	N_("Length") },
    /* translator: Column header for fax protection mode */				{ 'm',	N_("Protect") },
    /* translator: Column header for file size */					{ 'n',	N_("Size") },
    /* translator: Column header for job owner */					{ 'o',	N_("Owner") },
    /* translator: Column header for number of pages */					{ 'p',	N_("Pages") },
    /* translator: Column header for fax protection mode */				{ 'q',	N_("Protect") },
    /* translator: Column header for fax image resolution */				{ 'r',	N_("Resolution") },
    /* translator: Column header for sender TSI (Transmitting Station Information) */	{ 's',	N_("Sender/TSI") },
    /* translator: Column header for time received */					{ 't',	N_("Recvd@") },
    /* translator: Column header for page width */					{ 'w',	N_("Width") },
    /* ``*'' if being received */							{ 'z',	" " },
											{ '\0' },
};
void FaxClient::getRecvStatusHeader(fxStr& header)
{
   makeHeader(getRecvStatusFormat(), recvFormats, header);
   if (recvSFmt.length())
	command("RCVSORTFMT \"%s\"", (const char*)recvSFmt);
}

/*
 * Table of known format strings for the modem
 * status listings returned by the server.
 */
const FaxClient::FaxFmtHeader FaxClient::modemFormats[] = {
    /* translator: Column header for machine hostname */		{ 'h',	N_("Host") },
    /* translator: Column header for local identifier */		{ 'l',	N_("LocalID") },
    /* translator: Column header for canonical modem name */		{ 'm',	N_("Modem") },
    /* translator: Column header for fax phone number */		{ 'n',	N_("Number") },
    /* translator: Column header for maximum received pages */		{ 'r',	N_("MaxRecv") },
    /* translator: Column header for status information */		{ 's',	N_("Status") },
    /* translator: Column header for server:session tracing level */	{ 't',	N_("Tracing") },
    /* translator: Column header for speaker volume (symbol) */		{ 'v',	N_("Speaker") },
    /* ``*'' if faxgetty is running */					{ 'z',	" " },
									{ '\0' },
};
void FaxClient::getModemStatusHeader(fxStr& header)
{
    makeHeader(getModemStatusFormat(), modemFormats, header);
    if (modemSFmt.length() )
	command("MDMSORTFMT \"%s\"", (const char*)modemSFmt);

}

/*
 * Table of known format strings for the file
 * status listings returned by the server.
 */
const FaxClient::FaxFmtHeader FaxClient::fileFormats[] = {
    /* translator: Column header for file last access time */		{ 'a',	N_("LastAcc") },
    /* translator: Column header for file creation time */		{ 'c',	N_("Created") },
    /* translator: Column header for ID of device containing file */	{ 'd',	N_("Device") },
    /* translator: Column header for file name */			{ 'f',	N_("Filename") },
    /* translator: Column header for file GID */			{ 'g',	N_("GID") },
    /* translator: Column header for file link count */			{ 'l',	N_("Links") },
    /* translator: Column header for file last modification time */	{ 'm',	N_("LastMod") },
    /* translator: Column header for file owner */			{ 'o',	N_("Owner") },
    /* translator: Column header for file protection flags */		{ 'p',	N_("Protect") },
    /* translator: Column header for file protection flags */		{ 'q',	N_("Protect") },
    /* translator: Column header for ID of device if special file */	{ 'r',	N_("RootDev") },
    /* translator: Column header for file size */			{ 's',	N_("Size") },
    /* translator: Column header for file UID */			{ 'u',	N_("UID") },
									{ '\0' },
};
void FaxClient::getFileStatusHeader(fxStr& header)
{
    makeHeader(getFileStatusFormat(), fileFormats, header);
    if (fileSFmt.length() )
	command("FILESORTFMT \"%s\"", (const char*)fileSFmt);
}
