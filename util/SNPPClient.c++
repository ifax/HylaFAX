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
#include "config.h"
#include "Sys.h"
#include "SNPPClient.h"
#include <pwd.h>
#include <ctype.h>
#include <sys/types.h>
#include <errno.h>

#define	N(a)	(sizeof (a) / sizeof (a[0]))

SNPPClient::SNPPClient()
{
    init();
}

SNPPClient::SNPPClient(const fxStr& hostarg)
{
    init();
    setupHostModem(hostarg);
}

SNPPClient::SNPPClient(const char* hostarg)
{
    init();
    setupHostModem(hostarg);
}

void
SNPPClient::init()
{
    jobs = new SNPPJobArray;
    fdIn = NULL;
    fdOut = NULL;
    state = 0;
    msg = NULL;

    setupConfig();
}

void
SNPPClient::initServerState(void)
{
}

SNPPClient::~SNPPClient()
{
    (void) hangupServer();
    delete jobs;
    delete msg;
}

void
SNPPClient::printError(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintError(fmt, ap);
    va_end(ap);
}
void
SNPPClient::vprintError(const char* fmt, va_list ap)
{
    vfprintf(stderr, fmt, ap);
    fputs("\n", stderr);
}

void
SNPPClient::printWarning(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintWarning(fmt, ap);
    va_end(ap);
}
void
SNPPClient::vprintWarning(const char* fmt, va_list ap)
{
    fprintf(stderr, _("Warning, "));
    vfprintf(stderr, fmt, ap);
    fputs("\n", stderr);
}

void
SNPPClient::traceServer(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vtraceServer(fmt, ap);
    va_end(ap);
}
void
SNPPClient::vtraceServer(const char* fmt, va_list ap)
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
 */
void
SNPPClient::setupHostModem(const fxStr& s)
{
    u_int pos = s.next(0, '@');
    if (pos != s.length()) {
	modem = s.head(pos);
	host = s.tail(s.length() - (pos+1));
    } else
	host = s;
    pos = host.next(0, ':');
    if (pos != host.length()) {
	port = atoi(host.tail(host.length() - (pos+1)));
	host.resize(pos);
    }
}
void SNPPClient::setupHostModem(const char* cp) { setupHostModem(fxStr(cp)); }

void SNPPClient::setHost(const fxStr& hostarg)	{ setupHostModem(hostarg); }
void SNPPClient::setHost(const char* hostarg)	{ setupHostModem(hostarg); }
void SNPPClient::setPort(int p)			{ port = p; }
void SNPPClient::setProtoName(const char* s)	{ proto = s; }

void SNPPClient::setModem(const fxStr& modemarg){ modem = modemarg; }
void SNPPClient::setModem(const char* modemarg)	{ modem = modemarg; }

void
SNPPClient::setVerbose(bool v)
{
    if (v)
	state |= SS_VERBOSE;
    else
	state &= ~SS_VERBOSE;
}

/*
 * Setup the sender's identity.
 */
bool
SNPPClient::setupSenderIdentity(fxStr& emsg)
{
    setupUserIdentity(emsg);			// client identity

    if (from != "") {
	u_int l = from.next(0, '<');
	if (l == from.length()) {
	    l = from.next(0, '(');
	    if (l != from.length()) {		// joe@foobar (Joe Schmo)
		setBlankMailboxes(from.head(l));
		l++, senderName = from.token(l, ')');
	    } else {				// joe
		setBlankMailboxes(from);
		if (from != getUserName())
		    senderName = "";
	    }
	} else {				// Joe Schmo <joe@foobar>
	    senderName = from.head(l);
	    l++, setBlankMailboxes(from.token(l, '>'));
	}
	if (senderName == "" && getNonBlankMailbox(senderName)) {
	    /*
	     * Mail address, but no "real name"; construct one from
	     * the account name.  Do this by first stripping anything
	     * to the right of an '@' and then stripping any leading
	     * uucp patch (host!host!...!user).
	     */
	    senderName.resize(senderName.next(0, '@'));
	    senderName.remove(0, senderName.nextR(senderName.length(), '!'));
	}

	// strip and leading&trailing white space
	senderName.remove(0, senderName.skip(0, " \t"));
	senderName.resize(senderName.skipR(senderName.length(), " \t"));
    } else {
	setBlankMailboxes(getUserName());
    }
    fxStr mbox;
    if (senderName == "" || !getNonBlankMailbox(mbox)) {
	emsg = _("Malformed (null) sender name or mail address");
	return (false);
    } else
	return (true);
}
void SNPPClient::setFromIdentity(const char* s)		{ from = s; }

