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

#ifdef OLDPROTO_SUPPORT
/*
 * ``Old Protocol'' Support.
 *
 * This optional module implements the old client-server
 * protocol using the new mechanisms (mostly).  This
 * stuff is intended to be used for old clients that
 * cannot easily upgrade to use the new protocol (e.g.
 * the Mac- and PC-based clients).
 */

#include "port.h"
#include "Dispatcher.h"
#include "OldProtocol.h"
#include "Sys.h"
#include "Socket.h"
#include "config.h"

#include <sys/file.h>
#include <netdb.h>
#include <ctype.h>

extern "C" {
#include <arpa/inet.h>

#include <netinet/in_systm.h>
#include <netinet/ip.h>
}

#define	FAX_OSERVICE	"fax"		// old protocol service name
#define	FAX_ODEFPORT	4557		// old protocol default port

OldProtocolSuperServer::OldProtocolSuperServer(const char* p, int bl)
    : SuperServer("Old", bl)
    , port(p)
{}
OldProtocolSuperServer::~OldProtocolSuperServer() {}

bool
OldProtocolSuperServer::startServer(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s >= 0) {
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;
	const char* cp = port;
	struct servent* sp = getservbyname(cp, FAX_PROTONAME);
	if (!sp) {
	    if (isdigit(cp[0]))
		sin.sin_port = htons(atoi(cp));
	    else
		sin.sin_port = htons(FAX_ODEFPORT);
	} else
	    sin.sin_port = sp->s_port;
	if (Socket::bind(s, &sin, sizeof (sin)) >= 0) {
	    (void) listen(s, getBacklog());
	    Dispatcher::instance().link(s, Dispatcher::ReadMask, this);
	    return (true);				// success
	}
	Sys::close(s);
	logError("HylaFAX %s: bind (port %u): %m",
	    getKind(), ntohs(sin.sin_port));
    } else
	logError("HylaFAX %s: socket: %m", getKind());
    return (false);
}
HylaFAXServer* OldProtocolSuperServer::newChild(void)
    { return new OldProtocolServer; }

OldProtocolServer::OldProtocolServer()
{
    version = 0;
    alreadyChecked = false;
    codetab = NULL;
}
OldProtocolServer::~OldProtocolServer() {}

void
OldProtocolServer::open(void)
{
    setupNetwork(STDIN_FILENO);

    initServer();

    if (TRACE(CONNECT))
        logInfo("HylaFAX Old connection from %s [%s]",
	    (const char*) remotehost, (const char*) remoteaddr);

    fxStr emsg;
    if (isShutdown(true)) {
        logInfo("HylaFAX Old connection refused (server shut down) from %s [%s]",
	    (const char*) remotehost, (const char*) remoteaddr);
	dologout(-1);
    }
    if (!initClientFIFO(emsg)) {
        logInfo("HylaFAX Old connection refused (%s) from %s [%s]",
	    (const char*) emsg,
	    (const char*) remotehost, (const char*) remoteaddr);
	dologout(-1);
    }
    /*
     * We must chroot to the top of the spooling area to
     * mimic the new protocol; otherwise various pathname
     * assumptions will not be right.
     */
    uid_t ouid = geteuid();
    seteuid(0);
    if (chroot(".") < 0 || chdir("/") < 0)
	dologout(-1);
    seteuid(ouid);
    (void) umask(077);
    dirSetup();					// initialize directory handling

    doProtocol();
}

/*
 * Check host identity returned by gethostbyaddr to
 * weed out clients trying to spoof us (this is mostly
 * a sanity check; it's still trivial to spoof).
 * If the name returned by gethostbyaddr is in our domain,
 * look up the name and check that the peer's address
 * corresponds to the host name.
 */
bool
OldProtocolServer::checkHostIdentity(hostent*& hp)
{
    if (!isLocalDomain(hp->h_name))		// not local, don't check
	return (true);
    fxStr name(hp->h_name);			// must copy static value
    hp = Socket::gethostbyname(name);
    if (hp) {
	for (const char** cpp = (const char**) hp->h_addr_list; *cpp; cpp++)
	    if (memcmp(*cpp, &peer_addr.sin_addr, hp->h_length) == 0)
		return (true);
	sendError("Client address %s is not listed for host name %s.",
	    (const char*) remoteaddr, hp->h_name);
    } else
	sendError("No inverse address mapping for client host name %s.",
	    (const char*) name);
    return (false);
}

void
OldProtocolServer::setupNetwork(int fd)
{
    socklen_t addrlen;

    addrlen = sizeof (peer_addr);
    if (Socket::getpeername(fd, &peer_addr, &addrlen) < 0) {
        logError("getpeername: %m");
        dologout(-1);
    }
    addrlen = sizeof (ctrl_addr);
    if (Socket::getsockname(fd, &ctrl_addr, &addrlen) < 0) {
        logError("getsockname: %m");
        dologout(-1);
    }
#if defined(IPTOS_LOWDELAY)
    { int tos = IPTOS_LOWDELAY;
      if (Socket::setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0)
          logWarning("setsockopt (IP_TOS): %m");
    }
#endif
#if defined(SO_LINGER) && !defined(__linux__)
    { struct linger opt;
      opt.l_onoff = 1;
      opt.l_linger = 60;
      if (Socket::setsockopt(fd, SOL_SOCKET, SO_LINGER, &opt, sizeof (opt)) < 0)
	logWarning("setsockopt (SO_LINGER): %m");
    }
#endif
#ifdef  F_SETOWN
    if (fcntl(fd, F_SETOWN, getpid()) == -1)
        logError("fcntl (F_SETOWN): %m");
#endif
    hostent* hp = gethostbyaddr((char*) &peer_addr.sin_addr,
	sizeof (struct in_addr), AF_INET);
    remoteaddr = inet_ntoa(peer_addr.sin_addr);
    if (remoteaddr == "0.0.0.0")
        remotehost = "localhost";
    else if (hp) {
	if (checkHostIdentity(hp))
	    remotehost = hp->h_name;
	else
	    remotehost = remoteaddr;
    } else {
	sendError("Can not map your network address (%s) to a hostname",
	    (const char*) remoteaddr);
	remotehost =  remoteaddr;
    }
}

