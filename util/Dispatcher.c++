/*
 * Copyright (c) 1987-1991 Stanford University
 * Copyright (c) 1991-1996 Silicon Graphics, Inc.
 * HylaFAX is a trademark of Silicon Graphics
 *
 * Permission to use, copy, modify, distribute, and sell this software and 
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Stanford and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Stanford and Silicon Graphics.
 * 
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
 *
 * IN NO EVENT SHALL STANFORD OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
 * OF THIS SOFTWARE.
 */

// Dispatcher provides an interface to the "select" system call.
#include "Sys.h"			// NB: must be first

#include <errno.h>
#include <sys/param.h>
#if HAS_SELECT_H
#include <sys/select.h>
#endif
extern "C" {
#include <sys/time.h>
}
#include <limits.h>

#include "Dispatcher.h"
#include "IOHandler.h"

Dispatcher* Dispatcher::_instance;

/*
    XXX This is a bad hack to get it to work with glibc 2.1, should
    be removed and the code dependent on it fixed at the first opportunity.
*/
#if defined __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 1
#define fds_bits __fds_bits
#endif

class FdMask : public fd_set {
public:
    FdMask();
    void zero();
    void setBit(int);
    void clrBit(int);
    fxBool isSet(int) const;
    fxBool anySet() const;
    int numSet() const;
};

FdMask::FdMask() {
    zero();
}

void FdMask::zero() { memset(this, 0, sizeof(FdMask)); }
void FdMask::setBit(int fd) { FD_SET(fd,this); }
void FdMask::clrBit(int fd) { FD_CLR(fd,this); }
fxBool FdMask::isSet(int fd) const { return (FD_ISSET(fd,this) != 0); }

fxBool FdMask::anySet() const {
    const int mskcnt = howmany(FD_SETSIZE,NFDBITS);
    for (int i = 0; i < mskcnt; i++) {
	if (fds_bits[i]) {
	    return TRUE;
	}
    }
    return FALSE;
}

int FdMask::numSet() const {
    const int mskcnt = howmany(FD_SETSIZE,NFDBITS);
    int n = 0;
    for (int i = 0; i < mskcnt; i++) {
	if (fds_bits[i]) {
	    for (int j = 0; j < NFDBITS; j++) {
		if ((fds_bits[i] & (1 << j)) != 0) {
		    n += 1;
		}
	    }
	}
    }
    return n;
}

/*
 * Operations on timeval structures.
 */

const long ONE_SECOND = 1000000;

timeval operator+(timeval src1, timeval src2) {
    timeval sum;
    sum.tv_sec = src1.tv_sec + src2.tv_sec;
    sum.tv_usec = src1.tv_usec + src2.tv_usec;
    if (sum.tv_usec >= ONE_SECOND) {
	sum.tv_usec -= ONE_SECOND;
	sum.tv_sec++;
    } else if (sum.tv_sec >= 1 && sum.tv_usec < 0) {
	sum.tv_usec += ONE_SECOND;
	sum.tv_sec--;
    }
    return sum;
}

timeval operator-(timeval src1, timeval src2) {
    timeval delta;
    delta.tv_sec = src1.tv_sec - src2.tv_sec;
    delta.tv_usec = src1.tv_usec - src2.tv_usec;
    if (delta.tv_usec < 0) {
	delta.tv_usec += ONE_SECOND;
	delta.tv_sec--;
    } else if (delta.tv_usec >= ONE_SECOND) {
	delta.tv_usec -= ONE_SECOND;
	delta.tv_sec++;
    }
    return delta;
}

fxBool operator>(timeval src1, timeval src2) {
    if (src1.tv_sec > src2.tv_sec) {
	return TRUE;
    } else if (src1.tv_sec == src2.tv_sec && src1.tv_usec > src2.tv_usec) {
	return TRUE;
    } else {
	return FALSE;
    }
}

fxBool operator<(timeval src1, timeval src2) {
    if (src1.tv_sec < src2.tv_sec) {
	return TRUE;
    } else if (src1.tv_sec == src2.tv_sec && src1.tv_usec < src2.tv_usec) {
	return TRUE;
    } else {
	return FALSE;
    }
}

/*
 * Interface to timers.
 */

struct Timer {
    Timer(timeval t, IOHandler* h, Timer* n);