/*
 * Assign the specified string to any unspecified email
 * addresses used for notification mail.
 */
void
SNPPClient::setBlankMailboxes(const fxStr& s)
{
    for (u_int i = 0, n = jobs->length(); i < n; i++) {
	SNPPJob& job = (*jobs)[i];
	if (job.getMailbox() == "")
	    job.setMailbox(s);
    }
}

/*
 * Return the first non-null mailbox string
 * in the set of jobs.
 */
bool
SNPPClient::getNonBlankMailbox(fxStr& s)
{
    for (u_int i = 0, n = jobs->length(); i < n; i++) {
	SNPPJob& job = (*jobs)[i];
	if (job.getMailbox() != "") {
	    s = job.getMailbox();
	    return (true);
	}
    }
    return (false);
}

bool
SNPPClient::setupUserIdentity(fxStr& emsg)
{
    struct passwd* pwd;

    pwd = getpwuid(getuid());
    if (!pwd) {
	emsg = fxStr::format(
	    _("Can not locate your password entry (uid %lu): %s."),
		(u_long) getuid(), strerror(errno));
	return (false);
    }
    userName = pwd->pw_name;
    if (pwd->pw_gecos && pwd->pw_gecos[0] != '\0') {
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
	emsg = _("Bad (null) user name; your password file entry"
	    " probably has bogus GECOS field information.");
	return (false);
    } else
	return (true);
}

void
SNPPClient::setPagerMsg(const char* v)
{
    delete msg;
    msg = new fxStr(v);
    msgFile ="";
}

void
SNPPClient::setPagerMsgFile(const char* v)
{
    msgFile = v;
    delete msg;
}

/*
 * Configuration file support.
 */

SNPPClient::S_stringtag SNPPClient::strings[] = {
{ "protocol",			&SNPPClient::proto,	SNPP_PROTONAME },
{ "host",			&SNPPClient::host,	NULL },
{ "modem",			&SNPPClient::modem,	NULL },
};
SNPPClient::S_numbertag SNPPClient::numbers[] = {
{ "port",			&SNPPClient::port,	(u_int) -1 },
};

/*
 * Configuration file support.
 */
#define	N(a)	(sizeof (a) / sizeof (a[0]))

void
SNPPClient::setupConfig()
{
    int i;

    for (i = N(strings)-1; i >= 0; i--)
	(*this).*strings[i].p = (strings[i].def ? strings[i].def : "");
    for (i = N(numbers)-1; i >= 0; i--)
	(*this).*numbers[i].p = numbers[i].def;
    initServerState();

    jproto.setQueued(SNPP_DEFQUEUE);
    jproto.setNotification(SNPP_DEFNOTIFY);
    jproto.setHoldTime(0);		// immediate delivery
    jproto.setRetryTime((u_int) -1);
    jproto.setMaxTries(SNPP_DEFRETRIES);
    jproto.setMaxDials(SNPP_DEFREDIALS);
    jproto.setServiceLevel(SNPP_DEFLEVEL);
    jproto.setMailbox("");
}

void
SNPPClient::resetConfig()
{
    setupConfig();
}

void
SNPPClient::configError(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintError(fmt, ap);
    va_end(ap);
}

void
SNPPClient::configTrace(const char* fmt ...)
{
    if (getVerbose()) {
	va_list ap;
	va_start(ap, fmt);
	vprintWarning(fmt, ap);
	va_end(ap);
    }
}

