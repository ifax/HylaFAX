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
#ifndef _DestControl_
#define	_DestControl_
/*
 * Destination Controls.
 */
#include "RE.h"
#include "TimeOfDay.h"
#include "Array.h"

/*
 * Destination controls are defined by sets of parameters
 * and a regular expression.  If the canonical destination
 * phone number matches the regex, then associated parameters
 * are used.
 */
class DestControlInfo {
private:
    RE	pattern;		// destination pattern
    u_long	defined;		// parameters that were defined
    u_int	maxConcurrentJobs;	// max number of parallel calls
    u_int	maxSendPages;		// max pages in a send job
    u_int	maxDials;		// max times to dial the phone
    u_int	maxTries;		// max transmit attempts
    fxStr	rejectNotice;		// if set, reject w/ this notice
    fxStr	modem;			// if set, try with it
    TimeOfDay	tod;			// time of day restrictions
    fxStr	args;			// arguments for subprocesses

    // default returned on no match
    static const DestControlInfo defControlInfo;

    friend class DestControl;
public:
    DestControlInfo(const char* regex);
    DestControlInfo();
    DestControlInfo(const DestControlInfo& other);
    ~DestControlInfo();

    int compare(const DestControlInfo*) const;
    void parseEntry(const char* tag, const char* value, bool quoted);

    u_int getMaxConcurrentJobs() const;
    u_int getMaxSendPages() const;
    u_int getMaxDials() const;
    u_int getMaxTries() const;
    const fxStr& getRejectNotice() const;
    const fxStr& getModem() const;
    time_t nextTimeToSend(time_t) const;
    const fxStr& getArgs() const;
};
inline const fxStr& DestControlInfo::getArgs() const	{ return args; }

fxDECLARE_ObjArray(DestControlInfoArray, DestControlInfo)

/*
 * Destination control information database.
 */
class DestControl {
private:
    fxStr	filename;		// database filename
    time_t	lastModTime;		// last modification timestamp
    u_int	lineno;			// line number while parsing
    DestControlInfoArray info;		// control information

    void	readContents();
    bool	parseEntry(FILE* fp);
    bool	readLine(FILE* fp, char line[], u_int cc);
    void	skipEntry(FILE*, char line[], u_int cc);
    void	parseError(const char* fmt ...);
public:
    DestControl();
    virtual ~DestControl();

    void setFilename(const char* filename);

    const DestControlInfo& operator[](const fxStr&);
};
#endif /* _DestControl_ */
