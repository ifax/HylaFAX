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

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <sys/file.h>
#include <tiffio.h>

#include "Dispatcher.h"

#include "FaxMachineInfo.h"
#include "FaxAcctInfo.h"
#include "FaxRequest.h"
#include "FaxTrace.h"
#include "FaxRecvInfo.h"
#include "Timeout.h"
#include "UUCPLock.h"
#include "DialRules.h"
#include "RE.h"
#include "Modem.h"
#include "Trigger.h"
#include "faxQueueApp.h"
#include "HylaClient.h"
#include "MemoryDecoder.h"
#include "FaxSendInfo.h"
#include "config.h"

/*
 * HylaFAX Spooling and Command Agent.
 */

const fxStr faxQueueApp::sendDir	= FAX_SENDDIR;
const fxStr faxQueueApp::docDir		= FAX_DOCDIR;
const fxStr faxQueueApp::clientDir	= FAX_CLIENTDIR;

fxStr strTime(time_t t)	{ return fxStr(fmtTime(t)); }

faxQueueApp::SchedTimeout::SchedTimeout()
{
    started = false;
    pending = false;
    lastRun = Sys::now() - 1;
}

faxQueueApp::SchedTimeout::~SchedTimeout() {}

void
faxQueueApp::SchedTimeout::timerExpired(long, long)
{
    if (faxQueueApp::instance().scheduling() ) {
    	start(0);
	return;
    }
    faxQueueApp::instance().runScheduler();
    started = false;
}

void
faxQueueApp::SchedTimeout::start(u_short s)
{
    /*
     * If we don't throttle the scheduler then large
     * queues can halt the system with CPU consumption.
     * So we keep the scheduler from running more than
     * once per second.
     */
    if (!started && Sys::now() > lastRun) {
	started = true;
	pending = false;
	Dispatcher::instance().startTimer(s, 1, this);
	lastRun = Sys::now() + s;
    } else {
	if (!pending && lastRun <= Sys::now()) {
	    /*
	     * The scheduler is either running now or has been run
	     * within the last second and there are no timers set
	     * to trigger another scheduler run.  So we set a
	     * timer to go off in one second to avoid a stalled
	     * run queue.
	     */
	    Dispatcher::instance().startTimer(s + 1, 0, this);
	    lastRun = Sys::now() + 1 + s;
	    pending = true;
	}
    }
}

faxQueueApp* faxQueueApp::_instance = NULL;

faxQueueApp::faxQueueApp()
    : configFile(FAX_CONFIG)
{
    fifo = -1;
    quit = false;
    dialRules = NULL;
    inSchedule = false;
    setupConfig();

    fxAssert(_instance == NULL, "Cannot create multiple faxQueueApp instances");
    _instance = this;
}

faxQueueApp::~faxQueueApp()
{
    HylaClient::purge();
    delete dialRules;
}

faxQueueApp& faxQueueApp::instance() { return *_instance; }

void
faxQueueApp::initialize(int argc, char** argv)
{
    updateConfig(configFile);		// read config file
    faxApp::initialize(argc, argv);

    logInfo("%s", HYLAFAX_VERSION);
    logInfo("%s", "Copyright (c) 1990-1996 Sam Leffler");
    logInfo("%s", "Copyright (c) 1991-1996 Silicon Graphics, Inc.");

    scanForModems();
}

void
faxQueueApp::open()
{
    faxApp::open();
    scanQueueDirectory();
    Modem::broadcast("HELLO");		// announce queuer presence
    scanClientDirectory();		// announce queuer presence
    pokeScheduler();
}

/*
 * Scan the spool area for modems.  We can't be certain the
 * modems are actively working without probing them; this
 * work is done simply to buildup the internal database for
 * broadcasting a ``HELLO'' message.  Later on, individual
 * modems are enabled for use based on messages received
 * through the FIFO.
 */
void
faxQueueApp::scanForModems()
{
    DIR* dir = Sys::opendir(".");
    if (dir == NULL) {
	logError("Could not scan directory for modems");
	return;
    }
    fxStr fifoMatch(fifoName | ".");
    for (dirent* dp = readdir(dir); dp; dp = readdir(dir)) {
	if (dp->d_name[0] != fifoName[0])
	    continue;
	if (!strneq(dp->d_name, fifoMatch, fifoMatch.length()))
	    continue;
	if (Sys::isFIFOFile(dp->d_name)) {
	    fxStr devid(dp->d_name);
	    devid.remove(0, fifoMatch.length()-1);	// NB: leave "."
	    if (Sys::isRegularFile(FAX_CONFIG | devid)) {
		devid.remove(0);			// strip "."
		(void) Modem::getModemByID(devid);	// adds to list
	    }
	}
    }
    closedir(dir);
}

/*
 * Scan the spool directory for queue files and
 * enter them in the queues of outgoing jobs.
 */
void
faxQueueApp::scanQueueDirectory()
{
    DIR* dir = Sys::opendir(sendDir);
    if (dir == NULL) {
	logError("Could not scan %s directory for outbound jobs",
		(const char*)sendDir);
	return;
    }
    for (dirent* dp = readdir(dir); dp; dp = readdir(dir)) {
	if (dp->d_name[0] == 'q')
	    submitJob(&dp->d_name[1], true);
    }
    closedir(dir);
}

/*
 * Scan the client area for active client processes
 * and send a ``HELLO message'' to notify them the
 * queuer process has restarted.  If no process is
 * listening on the FIFO, remove it; the associated
 * client state will be purged later.
 */
void
faxQueueApp::scanClientDirectory()
{
    DIR* dir = Sys::opendir(clientDir);
    if (dir == NULL) {
	logError("Could not scan %s directory for clients",
		(const char*) clientDir);
	return;
    }
    for (dirent* dp = readdir(dir); dp; dp = readdir(dir)) {
	if (!isdigit(dp->d_name[0]))
	    continue;
	fxStr fifo(clientDir | "/" | dp->d_name);
	if (Sys::isFIFOFile((const char*) fifo))
	    if (!HylaClient::getClient(fifo).send("HELLO", 6))
		Sys::unlink(fifo);
    }
    closedir(dir);
}


void
faxQueueApp::startBatch(Modem* modem, Job& job, FaxRequest* req, DestInfo& di)
{
   traceJob(job, "BATCH");
   Batch* b = new Batch(di, job.dest, req->jobtype);
   b->insert(*batchq.next);
   b->modem = job.modem;
   di.active(*b);
   processJob(*b, job, req);
}


/*
 * canJobBatch()
 * Check if the following job can actually go into a batch.
 * This should check a bunch of things (like compatibility of JCI, etc.
 * but for now, we only check the modem is OK.
 */
bool
faxQueueApp::canJobBatch(Batch& batch, Job& job, FaxRequest* req)
{
    if (batch.jobs.isEmpty())
	return true;

    if (! batch.modem->isInGroup(req->modem))
    {
	traceJob(job, "Not added to BATCH because specified modem not compatible");
	return false;
    }

    if (! batch.firstJob().getJCI().isCompatible(job.getJCI()))
    {
	traceJob(job, "Not added to BATCH because JobControlInfo not compatible");
	return false;
    }

    return true;
}


/*
 * Here, we look for new jobs to put into the batch.
 */
void
faxQueueApp::fillBatch(Batch& batch)
{
    if (! (batch.jobCount() < maxBatchJobs) )
    {
	traceQueue(batch.di, "No more jobs allowed in batch: max %d reached",
		maxBatchJobs);
	sendStart(batch);
	return;
    }

    for (JobIter iter(batch.di.readyQ); iter.notDone(); iter++)
    {
	Job& job(iter);

	FaxRequest* req = readRequest(job);
	if (! req)
	{
	    // NB: no way to notify the user (XXX)
	    logError("JOB %s: qfile vanished while on the runq",
		    (const char*)job.jobid);
	    setDead(job);
	    continue;
	}

	if (! isJobSendOK(job, req) )
	    continue;

	/*
	 * Check if we can batch this job.  It might not be batchable
	 * for some reason (like modem group compatiblity, etc)
	 */
	if (canJobBatch(batch, job, req) )
	{
	    traceJob(job, "Adding to batch");
	    job.modem = batch.modem;
	    job.remove();
	    processJob(batch, job, req);
	    return;				// processJob() is async
	}

	delete req;
    }

    /*
     * There are no other jobs that we *can* batch right now, because
     * we reached the end of the di.jobs list of ready jobs
     */
    sendStart(batch);
}

/*
 * Process a job.  Prepare it for transmission and
 * pass it on to the thread that does the actual
 * transmission work.  The job is marked ``active to
 * this destination'' prior to preparing it because
 * preparation may involve asynchronous activities.
 * The job is placed on the active list so that it
 * can be located by filename if necessary.
 */
void
faxQueueApp::processJob(Batch& batch, Job& job, FaxRequest* req)
{
    job.commid = "";			// set on return
    req->result.clear();			// Clear for new procssing
    JobStatus status;
    setActive(batch,job);
    req->status = send_nobatch;
    updateRequest(*req, job);
    if (!prepareJobNeeded(job, *req, status)) {
	if (status != Job::done) {
	    job.state = FaxRequest::state_failed;
	    deleteRequest(job, req, status, true);
	    setDead(job);
	} else {
	    delete req;
	    fillBatch(batch);
	}
    } else
	prepareStart(batch, job, req);
}

/*
 * Check if the job requires preparation that should
 * done in a fork'd copy of the server.  A sub-fork is
 * used if documents must be converted or a continuation
 * cover page must be crafted (i.e. the work may take
 * a while).
 */
bool
faxQueueApp::prepareJobNeeded(Job& job, FaxRequest& req, JobStatus& status)
{
    if (!req.items.length()) {
	req.result = Status(323, "Job contains no documents");
	status = Job::rejected;
	jobError(job, "SEND REJECT: %s", req.result.string());
	return (false);
    }
    for (u_int i = 0, n = req.items.length(); i < n; i++)
	switch (req.items[i].op) {
	case FaxRequest::send_postscript:	// convert PostScript
	case FaxRequest::send_pcl:		// convert PCL
	case FaxRequest::send_tiff:		// verify&possibly convert TIFF
	case FaxRequest::send_pdf:		// convert PDF
	    return (true);
	case FaxRequest::send_poll:		// verify modem is capable
	    if (!job.modem->supportsPolling()) {
		req.result = Status(324, "Modem does not support polling");
		status = Job::rejected;
		jobError(job, "SEND REJECT: %s", req.result.string());
		return (false);
	    }
	    break;
	}
    status = Job::done;
    return (req.cover != "");			// need continuation cover page
}

/*
 * Handler used by job preparation subprocess
 * to pass signal from parent queuer process.
 * We mark state so job preparation will be aborted
 * at the next safe point in the procedure.
 */
void
faxQueueApp::prepareCleanup(int s)
{
    int old_errno = errno;
    signal(s, fxSIGHANDLER(faxQueueApp::prepareCleanup));
    logError("CAUGHT SIGNAL %d, ABORT JOB PREPARATION", s);
    faxQueueApp::instance().abortPrepare = true;
    errno = old_errno;
}

/*
 * Start job preparation in a sub-fork.  The server process
 * forks and sets up a Dispatcher handler to reap the child
 * process.  The exit status from the child is actually the
 * return value from the prepareJob method; this and a
 * reference to the original Job are passed back into the
 * server thread at which point the transmit work is actually
 * initiated.
 */
void
faxQueueApp::prepareStart(Batch& batch, Job& job, FaxRequest* req)
{
    traceQueue(job, "PREPARE START");
    abortPrepare = false;
    pid_t pid = fork();
    switch (pid) {
    case 0:				// child, do work
	/*
	 * NB: There is a window here where the subprocess
	 * doing the job preparation can have the old signal
	 * handlers installed when a signal comes in.  This
	 * could be fixed by using the appropriate POSIX calls
	 * to block and unblock signals, but signal usage is
	 * quite tenuous (i.e. what is and is not supported
	 * on a system), so rather than depend on this
	 * functionality being supported, we'll just leave
	 * the (small) window in until it shows itself to
	 * be a problem.
	 */
	signal(SIGTERM, fxSIGHANDLER(faxQueueApp::prepareCleanup));
	signal(SIGINT, fxSIGHANDLER(faxQueueApp::prepareCleanup));
	_exit(prepareJob(job, *req, batch));
	/*NOTREACHED*/
    case -1:				// fork failed, sleep and retry
	job.remove();			// Remove from active queue
	delayJob(job, *req, Status(340, "Could not fork to prepare job for transmission"),
	    Sys::now() + random() % requeueInterval);
	delete req;
	fillBatch(batch);
    default:				// parent, setup handler to wait
	batch.startPrepare(job, pid);
	job.pid = pid;
	delete req;			// must reread after preparation
	Trigger::post(Trigger::JOB_PREP_BEGIN, job);
	break;
    }
}

/*
 * Handle notification from the sub-fork that job preparation
 * is done.  The exit status is checked and interpreted as the
 * return value from prepareJob if it was passed via _exit.
 */
void
faxQueueApp::prepareDone(Batch& batch, int status)
{
    Job& job = batch.lastJob();

    traceQueue(job, "PREPARE DONE");
    Trigger::post(Trigger::JOB_PREP_END, job);
    if (status&0xff) {
	logError("JOB %s: bad exit status %#x from sub-fork",
	    (const char*) job.jobid, status);
	status = Job::failed;
    } else
	status >>= 8;

    job.pid = 0;

    if (job.suspendPending) {		// co-thread waiting
	job.modem = NULL;
	job.remove();			// remove from batch
	job.insert(suspendq);		// co-thread expects it on a list
	job.suspendPending = false;
    } else if (status != Job::done) {
	/*
	 * We need to clear the modem so the following delete/requeue/dead
	 * doesn't come across it.  The modem's released by the batch
	 * ending
	 */
	job.modem = NULL;
	FaxRequest* req = readRequest(job);
	if (!req) {
	    // NB: no way to notify the user (XXX)
	    logError("JOB %s: qfile vanished during preparation",
		(const char*) job.jobid);
	    setDead(job);
	} else if (status == Job::requeued) {
	    delayJob(job, *req, Status(340, "Cannot fork to prepare job for transmission"),
		    Sys::now() + random() % requeueInterval);
	    delete req;
	} else {
	    deleteRequest(job, req, status, true);
	    setDead(job);
	}
    }
    fillBatch(batch);
}

/*
 * Document Use Database.
 *
 * The server minimizes imaging operations by checking for the
 * existence of compatible, previously imaged, versions of documents.
 * This is done by using a file naming convention that includes the
 * source document name and the remote machine capabilities that are
 * used for imaging.  The work done here (and in other HylaFAX apps)
 * also assumes certain naming convention used by hfaxd when creating
 * document files.  Source documents are named:
 *
 *     doc<docnum>.<type>
 *
 * where <docnum> is a unique document number that is assigned by
 * hfaxd at the time the document is stored on the server.  Document
 * references by a job are then done using filenames (i.e. hard
 * links) of the form:
 *
 *	doc<docnum>.<type>.<jobid>
 *
 * where <jobid> is the unique identifier assigned to each outbound
 * job.  Then, each imaged document is named:
 *
 *	doc<docnum>.<type>;<encoded-capabilities>
 *
 * where <encoded-capabilities> is a string that encodes the remote
 * machine's capabilities.
 *
 * Before imaging a document the scheduler checks to see if there is
 * an existing file with the appropriate name.  If so then the file
 * is used and no preparation work is needed for sending the document.
 * Otherwise the document must be converted for transmission; this
 * result is written to a file with the appropriate name.  After an
 * imaged document has been transmitted it is not immediately removed,
 * but rather the scheduler is informed that the job no longer holds
 * (needs) a reference to the document and the scheduler records this
 * information so that when no jobs reference the original source
 * document, all imaged forms may be expunged.  As documents are
 * transmitted the job references to the original source documents are
 * converted to references to the ``base document name'' (the form
 * without the <jobid>) so that the link count on the inode for this
 * file reflects the number of references from jobs that are still
 * pending transmission.  This means that the scheduler can use the
 * link count to decide when to expunge imaged versions of a document.
 *
 * Note that the reference counts used here do not necessarily
 * *guarantee* that a pre-imaged version of a document will be available.
 * There are race conditions under which a document may be re-imaged
 * because a previously imaged version was removed.
 *
 * A separate document scavenger program should be run periodically
 * to expunge any document files that might be left in the docq for
 * unexpected reasons.  This program should read the set of jobs in
 * the sendq to build a onetime table of uses and then remove any
 * files found in the docq that are not referenced by a job.
 */