// NB: there is no support for the old style data transfer
const OldProtocolServer::protoCmd OldProtocolServer::cmds[] = {
{ "begin",		true,	&OldProtocolServer::submitJob },
{ "checkPerm",		true,	&OldProtocolServer::ackPermission },
{ "tiff",		true,	&OldProtocolServer::getTIFFData },
{ "postscript",		true,	&OldProtocolServer::getPostScriptData },
{ "zpostscript",	true,	&OldProtocolServer::getZPostScriptData },
{ "opaque",		true,	&OldProtocolServer::getOpaqueData },
{ "zopaque",		true,	&OldProtocolServer::getZOpaqueData },
{ "poll",		true,	&OldProtocolServer::newPollID },
{ "userID",		false,	&OldProtocolServer::setUserID },
{ "version",		false,	&OldProtocolServer::setProtoVersion },
{ "serverStatus",	false,	&OldProtocolServer::sendServerStatus },
{ "serverInfo",		false,	&OldProtocolServer::sendServerInfo },
{ "allStatus",		false,	&OldProtocolServer::sendAllStatus },
{ "userStatus",		false,	&OldProtocolServer::sendUserStatus },
{ "jobStatus",		false,	&OldProtocolServer::sendJobStatus },
{ "recvStatus",		false,	&OldProtocolServer::sendRecvStatus },
{ "remove",		true,	&OldProtocolServer::removeJob },
{ "removeGroup",	true,	&OldProtocolServer::removeJobGroup },
{ "kill",		true,	&OldProtocolServer::killJob },
{ "killGroup",		true,	&OldProtocolServer::killJobGroup },
{ "alterTTS",		true,	&OldProtocolServer::alterJobTTS },
{ "alterGroupTTS",	true,	&OldProtocolServer::alterJobGroupTTS },
{ "alterKillTime",	true,	&OldProtocolServer::alterJobKillTime },
{ "alterGroupKillTime",	true,	&OldProtocolServer::alterJobGroupKillTime },
{ "alterMaxDials",	true,	&OldProtocolServer::alterJobMaxDials },
{ "alterGroupMaxDials",	true,	&OldProtocolServer::alterJobGroupMaxDials },
{ "alterNotify",	true,	&OldProtocolServer::alterJobNotification },
{ "alterGroupNotify",	true,	&OldProtocolServer::alterJobGroupNotification },
{ "alterModem",		true,	&OldProtocolServer::alterJobModem },
{ "alterGroupModem",	true,	&OldProtocolServer::alterJobGroupModem },
{ "alterPriority",	true,	&OldProtocolServer::alterJobPriority },
{ "alterGroupPriority",	true,	&OldProtocolServer::alterJobGroupPriority },
};
#define	NCMDS	(sizeof (cmds) / sizeof (cmds[0]))

#define	isCmd(a)	(strcasecmp(line,a) == 0)

void
OldProtocolServer::doProtocol(void)
{
    modem = MODEM_ANY;
    the_user = "";

    char line[1024];
    char* tag;
    for (;;) {
	getCommandLine(line, tag);
	if (isCmd("."))
	    break;
	if (isCmd("modem")) {			// select outgoing device
	    int l = strlen(_PATH_DEV);
	    char* cp;
	    /*
	     * Convert modem name to identifier form by stripping
	     * any leading device pathname prefix and by replacing
	     * '/'s with '_'s for SVR4 where terminal devices are
	     * in subdirectories.
	     */
	    if (strncmp(tag, _PATH_DEV, l) == 0)
		tag += l;
	    for (cp = tag; cp = strchr(cp, '/'); *cp = '_')
		;
	    modem = tag;
	} else {
	    int i;
	    for (i = 0; i < NCMDS && !isCmd(cmds[i].cmd); i++)
		;
	    if (i == NCMDS)
		protocolBotch("unrecognized cmd \"%s\".", line);
	    if (cmds[i].check) {
		if (!alreadyChecked) {
		    if (!checkUser(the_user)) {
			logError("%s (%s): HylaFAX Old service refused",
			    (const char*) remotehost, (const char*) remoteaddr);
			sendError(
		    "Service refused; %s to use the fax server from %s (%s)."
			    , "you do not have permission"
			    , (const char*) remotehost
			    , (const char*) remoteaddr
			);
			dologout(-1);
		    }
		    alreadyChecked = true;
		}
	    }
	    (this->*cmds[i].cmdFunc)(tag);
	}
    }
    fflush(stdout);
    dologout(0);
}

void
OldProtocolServer::getCommandLine(char line[1024], char*& tag)
{
    if (!fgets(line, 1024-1, stdin))
	protocolBotch("unexpected EOF.");
    char* cp = strchr(line, '\0');
    if (cp > line && cp[-1] == '\n')
	*--cp = '\0';
    if (cp > line && cp[-1] == '\r')		// for telnet users
	*--cp = '\0';
    if (TRACE(PROTOCOL))
	logDebug("line \"%s\"", line);
    if (strcmp(line, ".") && strcmp(line, "..")) {
	tag = strchr(line, ':');
	if (!tag)
	    protocolBotch("malformed line \"%s\".", line);
	*tag++ = '\0';
	while (isspace(*tag))
	    tag++;
    }
}