    timeval timerValue;
    IOHandler* handler;
    Timer* next;
};

class TimerQueue {
public:
    TimerQueue();
    virtual ~TimerQueue();

    fxBool isEmpty() const;
    static timeval zeroTime();
    timeval earliestTime() const;
    static timeval currentTime();

    void insert(timeval, IOHandler*);
    void remove(IOHandler*);
    void expire(timeval);
private:
    Timer* _first;
    static timeval _zeroTime;
};

Timer::Timer(timeval t, IOHandler* h, Timer* n) :
    timerValue(t),
    handler(h),
    next(n) {}

timeval TimerQueue::_zeroTime;

TimerQueue::TimerQueue() :
    _first(NULL) {}

TimerQueue::~TimerQueue() {
    Timer* doomed = _first;
    while (doomed != NULL) {
	Timer* next = doomed->next;
	delete doomed;
	doomed = next;
    }
}

inline fxBool TimerQueue::isEmpty() const {
    return _first == NULL;
}

inline timeval TimerQueue::zeroTime() {
    return _zeroTime;
}

inline timeval TimerQueue::earliestTime() const {
    return _first->timerValue;
}

timeval TimerQueue::currentTime() {
    timeval curTime;
    gettimeofday(&curTime, 0);
    return curTime;
}

void TimerQueue::insert(timeval futureTime, IOHandler* handler) {
    if (isEmpty() || futureTime < earliestTime()) {
	_first = new Timer(futureTime, handler, _first);
    } else {
	Timer* before = _first;
	Timer* after = _first->next;
	while (after != NULL && futureTime > after->timerValue) {
	    before = after;
	    after = after->next;
	}
	before->next = new Timer(futureTime, handler, after);
    }
}

void TimerQueue::remove(IOHandler* handler) {
    Timer* before = NULL;
    Timer* doomed = _first;
    while (doomed != NULL && doomed->handler != handler) {
	before = doomed;
	doomed = doomed->next;
    }
    if (doomed != NULL) {
	if (before == NULL) {
	    _first = doomed->next;
	} else {
	    before->next = doomed->next;
	}
	delete doomed;
    }
}

void TimerQueue::expire(timeval curTime) {
    while (!isEmpty() && earliestTime() < curTime) {
	Timer* expired = _first;
	_first = _first->next;
	expired->handler->timerExpired(curTime.tv_sec, curTime.tv_usec);
	delete expired;
    }
}

/*
 * Interface to child process handling.
 */

struct Child {
    Child(pid_t pid, IOHandler* h, Child* n);

    pid_t	pid;		// process's PID
    int		status;		// wait status
    IOHandler*	handler;	// associated handler
    Child*	next;
};

class ChildQueue {
public:
    ChildQueue();
    virtual ~ChildQueue();

    fxBool isEmpty() const;
    fxBool isReady() const;

    void insert(pid_t, IOHandler*);
    void remove(IOHandler*);
    void notify();
    void setStatus(pid_t, int status);
private:
    Child* _first;		// queue head
    fxBool _ready;		// something is ready
};

Child::Child(pid_t p, IOHandler* h, Child* n)
{
    pid = p;
    status = -1;
    handler = h;
    next = n;
}

ChildQueue::ChildQueue()
{
    _first = NULL;
    _ready = FALSE;
}

ChildQueue::~ChildQueue() {
    Child* doomed = _first;
    while (doomed != NULL) {
	Child* next = doomed->next;
	delete doomed;
	doomed = next;
    }
}

inline fxBool ChildQueue::isEmpty() const { return _first == NULL; }
inline fxBool ChildQueue::isReady() const { return _ready; }

void ChildQueue::insert(pid_t p, IOHandler* handler) {
    /*
     * Place the entry at the end.  This is intentional
     * so that the work done in the notify method below
     * functions correctly when entries are added by
     * childStatus handlers.  On busy systems it may pay
     * to use a doubly linked list or to otherwise sort
     * the list to reduce searching, but since there
     * should never be more than 2x entries where x is
     * the number of ready modems on the system a singly
     * linked list should be ok.
     */
    Child** prev = &_first;
    while (*prev != NULL)
	prev = &(*prev)->next;
    *prev = new Child(p, handler, NULL);
}

