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
#ifndef _SNPPClient_
#define	_SNPPClient_

#include "Types.h"
#include "Str.h"
#include "FaxConfig.h"
#include "SNPPJob.h"

class SNPPClient : public FaxConfig {
public:
    enum {			// command reply codes
	COMPLETE  = 2,		// positive completion
	CONTINUE  = 3,		// positive intermediate
	FATAL	  = 4,		// negative completion, connection terminated
	ERROR	  = 5,		// negative completion, but continue session
	ERROR2WAY = 7,		// unsuccessfuly 2-way op, continue session
	COMPLETE2WAY = 8,	// positive 2-way op, continue
	QUEUED	  = 9		// successful queued 2-way op, continue
    };

    // NB: the S_ prefixes workaround a bug in the AIX xlC compiler
    struct S_stringtag {
	const char*	 name;
	fxStr SNPPClient::* p;
	const char*	 def;	// NULL is shorthand for ""
    };
    struct S_numbertag {
	const char*	 name;
	u_int SNPPClient::*p;
	u_int		 def;
    };
private:
    SNPPJobArray* jobs;		// job state information
    SNPPJob	jproto;		// prototypical job
    fxStr	host;		// server's host
    fxStr	modem;		// server's modem
    u_int	state;		// state flags
#define	SS_VERBOSE	0x0001	// print data as sent or received
#define	SS_LOGGEDIN	0x0002	// logged in on server
#define	SS_HASSITE	0x0004	// supports non-standard SITE command
    fxStr	userName;	// sender's account name
    fxStr	senderName;	// sender's full name (if available)
    fxStr	from;		// from identity
    FILE*	fdIn;		// control stream input handle
    FILE*	fdOut;		// control stream output handle
    char	buf[1024];	// input buffer
    int		code;		// code from last server repsonse
    fxStr	proto;		// protocol to use for service query
    fxStr	lastResponse;	// text message from last server response
    u_int	port;		// server port to connect to
    fxStr	msgFile;	// filename of message text
    fxStr*	msg;		// 1-line message text string

    static const S_stringtag strings[];
    static const S_numbertag numbers[];

    void init(void);
    fxBool callInetServer(fxStr& emsg);
protected:
    SNPPClient();
    SNPPClient(const fxStr& hostarg);
    SNPPClient(const char* hostarg);

    /*
     * Derived classes can override these methods to
     * provide more suitable feedback than the default
     * ``print to the terminal'' work done by SNPPClient.
     */
    virtual void vprintError(const char* fmt, va_list ap);
    virtual void vprintWarning(const char* fmt, va_list ap);
    virtual void vtraceServer(const char* fmt, va_list ap);
    /*
     * Derived classes can override this method to capture
     * job identifiers returned by the server when a job is
     * submitted.  The default action is to print a message
     * to the terminal identifying the jobid of the newly
     * submitted job.
     */
    virtual void notifyNewJob(const SNPPJob& job);

    void initServerState(void);
    fxBool extract(u_int& pos, const char* pattern, fxStr& result);

    virtual fxBool setupUserIdentity(fxStr& emsg);
    void setupHostModem(const char*);
    void setupHostModem(const fxStr&);

    /*
     * Miscellaneous stuff used by setupSenderIdentity.
     */
    void setBlankMailboxes(const fxStr&);
    fxBool getNonBlankMailbox(fxStr&);

    /*
     * Configuration file support; derived classes may override
     * these to implement application-specific config controls.
     */
    virtual void resetConfig(void);
    virtual void setupConfig(void);
    virtual fxBool setConfigItem(const char* tag, const char* value);
    virtual void configError(const char* fmt ...);
    virtual void configTrace(const char* fmt ...);

    fxBool sendRawData(void* buf, int cc, fxStr& emsg);
    void unexpectedResponse(fxStr& emsg);
    void protocolBotch(fxStr& emsg, const char* fmt ...);
    virtual void lostServer(void);
public:
    virtual ~SNPPClient();
						// prepare jobs for submission
    virtual fxBool prepareForJobSubmissions(fxStr& emsg);
    virtual fxBool submitJobs(fxStr& emsg);	// submit documents & jobs