bool
SNPPClient::setConfigItem(const char* tag, const char* value)
{
    u_int ix;
    if (findTag(tag, (const tags*) strings, N(strings), ix)) {
	(*this).*strings[ix].p = value;
    } else if (findTag(tag, (const tags*) numbers, N(numbers), ix)) {
	(*this).*numbers[ix].p = getNumber(value);
    } else if (streq(tag, "verbose")) {
	if (getBoolean(value))
	    state |= SS_VERBOSE;
	else
	    state &= ~SS_VERBOSE;
    } else if (streq(tag, "queuesend")) {
	jproto.setQueued(getBoolean(value));
    } else if (streq(tag, "notify") || streq(tag, "notification")) {
	jproto.setNotification(value);
    } else if (streq(tag, "holdtime")) {
	fxStr emsg;
	if (!jproto.setHoldTime(tag, emsg))
	    configError(_("Invalid hold time \"%s\": %s"),
		value, (const char*) emsg);
    } else if (streq(tag, "retrytime")) {
	jproto.setRetryTime(value);
    } else if (streq(tag, "maxtries")) {
	jproto.setMaxTries(getNumber(value));
    } else if (streq(tag, "maxdials")) {
	jproto.setMaxDials(getNumber(value));
    } else if (streq(tag, "servicelevel")) {
	jproto.setServiceLevel(getNumber(value));
    } else if (streq(tag, "mailaddr")) {
	jproto.setMailbox(value);
    } else
	return (false);
    return (true);
}

bool
SNPPClient::callServer(fxStr& emsg)
{
    if (host.length() == 0) {		// if host not specified by -h
	const char* cp = getenv("SNPPSERVER");
	if (cp && *cp != '\0') {
	    if (modem != "") {		// don't clobber specified modem
		fxStr m(modem);
		setupHostModem(cp);
		modem = m;
	    } else
		setupHostModem(cp);
	} else				// use default host
	    host = SNPP_DEFHOST;
    }
    if (callInetServer(emsg)) {
	signal(SIGPIPE, fxSIGHANDLER(SIG_IGN));
	/*
	 * Transport code is expected to call back through
	 * setCtrlFds so fdIn should be properly setup...
	 */
	return (fdIn != NULL && getReply(false) == COMPLETE);
    } else
	return (false);
}

#if CONFIG_INETTRANSPORT
#include "Socket.h"

extern "C" {
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netdb.h>
}

bool
SNPPClient::callInetServer(fxStr& emsg)
{
    fxStr proto(getProtoName());
    char* cp;
    if ((cp = getenv("SNPPSERVICE")) && *cp != '\0') {
	fxStr s(cp);
	u_int l = s.next(0,'/');
	port = (u_int) (int) s.head(l);
	if (l < s.length())
	    proto = s.tail(s.length()-(l+1));
    }
    struct hostent* hp = Socket::gethostbyname(getHost());
    if (!hp) {
	emsg = getHost() | _(": Unknown host");
	return (false);
    }
    int protocol;
    const char* cproto = proto;			// XXX for busted include files
    struct protoent* pp = getprotobyname(cproto);
    if (!pp) {
	printWarning(_("%s: No protocol definition, using default."), cproto);
	protocol = 0;
    } else
	protocol = pp->p_proto;
    int fd = socket(hp->h_addrtype, SOCK_STREAM, protocol);
    if (fd < 0) {
	emsg = _("Can not create socket to connect to server.");
	return (false);
    }
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof (sin));
    sin.sin_family = hp->h_addrtype;
    if (port == (u_int) -1) {
	struct servent* sp = getservbyname(SNPP_SERVICE, cproto);
	if (!sp) {
	    if (!isdigit(cproto[0])) {
		printWarning(
		    _("No \"%s\" service definition, using default %u/%s."),
		    SNPP_SERVICE, SNPP_DEFPORT, cproto);
		sin.sin_port = htons(SNPP_DEFPORT);
	    } else
		sin.sin_port = htons(atoi(cproto));
	} else
	    sin.sin_port = sp->s_port;
    } else
	sin.sin_port = htons(port);
    for (char** cpp = hp->h_addr_list; *cpp; cpp++) {
	memcpy(&sin.sin_addr, *cpp, hp->h_length);
	if (getVerbose())
	    traceServer(_("Trying %s (%s) at port %u..."),
		(const char*) getHost(),
		inet_ntoa(sin.sin_addr),
		ntohs(sin.sin_port));
	if (Socket::connect(fd, &sin, sizeof (sin)) >= 0) {
	    if (getVerbose())
		traceServer(_("Connected to %s."), hp->h_name);
#if defined(IP_TOS) && defined(IPTOS_LOWDELAY)
	    int tos = IPTOS_LOWDELAY;
	    if (Socket::setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0)
		printWarning(_("setsockopt(TOS): %s (ignored)"),
		    strerror(errno));
#endif
#ifdef SO_OOBINLINE
	    int on = 1;
	    if (Socket::setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &on, sizeof (on)) < 0)
		printWarning(_("setsockopt(OOBLINE): %s (ignored)"),
		    strerror(errno));
#endif
	    setCtrlFds(fd, fd);
	    return (true);
	}
    }
    emsg = fxStr::format(_("Can not reach server at host \"%s\", port %u."),
	(const char*) getHost(), ntohs(sin.sin_port));
    close(fd), fd = -1;
    return (false);
}
#else
bool
SNPPServer::callInetServer(fxStr& emsg)
{
    emsg = _("Sorry, no TCP/IP communication support was configured.");
    return (false);
}
#endif

