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
#ifndef _Job_
#define	_Job_
/*
 * Queue Manager Job.
 */
#include "IOHandler.h"
#include "Dictionary.h"
#include "QLink.h"
#include "Str.h"

typedef unsigned int JobStatus;
class Modem;
class Job;
class FaxRequest;
class fxStackBuffer;

/*
 * NB: These should be private nested classes but various
 *     C++ compilers cannot grok it.
 */
class JobKillHandler : public IOHandler {
private:
    Job& job;
public:
    JobKillHandler(Job&);
    ~JobKillHandler();
    void timerExpired(long, long);
};
class JobTTSHandler : public IOHandler {
private:
    Job& job;
public:
    JobTTSHandler(Job&);
    ~JobTTSHandler();
    void timerExpired(long, long);
};
class JobPrepareHandler : public IOHandler {
private:
    Job& job;
public:
    JobPrepareHandler(Job&);
    ~JobPrepareHandler();
    void childStatus(pid_t, int);
};
class JobSendHandler : public IOHandler {
private:
    Job& job;
public:
    JobSendHandler(Job&);
    ~JobSendHandler();
    void childStatus(pid_t, int);
};

fxDECLARE_StrKeyDictionary(JobDict, Job*)

/*
 * Jobs represent outbound requests in the queue.
 */
class Job : public QLink {
private:
    JobKillHandler	killHandler;	// Dispatcher handler for kill timeout
    JobTTSHandler	ttsHandler;	// Dispatcher handler for tts timeout
    JobPrepareHandler	prepareHandler;	// Dispatcher handler for job prep work
    JobSendHandler	sendHandler;	// Dispatcher handler for job send work

    static JobDict registry;
public:
    enum {
	no_status	= 0,
	done		= 1,		// job completed successfully
	requeued	= 2,		// job requeued after attempt
	removed		= 3,		// job removed by user command
	timedout	= 4,		// job kill time expired
	no_formatter	= 5,		// PostScript formatter not found
	failed		= 6,		// job completed w/o success
	format_failed	= 7,		// PostScript formatting failed
	poll_rejected	= 8,		// poll rejected by destination
	poll_no_document= 9,		// poll found no documents
	poll_failed	= 10,		// poll failed for unknown reason
	killed		= 11,		// job killed by user command
	blocked		= 12,		// job waiting for resource or event
	rejected	= 13		// job rejected before send attempted
    };
    // NB: members are aligned for quick encode/decode
    time_t	tts;		// time to send job
    time_t	killtime;	// time to kill job
    time_t	start;		// time job passed to modem
    int		pri;		// priority
    pid_t	pid;		// pid of current subprocess
    u_short	state;		// scheduling state
    u_short	pagewidth;	// desired output page width (mm)
    u_short	pagelength;	// desired output page length (mm)
    u_short	resolution;	// desired vertical resolution (lpi) (normal/fine)
    bool	willpoll;	// job has polling request
    bool	suspendPending;	// suspend state change pending for job

    fxStr	file;		// queue file name
    fxStr	jobid;		// job identifier
    fxStr	dest;		// canonical destination identity
    fxStr	device;		// modem to be used
    fxStr	commid;		// commid of last call
    Job*	dnext;		// linked list by destination
    Modem*	modem;		// modem/server currently assigned to job
    QLink	triggers;	// waiting specifically on this job

    Job(const FaxRequest&);
    void update(const FaxRequest& req);
    ~Job();

    static Job* getJobByID(const fxStr& jobid);
    static fxStr jobStatusName(const JobStatus);

    void startKillTimer(long sec);
    void stopKillTimer();

    void startTTSTimer(long sec);
    void stopTTSTimer();

    void startPrepare(pid_t pid);
    void startSend(pid_t pid);

    void encode(fxStackBuffer&) const;	// encode in JobExt format
};

/*
 * Job iterator class for iterating over lists.
 */
class JobIter {
private:
    const QLink* head;
    QLink*	ql;
    QLink*	next;
public:
    JobIter(QLink& q)		{ head = &q; ql = q.next, next = ql->next; }
    ~JobIter() {}

    void operator=(QLink& q)	{ head = &q; ql = q.next; next = ql->next; }
    void operator++()		{ ql = next, next = ql->next; }
    void operator++(int)	{ ql = next, next = ql->next; }
    operator Job&() const	{ return *(Job*)ql; }
    operator Job*() const	{ return (Job*) ql; }
    Job& job() const		{ return *(Job*)ql; }
    bool notDone()		{ return ql != head; }
};
#endif /* _Job_ */
