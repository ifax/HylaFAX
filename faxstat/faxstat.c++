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
#include "StrArray.h"
#include "Sys.h"
#include "NLS.h"
#include "config.h"

#include <errno.h>

class faxStatApp : public FaxClient {
private:
    fxStr	header;

    bool listWithHeader(const fxStr& dir, fxStr& emsg);
public:
    faxStatApp();
    ~faxStatApp();

    void run(int argc, char** argv);
};
faxStatApp::faxStatApp() {}
faxStatApp::~faxStatApp() {}

static bool
writeStdout(void*, const char* buf, int cc, fxStr&)
{
    (void) Sys::write(STDOUT_FILENO, buf, cc);
    return (true);
}

void
faxStatApp::run(int argc, char** argv)
{
    resetConfig();
    readConfig(FAX_SYSCONF);
    readConfig(FAX_USERCONF);

    fxStrArray dirs;
    dirs.append(FAX_STATUSDIR);		// server status
    bool checkInfo = false;
    int c;
    while ((c = Sys::getopt(argc, argv, "h:adgfilrsv")) != -1)
	switch (c) {
	case 'a':			// display archived jobs
	    dirs.append(FAX_ARCHDIR);
	    break;
	case 'd':			// display jobs in done queue
	    dirs.append(FAX_DONEDIR);
	    break;
	case 'f':			// display queued documents
	    dirs.append(FAX_DOCDIR);
	    break;
	case 'g':			// use GMT for dates & times
	    setTimeZone(TZ_GMT);
	    break;
	case 'h':			// server's host
	    setHost(optarg);
	    break;
	case 'i':			// display any.info file
	    checkInfo = true;
	    break;
	case 'l':			// use local timezone for dates & times
	    setTimeZone(TZ_LOCAL);
	    break;
	case 'r':			// display receive queue
	    dirs.append(FAX_RECVDIR);
	    break;
	case 's':			// display jobs in send queue
	    dirs.append(FAX_SENDDIR);
	    break;
	case 'v':			// enable protocol tracing
	    setVerbose(true);
	    break;
	case '?':
	    fxFatal(_("usage: faxstat [-h server-host] [-adfgilrsv]"));
	}
    fxStr emsg;
    if (callServer(emsg)) {
	if (login(NULL, emsg)) {
	    if (checkInfo)
		(void) recvData(writeStdout, NULL, emsg, 0,
		    "RETR " FAX_STATUSDIR "/any." FAX_INFOSUF);
	    for (u_int i = 0, n = dirs.length(); i < n; i++) {
		header = (i > 0 ? "\n" : "");
		if (dirs[i] == FAX_SENDDIR || dirs[i] == FAX_DONEDIR) {
		    getJobStatusHeader(header);
		    header.append('\n');
		} else if (dirs[i] == FAX_RECVDIR) {
		    getRecvStatusHeader(header);
		    header.append('\n');
		} else if (dirs[i] == FAX_STATUSDIR) {
		    fxStr notused;
		    getModemStatusHeader(notused);
		}
		if (!listWithHeader(dirs[i], emsg))
		    break;
	    }
	}
	hangupServer();
    }
    if (emsg != "")
	printError("%s", (const char*) emsg);
}

bool
faxStatApp::listWithHeader(const fxStr& dir, fxStr& emsg)
{
    if (!setMode(MODE_S))
	goto bad;
    if (!initDataConn(emsg))
	goto bad;
    if (command("LIST " | dir) != PRELIM)
	goto bad;
    if (!openDataConn(emsg))
	goto bad;
    u_long byte_count; byte_count = 0;			// XXX for __GNUC__
    for (;;) {
	char buf[16*1024];
	int cc = read(getDataFd(), buf, sizeof (buf));
	if (cc == 0) {
	    closeDataConn();
	    return (getReply(false) == COMPLETE);
	}
	if (cc < 0) {
	    emsg = fxStr::format(_("Data Connection: %s"), strerror(errno));
	    (void) getReply(false);
	    break;
	}
	if (byte_count == 0 && header.length() > 0)
	    (void) Sys::write(STDOUT_FILENO, header, header.length());
	byte_count += cc;
	(void) Sys::write(STDOUT_FILENO, buf, cc);
    }
bad:
    closeDataConn();
    return (false);
}

int
main(int argc, char** argv)
{
    NLS::Setup("hylafax-client");
    faxStatApp app;
    app.run(argc, argv);
    return 0;
}
