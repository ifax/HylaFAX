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

#ifdef SNPP_SUPPORT
/*
 * Simple Network Paging Protocol (SNPP) Support.
 */
#include "config.h"
#include "Sys.h"
#include "Socket.h"
#include "SNPPServer.h"
#include "Dispatcher.h"
#include "RE.h"

#include <ctype.h>
#if HAS_CRYPT_H
#include <crypt.h>
#endif

extern "C" {
#include <netdb.h>
#include <arpa/inet.h>

#include <netinet/in_systm.h>
#include <netinet/ip.h>
}

SNPPSuperServer::SNPPSuperServer(const char* p, int bl)
    : SuperServer("SNPP", bl)
    , port(p)
{}
SNPPSuperServer::~SNPPSuperServer() {}

bool
SNPPSuperServer::startServer(void)
{
    /*
     * Switch to super-user to do the bind in case
     * we are to use a port in the privileged region
     * on a BSD system (ports <=1024 are reserved
     * and the default SNPP port is 444).
     *
     * NB: We do it for both the socket+bind calls
     *     to workaround a bug in Solaris 2.5.
     */
    uid_t ouid = geteuid();
    (void) seteuid(0);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s >= 0) {
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;
	const char* cp = port;
	struct servent* sp = getservbyname(cp, SNPP_PROTONAME);
	if (!sp) {
	    if (isdigit(cp[0]))
		sin.sin_port = htons(atoi(cp));
	    else
		sin.sin_port = htons(SNPP_DEFPORT);
	} else
	    sin.sin_port = sp->s_port;
	if (Socket::bind(s, &sin, sizeof (sin)) >= 0) {
	    (void) listen(s, getBacklog());
	    (void) seteuid(ouid);
	    Dispatcher::instance().link(s, Dispatcher::ReadMask, this);
	    return (true);				// success
	}
	Sys::close(s);
	logError("HylaFAX %s: bind (port %u): %m",
	    getKind(), ntohs(sin.sin_port));
    } else
	logError("HylaFAX %s: socket: %m", getKind());
    (void) seteuid(ouid);
    return (false);
}
HylaFAXServer* SNPPSuperServer::newChild(void)	{ return new SNPPServer; }

SNPPServer::SNPPServer() {}
SNPPServer::~SNPPServer() {}

void
SNPPServer::open(void)
{
    setupNetwork(STDIN_FILENO);
    initServer();				// complete state initialization
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
    if (isShutdown(true)) {
	reply(421, "%s SNPP server unavailable; try again later.",
	    (const char*) hostname);
	dologout(-1);
    }
    reply(220, "%s SNPP server (%s) ready.", (const char*) hostname, version);
    if (TRACE(CONNECT))
        logInfo("SNPP connection from %s [%s]",
	    (const char*) remotehost, (const char*) remoteaddr);
}

void
SNPPServer::initServer(void)
{
    InetFaxServer::initServer();
    state &= ~S_USEGMT;			// SNPP uses local times
    resetState();
}

void
SNPPServer::initDefaultJob(void)
{
    InetFaxServer::initDefaultJob();

    defJob.jobtype = "pager";
    initSNPPJob();
}

void
SNPPServer::initSNPPJob(void)
{
    defJob.queued = false;		// jobs not queued--per protocol
    defJob.maxdials = SNPP_DEFREDIALS;	// configuration defaults...
    defJob.maxtries = SNPP_DEFRETRIES;
    defJob.killtime = 60*killMap[SNPP_DEFLEVEL];
    defJob.retrytime = retryMap[SNPP_DEFLEVEL];
    defJob.usrpri = priMap[SNPP_DEFLEVEL];
    // XXX default notification
}

void
SNPPServer::resetState(void)
{
    initDefaultJob();			// default job state
    msgFile = "";
    haveText = false;			// no message text yet
    msgs.resize(0);			// purge any message refs
}

void
SNPPServer::dologout(int status)
{
    /*
     * Purge any partially constructed jobs.  If we were
     * doing a SEND when the connection was dropped then
     * we do not purge jobs but instead just let them run
     * detached.  This may not be the best thing; we might
     * instead want to kill the jobs--we'll need to think
     * about this some more before changing this behaviour.
     */
    if (msgs.length() > 0 && !IS(SENDWAIT)) {
	fxStr emsg;
	for (u_int i = 0, n = msgs.length(); i < n; i++) {
	    Job* job = findJob(msgs[i], emsg);
	    if (job) {
		for (u_int j = 0, m = job->items.length(); j < m; j++)
		    if (job->items[j].op == FaxRequest::send_data)
			(void) Sys::unlink(job->items[j].item);
		(void) Sys::unlink(job->qfile);
		delete job;
	    }
	}
    }
    InetFaxServer::dologout(status);
}

static inline bool
isMagic(char c)
{
    return (c == '[' || c == ']' || c == '*' || c == '.' || c == '^' ||
	c == '$' || c == '-' || c == '+' || c == '{' || c == '}' ||
	c == '(' || c == ')');
}

/*
 * Handle \escapes for a pager ID replacement string.
 */
static void
subRHS(fxStr& result, const RE& re, const fxStr& match)
{
    /*
     * Do ``&'' and ``\n'' interpolations in the replacement.
     */
    for (u_int i = 0, n = result.length(); i < n; i++) {
	if (result[i] == '\\') {		// process \<char> escapes
	    result.remove(i), n--;
	    if (isdigit(result[i])) {
		int mn = result[i] - '0';
		int ms = re.StartOfMatch(mn);
		int mlen = re.EndOfMatch(mn) - ms;
		result.remove(i);		// delete \n
		if (ms >= 0)
		    result.insert(match.extract(ms, mlen), i);
		else
		    logError("Invalid backreference in pagermap: \\%d", mn);
		n = result.length();		// adjust string length ...
		i += mlen - 1;			// ... and scan index
	    }
	} else if (result[i] == '&') {		// process & replacement
	    int ms = re.StartOfMatch(0);
	    int mlen = re.EndOfMatch(0) - ms;
	    result.remove(i);			// delete &
	    result.insert(match.extract(ms, mlen), i);
	    n = result.length();		// adjust string length ...
	    i += mlen - 1;			// ... and scan index
	}
    }
}