/*
 * Remove a reference to an imaged document and if no
 * references exist for the corresponding source document,
 * expunge all imaged versions of the document.
 */
void
faxQueueApp::unrefDoc(const fxStr& file)
{
    /*
     * Convert imaged document name to the base
     * (source) document name by removing the
     * encoded session parameters used for imaging.
     */
    u_int l = file.nextR(file.length(), ';');
    if (l == 0) {
	logError("Bogus document handed to unrefDoc: %s", (const char*) file);
	return;
    }
    fxStr doc = file.head(l-1);
    /*
     * Add file to the list of pending removals.  We
     * do this before checking below so that the list
     * of files will always have something on it.
     */
    fxStr& files = pendingDocs[doc];
    if (files.find(0, file) == files.length())		// suppress duplicates
	files.append(file | " ");
    if (tracingLevel & FAXTRACE_DOCREFS)
	logInfo("DOC UNREF: %s files %s",
	    (const char*) file, (const char*) files);
    /*
     * The following assumes that any source document has
     * been renamed to the base document name *before* this
     * routine is invoked (either directly or via a msg
     * received on a FIFO).  Specifically, if the stat
     * call fails we assume the file does not exist and
     * that it is safe to remove the imaged documents.
     * This is conservative and if wrong will not break
     * anything; just potentially cause extra imaging
     * work to be done.
     */
    struct stat sb;
    if (Sys::stat(doc, sb) < 0 || sb.st_nlink == 1) {
	if (tracingLevel & FAXTRACE_DOCREFS)
	    logInfo("DOC UNREF: expunge imaged files");
	/*
	 * There are no additional references to the
	 * original source document (all references
	 * should be from completed jobs that reference
	 * the original source document by its basename).
	 * Expunge imaged documents that were waiting for
	 * all potential uses to complete.
	 */
	l = 0;
	do {
	    (void) Sys::unlink(files.token(l, ' '));
	} while (l < files.length());
	pendingDocs.remove(doc);
    }
}

#include "class2.h"

/*
 * Prepare a job by converting any user-submitted documents
 * to a format suitable for transmission.
 */
JobStatus
faxQueueApp::prepareJob(Job& job, FaxRequest& req,
    const FaxMachineInfo& info)
{
    /*
     * Select imaging parameters according to requested
     * values, client capabilities, and modem capabilities.
     * Note that by this time we believe the modem is capable
     * of certain requirements needed to transmit the document
     * (based on the capabilities passed to us by faxgetty).
     */
    Class2Params params;
    
    /*
     * User requested vres (98 or 196) and usexvres (1=true or 0=false)
     */
    int vres = req.resolution;
    int usexvres = req.usexvres;
    /*
     * System overrides in destcontrols:
     * VRes: we check for vres = 98 or vres = 196 in destroncontrols;
     *       if vres is not set getVRes returns 0.
     * UseXVres: we check for usexvres = 0 or usexvres = 1 in destcontrols;
     *           if usexvres is not set getUseXVRes retuns -1.
     */
    if (job.getJCI().getVRes() == 98)
	vres = 98;
    else if (job.getJCI().getVRes() == 196)
	vres = 196;
    if (job.getJCI().getUseXVRes() == 0)
	usexvres = 0;
    else if (job.getJCI().getUseXVRes() == 1)
	usexvres = 1;

    // use the highest resolution the client supports
    params.vr = VR_NORMAL;

    if (usexvres) {
	if (info.getSupportsVRes() & VR_200X100 && job.modem->supportsVR(VR_200X100))
	    params.vr = VR_200X100;
	if (info.getSupportsVRes() & VR_FINE && job.modem->supportsVR(VR_FINE))
	    params.vr = VR_FINE;
	if (info.getSupportsVRes() & VR_200X200 && job.modem->supportsVR(VR_200X200))
	    params.vr = VR_200X200;
	if (info.getSupportsVRes() & VR_R8 && job.modem->supportsVR(VR_R8))
	    params.vr = VR_R8;
	if (info.getSupportsVRes() & VR_200X400 && job.modem->supportsVR(VR_200X400))
	    params.vr = VR_200X400;
	if (info.getSupportsVRes() & VR_300X300 && job.modem->supportsVR(VR_300X300))
	    params.vr = VR_300X300;
	if (info.getSupportsVRes() & VR_R16 && job.modem->supportsVR(VR_R16))
	    params.vr = VR_R16;
    } else {				// limit ourselves to normal and fine
	if (vres > 150) {
	    if (info.getSupportsVRes() & VR_FINE && job.modem->supportsVR(VR_FINE))
		params.vr = VR_FINE;
	}
    }
    params.setPageWidthInMM(
	fxmin((u_int) req.pagewidth, (u_int) info.getMaxPageWidthInMM()));

    /*
     * Follow faxsend and use unlimited page length whenever possible.
     */
    useUnlimitedLN = (info.getMaxPageLengthInMM() == (u_short) -1);
    params.setPageLengthInMM(
	fxmin((u_int) req.pagelength, (u_int) info.getMaxPageLengthInMM()));

    /*
     * Generate MMR or 2D-encoded facsimile if:
     * o the server is permitted to generate it,
     * o the modem is capable of sending it,
     * o the remote side is known to be capable of it, and
     * o the user hasn't specified a desire to send 1D data.
     */
    int jcdf = job.getJCI().getDesiredDF();
    if (jcdf != -1) req.desireddf = jcdf;
    if (req.desireddf == DF_2DMMR && (req.desiredec != EC_DISABLE) && 
	use2D && job.modem->supportsMMR() &&
	 (! info.getCalledBefore() || info.getSupportsMMR()) )
	    params.df = DF_2DMMR;
    else if (req.desireddf > DF_1DMH) {
	params.df = (use2D && job.modem->supports2D() &&
	    (! info.getCalledBefore() || info.getSupports2DEncoding()) ) ?
		DF_2DMR : DF_1DMH;
    } else
	params.df = DF_1DMH;
    /*
     * Check and process the documents to be sent
     * using the parameter selected above.
     */
    JobStatus status = Job::done;
    bool updateQFile = false;
    fxStr tmp;		// NB: here to avoid compiler complaint
    u_int i = 0;
    while (i < req.items.length() && status == Job::done && !abortPrepare) {
	FaxItem& fitem = req.items[i];
	switch (fitem.op) {
	case FaxRequest::send_postscript:	// convert PostScript
	case FaxRequest::send_pcl:		// convert PCL
	case FaxRequest::send_tiff:		// verify&possibly convert TIFF
        case FaxRequest::send_pdf:		// convert PDF
	    tmp = FaxRequest::mkbasedoc(fitem.item) | ";" | params.encodePage();
	    status = convertDocument(job, fitem, tmp, params, req.result);
	    if (status == Job::done) {
		/*
		 * Insert converted file into list and mark the
		 * original document so that it's saved, but
		 * not processed again.  The converted file
		 * is sent, while the saved file is kept around
		 * in case it needs to be returned to the sender.
		 */
		fitem.op++;			// NB: assumes order of enum
		req.insertFax(i+1, tmp);
	    } else
		Sys::unlink(tmp);		// bail out
	    updateQFile = true;
	    break;
	}
	i++;
    }
    if (status == Job::done && !abortPrepare) {
	if (req.cover != "") {
	    /*
	     * Generate a continuation cover page if necessary.
	     * Note that a failure in doing this is not considered
	     * fatal; perhaps this should be configurable?
	     */
	    makeCoverPage(job, req, params);
	    updateQFile = true;
	}
	if (req.pagehandling == "" && !abortPrepare) {
	    /*
	     * Calculate/recalculate the per-page session parameters
	     * and check the page count against the max pages.
	     */
            Status r;
	    if (!preparePageHandling(job, req, info, r)) {
		status = Job::rejected;		// XXX
		req.result= Status(314, "Document preparation failed: %s"
			, r.string());
	    }
	    updateQFile = true;
	}    
    }
    if (updateQFile)
	updateRequest(req, job);
    return (status);
}

/*
 * Prepare the job for transmission by analysing
 * the page characteristics and determining whether
 * or not the page transfer parameters will have
 * to be renegotiated after the page is sent.  This
 * is done before the call is placed because it can
 * be slow and there can be timing problems if this
 * is done during transmission.
 */
bool
faxQueueApp::preparePageHandling(Job& job, FaxRequest& req,
    const FaxMachineInfo& info, Status& result)
{
    /*
     * Figure out whether to try chopping off white space
     * from the bottom of pages.  This can only be done
     * if the remote device is thought to be capable of
     * accepting variable-length pages.
     */
    u_int pagechop;
    if (info.getMaxPageLengthInMM() == (u_short)-1) {
	pagechop = req.pagechop;
	if (pagechop == FaxRequest::chop_default)
	    pagechop = pageChop;
    } else
	pagechop = FaxRequest::chop_none;
    u_int maxPages = job.getJCI().getMaxSendPages();
    Range range(1,maxPages);
    if (req.pagerange.length())
	range.parse(req.pagerange);
    /*
     * Scan the pages and figure out where session parameters
     * will need to be renegotiated.  Construct a string of
     * indicators to use when doing the actual transmission.
     *
     * NB: all files are coalesced into a single fax document
     *     if possible
     */
    Class2Params params;		// current parameters
    Class2Params next;			// parameters for ``next'' page
    TIFF* tif = NULL;			// current open TIFF image
    req.totpages = req.npages;		// count pages previously transmitted
    req.skippages = req.nskip;
    req.coverpages = req.ncover;
    bool firstpage = true;
    bool skiplast = false;
    bool coverdoc = false;
    for (u_int i = 0;;) {
	if (!tif || TIFFLastDirectory(tif)) {
	    /*
	     * Locate the next file to be sent.
	     */
	    if (tif)			// close previous file
		TIFFClose(tif), tif = NULL;
	    if (i >= req.items.length()) {
		if (skiplast) {					// skip previous page
		    req.skippages++;
		    req.pagehandling.append('X');
		    req.pagehandling.replace('#', 'P');
		} else
		    req.pagehandling.append('P');		// EOP
		return (true);
	    }
	    i = req.findItem(FaxRequest::send_fax, i);
	    if (i == fx_invalidArrayIndex) {
		if (skiplast) {				// skip previous page
		    req.skippages++;
		    req.pagehandling.append('X');
		    req.pagehandling.replace('#', 'P');
		} else
		    req.pagehandling.append('P');		// EOP
		return (true);
	    }
	    const FaxItem& fitem = req.items[i];

	    logDebug("req.items[%d].item = \"%s\" (%s)", i,
		    (const char*)fitem.item, (const char*)req.cover);
	    if (fitem.item.find(0, "/cover") < fitem.item.length())
		coverdoc = true;
	    else
		coverdoc = false;

	    tif = TIFFOpen(fitem.item, "r");
	    if (tif == NULL) {
		result = Status(314, "Can not open document file %s", (const char*)fitem.item);
		if (tif)
		    TIFFClose(tif);
		return (false);
	    }
	    if (fitem.dirnum != 0 && !TIFFSetDirectory(tif, fitem.dirnum)) {
		result = Status(315,
				fxStr::format("Can not set directory %u in document file %s"
		    , fitem.dirnum
		    , (const char*) fitem.item)
		);
		if (tif)
		    TIFFClose(tif);
		return (false);
	    }
	    i++;			// advance for next find
	} else {
	    /*
	     * Read the next TIFF directory.
	     */
	    if (!TIFFReadDirectory(tif)) {
		result = Status(316, "Error reading directory %u in document file %s"
		    , TIFFCurrentDirectory(tif)
		    , TIFFFileName(tif));
		if (tif)
		    TIFFClose(tif);
		return (false);
	    }
	}
	if (++req.totpages > maxPages) {
	    result = Status(317, "Too many pages in submission; max %u"
		,maxPages);
	    if (tif)
		TIFFClose(tif);
	    return (false);
	}
	next = params;
	setupParams(tif, next, info);

	if (coverdoc)
	    req.coverpages++;

	u_int p = req.totpages - req.coverpages;

	if (!firstpage) {
	    /*
	     * The pagehandling string has:
	     * 'M' = EOM, for when parameters must be renegotiated
	     * 'S' = MPS, for when next page uses the same parameters
	     * 'P' = EOP, for the last page to be transmitted
	     * 'X' for when the page is to be skipped
	     *
	     * '#' is used temporarily when the right flag is unknown
	     */
	    char c = next == params ? 'S' : 'M';
	    if (skiplast) {	// skip previous page
		req.skippages++;
		req.pagehandling.append('X');
		if (coverdoc || range.contains(p))
		{
		    req.pagehandling.replace('#', c);
		    skiplast = false;
		}
	    } else if (!range.contains(p))
	    {
		req.pagehandling.append('#');
		skiplast = true;
	    } else
	    {
		req.pagehandling.append(c);
	    }
	} else {
	    if (! (coverdoc || range.contains(p)) )
		skiplast = true;
	    firstpage = false;
	}
	/*
	 * Record the session parameters needed by each page
	 * so that we can set the initial session parameters
	 * as needed *before* dialing the telephone.  This is
	 * to cope with Class 2 modems that do not properly
	 * implement the +FDIS command.
	 */
	req.pagehandling.append(next.encodePage());
	/*
	 * If page is to be chopped (i.e. sent with trailing white
	 * space removed so the received page uses minimal paper),
	 * scan the data and, if possible, record the amount of data
	 * that should not be sent.  The modem drivers will use this
	 * information during transmission if it's actually possible
	 * to do the chop (based on the negotiated session parameters).
	 */
	if (pagechop == FaxRequest::chop_all ||
	  (pagechop == FaxRequest::chop_last && TIFFLastDirectory(tif)))
	    preparePageChop(req, tif, next, req.pagehandling);
	params = next;
    }
}

/*
 * Select session parameters according to the info
 * in the TIFF file.  We setup the encoding scheme,
 * page width & length, and vertical-resolution
 * parameters.
 */
