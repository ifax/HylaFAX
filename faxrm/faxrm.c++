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

class faxRmApp : public FaxClient {
private:
    void usage();

    fxBool removeJob(const char* id, fxStr& emsg);
    fxBool deleteDoc(const char* id);
public:
    faxRmApp();
    ~faxRmApp();

    void run(int argc, char** argv);
};

faxRmApp::faxRmApp() {}
faxRmApp::~faxRmApp() {}

void
faxRmApp::run(int argc, char** argv)
{
    extern int optind;
    extern char* optarg;
    int c;

    resetConfig();
    readConfig(FAX_SYSCONF);
    readConfig(FAX_USERCONF);

    fxBool jobs = TRUE;
    fxBool docs = FALSE;
    fxBool useadmin = FALSE;

    while ((c = getopt(argc, argv, "ah:dv")) != -1)
	switch (c) {
	case 'a':
	    useadmin = TRUE;
	    break;
	case 'd':			// treat args as document names
	    jobs = FALSE;
	    docs = TRUE;
	    break;
	case 'h':			// server's host
	    setHost(optarg);
	    break;
	case 'v':
	    setVerbose(TRUE);
	    break;
	case '?':
	    usage();
	}
    if (optind >= argc)
	usage();
    fxStr emsg;
    if (callServer(emsg)) {
	if (login(NULL, emsg) &&
	    (!useadmin || admin (NULL, emsg))) {

	    for (; optind < argc; optind++) {
		const char* id = argv[optind];
		if (jobs) {
		    if (!removeJob(id, emsg))
			break;
		} else if (docs) {
		    if (!deleteDoc(id)) {
			emsg = getLastResponse();
			break;
		    }
		    printf("%s removed.\n", id);
		}
	    }
	}
	hangupServer();
    }
    if (emsg != "")
	printError(emsg);
}

fxBool
faxRmApp::removeJob(const char* id, fxStr& emsg)
{
    if (jobKill(id)) {
	printf("Job %s removed.\n", id);
	return (TRUE);
    }
    emsg = getLastResponse();
    if (getLastCode() == 504 && jobDelete(id)) {
	printf("Job %s removed (from doneq).\n", id);
	emsg = "";
	return (TRUE);
    }
    return (FALSE);
}

fxBool
faxRmApp::deleteDoc(const char* id)
{
    return (command("DELE %s%s"
	, id[0] == '/' || strncmp(id, FAX_DOCDIR, sizeof (FAX_DOCDIR)-1) == 0 ?
	    "" : "/" FAX_DOCDIR "/"
	, id
	) == COMPLETE);
}

void
faxRmApp::usage()
{
    fxFatal("usage: faxrm [-h server-host] [-adv] id...");
}

int
main(int argc, char** argv)
{
    faxRmApp app;
    app.run(argc, argv);
    return 0;
}