bool
SNPPClient::hangupServer(void)
{
    if (fdIn != NULL)
	fclose(fdIn), fdIn = NULL;
    if (fdOut != NULL)
	fclose(fdOut), fdOut = NULL;
    initServerState();
    return (true);
}

void
SNPPClient::setCtrlFds(int in, int out)
{
    if (fdIn != NULL)
	fclose(fdIn);
    fdIn = fdopen(in, "r");
    if (fdOut != NULL)
	fclose(fdOut);
    fdOut = fdopen(out, "w");
}

/*
 * Do login procedure.
 */
bool
SNPPClient::login(const char* user, fxStr& emsg)
{
    if (user == NULL) {
	setupSenderIdentity(emsg);		// invokes setupUserIdentity
	user = userName;
    }
    int n = command("LOGI %s", user);
    if (code == 550)
	n = command("LOGI %s %s", user, getPasswd("Password:"));
    if (n == COMPLETE)
	state |= SS_LOGGEDIN;
    else
	state &= ~SS_LOGGEDIN;
    if (isLoggedIn()) {
	if (command("SITE HELP NOTIFY") == COMPLETE)
	    state |= SS_HASSITE;
	else
	    state &= ~SS_HASSITE;
	return (true);
    } else {
	emsg = _("Login failed: ") | lastResponse;
	return (false);
    }
}

/*
 * Prompt for a password.
 */
const char*
SNPPClient::getPasswd(const char* prompt)
{
    return (getpass(prompt));
}

void
SNPPClient::lostServer(void)
{
    printError(_("Service not available, remote server closed connection"));
    hangupServer();
}

void
SNPPClient::unexpectedResponse(fxStr& emsg)
{
    emsg = _("Unexpected server response: ") | lastResponse;
}

void
SNPPClient::protocolBotch(fxStr& emsg, const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    emsg = _("Protocol botch") | fxStr::vformat(fmt, ap);
    va_end(ap);
}

/*
 * Send a command and wait for a response.
 * The primary response code is returned.
 */
int
SNPPClient::command(const char* fmt ...)
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
SNPPClient::vcommand(const char* fmt, va_list ap)
{
    if (getVerbose()) {
	if (strncasecmp("LOGI", fmt, 4) == 0)
	    traceServer("-> LOGI XXXX");
	else {
        fxStr f("-> ");
        f.append(fmt);
	    vtraceServer(f, ap);
	}
    }
    if (fdOut == NULL) {
	printError(_("No control connection for command"));
	code = -1;
	return (0);
    }
    vfprintf(fdOut, fmt, ap);
    fputs("\r\n", fdOut);
    (void) fflush(fdOut);
    int r = getReply(strncmp(fmt, "QUIT", 4) == 0);
    return (r);
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
    if (!isdigit(cp[0]))
	return (0);
    int c = (cp[0] - '0');
    if (!isdigit(cp[1]))
	return (0);
    c = 10*c + (cp[1]-'0');
    if (!isdigit(cp[2]))
	return (0);
    c = 10*c + (cp[2]-'0');
    return ((cp[3] == ' ' || cp[3] == '-') ? c : 0);
}

