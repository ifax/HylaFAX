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
#ifndef _FaxRequest_
#define	_FaxRequest_
/*
 * HylaFAX Job Request Structure.
 */
#include "FaxSendStatus.h"
#include "FaxItem.h"
#include <time.h>
#include <stdio.h>

class Class2Params;

/*
 * This structure is passed from the queue manager
 * to the fax modem+protocol service for each job
 * to be processed.  This class also supports the
 * reading and writing of this information to an
 * external file.
 */
class FaxRequest {
protected:
    void reset(void);
    virtual bool checkDocument(const char* pathname);
    virtual void error(const char* fmt, ...);
public:
    enum {
	send_fax	= 0,	// send prepared file via fax
	send_tiff	= 1,	// send tiff file via fax
	send_tiff_saved	= 2,	// saved tiff file (converted)
	send_pdf	= 3,	// send PDF file via fax
	send_pdf_saved	= 4,	// saved PDF file (converted)
	send_postscript	= 5,	// send postscript file via fax
	send_postscript_saved = 6, // saved postscript file (converted)
	send_pcl	= 7,	// send PCL file via fax
	send_pcl_saved	= 8,	// saved PCL file (converted to tiff)
	send_data	= 9,	// send untyped data file
	send_data_saved	= 10,	// send untyped data file (converted)
	send_poll	= 11,	// make fax poll request
	send_page	= 12,	// send pager message (converted)
	send_page_saved	= 13,	// send pager message
	send_uucp	= 14,	// send file via uucp
	send_unknown= 15

    };
    enum {			// notification flags
	no_notice	= 0x0,	// no notifications
	when_done	= 0x1,	// notify when send completed
	when_requeued	= 0x2,	// notify if job requeued
	notify_any	= 0x3
    };
    enum {			// page chopping control
	chop_default	= 0,	// default server setting
	chop_none	= 1,	// chop no pages
	chop_all	= 2,	// chop all pages with trailing white
	chop_last	= 3	// chop last page in each document
    };
    enum {			// job scheduling state
	state_undefined	= 0,	// undefined state (should never be used)
	state_suspended	= 1,	// not being scheduled
	state_pending	= 2,	// waiting for time to send
	state_sleeping	= 3,	// waiting for scheduled timeout
	state_blocked	= 4,	// blocked by concurrent activity
	state_ready	= 5,	// ready to be go, waiting for resources
	state_active	= 6,	// actively being processed
	state_done	= 7,	// processing completed with success
	state_failed	= 8	// processing completed with a failure
    };
    struct stringval {		// string-valued item
	const char* name;
	fxStr FaxRequest::* p;
    };
    struct shortval {		// u_short-valued item
	const char* name;
	u_short FaxRequest::* p;
    };

    fxStr	qfile;		// associated queue file name
    fxStr	jobid;		// job identifier
    fxStr	groupid;	// job group identifier
    fxStr	owner;		// job owner identifier
    fxStr	commid;		// last session's communication ID
    int		fd;		// open+locked queue file
    u_short	state;		// job scheduling state
    u_short	lineno;		// line number when reading queue file
    FaxSendStatus status;	// request status indicator
    u_short	totpages;	// total cummulative pages in documents
    u_short	npages;		// total pages sent/received
    u_short	ntries;		// # tries to send current page
    u_short	ndials;		// # consecutive failed tries to call dest
    u_short	totdials;	// total # calls to dest
    u_short	maxdials;	// max # times to dial the phone
    u_short	tottries;	// total # attempts to deliver
    u_short	maxtries;	// max # attempts to deliver (answered calls)
    u_short	pagewidth;	// desired output page width (mm)
    u_short	pagelength;	// desired output page length (mm)
    u_short	resolution;	// desired vertical resolution (lpi) (normal/fine)
    u_short	usrpri;		// user-requested scheduling priority
    u_short	pri;		// current scheduling priority
    u_short	minsp;		// minimum acceptable signalling rate
    u_short	desiredbr;	// desired signalling rate
    u_short	desiredst;	// desired min-scanline-time
    u_short	desiredec;	// enable use of ECM if available
    u_short	desireddf;	// desired data format
    u_short	desiredtl;	// desired tagline handling
    u_short	useccover;	// whether to use continuation cover page
    u_short	usexvres;	// whether to use extended VR
    u_short	pagechop;	// whether to do page chopping
    u_short	notify;		// email notification flags
    float	chopthreshold;	// minimum white space before chopping
    time_t	tts;		// time to send
    time_t	killtime;	// time to kill job
    time_t	retrytime;	// time to delay between retries
    fxStr	sender;		// sender's name
    fxStr	mailaddr;	// return mail address
    fxStr	jobtag;		// user-specified job tag
    fxStr	number;		// dialstring for fax machine
    fxStr	subaddr;	// transmit subaddress
    fxStr	passwd;		// transmit password
    fxStr	external;	// displayable phone number for fax machine
    fxStr	notice;		// message to send for notification
    fxStr	modem;		// outgoing modem to use
    fxStr	pagehandling;	// page analysis information
    fxStr	receiver;	// receiver's identity for cover page generation
    fxStr	company;	// receiver's company for cover page generation
    fxStr	location;	// receiver's location for cover page generation
    fxStr	cover;		// continuation cover page filename
    fxStr	client;		// identity of machine that submitted job
    fxStr	sigrate;	// negotiated signalling rate
    fxStr	df;		// negotiated data format
    fxStr	jobtype;	// job type for selecting send command
    fxStr	tagline;	// tag line format
    fxStr	doneop;		// operation to do when job completes
    FaxItemArray items;	// set of requests

    static stringval strvals[];
    static shortval shortvals[];
    static char* opNames[18];
    static char* notifyVals[4];
    static char* chopVals[4];

    FaxRequest(const fxStr& qf, int fd = -1);
    virtual ~FaxRequest();
    bool readQFile(bool& rejectJob);
    bool reReadQFile(bool& rejectJob);
    void writeQFile();
    u_int findItem(FaxSendOp, u_int start = 0) const;

    bool isNotify(u_int what) const;

    static bool isStrCmd(const char* cmd, u_int& ix);
    static bool isShortCmd(const char* cmd, u_int& ix);

    void insertFax(u_int ix, const fxStr& file);
    void addItem(FaxSendOp op, char* tag);
    void addItem(FaxSendOp op, char* tag, bool& rejectJob);
    void checkNotifyValue(const char* tag);
    void checkChopValue(const char* tag);

    static fxStr mkbasedoc(const fxStr& file);
    void renameSaved(u_int fi);
    bool isUnreferenced(u_int fi);
};
inline bool FaxRequest::isNotify(u_int what) const
    { return (notify & (u_short) what) != 0; }
#endif /* _FaxRequest_ */
