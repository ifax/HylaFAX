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
#include "Sys.h"

#include "faxApp.h"
#include "Dictionary.h"
#include "FaxRequest.h"
#include "config.h"

#include <sys/file.h>
#include <ctype.h>
#include <errno.h>

/*
 * HylaFAX Spooling Area Scavenger.
 */

fxDECLARE_StrKeyDictionary(RefDict, u_int)
fxIMPLEMENT_StrKeyPtrValueDictionary(RefDict, u_int)

class faxQCleanApp : public faxApp {
private:
    bool	archiving;		// enable archival support
    bool	verbose;		// trace interesting actions
    bool	trace;			// trace all actions
    bool	nowork;			// do not carry out actions
    time_t	minJobAge;		// threshold for processing done jobs
    time_t	minDocAge;		// threshold for purging unref'd docs

    fxStr	qFilePrefix;
    RefDict	docrefs;

    static const fxStr archDir;
    static const fxStr doneDir;
    static const fxStr docDir;

    void scanDirectory(void);
    void collectRefs(const FaxRequest&);
    void archiveJob(const FaxRequest& req);
    void expungeCruft(void);
public:
    faxQCleanApp();
    ~faxQCleanApp();

    void run(void);

    void setJobAge(const char*);
    void setDocAge(const char*);
    void setArchiving(bool);
    void setVerbose(bool);
    void setTracing(bool);
    void setNoWork(bool);
};

const fxStr faxQCleanApp::archDir	= FAX_ARCHDIR;
const fxStr faxQCleanApp::doneDir	= FAX_DONEDIR;
const fxStr faxQCleanApp::docDir	= FAX_DOCDIR;

faxQCleanApp::faxQCleanApp()
{
    minJobAge = 15*60;		// jobs kept max 15 minutes in doneq
    minDocAge = 60*60;		// 1 hour threshold on keeping unref'd documents
    archiving = false;		// default is to disable job archiving
    verbose = false;		// log actions to stdout
    nowork = false;		// default is to carry out work
    trace = false;		// trace work

    qFilePrefix = FAX_SENDDIR "/q";
}

faxQCleanApp::~faxQCleanApp() {}

void faxQCleanApp::setJobAge(const char* s)
    { minJobAge = (time_t) strtoul(s, NULL, 0); }
void faxQCleanApp::setDocAge(const char* s)
    { minDocAge = (time_t) strtoul(s, NULL, 0); }
void faxQCleanApp::setArchiving(bool b)	{ archiving = b; }
void faxQCleanApp::setVerbose(bool b)		{ verbose = b; }
void faxQCleanApp::setTracing(bool b)		{ trace = b; }
void faxQCleanApp::setNoWork(bool b)		{ nowork = b; }

void
faxQCleanApp::run(void)
{
    if (trace)
	verbose = true;
    scanDirectory();
    expungeCruft();
}

/*
 * Scan the doneq directory for jobs that need processing.
 */