/*
 * Read from the control channel until a valid reply is
 * received or the connection is dropped.  The last line
 * of the received response is left in SNPPClient::lastResponse
 * and the reply code is left in SNPPClient::code.  The
 * primary response (the first digit of the reply code)
 * is returned to the caller.  Continuation lines are
 * handled but not collected.
 */
int
SNPPClient::getReply(bool expecteof)
{
    int firstCode = 0;
    bool continuation = false;
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
	    if (c != '\r')
		lastResponse.append(c);
	}
	if (getVerbose())
	    traceServer("%s", (const char*) lastResponse);
	code = getReplyCode(lastResponse);
	if (code != 0) {			// found valid reply code
	    if (lastResponse[3] == '-') {	// continuation line
		if (firstCode == 0)		// first line of reponse
		    firstCode = code;
		continuation = true;
	    } else if (code == firstCode)	// end of continued reply
		continuation = false;
	}
    } while (continuation || code == 0);

    if (code == 421)				// server closed connection
	lostServer();
    return (code/100);
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
SNPPClient::extract(u_int& pos, const char* pattern, fxStr& result)
{
    fxStr pat(pattern);
    u_int l = lastResponse.find(pos, pat);
    if (l == lastResponse.length()) {		// try inverse-case version
	if (isupper(pattern[0]))
	    pat.lowercase();
	else
	    pat.raisecase();
	l = lastResponse.find(pos, pat);
    }
    if (l == lastResponse.length())
	return (false);
    l = lastResponse.skip(l+pat.length(), ' ');// skip white space
    result = lastResponse.extract(l, lastResponse.next(l, ' ')-l);
    if (result == "")
	return (false);
    pos = l;					// update position
    return (true);
}

/*
 * Create a new job and return its job-id if
 * parsed from the reply (this is for HylaFAX,
 * RFC 1861 says nothing about this).
 */
bool
SNPPClient::newPage(const fxStr& pin, const fxStr& passwd, fxStr& jobid, fxStr& emsg)
{
    int result;
    if (passwd != "")
	result = command("PAGE %s %s", (const char*) pin, (const char*) passwd);
    else
	result = command("PAGE %s", (const char*) pin);
    if (result == COMPLETE) {
	if (code == 250) {
	    /*
	     * If the server is hfaxd, then the response should be
	     * of the form:
	     *
	     * 250 ... jobid: xxxx.
	     *
	     * where xxxx is the ID for the new job.
	     */
	    u_int l = 0;
	    if (extract(l, "jobid:", jobid)) {
		/*
		 * Force job IDs to be numeric;
		 * this deals with servers that want to append
		 * punctuation such as ``,'' or ``.''.
		 */
		jobid.resize(jobid.skip(0, "0123456789"));
	    } else
		jobid = "unknown";
	    return (true);
	} else
	    unexpectedResponse(emsg);
    } else
	emsg = lastResponse;
    return (false);
}

SNPPJob&
SNPPClient::addJob(void)
{
    u_int ix = jobs->length();
    jobs->resize(ix+1);
    (*jobs)[ix] = jproto;
    return ((*jobs)[ix]);
}
u_int SNPPClient::getNumberOfJobs() const	{ return jobs->length(); }

SNPPJob*
SNPPClient::findJob(const fxStr& pin)
{
    for (u_int i = 0, n = jobs->length(); i < n; i++) {
	SNPPJob& job = (*jobs)[i];
	if (job.getPIN() == pin)
	    return (&job);
    }
    return (NULL);
}

void
SNPPClient::removeJob(const SNPPJob& job)
{
    u_int ix = jobs->find(job);
    if (ix != fx_invalidArrayIndex)
	jobs->remove(ix);
}

bool
SNPPClient::prepareForJobSubmissions(fxStr&)
{
    // XXX nothing to do right now
    return (true);
}

bool
SNPPClient::submitJobs(fxStr& emsg)
{
    if (!isLoggedIn()) {
	emsg = _("Not logged in to server");
	return (false);
    }
    /*
     * Construct jobs and submit them.
     */
    for (u_int i = 0, n = jobs->length(); i < n; i++) {
	SNPPJob& job = (*jobs)[i];
	if (!job.createJob(*this, emsg))
	    return (false);
	notifyNewJob(job);			// notify client
    }
    if (msgFile != "") {
	if (!sendData(msgFile, emsg))
	    return (false);
    } else if (msg) {
	if (!sendMsg(*msg, emsg))
	    return (false);
    }
    if (command("SEND") != COMPLETE) {
	emsg = lastResponse;
	return (false);
    } else
	return (true);
}