/*
 * Given a client-supplied pager ID return the phone number
 * to dial of the service provider and the PIN to supply
 * to the provider when talking IXO/TAP (for aliases).
 */
bool
SNPPServer::mapPagerID(const char* pagerID, fxStr& number, fxStr& pin, fxStr& emsg)
{
    if (pagerIDMapFile != "") {
	FILE* fd = fopen(fixPathname(pagerIDMapFile), "r");
	if (fd != NULL) {
	    char buf[1024];
	    while (fgets(buf, sizeof (buf), fd)) {
		char* cp;
		for (cp = buf; isspace(*cp); cp++)	// leading whitespace
		    ;
		if (*cp == '#' || *cp == '\0')
		    continue;
		/*
		 * Syntax is:
		 *
		 * <pattern> <dialstring>[/<PIN>]
		 *
		 * where <pattern> can be a simple string of alpha
		 * numerics or a regular expression.  The first line
		 * that matches the client-specified pager ID is used.
		 * If no <PIN> is specified then the client-specified
		 * string is sent to the provider.  If <dialstring>
		 * is "reject" (verbatim) then matches are rejected.
		 * <PIN> is treated as the RHS of an RE-style substitution:
		 * \n and & escapes are replaced according to the RE
		 * matching work.
		 *
		 * Leading white space on a line is ignored.  Lines
		 * that begin with '#' are ignored (i.e. comments).
		 */
		const char* pattern = cp;
		bool isRE = false;
		for (; *cp != '\0' && !isspace(*cp); cp++)
		    if (isMagic(*cp))
			isRE = true;
		if (*cp != '\0')			// \0-term. <pattern>
		    *cp++ = '\0';
		bool match;
		RE* re;
		if (isRE) {
		    re = new RE(pattern);
		    match = re->Find(pagerID, strlen(pagerID));
		} else {
		    match = streq(pattern, pagerID);
		    re = NULL;
		}
		if (match) {
		    while (isspace(*cp))		// leading whitespace
			cp++;
		    if (*cp != '\n' && *cp != '\0') {	// got <dialstring>
			char* np = strchr(cp, '\n');	// remove trailing \n
			if (np)
			    *np = '\0';
			np = cp;
			for (; *cp && *cp != '/'; cp++)
			    ;
			if (*cp == '/') {		// <dialstring>/<PIN>
			    *cp++ = '\0';
			    number = np;
			    pin = cp;
			    if (re) {			// do substitutions
				    subRHS(pin, *re, pagerID);
                    subRHS(number, *re, pagerID);
                }
			} else {			// <dialstring>
			    number = np;
			    pin = pagerID;
			}
			delete re;
			(void) fclose(fd);
			fxStr s(number);
			s.raisecase();
			if (s == "REJECT") {
			    emsg = fxStr::format("Invalid pager ID %s",pagerID);
			    return (false);
			} else
			    return (true);
		    }
		}
		delete re;
	    }
	    (void) fclose(fd);
	    emsg = fxStr::format("Unknown or illegal pager ID %s", pagerID);
	} else {
	    emsg = fxStr::format("Cannot open pager ID mapping file %s (%s)", 
		(const char*) pagerIDMapFile, strerror(errno));
	    logError("%s", (const char*) emsg);
	}
    } else {
	emsg = "No pager ID mapping file found";
	logError("%s", (const char*) emsg);
    }
    return (false);
}

/*
 * SNPP Parser.
 */

#define	N(a)	(sizeof (a) / sizeof (a[0]))

/*
 * Standard protocol commands.
 */
static const tab cmdtab[] = {
{ "2WAY", T_2WAY,	 true,false, "(preface 2-way transaction)" },
{ "ABOR", T_ABOR,	false, true, "(abort operation)" },
{ "ACKR", T_ACKREAD,	 true,false, "0|1" },
{ "ALER", T_ALERT,	 true,false, "alert-level" },
{ "CALL", T_CALLERID,    true,false, "caller-ID" },
{ "COVE", T_COVERAGE,    true,false, "alternate-area" },
{ "DATA", T_DATA,	 true, true, "(specify multi-line message)" },
{ "EXPT", T_EXPTAG,	 true,false, "hours" },
{ "HELP", T_HELP,	false, true, "[<string>]" },
{ "HOLD", T_HOLDUNTIL,   true, true, "YYYYMMDDHHMMSS [+/-GMT-diff]" },
{ "KTAG", T_KTAG,	 true,false, "message-tag pass-code" },
{ "LEVE", T_LEVEL,	 true, true, "service-level" },
{ "LOGI", T_LOGIN,	false, true, "username [password]" },
{ "MCRE", T_MCRESPONSE,  true,false, "2-byte-code response-text" },
{ "MESS", T_MESSAGE,	 true, true, "message" },
{ "MSTA", T_MSTATUS,	 true,false, "message-tag pass-code" },
{ "NOQU", T_NOQUEUEING,  true,false, "(disable message queueing)" },
{ "PAGE", T_PAGER,	 true, true, "pager-ID|alias [PIN]" },
{ "PING", T_PING,	 true, true, "pager-ID|alias" },
{ "QUIT", T_QUIT,	false, true, "(terminate service)" },
{ "RESE", T_REST,	 true, true, "(reset state)" },
{ "RTYP", T_RTYPE,	 true,false, "reply-type-code" },
{ "SEND", T_SEND,	 true, true, "(send message)" },
{ "SITE", T_SITE,	 true, true, "site-cmd [arguments]" },
{ "STAT", T_STAT,	false, true, "(return server status)" },
{ "SUBJ", T_SUBJECT,	 true, true, "message-subject" },
};

