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

bool operator>(timeval src1, timeval src2) {
    if (src1.tv_sec > src2.tv_sec) {
	return true;
    } else if (src1.tv_sec == src2.tv_sec && src1.tv_usec > src2.tv_usec) {
	return true;
    } else {
	return false;
    }
}

bool operator<(timeval src1, timeval src2) {
    if (src1.tv_sec < src2.tv_sec) {
	return true;
    } else if (src1.tv_sec == src2.tv_sec && src1.tv_usec < src2.tv_usec) {
	return true;
    } else {
	return false;
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

    bool isEmpty() const;
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

inline bool TimerQueue::isEmpty() const {
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

    bool isEmpty() const;
    bool isReady() const;

    void insert(pid_t, IOHandler*);
    void remove(IOHandler*);
    void notify();
    void setStatus(pid_t, int status);
private:
    Child* _first;		// queue head
    bool _ready;		// something is ready
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
    _ready = false;
}

ChildQueue::~ChildQueue() {
    Child* doomed = _first;
    while (doomed != NULL) {
	Child* next = doomed->next;
	delete doomed;
	doomed = next;
    }
}

inline bool ChildQueue::isEmpty() const { return _first == NULL; }
inline bool ChildQueue::isReady() const { return _ready; }

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
	    _ready = true;
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
    _ready = false;
}

Dispatcher::Dispatcher() {
    _nfds = 0;
    FD_ZERO(&_rmask);
    FD_ZERO(&_wmask);
    FD_ZERO(&_emask);
    FD_ZERO(&_rmaskready);
    FD_ZERO(&_wmaskready);
    FD_ZERO(&_emaskready);
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
        FD_SET(fd, &_rmask);
        _rtable[fd] = handler;
    } else if (mask == WriteMask) {
        FD_SET(fd, &_wmask);
        _wtable[fd] = handler;
    } else if (mask == ExceptMask) {
        FD_SET(fd, &_emask);
        _etable[fd] = handler;
    } else {
        abort();
    }
    if (_nfds < fd+1) {
	_nfds = fd+1;
    }
}

