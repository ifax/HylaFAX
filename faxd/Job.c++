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
#include "faxQueueApp.h"
#include "FaxRequest.h"
#include "Dispatcher.h"
#include "TriggerRef.h"
#include "Sys.h"
#include "config.h"

JobKillHandler::JobKillHandler(Job& j) : job(j) {}
JobKillHandler::~JobKillHandler() {}
void JobKillHandler::timerExpired(long, long)
    { faxQueueApp::instance().timeoutJob(job); }

JobTTSHandler::JobTTSHandler(Job& j) : job(j) {}
JobTTSHandler::~JobTTSHandler() {}
void JobTTSHandler::timerExpired(long, long)
    { faxQueueApp::instance().runJob(job); }

JobPrepareHandler::JobPrepareHandler(Job& j) : job(j) {}
JobPrepareHandler::~JobPrepareHandler() {}
void JobPrepareHandler::childStatus(pid_t, int status)
    { faxQueueApp::instance().prepareJobDone(job, status); }

JobSendHandler::JobSendHandler(Job& j) : job(j) {}
JobSendHandler::~JobSendHandler() {}
void JobSendHandler::childStatus(pid_t, int status)
    { faxQueueApp::instance().sendJobDone(job, status); }

fxIMPLEMENT_StrKeyPtrValueDictionary(JobDict, Job*)
JobDict Job::registry;

Job::Job(const FaxRequest& req)
    : killHandler(*this)
    , ttsHandler(*this)
    , prepareHandler(*this)
    , sendHandler(*this)
    , file(req.qfile)
    , jobid(req.jobid)
{
    update(req);

    start = 0;
    pid = 0;
    state = req.state;

    dnext = NULL;
    modem = NULL;
    suspendPending = false;
    registry[jobid] = this;
}

Job::~Job()
{
    registry.remove(jobid);
    stopKillTimer();
    stopTTSTimer();
    if (!triggers.isEmpty())		// purge trigger references
	TriggerRef::purge(triggers);
}

/*
 * Update volatile job state from the job description
 * file.  This propagates client-alterable state from
 * on-disk to in-memory.  Note that we do not touch the
 * job state since there are cases where the scheduler
 * updates the in-memory state but does not save it to
 * the description file (maybe this will change?).
 */
void
Job::update(const FaxRequest& req)
{
    tts = (req.tts == 0 ? Sys::now() : req.tts);
    killtime = req.killtime;
    pri = req.pri;
    // NB: state is not overwritten
    pagewidth = req.pagewidth;
    pagelength = req.pagelength;
    resolution = req.resolution;
    willpoll = (req.findRequest(FaxRequest::send_poll) != fx_invalidArrayIndex);
    device = req.modem;
}

Job*
Job::getJobByID(const fxStr& id)
{
    Job** jpp = (Job**) registry.find(id);
    return (jpp ? *jpp : (Job*) NULL);
}

void
Job::startKillTimer(long sec)
{
    killtime = sec;
    Dispatcher::instance().startTimer(sec - Sys::now(), 0, &killHandler);
}

void
Job::stopKillTimer()
{
    Dispatcher::instance().stopTimer(&killHandler);
}

void
Job::startTTSTimer(long sec)
{
    tts = sec;
    Dispatcher::instance().startTimer(sec - Sys::now(), 0, &ttsHandler);
}

void
Job::stopTTSTimer()
{
    Dispatcher::instance().stopTimer(&ttsHandler);
}

void
Job::startPrepare(pid_t p)
{
    Dispatcher::instance().startChild(pid = p, &prepareHandler);
}

void
Job::startSend(pid_t p)
{
    Dispatcher::instance().startChild(pid = p, &sendHandler);
}

fxStr
Job::jobStatusName(const JobStatus status)
{
    static const char* names[] = {
	"no_status",
	"done",
	"requeued",
	"removed",
	"timedout",
	"no_formatter",
	"failed",
	"format_failed",
	"poll_rejected",
	"poll_no_document",
	"poll_failed",
	"killed",
	"blocked",
	"rejected",
    };
#define	N(a)	(sizeof (a) / sizeof (a[0]))
    if ((u_int) status >= N(names)) {
        return fxStr::format("status_%u", (u_int) status);
    } else {
        return fxStr(names[status]);
    }
}
#undef N

#include "JobExt.h"
#include "StackBuffer.h"

void
Job::encode(fxStackBuffer& buf) const
{
    buf.put((const char*) &tts, sizeof (JobExtFixed));

    buf.put(jobid,  jobid.length()+1);
    buf.put(dest,   dest.length()+1);
    buf.put(device, device.length()+1);
    buf.put(commid, commid.length()+1);
}
