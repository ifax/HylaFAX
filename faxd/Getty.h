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
#ifndef _GETTY_
#define	_GETTY_

/*
 * Getty support base class.  Support for
 * System V, BSD, etc. are derived from this. 
 */
#include "Str.h"

const int GETTY_MAXARGS		= 64;	// max args passed to getty

class Getty {
private:
    fxStr	getty;			// subprogram pathname
    pid_t	pid;			// pid of getty/login process
    fxStr	line;			// device name
    fxStr	speed;			// line speed
    char*	argv[GETTY_MAXARGS];	// argv passed to getty
    fxStr	argbuf;			// stash for argv strings
    fxStr	tzVar;			// TZ environment variable
    fxStr	langVar;		// LANG environment variable
protected:
    Getty(const char* program, const fxStr& line, const fxStr& speed);

    void addEnvVar(int& envc, char* env[], fxStr& var);
    virtual void setupSession(int modemFd);
    virtual void fatal(const char* fmt ...);
public:
    virtual ~Getty();

    void setupArgv(const char* args);	// setup arguments for getty process
					// run getty process
    virtual void run(int fd, bool parentIsInit);
    virtual bool wait(int& status, bool block = false);
    virtual void hangup();		// cleanup state after hangup

    pid_t getPID() const;		// getty pid
    void setPID(pid_t);

    const char* getLine() const;	// return tty filename
    fxStr getCmdLine() const;		// return command line args
};

extern Getty* OSnewGetty(const fxStr& dev, const fxStr& speed);
extern Getty* OSnewVGetty(const fxStr& dev, const fxStr& speed);
extern Getty* OSnewEGetty(const fxStr& dev, const fxStr& speed);
#endif /* _GETTY_ */