void
faxQueueApp::setupParams(TIFF* tif, Class2Params& params, const FaxMachineInfo& info)
{
    uint16 compression = 0;
    (void) TIFFGetField(tif, TIFFTAG_COMPRESSION, &compression);
    if (compression == COMPRESSION_CCITTFAX4) {
	params.df = DF_2DMMR;
    } else {
	uint32 g3opts = 0;
	TIFFGetField(tif, TIFFTAG_GROUP3OPTIONS, &g3opts);
	params.df = (g3opts&GROUP3OPT_2DENCODING ? DF_2DMR : DF_1DMH);
    }

    uint32 w;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    params.setPageWidthInPixels((u_int) w);

    /*
     * Try to deduce the vertical resolution of the image
     * image.  This can be problematical for arbitrary TIFF
     * images 'cuz vendors sometimes don't give the units.
     * We, however, can depend on the info in images that
     * we generate 'cuz we're careful to include valid info.
     */
    float yres, xres;
    if (TIFFGetField(tif, TIFFTAG_YRESOLUTION, &yres) && TIFFGetField(tif, TIFFTAG_XRESOLUTION, &xres)) {
	uint16 resunit;
	TIFFGetFieldDefaulted(tif, TIFFTAG_RESOLUTIONUNIT, &resunit);
	if (resunit == RESUNIT_CENTIMETER)
	    yres *= 25.4;
	    xres *= 25.4;
	params.setRes((u_int) xres, (u_int) yres);
    } else {
	/*
	 * No resolution is specified, try
	 * to deduce one from the image length.
	 */
	uint32 l;
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &l);
	// B4 at 98 lpi is ~1400 lines
	params.setRes(204, (l < 1450 ? 98 : 196));
    }

    /*
     * Select page length according to the image size and
     * vertical resolution.  Note that if the resolution
     * info is bogus, we may select the wrong page size.
     */
    if (info.getMaxPageLengthInMM() != (u_short)-1) {
	uint32 h;
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
	params.setPageLengthInMM((u_int)(h / yres));
    } else
	params.ln = LN_INF;
}

void
faxQueueApp::preparePageChop(const FaxRequest& req,
    TIFF* tif, const Class2Params& params, fxStr& pagehandling)
{
    tstrip_t s = TIFFNumberOfStrips(tif)-1;
    tiff_bytecount_t* stripbytecount;
    (void) TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &stripbytecount);
    u_int stripSize = (u_int) stripbytecount[s];
    if (stripSize == 0)
	return;
    u_char* data = new u_char[stripSize];
    if (TIFFReadRawStrip(tif, s, data, stripSize) >= 0) {
	uint16 fillorder;
	TIFFGetFieldDefaulted(tif, TIFFTAG_FILLORDER, &fillorder);

	MemoryDecoder dec(data, stripSize);
	dec.scanPageForBlanks(fillorder, params);

	float threshold = req.chopthreshold;
	if (threshold == -1)
	    threshold = pageChopThreshold;
	u_int minRows = 0;
	switch(params.vr) {
	    case VR_NORMAL:
	    case VR_200X100:
		minRows = (u_int) (98. * threshold);
		break;
	    case VR_FINE:
	    case VR_200X200:
		minRows = (u_int) (196. * threshold);
		break;
	    case VR_300X300:
		minRows = (u_int) (300. * threshold);
		break;
	    case VR_R8:
	    case VR_R16:
	    case VR_200X400:
		minRows = (u_int) (391. * threshold);
		break;
	}
	if (dec.getLastBlanks() > minRows)
	{
	    pagehandling.append(fxStr::format("Z%04x",
		fxmin((u_int)0xFFFF, (u_int)(stripSize - (dec.getEndOfPage() - data)))));
	}
    }
    delete [] data;
}

/*
 * Convert a document into a form suitable
 * for transmission to the remote fax machine.
 */
JobStatus
faxQueueApp::convertDocument(Job& job,
    const FaxItem& req,
    const fxStr& outFile,
    const Class2Params& params,
    Status& result)
{
    JobStatus status;
    /*
     * Open/create the target file and lock it to guard against
     * concurrent jobs imaging the same document with the same
     * parameters.  The parent will hold the open file descriptor
     * for the duration of the imaging job.  Concurrent jobs will
     * block on flock and wait for the imaging to be completed.
     * Previously imaged documents will be flock'd immediately
     * and reused without delays.
     *
     * NB: There is a race condition here.  One process may create
     * the file but another may get the shared lock above before
     * the exclusive lock below is captured.  If this happens
     * then the exclusive lock will block temporarily, but the
     * process with the shared lock may attempt to send a document
     * before it's preparation is completed.  We could add a delay
     * before the shared lock but that would slow down the normal
     * case and the window is small--so let's leave it there for now.
     */
    int fd = Sys::open(outFile, O_RDWR|O_CREAT|O_EXCL, 0600);
    if (fd == -1) {
	if (errno == EEXIST) {
	    /*
	     * The file already exist, flock it in case it's
	     * being created (we'll block until the imaging
	     * is completed).  Otherwise, the document imaging
	     * has already been completed and we can just use it.
	     */
	    fd = Sys::open(outFile, O_RDWR);	// NB: RDWR for flock emulations
	    if (fd != -1) {
		if (flock(fd, LOCK_SH) == -1) {
		    status = Job::format_failed;
		    result = Status(318, "Unable to lock shared document file");
		} else
		    status = Job::done;
		(void) Sys::close(fd);		// NB: implicit unlock
	    } else {
		/*
		 * This *can* happen if document preparation done
		 * by another job fails (e.g. because of a time
		 * limit or a malformed PostScript submission).
		 */
		status = Job::format_failed;
		result = Status(319, "Unable to open shared document file");
	    }
	} else {
	    status = Job::format_failed;
	    result = Status(320, "Unable to create document file");
	}
	/*
	 * We were unable to open, create, or flock
	 * the file.  This should not happen.
	 */
	if (status != Job::done)
	    jobError(job, "CONVERT DOCUMENT: %s: %m", result.string());
    } else {
	(void) flock(fd, LOCK_EX);		// XXX check for errors?
	/*
	 * Imaged document does not exist, run the document converter
	 * to generate it.  The converter is invoked according to:
	 *   -o file		output (temp) file
	 *   -r <res>		output resolution (dpi)
	 *   -w <pagewidth>	output page width (pixels)
	 *   -l <pagelength>	output page length (mm)
	 *   -m <maxpages>	max pages to generate
	 *   -1|-2|-3		1d, 2d, or 2d-mmr encoding
	 */
	fxStr rbuf = fxStr::format("%u", params.verticalRes());
	fxStr wbuf = fxStr::format("%u", params.pageWidth());
	fxStr lbuf = fxStr::format("%d", params.pageLength());
	fxStr mbuf = fxStr::format("%u", job.getJCI().getMaxSendPages());
	const char* argv[30];
	int ac = 0;
	switch (req.op) {
	case FaxRequest::send_postscript: argv[ac++] = ps2faxCmd; break;
	case FaxRequest::send_pdf:	  argv[ac++] = pdf2faxCmd; break;
	case FaxRequest::send_pcl:	  argv[ac++] = pcl2faxCmd; break;
	case FaxRequest::send_tiff:	  argv[ac++] = tiff2faxCmd; break;
	}
	argv[ac++] = "-o"; argv[ac++] = outFile;
	argv[ac++] = "-r"; argv[ac++] = (const char*)rbuf;
	argv[ac++] = "-w"; argv[ac++] = (const char*)wbuf;
	argv[ac++] = "-l"; argv[ac++] = (const char*)lbuf;
	argv[ac++] = "-m"; argv[ac++] = (const char*)mbuf;
	if (useUnlimitedLN) argv[ac++] = "-U";
	if (params.df == DF_2DMMR)
	    argv[ac++] = "-3";
	else
	    argv[ac++] = params.df == DF_1DMH ? "-1" : "-2";
	argv[ac++] = req.item;
	argv[ac] = NULL;
	// XXX the (char* const*) is a hack to force type compatibility
	status = runConverter(job, argv[0], (char* const*) argv, result);
	if (status == Job::done) {
	    /*
	     * Many converters exit with zero status even when
	     * there are problems so scan the the generated TIFF
	     * to verify the integrity of the converted data.
	     *
	     * NB: We must reopen the file instead of using the
	     *     previously opened file descriptor in case the
	     *     converter creates a new file with the given
	     *     output filename instead of just overwriting the
	     *     file created above.  This can easily happen if,
	     *     for example, the converter creates a link from
	     *     the input file to the target (e.g. tiff2fax
	     *     does this when no conversion is required).
	     */
	    TIFF* tif = TIFFOpen(outFile, "r");
	    if (tif) {
		while (!TIFFLastDirectory(tif))
		    if (!TIFFReadDirectory(tif)) {
			status = Job::format_failed;
			result = Status(321, "Converted document is not valid TIFF");
			break;
		    }
		TIFFClose(tif);
	    } else {
		status = Job::format_failed;
		result = Status(322, "Could not reopen converted document to verify format");
	    }
	    if (status == Job::done)	// discard any debugging output
		result.clear();
	    else
		jobError(job, "CONVERT DOCUMENT: %s", result.string());
	} else if (status == Job::rejected)
	    jobError(job, "SEND REJECT: %s", result.string());
	(void) Sys::close(fd);		// NB: implicit unlock
    }
    return (status);
}

static void
closeAllBut(int fd)
{
    for (int f = Sys::getOpenMax()-1; f >= 0; f--)
	if (f != fd)
	    Sys::close(f);
}

/*
 * Startup a document converter program in a subprocess
 * with the output returned through a pipe.  We could just use
 * popen or similar here, but we want to detect fork failure
 * separately from others so that jobs can be requeued instead
 * of rejected.
 */
JobStatus
faxQueueApp::runConverter(Job& job, const char* app, char* const* argv, Status& result)
{
    fxStr cmdline(argv[0]);
    for (u_int i = 1; argv[i] != NULL; i++)
	cmdline.append(fxStr::format(" %s", argv[i]));
    traceQueue(job, "CONVERT DOCUMENT: %s", (const char*)cmdline);
    JobStatus status;
    int pfd[2];
    if (pipe(pfd) >= 0) {
	int fd;
	pid_t pid = fork();
	switch (pid) {
	case -1:			// error
	    jobError(job, "CONVERT DOCUMENT: fork: %m");
	    status = Job::requeued;	// job should be retried
	    Sys::close(pfd[1]);
	    break;
	case 0:				// child, exec command
	    if (pfd[1] != STDOUT_FILENO)
		dup2(pfd[1], STDOUT_FILENO);
	    closeAllBut(STDOUT_FILENO);
	    dup2(STDOUT_FILENO, STDERR_FILENO);
	    fd = Sys::open(_PATH_DEVNULL, O_RDWR);
	    if (fd != STDIN_FILENO)
	    {
		    dup2(fd, STDIN_FILENO);
		    Sys::close(fd);
	    }
	    Sys::execv(app, argv);
	    sleep(3);			// XXX give parent time to catch signal
	    _exit(255);
	    /*NOTREACHED*/
	default:			// parent, read from pipe and wait
	    Sys::close(pfd[1]);
	    fxStr output;
	    if (runConverter1(job, pfd[0], output)) {
		int estat = -1;
		(void) Sys::waitpid(pid, estat);
		if (estat)
		    jobError(job, "CONVERT DOCUMENT: exit status %#x", estat);
		switch (estat) {
		case 0:			 status = Job::done; break;
	        case (254<<8):		 status = Job::rejected; break;
		case (255<<8): case 255: status = Job::no_formatter; break;
		default:		 status = Job::format_failed; break;
		}
		result = Status(347, "%s", (const char*)output);
	    } else {
		kill(pid, SIGTERM);
		(void) Sys::waitpid(pid);
		status = Job::format_failed;
		result = Status(347, "%s", (const char*)output);
	    }
	    break;
	}
	Sys::close(pfd[0]);
    } else {
	jobError(job, "CONVERT DOCUMENT: pipe: %m");
	status = Job::format_failed;
    }
    return (status);
}

/*
 * Replace unprintable characters with ``?''s.
 */
static void
cleanse(char buf[], int n)
{
    while (--n >= 0)
	if (!isprint(buf[n]) && !isspace(buf[n]))
	    buf[n] = '?';
}

/*
 * Run the interpreter with the configured timeout and
 * collect the output from the interpreter in case there
 * is an error -- this is sent back to the user that
 * submitted the job.
 */
bool
faxQueueApp::runConverter1(Job& job, int fd, fxStr& output)
{
    int n;
    Timeout timer;
    timer.startTimeout(postscriptTimeout*1000);
    char buf[1024];
    while ((n = Sys::read(fd, buf, sizeof (buf))) > 0 && !timer.wasTimeout()) {
	cleanse(buf, n);
	output.append(buf, n);
    }
    timer.stopTimeout();
    if (timer.wasTimeout()) {
	jobError(job, "CONVERT DOCUMENT: job time limit exceeded");
	if (output.length() > 0)
	    output.append('\n');
	output.append("[Job time limit exceeded]\n");
	return (false);
    } else
	return (true);
}

/*
 * Generate a continuation cover page and insert it in
 * the array of files to be sent.  Note that we assume
 * the cover page command generates PostScript which we
 * immediately image, discarding the PostScript.  We
 * could have the cover page command script do this, but
 * then it would need to know how to invoke the PostScript
 * imager per the job characteristics.  Note that we could
 * optimize things here by updating the pagehandling and
 * page counts for the job instead of resetting pagehandling
 * so that everything just gets recalculated from scratch.
 */
void
faxQueueApp::makeCoverPage(Job& job, FaxRequest& req, const Class2Params& params)
{
    FaxItem fitem(FaxRequest::send_postscript, 0, fxStr::null, req.cover);
    fxStr cmd(coverCmd
	| " " | req.qfile
	| " " | contCoverPageTemplate
	| " " | fitem.item
    );
    traceQueue(job, "COVER PAGE: %s", (const char*)cmd);
    if (runCmd(cmd, true)) {
	Status result;
	fxStr tmp = fitem.item | ";" | params.encodePage();
	if (convertDocument(job, fitem, tmp, params, result)) {
	    req.insertFax(0, tmp);
	    req.cover = tmp;			// needed in sendJobDone
	    req.pagehandling = "";		// XXX force recalculation
	} else {
	    jobError(job, "SEND: No continuation cover page, "
		" document conversion failed: %s", result.string());
	}
	Sys::unlink(fitem.item);
    } else {
	jobError(job,
	    "SEND: No continuation cover page, generation cmd failed");
    }
}

const fxStr&
faxQueueApp::pickCmd(const fxStr& jobtype)
{
    if (jobtype == "pager")
	return (sendPageCmd);
    if (jobtype == "uucp")
	return (sendUUCPCmd);
    return (sendFaxCmd);			// XXX gotta return something
}

/*
 * Setup the argument vector and exec a subprocess.
 * This code assumes the command and dargs strings have
 * previously been processed to insert \0 characters
 * between each argument string (see crackArgv below).
 */
static void
doexec(const char* cmd, const fxStr& dargs, const char* devid, const char* files, int nfiles)
{
#define	MAXARGS	128
    const char* av[MAXARGS];
    int ac = 0;
    const char* cp = strrchr(cmd, '/');
    // NB: can't use ?: 'cuz of AIX compiler (XXX)
    if (cp)
	av[ac++] = cp+1;			// program name
    else
	av[ac++] = cmd;
    cp = strchr(cmd,'\0');
    const char* ep = strchr(cmd, '\0');
    while (cp < ep && ac < MAXARGS-4) {		// additional pre-split args
	av[ac++] = ++cp;
	cp = strchr(cp,'\0');
    }
    cp = dargs;
    ep = cp + dargs.length();
    while (cp < ep && ac < MAXARGS-4) {		// pre-split dargs
	av[ac++] = cp;
	cp = strchr(cp,'\0')+1;
    }
    av[ac++] = "-m"; av[ac++] = devid;

    if (! (MAXARGS > ac + nfiles))
    {
	sleep(1);
    	logError("%d files requires %d arguments, max %d", nfiles, ac+nfiles+1, MAXARGS);
	return;
    }
    while (files)
    {
	av[ac++] = files;
	files = strchr(files, ' ');
	/*
	 * We can be naster with memory here - we're exec()ing right way
	 */
	if (files)
	    *(char*)files++ = '\0';
    }

    av[ac] = NULL;
    Sys::execv(cmd, (char* const*) av);
}
#undef MAXARGS