extern int parseAtSyntax(const char*, const struct tm&, struct tm&, fxStr& emsg);

u_long
OldProtocolServer::cvtTime(const char* spec, const struct tm* ref, const char* what)
{
    fxStr emsg;
    struct tm when;
    if (!parseAtSyntax(spec, *ref, when, emsg)) {
	sendAndLogError("Error parsing %s \"%s\": %s.", what, spec, (const char*) emsg);
	/*NOTREACHED*/
    }
    return (u_long) mktime(&when);
}

void
OldProtocolServer::vsendClient(const char* tag, const char* fmt, va_list ap)
{
    fxStr s = fxStr::format("%s:", tag) | fxStr::vformat(fmt, ap);
    fprintf(stdout, "%s\n", (const char*)s);
    if (TRACE(PROTOCOL)) {
        logDebug("%s", (const char*)s);
    }
}

void
OldProtocolServer::sendClient(const char* tag, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsendClient(tag, fmt, ap);
    va_end(ap);
}

void
OldProtocolServer::sendError(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsendClient("error", fmt, ap);
    va_end(ap);
}

void
OldProtocolServer::sendAndLogError(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsendClient("error", fmt, ap);
    va_end(ap);
    va_start(ap, fmt);
    vlogError(fmt, ap);
    va_end(ap);
    dologout(1);
}

void
OldProtocolServer::protocolBotch(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fxStr buf(fxStr::format("Protocol botch, %s", fmt));
    vsendClient("error", buf, ap);
    va_end(ap);
    va_start(ap, fmt);
    vlogError(buf, ap);
    va_end(ap);
    dologout(1);
}

/*
 * Miscellaneous protocol requests.
 */

/*
 * Define client identity.
 */
void
OldProtocolServer::setUserID(const char* tag)
{
    the_user = tag;
    initDefaultJob();

    if (TRACE(LOGIN))
	logInfo("HylaFAX OLD LOGIN FROM %s [%s], %s"
	    , (const char*) remotehost
	    , (const char*) remoteaddr
	    , (const char*) the_user
	);
}

/*
 * Add a poll request.
 */
void
OldProtocolServer::newPollID(const char* tag)
{
    reqs.append(faxRequest(FaxRequest::send_poll, 0, tag, ""));
}

/*
 * Set the required/expected protocol version.
 */
void
OldProtocolServer::setProtoVersion(const char* tag)
{
    version = atoi(tag);
    if (version > FAX_PROTOVERS)
	protocolBotch(
	    "protocol version %u requested: only understand up to %u.",
	    version, FAX_PROTOVERS);
}

/*
 * Send the client acknowledgement that
 * it has permission to submit jobs.
 */
void
OldProtocolServer::ackPermission(const char*)
{
    sendClient("permission", "%s", "granted");
    fflush(stdout);
}

/*
 * General job manipulation support.
 */
#include <pwd.h>

bool
OldProtocolServer::_checkUser(const char* requestor, struct passwd* pwd)
{
    char buf[1024];
    char* cp;

    buf[0] = '\0';
    if (pwd->pw_gecos) {
	if (pwd->pw_gecos[0] == '&') {
	    strcpy(buf, pwd->pw_name);
	    strcat(buf, pwd->pw_gecos+1);
	    if (islower(buf[0]))
		buf[0] = toupper(buf[0]);
	} else
	    strcpy(buf, pwd->pw_gecos);
	if ((cp = strchr(buf,',')) != 0)
	    *cp = '\0';
	/* see FaxClient::setupUserIdentity; strip SysV junk */
	if ((cp = strchr(buf,'(')) != 0)
	    *cp = '\0';
    } else
	strcpy(buf, pwd->pw_name);
    if (TRACE(PROTOCOL)) {
	if (*buf)
	     logDebug("%s user: \"%s\"", pwd->pw_name, buf);
	else
	     logDebug("name of %s user unknown", pwd->pw_name);
    }
    return (strcmp(requestor, buf) == 0);
}

bool
OldProtocolServer::isAdmin(const char* requestor)
{
    static bool checked = false;
    static bool isadmin = false;

    if (!checked) {
	struct passwd* pwd = getpwuid(getuid());

	if (!pwd) {
	    logError("getpwuid failed for uid %d: %m", getuid());
	    pwd = getpwuid(geteuid());
	}
	if (!pwd) {
	    logError("getpwuid failed for effective uid %d: %m", geteuid());
	    isadmin = false;
	}
	isadmin = _checkUser(requestor, pwd);
	if (!isadmin) {					/* not fax user */
	    pwd = getpwnam("root");
	    if (!pwd) {
		logError("getpwnam failed for \"root\": %m");
		isadmin = false;
	    } else
		isadmin = _checkUser(requestor, pwd);	/* root user */
	}
	checked = true;
    }
    return (isadmin);
}

