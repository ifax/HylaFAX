/*	$Id$ */
/*
 * Copyright (c) 1993-1996 Sam Leffler
 * Copyright (c) 1993-1996 Silicon Graphics, Inc.
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
#ifndef _SendFaxJob_
#define	_SendFaxJob_

#include "Str.h"
#include "Array.h"

class SendFaxClient;

typedef unsigned int FaxNotify;
typedef	unsigned int FaxPageChop;

class SendFaxJob : public fxObj {
public:
    enum {		// email notification control
	no_notice,	// no messages
	when_done,	// when job is completed
	when_requeued	// when job is requeued or completed
    };
    enum {		// page chopping control
	chop_default,	// default server setting
	chop_none,	// chop no pages
	chop_all,	// chop all pages with trailing white
	chop_last	// chop last page in each document
    };

    // NB: the SFJ_ prefixes workaround a bug in the AIX xlC compiler
    struct SFJ_stringtag {
	const char*	 name;
	fxStr SendFaxJob::* p;
	const char*	 def;		// NULL is shorthand for ""
    };
    struct SFJ_numbertag {
	const char*	 name;
	u_int SendFaxJob::*p;
	u_int		 def;
    };
    struct SFJ_floattag {
	const char*	 name;
	float SendFaxJob::*p;
	float		 def;
    };
private:
    fxStr	jobid;			// job ID
    fxStr	groupid;		// job-group ID
    fxStr	jobtag;			// user-specified job identifier
    FaxNotify	notify;			// job notification control
    fxStr	mailbox;		// mail address for notification

    fxStr	number;			// fax phone number/dialing string
    fxStr	subaddr;		// subaddress for routing (opt)
    fxStr	passwd;			// password (opt)
    fxStr	external;		// displayable version of number (opt)

    bool	autoCover;		// if true, auto-generate cover sheet
    bool	coverIsTemp;		// if true, remove coverFile on exit
    fxStr	coverFile;		// cover page file
    fxStr	coverTemplate;		// cover page template file
    fxStr	name;			// receipient for cover page
    fxStr	voicenumber;		// rec. voice number for cover page
    fxStr	location;		// rec. physical location for cover page
    fxStr	company;		// rec. company identity for cover page
    fxStr	comments;		// comments for cover page
    fxStr	regarding;		// regarding info for cover page

    bool	sendTagLine;		// if true, use custom tagline format
    fxStr	killTime;		// job's time to be killed
    fxStr	sendTime;		// job's time to be sent
    u_int	retryTime;		// retry time for failures (secs)
    fxStr	tagline;		// tag line format string
    float	hres, vres;		// sending resolution (dpi)
    float	pageWidth;		// sending page width (mm)
    float	pageLength;		// sending page length (mm)
    fxStr	pageSize;		// arg to pass to subprocesses
    int		totalPages;		// counted pages (for cover sheet)
    u_int	maxRetries;		// max number times to try send
    u_int	maxDials;		// max number times to dial telephone
    u_int	priority;		// scheduling priority
    u_int	minsp;			// minimum transmit speed
    u_int	desiredbr;		// upper bound on transmit speed
    u_int	desiredst;		// min-scanline-time to use
    u_int	desiredec;		// enable/disable use of ECM
    u_int	desireddf;		// data format to use
    FaxPageChop	pagechop;		// page chop handling
    float	chopthreshold;		// white space threshold for chopping

    static const SFJ_stringtag strings[];
    static const SFJ_numbertag numbers[];
    static const SFJ_floattag floats[];

    int getSpeed(const char* value) const;
public:
    SendFaxJob();
    SendFaxJob(const SendFaxJob& other);
    virtual ~SendFaxJob();

    virtual bool createJob(SendFaxClient& client, fxStr& emsg);

    virtual void setupConfig();
    virtual bool setConfigItem(const char* tag, const char* value);

    const fxStr& getJobID(void) const;
    const fxStr& getGroupID(void) const;

    /*
     * Job notification.
     */
    bool setNotification(const char*);// email notification
    void setNotification(FaxNotify);
    FaxNotify getNotification() const;
    void setMailbox(const char*);	// email notification address
    const fxStr& getMailbox() const;
    void setJobTag(const char*);	// job identifier string
    const fxStr& getJobTag() const;

    /*
     * Job scheduling controls.
     */
    void setRetryTime(u_int);		// job retry time (seconds)
    void setRetryTime(const char* v);
    u_int getRetryTime() const;
    void setKillTime(const char*);	// job kill time
    const fxStr& getKillTime() const;
    void setSendTime(const char*);	// time to initiate tranmission
    const fxStr& getSendTime() const;
    void setMaxRetries(u_int);		// maximum attempts to send
    u_int getMaxRetries() const;
    void setMaxDials(u_int);		// maximum phone call attempts
    u_int getMaxDials() const;
    void setPriority(int);		// job scheduling priority
    void setPriority(const char*);
    int getPriority() const;

    /*
     * Job delivery/envelope support.
     *
     * Note that the delivery subaddress and password are
     * only used when the receiving station supports this
     * optional part of the protocol.  The external form
     * of the dialing string is used in generating cover
     * pages (see below) and when job status is displayed.
     */
    void setDialString(const char*);	// dialing string/phone number
    const fxStr& getDialString() const;
    void setSubAddress(const char*);	// destination subaddress for routing
    const fxStr& getSubAddress() const;
    void setPassword(const char*);	// transmit password
    const fxStr& getPasswd() const;
    void setExternalNumber(const char*);// displayable version of fax number
    const fxStr& getExternalNumber() const;

    /*
     * Cover page support.
     *
     * If cover pages are automatically generated then the
     * template file and cover page information is passed
     * to a cover page generation procedure that creates the
     * cover page file.  Otherwise cover pages can be
     * suppressed or an application-specified file can be
     * specified instead.
     */
    void setAutoCoverPage(bool);	// auto-generate cover page
    bool getAutoCoverPage() const;
					// cover page file
    void setCoverPageFile(const char*, bool removeOnExit);
    const fxStr& getCoverPageFile() const;
    void setCoverTemplate(const char*);	// cover page template file
    const fxStr& getCoverTemplate() const;
    void setCoverName(const char*);	// receipient's name
    const fxStr& getCoverName() const;
    void setCoverLocation(const char*);	// receipient's location
    const fxStr& getCoverLocation() const;
    void setCoverCompany(const char*);	// receipient's company
    const fxStr& getCoverCompany() const;
    void setCoverRegarding(const char*);// Re: information
    const fxStr& getCoverRegarding() const;
    void setCoverComments(const char*);	// other comments
    const fxStr& getCoverComments() const;
    void setCoverVoiceNumber(const char*);// receipient's voice number
    const fxStr& getCoverVoiceNumber() const;
    u_int getTotalPages() const;	// counted pages (for cover sheet)

    /*
     * Page size support.
     *
     * Page dimensions are specified using the page size database.
     * The transmit resolution is currently limited to 98 or 196
     * lines/inch with the latter possibly ignored if the transmitting
     * modem or receiving device are incapable of supporting it.
     */
    float getPageWidth() const;		// sending page width (mm)
    float getPageLength() const;	// sending page length (mm)
    const fxStr& getPageSize() const;	// page size by name
    bool setPageSize(const char* name);
    void setVResolution(float);		// vertical resolution (lines/inch)
    float getVResolution() const;
    void setHResolution(float);		// horizontal resolution (lines/inch)
    float getHResolution() const;

    /*
     * Fax session parameter controls.
     */
    void setMinSpeed(int);		// minimum signalling rate
    void setMinSpeed(const char* v);
    int getMinSpeed() const;
    void setDesiredSpeed(int);		// desired/initial signalling rate
    void setDesiredSpeed(const char* v);
    int getDesiredSpeed() const;
    void setDesiredMST(int);		// desired/initial min-scanline time
    void setDesiredMST(const char* v);
    int getDesiredMST() const;
    void setDesiredEC(bool b);	// desired use of Error Correction mode
    bool getDesiredEC() const;
    void setDesiredDF(int);		// desired data format
    void setDesiredDF(const char*);
    int getDesiredDF() const;

    void setTagLineFormat(const char* v); // job-specific tagline format
    const fxStr& getTagLineFormat() const;

    void setChopHandling(const char*);	// page truncation handling
    void setChopHandling(u_int);
    u_int getChopHandling() const;
    void setChopThreshold(float);	// threshold (inches) for page chopping
    float getChopThreshold() const;
};

