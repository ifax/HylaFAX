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

/*
                    SendFaxClient class
*/
#ifndef _SendFaxClient_
#define	_SendFaxClient_

#include "FaxClient.h"
#include "SendFaxJob.h"

class TypeRule;
class TypeRules;
class DialStringRules;
struct FileInfo;
class FileInfoArray;
struct PollRequest;
class PollRequestArray;

class SendFaxClient : public FaxClient {
public:
    // NB: the SF_ prefixes workaround a bug in the AIX xlC compiler
    struct SF_stringtag {
	const char*	 name;
	fxStr SendFaxClient::* p;
	const char*	 def;		// NULL is shorthand for ""
    };
private:
    SendFaxJobArray* jobs;		// job state information
    SendFaxJob	proto;			// prototypical job
    fxStr	typeRulesFile;		// filename for type deduction rules
    TypeRules*	typeRules;		// file type/conversion database
    fxStr	dialRulesFile;		// filename for dialstring rules
    DialStringRules* dialRules;		// dial string conversion database
    FileInfoArray* files;		// files to send (possibly converted)
    PollRequestArray* polls;		// polling requests
    bool	verbose;		// enable debugging information
    bool	setup;			// if true, then ready to send
    fxStr	tmpFile;		// stuff to cleanup on abort
    fxStr	from;			// command line from information
    fxStr	senderName;		// sender's full name
    fxStr	coverCmd;		// cover page program name
    u_int	totalPages;		// total pages in submitted documents
    fxStr	dateFormat;		// date format string for cover page

    static const SF_stringtag strings[];
protected:
    SendFaxClient();

    /*
     * Derived classes can override these methods to
     * provide more suitable feedback than the default
     * ``print to the terminal'' work done by FaxClient.
     */
    virtual void vprintError(const char* fmt, va_list ap);
    virtual void vprintWarning(const char* fmt, va_list ap);
    virtual void vtraceServer(const char* fmt, va_list ap);
    /*
     * Derived classes can override this method to capture
     * job identifiers returned by the server when a job is
     * submitted.  The default action is to print a message
     * to the terminal identifying the jobid and groupid of
     * newly submitted job.
     */
    virtual void notifyNewJob(const SendFaxJob& job);
    /*
     * Derived classes can override makeCoverPage to supply an
     * application-specific cover page generation scheme.
     */
    virtual bool makeCoverPage(const SendFaxJob&, fxStr& file, fxStr& emsg);
    /*
     * These methods are used to count/estimate the number
     * of pages in a document that is to be transmitted.
     * Counting pages in a TIFF document is easy and reliable
     * but doing it for a PostScript document is not; so that
     * method is marked virtual in case a derived class wants to
     * provide a better algorithm than the default one.
     */
    void countTIFFPages(const char* name);
    virtual void estimatePostScriptPages(const char* name);

    /*
     * Configuration file support; derived classes may override
     * these to implement application-specific config controls.
     */
    virtual void setupConfig();
    virtual void resetConfig();
    virtual bool setConfigItem(const char* tag, const char* value);

    /*
     * File typerule support.
     */
    const TypeRule* fileType(const char* filename, fxStr& emsg);
    bool prepareFile(FileInfo& info, fxStr& emsg);

    /*
     * Miscellaneous stuff used by setupSenderIdentity.
     */
    void setBlankMailboxes(const fxStr&);
    bool getNonBlankMailbox(fxStr&);
public:
    virtual ~SendFaxClient();

						// prepare jobs for submission
    virtual bool prepareForJobSubmissions(fxStr& emsg);
    void purgeFileConversions(void);		// purge any converted docs
    virtual bool submitJobs(fxStr& emsg);	// submit documents & jobs
    virtual bool sendDocuments(fxStr& emsg);	// send prepared documents

    /*
     * Job manipulation interfaces.
     */
    SendFaxJob& addJob(void);
    SendFaxJob* findJob(const fxStr& number, const fxStr& name);
    SendFaxJob* findJobByTag(const fxStr& tag);
    void removeJob(const SendFaxJob&);
    u_int getNumberOfJobs() const;

    SendFaxJob& getProtoJob();
    /*
     * Document transmit request interfaces.
     */
    u_int addFile(const fxStr& filename);
    u_int findFile(const fxStr& filename) const;
    void removeFile(u_int);
    u_int getNumberOfFiles() const;
    const fxStr& getFileDocument(u_int) const;
    /*
     * Polled retrieval request interfaces.
     */
    u_int addPollRequest();
    u_int addPollRequest(const fxStr& sep);
    u_int addPollRequest(const fxStr& sep, const fxStr& pwd);
    void removePollRequest(u_int);
    u_int getNumberOfPollRequests() const;
    void getPollRequest(u_int, fxStr& sep, fxStr& pwd);

    /*
     * Sender identity controls.  There are two separate
     * identities maintained, one for the actual user/account
     * that submits the jobs and another for person identified
     * as the sender on the outbound facsimile.  This distinction
     * is used by proxy services such as email to fax gateways
     * and for folks people that submit jobs for other people.
     */
						// identity associated with job
    bool setupSenderIdentity(const fxStr&, fxStr& emsg);
    const fxStr& getSenderName() const;
    void setFromIdentity(const char*);		// identity associated with fax
    const fxStr& getFromIdentity() const;

    bool getVerbose() const;			// trace operation
    void setVerbose(bool);
};

inline SendFaxJob& SendFaxClient::getProtoJob()	{ return proto; }
inline const fxStr& SendFaxClient::getFromIdentity() const { return from; }
inline const fxStr& SendFaxClient::getSenderName() const { return senderName;}
inline void SendFaxClient::setVerbose(bool b) { verbose = b; }
inline bool SendFaxClient::getVerbose() const { return verbose; }
#endif /* _SendFaxClient_ */