/*
 * Default notification handler for when a new job is created.
 */
void
SNPPClient::notifyNewJob(const SNPPJob& job)
{
    printf(_("destination pin %s: request id is %s for host %s\n")
	, (const char*) job.getPIN()
	, (const char*) job.getJobID()
	, (const char*) getHost()
    );
}

/*
 * Send a block of raw data on the data
 * conenction, interpreting write errors.
 */
bool
SNPPClient::sendRawData(void* buf, int cc, fxStr& emsg)
{
#ifdef __linux__
    /*
     * Linux kernel bug: can get short writes on
     * stream sockets when setup for blocking i/o.
     */
    u_char* bp = (u_char*) buf;
    for (int cnt, sent = 0; cc; sent += cnt, cc -= cnt) 
	if ((cnt = write(fileno(fdOut), bp + sent, cc)) <= 0) {
	    protocolBotch(emsg, errno == EPIPE ?
		_(" (server closed connection)") : _(" (server write error: %s)."),
		strerror(errno));
	    return (false);
	}
#else
    if (write(fileno(fdOut), buf, cc) != cc) {
	protocolBotch(emsg, errno == EPIPE ?
	    _(" (server closed connection)") : _(" (server write error: %s)."),
	    strerror(errno));
	return (false);
    }
#endif
    return (true);
}

bool
SNPPClient::sendData(int fd, fxStr& emsg)
{
    struct stat sb;
    (void) Sys::fstat(fd, sb);
    if (getVerbose())
	traceServer(_("SEND message data, %lu bytes"), (u_long) sb.st_size);
    if (command("DATA") == CONTINUE) {
	size_t cc = (size_t) sb.st_size;
	while (cc > 0) {
	    char buf[32*1024];
	    size_t n = fxmin(cc, sizeof (buf));
	    if (read(fd, buf, n) != (ssize_t) n) {
		protocolBotch(emsg, _(" (data read: %s)."), strerror(errno));
		return (false);
	    }
	    if (!sendRawData(buf, n, emsg))
		return (false);
	    cc -= n;
	}
	if (command(".") == COMPLETE)
	    return (true);
    }
    emsg = getLastResponse();
    return (false);
}

bool
SNPPClient::sendData(const fxStr& filename, fxStr& emsg)
{
    bool ok = false;
    int fd = Sys::open(filename, O_RDONLY);
    if (fd >= 0) {
	ok = sendData(fd, emsg);
	Sys::close(fd);
    } else
	emsg = fxStr::format(_("Unable to open message file \"%s\"."),
	    (const char*) filename);
    return (ok);
}

bool
SNPPClient::sendMsg(const char* msg, fxStr& emsg)
{
    if (command("MESS %s", msg) != COMPLETE) {
	emsg = getLastResponse();
	return (false);
    } else
	return (true);
}

bool
SNPPClient::siteParm(const char* name, const fxStr& value)
{
    if (!hasSiteCmd()) {
	printWarning(_("no SITE %s support; ignoring set request."), name);
	return (true);
    } else
	return (command("SITE %s %s", name, (const char*) value) == COMPLETE);
}

bool
SNPPClient::siteParm(const char* name, u_int value)
{
    if (!hasSiteCmd()) {
	printWarning(_("no SITE %s support; ignoring set request."), name);
	return (true);
    } else
	return (command("SITE %s %u", name, value) == COMPLETE);
}

bool
SNPPClient::setHoldTime(u_int t)
{
    time_t tv = t;
    struct tm* tm = gmtime(&tv);
    return (command("HOLD %02d%02d%02d%02d%02d"
	, (tm->tm_year) % 100
	, tm->tm_mon+1
	, tm->tm_mday
	, tm->tm_hour
	, tm->tm_min) == COMPLETE);
}

bool
SNPPClient::setRetryTime(u_int t)
{
    return siteParm("RETRYTIME", fxStr::format("%02d%02d", t/60, t%60));
}