static void
join(fxStr& s, const fxStr& a)
{
    const char* cp = a;
    const char* ep = cp + a.length();
    while (cp < ep) {
	s.append(' ');
	s.append(cp);
	cp = strchr(cp,'\0')+1;
    }
}

static fxStr
joinargs(const fxStr& cmd, const fxStr& dargs)
{
    fxStr s;
    join(s, cmd);
    join(s, dargs);
    return s;
}

void
faxQueueApp::sendStart(Batch& batch)
{
    batch.start = Sys::now();

    int nfiles = 0;
    fxStr files;
    for (JobIter iter(batch.jobs); iter.notDone(); iter++)
    {
	Job& job(iter);
	traceJob(job, "SEND IN BATCH to %s", (const char*)batch.dest);
	job.pid = 0;
	job.start = 0;

	Trigger::post(Trigger::SEND_BEGIN, job);

	if (nfiles++ == 0)
	    job.start = Sys::now();
	else
	    files.append(' ');

	files.append(job.file);
    }

    /*
     * There is a possiblity no jobs survived preparation
     * successfully
     */
    if (nfiles == 0)
    {
	traceQueue(batch.di, "BATCH sent with no jobs prepared successfully");
	sendDone(batch, send_failed);
	return;
    };

    // XXX start deadman timeout on active jobs
    const fxStr& cmd = pickCmd(batch.jobtype);
    fxStr dargs(batch.firstJob().getJCI().getArgs());
    pid_t pid = fork();
    switch (pid) {
    case 0:				// child, startup command
	closeAllBut(-1);		// NB: close 'em all
	doexec(cmd, dargs, batch.modem->getDeviceID(), files, nfiles);
	sleep(10);			// XXX give parent time to catch signal
	_exit(127);
	/*NOTREACHED*/
    case -1:				// fork failed, forward to sendDone
	sendDone(batch, send_retry);
	return;
    default:				// parent, setup handler to wait
	// joinargs puts a leading space so this looks funny here
	traceQueue(batch.firstJob(), "CMD START%s -m %s %s (PID %lu)"
	    , (const char*) joinargs(cmd, dargs)
	    , (const char*) batch.modem->getDeviceID()
	    , (const char*) files
	    , pid
	);
	batch.startSend(pid);
	batch.firstJob().pid = pid;
	break;
    }
}

void
faxQueueApp::sendDone(Batch& batch, int status)
{
    traceQueue("BATCH to %s CMD DONE: exit status %#x",
	    (const char*)batch.dest, status);

    releaseModem(*batch.modem);				// done with modem

    u_int count = 0;
    for (JobIter iter(batch.jobs); iter.notDone(); iter++)
    {
	Job& job(iter);
	FaxRequest* req = readRequest(job);		// reread the qfile
	if (! req)
	{
	    logError("JOB %s: SEND FINISHED: but job file vanished",
		    (const char*) job.jobid);
	    setDead(job);
	    continue;
	}

	/*
	 * The first job is handled specially, so that we can make sure
	 * that the return status is used if it didn't set the req->status
	 * If something went wrong (like a exec problem, segfault, etc), or if
	 * it's a really old "faxsend immitator" it won't set req->status, and
	 * we want to use the exit status for this first job.
	 */
	if (count++ == 0 && req->status == send_nobatch)
	{
	    traceJob(job, "Filling in status from %#x", status);
	    if (status & 0xFF) {
		req->result = Status(343, "Send program terminated abnormally with exit status %#x", status);
		req->status = send_failed;
		logError("JOB %s: %s", (const char*)job.jobid, req->result.string());
	    } else if ((status >>= 8) == 127) {
		req->result = Status(343, "Send program terminated abnormally; unable to exec " |
		    pickCmd(req->jobtype));
		req->status = send_failed;
		logError("JOB %s: %s",
			(const char*)job.jobid, req->result.string());
	    } else
		req->status = (FaxSendStatus) status;
	};
	job.modem = NULL;

	if (req->status == send_nobatch)
	{
	    traceJob(job, "Not tried - setting directly to ready");
	    /*
	     * This job wasn't even tried - just dump it off again
	     * to be sent
	     */
	    job.remove();
	    setReadyToRun(job, *req);
	    delete req;
	} else
	{
	    sendJobDone(job, req);
	}
    }

    if (count > 1)
	traceServer("BATCH to %s done after %d jobs",
		(const char*)batch.dest, count);

    // Take it off the batchq
    batch.di.done(batch);			// remove from active destination list
    batch.remove();

    DestInfo& di(batch.di);
    fxStr dest(batch.dest);

    // We're done with the batch, we need to delete it
    Batch* b = &batch;
    delete b;

    // And close up any DI now empty
    if (di.isEmpty()) {
	/*
	 * This is the last job to the destination; purge
	 * the entry from the destination jobs database.
	 */
	traceQueue(di, "now empty, removing from destJobs");
	destJobs.remove(dest);
    }

}

void
faxQueueApp::doneJob(Job& job)
{
    job.modem = NULL;
    job.pid = 0;

    FaxRequest* req = readRequest(job);		// reread the qfile
    if (! req)
    {
	logError("JOB %s: SEND FINISHED: but job file vanished",
		(const char*) job.jobid);
	setDead(job);
	return;
    }

    /*
     * If the sub process really didn't handle batching correctly, it
     * will have left req->status be left alone.
     * In this case, we'll leave it for the complete batch to finish and
     * let the normal sendDone batch processing worry about it
     */
    if (req->status == send_nobatch)
	return;

    sendJobDone(job, req);
}


void
faxQueueApp::sendJobDone(Job& job, FaxRequest* req)
{
    time_t now = Sys::now();
    time_t duration = now - job.start;

    Trigger::post(Trigger::SEND_END, job);
    job.commid = req->commid;			// passed from subprocess
    if (req->status == send_reformat) {
	/*
	 * Job requires reformatting to deal with the discovery
	 * of unexpected remote capabilities (either because
	 * the capabilities changed or because the remote side
	 * has never been called before and the default setup
	 * created a capabilities mismatch).  Purge the job of
	 * any formatted information and reset the state so that
	 * when the job is retried it will be reformatted according
	 * to the updated remote capabilities.
	 */
	Trigger::post(Trigger::SEND_REFORMAT, job);
	u_int i = 0;
	while (i < req->items.length()) {
	    FaxItem& fitem = req->items[i];
	    if (fitem.op == FaxRequest::send_fax) {
		unrefDoc(fitem.item);
		req->items.remove(i);
		continue;
	    }
	    if (fitem.isSavedOp())
		fitem.op--;			// assumes order of enum
	    i++;
	}
	req->pagehandling = "";			// force recalculation
	req->status = send_retry;		// ... force retry
    }
    /*
     * If the job did not finish and it is due to be
     * suspended (possibly followed by termination),
     * then treat it as if it is to be retried in case
     * it does get rescheduled.
     */
    if (req->status != send_done && job.suspendPending) {
	req->result = Status(344, "Job interrupted by user");
	req->status = send_retry;
    }
    if (job.killtime == 0 && !job.suspendPending && req->status == send_retry) {
	/*
	 * The job timed out during the send attempt.  We
	 * couldn't do anything then, but now the job can
	 * be cleaned up.  Not sure if the user should be
	 * notified of the requeue as well as the timeout?
	 */
	traceQueue(job, "SEND DONE: %s, Kill time expired", (const char*)strTime(duration));
	req->result = Status(325, "Kill time expired");
	updateRequest(*req, job);
	job.state = FaxRequest::state_failed;
	deleteRequest(job, req, Job::timedout, true);
	Trigger::post(Trigger::SEND_DONE, job);
	setDead(job);
    } else if (req->status == send_retry) {
	/*
	 * If a continuation cover page is required for
	 * the retransmission, fixup the job state so
	 * that it'll get one when it's next processed.
	 */
	if (req->cover != "") {
	    /*
	     * Job was previously setup to get a continuation
	     * cover page.  If the generated cover page was not
	     * sent, then delete it so that it'll get recreated.
	     */
	    if (req->items[0].item == req->cover) {
		Sys::unlink(req->cover);
		req->items.remove(0);
	    }
	} else if (req->useccover &&
	  req->npages > 0 && contCoverPageTemplate != "") {
	    /*
	     * At least one page was sent so any existing
	     * cover page is certain to be gone.  Setup
	     * to generate a cover page when the job is
	     * retried.  Note that we assume the continuation
	     * cover page will be PostScript (though the
	     * type is not used anywhere just now).
	     */
	    req->cover = docDir | "/cover" | req->jobid | ".ps";
	}
	if (req->tts < now) {
	    /*
	     * Send failed and send app didn't specify a new
	     * tts, bump the ``time to send'' by the requeue
	     * interval, then rewrite the queue file.  This causes
	     * the job to be rescheduled for transmission
	     * at a future time.
	     */
	    req->tts = now + (req->retrytime != 0
		? req->retrytime
		: (requeueInterval>>1) + (random()%requeueInterval));
	}
	/*
	 * Bump the job priority if is not bulk-style in which case
	 * we dip the job job priority.  This is intended to prevent
	 * non-bulk faxes from becoming buried by new jobs which
	 * could prevent a timely retry.  However, it is also intended
	 * to allow all bulk faxes to be attempted before retrying
	 * any that could not complete on the first attempt.  This 
	 * aids in timely delivery of bulk faxes as a group rather than
	 * preoccupation with individual jobs as is the case with 
	 * non-bulk style jobs.  We bound the priority to keep it
	 * within a fixed "range" around it's starting priority.  This
	 * is intended to keep "normal" and "high" priority jobs
	 * from conflicting.
	 */
#define JOB_PRI_BUCKET(pri)	((pri) >> 4)

	if (job.pri != 255 && job.pri > 190)
	    job.pri++;
	else if (JOB_PRI_BUCKET(job.pri-1) == JOB_PRI_BUCKET(req->usrpri))
	    job.pri--; 
	job.state = (req->tts > now) ?
	    FaxRequest::state_sleeping : FaxRequest::state_ready;
	updateRequest(*req, job);		// update on-disk status
	if (!job.suspendPending) {
	    job.remove();			// remove from active list
	    if (req->tts > now) {
		traceQueue(job, "SEND INCOMPLETE: requeue for %s; %s",
		    (const char*)strTime(req->tts - now), req->result.string());
		setSleep(job, req->tts);
		Trigger::post(Trigger::SEND_REQUEUE, job);
		notifySender(job, Job::requeued);
	    } else {
		traceQueue(job, "SEND INCOMPLETE: retry immediately; %s",
		    req->result.string());
		setReadyToRun(job, *req);		// NB: job.tts will be <= now
	    }
	} else					// signal waiting co-thread
	    job.suspendPending = false;
	delete req;				// implicit unlock of q file
    } else {
	// NB: always notify client if job failed
	if (req->status == send_failed) {
	    job.state = FaxRequest::state_failed;
	    deleteRequest(job, req, Job::failed, true, fmtTime(duration));
	} else {
	    job.state = FaxRequest::state_done;
	    deleteRequest(job, req, Job::done, false, fmtTime(duration));
	}
	traceQueue(job, "SEND DONE: %s", (const char*)strTime(duration));
	Trigger::post(Trigger::SEND_DONE, job);
	setDead(job);
    }
}

/*
 * Job Queue Management Routines.
 */

/*
 * Begin the process to insert a job in the queue
 * of ready-to-run jobs.  We run JobControl, and when it's done, it's
 * plased on the ready-to-run queue.
 * JobControl is done running
 */
void
faxQueueApp::setReadyToRun(Job& job, FaxRequest& req)
{
    if (job.state == FaxRequest::state_blocked) {
	/*
	 * If the job was "blocked", then jobcontrol previously ran
	 * just prior to it becoming blocked.  Don't run it again right
	 * now
	 */
	setReady(job, req);
	return;
    }
    if (jobCtrlCmd.length()) {
	const char *app[3];
	app[0] = jobCtrlCmd;
	app[1] = job.jobid;
	app[2] = NULL;
	traceJob(job, "CONTROL");
	job.state = FaxRequest::state_active;
	job.insert(*jcontrolq.next);
	int pfd[2];
	if (pipe(pfd) >= 0) {
	    pid_t pid = fork();
	    switch (pid) {
	    case -1:			// error - continue with no JCI
		jobError(job, "JOB CONTROL: fork: %m");
		Sys::close(pfd[1]);
                // When fork fails we need to set it ready, because there
                // will be no child signal to start it.
                setReady(job, req);
		break;
	    case 0:				// child, exec command
		/*
		 * We set up our file handles carefully here because
		 * we want to have STDIN/STDOUT/STDERR all appearing
		 * normally.  We rely on knowing that faxq has a "good" STDIN
		 * STDERR at all times.  We just need to set out STDOUT
		 * to the pipe, and close everything else.
		 */
		if (pfd[1] != STDOUT_FILENO)
		    dup2(pfd[1], STDOUT_FILENO);

		for (int fd = Sys::getOpenMax()-1; fd >= 0; fd--)
		    if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO)
			(void) Sys::close(fd);

		traceQueue(job, "JOB CONTROL: %s %s", app[0], app[1]);
		Sys::execv(app[0], (char * const*)app);
		sleep(3);			// XXX give parent time to catch signal
		traceQueue(job, "JOB CONTROL: failed to exec: %m");
		_exit(255);
		/*NOTREACHED*/
	    default:			// parent, read from pipe and wait
		job.startControl(pid, pfd[0]);	// First, get our child PID handled
		Sys::close(pfd[1]);
	    }
	} else
	{
	    jobError(job, "JOB CONTROL: pipe failed: %m");
	    // If our pipe fails, we can't run the child, but we still
	    // Need setReady to be called to proceed this job
	    setReady(job, req);
	}
    } else
	setReady(job, req);
}

/*
 * Insert the job into the runq.  We have finished
 * all the JobControl execution
 */
void
faxQueueApp::ctrlJobDone(Job& job, int status)
{
    if (jobCtrlCmd.length() )
	traceQueue(job, "CMD DONE: exit status %#x", status);
    if (status) {
	logError("JOB %s: bad exit status %#x from sub-fork",
	    (const char*) job.jobid, status);
    }
    if (job.suspendPending) {		// co-thread waiting
	job.suspendPending = false;
	return;
    }
    job.remove();
    FaxRequest* req = readRequest(job);
    setReady(job, *req);
    if (req) {
    	updateRequest(*req, job);
	delete req;
    }
}



/*
 * set a job as really ready
 */