void Dispatcher::detach(int fd) {
    FD_CLR(fd, &_rmask);
    _rtable[fd] = NULL;
    FD_CLR(fd, &_wmask);
    _wtable[fd] = NULL;
    FD_CLR(fd, &_emask);
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

bool Dispatcher::setReady(int fd, DispatcherMask mask) {
    if (handler(fd, mask) == NULL) {
        return false;
    }
    if (mask == ReadMask) {
        FD_SET(fd, &_rmaskready);
    } else if (mask == WriteMask) {
        FD_SET(fd, &_wmaskready);
    } else if (mask == ExceptMask) {
        FD_SET(fd, &_emaskready);
    } else {
        return false;
    }
    return true;
}

void Dispatcher::dispatch() {
    dispatch(NULL);
}

bool Dispatcher::dispatch(long& sec, long& usec) {
    timeval howlong;
    timeval prevTime;
    timeval elapsedTime;

    howlong.tv_sec = sec;
    howlong.tv_usec = usec;
    prevTime = TimerQueue::currentTime();

    bool success = dispatch(&howlong);

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

bool Dispatcher::dispatch(timeval* howlong) {
    fd_set rmask;
    fd_set wmask;
    fd_set emask;

    FD_ZERO(&rmask);
    FD_ZERO(&wmask);
    FD_ZERO(&emask);

    int nfound = (anyReady()) ?
        fillInReady(rmask, wmask, emask)
        : waitFor(rmask, wmask, emask, howlong);

    notify(nfound, rmask, wmask, emask);

    return (nfound != 0);
}

bool Dispatcher::anyReady() const {
    if (!_cqueue->isEmpty()) {
        Dispatcher::sigCLD(0);		// poll for pending children
        return _cqueue->isReady();
    }
    for (int i = 0; i < _nfds; i++) {
        if (FD_ISSET(i, &_rmaskready) ||
                FD_ISSET(i, &_wmaskready) || FD_ISSET(i, &_emaskready)) {
            return true;
	    }
    }
    return false;
}

int Dispatcher::fillInReady(
    fd_set& rmaskret, fd_set& wmaskret, fd_set& emaskret) {

    //note - this is an array copy, not a pointer assignment
    rmaskret = _rmaskready;
    wmaskret = _wmaskready;
    emaskret = _emaskready;
    FD_ZERO(&_rmaskready);
    FD_ZERO(&_wmaskready);
    FD_ZERO(&_emaskready);
    int n = 0;
    for (int i = 0; i < _nfds; i++) {
        if (FD_ISSET(i, &rmaskret)) n++;
        if (FD_ISSET(i, &wmaskret)) n++;
        if (FD_ISSET(i, &emaskret)) n++;
    }
    return n;
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
    fd_set& rmaskret, fd_set& wmaskret, fd_set& emaskret, timeval* howlong
) {
    int nfound;
#if defined(SA_NOCLDSTOP)		// POSIX
    static struct sigaction sa, osa;
#elif defined(SV_INTERRUPT)		// BSD-style
    static struct sigvec sv, osv;
#else					// System V-style
    void (*osig)();
#endif

    if (!_cqueue->isEmpty()) {
#if defined(SA_NOCLDSTOP)		// POSIX
	sa.sa_handler = fxSIGACTIONHANDLER(&Dispatcher::sigCLD);
	sa.sa_flags = SA_INTERRUPT;
	sigaction(SIGCLD, &sa, &osa);
#elif defined(SV_INTERRUPT)		// BSD-style
	sv.sv_handler = fxSIGVECHANDLER(&Dispatcher::sigCLD);
	sv.sv_flags = SV_INTERRUPT;
	sigvec(SIGCHLD, &sv, &osv);
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
        //note - this is an array copy, not a pointer assignment
        rmaskret = _rmask;
        wmaskret = _wmask;
        emaskret = _emask;
        howlong = calculateTimeout(howlong);

#if CONFIG_BADSELECTPROTO
	    nfound = select(_nfds,
		(int*) &rmaskret, (int*) &wmaskret, (int*) &emaskret, howlong);
#else
	    nfound = select(_nfds, &rmaskret, &wmaskret, &emaskret, howlong);
#endif
#ifdef SGISELECTBUG
        // XXX hack to deal with IRIX 5.2 FIFO+select bug
        for (int i = 0; i < _nfds; i++) {
            if (FD_ISSET(i, &wmaskret) && !FD_ISSET(i, &_wmask)) {
                FD_CLR(i, &wmaskret);
            } 
        }
        for (int i = 0; i < _nfds; i++) {
            if (FD_ISSET(i, &emaskret)  && !FD_ISSET(i, &_emask)) {
                FD_CLR(i, &emaskret);
            }
        }
#endif
	    howlong = calculateTimeout(howlong);
	} while (nfound < 0 && !handleError());
    }
    if (!_cqueue->isEmpty()) {
#if defined(SA_NOCLDSTOP)		// POSIX
	sigaction(SIGCLD, &osa, (struct sigaction*) 0);
#elif defined(SV_INTERRUPT)		// BSD-style
	sigvec(SIGCHLD, &osv, (struct sigvec*) 0);
#else					// System V-style
	(void) signal(SIGCLD, fxSIGHANDLER(osig));
#endif
    }

    return nfound;			// timed out or input available
}

void Dispatcher::notify(int nfound,
        fd_set& rmaskret, fd_set& wmaskret, fd_set& emaskret) {
    for (int i = 0; i < _nfds && nfound > 0; i++) {
        if (FD_ISSET(i, &rmaskret)) {
            int status = _rtable[i]->inputReady(i);
	        if (status < 0) {
		        detach(i);
	        } else if (status > 0) {
        	    FD_SET(i, &_rmaskready);
            }
            nfound--;
        }
        if (FD_ISSET(i, &wmaskret)) {
            int status = _wtable[i]->outputReady(i);
            if (status < 0) {
                detach(i);
            } else if (status > 0) {
       	        FD_SET(i, &_wmaskready);
            }
            nfound--;
        }
        if (FD_ISSET(i, &emaskret)) {
            int status = _etable[i]->exceptionRaised(i);
            if (status < 0) {
                detach(i);
            } else if (status > 0) {
       	        FD_SET(i, &_emaskready);
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

bool Dispatcher::handleError() {
    switch (errno) {
    case EBADF:
	checkConnections();
	break;
    case EINTR:
	if (_cqueue->isReady())
	    return true;
	break;
    default:
	fxFatal("Dispatcher: select: %s", strerror(errno));
	/*NOTREACHED*/
    }
    return false;			// retry select
}

void Dispatcher::checkConnections() {
    fd_set rmask;
    FD_ZERO(&rmask);
    timeval poll = TimerQueue::zeroTime();

    for (int fd = 0; fd < _nfds; fd++) {
        if (_rtable[fd] != NULL) {
            FD_SET(fd, &rmask);
#if CONFIG_BADSELECTPROTO
            if (select(fd+1, (int*)&rmask, NULL, NULL, &poll) < 0) {
#else
            if (select(fd+1, &rmask, NULL, NULL, &poll) < 0) {
#endif
    	        detach(fd);
            }
            FD_CLR(fd, &rmask);
        }
    }
}