void ChildQueue::remove(IOHandler* handler) {
    Child* before = NULL;
    Child* doomed = _first;
    while (doomed != NULL && doomed->handler != handler) {
	before = doomed;
	doomed = doomed->next;
    }
    if (doomed != NULL) {
	if (before == NULL) {
	    _first = doomed->next;
	} else {
	    before->next = doomed->next;
	}
	delete doomed;
    }
}

void ChildQueue::setStatus(pid_t p, int status) {
    for (Child* c = _first; c != NULL; c = c->next)
	if (c->pid == p) {
	    c->status = status;
	    _ready = TRUE;
	    break;
	}
}

void ChildQueue::notify() {
    Child** prev = &_first;
    Child* c;

    while ((c = *prev) != NULL) {
	if (c->status != -1) {
	    *prev = c->next;
	    c->handler->childStatus(c->pid, c->status);
	    delete c;
	} else
	    prev = &c->next;
    }
    _ready = FALSE;
}

Dispatcher::Dispatcher() {
    _nfds = 0;
    _rmask = new FdMask;
    _wmask = new FdMask;
    _emask = new FdMask;
    _rmaskready = new FdMask;
    _wmaskready = new FdMask;
    _emaskready = new FdMask;
    _max_fds = Sys::getOpenMax();
    _rtable = new IOHandler*[_max_fds];
    _wtable = new IOHandler*[_max_fds];
    _etable = new IOHandler*[_max_fds];
    _queue = new TimerQueue;
    _cqueue = new ChildQueue;
    for (int i = 0; i < _max_fds; i++) {
	_rtable[i] = NULL;
	_wtable[i] = NULL;
	_etable[i] = NULL;
    }
}

Dispatcher::~Dispatcher() {
    delete _rmask;
    delete _wmask;
    delete _emask;
    delete _rmaskready;
    delete _wmaskready;
    delete _emaskready;
    delete _rtable;
    delete _wtable;
    delete _etable;
    delete _queue;
    delete _cqueue;
}

Dispatcher& Dispatcher::instance() {
    if (_instance == NULL) {
	_instance = new Dispatcher;
    }
    return *_instance;
}

void Dispatcher::instance(Dispatcher* d) { _instance = d; }

IOHandler* Dispatcher::handler(int fd, DispatcherMask mask) const {
    if ((unsigned) fd >= _max_fds) {
	abort();
    }
    IOHandler* cur = NULL;
    if (mask == ReadMask) {
	cur = _rtable[fd];
    } else if (mask == WriteMask) {
	cur = _wtable[fd];
    } else if (mask == ExceptMask) {
	cur = _etable[fd];
    } else {
	abort();
    }
    return cur;
}

void Dispatcher::link(int fd, DispatcherMask mask, IOHandler* handler) {
    if ((unsigned) fd >= _max_fds) {
	abort();
    }
    attach(fd, mask, handler);
}

void Dispatcher::unlink(int fd) {
    if ((unsigned) fd >= _max_fds) {
	abort();
    }
    detach(fd);
}

void Dispatcher::attach(int fd, DispatcherMask mask, IOHandler* handler) {
    if (mask == ReadMask) {
	_rmask->setBit(fd);
	_rtable[fd] = handler;
    } else if (mask == WriteMask) {
	_wmask->setBit(fd);
	_wtable[fd] = handler;
    } else if (mask == ExceptMask) {
	_emask->setBit(fd);
	_etable[fd] = handler;
    } else {
	abort();
    }
    if (_nfds < fd+1) {
	_nfds = fd+1;
    }
}

void Dispatcher::detach(int fd) {
    _rmask->clrBit(fd);
    _rtable[fd] = NULL;
    _wmask->clrBit(fd);
    _wtable[fd] = NULL;
    _emask->clrBit(fd);
    _etable[fd] = NULL;
    if (_nfds == fd+1) {
	while (_nfds > 0 && _rtable[_nfds-1] == NULL &&
	       _wtable[_nfds-1] == NULL && _etable[_nfds-1] == NULL
	) {
	    _nfds--;
	}
    }
}

void Dispatcher::startTimer(long sec, long usec, IOHandler* handler) {
    timeval deltaTime;
    deltaTime.tv_sec = sec;
    deltaTime.tv_usec = usec;
    _queue->insert(TimerQueue::currentTime() + deltaTime, handler);
}