/*
 * Site-specific commands.
 */
static const tab sitetab[] = {
{ "FROMUSER",   T_FROM_USER,  true, true, "[<string>]" },
{ "HELP",       T_HELP,      false, true, "[<string>]" },
{ "IDLE",       T_IDLE,       true, true, "[max-idle-timeout]" },
{ "JPARM",      T_JPARM,      true, true, "(print job parameter status)" },
{ "JQUEUE",	T_JQUEUE,     true, true, "[on|off]" },
{ "LASTTIME",   T_LASTTIME,  false, true, "[DDHHSS]" },
{ "MAXDIALS",   T_MAXDIALS,   true, true, "[<number>]" },
{ "MAXTRIES",   T_MAXTRIES,   true, true, "[<number>]" },
{ "MODEM",      T_MODEM,      true, true, "[device|class]" },
{ "NOTIFY",     T_NOTIFY,     true, true, "[NONE|DONE|REQUEUE|DONE+REQUEUE]"},
{ "MAILADDR",   T_NOTIFYADDR, true, true, "[email-address]" },
{ "RETRYTIME",  T_RETRYTIME,  true, true, "[HHSS]" },
{ "SCHEDPRI",   T_SCHEDPRI,   true, true, "[<number>]" },
};

static const tab*
lookup(const tab* p, u_int n, const char* cmd)
{
    while (n != 0) {
        if (strneq(cmd, p->name, 4))		// NB: match on first 4 chars
	    return (p);
	p++, n--;
    }
    return (NULL);
}
static const char*
tokenName(const tab* p, u_int n, Token t)
{
    while (n != 0) {
	if (p->token == t)
	    return (p->name);
	p++, n--;
    }
    return ("???");
}
const char*
SNPPServer::cmdToken(Token t)
{
    return tokenName(cmdtab, N(cmdtab), t);
}
const char*
SNPPServer::siteToken(Token t)
{
    return tokenName(sitetab, N(sitetab), t);
}

bool
SNPPServer::checklogin(Token t)
{
    if (!IS(LOGGEDIN)) {
	cmdFailure(t, "not logged in");
	reply(530, "Please login with LOGIN.");
	return (false);
    } else
	return (true);
}

/*
 * Parse and process command input received on the
 * control channel.  This method is invoked whenever
 * data is present on the control channel.  We read
 * everything and parse (and execute) as much as possible
 * but do not block waiting for more data except when
 * a partial line of input is received.  This is done
 * to ensure other processing will be handled in a
 * timely fashion (e.g. processing of messages from
 * the scheduler received via the FIFO).
 */
int
SNPPServer::parse()
{
    if (IS(WAITDATA)) {			// recursive invocation
	state &= ~S_WAITDATA;
	return (0);
    }
    if (IS(SENDWAIT)) {			// waiting in SEND, discard data
	while (getCmdLine(cbuf, sizeof (cbuf)))
	    ;
	return (0);
    }
					// stop control channel idle timeout
    Dispatcher::instance().stopTimer(this);
    pushToken(T_NIL);			// reset state
    for (;;) {
	/*
	 * Fetch the next complete line of input received on the
	 * control channel.  This call will fail when no more data
	 * is *currently* waiting on the control channel.  Note
	 * that this does not mean the connection has dropped; just
	 * that data is not available at this instant.  Note also
	 * that if a partial line of input is received a complete
	 * line will be waited for (see below).
	 */
	if (!getCmdLine(cbuf, sizeof (cbuf)))
	    break;
	/*
	 * Parse the line of input read above.
	 */
	if (getToken(T_STRING, "command token")) {
	    tokenBody.raisecase();
	    const tab* p = lookup(cmdtab, N(cmdtab), tokenBody);
	    if (p == NULL)
		reply(500, "%s: Command not recognized.",
		    (const char*) tokenBody);
	    else if (!p->implemented)
		reply(500, "%s: Command not implemented.", p->name);
	    else if (isShutdown(!IS(LOGGEDIN))) {
		reply(421, "Server shutting down.  Goodbye.");
		dologout(0);		// XXX
	    /*
	     * If command requires client to be logged in check
	     * for this.  Note that some commands have variants
	     * that do not require the client be logged in--these
	     * cannot check here and must do it specially below.
	     */
	    } else if (p->checklogin && !checklogin(p->token))
		;
	    /*
	     * Command is valid, implemented, and the server
	     * is available to service it.  If the syntax is
	     * correct then reset the number of consecutive
	     * bad commands.  Note that since part of the syntax
	     * checking is login validation this logic will also
	     * catch people typing syntacitcally valid but
	     * unacceptable commands just to remain connected.
	     */
	    else if (cmd(p->token)) {
		consecutiveBadCmds = 0;
		continue;
	    }
	}
	/*
	 * If too many consecutive bad commands have been
	 * received disconnect.  This is to safeguard against
	 * a client that spews trash down the control connection.
	 */
	if (++consecutiveBadCmds >= maxConsecutiveBadCmds) {
	    /*
	     * Check for shutdown so that any shutdown message
	     * will get prepended to client reply.
	     */
	    (void) isShutdown(!IS(LOGGEDIN));
	    reply(421, "Too many errors, server shutting down.  Goodbye.");
	    dologout(0);
	}
    }
    Dispatcher::instance().startTimer(idleTimeout, 0, this);
    return (0);
}

/*
 * Protocol command (one line).
 */
