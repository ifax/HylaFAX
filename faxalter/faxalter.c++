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

extern int
parseAtSyntax(const char* s, const struct tm& ref, struct tm& at0, fxStr& emsg);

class faxAlterApp : public FaxClient {
private:
    fxBool	groups;			// group or job id's
    fxStr	script;			// commands to send for each job

    void addToScript(const char* fmt ...);

    void usage();
public:
    faxAlterApp();
    ~faxAlterApp();

    void run(int argc, char** argv);
};
faxAlterApp::faxAlterApp() { groups = FALSE; }
faxAlterApp::~faxAlterApp() {}

void
faxAlterApp::run(int argc, char** argv)
{
    resetConfig();
    readConfig(FAX_SYSCONF);
    readConfig(FAX_USERCONF);

    fxStr emsg;
    time_t now = Sys::now();
    struct tm tts = *localtime(&now);
    struct tm when;

    int c;
    while ((c = getopt(argc, argv, "a:h:k:m:n:P:t:DQRgpv")) != -1)
	switch (c) {
	case 'D':			// set notification to when done
	    addToScript("NOTIFY DONE");
	    break;
	case 'Q':			// no notification (quiet)
	    addToScript("NOTIFY NONE");
	    break;
	case 'R':			// set notification to when requeued
	    addToScript("NOTIFY DONE+REQUEUE");
	    break;
	case 'a':			// send at specified time
	    if (strcasecmp(optarg, "NOW")) {
		if (!parseAtSyntax(optarg, *localtime(&now), tts, emsg)) {
		    printError("%s", (const char*) emsg);
		    return;
		}
		now = mktime(&tts);
		when = *gmtime(&now);	// NB: must be relative to GMT
		addToScript("SENDTIME %d%02d%02d%02d%02d"
		    , when.tm_year+1900
		    , when.tm_mon+1
		    , when.tm_mday
		    , when.tm_hour
		    , when.tm_min
		);
	    } else
		addToScript("SENDTIME NOW");
	    break;
	case 'g':			// apply to groups, not jobs
	    groups = TRUE;
	    break;
	case 'h':			// server's host
	    setHost(optarg);
	    break;
	case 'k':			// kill job at specified time
	    if (!parseAtSyntax(optarg, tts, when, emsg)) {
		printError("%s", optarg, (const char*) emsg);
		return;
	    }
	    { time_t tv = mktime(&when) - now;
	      addToScript("LASTTIME %02d%02d%02d"
		, tv/(24*60*60)
		, (tv/(60*60))%24
		, (tv/60)%60
	      );
	    }
	    break;
	case 'm':			// modem
	    addToScript("MODEM %s", optarg);
	    break;
	case 'n':			// set notification
	    addToScript("NOTIFY %s",
		strcasecmp(optarg, "done") == 0 ?	"DONE" :
		strcasecmp(optarg, "requeued") == 0 ?	"DONE+REQUEUE" :
							optarg);
	    break;
	case 'p':			// send now (push)
	    addToScript("SENDTIME NOW");
	    break;
	case 'P':			// scheduling priority
	    if ((u_int) atoi(optarg) > 255)
		fxFatal("Invalid job priority %s;"
		    " values must be in the range [0,255]", optarg);
	    addToScript("SCHEDPRI %s", optarg);
	    break;
	case 't':			// set max number of retries
	    if (atoi(optarg) < 0)
		fxFatal("Bad number of retries for -t option: %s", optarg);
	    addToScript("MAXDIALS %s", optarg);
	    break;
	case 'v':			// trace protocol
	    setVerbose(TRUE);
	    break;
	case '?':
	    usage();
	}
    if (optind >= argc)
	usage();
    if (script == "")
	fxFatal("No job parameters specified for alteration.");
    if (callServer(emsg)) {
	if (login(NULL, emsg)) {
	    for (; optind < argc; optind++) {
		const char* jobid = argv[optind];
		if (setCurrentJob(jobid) && jobSuspend(jobid)) {
		    if (!runScript(script, script.length(), "<stdin>", emsg))
			break;			// XXX???
		    if (!jobSubmit(jobid)) {
			emsg = getLastResponse();
			break;
		    }
		    printf("Job %s: done.\n", jobid);
		}
	    }
	}
	hangupServer();
    }
    if (emsg != "")
	printError(emsg);
}

void
faxAlterApp::usage()
{
    fxFatal("usage: faxalter"
      " [-h server-host]"
      " [-a time]"
      " [-k time]"
      " [-m modem]"
      " [-n notify]"
      " [-P priority]"
      " [-t tries]"
      " [-p]"
      " [-g]"
      " [-DQR]"
      " jobID...");
}

void
faxAlterApp::addToScript(const char* fmt0 ...)
{
    va_list ap;
    va_start(ap, fmt0);
    char fmt[1024];
    sprintf(fmt, "%s %s\n", groups ? "JGPARM" : "JPARM", fmt0);
    script.append(fxStr::vformat(fmt, ap));
    va_end(ap);
}

int
main(int argc, char** argv)
{
    faxAlterApp app;
    app.run(argc, argv);
    return 0;
}
