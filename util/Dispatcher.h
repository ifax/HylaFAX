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

/*
 * Wait on multiple file descriptors until a condition occurs.
 */

#ifndef dp_dispatcher_h
#define dp_dispatcher_h

#include "Types.h"

class IOHandler;
class TimerQueue;
class ChildQueue;
struct timeval;

class Dispatcher {
public:
    enum DispatcherMask {
	ReadMask,
	WriteMask,
	ExceptMask
    };

    Dispatcher();
    virtual ~Dispatcher();

    virtual void link(int fd, DispatcherMask, IOHandler*);
    virtual IOHandler* handler(int fd, DispatcherMask) const;
    virtual void unlink(int fd);

    virtual void startTimer(long sec, long usec, IOHandler*);
    virtual void stopTimer(IOHandler*);

    virtual void startChild(pid_t pid, IOHandler*);
    virtual void stopChild(IOHandler*);

    virtual fxBool setReady(int fd, DispatcherMask);
    virtual void dispatch();
    virtual fxBool dispatch(long& sec, long& usec);

    static Dispatcher& instance();
    static void instance(Dispatcher*);
protected:
    virtual void attach(int fd, DispatcherMask, IOHandler*);
    virtual void detach(int fd);
    virtual fxBool dispatch(timeval*);
    virtual fxBool anyReady() const;
    virtual int fillInReady(fd_set&, fd_set&, fd_set&);
    virtual int waitFor(fd_set&, fd_set&, fd_set&, timeval*);
    virtual void notify(int, fd_set&, fd_set&, fd_set&);
    virtual timeval* calculateTimeout(timeval*) const;
    virtual fxBool handleError();
    virtual void checkConnections();
protected:
    int	_nfds;
    int _max_fds;
    fd_set _rmask;
    fd_set _wmask;
    fd_set _emask;
    fd_set _rmaskready;
    fd_set _wmaskready;
    fd_set _emaskready;
    IOHandler** _rtable;
    IOHandler** _wtable;
    IOHandler** _etable;
    TimerQueue* _queue;
    ChildQueue* _cqueue;
private:
    static Dispatcher* _instance;

    static void sigCLD(int);
private:
    /* deny access since member-wise won't work */
    Dispatcher(const Dispatcher&);
    Dispatcher& operator =(const Dispatcher&);
};

#endif
