/*	$Id$ */
/*
 * Copyright (c) 2007 iFAX Solutions Inc.
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
#include "Batch.h"

Batch::Batch (DestInfo& d, const char* i, const char* t)
  : di(d)
  , dest(i)
  , jobtype(t)
{
    pid = 0;
    prepareJob = NULL;
    dnext = NULL;
}

Batch::~Batch ()
{
    fxAssert(pid == 0, "PID not empty for Batch::~~Batch()");
}

u_int
Batch::jobCount() const
{
    u_int c = 0;
    for (QLink* ql = jobs.next; ql != &jobs; ql = ql->next)
	c++;
    return c;
}

Job&
Batch::firstJob()
{
   fxAssert(!jobs.isEmpty(), "No Jobs for Batch::firstJob()");
   return *(Job*)jobs.next;
}

Job&
Batch::lastJob()
{
   fxAssert(!jobs.isEmpty(), "No Jobs for Batch::lastJob()");
   return *(Job*)jobs.prev;
}

void
Batch::startPrepare(Job& job, pid_t p)
{
    fxAssert(pid == 0, "PID not empty for Batch::startPrepare()");
    fxAssert(prepareJob == NULL, "prepareJob not NULL for Batch::startPrepare()");
    prepareJob = &job;
    pid = p;
    Dispatcher::instance().startChild(pid, this);
}

void
Batch::startSend(pid_t p)
{
    fxAssert(pid == 0, "PID not empty for Batch::startSend()");
    fxAssert(prepareJob == NULL, "prepareJob not NULL for Batch::startSend()");
    pid = p;
    Dispatcher::instance().startChild(pid, this);
}

void
Batch::childStatus(pid_t p, int status)
{
    fxAssert(p == pid, "PID does not match for Batch::ChildStatus");
    pid = 0;
    if (prepareJob)
    {
	prepareJob = NULL;
	faxQueueApp::instance().prepareDone(*this, status);
    } else
    {
	faxQueueApp::instance().sendDone(*this, status);
    }
}

