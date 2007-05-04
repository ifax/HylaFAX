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
#include "DestInfo.h"
#include "Job.h"
#include "Str.h"

DestInfo::DestInfo()
{
    activeCount = 0;
    running = NULL;
}

DestInfo::DestInfo(const DestInfo& other)
    : QLink(other)
    , info(other.info)
{
    activeCount = other.activeCount;
    running = other.running;
}

DestInfo::~DestInfo()
{
    fxAssert(readyQ.isEmpty(), "DestInfo destructed with jobs on it's readyQ");
    fxAssert(sleepQ.isEmpty(), "DestInfo destructed with jobs on it's sleepQ");
    fxAssert(activeCount == 0, "DestInfo destructed with jobs active");
    if (isOnList())
	remove();
}

FaxMachineInfo&
DestInfo::getInfo(const fxStr& number)
{
    info.updateConfig(number);			// update as necessary
    return info;
}

void
DestInfo::updateConfig()
{
    info.writeConfig();				// update as necessary
}

u_int
DestInfo::getReadyCount() const
{
    u_int c = 0;

    for (const QLink* ql = readyQ.next; ql != &readyQ; ql = ql->next)
	c++;

    return c;
}

u_int
DestInfo::getSleepCount() const
{
    u_int c = 0;

    for (const QLink* ql = sleepQ.next; ql != &sleepQ; ql = ql->next)
	c++;

    return c;
}

u_int
DestInfo::getActiveCount() const
{
    return activeCount;
}

bool
DestInfo::isEmpty() const
{
    if (! readyQ.isEmpty())
	return false;
    if (!sleepQ.isEmpty())
	return false;
    if (getActiveCount())
	return false;

    return true;
}

bool
DestInfo::isActive(Job& job) const
{
    if (running == NULL)
	return (false);
    else if (running == &job)
	return (true);
    else {
	for (Job* jp = running->dnext; jp != NULL; jp = jp->dnext)
	    if (jp == &job)
		return (true);
	return (false);
    }
}

void
DestInfo::active(Job& job)
{
    if (running == NULL) {			// list empty
	running = &job;
	job.dnext = NULL;
	activeCount++;
    } else if (running == &job) {		// job on list already
	return;
    } else {					// general case
	Job* jp;
	Job** jpp;
	for (jpp = &running->dnext; (jp = *jpp) != NULL; jpp = &jp->dnext)
	    if (jp == &job)
		return;
	*jpp = &job;
	job.dnext = NULL;
	activeCount++;
    }
}

void
DestInfo::done(Job& job)
{
    if (running == &job) {			// job at head of list
	running = job.dnext;
	job.dnext = NULL;
	activeCount--;
    } else if (running == NULL) {		// list empty
	return;
    } else {					// general case
	Job* jp;
	for (Job** jpp = &running->dnext; (jp = *jpp) != NULL; jpp = &jp->dnext)
	    if (jp == &job) {
		*jpp = job.dnext;
		job.dnext = NULL;
		activeCount--;
		break;
	    }
    }
}

fxIMPLEMENT_StrKeyObjValueDictionary(DestInfoDict, DestInfo)