void
faxQueueApp::setReady(Job& job, FaxRequest& req)
{
    job.state = FaxRequest::state_ready;
    traceJob(job, "READY");
    Trigger::post(Trigger::JOB_READY, job);

    /*
     * First we insert the job on the per-destination readyQ
     */
    DestInfo& di = destJobs[job.dest];

    bool skipped = false;
    JobIter iter(di.readyQ);
    for (; iter.notDone(); iter++)
    {
	if (! iter.job().higherPriority(job))
	    break;
	skipped = true;
    }
    job.insert(iter.job());

    /*
     * If we skipped some jobs, then we *know* that we haven't
     * affected the highest priority job for this DI.  Otherwise,
     * we are either the 1st job to this dest, or the highest priority,
     * so we must "priority insert" the DI now.
     */

    if (! skipped)
    {
	if (di.isOnList())
	    di.remove();
	QLink* ql = runq.next;
	while (ql != &runq)
	{
	    DestInfo* dip = (DestInfo*)ql;
	    if (! dip->readyQ.isEmpty() )
	    {
		Job& nj(*(Job*)(dip->readyQ.next));
		if (! nj.higherPriority(job) )
		    break;
	    }
	    ql = ql->next;
	}
	di.insert(*ql);
	traceQueue(di, "SORT INSERT DONE");
    } else
    {
	/*
	 * Since we skipped we know the job is blocked for now...
	 */
	blockJob(job, req, Status(337, "Blocked by concurrent calls"));
    }
    /*
     * In order to deliberately batch jobs by using a common
     * time-to-send we need to give time for the other jobs'
     * timers to expire and to enter the run queue before
     * running the scheduler.  Thus the scheduler is poked
     * with a delay.
     */
    pokeScheduler(1);
}

/*
 * Place a job on the queue of jobs pending future tts
 * and start the associated timer.
 *
 * Pending jobs are kept separate from "sleeping" jobs to
 * make sure we can account for sleeping jobs (sleeping because they have
 * already been active) separately from jobs sleeping because their original
 * TTS has not arrived yet.
 */

void
faxQueueApp::setPending(Job& job)
{
    traceJob(job, "PENDING FOR %s", (const char*)strTime(job.tts - Sys::now()));
    Trigger::post(Trigger::JOB_SLEEP, job);
    JobIter iter(pendq);
    for (; iter.notDone() && iter.job().tts <= job.tts; iter++)
	;
    job.insert(iter.job());
    job.startTTSTimer(job.tts);
}

/*
 * Place a job on the queue of jobs waiting to run
 * and start the associated timer.
 */
void
faxQueueApp::setSleep(Job& job, time_t tts)
{
    traceJob(job, "SLEEP FOR %s", (const char*)strTime(tts - Sys::now()));
    Trigger::post(Trigger::JOB_SLEEP, job);
    DestInfo& di = destJobs[job.dest];
    JobIter iter(di.sleepQ);
    for (; iter.notDone() && iter.job().tts <= tts; iter++)
	;
    job.insert(iter.job());
    job.startTTSTimer(tts);
}

#define	isOKToCall(di, jci, n) \
    ((di).getActiveCount()+(di).getSleepCount()+n <= jci.getMaxConcurrentCalls())

/*
 * Process a job that's finished.  The corpse gets placed
 * on the deadq and is reaped the next time the scheduler
 * runs.  If any jobs are blocked waiting for this job to
 * complete, one is made ready to run.
 */
void
faxQueueApp::setDead(Job& job)
{
    if (job.state != FaxRequest::state_done 
      && job.state != FaxRequest::state_failed)
	job.state = FaxRequest::state_failed;
    job.suspendPending = false;
    traceJob(job, "DEAD");
    Trigger::post(Trigger::JOB_DEAD, job);
    DestInfo& di = destJobs[job.dest];
    di.updateConfig();			// update file if something changed
    if ( di.isEmpty()) {
	/*
	 * This is the last job to the destination; purge
	 * the entry from the destination jobs database.
	 */
	traceQueue(di, "Now empty - removing from destJobs");
	destJobs.remove(job.dest);
    }

    if (job.isOnList())			// lazy remove from active list
	job.remove();
    job.insert(*deadq.next);		// setup job corpus for reaping
    fxAssert(job.modem == NULL, "Dead job with modem");
    pokeScheduler();
}

/*
 * Place a job on the list of jobs actively being processed.
 */
void
faxQueueApp::setActive(Batch& batch, Job& job)
{
    job.state = FaxRequest::state_active;
    traceJob(job, "ACTIVE");
    Trigger::post(Trigger::JOB_ACTIVE, job);
    job.insert(batch.jobs);
}

/*
 * Place a job on the list of jobs not being scheduled.
 */
void
faxQueueApp::setSuspend(Job& job)
{
    job.state = FaxRequest::state_suspended;
    traceJob(job, "SUSPEND");
    Trigger::post(Trigger::JOB_SUSPEND, job);
    job.insert(*suspendq.next);
}

/*
 * Create a new job entry and place them on the
 * appropriate queue.  A kill timer is also setup
 * for the job.
 */
bool
faxQueueApp::submitJob(FaxRequest& req, bool checkState)
{
    Job* job = new Job(req);
    traceJob(*job, "CREATE");
    Trigger::post(Trigger::JOB_CREATE, *job);
    return (submitJob(*job, req, checkState));
}

bool
faxQueueApp::submitJob(Job& job, FaxRequest& req, bool checkState)
{
    /*
     * Check various submission parameters.  We setup the
     * canonical version of the destination phone number
     * first so that any rejections that cause the notification
     * script to be run will return a proper value for the
     * destination phone number.
     */
    job.dest = canonicalizePhoneNumber(req.number);
    if (job.dest == "") {
	if (req.external == "")			// NB: for notification logic
	    req.external = req.number;
	rejectSubmission(job, req,
	    Status(327, "REJECT: Unable to convert dial string to canonical format"));
	return (false);
    }
    time_t now = Sys::now();
    if (req.killtime <= now) {
	/*
	 * timeoutJob expects the job to be on the a QLink queue
	 * submitJob is a special case, where often we're a new job (not
	 * on any queue), but sometimes we're on the suspendq.
	 */
	QLink tmp;
	if (! job.isOnList())
	    job.insert(tmp);
	timeoutJob(job, req);
	return (false);
    }
    if (!Modem::modemExists(req.modem) && !ModemGroup::find(req.modem)) {
	rejectSubmission(job, req,
	    Status(328, "REJECT: Requested modem %s is not registered", (const char*)req.modem));
	return (false);
    }
    if (req.items.length() == 0) {
	rejectSubmission(job, req, Status(329, "REJECT: No work found in job file"));
	return (false);
    }
    if (req.pagewidth > 303) {
	rejectSubmission(job, req,
	    Status(330, "REJECT: Page width (%u) appears invalid", req.pagewidth));
	return (false);
    }
    /*
     * Verify the killtime is ``reasonable''; otherwise
     * select (through the Dispatcher) may be given a
     * crazy time value, potentially causing problems.
     */
    if (req.killtime-now > 365*24*60*60) {	// XXX should be based on tts
	rejectSubmission(job, req,
	    Status(331, "REJECT: Job expiration time (%u) appears invalid",
		req.killtime));
	return (false);
    }
    if (checkState) {
	/*
	 * Check the state from queue file and if
	 * it indicates the job was not being
	 * scheduled before then don't schedule it
	 * now.  This is used when the scheduler
	 * is restarted and reading the queue for
	 * the first time.
	 *
	 * NB: We reschedule blocked jobs in case
	 *     the job that was previously blocking
	 *     it was removed somehow.
	 */
	switch (req.state) {
	case FaxRequest::state_suspended:
	    setSuspend(job);
	    return (true);
	case FaxRequest::state_done:
	case FaxRequest::state_failed:
	    setDead(job);
	    return (true);
	}
    }
    /*
     * Put the job on the appropriate queue
     * and start the job kill timer.
     */
    if (req.tts > now) {			// scheduled for future
	/*
	 * Check time-to-send as for killtime above.
	 */
	if (req.tts - now > 365*24*60*60) {
	    rejectSubmission(job, req, Status(332,
		    fxStr::format("REJECT: Time-to-send (%u) appears invalid", req.tts)));
	    return (false);
	}
	job.startKillTimer(req.killtime);
	job.state = FaxRequest::state_pending;
	setPending(job);
    } else {					// ready to go now
	job.startKillTimer(req.killtime);
	setReadyToRun(job, req);
    }
    updateRequest(req, job);
    return (true);
}

/*
 * Reject a job submission.
 */
void
faxQueueApp::rejectSubmission(Job& job, FaxRequest& req, const Status& r)
{
    Trigger::post(Trigger::JOB_REJECT, job);
    req.status = send_failed;
    req.result = r;
    traceServer("JOB %s: ", (const char*)job.jobid, r.string());
    deleteRequest(job, req, Job::rejected, true);
    setDead(job);				// dispose of job
}

/*
 * Suspend a job by removing it from whatever
 * queue it's currently on and/or stopping any
 * timers.  If the job has an active subprocess
 * then the process is optionally sent a signal
 * and we wait for the process to stop before
 * returning to the caller.
 */
bool
faxQueueApp::suspendJob(Job& job, bool abortActive)
{
    if (job.suspendPending)			// already being suspended
	return (false);
    switch (job.state) {
    case FaxRequest::state_active:
	/*
	 * We can't suspend the job if it's active, but not the current
	 * one.  If we did, users would be able to kill other users jobs.
	 * Unfortunately, faxq doesn't have a way to communicate this to
	 * hfaxd - we just have to hope they can see the logs.
	 */
	if (job.pid == 0)
	{
	    traceJob(job, "Cannot kill job that is batched but not active");
	    return false;
	}
	/*
	 * Job is being handled by a subprocess; optionally
	 * signal the process and wait for it to terminate
	 * before returning.  We disable the kill timer so
	 * that if it goes off while we wait for the process
	 * to terminate the process completion work will not
	 * mistakenly terminate the job (see sendJobDone).
	 */
	job.suspendPending = true;		// mark thread waiting
	if (abortActive)
	    (void) kill(job.pid, SIGTERM);	// signal subprocess
	job.stopKillTimer();
	while (job.suspendPending)		// wait for subprocess to exit
	    Dispatcher::instance().dispatch();
	/*
	 * Recheck the job state; it may have changed while
	 * we were waiting for the subprocess to terminate.
	 */
	if (job.state != FaxRequest::state_done &&
	  job.state != FaxRequest::state_failed)
	    break;
	/* fall thru... */
    case FaxRequest::state_done:
    case FaxRequest::state_failed:
	return (false);
    case FaxRequest::state_sleeping:
    case FaxRequest::state_pending:
	job.stopTTSTimer();			// cancel timeout
	/* fall thru... */
    case FaxRequest::state_suspended:
    case FaxRequest::state_ready:
    case FaxRequest::state_blocked:
	break;
    }
    job.remove();				// remove from old queue
    job.stopKillTimer();			// clear kill timer
    return (true);
}

/*
 * Suspend a job and place it on the suspend queue.
 * If the job is currently active then we wait for
 * it to reach a state where it can be safely suspended.
 * This control is used by clients that want to modify
 * the state of a job (i.e. suspend, modify, submit).
 */
bool
faxQueueApp::suspendJob(const fxStr& jobid, bool abortActive)
{
    Job* job = Job::getJobByID(jobid);
    if (job && suspendJob(*job, abortActive)) {
	setSuspend(*job);
	FaxRequest* req = readRequest(*job);
	if (req) {
	    updateRequest(*req, *job);
	    delete req;
	}
	return (true);
    } else
	return (false);
}

/*
 * Terminate a job in response to a command message.
 * If the job is currently running the subprocess is
 * sent a signal to request that it abort whatever
 * it's doing and we wait for the process to terminate.
 * Otherwise, the job is immediately removed from
 * the appropriate queue and any associated resources
 * are purged.
 */
bool
faxQueueApp::terminateJob(const fxStr& jobid, JobStatus why)
{
    Job* job = Job::getJobByID(jobid);
    if (job && suspendJob(*job, true)) {
	job->state = FaxRequest::state_failed;
	Trigger::post(Trigger::JOB_KILL, *job);
	FaxRequest* req = readRequest(*job);
	if (req) {
	    req->result = Status(345, "Job aborted by request");
	    deleteRequest(*job, req, why, why != Job::removed);
	}
	setDead(*job);
	return (true);
    } else
	return (false);
}

/*
 * Reject a job at some time before it's handed off to the server thread.
 */
void
faxQueueApp::rejectJob(Job& job, FaxRequest& req, const Status& r)
{
    req.status = send_failed;
    req.result = r;
    traceServer("JOB %s: %s",
	    (const char*)job.jobid, r.string());
    job.state = FaxRequest::state_failed;
    Trigger::post(Trigger::JOB_REJECT, job);
    setDead(job);				// dispose of job
}

/*
 * Deal with a job that's blocked by a concurrent call.
 */
void
faxQueueApp::blockJob(Job& job, FaxRequest& req, const Status& r)
{
    int old_state = job.state;
    job.state = FaxRequest::state_blocked;
    req.result = r;
    updateRequest(req, job);
    traceQueue(job, "%s", r.string());
    if (old_state != FaxRequest::state_blocked)
	notifySender(job, Job::blocked);
    Trigger::post(Trigger::JOB_BLOCKED, job);
}

/*
 * Requeue a job that's delayed for some reason.
 */
void
faxQueueApp::delayJob(Job& job, FaxRequest& req, const Status& r, time_t tts)
{
    job.state = FaxRequest::state_sleeping;
    req.tts = tts;
    time_t delay = tts - Sys::now();
    // adjust kill time so job isn't removed before it runs
    job.stopKillTimer();
    req.killtime += delay;
    job.startKillTimer(req.killtime);
    req.result = r;
    updateRequest(req, job);
    traceQueue(job, "%s: requeue for %s",
	    r.string(), (const char*)strTime(delay));
    setSleep(job, tts);
    notifySender(job, Job::requeued);
    Trigger::post(Trigger::JOB_DELAYED, job);
}

/*
 * Process the job who's kill time expires.  The job is
 * terminated unless it is currently being tried, in which
 * case it's marked for termination after the attempt is
 * completed.
 */
void
faxQueueApp::timeoutJob(Job& job)
{
    traceQueue(job, "KILL TIME EXPIRED");
    Trigger::post(Trigger::JOB_TIMEDOUT, job);
    if (job.state != FaxRequest::state_active) {
	job.remove();				// remove from sleep queue
	job.state = FaxRequest::state_failed;
	FaxRequest* req = readRequest(job);
	if (req) {
	    req->result = Status(325, "Kill time expired");
	    deleteRequest(job, req, Job::timedout, true);
	}
	setDead(job);
    } else
	job.killtime = 0;			// mark job to be removed
}

/*
 * Like above, but called for a job that times
 * out at the point at which it is submitted (e.g.
 * after the server is restarted).  The work here
 * is subtley different; the q file must not be
 * re-read because it may result in recursive flock
 * calls which on some systems may cause deadlock
 * (systems that emulate flock with lockf do not
 * properly emulate flock).
 */
void
faxQueueApp::timeoutJob(Job& job, FaxRequest& req)
{
    job.state = FaxRequest::state_failed;
    traceQueue(job, "KILL TIME EXPIRED");
    Trigger::post(Trigger::JOB_TIMEDOUT, job);
    req.result = Status(325, "Kill time expired");
    deleteRequest(job, req, Job::timedout, true);
    setDead(job);
}

/*
 * Resubmit an existing job or create a new job
 * using the specified job description file.
 */