bool
SNPPServer::cmd(Token t)
{
    fxStr s;
    long n;
    time_t tv;

    switch (t) {
    case T_ABOR:			// abort active command (e.g. SEND)
	if (CRLF()) {
	    logcmd(t);
	    ack(250, cmdToken(t));
	    return (true);
	}
	break;
    case T_DATA:			// submit multi-line message data
	if (CRLF()) {
	    logcmd(t);
	    dataCmd();
	    return (true);
	}
	break;
    case T_HELP:			// return help
	if (opt_CRLF()) {
	    logcmd(t);
	    helpCmd(cmdtab, (char*) NULL);
	    return (true);
	} else if (string_param(s, "command name")) {
	    logcmd(t, "%s", (const char*) s);
	    s.raisecase();
	    if (s == "SITE")
		helpCmd(sitetab, NULL);
	    else
		helpCmd(cmdtab, s);
	    return (true);
	}
	break;
    case T_HOLDUNTIL:			// set time to send
	if (SPACE() && SNPPTime(tv)) {
	    if (opt_CRLF()) {
		holdCmd(tv);
		return (true);
	    } else if (string_param(s, "GMT-difference")) {
		// conver GMT offset to numeric value
		int sign = 1;
		const char* cp = s;
		if (*cp == '-')
		    cp++, sign = -1;
		else if (*cp == '+')
		    cp++;
		time_t off = (time_t) (strtoul(cp, 0, 10)*60 / 100);
		holdCmd(tv + sign*off);
		return (true);
	    }
	}
	break;
    case T_LEVEL:			// set level of operation
	if (number_param(n)) {
	    logcmd(t, "%lu", n);
	    serviceLevel((u_int) n);
	    return (true);
	}
	break;
    case T_LOGIN:			// login as user
	if (SPACE() && STRING(s, "login-ID")) {
	    fxStr pwd;
	    if (opt_CRLF()) {
		logcmd(t, (const char*) s);
		loginCmd(s);
		return (true);
	    } else if (string_param(pwd, "password")) {
		logcmd(t, "%s <passwd>", (const char*) s);
		loginCmd(s, pwd);
		return (true);
	    }
	}
	break;
    case T_MESSAGE:			// specify 1-line message data
	if (SPACE() && multi_STRING(s) && CRLF()) {
	    logcmd(t, "%s", (const char*) s);
	    messageCmd(s);
	    return (true);
	}
	break;
    case T_PAGER:			// specify destination pager ID
	if (SPACE() && STRING(s, "pager-ID")) {
	    fxStr pwd;
	    if (opt_CRLF()) {
		logcmd(t, "%s", (const char*) s);
		pagerCmd(s);
		return (true);
	    } else if (string_param(pwd, "password/PIN")) {
		logcmd(t, "%s <passwd/PIN>", (const char*) s);
		pagerCmd(s, pwd);
		return (true);
	    }
	}
	break;
    case T_PING:			// localize/verify pager ID
	if (string_param(s, "pager-ID")) {
	    logcmd(t, "%s", (const char*) s);
	    pingCmd(s);
	    return (true);
	}
	break;
    case T_QUIT:			// terminate session
	if (CRLF()) {
	    logcmd(t);
	    reply(221, "Goodbye.");
	    dologout(0);
	    return (true);
	}
	break;
    case T_REST:			// reset server state
	logcmd(t);
	initServer();
	reply(220, "%s server (%s) ready.", (const char*) hostname, version);
	break;
    case T_SEND:			// initiate send operation
	if (CRLF()) {
	    logcmd(t);
	    sendCmd();
	    return (true);
	}
	break;
    case T_SITE:			// site-specific command
	if (SPACE() && getToken(T_STRING, "site command")) {
	    tokenBody.raisecase();
	    const tab* p = lookup(sitetab, N(sitetab), tokenBody);
	    if (p == NULL) {
		reply(500, "SITE %s: Command not recognized.",
		    (const char*) tokenBody);
	    } else if (!p->implemented)
		reply(500, "SITE %s: Command not implemented.", p->name);
	    else
		return (site_cmd(p->token));
	}
	break;
    case T_STAT:			// server status
	if (CRLF()) {
	    logcmd(t);
	    statusCmd();
	    return (true);
	}
	break;
    case T_SUBJECT:			// message subject
	if (SPACE() && multi_STRING(s) && CRLF()) {
	    logcmd(t, "%s", (const char*) s);
	    subjectCmd(s);
	    return (true);
	}
	break;
    }
    return (false);
}

/*
 * Site-specific protocol commands (one line).
 */
