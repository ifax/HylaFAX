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
#ifndef _faxApp_
#define	_faxApp_
/*
 * HylaFAX Application Support.
 */
#include "Str.h"
#include "Syslog.h"
#include "Dispatcher.h"
#include <stdarg.h>

class faxApp : public Syslog {
private:
    static fxStr getopts;		// main arguments

    bool	running;		// server running
    int		faxqfifo;		// cached descriptor to faxq
protected:
    int		openFIFO(const char* fifoName, int mode,
		    bool okToExist = false);
public:
    faxApp();
    virtual ~faxApp();

    static const fxStr fifoName;

    static void setupPermissions(void);
    static void setRealIDs();
    static void detachFromTTY(void);
    static void fatal(const char* fmt ...);

    virtual void initialize(int argc, char** argv);
    virtual void open(void);
    virtual void close(void);

    bool isRunning(void) const;

    virtual void openFIFOs(void);
    virtual void closeFIFOs(void);
    virtual int FIFOInput(int);
    virtual void FIFOMessage(const char* mesage);

    bool sendModemStatus(const char* devid, const char* fmt ...);
    bool sendJobStatus(const char* jobid, const char* fmt ...);
    bool sendRecvStatus(const char* devid, const char* fmt ...);
    bool sendQueuer(const char* fmt ...);
    bool vsendQueuer(const char* fmt, va_list ap);

    static void setOpts(const char*);
    static const fxStr& getOpts(void);

    static fxStr idToDev(const fxStr& id);
    static fxStr devToID(const fxStr& dev);

    static const fxStr quote;
    static const fxStr enquote;

    bool runCmd(const char* cmd, bool changeIDs = false, IOHandler* waiter = NULL);
};
inline bool faxApp::isRunning(void) const	{ return running; }

class GetoptIter {
private:
    const fxStr& opts;
    int		argc;
    char**	argv;
    int		c;
public:
    GetoptIter(int ac, char** av, const fxStr& s);
    ~GetoptIter();

    void operator++();
    void operator++(int);
    int option() const;
    bool notDone() const;
    const char* optArg() const;
    const char* getArg();
    const char* nextArg();
};
inline int GetoptIter::option() const		{ return c; }
inline bool GetoptIter::notDone() const	{ return c != -1; }

extern	const char* fmtTime(time_t);
#endif