bool
faxQueueApp::submitJob(const fxStr& jobid, bool checkState)
{
    Job* job = Job::getJobByID(jobid);
    if (job) {
	bool ok = false;
	if (job->state == FaxRequest::state_suspended) {
	    job->remove();			// remove from suspend queue
	    FaxRequest* req = readRequest(*job);// XXX need better mechanism
	    if (req) {
		job->update(*req);		// update job state from file
		ok = submitJob(*job, *req);	// resubmit to scheduler
		delete req;			// NB: unlock qfile
	    } else
		setDead(*job);			// XXX???
	} else if (job->state == FaxRequest::state_done ||
	  job->state == FaxRequest::state_failed)
	    jobError(*job, "Cannot resubmit a completed job");
	else
	    ok = true;				// other, nothing to do
	return (ok);
    }
    /*
     * Create a job from a queue file and add it
     * to the scheduling queues.
     */
    fxStr filename(FAX_SENDDIR "/" FAX_QFILEPREF | jobid);
    if (!Sys::isRegularFile(filename)) {
	logError("JOB %s: qfile %s is not a regular file.",
	    (const char*) jobid, (const char*) filename);
	return (false);
    }
    bool status = false;
    int fd = Sys::open(filename, O_RDWR);
    if (fd >= 0) {
	if (flock(fd, LOCK_SH) >= 0) {
	    FaxRequest req(filename, fd);
	    /*
	     * There are four possibilities:
	     *
	     * 1. The queue file was read properly and the job
	     *    can be submitted.
	     * 2. There were problems reading the file, but
	     *    enough information was obtained to purge the
	     *    job from the queue.
	     * 3. The job was previously submitted and completed
	     *    (either with success or failure).
	     * 4. Insufficient information was obtained to purge
	     *    the job; just skip it.
	     */
	    bool reject;
	    if (req.readQFile(reject) && !reject &&
	      req.state != FaxRequest::state_done &&
	      req.state != FaxRequest::state_failed) {
		status = submitJob(req, checkState);
	    } else if (reject) {
		Job job(req);
		job.state = FaxRequest::state_failed;
		req.status = send_failed;
		req.result = Status(326, "Invalid or corrupted job description file");
		traceServer("JOB %s : %s", (const char*)jobid, req.result.string());
		// NB: this may not work, but we try...
		deleteRequest(job, req, Job::rejected, true);
	    } else if (req.state == FaxRequest::state_done ||
	      req.state == FaxRequest::state_failed) {
		logError("JOB %s: Cannot resubmit a completed job",
		    (const char*) jobid);
	    } else
		traceServer("%s: Unable to purge job, ignoring it",
			(const char*)filename);
	} else
	    logError("JOB %s: Could not lock job file; %m.",
		(const char*) jobid);
	Sys::close(fd);
    } else
	logError("JOB %s: Could not open job file; %m.", (const char*) jobid);
    return (status);
}

/*
 * Process the expiration of a job's time-to-send timer.
 * The job is moved to the ready-to-run queues and the
 * scheduler is poked.
 */
void
faxQueueApp::runJob(Job& job)
{
    job.remove();
    FaxRequest* req = readRequest(job);
    if (! req) {
	logError("JOB %s: qfile vanished while moving to runq",
		(const char*)job.jobid);
	setDead(job);
	return;
    }
    setReadyToRun(job, *req);
    updateRequest(*req, job);
    delete req;
}

/*
 * Check if a job is runnable.  If it is, it will return true,
 * and the job/req are left for the caller to deal with.  If the
 * job is not runnable, it returns false, and the job and req will
 * have been dealt with appropriately (put on another list, deleted,etc
 */
bool
faxQueueApp::isJobSendOK (Job& job, FaxRequest* req)
{
    /*
     * Constrain the maximum number of times the phone
     * will be dialed and/or the number of attempts that
     * will be made (and reject jobs accordingly).
     */
    u_short maxdials = fxmin((u_short) job.getJCI().getMaxDials(),req->maxdials);
    if (req->totdials >= maxdials) {
	rejectJob(job, *req, Status(333,
		fxStr::format("REJECT: Too many attempts to dial: %u, max %u",
	    req->totdials, maxdials)));
	deleteRequest(job, req, Job::rejected, true);
	return false;
    }
    u_short maxtries = fxmin((u_short) job.getJCI().getMaxTries(),req->maxtries);
    if (req->tottries >= maxtries) {
	rejectJob(job, *req, Status(334, "REJECT: Too many attempts to transmit: %u, max %u",
	    req->tottries, maxtries));
	deleteRequest(job, req, Job::rejected, true);
	return false;
    }
    // NB: repeat this check so changes in max pages are applied
    u_int maxpages = job.getJCI().getMaxSendPages();
    if (req->totpages > maxpages) {
	rejectJob(job, *req, Status(335, "REJECT: Too many pages in submission: %u, max %u",
	    req->totpages, maxpages));
	deleteRequest(job, req, Job::rejected, true);
	return false;
    }
    if (job.getJCI().getRejectNotice() != "") {
	/*
	 * Calls to this destination are being rejected for
	 * a specified reason that we return to the sender.
	 */
	rejectJob(job, *req, Status(348, "REJECT: %s", (const char*) job.getJCI().getRejectNotice()));
	deleteRequest(job, req, Job::rejected, true);
	return false;
    }
    time_t now = Sys::now();
    time_t tts;
    if ((tts = job.getJCI().nextTimeToSend(now)) != now) {
	/*
	 * This job may not be started now because of time-of-day
	 * restrictions.  Reschedule it for the next possible time.
	 */
	job.remove();			// remove from run queue
	delayJob(job, *req, Status(338, "Delayed by time-of-day restrictions"), tts);
	delete req;
	return false;
    }

    return true;
}

/*
 * Scan the list of jobs and process those that are ready
 * to go.  Note that the scheduler should only ever be
 * invoked from the dispatcher via a timeout.  This way we
 * can be certain there are no active contexts holding
 * references to job corpses (or other data structures) that
 * we want to reap.  To invoke the scheduler the pokeScheduler
 * method should be called to setup an immediate timeout that
 * will cause the scheduler to be invoked from the dispatcher.
 */
void
faxQueueApp::runScheduler()
{
    fxAssert(inSchedule == false, "Scheduler running twice");
    inSchedule = true;

    /*
     * Reread the configuration file if it has been
     * changed.  We do this before each scheduler run
     * since we are a long-running process and it should
     * not be necessary to restart the process to have
     * config file changes take effect.
     */
    (void) updateConfig(configFile);
    /*
     * Scan the job queue and locate a compatible modem to
     * use in processing the job.  Doing things in this order
     * insures the highest priority job is always processed
     * first.
     */
    if (! quit) 
    {
	for (QLink* ql = runq.next; ql != &runq; ql = ql->next)
	{
	    /*
	     * If there are no modems ready, then no job will be
	     * able to start
	     */
	    if (! Modem::anyReady() )
	    {
		traceQueue("No devices available");
		break;
	    }

	    DestInfo* dp = (DestInfo*)ql;
	    traceQueue(*dp, "Picking next job");
	    if (dp->readyQ.isEmpty() )
	    {
		/*
		 * Don't keep this di on the runq, otherwise we just uselessly
		 * iterate it.  We need to be careful here so that
		 * the ql = ql->next list is kept working when we
		 * remove dip from the QLink list.
		 */
		traceQueue(*dp, "Empty - removing");
		QLink* tmp = ql->next;
		dp->remove();
		dp->insert(destq);
		ql = tmp->prev;
		continue;
	    }

	    Job& job = *(Job*)dp->readyQ.next;

	    fxAssert(job.tts <= Sys::now(), "Sleeping job on run queue");
	    fxAssert(job.modem == NULL, "Job on run queue holding modem");


	    /*
	     * Read the on-disk job state and process the job.
	     * Doing all the processing below each time the job
	     * is considered for processing could be avoided by
	     * doing it only after assigning a modem but that
	     * would potentially cause the order of dispatch
	     * to be significantly different from the order
	     * of submission; something some folks care about.
	     */
	    traceJob(job, "PROCESS");
	    Trigger::post(Trigger::JOB_PROCESS, job);
	    FaxRequest* req = readRequest(job);
	    if (!req) {			// problem reading job state on-disk
		setDead(job);
		continue;
	    }

	    /*
	     * Do job limits and checking
	     */
	    if (! isJobSendOK(job, req) )
		continue;

	    /*
	     * Do per-destination processing and checking.
	     */

	    if (!isOKToCall(*dp, job.getJCI(), 1))
	    {
		/*
		 * This job would exceed the max number of concurrent
		 * calls that may be made to this destination.  Mark it
		 * as ``blocked'' for the destination; the job will
		 * be stay ready to run and go when one of the existing
		 * jobs terminates.
		 */
		blockJob(job, *req, Status(337, "Blocked by concurrent calls"));
		delete req;
	    } else if (assignModem(job))
	    {
		job.remove();			// remove from run queue
		/*
		 * We have a modem and have assigned it to the
		 * job.  The job is not on any list; processJob
		 * is responsible for requeing the job according
		 * to the outcome of the work it does (which may
		 * take place asynchronously in a sub-process).
		 * Likewise the release of the assigned modem is
		 * also assumed to take place asynchronously in
		 * the context of the job's processing.
		 */
		startBatch(job.modem, job, req, *dp);
	    } else				// leave job on run queue
		delete req;
	}
    }
    /*
     * Reap dead jobs.
     */
    for (JobIter iter(deadq); iter.notDone(); iter++) {
	Job* job = iter;
	job->remove();
	traceJob(*job, "DELETE");
	Trigger::post(Trigger::JOB_REAP, *job);
	delete job;
    }
    /*
     * Reclaim resources associated with clients
     * that terminated without telling us.
     */
    HylaClient::purge();		// XXX maybe do this less often
    inSchedule = false;

    /*
     * Terminate the server if there are no jobs currently
     * being processed.  We must be sure to wait for jobs
     * so that we can capture exit status from subprocesses
     * and so that any locks held on behalf of outbound jobs
     * do not appear to be stale (since they are held by this
     * process).
     *
     * To ensure we remain valgrind-squeaky clean, we have
     * to go through and remove jobs from the DestInfo queue,
     * and make sure that we've processed the deadq, so this
     * goes at the end of the runSchedule() loop.
     */
    if (quit && batchq.next == &batchq) {
	for (DestInfoDictIter diter(destJobs); diter.notDone(); diter++)
	{
	    DestInfo& di(diter.value());
	    traceQueue(di, "Cleaning to be squeaky");
	    for (JobIter iter(di.readyQ); iter.notDone(); iter++)
	    {
		Job* jb = iter;
		traceJob(*jb, "Removing to be squeaky clean");
		jb->remove();
		delete jb;
	    }
	    for (JobIter iter(di.sleepQ); iter.notDone(); iter++)
	    {
		Job* jb = iter;
		traceJob(*jb, "Removing to be squeaky clean");
		jb->remove();
		delete jb;
	    }
	    di.remove();
	}
	showDebugState();
	close();
	return;
    }
}

bool
faxQueueApp::scheduling (void)
{
    return inSchedule;
}

/*
 * Attempt to assign a modem to a job.  If we are
 * unsuccessful and it was due to the modem being
 * locked for use by another program then we start
 * a thread to poll for the removal of the lock file;
 * this is necessary for send-only setups where we
 * do not get information about when modems are in
 * use from faxgetty processes.
 */
bool
faxQueueApp::assignModem(Job& job)
{
    bool retryModemLookup;
    do {
	retryModemLookup = false;
	Modem* modem = Modem::findModem(job);
	if (modem) {
	    if (modem->assign()) {
		job.modem = modem;
		Trigger::post(Trigger::MODEM_ASSIGN, *modem);
		return (true);
	    }
	    /*
	     * Modem could not be assigned to job.  The
	     * modem is assumed to be ``removed'' from
	     * the list of potential modems scanned by
	     * findModem so we arrange to re-lookup a
	     * suitable modem for this job.  (a goto would
	     * be fine here but too many C++ compilers
	     * can't handle jumping past the above code...)
	     */
	    traceJob(job, "Unable to assign modem %s (cannot lock)",
		(const char*) modem->getDeviceID());
	    modem->startLockPolling(pollLockWait);
	    traceModem(*modem, "BUSY (begin polling)");
	    retryModemLookup = true;
	} else
	    traceJob(job, "No assignable modem located");
    } while (retryModemLookup);
    return (false);
}

/*
 * Release a modem assigned to a job.  The scheduler
 * is prodded since doing this may permit something
 * else to be processed.
 */
void
faxQueueApp::releaseModem(Modem& modem)
{
    Trigger::post(Trigger::MODEM_RELEASE, modem);
    modem.release();
    pokeScheduler();
}

/*
 * Poll to see if a modem's UUCP lock file is still
 * present.  If the lock has been removed then mark
 * the modem ready for use and poke the job scheduler
 * in case jobs were waiting for an available modem.
 * This work is only done when a modem is ``discovered''
 * to be in-use by an outbound process when operating
 * in a send-only environment (i.e. one w/o a faxgetty
 * process monitoring the state of each modem).
 */
void
faxQueueApp::pollForModemLock(Modem& modem)
{
    if (modem.lock->lock()) {
	modem.release();
	traceModem(modem, "READY (end polling)");
	pokeScheduler();
    } else
	modem.startLockPolling(pollLockWait);
}

/*
 * Set a timeout so that the job scheduler runs the
 * next time the dispatcher is invoked.
 */
void
faxQueueApp::pokeScheduler(u_short s)
{
    schedTimeout.start(s);
}

/*
 * Create a request instance and read the
 * associated queue file into it.
 */
FaxRequest*
faxQueueApp::readRequest(Job& job)
{
    int fd = Sys::open(job.file, O_RDWR);
    if (fd >= 0) {
	if (flock(fd, LOCK_EX) >= 0) {
	    FaxRequest* req = new FaxRequest(job.file, fd);
	    bool reject;
	    if (req->readQFile(reject) && !reject) {
		if (req->external == "")
		    req->external = job.dest;
		return (req);
	    }
	    jobError(job, "Could not read job file");
	    delete req;
	} else
	    jobError(job, "Could not lock job file: %m");
	Sys::close(fd);
    } else {
	// file might have been removed by another server
	if (errno != ENOENT)
	    jobError(job, "Could not open job file: %m");
    }
    return (NULL);
}

/*
 * Update the request instance with information
 * from the job structure and then write the
 * associated queue file.
 */
void
faxQueueApp::updateRequest(FaxRequest& req, Job& job)
{
    req.state = job.state;
    req.pri = job.pri;
    req.writeQFile();
}

/*
 * Delete a request and associated state.
 */
void
faxQueueApp::deleteRequest(Job& job, FaxRequest* req, JobStatus why,
    bool force, const char* duration)
{
    deleteRequest(job, *req, why, force, duration);
    delete req;
}