bool
SNPPServer::site_cmd(Token t)
{
    fxStr s;
    long n;
    time_t tv;
    bool b;

    switch (t) {
    case T_IDLE:			// set/query idle timeout
	if (opt_CRLF()) {
	    logcmd(t);
	    reply(250, "%u seconds.", idleTimeout);
	    return (true);
	} else if (number_param(n)) {
	    logcmd(t, "%lu", n);
	    if ((unsigned)n > maxIdleTimeout && !IS(PRIVILEGED)) {
		idleTimeout = maxIdleTimeout;
		reply(250, "%lu: Idle timeout too large, set to %u.",
		    n, maxIdleTimeout);
	    } else {
		idleTimeout = (int) n;
		reply(250, "Idle timeout set to %u.", idleTimeout);
	    }
	    return (true);
	}
	break;
    case T_HELP:			// return help
	if (opt_CRLF()) {
	    helpCmd(sitetab, (char*) NULL);
	    return (true);
	} else if (string_param(s, "command name")) {
	    logcmd(T_SITE, "HELP %s", (const char*) s);
	    s.raisecase();
	    helpCmd(sitetab, s);
	    return (true);
	}
	break;
    case T_JQUEUE:
	if (boolean_param(b)) {
	    logcmd(t, "%s", b ? "YES" : "NO");
	    defJob.queued = b;
	    reply(250, "Job will%s be queued.", b ? "" : " not");
	    return (true);
	}
	break;
    case T_FROM_USER:
    case T_MODEM:
    case T_NOTIFY:
    case T_NOTIFYADDR:
	if (SPACE() && multi_STRING(s) && CRLF() && setJobParameter(defJob, t, s)) {
	    logcmd(t, "%s", (const char*) s);
	    reply(250, "%s set to \"%s\".", parmToken(t), (const char*) s);
	    return (true);
	}
	break;
    case T_MAXDIALS:
    case T_MAXTRIES:
    case T_SCHEDPRI:
	if (number_param(n) && setJobParameter(defJob, t, (u_short) n)) {
	    logcmd(t, "%lu", n);
	    reply(250, "%s set to %u.", parmToken(t), n);
	    return (true);
	}
	break;
    case T_LASTTIME:			// time to kill job
	if (timespec_param(6, tv) && setJobParameter(defJob, t, tv)) {
	    logcmd(t, "%02d%02d%02d"
		, tv/(24*60*60) , (tv/(60*60))%24 , (tv/60)%60);
	    reply(250, "%s set to %02d%02d%02d."
		, parmToken(t)
		, tv/(24*60*60) , (tv/(60*60))%24 , (tv/60)%60);
	    return (true);
	}
	break;
    case T_RETRYTIME:			// retry interval for job
	if (timespec_param(4, tv) && setJobParameter(defJob, t, tv)) {
	    logcmd(t, "%02d%02d" , tv/60, tv%60);
	    reply(250, "%s set to %02d%02d.", parmToken(t), tv/60, tv%60);
	    return (true);
	}
	break;
    case T_JPARM:			// query job parameters
	if (CRLF()) {
	    logcmd(t);
	    jstatCmd(defJob);
	    return (true);
	}
	break;
    }
    return (false);
}

/*
 * Parse an SNPP time specification.
 */
bool
SNPPServer::SNPPTime(time_t& result)
{
    if (getToken(T_STRING, "time specification") && checkNUMBER(tokenBody)) {
	u_int tlen = tokenBody.length();
	const char* cp = tokenBody;
	// YYMMDDHHMM[SS]
	if (tlen == 12 || tlen == 10) {
	    struct tm tm;
	    tm.tm_sec  = (tlen == 12 ? twodigits(cp+10, 60) : 0);
	    tm.tm_min  = twodigits(cp+8, 60);
	    tm.tm_hour = twodigits(cp+6, 24);
	    tm.tm_mday = twodigits(cp+4, 32);
	    tm.tm_mon  = twodigits(cp+2, 13) - 1;
	    tm.tm_year = twodigits(cp+0, 100);
	    /*
	     * SNPP botched time specifications by not using 4 digits
	     * to specify a year.  This means that we have to guess at
	     * the intended year when the value is far in the future
	     * (as opposed to a year in the past that was given by
	     * mistake).  We arbitrarily assume years prior to 1990
	     * are in the next century and adjust them accordingly.
	     */
	    if (tm.tm_year < 90)
		tm.tm_year += 100;
	    tm.tm_isdst= -1;		// XXX not sure about this???
	    /*
	     * The above time is assumed to be relative to
	     * GMT and mktime returns locally adjusted time
	     * so we need to adjust the result to get things
	     * in the right timezone.  Note that any additional
	     * timezone correction factor specified by the
	     * client will then be applied to this result.
	     */
	    result = mktime(&tm) - gmtoff;
	    return (true);
	}
	syntaxError(fxStr::format(
	    "bad time specification (expecting 10/12 digits, got %u)", tlen));
    }
    return (false);
}

void
SNPPServer::syntaxError(const char* msg)
{
    const char* cp = strchr(cbuf, '\0');
    if (cp[-1] == '\n')
	cp--;
    reply(500, "'%.*s': Syntax error, %s.", cp-cbuf, cbuf, msg);
}

/*
 * Command support methods.
 */

/*
 * Process a multi-line text message.
 */
void
SNPPServer::dataCmd(void)
{
    if (!haveText) {
	fxStr emsg;
	u_int seqnum = getDocumentNumber(emsg);
	if (seqnum == (u_int) -1) {
	    reply(554, "%s", (const char*)emsg);
	    return;
	}
	msgFile = fxStr::format("/%s/doc%u.page", FAX_TMPDIR, seqnum);
	FILE* fout = Sys::fopen(msgFile, "w");
	if (fout != NULL) {
	    setFileOwner(msgFile);
	    FileCache::chmod(msgFile, 0660);		// sync cache
	    tempFiles.append(msgFile);
	    reply(354, "Begin message input; end with <CRLF>'.'<CRLF>.");
	    char buf[1024];
	    u_int len = 0;
	    for (;;) {
		if (getCmdLine(buf, sizeof (buf), true)) {
		    const char* bp = buf;
		    if (bp[0] == '.') {
			if ((bp[1] == '\n' && bp[2] == '\0') || bp[1] == '\0')
			    break;
			if (bp[1] == '.' && bp[2] == '\0')	// .. -> .
			    bp++;
		    }
		    u_int blen = strlen(bp);
		    if ((len += blen) > maxMsgLength) {
			reply(550,
			    "Error, message too long; max %u characters.",
			    maxMsgLength);
			(void) fclose(fout);
			Sys::unlink(msgFile);
			return;
		    }
		    // if buf only contains .., we skipped the first .
		    // hence we must write bp, not buf. also blen is
		    // the length of bp, not buf.
		    (void) fwrite((const char*) bp, blen, 1, fout);
		}
	    }
	    if (fclose(fout)) {
		perror_reply(554, msgFile, errno);
		Sys::unlink(msgFile);
	    } else {
		haveText = true;
		reply(250, "Message text OK.");
	    }
	} else
	    perror_reply(554, msgFile, errno);
    } else
	reply(503, "Error, message already entered.");
}

