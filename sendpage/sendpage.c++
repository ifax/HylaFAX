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
#include "SNPPClient.h"
#include "Sys.h"
#include "config.h"

#if HAS_LOCALE
extern "C" {
#include <locale.h>
}
#endif

class sendPageApp : public SNPPClient {
public:
private:
    fxStr	appName;		// for error messages
    fxStr	msgFile;		// file containing any text
protected:
    void copyToTemporary(int fin, fxStr& tmpl);

    void vprintError(const char* fmt, va_list ap);
    void vprintWarning(const char* fmt, va_list ap);
    void fatal(const char* fmt ...);
    void usage();
public:
    sendPageApp();
    ~sendPageApp();

    bool run(int argc, char** argv);
};

sendPageApp::sendPageApp()
{
    setupConfig();
}

sendPageApp::~sendPageApp()
{
    if (msgFile != "")
	Sys::unlink(msgFile);
}

bool
sendPageApp::run(int argc, char** argv)
{
    extern int optind;
    extern char* optarg;
    int c;

    appName = argv[0];
    u_int l = appName.length();
    appName = appName.tokenR(l, '/');

    resetConfig();
    readConfig(FAX_SYSCONF);
    readConfig(FAX_LIBDATA "/sendpage.conf");
    readConfig(FAX_USERCONF);

    fxStr emsg;
    bool noText = false;		// default is to assume message text
    SNPPJob& proto = getProtoJob();
    while ((c = Sys::getopt(argc, argv, "a:De:f:h:i:I:l:nNp:qRs:t:T:v")) != -1)
	switch (c) {
	case 'a':			// time at which to transmit page
	    if (!proto.setHoldTime(optarg, emsg)) {
		printError("Invalid hold time \"%s\": %s",
		    optarg, (const char*) emsg);
		exit(-1);
	    }
	    break;
	case 'D':			// notify when done
	    proto.setNotification("when done");
	    break;
	case 'f':			// sender's identity
	    setFromIdentity(optarg);
	    break;
	case 'h':			// server's host
	    setHost(optarg);
	    break;
	case 's':			// user-specified job identifier
	    proto.setSubject(optarg);
	    break;
	case 'I':			// fixed retry time
	    proto.setRetryTime(atoi(optarg));
	    break;
	case 'l':			// service level
	    proto.setServiceLevel(atoi(optarg));
	    break;
	case 'n':			// numeric-only page, no message text
	    noText = true;
	    break;
	case 'N':			// no notification
	    proto.setNotification("none");
	    break;
	case 'p':			// PIN
	    addJob().setPIN(optarg);
	    break;
	case 'q':			// queue job and don't wait
	    proto.setQueued(true);
	    break;
	case 'R':			// notify when requeued or done
	    proto.setNotification("when requeued");
	    break;
	case 't':			// times to try sending
	    proto.setMaxTries(atoi(optarg));
	    break;
	case 'T':			// times to dial telephone
	    proto.setMaxDials(atoi(optarg));
	    break;
	case 'v':			// client-server protocol tracing
	    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
	    setVerbose(true);
	    break;
	case '?':
	    usage();
	    /*NOTREACHED*/
	}

    if (getNumberOfJobs() == 0) {
	fprintf(stderr, "%s: No pager identification number (PIN) specified.\n",
	    (const char*) appName);
	usage();
    }
    if (!noText) {			// collect message text ...
	if (optind < argc) {		// ... from command line
	    fxStr msg;
	    for (; optind < argc; optind++) {
		if (msg.length() > 0)
		    msg.append(' ');
		msg.append(argv[optind]);
	    }
	    setPagerMsg(msg);
	} else {			// ... from standard input
	    copyToTemporary(fileno(stdin), msgFile);
	    setPagerMsgFile(msgFile);
	}
    }
    bool status = false;
    if (callServer(emsg)) {
	status = login(NULL, emsg)
	      && prepareForJobSubmissions(emsg)
	      && submitJobs(emsg);
	hangupServer();
    }
    if (!status)
	printError(emsg);
    return (status);
}

void
sendPageApp::usage()
{
    fxFatal("usage: %s"
	" -p PIN [-p PIN ...]\n"
	" [-a time-to-send]"
	" [-l service-level]"
	" [-s message-subject]\n"
	"    "
	" [-h host[:modem]]"
	" [-f from]\n"
	"    "
	" [-I retry-time]"
	" [-t max-tries]"
	" [-T max-dials]"
	" [-nqvDNR]"
	" [msgs ...]",
	(const char*) appName);
}

/*
 * Copy data from fin to a temporary file.
 */
void
sendPageApp::copyToTemporary(int fin, fxStr& tmpl)
{
    char buff[128];
    sprintf(buff, "%s/sndpageXXXXXX", _PATH_TMP);
    int fd = Sys::mkstemp(buff);
    tmpl = buff;
    if (fd >= 0) {
	int cc;
	char buf[16*1024];
    while ((cc = Sys::read(fin, buf, sizeof (buf))) > 0) {
        if (Sys::write(fd, buf, cc) != cc) {
            Sys::unlink(tmpl);
            fatal("%s: write error", (const char*) tmpl);
        }
    }
	Sys::close(fd);
    } else
	fatal("%s: Can not create temporary file", (const char*) tmpl);
}

void
sendPageApp::vprintError(const char* fmt, va_list ap)
{
    fprintf(stderr, "%s: ", (const char*) appName);
    SNPPClient::vprintError(fmt, ap);
}

void
sendPageApp::vprintWarning(const char* fmt, va_list ap)
{
    fprintf(stderr, "%s: ", (const char*) appName);
    SNPPClient::vprintWarning(fmt, ap);
}

#include <signal.h>

static	sendPageApp* app = NULL;

static void
cleanup()
{
    sendPageApp* a = app;
    app = NULL;
    delete a;
}

static void
sigDone(int)
{
    cleanup();
    exit(-1);
}

int
main(int argc, char** argv)
{
#ifdef LC_CTYPE
    setlocale(LC_CTYPE, "");			// for <ctype.h> calls
#endif
#ifdef LC_TIME
    setlocale(LC_TIME, "");			// for strftime calls
#endif
    signal(SIGHUP, fxSIGHANDLER(sigDone));
    signal(SIGINT, fxSIGHANDLER(sigDone));
    signal(SIGTERM, fxSIGHANDLER(sigDone));
    signal(SIGCHLD, fxSIGHANDLER(SIG_DFL));    // by YC
    app = new sendPageApp;
    if (!app->run(argc, argv))
	sigDone(0);
    signal(SIGHUP, fxSIGHANDLER(SIG_IGN));
    signal(SIGINT, fxSIGHANDLER(SIG_IGN));
    signal(SIGTERM, fxSIGHANDLER(SIG_IGN));
    cleanup();
    return (0);
}

static void
vfatal(FILE* fd, const char* fmt, va_list ap)
{
    vfprintf(fd, fmt, ap);
    va_end(ap);
    fputs(".\n", fd);
    sigDone(0);
}

void
fxFatal(const char* va_alist ...)
#define	fmt va_alist
{
    va_list ap;
    va_start(ap, fmt);
    vfatal(stderr, fmt, ap);
    /*NOTREACHED*/
}
#undef fmt

void
sendPageApp::fatal(const char* va_alist ...)
#define	fmt va_alist
{
    fprintf(stderr, "%s: ", (const char*) appName);
    va_list ap;
    va_start(ap, fmt);
    vfatal(stderr, fmt, ap);
    /*NOTREACHED*/
}
#undef fmt