fxDECLARE_ObjArray(SendFaxJobArray, SendFaxJob)

inline const fxStr& SendFaxJob::getJobID(void) const	{ return jobid; }
inline const fxStr& SendFaxJob::getGroupID(void) const	{ return groupid; }
inline FaxNotify SendFaxJob::getNotification() const	{ return notify; }
inline const fxStr& SendFaxJob::getMailbox() const	{ return mailbox; }
inline const fxStr& SendFaxJob::getJobTag() const	{ return jobtag; }
inline u_int SendFaxJob::getRetryTime() const		{ return retryTime; }
inline const fxStr& SendFaxJob::getKillTime() const	{ return killTime; }
inline const fxStr& SendFaxJob::getSendTime() const	{ return sendTime; }
inline u_int SendFaxJob::getMaxRetries() const		{ return maxRetries; }
inline u_int SendFaxJob::getMaxDials() const		{ return maxDials; }
inline int SendFaxJob::getPriority() const		{ return priority; }
inline const fxStr& SendFaxJob::getDialString() const	{ return number; }
inline const fxStr& SendFaxJob::getSubAddress() const	{ return subaddr; }
inline const fxStr& SendFaxJob::getPasswd() const	{ return passwd; }
inline const fxStr& SendFaxJob::getExternalNumber() const{ return external; }
inline bool SendFaxJob::getAutoCoverPage() const	{ return autoCover; }
inline const fxStr& SendFaxJob::getCoverPageFile() const{ return coverFile; }
inline const fxStr& SendFaxJob::getCoverTemplate() const{ return coverTemplate;}
inline const fxStr& SendFaxJob::getCoverName() const	{ return name; }
inline const fxStr& SendFaxJob::getCoverLocation() const{ return location; }
inline const fxStr& SendFaxJob::getCoverCompany() const	{ return company; }
inline const fxStr& SendFaxJob::getCoverRegarding() const{ return regarding; }
inline const fxStr& SendFaxJob::getCoverComments() const{ return comments; }
inline const fxStr& SendFaxJob::getCoverVoiceNumber() const{ return voicenumber; }
inline float SendFaxJob::getPageWidth() const		{ return pageWidth; }
inline float SendFaxJob::getPageLength() const		{ return pageLength; }
inline const fxStr& SendFaxJob::getPageSize() const	{ return pageSize; }
inline float SendFaxJob::getVResolution() const		{ return vres; }
inline float SendFaxJob::getHResolution() const		{ return hres; }
inline int SendFaxJob::getMinSpeed() const		{ return minsp; }
inline int SendFaxJob::getDesiredSpeed() const		{ return desiredbr; }
inline int SendFaxJob::getDesiredMST() const		{ return desiredst; }
inline bool SendFaxJob::getDesiredEC() const		{ return desiredec; }
inline int SendFaxJob::getDesiredDF() const		{ return desireddf; }
inline const fxStr& SendFaxJob::getTagLineFormat() const{ return tagline; }
inline u_int SendFaxJob::getChopHandling() const	{ return pagechop; }
inline float SendFaxJob::getChopThreshold() const	{ return chopthreshold; }
#endif /* _SendFaxJob_ */