void Dispatcher::stopTimer(IOHandler* handler) {
    _queue->remove(handler);
}

void Dispatcher::startChild(pid_t pid, IOHandler* handler) {
    _cqueue->insert(pid, handler);
}

void Dispatcher::stopChild(IOHandler* handler) {
    _cqueue->remove(handler);
}

fxBool Dispatcher::setReady(int fd, DispatcherMask mask) {
    if (handler(fd, mask) == NULL) {
	return FALSE;
    }
    if (mask == ReadMask) {
	_rmaskready->setBit(fd);
    } else if (mask == WriteMask) {
	_wmaskready->setBit(fd);
    } else if (mask == ExceptMask) {
	_emaskready->setBit(fd);
    } else {
	return FALSE;
    }
    return TRUE;
}

void Dispatcher::dispatch() {
    dispatch(NULL);
}

fxBool Dispatcher::dispatch(long& sec, long& usec) {
    timeval howlong;
    timeval prevTime;
    timeval elapsedTime;

    howlong.tv_sec = sec;
    howlong.tv_usec = usec;
    prevTime = TimerQueue::currentTime();

    fxBool success = dispatch(&howlong);

    elapsedTime = TimerQueue::currentTime() - prevTime;
    if (howlong > elapsedTime) {
	howlong = howlong - elapsedTime;
    } else {
	howlong = TimerQueue::zeroTime(); /* Used all of timeout */
    }

    sec = howlong.tv_sec;
    usec = howlong.tv_usec;
    return success;
}

fxBool Dispatcher::dispatch(timeval* howlong) {
    FdMask rmaskret;
    FdMask wmaskret;
    FdMask emaskret;
    int nfound;

    if (anyReady()) {
	nfound = fillInReady(rmaskret, wmaskret, emaskret);
    } else {
	nfound = waitFor(rmaskret, wmaskret, emaskret, howlong);
    }

    notify(nfound, rmaskret, wmaskret, emaskret);

    return (nfound != 0);
}

fxBool Dispatcher::anyReady() const {
    if (!_cqueue->isEmpty()) {
	Dispatcher::sigCLD(0);		// poll for pending children
	return _cqueue->isReady();
    }
    return
	_rmaskready->anySet() || _wmaskready->anySet() || _emaskready->anySet();
}

int Dispatcher::fillInReady(
    FdMask& rmaskret, FdMask& wmaskret, FdMask& emaskret
) {
    rmaskret = *_rmaskready;
    wmaskret = *_wmaskready;
    emaskret = *_emaskready;
    _rmaskready->zero();
    _wmaskready->zero();
    _emaskready->zero();
    return rmaskret.numSet() + wmaskret.numSet() + emaskret.numSet();
}

void Dispatcher::sigCLD(int)
{
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
	Dispatcher::instance()._cqueue->setStatus(pid, status);
}

#ifndef SA_INTERRUPT
#define	SA_INTERRUPT	0
#endif