/*
 * Provide help.  We cannot share the base class
 * implementation of this command because SNPP
 * defines a different style for responses (sigh).
 */
void
SNPPServer::helpCmd(const tab* ctab, const char* s)
{
    const char* type;
    u_int NCMDS;
    if (ctab == sitetab) {
        type = "SITE ";
	NCMDS = N(sitetab);
    } else {
        type = "";
	NCMDS = N(cmdtab);
    }
    int width = 0;
    const tab* c = ctab;
    for (u_int n = NCMDS; n != 0; c++, n--) {
        int len = strlen(c->name);
        if (len > width)
            width = len;
    }
    width = (width + 8) &~ 7;
    if (s == NULL) {
        reply(214, "The following %scommands are recognized %s.",
            type, "(* =>'s unimplemented)");
        int columns = 76 / width;
        if (columns == 0)
            columns = 1;
        int lines = (NCMDS + columns - 1) / columns;
        for (int i = 0; i < lines; i++) {
            printf("214 ");
            for (int j = 0; j < columns; j++) {
                c = &ctab[j*lines + i];
                printf("%s%c", c->name, !c->implemented ? '*' : ' ');
                if (c + lines >= &ctab[NCMDS])
                    break;
                int w = strlen(c->name) + 1;
                while (w < width) {
                    putchar(' ');
                    w++;
                }
            }
            printf("\r\n");
        }
        (void) fflush(stdout);
        reply(250, "Direct comments to %s.", (const char*) faxContact);
    } else {
	c = lookup(ctab, NCMDS, s);
	if (c == NULL)
	    reply(550, "Unknown command %s.", s);
	else if (c->implemented)
	    reply(218, "Syntax: %s%s %s", type, c->name, c->help);
	else
	    reply(218, "%s%-*s\t%s; unimplemented.",
		type, width, c->name, c->help);
    }
}

/*
 * Specify the hold time (time to send) for a request.
 */
void
SNPPServer::holdCmd(time_t when)
{
    time_t now = Sys::now();
    if (when > now) {
	defJob.tts = when;
	const struct tm* tm = cvtTime(defJob.tts);
	reply(250, "Message will be processed at %02d%02d%02d%02d%02d."
	    , tm->tm_year
	    , tm->tm_mon+1
	    , tm->tm_mday
	    , tm->tm_hour
	    , tm->tm_min
	);
    } else
	reply(550, "Invalid delivery date/time; time in the past.");
}

/*
 * Login as the specified user.
 */
void
SNPPServer::loginCmd(const char* loginID, const char* pass)
{
    if (IS(LOGGEDIN))
        end_login();
    the_user = loginID;
    state &= ~S_PRIVILEGED;
    adminwd = "*";			// make sure no admin privileges
    passwd = "*";			// just in case...

    if (checkUser(loginID)) {
	if (passwd != "") {
	    if (pass[0] == '\0' || !streq(crypt(pass, passwd), passwd)) {
		if (++loginAttempts >= maxLoginAttempts) {
		    reply(421, "Login incorrect (closing connection).");
		    logNotice("Repeated SNPP login failures for user %s from %s [%s]"
			, (const char*) the_user
			, (const char*) remotehost
			, (const char*) remoteaddr
		    );
		    dologout(0);
		}
		reply(550, "Login incorrect.");
		logInfo("SNPP login failed from %s [%s], %s"
		    , (const char*) remotehost
		    , (const char*) remoteaddr
		    , (const char*) the_user
		);
		return;
	    }
	}
	login();
    } else {
	if (++loginAttempts >= maxLoginAttempts) {
	    reply(421, "Login incorrect (closing connection).");
	    logNotice("Repeated SNPP login failures for user %s from %s [%s]"
		, (const char*) the_user
		, (const char*) remotehost
		, (const char*) remoteaddr
	    );
	    dologout(0);
	} else {
	    reply(421, "User %s access denied.", (const char*) the_user);
	    logNotice("SNPP LOGIN REFUSED (%s) FROM %s [%s], %s"
		, "user denied"
		, (const char*) remotehost
		, (const char*) remoteaddr
		, (const char*) the_user
	    );
	}
    }
}

/*
 * Specify a one-line message text.
 */
void
SNPPServer::messageCmd(const char* msg)
{
    if (!haveText) {
	u_int len = strlen(msg);
	if (len > maxMsgLength) {
	    reply(550,
		"Error, message too long; no more than %u characters accepted.",
		maxMsgLength);
	    return;
	}
	fxStr emsg;
	u_int seqnum = getDocumentNumber(emsg);
	if (seqnum == (u_int) -1) {
	    reply(554, "%s", (const char*)emsg);
	    return;
	}
	msgFile = fxStr::format("/%s/doc%u.page", FAX_TMPDIR, seqnum);
	FILE* fout = Sys::fopen(msgFile, "w");
	if (fout != NULL) {
	    setFileOwner(msgFile);
	    FileCache::chmod(msgFile, 0660);		// sync cache
	    tempFiles.append(msgFile);
	    (void) fwrite(msg, len, 1, fout);
	    if (fclose(fout)) {
		perror_reply(554, msgFile, errno);
		Sys::unlink(msgFile);
	    } else {
		haveText = true;
		reply(250, "Message text OK.");
	    }
	} else
	    perror_reply(554, msgFile, errno);
    } else
	reply(503, "Error, message already entered.");
}

/*
 * Process a PAGE command.  Map the client-specified pager
 * identification string to a service provider phone number
 * and destination PIN and create a new job for this request.
 */