void
OldProtocolServer::applyToJob(const char* tag, const char* op,
    void (OldProtocolServer::*f)(Job&, const char*))
{
    char* requestor = strchr(tag, ':');
    if (!requestor)
	protocolBotch("no requestor name for \"%s\" command.", op);
    *requestor++ = '\0';
    char* arg = strchr(requestor, ':');
    if (arg)
	*arg++ = '\0';

    fxStr emsg;
    Job* job = findJob(tag, emsg);
    if (job) {
	/*
	 * Validate requestor is permitted to do op to the
	 * requested job.  We permit the person that submitted
	 * the job, the fax user, and root.  Using the GECOS
	 * field in doing the comparison is a crock, but not
	 * something to change now--leave it for a protocol
	 * redesign.
	 */
	if (requestor == job->sender || isAdmin(requestor)) {
	    if (TRACE(PROTOCOL))
		logDebug("%s request by %s for %s", op, requestor, tag);
	    (this->*f)(*job, arg);
	} else
	    sendClient("jobOwner", tag);
    } else
	sendClient("notQueued", tag);
}

void
OldProtocolServer::applyToJobGroup(const char* tag, const char* op,
    void (OldProtocolServer::*f)(Job&, const char*))
{
    char* requestor = strchr(tag, ':');
    if (!requestor)
	protocolBotch("no requestor name for \"%s\" command.", op);
    *requestor++ = '\0';
    char* arg = strchr(requestor, ':');
    if (arg)
	*arg++ = '\0';

    if (jobs.size() == 0)
	readJobs();
    u_int jobsDone = 0;
    u_int jobsSkipped = 0;
    for (JobDictIter iter(jobs); iter.notDone(); iter++) {
	Job& job = *iter.value();
	if (job.groupid != tag)
	    continue;
	/*
	 * Validate requestor is permitted to do op to the
	 * requested job.  We permit the person that submitted
	 * the job, the fax user, and root.  Using the GECOS
	 * field in doing the comparison is a crock, but not
	 * something to change now--leave it for a protocol
	 * redesign.
	 */
	if (requestor == job.sender || isAdmin(requestor)) {
	    if (TRACE(PROTOCOL))
		logDebug("%s request by %s for %s", op, requestor, tag);
	    (this->*f)(job, arg);
	    jobsDone++;
	} else
	    jobsSkipped++;
    }
    if (jobsDone == 0)
	sendClient(jobsSkipped ? "jobOwner" : "notQueued", tag);
}

/*
 * Job parameter alteration support.
 */