void
faxQueueApp::deleteRequest(Job& job, FaxRequest& req, JobStatus why,
    bool force, const char* duration)
{
    fxStr dest = FAX_DONEDIR |
	req.qfile.tail(req.qfile.length() - (sizeof (FAX_SENDDIR)-1));
    /*
     * Move completed jobs to the doneq area where
     * they can be retrieved for a period of time;
     * after which they are either removed or archived.
     */
    if (Sys::rename(req.qfile, dest) >= 0) {
	u_int i = 0;
	/*
	 * Remove entries for imaged documents and
	 * delete/rename references to source documents
	 * so the imaged versions can be expunged.
	 */
	while (i < req.items.length()) {
	    FaxItem& fitem = req.items[i];
	    if (fitem.op == FaxRequest::send_fax) {
		req.renameSaved(i);
		unrefDoc(fitem.item);
		req.items.remove(i);
	    } else
		i++;
	}
	req.qfile = dest;			// moved to doneq
	job.file = req.qfile;			// ...and track change
	if (why == Job::done)
	    req.state = FaxRequest::state_done;	// job is definitely done
	else
	    req.state = FaxRequest::state_failed;// job is definitely done
	req.pri = job.pri;			// just in case someone cares
	req.tts = Sys::now();			// mark job termination time
	req.writeQFile();
	notifySender(job, why, duration);
    } else {
	/*
	 * Move failed, probably because there's no
	 * directory.  Treat the job the way we used
	 * to: purge everything.  This avoids filling
	 * the disk with stuff that'll not get removed;
	 * except for a scavenger program.
	 */
	jobError(job, "rename to %s failed: %s",
	    (const char*) dest, strerror(errno));
	req.writeQFile();
	notifySender(job, why, duration);
	u_int n = req.items.length();
	for (u_int i = 0; i < n; i++) {
	    const FaxItem& fitem = req.items[i];
	    switch (fitem.op) {
	    case FaxRequest::send_fax:
		unrefDoc(fitem.item);
		break;
	    case FaxRequest::send_tiff:
	    case FaxRequest::send_tiff_saved:
	    case FaxRequest::send_pdf:
	    case FaxRequest::send_pdf_saved:
	    case FaxRequest::send_postscript:
	    case FaxRequest::send_postscript_saved:
	    case FaxRequest::send_pcl:
	    case FaxRequest::send_pcl_saved:
		Sys::unlink(fitem.item);
		break;
	    }
	}
	req.items.remove(0, n);
	Sys::unlink(req.qfile);
    }
}

/*
 * FIFO-related support.
 */

/*
 * Open the requisite FIFO special files.
 */
void
faxQueueApp::openFIFOs()
{
    fifo = openFIFO(fifoName, 0600, true);
    Dispatcher::instance().link(fifo, Dispatcher::ReadMask, this);
}

void
faxQueueApp::closeFIFOs()
{
    Sys::close(fifo), fifo = -1;
}

int faxQueueApp::inputReady(int fd)		{ return FIFOInput(fd); }

/*
 * Process a message received through a FIFO.
 */
void
faxQueueApp::FIFOMessage(const char* cp)
{
    if (tracingLevel & FAXTRACE_FIFO)
	logInfo("FIFO RECV \"%s\"", cp);
    if (cp[0] == '\0') {
	logError("Bad fifo message \"%s\"", cp);
	return;
    }
    const char* tp = strchr(++cp, ':');
    if (tp)
	FIFOMessage(cp[-1], fxStr(cp,tp-cp), tp+1);
    else
	FIFOMessage(cp[-1], fxStr::null, cp);
}

void
faxQueueApp::FIFOMessage(char cmd, const fxStr& id, const char* args)
{
    bool status = false;
    switch (cmd) {
    case '+':				// modem status msg
	FIFOModemMessage(id, args);
	return;
    case '*':				// job status msg from subproc's
	FIFOJobMessage(id, args);
	return;
    case '@':				// receive status msg
	FIFORecvMessage(id, args);
	return;
    case 'Q':				// quit
	traceServer("QUIT");
	quit = true;
	pokeScheduler();
	return;				// NB: no return value expected
    case 'T':				// create new trigger 
	traceServer("TRIGGER %s", args);
	Trigger::create(id, args);
	return;				// NB: trigger id returned specially

    /*
     * The remaining commands generate a response if
     * the client has included a return address.
     */
    case 'C':				// configuration control
	traceServer("CONFIG %s", args);
	status = readConfigItem(args);
	break;
    case 'D':				// cancel an existing trigger
	traceServer("DELETE %s", args);
	status = Trigger::cancel(args);
	break;
    case 'R':				// remove job
	traceServer("REMOVE JOB %s", args);
	status = terminateJob(args, Job::removed);
	break;
    case 'K':				// kill job
	traceServer("KILL JOB %s", args);
	status = terminateJob(args, Job::killed);
	break;
    case 'S':				// submit an outbound job
	traceServer("SUBMIT JOB %s", args);
	if (status = submitJob(args))
	    pokeScheduler();
	break;
    case 'U':				// unreference file
	traceServer("UNREF DOC %s", args);
	unrefDoc(args);
	status = true;
	break;
    case 'X':				// suspend job
	traceServer("SUSPEND JOB %s", args);
	if (status = suspendJob(args, false))
	    pokeScheduler();
	break;
    case 'Y':				// interrupt job
	traceServer("INTERRUPT JOB %s", args);
	if (status = suspendJob(args, true))
	    pokeScheduler();
	break;
    case 'N':				// noop
	status = true;
	break;
    case 'Z':
	showDebugState();
	break;
    default:
	logError("Bad FIFO cmd '%c' from client %s", cmd, (const char*) id);
	break;
    }
    if (id != fxStr::null) {
	char msg[3];
	msg[0] = cmd;
	msg[1] = (status ? '*' : '!');
	msg[2] = '\0';
	if (tracingLevel & FAXTRACE_FIFO)
	    logInfo("FIFO SEND %s msg \"%s\"", (const char*) id, msg);
	HylaClient::getClient(id).send(msg, sizeof (msg));
    }
}

void
faxQueueApp::notifyModemWedged(Modem& modem)
{
    fxStr dev(idToDev(modem.getDeviceID()));
    logError("MODEM %s appears to be wedged", (const char*)dev);
    fxStr cmd(wedgedCmd
	| quote |  modem.getDeviceID() | enquote
	| quote |                  dev | enquote
    );
    traceServer("MODEM WEDGED: %s", (const char*) cmd);
    runCmd(cmd, true, this);
}

void
faxQueueApp::FIFOModemMessage(const fxStr& devid, const char* msg)
{
    if (! devid.length() > 0)
    {
	traceServer("Invalid modem FIFO message");
	return;
    }

    Modem& modem = Modem::getModemByID(devid);
    switch (msg[0]) {
    case 'R':			// modem ready, parse capabilities
	modem.stopLockPolling();
	if (msg[1] != '\0') {
	    modem.setCapabilities(msg+1);	// NB: also sets modem READY
	    traceModem(modem, "READY, capabilities %s", msg+1);
	} else {
	    modem.setState(Modem::READY);
	    traceModem(modem, "READY (no capabilities)");
	}
	Trigger::post(Trigger::MODEM_READY, modem);
	pokeScheduler();
	break;
    case 'B':			// modem busy doing something
	modem.stopLockPolling();
	traceModem(modem, "BUSY");
	modem.setState(Modem::BUSY);
	Trigger::post(Trigger::MODEM_BUSY, modem);
	break;
    case 'D':			// modem to be marked down
	modem.stopLockPolling();
	traceModem(modem, "DOWN");
	modem.setState(Modem::DOWN);
	Trigger::post(Trigger::MODEM_DOWN, modem);
	break;
    case 'N':			// modem phone number updated
	traceModem(modem, "NUMBER %s", msg+1);
	modem.setNumber(msg+1);
	break;
    case 'I':			// modem communication ID
	traceModem(modem, "COMID %s", msg+1);
	modem.setCommID(msg+1);
	break;
    case 'W':			// modem appears wedged
	// NB: modem should be marked down in a separate message
	notifyModemWedged(modem);
        Trigger::post(Trigger::MODEM_WEDGED, modem);
	break;
    case 'U':			// modem inuse by outbound job
	modem.stopLockPolling();
	traceModem(modem, "BUSY");
	modem.setState(Modem::BUSY);
	Trigger::post(Trigger::MODEM_INUSE, modem);
	break;
    case 'C':			// caller-ID information
	Trigger::post(Trigger::MODEM_CID, modem, msg+1);
	break;
    case 'd':			// data call begun
	Trigger::post(Trigger::MODEM_DATA_BEGIN, modem);
	break;
    case 'e':			// data call finished
	Trigger::post(Trigger::MODEM_DATA_END, modem);
	break;
    case 'v':			// voice call begun
	Trigger::post(Trigger::MODEM_VOICE_BEGIN, modem);
	break;
    case 'w':			// voice call finished
	Trigger::post(Trigger::MODEM_VOICE_END, modem);
	break;
    default:
	traceServer("FIFO: Bad modem message \"%s\" for modem %s",
		msg, (const char*)devid);
	break;
    }
}

void
faxQueueApp::FIFOJobMessage(const fxStr& jobid, const char* msg)
{
    Job* jp = Job::getJobByID(jobid);
    if (!jp) {
	traceServer("FIFO: JOB %s not found for msg \"%s\"",
	    (const char*) jobid, msg);
	return;
    }
    switch (msg[0]) {
    case 'c':			// call placed
	if (jp->pid == 0)
	{
	    /*
	     * We've now started a new job as part of a batch.
	     * This means we can "finish" the prevous job.
	     */
	    Job* ojp = (Job*)jp->prev;
	    jp->start = Sys::now();
	    jp->pid = ojp->pid;
	    doneJob(*ojp);
	}
	Trigger::post(Trigger::SEND_CALL, *jp);
	break;
    case 'C':			// call connected with fax
	Trigger::post(Trigger::SEND_CONNECTED, *jp);
	break;
    case 'd':			// page sent
	Trigger::post(Trigger::SEND_PAGE, *jp, msg+1);
	break;
    case 'D':			// document sent
	{ FaxSendInfo si; si.decode(msg+1); unrefDoc(si.qfile); }
	Trigger::post(Trigger::SEND_DOC, *jp, msg+1);
	break;
    case 'p':			// polled document received
	Trigger::post(Trigger::SEND_POLLRCVD, *jp, msg+1);
	break;
    case 'P':			// polling operation done
	Trigger::post(Trigger::SEND_POLLDONE, *jp, msg+1);
	break;
    default:
	traceServer("FIFO: Unknown job message \"%s\" for job %s",
		msg, (const char*)jobid);
	break;
    }
}

void
faxQueueApp::FIFORecvMessage(const fxStr& devid, const char* msg)
{
    if (! devid.length() > 0)
    {
	traceServer("Invalid modem FIFO message");
	return;
    }

    Modem& modem = Modem::getModemByID(devid);
    switch (msg[0]) {
    case 'B':			// inbound call started
	Trigger::post(Trigger::RECV_BEGIN, modem);
	break;
    case 'E':			// inbound call finished
	Trigger::post(Trigger::RECV_END, modem);
	break;
    case 'S':			// session started (received initial parameters)
	Trigger::post(Trigger::RECV_START, modem, msg+1);
	break;
    case 'P':			// page done
	Trigger::post(Trigger::RECV_PAGE, modem, msg+1);
	break;
    case 'D':			// document done
	Trigger::post(Trigger::RECV_DOC, modem, msg+1);
	break;
    default:
	traceServer("FIFO: Unknown recv message \"%s\" for modem %s",
		msg, (const char*)devid);
	break;
    }
}

/*
 * Configuration support.
 */

void
faxQueueApp::resetConfig()
{
    FaxConfig::resetConfig();
    dialRules = NULL;
    setupConfig();
}

#define	N(a)	(sizeof (a) / sizeof (a[0]))

faxQueueApp::stringtag faxQueueApp::strings[] = {
{ "logfacility",	&faxQueueApp::logFacility,	LOG_FAX },
{ "areacode",		&faxQueueApp::areaCode	},
{ "countrycode",	&faxQueueApp::countryCode },
{ "longdistanceprefix",	&faxQueueApp::longDistancePrefix },
{ "internationalprefix",&faxQueueApp::internationalPrefix },
{ "uucplockdir",	&faxQueueApp::uucpLockDir,	UUCP_LOCKDIR },
{ "uucplocktype",	&faxQueueApp::uucpLockType,	UUCP_LOCKTYPE },
{ "contcoverpage",	&faxQueueApp::contCoverPageTemplate },
{ "contcovercmd",	&faxQueueApp::coverCmd,		FAX_COVERCMD },
{ "notifycmd",		&faxQueueApp::notifyCmd,	FAX_NOTIFYCMD },
{ "ps2faxcmd",		&faxQueueApp::ps2faxCmd,	FAX_PS2FAXCMD },
{ "pdf2faxcmd",		&faxQueueApp::pdf2faxCmd,	FAX_PDF2FAXCMD },
{ "pcl2faxcmd",		&faxQueueApp::pcl2faxCmd,	FAX_PCL2FAXCMD },
{ "tiff2faxcmd",	&faxQueueApp::tiff2faxCmd,	FAX_TIFF2FAXCMD },
{ "sendfaxcmd",		&faxQueueApp::sendFaxCmd,
   FAX_LIBEXEC "/faxsend" },
{ "sendpagecmd",	&faxQueueApp::sendPageCmd,
   FAX_LIBEXEC "/pagesend" },
{ "senduucpcmd",	&faxQueueApp::sendUUCPCmd,
   FAX_LIBEXEC "/uucpsend" },
{ "wedgedcmd",		&faxQueueApp::wedgedCmd,	FAX_WEDGEDCMD },
{ "jobcontrolcmd",	&faxQueueApp::jobCtrlCmd,	"" },
};
faxQueueApp::numbertag faxQueueApp::numbers[] = {
{ "tracingmask",	&faxQueueApp::tracingMask,	// NB: must be first
   FAXTRACE_MODEMIO|FAXTRACE_TIMEOUTS },
{ "servertracing",	&faxQueueApp::tracingLevel,	FAXTRACE_SERVER },
{ "uucplocktimeout",	&faxQueueApp::uucpLockTimeout,	0 },
{ "postscripttimeout",	&faxQueueApp::postscriptTimeout, 3*60 },
{ "maxconcurrentjobs",	&faxQueueApp::maxConcurrentCalls, 1 },
{ "maxconcurrentcalls",	&faxQueueApp::maxConcurrentCalls, 1 },
{ "maxbatchjobs",	&faxQueueApp::maxBatchJobs,	(u_int) 64 },
{ "maxsendpages",	&faxQueueApp::maxSendPages,	(u_int) 4096 },
{ "maxtries",		&faxQueueApp::maxTries,		(u_int) FAX_RETRIES },
{ "maxdials",		&faxQueueApp::maxDials,		(u_int) FAX_REDIALS },
{ "jobreqother",	&faxQueueApp::requeueInterval,	FAX_REQUEUE },
{ "polllockwait",	&faxQueueApp::pollLockWait,	30 },
};

faxQueueApp::booltag faxQueueApp::booleans[] = {
{ "use2d",		&faxQueueApp::use2D,		true },
{ "useunlimitedln",	&faxQueueApp::useUnlimitedLN,	true },
};

void
faxQueueApp::setupConfig()
{
    int i;

    for (i = N(strings)-1; i >= 0; i--)
	(*this).*strings[i].p = (strings[i].def ? strings[i].def : "");
    for (i = N(numbers)-1; i >= 0; i--)
	(*this).*numbers[i].p = numbers[i].def;
    for (i = N(booleans)-1; i >= 0; i--)
	(*this).*booleans[i].p = booleans[i].def;
    tod.reset();			// any day, any time
    uucpLockMode = UUCP_LOCKMODE;
    delete dialRules, dialRules = NULL;
    ModemGroup::reset();		// clear+add ``any modem'' class
    ModemGroup::set(MODEM_ANY, new RE(".*"));
    pageChop = FaxRequest::chop_last;
    pageChopThreshold = 3.0;		// minimum of 3" of white space
}

void
faxQueueApp::configError(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogError(fmt, ap);
    va_end(ap);
}

void
faxQueueApp::configTrace(const char* fmt, ...)
{
    if (tracingLevel & FAXTRACE_CONFIG) {
	va_list ap;
	va_start(ap, fmt);
	vlogError(fmt, ap);
	va_end(ap);
    }
}