    /*
     * Sender identity controls.  There are two separate
     * identities maintained, one for the actual user/account
     * that submits the jobs and another for person identified
     * as the sender on the outbound facsimile.  This distinction
     * is used by proxy services such as email to pager gateways
     * and for folks that submit jobs for other people.
     */
    fxBool setupSenderIdentity(fxStr& emsg);	// identity associated with job
    const fxStr& getSenderName() const;
    void setFromIdentity(const char*);		// identity associated with page
    const fxStr& getFromIdentity() const;

    // bookkeeping
    void setHost(const fxStr&);
    void setHost(const char*);
    void setPort(int);
    void setProtoName(const char*);
    const fxStr& getHost() const;
    void setModem(const fxStr&);
    void setModem(const char*);
    const fxStr& getModem(void) const;

    virtual fxBool callServer(fxStr& emsg);
    virtual fxBool hangupServer(void);
    fxBool isConnected(void) const;
    fxBool login(const char* user, fxStr& emsg);
    virtual const char* getPasswd(const char* prompt);
    fxBool isLoggedIn(void) const;
    void setCtrlFds(int in, int out);
    FILE* getCtrlFd(void) const;

    void setVerbose(fxBool);
    fxBool getVerbose(void) const;

    fxBool hasSiteCmd(void) const;		// server has SITE cmd support

    int getPort(void) const;
    const fxStr& getProtoName(void) const;

    const fxStr& getUserName(void) const;

    // output
    int command(const char* fmt ...);
    int vcommand(const char* fmt, va_list ap);
    int getReply(fxBool expectEOF);
    const fxStr& getLastResponse(void) const;
    int getLastCode(void) const;

    void setPagerMsg(const char*);
    const fxStr* getPagerMsg() const;
    void setPagerMsgFile(const char*);
    const fxStr& getPagerMsgFile() const;

    /*
     * Job manipulation interfaces.
     */
    SNPPJob& addJob(void);
    SNPPJob* findJob(const fxStr& pin);
    void removeJob(const SNPPJob&);
    u_int getNumberOfJobs() const;

    SNPPJob& getProtoJob();

    fxBool setHoldTime(u_int t);
    fxBool setRetryTime(u_int t);
    fxBool siteParm(const char* name, const fxStr& v);
    fxBool siteParm(const char* name, u_int v);
    fxBool newPage(const fxStr& pin, const fxStr& passwd,
	fxStr& jobid, fxStr& emsg);
    fxBool sendData(int fd, fxStr& emsg);
    fxBool sendData(const fxStr& filename, fxStr& emsg);
    fxBool sendMsg(const char* msg, fxStr& emsg);

    void printError(const char* fmt ...);
    void printWarning(const char* fmt ...);
    void traceServer(const char* fmt ...);
};
inline const fxStr& SNPPClient::getSenderName(void) const{ return senderName; }
inline const fxStr& SNPPClient::getFromIdentity(void) const { return from; }
inline const fxStr& SNPPClient::getUserName(void) const	{ return userName; }
inline const fxStr& SNPPClient::getHost(void) const	{ return host; }
inline const fxStr& SNPPClient::getModem(void) const	{ return modem; }
inline const fxStr& SNPPClient::getProtoName() const	{ return proto; }
inline fxBool SNPPClient::getVerbose(void) const
    { return (state&SS_VERBOSE) != 0; }
inline int SNPPClient::getPort(void) const		{ return port; }
inline FILE* SNPPClient::getCtrlFd(void) const		{ return fdOut; }
inline const fxStr& SNPPClient::getLastResponse(void) const
    { return lastResponse; }
inline int SNPPClient::getLastCode(void) const		{ return code; }
inline fxBool SNPPClient::isLoggedIn(void) const
    { return (state&SS_LOGGEDIN) != 0; }
inline fxBool SNPPClient::isConnected(void) const	{ return fdIn != NULL; }
inline fxBool SNPPClient::hasSiteCmd(void) const
    { return (state&SS_HASSITE) != 0 ; }
inline SNPPJob& SNPPClient::getProtoJob()		{ return jproto; }
inline const fxStr* SNPPClient::getPagerMsg() const	{ return msg; }
inline const fxStr& SNPPClient::getPagerMsgFile() const	{ return msgFile; }

extern void fxFatal(const char* fmt, ...);
#endif /* _SNPPClient_ */
