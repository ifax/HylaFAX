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

class faxFetchApp : public FaxClient {
private:
    virtual void vtraceServer(const char* fmt, va_list ap);
public:
    faxFetchApp();
    ~faxFetchApp();

    void run(int argc, char** argv);
};
faxFetchApp::faxFetchApp() {}
faxFetchApp::~faxFetchApp() {}

static bool
writeStdout(void*, const char* buf, int cc, fxStr&)
{
    (void) Sys::write(STDOUT_FILENO, buf, cc);
    return (true);
}

void
faxFetchApp::vtraceServer(const char* fmt, va_list ap)
{
    vfprintf(stderr, fmt, ap);
    fputs("\n", stderr);
}

void
faxFetchApp::run(int argc, char** argv)
{
    resetConfig();
    readConfig(FAX_SYSCONF);
    readConfig(FAX_USERCONF);

    int c;
    fxStr op = "RETR ";
    u_int mode = MODE_S;
    u_long page = 0;
    while ((c = getopt(argc, argv, "h:o:p:svz")) != -1)
	switch (c) {
	case 'h':			// server's host
	    setHost(optarg);
	    break;
	case 'p':			// retrieve page
	    op = "RETP ";
	    page = atol(optarg);
	    break;
	case 's':	
	    mode = MODE_S;
	    break;
	case 'v':			// enable protocol tracing
	    setVerbose(true);
	    break;
	case 'z':
	    mode = MODE_Z;
	    break;
	case '?':
	    fxFatal("usage: faxfetch [-h server-host] [-v] file");
	}
    if (optind < argc) {
	fxStr emsg;
	if (callServer(emsg)) {
	    if (login(NULL, emsg)) {
		setType(TYPE_I);	// always image type
		for (; optind < argc; optind++)
		    if (mode == MODE_S)
			recvData(writeStdout, NULL, emsg, page, op | argv[optind]);
		    else
			recvZData(writeStdout, NULL, emsg, page, op | argv[optind]);
	    }
	    hangupServer();
	}
	if (emsg != "")
	    printError("%s", (const char*) emsg);
    }
}

int
main(int argc, char** argv)
{
    faxFetchApp app;
    app.run(argc, argv);
    return 0;
}