int Dispatcher::waitFor(
    FdMask& rmaskret, FdMask& wmaskret, FdMask& emaskret, timeval* howlong
) {
    int nfound;
#if defined(SV_INTERRUPT)		// BSD-style
    static struct sigvec sv, osv;
#elif defined(SA_NOCLDSTOP)		// POSIX
    static struct sigaction sa, osa;
#else					// System V-style
    void (*osig)();
#endif

    if (!_cqueue->isEmpty()) {
#if defined(SV_INTERRUPT)		// BSD-style
	sv.sv_handler = fxSIGVECHANDLER(&Dispatcher::sigCLD);
	sv.sv_flags = SV_INTERRUPT;
	sigvec(SIGCHLD, &sv, &osv);
#elif defined(SA_NOCLDSTOP)		// POSIX
	sa.sa_handler = fxSIGACTIONHANDLER(&Dispatcher::sigCLD);
	sa.sa_flags = SA_INTERRUPT;
	sigaction(SIGCLD, &sa, &osa);
#else					// System V-style
	osig = (void (*)())signal(SIGCLD, fxSIGHANDLER(&Dispatcher::sigCLD));
#endif
    }
    /*
     * If SIGCLD is pending then it may be delivered on
     * exiting from the kernel after the above sig* call;
     * if so then we don't want to block in the select.
     */
    if (!_cqueue->isReady()) {
	do {
	    rmaskret = *_rmask;
	    wmaskret = *_wmask;
	    emaskret = *_emask;
	    howlong = calculateTimeout(howlong);

#if CONFIG_BADSELECTPROTO
	    nfound = select(_nfds,
		(int*) &rmaskret, (int*) &wmaskret, (int*) &emaskret, howlong);
#else
	    nfound = select(_nfds, &rmaskret, &wmaskret, &emaskret, howlong);
#endif
#ifdef SGISELECTBUG
	    // XXX hack to deal with IRIX 5.2 FIFO+select bug
	    if (wmaskret.anySet()) {
		for (int i = 0; i < _nfds; i++)
		    if (wmaskret.isSet(i) && !_wmask->isSet(i)) {
			wmaskret.clrBit(i);
		    }
	    }
	    if (emaskret.anySet()) {
		for (int i = 0; i < _nfds; i++)
		    if (emaskret.isSet(i) && !_emask->isSet(i)) {
			emaskret.clrBit(i);
		    }
	    }
#endif
	    howlong = calculateTimeout(howlong);
	} while (nfound < 0 && !handleError());
    }
    if (!_cqueue->isEmpty()) {
#if defined(SV_INTERRUPT)		// BSD-style
	sigvec(SIGCHLD, &osv, (struct sigvec*) 0);
#elif defined(SA_NOCLDSTOP)		// POSIX
	sigaction(SIGCLD, &osa, (struct sigaction*) 0);
#else					// System V-style
	(void) signal(SIGCLD, fxSIGHANDLER(osig));
#endif
    }

    return nfound;			// timed out or input available
}

void Dispatcher::notify(
    int nfound, FdMask& rmaskret, FdMask& wmaskret, FdMask& emaskret
) {
    for (int i = 0; i < _nfds && nfound > 0; i++) {
	if (rmaskret.isSet(i)) {
	    int status = _rtable[i]->inputReady(i);
	    if (status < 0) {
		detach(i);
	    } else if (status > 0) {
		_rmaskready->setBit(i);
	    }
	    nfound--;
	}
	if (wmaskret.isSet(i)) {
	    int status = _wtable[i]->outputReady(i);
	    if (status < 0) {
		detach(i);
	    } else if (status > 0) {
		_wmaskready->setBit(i);
	    }
	    nfound--;
	}
	if (emaskret.isSet(i)) {
	    int status = _etable[i]->exceptionRaised(i);
	    if (status < 0) {
		detach(i);
	    } else if (status > 0) {
		_emaskready->setBit(i);
	    }
	    nfound--;
	}
    }

    if (!_queue->isEmpty()) {
	_queue->expire(TimerQueue::currentTime());
    }
    if (_cqueue->isReady()) {
	_cqueue->notify();
    }
}

timeval* Dispatcher::calculateTimeout(timeval* howlong) const {
    static timeval timeout;

    if (!_queue->isEmpty()) {
	timeval curTime;

	curTime = TimerQueue::currentTime();
	if (_queue->earliestTime() > curTime) {
	    timeout = _queue->earliestTime() - curTime;
	    if (howlong == NULL || *howlong > timeout) {
		howlong = &timeout;
	    }
	} else {
	    timeout = TimerQueue::zeroTime();
	    howlong = &timeout;
	}
    }
    return howlong;
}

extern void fxFatal(const char* fmt, ...);

fxBool Dispatcher::handleError() {
    switch (errno) {
    case EBADF:
	checkConnections();
	break;
    case EINTR:
	if (_cqueue->isReady())
	    return TRUE;
	break;
    default:
	fxFatal("Dispatcher: select: %s", strerror(errno));
	/*NOTREACHED*/
    }
    return FALSE;			// retry select
}

void Dispatcher::checkConnections() {
    FdMask rmask;
    timeval poll = TimerQueue::zeroTime();

    for (int fd = 0; fd < _nfds; fd++) {
	if (_rtable[fd] != NULL) {
	    rmask.setBit(fd);
#if CONFIG_BADSELECTPROTO
          if (select(fd+1, (int*)&rmask, NULL, NULL, &poll) < 0) {
#else
	    if (select(fd+1, &rmask, NULL, NULL, &poll) < 0) {
#endif
		detach(fd);
	    }
	    rmask.clrBit(fd);
	}
    }
}
