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
#include "FaxClient.h"
#include "Sys.h"
#include "config.h"

#include <errno.h>

class watchApp : public FaxClient {
private:
    fxStr	appName;

    void fatal(const char* fmt ...);
    void usage();
public:
    watchApp();
    ~watchApp();

    void run(int argc, char** argv);
};

watchApp::watchApp() {}
watchApp::~watchApp() {}

static bool
writeData(int arg, const char* buf, int cc, fxStr& emsg)
{
    if (Sys::write( arg, buf, cc) != cc) {
	emsg = fxStr::format("write error: %s", strerror(errno));
	return (false);
    } else
	return (true);
}

void
watchApp::run(int argc, char** argv)
{
    extern int optind;
    extern char* optarg;
    int c;

    appName = argv[0];
    u_int l = appName.length();
    appName = appName.tokenR(l, '/');

    resetConfig();
    readConfig(FAX_SYSCONF);
    readConfig(FAX_USERCONF);

    while ((c = Sys::getopt(argc, argv, "gh:lv")) != -1)
	switch (c) {
	case 'g':
	    setTimeZone(TZ_GMT);
	    break;
	case 'h':			// server's host
	    setHost(optarg);
	    break;
	case 'l':
	    setTimeZone(TZ_LOCAL);
	    break;
	case 'v':			// verbose mode
	    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
	    FaxClient::setVerbose(true);// protocol tracing
	    break;
	case '?':
	    usage();
	    /*NOTREACHED*/
	}
    if (argc - optind != 1)
	usage();
    fxStr emsg;
    if (callServer(emsg)) {
	if (login(NULL, emsg) && setType(TYPE_A)) {
	    if (getTimeZone() == TZ_GMT)
		printWarning("time values reported in GMT");
	    (void) recvData(writeData, STDOUT_FILENO, emsg, 0,
		"SITE TRIGGER %s", argv[optind]);
	}
	hangupServer();
    }
    if (emsg != "")
	printError("%s", (const char*) emsg);
}

void
watchApp::usage()
{
    fxFatal("usage: %s [-h host] [-g] [-l] [-v] trigger-specification",
	(const char*) appName);
}

int
main(int argc, char** argv)
{
    watchApp app;
    app.run(argc, argv);
    return (-1);				// reached only on error
}
