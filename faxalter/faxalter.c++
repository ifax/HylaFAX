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
    bool	groups;			// group or job id's
    fxStr	script;			// commands to send for each job

    void usage();
public:
    faxAlterApp();
    ~faxAlterApp();

    void run(int argc, char** argv);
};
faxAlterApp::faxAlterApp() { groups = false; }
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
    bool useadmin = false;

    int c;
    while ((c = getopt(argc, argv, "a:d:h:k:m:n:P:t:ADQRgpv")) != -1)
	switch (c) {
	case 'A':			// connect with administrative privileges
	    useadmin = true;
	    break;
	case 'D':			// set notification to when done
        script.append(groups ? "JGPARM " : "JPARM ");
        script.append("NOTIFY DONE\n");
	    break;
	case 'Q':			// no notification (quiet)
        script.append(groups ? "JGPARM " : "JPARM ");
        script.append("NOTIFY DONE\n");
	    break;
	case 'R':			// set notification to when requeued
        script.append(groups ? "JGPARM " : "JPARM ");
        script.append("NOTIFY DONE+REQUEUE\n");
	    break;
	case 'a':			// send at specified time
	    if (strcasecmp(optarg, "NOW")) {
		if (!parseAtSyntax(optarg, *localtime(&now), tts, emsg)) {
		    printError("%s", (const char*) emsg);
		    return;
		}
		now = mktime(&tts);
		when = *gmtime(&now);	// NB: must be relative to GMT
        script.append(groups ? "JGPARM " : "JPARM ");
		script.append("SENDTIME ");
            {
                fxStr tmpbuf = fxStr::format(
                    "%d%02d%02d%02d%02d"
		            , when.tm_year+1900
		            , when.tm_mon+1
		            , when.tm_mday
		            , when.tm_hour
		            , when.tm_min
		        );
                script.append(tmpbuf);
                script.append("\n");
            }
	    } else {
            script.append(groups ? "JGPARM " : "JPARM ");
            script.append("SENDTIME NOW\n");
        }
	    break;
	case 'd':			// destination number
            script.append(groups ? "JGPARM " : "JPARM ");
	    script.append("EXTERNAL ");
            script.append(optarg);
            script.append("\n");
            script.append(groups ? "JGPARM " : "JPARM ");
	    script.append("DIALSTRING ");
            script.append(optarg);
            script.append("\n");
	    break;
	case 'g':			// apply to groups, not jobs
	    groups = true;
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
          script.append(groups ? "JGPARM " : "JPARM ");
	      script.append("LASTTIME ");
	      fxStr tmpbuf = fxStr::format(
             "%02d%02d%02d"
		     , tv/(24*60*60)
		     , (tv/(60*60))%24
		     , (tv/60)%60
	      );
          script.append(tmpbuf);
          script.append("\n");
	    }
	    break;
	case 'm':			// modem
        script.append(groups ? "JGPARM " : "JPARM ");
	    script.append("MODEM ");
        script.append(optarg);
        script.append("\n");
	    break;
	case 'n':			// set notification
        script.append(groups ? "JGPARM " : "JPARM ");
	    script.append("NOTIFY ");
		if (strcasecmp(optarg, "done") == 0) {
            script.append("DONE\n");
        } else if (strcasecmp(optarg, "requeued") == 0) {
            script.append("DONE+REQUEUE\n");
        } else {
            script.append(optarg);
            script.append("\n");
        }
	    break;
	case 'p':			// send now (push)
        script.append(groups ? "JGPARM " : "JPARM ");
	    script.append("SENDTIME NOW\n");
	    break;
	case 'P':			// scheduling priority
	    if ((u_int) atoi(optarg) > 255)
		fxFatal("Invalid job priority %s;"
		    " values must be in the range [0,255]", optarg);
        script.append(groups ? "JGPARM " : "JPARM ");
	    script.append("SCHEDPRI ");
	    script.append(optarg);
            script.append("\n");
	    break;
	case 't':			// set max number of retries
	    if (atoi(optarg) < 0)
		fxFatal("Bad number of retries for -t option: %s", optarg);
        script.append(groups ? "JGPARM " : "JPARM ");
	    script.append("MAXDIALS ");
	    script.append(optarg);
            script.append("\n");
	    break;
	case 'v':			// trace protocol
	    setVerbose(true);
	    break;
	case '?':
	    usage();
	}
    if (optind >= argc)
	usage();
    if (script == "")
	fxFatal("No job parameters specified for alteration.");
    if (callServer(emsg)) {
	if (login(NULL, emsg) &&
	    (!useadmin || admin(NULL, emsg))) {
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
	printError("%s", (const char*) emsg);
}

void
faxAlterApp::usage()
{
    fxFatal("usage: faxalter"
      " [-h server-host]"
      " [-a time]"
      " [-d number]"
      " [-k time]"
      " [-m modem]"
      " [-n notify]"
      " [-P priority]"
      " [-t tries]"
      " [-A]"
      " [-p]"
      " [-g]"
      " [-DQR]"
      " jobID...");
}

int
main(int argc, char** argv)
{
    faxAlterApp app;
    app.run(argc, argv);
    return 0;
}