static void
crackArgv(fxStr& s)
{
    u_int i = 0;
    do {
        while (i < s.length() && !isspace(s[i])) i++;
        if (i < s.length()) {
            s[i++] = '\0';
            u_int j = i;
            while (j < s.length() && isspace(s[j])) j++;
            if (j > i) {
                s.remove(i, j - i);
            }
        }
    } while (i < s.length());
    s.resize(i);
}

static void
tiffErrorHandler(const char* module, const char* fmt0, va_list ap)
{
    fxStr fmt = (module != NULL) ?
        fxStr::format("%s: Warning, %s.", module, fmt0)
        : fxStr::format("Warning, %s.", fmt0);
    vlogError(fmt, ap);
}

static void
tiffWarningHandler(const char* module, const char* fmt0, va_list ap)
{
    fxStr fmt = (module != NULL) ?
        fxStr::format("%s: Warning, %s.", module, fmt0)
        : fxStr::format("Warning, %s.", fmt0);
    vlogWarning(fmt, ap);
}

bool
faxQueueApp::setConfigItem(const char* tag, const char* value)
{
    u_int ix;
    if (findTag(tag, (const tags*) strings, N(strings), ix)) {
	(*this).*strings[ix].p = value;
	switch (ix) {
	case 0:	faxApp::setLogFacility(logFacility); break;
	}
	if (ix >= 8)
	    crackArgv((*this).*strings[ix].p);
    } else if (findTag(tag, (const tags*) numbers, N(numbers), ix)) {
	(*this).*numbers[ix].p = getNumber(value);
	switch (ix) {
	case 1:
	    tracingLevel &= ~tracingMask;
	    if (dialRules)
		dialRules->setVerbose((tracingLevel&FAXTRACE_DIALRULES) != 0);
	    if (tracingLevel&FAXTRACE_TIFF) {
		TIFFSetErrorHandler(tiffErrorHandler);
		TIFFSetWarningHandler(tiffWarningHandler);
	    } else {
		TIFFSetErrorHandler(NULL);
		TIFFSetWarningHandler(NULL);
	    }
	    break;
	case 2: UUCPLock::setLockTimeout(uucpLockTimeout); break;
	}
    } else if (findTag(tag, (const tags*) booleans, N(booleans), ix)) {
	(*this).*booleans[ix].p = getBoolean(value);
    } else if (streq(tag, "dialstringrules"))
	setDialRules(value);
    else if (streq(tag, "timeofday"))
	tod.parse(value);
    else if (streq(tag, "uucplockmode"))
	uucpLockMode = (mode_t) strtol(value, 0, 8);
    else if (streq(tag, "modemgroup")) {
	const char* cp;
	for (cp = value; *cp && *cp != ':'; cp++)
	    ;
	if (*cp == ':') {
	    fxStr name(value, cp-value);
	    for (cp++; *cp && isspace(*cp); cp++)
		;
	    if (*cp != '\0') {
		RE* re = new RE(cp);
		if (re->getErrorCode() > REG_NOMATCH) {
		    fxStr emsg;
		    re->getError(emsg);
		    configError("Bad pattern for modem group \"%s\": %s: %s", (const char*) emsg,
			(const char*) name, re->pattern());
		} else
		    ModemGroup::set(name, re);
	    } else
		configError("No regular expression for modem group");
	} else
	    configError("Missing ':' separator in modem group specification");
    } else if (streq(tag, "pagechop")) {
	if (streq(value, "all"))
	    pageChop = FaxRequest::chop_all;
	else if (streq(value, "none"))
	    pageChop = FaxRequest::chop_none;
	else if (streq(value, "last"))
	    pageChop = FaxRequest::chop_last;
    } else if (streq(tag, "pagechopthreshold"))
	pageChopThreshold = atof(value);
    else if (streq(tag, "audithook") )
    {
        const char* cp;
	for (cp = value; *cp && *cp != ':'; cp++)
	    ;
	if (*cp == ':') {
	    fxStr cmd(value, cp-value);
	    for (cp++; *cp && isspace(*cp); cp++)
		;
	    if (*cp != '\0') {
	    	Trigger::setTriggerHook(cmd, cp);
	    } else
		configError("No trigger specification for audit hook");
	} else
	    configError("Missing ':' separator in audit hook specification");
	
    	
    } else
	return (false);
    return (true);
}

/*
 * Subclass DialStringRules so that we can redirect the
 * diagnostic and tracing interfaces through the server.
 */
class MyDialStringRules : public DialStringRules {
private:
    virtual void parseError(const char* fmt ...);
    virtual void traceParse(const char* fmt ...);
    virtual void traceRules(const char* fmt ...);
public:
    MyDialStringRules(const char* filename);
    ~MyDialStringRules();
};
MyDialStringRules::MyDialStringRules(const char* f) : DialStringRules(f) {}
MyDialStringRules::~MyDialStringRules() {}

void
MyDialStringRules::parseError(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogError(fmt, ap);
    va_end(ap);
}
void
MyDialStringRules::traceParse(const char* fmt ...)
{
    if (faxQueueApp::instance().getTracingLevel() & FAXTRACE_DIALRULES) {
	va_list ap;
	va_start(ap, fmt);
	vlogInfo(fmt, ap);
	va_end(ap);
    }
}
void
MyDialStringRules::traceRules(const char* fmt ...)
{
    if (faxQueueApp::instance().getTracingLevel() & FAXTRACE_DIALRULES) {
	va_list ap;
	va_start(ap, fmt);
	vlogInfo(fmt, ap);
	va_end(ap);
    }
}

void
faxQueueApp::setDialRules(const char* name)
{
    delete dialRules;
    dialRules = new MyDialStringRules(name);
    dialRules->setVerbose((tracingLevel & FAXTRACE_DIALRULES) != 0);
    /*
     * Setup configuration environment.
     */
    dialRules->def("AreaCode", areaCode);
    dialRules->def("CountryCode", countryCode);
    dialRules->def("LongDistancePrefix", longDistancePrefix);
    dialRules->def("InternationalPrefix", internationalPrefix);
    if (!dialRules->parse()) {
	configError("Parse error in dial string rules \"%s\"", name);
	delete dialRules, dialRules = NULL;
    }
}

/*
 * Convert a dialing string to a canonical format.
 */
fxStr
faxQueueApp::canonicalizePhoneNumber(const fxStr& ds)
{
    if (dialRules)
	return dialRules->canonicalNumber(ds);
    else
	return ds;
}

/*
 * Create an appropriate UUCP lock instance.
 */
UUCPLock*
faxQueueApp::getUUCPLock(const fxStr& deviceName)
{
    return UUCPLock::newLock(uucpLockType,
	uucpLockDir, deviceName, uucpLockMode);
}

u_int faxQueueApp::getTracingLevel() const
    { return tracingLevel; }
u_int faxQueueApp::getMaxConcurrentCalls() const
    { return maxConcurrentCalls; }
u_int faxQueueApp::getMaxSendPages() const
    { return maxSendPages; }
u_int faxQueueApp::getMaxDials() const
    { return maxDials; }
u_int faxQueueApp::getMaxTries() const
    { return maxTries; }
time_t faxQueueApp::nextTimeToSend(time_t t) const
    { return tod.nextTimeOfDay(t); }

/*
 * Miscellaneous stuff.
 */

/*
 * Notify the sender of a job that something has
 * happened -- the job has completed, it's been requeued
 * for later processing, etc.
 */
void
faxQueueApp::notifySender(Job& job, JobStatus why, const char* duration)
{
    fxStr cmd(notifyCmd
	| quote |		 job.file | enquote
	| quote | Job::jobStatusName(why) | enquote
	| quote |		 duration | enquote
    );
    if (why == Job::requeued) {
	/*
	 * It's too hard to do localtime in an awk script,
	 * so if we may need it, we calculate it here
	 * and pass the result as an optional argument.
	 */
	char buf[30];
	strftime(buf, sizeof (buf), " \"%H:%M\"", localtime(&job.tts));
	cmd.append(buf);
    }
    traceServer("NOTIFY: %s", (const char*) cmd);
    runCmd(cmd, true, this);
}

void
faxQueueApp::vtraceServer(const char* fmt, va_list ap)
{
    if (tracingLevel & FAXTRACE_SERVER)
	vlogInfo(fmt, ap);
}

void
faxQueueApp::traceServer(const char* fmt ...)
{
    if (tracingLevel & FAXTRACE_SERVER) {
	va_list ap;
	va_start(ap, fmt);
	vlogInfo(fmt, ap);
	va_end(ap);
    }
}

static void
vtraceJob(const Job& job, const char* fmt, va_list ap)
{
    static const char* stateNames[] = {
        "state#0", "suspended", "pending", "sleeping", "blocked",
	"ready", "active", "done", "failed"
    };
    time_t now = Sys::now();
    vlogInfo(
	  "JOB " | job.jobid
	| " (" | stateNames[job.state%9]
	| " dest " | job.dest
	| fxStr::format(" pri %u", job.pri)
	| " tts " | strTime(job.tts - now)
	| " killtime " | strTime(job.killtime - now)
	| "): "
	| fmt, ap);
}
static void
vtraceDI(const DestInfo& di, const char* fmt, va_list ap)
{
    vlogInfo(
	  "DEST (" | fxStr::format("%p", &di)
	| ": " | fxStr::format("%d ready", di.getReadyCount())
	| ", " | fxStr::format("%d batches", di.getActiveCount())
	| ", " | fxStr::format("%d sleeping", di.getSleepCount())
	| ":) "
	| fmt, ap);
}

void
faxQueueApp::traceQueue(const DestInfo& di, const char* fmt ...)
{
    if (tracingLevel & FAXTRACE_QUEUEMGMT) {
	va_list ap;
	va_start(ap, fmt);
	vtraceDI(di, fmt, ap);
	va_end(ap);
    }
}

void
faxQueueApp::traceQueue(const Job& job, const char* fmt ...)
{
    if (tracingLevel & FAXTRACE_QUEUEMGMT) {
	va_list ap;
	va_start(ap, fmt);
	vtraceJob(job, fmt, ap);
	va_end(ap);
    }
}

void
faxQueueApp::traceJob(const Job& job, const char* fmt ...)
{
    if (tracingLevel & FAXTRACE_JOBMGMT) {
	va_list ap;
	va_start(ap, fmt);
	vtraceJob(job, fmt, ap);
	va_end(ap);
    }
}

void
faxQueueApp::traceQueue(const char* fmt ...)
{
    if (tracingLevel & FAXTRACE_QUEUEMGMT) {
	va_list ap;
	va_start(ap, fmt);
	vlogInfo(fmt, ap);
	va_end(ap);
    }
}

void
faxQueueApp::traceModem(const Modem& modem, const char* fmt ...)
{
    if (tracingLevel & FAXTRACE_MODEMSTATE) {
	va_list ap;
	va_start(ap, fmt);
	vlogInfo("MODEM " | modem.getDeviceID() | ": " | fmt, ap);
	va_end(ap);
    }
}

void
faxQueueApp::jobError(const Job& job, const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlogError("JOB " | job.jobid | ": " | fmt, ap);
    va_end(ap);
}

void
faxQueueApp::showDebugState(void)
{
    traceServer("DEBUG: Listing destJobs with %d items", destJobs.size());
    for (DestInfoDictIter iter(destJobs); iter.notDone(); iter++)
    {
	const fxStr& dest(iter.key());
	const DestInfo& di(iter.value());
	traceServer("DestInfo (%p) to %s", &di, (const char*)dest);
    }


    traceServer("DEBUG: runq(%p) next %p", &runq, runq.next);
    for (QLink* ql = runq.next; ql != &runq; ql = ql->next)
    {
	DestInfo& di = *(DestInfo*)ql;
	traceServer("DestInfo (%p) contains %d active, %d ready",
		&di, di.getActiveCount(), di.getReadyCount());

	for (JobIter iter(di.readyQ); iter.notDone(); iter++)
	{
	    Job& job(iter);
	    traceJob(job, "In run queue");
	}
	for (JobIter iter(di.sleepQ); iter.notDone(); iter++)
	{
	    Job& job(iter);
	    traceJob(job, "In sleep queue");
	}
    }

    traceServer("DEBUG: batchq(%p) next %p", &batchq, batchq.next);
    for (QLink* ql = batchq.next; ql != &batchq; ql = ql->next)
    {
	Batch& batch = *(Batch*)ql;
	for (JobIter iter(batch.jobs); iter.notDone(); iter++)
	{
	    Job& job(iter);
	    traceJob(job, "In active batch");
	}
    }

    traceServer("DEBUG: destq(%p) next %p", &destq, destq.next);
    for (QLink* ql = destq.next; ql != &destq; ql = ql->next)
    {
	DestInfo& di = *(DestInfo*)ql;
	traceServer("DestInfo (%p) contains %d active, %d ready",
		&di, di.getActiveCount(), di.getReadyCount());

	for (JobIter iter(di.readyQ); iter.notDone(); iter++)
	{
	    Job& job(iter);
	    traceJob(job, "SHOULD NOT BE IN RUN QUEUE");
	}
	for (JobIter iter(di.sleepQ); iter.notDone(); iter++)
	{
	    Job& job(iter);
	    traceJob(job, "In sleep queue");
	}
    }

    traceServer("DEBUG: pendq(%p) next %p", &pendq, pendq.next);
    for (JobIter iter(pendq); iter.notDone(); iter++)
    {
	Job& job(iter);
	traceJob(job, "In pending queue");
    }

    traceServer("DEBUG: suspendq(%p) next %p", &suspendq, suspendq.next);
    for (JobIter iter(suspendq); iter.notDone(); iter++)
    {
	Job& job(iter);
	traceJob(job, "In suspend queue");
    }
    traceServer("DEBUG: inSchedule: %s", inSchedule ? "YES" : "NO");

    // This is a hack to easlily *poke* it at any time we want to force
    // a runSchedule() for debugging purposes
    pokeScheduler();
}


void faxQueueApp::childStatus(pid_t pid, int status)
{
    // We don't do anything here - nothing to act on.
    traceServer("NOTIFY exit status: %#o (%u)", status, pid);
}

static void
usage(const char* appName)
{
    faxApp::fatal("usage: %s [-q queue-directory] [-D]", appName);
}

static void
sigCleanup(int)
{
    faxQueueApp::instance().close();
    _exit(-1);
}

int
main(int argc, char** argv)
{
    faxApp::setupLogging("FaxQueuer");

    fxStr appName = argv[0];
    u_int l = appName.length();
    appName = appName.tokenR(l, '/');

    faxApp::setupPermissions();

    faxApp::setOpts("q:D");

    bool detach = true;
    fxStr queueDir(FAX_SPOOLDIR);
    for (GetoptIter iter(argc, argv, faxApp::getOpts()); iter.notDone(); iter++)
	switch (iter.option()) {
	case 'q': queueDir = iter.optArg(); break;
	case 'D': detach = false; break;
	case '?': usage(appName);
	}
    if (Sys::chdir(queueDir) < 0)
	faxApp::fatal(queueDir | ": Can not change directory");
    if (!Sys::isRegularFile(FAX_ETCDIR "/setup.cache"))
	faxApp::fatal("No " FAX_ETCDIR "/setup.cache file; run faxsetup first");
    if (detach)
	faxApp::detachFromTTY();

    faxQueueApp* app = new faxQueueApp;

    signal(SIGTERM, fxSIGHANDLER(sigCleanup));
    signal(SIGINT, fxSIGHANDLER(sigCleanup));

    app->initialize(argc, argv);
    app->open();
    while (app->isRunning())
	Dispatcher::instance().dispatch();
    app->close();
    delete app;


    Modem::CLEANUP();
    delete &Dispatcher::instance();
    
    return 0;
}