#define	DEFINE_Alter(param)						\
void OldProtocolServer::alterJob##param(const char* tag)		\
    { applyToJob(tag, "alter", &OldProtocolServer::reallyAlterJob##param); }\
void OldProtocolServer::alterJobGroup##param(const char* tag)		\
    { applyToJobGroup(tag, "alter", &OldProtocolServer::reallyAlterJob##param); }

bool
OldProtocolServer::alterSuspend(Job& job)
{
    if (job.state == FaxRequest::state_active) {
	sendClient("jobLocked", job.jobid);	// NB: maintain old semantics
	return (false);
    }
    fxStr emsg;
    if (!sendQueuerACK(emsg, "X%s", (const char*) job.jobid)) {
	sendError("Unable to suspend job: %s", (const char*) emsg);
	return (false);
    }
    return (true);
}

void
OldProtocolServer::alterResubmit(Job& job)
{
    fxStr emsg;
    const char* jobid = job.jobid;
    if (updateJobOnDisk(job, emsg)) {		// update q file
	if (sendQueuerACK(emsg, "S%s", jobid))	// resubmit job
	    sendClient("altered", jobid);
	else
	    sendError("Could not resubmit job: %s", (const char*) emsg);
    } else {
	sendError("Could not update job state: %s", (const char*) emsg);
	(void) sendQueuer(emsg, "S%s", jobid);
    }
}

void
OldProtocolServer::reallyAlterJobTTS(Job& job, const char* spec)
{
    time_t now = Sys::now();
    time_t t = cvtTime(spec, localtime(&now), "time-to-send");
    if (alterSuspend(job)) {
	job.tts = t;
	alterResubmit(job);
    }
}
DEFINE_Alter(TTS)

void
OldProtocolServer::reallyAlterJobKillTime(Job& job, const char* spec)
{
    time_t now = Sys::now();
    time_t t = cvtTime(spec, localtime(&now), "kill-time");
    if (alterSuspend(job)) {
	job.killtime = t;
	alterResubmit(job);
    }
}
DEFINE_Alter(KillTime)

void
OldProtocolServer::reallyAlterJobModem(Job& job, const char* device)
{
    if (alterSuspend(job)) {
	job.modem = device;
	alterResubmit(job);
    }
}
DEFINE_Alter(Modem)

void
OldProtocolServer::reallyAlterJobPriority(Job& job, const char* priority)
{
    if (alterSuspend(job)) {
	job.usrpri = atoi(priority);
	alterResubmit(job);
    }
}
DEFINE_Alter(Priority)

void
OldProtocolServer::reallyAlterJobMaxDials(Job& job, const char* max)
{
    if (alterSuspend(job)) {
	job.maxdials = atoi(max);
	alterResubmit(job);
    }
}
DEFINE_Alter(MaxDials)

void
OldProtocolServer::reallyAlterJobNotification(Job& job, const char* note)
{
    if (alterSuspend(job)) {
	job.checkNotifyValue(note);
	alterResubmit(job);
    }
}
DEFINE_Alter(Notification)

/*
 * Job removal and aborting support.
 */

void
OldProtocolServer::reallyRemoveJob(const char* op, Job& job)
{
    /*
     * First ask server to do removal.  If the job is being
     * processed, it will first be aborted.  Otherwise, do
     * the cleanup here.
     */
    fxStr emsg;
    const char* cmd = (strcmp(op, "remove") == 0 ? "R" : "K");
    if (sendQueuerACK(emsg, "%s%s", cmd, (const char*) job.jobid)) {
	sendClient("removed", job.jobid);
    } else if (lockJob(job, LOCK_EX|LOCK_NB, emsg)) {
	for (u_int i = 0, n = job.requests.length(); i < n; i++) {
	    const faxRequest& freq = job.requests[i];
	    switch (freq.op) {
	    case FaxRequest::send_tiff:
	    case FaxRequest::send_tiff_saved:
	    case FaxRequest::send_pdf:
	    case FaxRequest::send_pdf_saved:
	    case FaxRequest::send_postscript:
	    case FaxRequest::send_postscript_saved:
	    case FaxRequest::send_pcl:
	    case FaxRequest::send_pcl_saved:
		Sys::unlink(freq.item);
		break;
	    case FaxRequest::send_fax:
		if (job.isUnreferenced(i))
		    Sys::unlink(freq.item);
		break;
	    }
	}
	if (Sys::unlink(job.qfile) < 0) {
	    logError("%s: unlink %s failed (%m)", op, (const char*) job.qfile);
	    sendClient("unlinkFailed", job.jobid);
	} else {
	    logInfo("%s %s completed", 
		strcmp(op, "remove") == 0 ? "REMOVE" : "KILL",
		(const char*) job.jobid);
	    sendClient("removed", job.jobid);
	}
	unlockJob(job);
    }
}

#define	DEFINE_Op(op)						\
void OldProtocolServer::##op##Job(const char* tag)		\
    { applyToJob(tag, fxQUOTE(op), &OldProtocolServer::do##op); }\
void OldProtocolServer::##op##JobGroup(const char* tag)		\
    { applyToJobGroup(tag, fxQUOTE(op), &OldProtocolServer::do##op); }
void
OldProtocolServer::doremove(Job& job, const char*)
{
    reallyRemoveJob("remove", job);
}
DEFINE_Op(remove)
void
OldProtocolServer::dokill(Job& job, const char*)
{
    reallyRemoveJob("kill", job);
}
DEFINE_Op(kill)

/*
 * Job submission support.
 */

static void
lower(char* cp)
{
    while (*cp) {
	if (isupper(*cp))
	    *cp = tolower(*cp);
	cp++;
    }
}

void
OldProtocolServer::submitJob(const char*)
{
    fxStr emsg;
    curJob = &defJob;				// inherit from default
    if (!newJob(emsg)) {
	sendError("Can not create new job: %s.", (const char*) emsg);
	dologout(1);
    }
    time_t t = Sys::now();
    struct tm now = *localtime(&t);
    Job& job = *curJob;
    for (;;) {
	char line[1024];
	char* tag;
	getCommandLine(line, tag);
	if (isCmd("end") || isCmd(".")) {
	    setupData();
	    break;
	}
	lower(line);
	u_int ix;
	if (FaxRequest::isStrCmd(line, ix)) {
	    if (isCmd("cover"))
		coverProtocol(0, atoi(tag));
	    else
		job.*FaxRequest::strvals[ix].p = tag;
	} else if (FaxRequest::isShortCmd(line, ix)) {
	    job.*FaxRequest::shortvals[ix].p = atoi(tag);
	} else if (isCmd("sendAt")) {
	    job.tts = (time_t) cvtTime(tag, &now, "time-to-send");
	} else if (isCmd("killtime")) {
	    struct tm* tm;
	    if (job.tts) {		// NB: assumes sendAt precedes killtime
		t = job.tts;
		tm = localtime(&t);
	    } else
		tm = &now;
	    job.killtime = cvtTime(tag, tm, "kill-time");
	} else if (isCmd("retrytime")) {
	    job.retrytime = (time_t) atoi(tag);
	} else if (isCmd("notify")) {	// email notification
	    job.checkNotifyValue(tag);
	} else if (isCmd("pagechop")) {
	    job.checkChopValue(tag);
	} else if (isCmd("chopthreshold")) {
	    job.chopthreshold = atof(tag);
	} else if (isCmd("zcover")) {
	    coverProtocol(1, atoi(tag));
	} else if (isCmd("page")) {
	   job.requests.append(faxRequest(FaxRequest::send_page, 0, "", tag));
	} else if (isCmd("!page")) {
	   job.requests.append(faxRequest(FaxRequest::send_page_saved, 0, "", tag));
	} else				// XXX discard unknown items
	    logInfo("Unknown job item %s:%s", line, tag);
    }
    if (job.external == "")
	job.external = job.number;
    job.modem = modem;
    if (!updateJobOnDisk(job, emsg))
	sendError("%s", (const char*) emsg);
    else {
	setFileOwner(job.qfile);		// force ownership
	FileCache::chmod(job.qfile, 0660);	// sync cache
	if (sendQueuerACK(emsg, "S%s", (const char*) job.jobid))
	    sendClient("job", version > 1 ? "%s:%s" : "%s",
		(const char*) job.jobid, (const char*) job.groupid);
	else
	    sendError("Warning, no job scheduler appears to be running.");
    }
}

void
OldProtocolServer::coverProtocol(int isLZW, int cc)
{
    fxStr templ = fxStr::format(FAX_DOCDIR "/doc%s.cover",
	(const char*) curJob->jobid);
    FILE* fd = fopen(templ, "w");
    if (fd == NULL)
	sendAndLogError("Could not create cover sheet file %s.",
	    (const char*) templ);
    if (isLZW) {
	/*
	 * Cover sheet comes across as LZW-compressed data;
	 * the version id is the count of the decompressed
	 * data bytes to expect (sigh, what a hack!)
	 */
	setupLZW();
	long total = decodeLZW(stdin, fd);
	if (total != cc)
	    protocolBotch("not enough data received: expected %u, got %u.",
		cc, total);
	if (TRACE(PROTOCOL))
	    logDebug(templ | ": %ld-byte compressed %s",
		cc, "PostScript document");
    } else {
	/*
	 * Old-style, data comes across as
	 * ascii terminated by "..".
	 */
	char line[1024];
	char* tag;
	for (;;) {
	    getCommandLine(line, tag);
	    if (isCmd(".."))
		break;
	    fprintf(fd, "%s\n", tag);
	}
    }
    if (fflush(fd) != 0)
	sendAndLogError("Error writing cover sheet data: %s.", strerror(errno));
    fclose(fd);
    curJob->requests.append(
	faxRequest(FaxRequest::send_postscript, 0, "", templ));
}

void
OldProtocolServer::setupData(void)
{
    for (u_int i = 0, n = reqs.length(); i < n; i++) {
	const faxRequest& freq = reqs[i];
	if (freq.op != FaxRequest::send_poll) {
	    const char* cp = strrchr(freq.item, '/');
	    if (!cp)				// relative name, e.g. doc123
		cp = freq.item;
	    fxStr doc = fxStr::format("/" FAX_DOCDIR "%s.", cp) | curJob->jobid;
	    if (Sys::link(freq.item, doc) < 0) {
		logError("Can not link document \"%s\": %s",
		    (const char*) doc, strerror(errno));
		sendError("Problem setting up document file: %s",
		    strerror(errno));
		// purge links that will never get used
		do {
		    const faxRequest& freq = reqs[--i];
		    switch (freq.op) {
		    case FaxRequest::send_tiff:
		    case FaxRequest::send_pdf:
		    case FaxRequest::send_postscript:
		    case FaxRequest::send_pcl:
			Sys::unlink(freq.item);
			break;
		    }
		} while (i != 0);
		dologout(1);
		/*NOTREACHED*/
	    }
	    curJob->requests.append(faxRequest(freq.op, 0, "", &doc[1]));
	} else
	    curJob->requests.append(freq);
    }
}

const char*
OldProtocolServer::fileDesc(FaxSendOp type)
{
    return type == FaxRequest::send_tiff ? "TIFF image"
	 : type == FaxRequest::send_postscript ? "PostScript document"
	 : "opaque data"
	 ;
}

fxStr
OldProtocolServer::dataTemplate(FaxSendOp type, int& dfd)
{
    fxStr emsg;
    u_int seqnum = getDocumentNumbers(1, emsg);
    if (seqnum == (u_int) -1)
	sendAndLogError("Could not create data temp file: %s.",
	    (const char*) emsg);
    fxStr templ = fxStr::format("/" FAX_TMPDIR "/doc%u%s"
	, seqnum
	, type == FaxRequest::send_tiff		? ".tif" :
	  type == FaxRequest::send_postscript	? ".ps"  :
	  type == FaxRequest::send_pdf	? ".pdf":
						  ""
	);
    dfd = Sys::open(templ, O_RDWR|O_CREAT|O_EXCL, 0660);
    if (dfd < 0)
	sendAndLogError("Could not create data temp file %s: %s.",
	    (const char*) templ, strerror(errno));
    tempFiles.append(templ);
    reqs.append(faxRequest(type, 0, "", templ));
    return (templ);
}

void
OldProtocolServer::getData(FaxSendOp type, long cc)
{
    if (cc > 0) {
	int dfd;
	fxStr templ = dataTemplate(type, dfd);
	long total = 0;
	while (cc > 0) {
	    char buf[4096];
	    long n = fxmin(cc, (u_long) sizeof (buf));
	    if (fread(buf, n, 1, stdin) != 1)
		protocolBotch("not enough data received: %u of %u bytes.",
		    total, total+cc);
	    if (Sys::write(dfd, buf, (u_int) n) != n)
		sendAndLogError("Error writing data file: %s.",
		    strerror(errno));
	    cc -= n;
	    total += n;
	}
	Sys::close(dfd);
	if (TRACE(PROTOCOL))
	    logDebug(templ | ": %ld-byte %s", total, fileDesc(type));
    }
}

void
OldProtocolServer::getLZWData(FaxSendOp type, long cc)
{
    if (cc > 0) {
	int dfd;
	fxStr templ = dataTemplate(type, dfd);
	FILE* fout = fdopen(dfd, "w");
	setupLZW();
	long total = decodeLZW(stdin, fout);
	if (total != cc)
	    protocolBotch("not enough data received: expected %u, got %u.",
		cc, total);
	if (fflush(fout) != 0)
	    sendAndLogError("Error writing data file: %s.", strerror(errno));
	fclose(fout);
	if (TRACE(PROTOCOL))
	    logDebug(templ | ": %ld-byte compressed %s", cc, fileDesc(type));
    }
}

#define MAXCODE(n)	((1L<<(n))-1)
#define	BITS_MIN	9		/* start with 9 bits */
#define	BITS_MAX	13		/* max of 13 bit strings */
#define	CSIZE		(MAXCODE(BITS_MAX)+1)

/* predefined codes */
#define	CODE_CLEAR	0		/* code to clear string table */
#define	CODE_EOI	1		/* end-of-information code */
#define CODE_FIRST	256		/* first free code entry */
#define	CODE_MAX	MAXCODE(BITS_MAX)

typedef u_short hcode_t;		/* codes fit in 16 bits */
typedef struct code_ent {
    code_ent*	next;
    u_short	length;			/* string len, including this token */
    u_char	value;			/* data value */
    u_char	firstchar;		/* first token of string */
} code_t;

void
OldProtocolServer::setupLZW()
{
    if (codetab == NULL) {
	codetab = new code_t[CSIZE];
	for (int code = CODE_FIRST-1; code > CODE_EOI; code--) {
	    codetab[code].value = code;
	    codetab[code].firstchar = code;
	    codetab[code].length = 1;
	    codetab[code].next = NULL;
	}
    }
}

/*
 * Decode a "hunk of data".
 */
#define	GetNextCode(fin, code) {				\
    nextdata = (nextdata<<8) | getc(fin);			\
    if ((nextbits += 8) < nbits) {				\
	nextdata = (nextdata<<8) | getc(fin);			\
	nextbits += 8;						\
    }								\
    code = (hcode_t)((nextdata >> (nextbits-nbits))&nbitsmask);	\
    nextbits -= nbits;						\
}

long
OldProtocolServer::decodeLZW(FILE* fin, FILE* fout)
{
    u_int nbits = BITS_MIN;
    u_int nextbits = 0;
    u_long nextdata = 0;
    u_long nbitsmask = MAXCODE(BITS_MIN);
    code_t* freep = &codetab[CODE_FIRST];
    code_t* oldcodep = codetab-1;
    code_t* maxcodep = &codetab[nbitsmask-1];
    long total = 0;

    for (;;) {
	hcode_t code;
	code_t* codep;

	GetNextCode(fin, code);
	if (code == CODE_EOI)
	    return (total);
	if (code == CODE_CLEAR) {
	    freep = &codetab[CODE_FIRST];
	    nbits = BITS_MIN;
	    nbitsmask = MAXCODE(BITS_MIN);
	    maxcodep = &codetab[nbitsmask-1];
	    GetNextCode(fin, code);
	    if (code == CODE_EOI)
		return (total);
	    putc(code, fout);
	    total++;
	    oldcodep = &codetab[code];
	    continue;
	}
	codep = &codetab[code];
	/*
	 * Add the new entry to the code table.
	 */
	freep->next = oldcodep;
	freep->firstchar = freep->next->firstchar;
	freep->length = freep->next->length+1;
	freep->value = (codep < freep) ? codep->firstchar : freep->firstchar;
	if (++freep > maxcodep) {
	    nbits++;
	    if (nbits > BITS_MAX) {
		protocolBotch("LZW code length overflow %s",
		    "(invalid compressed data)");
		/*NOTREACHED*/
	    }
	    nbitsmask = MAXCODE(nbits);
	    maxcodep = &codetab[nbitsmask-1];
	}
	oldcodep = codep;
	if (code >= CODE_FIRST) {
	    /*
	     * Code maps to a string, copy string
	     * value to output (written in reverse).
	     */
	    char buf[1024];
	    int len = codep->length;
	    char* tp = (len > sizeof (buf) ? (char*) malloc(len) : buf) + len;
	    do {
		*--tp = codep->value;
	    } while (codep = codep->next);
	    fwrite(tp, len, 1, fout);
	    total += len;
	    if (tp != buf)
		free(tp);
	} else {
	    putc(code, fout);
	    total++;
	}
    }
#ifdef notdef
    protocolBotch("not enough data received: out of data before EOI.");
#endif
    /*NOTREACHED*/
}

void OldProtocolServer::getTIFFData(const char* tag)
    { getData(FaxRequest::send_tiff, atol(tag)); }
void OldProtocolServer::getPostScriptData(const char* tag)
    { getData(FaxRequest::send_postscript, atol(tag)); }
void OldProtocolServer::getOpaqueData(const char* tag)
    { getData(FaxRequest::send_data, atol(tag)); }
void OldProtocolServer::getZPostScriptData(const char* tag)
    { getLZWData(FaxRequest::send_postscript, atol(tag)); }
void OldProtocolServer::getZOpaqueData(const char* tag)
    { getLZWData(FaxRequest::send_data, atol(tag)); }

void
OldProtocolServer::dologout(int status)
{
    if (status != 0) {
	Sys::unlink(fxStr::format(FAX_DOCDIR "/doc%s.cover",
	    (const char*) curJob->jobid));
	if (curJob->qfile != "")
	    Sys::unlink(curJob->qfile);
    }
    HylaFAXServer::dologout(status);
}

/*
 * Status support.
 */

static void
getConfig(const char* fileName, fxStr& number)
{
    FILE* fd = fopen(fileName, "r");
    if (fd != NULL) {
	char line[1024];
	while (fgets(line, sizeof (line)-1, fd) != NULL) {
	    char* cp = strchr(line, '#');
	    if (!cp)
		cp = strchr(line, '\n');
	    if (cp)
		*cp = '\0';
	    cp = strchr(line, ':');
	    if (cp) {
		for (*cp++ = '\0'; isspace(*cp); cp++)
		    ;
		if (strcasecmp(line, "FAXNumber") == 0) {
		    number = cp;
		    break;
		}
	    }
	}
	(void) fclose(fd);
    }
}

void
OldProtocolServer::sendServerStatus(const char*)
{
    DIR* dir = opendir(".");
    if (!dir)
	sendAndLogError("Problem accessing spool directory: %s.",
	    strerror(errno));
    int fifo;
    if (version > 0) {
	Sys::close(fifo = Sys::open(FAX_FIFO, O_WRONLY|O_NDELAY));
	sendClient("server", "all modems:" FAX_FIFO ":%s",
	    fifo != -1 ? "Running" : "Not running");
    } else
	sendClient("server", "all modems");
    /*
     * Setup a prefix for matching potential FIFO files.
     */
    fxStr match(FAX_FIFO ".");
    if (modem != MODEM_ANY)
	match.append(modem);
    struct dirent* dp;
    while ((dp = readdir(dir)) != 0) {
	if (strncmp(dp->d_name, match, match.length()) != 0)
	    continue;
	fifo = Sys::open(dp->d_name, O_WRONLY|O_NDELAY);
	if (fifo != -1) {
	    Sys::close(fifo);
	    const char* cp = strchr(dp->d_name, '.') + 1;
	    fxStr faxNumber;
	    getConfig(fxStr::format(FAX_CONFIG ".%s", cp), faxNumber);
	    if (version > 0) {
		fxStr status;
		getServerStatus(fxStr::format(FAX_STATUSDIR "/%s", cp), status);
		fxStr modemName(cp);
		canonModem(modemName);
		sendClient("server", "%s:%s:%s"
		    , (const char*) faxNumber
		    , (const char*) modemName
		    , (const char*) status
		);
	    } else
		sendClient("server", "%s", (const char*) faxNumber);
	}
    }
    (void) closedir(dir);
}

void
OldProtocolServer::sendServerInfo1(const char* name)
{
    fxStr modemFile(name);
    modemFile.resize(modemFile.next(0, '.'));		// discard ``.info''
    canonModem(modemFile);
    FILE* fd = fopen(fxStr::format(FAX_STATUSDIR "/%s", name), "r");
    if (fd != NULL) {
	char line[1024];
	while (fgets(line, sizeof (line), fd) != NULL) {
	    char* tp = strchr(line, '\n');
	    if (tp)
		*tp = '\0';
	    sendClient("serverInfo", "%s:%s", (const char*) modemFile, line);
	}
	fclose(fd);
    }
}

void
OldProtocolServer::sendServerInfo(const char*)
{
    if (modem == MODEM_ANY) {
	DIR* dir = opendir(FAX_STATUSDIR);
	if (!dir)
	    sendAndLogError("Problem accessing status directory: %s.",
		strerror(errno));
	struct dirent* dp;
	while ((dp = readdir(dir)) != 0) {
	    const char* cp = strrchr(dp->d_name, '.');
	    if (cp && strcmp(cp+1, FAX_INFOSUF) == 0)
		sendServerInfo1(dp->d_name);
	}
	(void) closedir(dir);
    } else
	sendServerInfo1(modem | "." | FAX_INFOSUF);
}

void
OldProtocolServer::sendClientJobStatus(const Job& job)
{
    if (version > 0) {
	if (job.tts != 0)
	    Jprintf(stdout, "jobStatus:%j:%S:%Y:%e:%m:%s\n", job);
	else
	    Jprintf(stdout, "jobStatus:%j:%S:asap:%e:%m:%s\n", job);
    } else
	Jprintf(stdout, "jobStatus:%j:%S:%Z:%%e\n", job);
}

void
OldProtocolServer::sendClientJobLocked(const Job& job)
{
    if (version > 0)
	Jprintf(stdout, "jobStatus:%j:%S:locked:%e:%m:%s\n", job);
    else
	Jprintf(stdout, "jobLocked:%j:%S:%e\n", job);
}

void
OldProtocolServer::sendJobStatus(Job& job)
{
    if (lockJob(job, LOCK_SH|LOCK_NB)) {
	sendClientJobStatus(job);
	unlockJob(job);
    } else
	sendClientJobLocked(job);
}

bool
OldProtocolServer::modemMatch(const fxStr& a, const fxStr& b)
{
    return (a == MODEM_ANY || b == MODEM_ANY || a == b);
}

void
OldProtocolServer::sendAllStatus(const char*)
{
    if (jobs.size() == 0)
	readJobs();
    for (JobDictIter iter(jobs); iter.notDone(); iter++) {
	Job& job = *iter.value();
	if (!modemMatch(modem, job.modem))
	    continue;
	sendJobStatus(job);
    }
}

void
OldProtocolServer::sendJobStatus(const char* jobid)
{
    fxStr emsg;
    Job* job = findJob(jobid, emsg);
    if (job && modemMatch(modem, job->modem))
	sendJobStatus(*job);
}

void
OldProtocolServer::sendUserStatus(const char* sender)
{
    if (jobs.size() == 0)
	readJobs();
    for (JobDictIter iter(jobs); iter.notDone(); iter++) {
	Job& job = *iter.value();
	if (!modemMatch(modem, job.modem))
	    continue;
	if (lockJob(job, LOCK_SH|LOCK_NB)) {
	    sendClientJobLocked(job);
	    unlockJob(job);
	} else if (job.sender == sender)
	    sendJobStatus(job);
    }
}

void
OldProtocolServer::sendRecvStatus(const char*)
{
    DIR* dir = opendir(FAX_RECVDIR);
    if (dir == NULL)
	sendAndLogError("Can not access receive queue directory \"%s\".",
	    FAX_RECVDIR);
    fxStr path(FAX_RECVDIR "/");
    struct dirent* dp;
    while (dp = readdir(dir)) {
	const char* name = dp->d_name;
	if (name[0] != 'f' || name[1] != 'a' || name[2] != 'x')
	    continue;
	RecvInfo ri(path | name);
	if (getRecvDocStatus(ri)) {
	    struct stat sb;			// XXX required by Rprintf
	    if (version > 0)
		Rprintf(stdout, "recvJob:%X:%w:%l:%r:%p:%Y:%s\n", ri, sb);
	    else
		Rprintf(stdout, "recvJob:%X:%w:%l:%r:%p:%Z:%s\n", ri, sb);
	}
    }
    closedir(dir);
}
#endif /* OLDPROTO_SUPPORT */