void
SNPPServer::pagerCmd(const char* pagerID, const char* pin)
{
    /*
     * Lookup pager ID and map to a service provider
     * and, optionally, PIN.
     */
    fxStr provider;
    fxStr PIN;
    fxStr emsg;
    if (!mapPagerID(pagerID, provider, PIN, emsg)) {
	reply(550, "%s.", (const char*) emsg);
	return;
    }
    /*
     * RFC 1861 says we should lock the Level, Coverage,
     * Holdtime, and Alert values for the request at this point.
     * To do this we create a job (inheriting the current state
     * from the default job) and fill in any other information.
     * However we do not submit it until later (when a SEND
     * request is made).
     */
    curJob = &defJob;				// inherit from default job
    // XXX merge items to same provider (maybe?)
    if (newJob(emsg) && updateJobOnDisk(*curJob, emsg)) {
	fxStr file("/" | curJob->qfile);
	setFileOwner(file);			// force ownership
	FileCache::chmod(file, 0660);		// sync cache
	curJob->lastmod = Sys::now();		// noone else should update

	curJob->number = provider;		// destination phone number
	if (!pin)				// use mapped value
	    pin = PIN;
	curJob->items.append(FaxItem(FaxRequest::send_page, 0, "", pin));
	curJob->items.append(
	    FaxItem(FaxRequest::send_page_saved, 0, "", pin));
	reply(250, "Pager ID accepted; provider: %s pin: %s jobid: %s."
	    , (const char*) provider
	    , pin
	    , (const char*) curJob->jobid
	);
	msgs.append(curJob->jobid);
    } else
	reply(554, "%s.", (const char*) emsg);
    initSNPPJob();				// reset job-related state
}

/*
 * Validate a client-specified pager ID string.
 * This is only sort of like the intended usage
 * but is the only thing that makes sense in
 * our environment (where the provider is not
 * directly accessible).
 */
void
SNPPServer::pingCmd(const char* pagerID)
{
    /*
     * Lookup pager ID and map to a service provider
     * and, optionally, PIN.
     */
    fxStr provider;
    fxStr PIN;
    fxStr emsg;
    if (mapPagerID(pagerID, provider, PIN, emsg))
	reply(821, "Valid pager ID, no location information available.");
    else
	reply(550, "%s.", (const char*) emsg);
}

static bool
notPresent(FaxItemArray& a, const char* name)
{
    for (u_int i = 0, n = a.length(); i < n; i++)
	if (a[i].op == FaxRequest::send_data && a[i].item == name)
	    return (false);
    return (true);
}

/*
 * Send (submit) a pager request.  We complete the
 * formulation of the job and submit it to the
 * scheduler.  If this job is marked for no queueing
 * then we also wait around for the job to complete.
 */
void
SNPPServer::sendCmd(void)
{
    if (msgs.length() == 0) {
	reply(503, "Error, no pager ID specified with PAGE.");
	return;
    }
    /*
     * If we need to wait for jobs to complete, construct
     * and register a trigger now before we submit the jobs.
     */
    fxStr emsg;
    u_int i, n = msgs.length();
    bool waitForJobs = false;
    for (i = 0; i < n; i++) {
	Job* job = findJob(msgs[i], emsg);
	if (!job)
	    msgs.remove(i), n--;
	else if (!job->queued)
	    waitForJobs = true;
    }
    if (waitForJobs) {
	state &= ~S_LOGTRIG;		// just process events
	if (!newTrigger(emsg, "J%04x", 1<<Trigger::JOB_DEAD)) {
	    reply(550,
		"Cannot register trigger to wait for job completion: %s.",
		(const char*) emsg);
	    return;
	}
    }
    const char* docname = msgFile;
    const char* cp = strrchr(docname, '/');
    if (!cp)				// relative name, e.g. doc123
	cp = docname;
    for (i = 0; i < n; i++) {
	Job* job = findJob(msgs[i], emsg);
	if (!job) {
	    msgs.remove(i), n--;
	    continue;
	}
	if (*cp != '\0') {
	    /*
	     * Add a reference to the message text for
	     * the current job, if not already present.
	     */
	    fxStr document =
		fxStr::format("/" FAX_DOCDIR "%s.", cp) | job->jobid;
	    if (notPresent(job->items, &document[1])) {
		if (Sys::link(docname, document) < 0) {
		    reply(554, "Unable to link document %s to %s: %s.",
			docname, (const char*) document, strerror(errno));
		    return;
		}
		job->items.append(
		    FaxItem(FaxRequest::send_data, 0, "", &document[1]));
	    }
	}
	/*
	 * Submit the job for scheduling.
	 */
	if (job->mailaddr == "")
	    replyBadJob(*job, T_NOTIFYADDR);
	else if (job->sender == "")
	    replyBadJob(*job, T_FROM_USER);
	else if (job->modem == "")
	    replyBadJob(*job, T_MODEM);
	else if (job->client == "")
	    replyBadJob(*job, T_CLIENT);
	else {
	    if (job->external == "")
		job->external = job->number;
	    if (job->tts == 0)
		job->tts = Sys::now();
	    job->killtime += job->tts;	// adjust based on any hold time
	    if (updateJobOnDisk(*job, emsg)) {
		const char* jobid = job->jobid;
		/*
		 * NB: we don't mark the lastmod time for the
		 * job since the scheduler should re-write the
		 * queue file to reflect what it did with it
		 * (e.g. what state it placed the job in).
		 */
		if (sendQueuerACK(emsg, "S%s", jobid))
		    continue;
		reply(554, "Failed to submit message %s: %s.",
		    jobid, (const char*) emsg);
	    } else
		reply(554, "%s.", (const char*) emsg);
	}
	if (waitForJobs) {		// cleanup trigger on error
	    cancelTrigger(emsg);
	    /*
	     * Not sure what to do here.  If some jobs got
	     * submitted then we want to remove them so the
	     * client can just reissue SEND if the error was
	     * transient (e.g. faxq was temporarily stopped).
	     * However we don't track which jobs got started
	     * and which did not so for now we'll just leave
	     * everything so the client can resubmit things
	     * with a subsequent SEND.  Unfortunately this
	     * can result in duplicate pages being sent.
	     */
	}
	return;				// failure
    }
    if (waitForJobs) {			// no queueing, wait for submitted jobs
	if (setjmp(urgcatch) == 0) {
	    Dispatcher& disp = Dispatcher::instance();
	    state |= S_WAITTRIG|S_SENDWAIT;
	    bool jobsPending;
	    do {
		disp.dispatch();
		/*
		 * The trigger event handlers update our notion
		 * of the job state asynchronously so we can just
		 * monitor the job state(s) after each event we
		 * receive.
		 */
		jobsPending = false;
		for (i = 0; i < n; i++) {
		    Job* job = findJob(msgs[i], emsg);
		    if (!job)
			msgs.remove(i), n--;
		    else if (!job->queued && 
		      job->state != FaxRequest::state_done &&
		      job->state != FaxRequest::state_failed)
			jobsPending = true;
		}
	    } while (IS(WAITTRIG) && jobsPending);
	    reply(250, "Message processing completed.");
	}
	state &= ~(S_WAITTRIG|S_SENDWAIT);
	(void) cancelTrigger(emsg);
    } else				// jobs queued, just acknowledge
	reply(250, "Message%s succesfully queued.",
	    msgs.length() > 1 ? "s" : "");
    resetState();			// reset SEND-related state
}

