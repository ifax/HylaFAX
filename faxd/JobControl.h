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
#ifndef _JobControl_
#define	_JobControl_
/*
 * Destination Controls.
 */
#include "FaxConfig.h"
#include "Str.h"
#include "TimeOfDay.h"
#include "Array.h"

/*
 * Destination controls are defined by sets of parameters
 * and a regular expression.  If the canonical destination
 * phone number matches the regex, then associated parameters
 * are used.
 */
class JobControlInfo
  : public FaxConfig
{
private:
    u_long	defined;		// parameters that were defined
    u_int	maxConcurrentCalls;	// max number of parallel calls
    u_int	maxSendPages;		// max pages in a send job
    u_int	maxDials;		// max times to dial the phone
    u_int	maxTries;		// max transmit attempts
    fxStr	rejectNotice;		// if set, reject w/ this notice
    fxStr	modem;			// if set, try with it
    TimeOfDay	tod;			// time of day restrictions
    int		usexvres;		// use extended resolution
    u_int	vres;			// use extended resolution
    fxStr	args;			// arguments for subprocesses

    // default returned on no match
    static const JobControlInfo defControlInfo;

    friend class JobControl;
public:
    JobControlInfo();
    JobControlInfo(const fxStr& buffer);
    JobControlInfo(const JobControlInfo& other);
    ~JobControlInfo();


    bool isCompatible (const JobControlInfo& other) const;

    void parseEntry(const char* tag, const char* value, bool quoted);

    u_int getMaxConcurrentCalls() const;
    u_int getMaxSendPages() const;
    u_int getMaxDials() const;
    u_int getMaxTries() const;
    const fxStr& getRejectNotice() const;
    const fxStr& getModem() const;
    time_t nextTimeToSend(time_t) const;
    int getUseXVRes() const;
    u_int getVRes() const;
    const fxStr& getArgs() const;

    virtual bool setConfigItem(const char*, const char*);
    virtual void configError(const char*, ...);
    virtual void configTrace(const char*, ...);
};

#endif /* _JobControl_ */
