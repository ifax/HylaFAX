/*	$Id$ */
/*
 * Copyright (c) 1995-1996 Sam Leffler
 * Copyright (c) 1995-1996 Silicon Graphics, Inc.
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
#ifndef _SNPPJob_
#define	_SNPPJob_

#include "Str.h"
#include "Array.h"

class SNPPClient;

typedef unsigned int PageNotify;

class SNPPJob : public fxObj {
public:
    enum {		// email notification control
	no_notice,	// no messages
	when_done,	// when job is completed
	when_requeued	// when job is requeued or completed
    };
private:
    fxStr	jobid;			// job ID
    PageNotify	notify;			// job notification control
    fxStr	mailbox;		// mail address for notification

    fxStr	pin;			// pager identification number
    fxStr	passwd;			// password (opt)
    fxStr	subject;		// message subject

    time_t	holdTime;		// job's time to be sent
    u_int	retryTime;		// retry time for failures (secs)
    u_int	maxRetries;		// max number times to try send
    u_int	maxDials;		// max number times to dial telephone
    u_int	serviceLevel;		// delivery service type
    bool	queued;			// queued delivery

    u_int parseTime(const char* v);
public:
    SNPPJob();
    SNPPJob(const SNPPJob& other);
    virtual ~SNPPJob();

    virtual bool createJob(SNPPClient& client, fxStr& emsg);

    const fxStr& getJobID(void) const;

    /*
     * Job notification.
     */
    bool setNotification(const char*);// email notification
    void setNotification(PageNotify);
    PageNotify getNotification() const;
    void setMailbox(const char*);	// email notification address
    const fxStr& getMailbox() const;

    /*
     * Job scheduling controls.
     */
    void setQueued(bool);		// queued or synchronous wait
    bool getQueued() const;
    void setRetryTime(u_int);		// job retry time (seconds)
    void setRetryTime(const char* v);
    u_int getRetryTime() const;
					// time to initiate tranmission
    bool setHoldTime(const char*, fxStr& emsg);
    void setHoldTime(u_int);
    u_int getHoldTime() const;
    void setMaxTries(u_int);		// maximum attempts to send
    u_int getMaxTries() const;
    void setMaxDials(u_int);		// maximum phone call attempts
    u_int getMaxDials() const;

    /*
     * Page delivery information.
     */
    void setPIN(const char*);		// pager identification number
    const fxStr& getPIN() const;
    void setPassword(const char*);	// transmit password
    const fxStr& getPasswd() const;
    void setSubject(const char*);	// message subject
    const fxStr& getSubject() const;
    void setServiceLevel(u_int);	// service delivery level
    u_int getServiceLevel() const;
};

fxDECLARE_ObjArray(SNPPJobArray, SNPPJob)

inline const fxStr& SNPPJob::getJobID(void) const	{ return jobid; }
inline PageNotify SNPPJob::getNotification() const	{ return notify; }
inline const fxStr& SNPPJob::getMailbox() const		{ return mailbox; }
inline u_int SNPPJob::getRetryTime() const		{ return retryTime; }
inline u_int SNPPJob::getHoldTime() const		{ return holdTime; }
inline u_int SNPPJob::getMaxTries() const		{ return maxRetries; }
inline u_int SNPPJob::getMaxDials() const		{ return maxDials; }
inline const fxStr& SNPPJob::getPIN() const		{ return pin; }
inline const fxStr& SNPPJob::getPasswd() const		{ return passwd; }
inline bool SNPPJob::getQueued() const		{ return queued; }
inline const fxStr& SNPPJob::getSubject(void) const	{ return subject; }
inline u_int SNPPJob::getServiceLevel() const		{ return serviceLevel; }
#endif /* _SNPPJob_ */