/*
 * Set the message service level.  We currently use
 * this just to select a scheduling priority and
 * job expiration time.
 */
void
SNPPServer::serviceLevel(u_int level)
{
    if (level <= 11) {
	defJob.usrpri = priMap[level];
	defJob.killtime = 60*killMap[level];
	defJob.retrytime = retryMap[level];
	reply(250, "OK, service level %u accepted.", level);
    } else
	reply(550, "Invalid service level %u; we only handle 0-11.", level);
}

/*
 * Return server status.
 */
void
SNPPServer::statusCmd(void)
{
    reply(214, "%s SNPP server status:", (const char*) hostname);
    reply(214, "    %s", version);
    if (!isdigit(remotehost[0]))
	reply(214, "    Connected to %s (%s).",
	    (const char*) remotehost, (const char*) remoteaddr);
    else
	reply(214, "    Connected to %s.", (const char*) remotehost);
    if (IS(LOGGEDIN)) {
	reply(214, "    Logged in as user %s (uid %u)."
	    , (const char*) the_user
	    , uid
	);
    } else
	reply(214, "    Waiting for login.");
    reply(214, "    Idle timeout set to %d seconds.", idleTimeout);
    if (discTime > 0)
	reply(214, "    Server scheduled to be unavailable at %.24s.",
	    asctime(cvtTime(discTime)));
    else
	reply(214, "    No server down time currently scheduled.");
    reply(214, "    HylaFAX scheduler reached at %s (%sconnected)."
	, (const char*) faxqFIFOName
	, faxqFd == -1 ? "not " : ""
    );
    if (clientFd != -1)
	reply(214, "    Server FIFO is /%s (%sopen)."
	    , (const char*) clientFIFOName
	    , clientFd == -1 ? "not " : ""
	);
    if (IS(WAITFIFO))
	reply(214, "    Waiting for response from HylaFAX scheduler.");
    if (msgs.length() > 0) {
	reply(214, "    %u message%s prepared for transmission.",
	    msgs.length(), msgs.length() > 1 ? "s" : "");
	// XXX dump status of msgs
    } else
	reply(214, "    No messages prepared for transmission.");
    reply(214, "    %s message text.", haveText ? "Received" : "No received");

    reply(250, "End of status");
}

/*
 * Set the subject for outgoing messages.
 * For now we just use it to tag the jobs.
 */
void
SNPPServer::subjectCmd(const char* subj)
{
    defJob.jobtag = subj;			// XXX
    reply(250, "Message subject OK.");
}

/*
 * Configuration support.
 */

void
SNPPServer::resetConfig()
{
    InetFaxServer::resetConfig();
    setupConfig();
}

void
SNPPServer::setupConfig()
{
    setConfigItem("maxmsglength",  "128");
    setConfigItem("pageridmapfile","/etc/pagermap");
    setConfigItem("prioritymap",
	"63 127 127 127 127 127 127  127  127  127  127  127");
    setConfigItem("killtimemap",
	" 5   5   5  15  60 240 720 1440 1440 1440 1440 1440");
    setConfigItem("retrytimemap",
	"30  60  60 180   0   0   0    0    0    0    0    0");
}

static void
setShortMap(u_short map[12], const char* value)
{
    char* cp;
    for (int i = 0; i < 12; i++) {
	map[i] = (u_short) strtoul(value, &cp, 0);
	if (!cp && *cp == '\0')
	    break;
	value = cp;
    }
}

static void
setTimeMap(time_t map[12], const char* value)
{
    char* cp;
    for (int i = 0; i < 12; i++) {
	map[i] = (time_t) strtoul(value, &cp, 0);
	if (!cp && *cp == '\0')
	    break;
	value = cp;
    }
}

bool
SNPPServer::setConfigItem(const char* tag, const char* value)
{
    if (streq(tag, "maxmsglength")) {
	maxMsgLength = getNumber(value);
    } else if (streq(tag, "pageridmapfile")) {
	pagerIDMapFile = value;
    } else if (streq(tag, "prioritymap")) {
	setShortMap(priMap, value);
    } else if (streq(tag, "killtimemap")) {
	setTimeMap(killMap, value);
    } else if (streq(tag, "retrytimemap")) {
	setTimeMap(retryMap, value);
    } else if (!InetFaxServer::setConfigItem(tag, value))
	return (false);
    return (true);
}
#endif /* SNPP_SUPPORT */