void
faxQCleanApp::scanDirectory(void)
{
    if (trace)
	printf("Scan %s directory for jobs to remove+archive.\n",
	    (const char*) doneDir);

    DIR* dir = Sys::opendir(doneDir);
    if (dir == NULL) {
	printf("%s: Could not scan directory for jobs.\n",
	    (const char*) doneDir);
	return;
    }
    time_t now = Sys::now();
    fxStr path(doneDir | "/");
    for (dirent* dp = readdir(dir); dp; dp = readdir(dir)) {
	if (dp->d_name[0] != 'q')
	    continue;
	fxStr filename(path | dp->d_name);
	struct stat sb;
	if (Sys::stat(filename, sb) < 0 || !S_ISREG(sb.st_mode)) {
	    if (trace)
		printf("%s: ignored (cannot stat or not a regular file)\n",
		    (const char*) filename);
	    continue;
	}
	int fd = Sys::open(filename, O_RDWR);
	if (fd >= 0) {
	    if (flock(fd, LOCK_SH|LOCK_NB) >= 0) {
		FaxRequest* req = new FaxRequest(filename, fd);
		bool reject;
		if (req->readQFile(reject) && !reject) {
		    if (now - sb.st_mtime < minJobAge) {
			/*
			 * Job is not old enough to implement the ``doneop''
			 * action, just collect the references to documents
			 * and forget it until later.
			 */
			if (trace)
			    printf("%s: job too new, ignored (for now).\n",
				(const char*) filename);
			collectRefs(*req);
		    } else if (archiving &&
		      strncmp(req->doneop, "archive", 7) == 0) {
			/*
			 * Job should be archived, create an entry in the
			 * archive directory that contains the job's state,
			 * references to the documents, and the session logs.
			 */
			if (verbose)
			    printf("JOB %s: archive (%s)%s.\n"
				, (const char*) req->jobid
				, (const char*) req->doneop
				, nowork ? " (not done)" : ""
			    );
			if (!nowork)
			    archiveJob(*req);
		    } else {
			if (verbose)
			    printf("JOB %s: remove (%s) %s.\n"
				, (const char*) req->jobid
				, (const char*) req->doneop
				, nowork ? " (not done)" : ""
			    );
			if (!nowork)
			    Sys::unlink(req->qfile);
		    }
		} else {
		    /*
		     * Job file is corrupted or unreadable in some
		     * way.  We were able to lock the file so we
		     * assume it is in a determinant state and not
		     * just in the process of being created; and
		     * so therefore can be removed.
		     */
		    if (verbose)
			printf("%s: malformed queue file: remove\n",
			    (const char*) filename);
		    if (!nowork)
			Sys::unlink(filename);
		}
		delete req;			// NB: implicit close+unlock
	    } else {
		if (verbose)
		    printf("%s: flock(LOCK_SH|LOCK_NB): %s\n",
			(const char*) filename, strerror(errno));
		Sys::close(fd);
	    }
	} else if (verbose)
	    printf("%s: open: %s\n", (const char*) filename, strerror(errno));
    }
    if (trace)
	printf("Done scanning %s directory\n", (const char*) doneDir);
    closedir(dir);
}

/*
 * Record references to documents.
 */
void
faxQCleanApp::collectRefs(const FaxRequest& req)
{
    for (u_int i = 0, n = req.requests.length(); i < n; i++) {
	const faxRequest& freq = req.requests[i];
	switch (freq.op) {
	case FaxRequest::send_pdf:
	case FaxRequest::send_pdf_saved:
	case FaxRequest::send_tiff:
	case FaxRequest::send_tiff_saved:
	case FaxRequest::send_postscript:
	case FaxRequest::send_postscript_saved:
	case FaxRequest::send_pcl:
	case FaxRequest::send_pcl_saved:
	    if (trace)
		printf("JOB %s: reference %s\n",
		    (const char*) req.jobid,
		    (const char*) freq.item);
	    docrefs[freq.item]++;
	    break;
	}
    }
}

/*
 * Archive completed fax job.
 */
void
faxQCleanApp::archiveJob(const FaxRequest&)
{
}

/*
 * Scan the document directory and look for stuff
 * that has no references in the sendq or doneq.
 * These documents are removed if they are older
 * than some threshold.
 */
void
faxQCleanApp::expungeCruft(void)
{
    if (trace)
	printf("Scan %s directory and remove unreferenced documents.\n",
	    (const char*) docDir);

    DIR* dir = Sys::opendir(docDir);
    if (dir == NULL) {
	printf("%s: Could not scan directory for unreferenced documents.\n",
	    (const char*) docDir);
	return;
    }
    time_t now = Sys::now();
    fxStr path(docDir | "/");
    for (dirent* dp = readdir(dir); dp; dp = readdir(dir)) {
	/*
	 * By convention, document files are known to be named
	 * with a leading ``doc'' or ``cover'' prefix.  We ignore
	 * any other files that may be present.
	 */
	if (strncmp(dp->d_name, "doc", 3) && strncmp(dp->d_name, "cover", 5)) {
	    if (trace)
		printf("%s%s: ignored, no leading \"doc\" in filename\n",
		    (const char*) path, dp->d_name);
	    continue;
	}
	fxStr file(path | dp->d_name);
	struct stat sb;
	if (Sys::stat(file, sb) < 0 || !S_ISREG(sb.st_mode)) {
	    if (trace)
		printf("%s: ignored, cannot stat or not a regular file\n",
		    (const char*) file);
	    continue;
	}
	/*
	 * Document files should only have one link to them except
	 * during the job preparation stage (i.e. when hfaxd has
	 * original in the tmp directory and a link in the docq
	 * directory).  Therefore if file has multiple links then
	 * it's not a candidate for removal.  Note that this assumes
	 * the hard links are not created by any imaging work such
	 * as done by tiff2fax.
	 */
	if (sb.st_nlink > 1) {			// can't be orphaned yet
	    if (trace)
		printf("%s: ignored, file has %u links\n",
		    (const char*) file, sb.st_nlink);
	    continue;
	}
	if (docrefs.find(dp->d_name)) {		// referenced from doneq
	    if (trace)
		printf("%s: ignored, file has %u references\n",
		    (const char*) file, docrefs[dp->d_name]);
	    continue;
	}
	if (now - sb.st_mtime < minDocAge) {	// not old enough
	    if (trace)
		printf("%s: ignored, file is too new to remove\n",
		    (const char*) file);
	    continue;
	}
	/*
	 * Document may be referenced from a job in the sendq
	 * or it may be orphaned.  Files with base-style names
	 * represent documents that can only have references
	 * from jobs in the doneq--these can safely be removed.
	 * Other documents are only removed if they are older
	 * than the threshold (checked above).
	 */
	u_int l = file.nextR(file.length(), '.');
	if (l != 0 && l < file.length()) {
	    if (isdigit(file[l])) {
		/*
		 * Filename has a jobid suffix (or should);
		 * look to see if the job still exists in
		 * the sendq.
		 */
		fxStr qfile = qFilePrefix | file.tail(file.length()-l);
		if (Sys::stat(qfile, sb) == 0) {
		    if (trace)
			printf("%s: file looks to be referenced by job\n",
			    (const char*) file);
		    continue;			// skip, in use
		}
	    } else if (strcmp(&file[l], "cover") == 0) {
		/*
		 * Cover page document has a jobid suffix
		 * at the front; look to see if the job still
		 * exists in the sendq.
		 */
		u_int prefix = docDir.length()+1+3;
		fxStr qfile = qFilePrefix | file.extract(prefix, l-1-prefix);
		if (Sys::stat(qfile, sb) == 0) {
		    if (trace)
			printf("%s: file looks to be referenced by job\n",
			    (const char*) file);
		    continue;			// skip, in use
		}
	    }
	}
	if (nowork || Sys::unlink(file) >= 0) {
	    if (verbose)
		printf("DOC %s: unreferenced document removed%s.\n"
		    , (const char*) file
		    , nowork ? " (not done)" : ""
		);
	} else {
	    if (verbose)
		printf("%s: error removing unreferenced document: %s.\n",
		    (const char*) file, strerror(errno));
	}
    }
    if (trace)
	printf("Done scanning %s directory\n", (const char*) docDir);
    closedir(dir);
}

static void
usage(const char* appName)
{
    fprintf(stderr,
	"usage: %s [-a] [-j time] [-d time] [-q queue-directory]\n",
	appName);
}

int
main(int argc, char** argv)
{
    faxApp::setupLogging("FaxQCleaner");

    fxStr appName = argv[0];
    u_int l = appName.length();
    appName = appName.tokenR(l, '/');

    faxApp::setupPermissions();

    faxApp::setOpts("j:d:q:antv");

    faxQCleanApp app;
    fxStr queueDir(FAX_SPOOLDIR);
    for (GetoptIter iter(argc, argv, faxApp::getOpts()); iter.notDone(); iter++)
	switch (iter.option()) {
	case 'a': app.setArchiving(true); break;
	case 'j': app.setJobAge(optarg); break;
	case 'd': app.setDocAge(optarg); break;
	case 'n': app.setNoWork(true); break;
	case 'q': queueDir = iter.optArg(); break;
	case 't': app.setTracing(true); break;
	case 'v': app.setVerbose(true); break;
	case '?': usage(appName);
	}
    if (Sys::chdir(queueDir) < 0) {
	fprintf(stderr, "%s: Can not change directory: %s.\n",
	    (const char*) queueDir, strerror(errno));
	exit(-1);
    }

    app.run();

    return 0;
}
