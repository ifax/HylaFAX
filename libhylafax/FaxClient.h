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
#ifndef _FaxClient_
#define	_FaxClient_

#include "Types.h"
#include "Str.h"
#include "FaxConfig.h"

class Transport;

class FaxClient : public FaxConfig {
public:
    enum {			// command reply codes
        PRELIM	  = 1,		// positive preliminary
        COMPLETE  = 2,		// positive completion
        CONTINUE  = 3,		// positive intermediate
        TRANSIENT = 4,		// transient negative completion
        ERROR	  = 5		// permanent negative completion
    };
    enum {			// data transfer TYPEs
        TYPE_A	= 1,		// ASCII
        TYPE_E	= 2,		// EBCDIC
        TYPE_I	= 3,		// image
        TYPE_L	= 4		// local byte size
    };
    enum {			// data transfer file STRUctures
        STRU_F = 1,		// file (no record structure)
        STRU_R = 2,		// record structure
        STRU_P = 3,		// page structure
        STRU_T = 4		// multi-IFD TIFF
    };
    enum {			// data transfer MODEs
        MODE_S = 1,		// stream
        MODE_B = 2,		// block
        MODE_C = 3,		// run-length compressed
        MODE_Z = 4		// zlib compressed
    };
    enum {			// data file FORMats
        FORM_UNKNOWN = 0,	// unknown, initial setting
        FORM_PS   = 1,		// PostScript Level I
        FORM_PS2  = 2,		// PostScript Level II
        FORM_TIFF = 3,		// TIFF
        FORM_PCL  = 4,		// HP PCL5
        FORM_PDF  = 5		// Portable Document Format
    };
    enum {
        TZ_GMT	  = 1,		// use GMT timezone for time values
        TZ_LOCAL  = 2		// use local timezone for time values
    };

    // NB: the F_ prefixes workaround a bug in the AIX xlC compiler
    struct F_stringtag {
        const char*	 name;
        fxStr FaxClient::* p;
        const char*	 def;	// NULL is shorthand for ""
    };
    struct F_numbertag {
        const char*	 name;
        u_int FaxClient::*p;
        u_int		 def;
    };
    struct FaxParam {
        const char* cmd;
        const char** parmNames;
        u_int	NparmNames;
        u_int	FaxClient::*pv;
    };
    struct FaxFmtHeader {
        char	fmt;		// format character used by server
        const char* title;	// column title to use
    };
private:
    Transport*	transport;	// underlying transport protocol support
    fxStr	host;		// server's host
    fxStr	modem;		// server's modem
    u_int	state;		// state flags
#define	FS_VERBOSE	0x0001	// print data as sent or received
#define	FS_LOGGEDIN	0x0002	// logged in on server
#define	FS_TZPEND	0x0004	// tzone setting pending
#define	FS_JFMTPEND	0x0008	// job status format string pending
#define	FS_RFMTPEND	0x0010	// job status format string pending
#define	FS_MFMTPEND	0x0020	// modem status format string pending
#define	FS_FFMTPEND	0x0040	// file status format string pending
    fxStr	userName;	// sender's account name
    fxStr	senderName;	// sender's full name (if available)
    FILE*	fdIn;		// control stream input handle
    FILE*	fdOut;		// control stream output handle
    int		fdData;		// data transfer connection
    char	buf[1024];	// input buffer
    int		code;		// code from last server repsonse
    bool	pasv;		// use of passive mode
    fxStr	proto;		// protocol to use for service query
    fxStr	lastResponse;	// text message from last server response
    fxStr	lastContinuation; // continuation message from last server response
    u_int	port;		// server port to connect to
    u_int	type;		// data transfer type
    u_int	stru;		// file structure
    u_int	mode;		// data transfer mode
    u_int	format;		// document format
    u_int	tzone;		// use GMT or local timezone for time values
    fxStr	curjob;		// current job's ID
    fxStr	jobFmt;		// job status format string
    fxStr	recvFmt;	// recv queue status format string
    fxStr	modemFmt;	// modem status format string
    fxStr	fileFmt;	// file status format string
    fxStr	jobSFmt;	// job status sort format string
    fxStr	recvSFmt;	// recv queue status sort format string
    fxStr	modemSFmt;	// modem status sort format string
    fxStr	fileSFmt;	// file status sort format string

    static F_stringtag strings[];
    static F_numbertag numbers[];
    static FaxParam typeParam;
    static FaxParam modeParam;
    static FaxParam struParam;
    static FaxParam formParam;
    static FaxParam tzoneParam;

    void init(void);

    bool sendRawData(void* buf, int cc, fxStr& emsg);
    bool setCommon(FaxParam&, u_int);
protected:
    FaxClient();
    FaxClient(const fxStr& hostarg);
    FaxClient(const char* hostarg);
    ~FaxClient();

    virtual void vprintError(const char* fmt, va_list ap);
    virtual void vprintWarning(const char* fmt, va_list ap);
    virtual void vtraceServer(const char* fmt, va_list ap);

    void initServerState(void);
    bool jobOp(const char* op, const char* jobid);
    bool extract(u_int& pos, const char* pattern, fxStr& result,
        const char* cmd, fxStr& emsg);
    bool storeUnique(const char* cmd, fxStr& docname, fxStr& emsg);

    const fxStr& getStatusFormat(u_int flag, const char* cmd, fxStr& fmt);
    bool setStatusFormat(const char* cmd, u_int flag, fxStr&, const char*);
    void makeHeader(const char* fmt, const FaxFmtHeader fmts[], fxStr& header);

    virtual bool setupUserIdentity(fxStr& emsg);
    void setupHostModem(const char*);
    void setupHostModem(const fxStr&);

    virtual void resetConfig(void);
    virtual void setupConfig(void);
    virtual bool setConfigItem(const char* tag, const char* value);
    virtual void configError(const char* fmt ...);
    virtual void configTrace(const char* fmt ...);

    void unexpectedResponse(fxStr& emsg);
    void protocolBotch(fxStr& emsg, const char* fmt ...);
    virtual void lostServer(void);
public:
    void printError(const char* fmt ...);
    void printWarning(const char* fmt ...);
    void traceServer(const char* fmt ...);

    // bookkeeping
    void setHost(const fxStr&);
    void setHost(const char*);
    void setPort(int);
    void setProtoName(const char*);
    const fxStr& getHost() const;
    void setModem(const fxStr&);
    void setModem(const char*);
    const fxStr& getModem(void) const;

    virtual bool callServer(fxStr& emsg);
    virtual bool hangupServer(void);
    bool isConnected(void) const;
    bool isPassive(void) const;
    bool login(const char* user, fxStr& emsg);
    bool admin(const char* pass, fxStr& emsg);
    virtual const char* getPasswd(const char* prompt);
    bool isLoggedIn(void) const;
    void setCtrlFds(int in, int out);
    FILE* getCtrlFd(void) const;

    virtual bool initDataConn(fxStr& emsg);
    virtual bool openDataConn(fxStr& emsg);
    virtual void closeDataConn(void);
    virtual bool abortDataConn(fxStr& emsg);
    void setDataFd(int fd);
    int getDataFd(void) const;

    void setVerbose(bool);
    bool getVerbose(void) const;

    int getPort(void) const;
    const fxStr& getProtoName(void) const;

    const fxStr& getSenderName(void) const;
    const fxStr& getUserName(void) const;

    // output
    int command(const char* fmt ...);
    int vcommand(const char* fmt, va_list ap);
    int getReply(bool expectEOF);
    const fxStr& getLastResponse(void) const;
    const fxStr& getLastContinuation(void) const;
    int getLastCode(void) const;
    /*
     * Job control support.
     */
    const fxStr& getCurrentJob(void) const;
    bool setCurrentJob(const char* jobid);
    bool newJob(fxStr& jobid, fxStr& groupid, fxStr& emsg);
    bool jobSubmit(const char* jobid);
    bool jobSuspend(const char* jobid);
    bool jobKill(const char* jobid);
    bool jobDelete(const char* jobid);
    bool jobWait(const char* jobid);
    /*
     * Set various job parameters.
     */
    bool jobParm(const char* name, const fxStr& value);
    bool jobParm(const char* name, const char* value);
    bool jobParm(const char* name, bool b);
    bool jobParm(const char* name, u_int v);
    bool jobParm(const char* name, float v);
    bool jobSendTime(const struct tm tm);
    bool jobLastTime(u_long);
    bool jobRetryTime(u_long);
    bool jobCover(const char* docname);
    bool jobDocument(const char* docname);
    bool jobPollRequest(const char* sep, const char* pwd);
    /*
     * Job group control support.
     */
    bool jgrpSubmit(const char* jgrpid);
    bool jgrpSuspend(const char* jgrpid);
    bool jgrpKill(const char* jgrpid);
    bool jgrpWait(const char* jgrpid);
    /*
     * Query/set transfer state parameters.
     */
    u_int getType(void) const;
    bool setType(u_int);
    u_int getMode(void) const;
    bool setMode(u_int);
    u_int getStruct(void) const;
    bool setStruct(u_int);
    u_int getFormat(void) const;
    bool setFormat(u_int);
    u_int getTimeZone(void) const;
    bool setTimeZone(u_int);
    /*
     * Send documents to the server.
     */
    bool storeUnique(fxStr& docname, fxStr& emsg);	// STOU
    bool storeTemp(fxStr& docname, fxStr& emsg);	// STOT
    bool storeFile(fxStr&, fxStr& emsg);		// STOR
    bool sendData(int fd, bool (FaxClient::*store)(fxStr&, fxStr&),
	fxStr& docname, fxStr& emsg);
    bool sendZData(int fd, bool (FaxClient::*store)(fxStr&, fxStr&),
	fxStr& docname, fxStr& emsg);
    /*
     * Retrieve information from the server.
     */
    bool recvData(bool (*f)(int, const char*, int, fxStr&),
    int arg, fxStr& emsg, u_long restart, const char* fmt, ...);
    bool recvZData(bool (*f)(void*, const char*, int, fxStr&),
    void* arg, fxStr& emsg, u_long restart, const char* fmt, ...);
    /*
     * Job scripting support.
     */
    bool runScript(const char* filename, fxStr& emsg);
    bool runScript(FILE*, const char* filename, fxStr& emsg);
    bool runScript(const char* script, u_long scriptLen,
        const char* filename, fxStr& emsg);
    /*
     * Status query support.
     */
    static const FaxFmtHeader jobFormats[];
    static const FaxFmtHeader recvFormats[];
    static const FaxFmtHeader modemFormats[];
    static const FaxFmtHeader fileFormats[];

    bool setJobStatusFormat(const char*);
    const fxStr& getJobStatusFormat(void);
    bool setRecvStatusFormat(const char*);
    const fxStr& getRecvStatusFormat(void);
    bool setModemStatusFormat(const char*);
    const fxStr& getModemStatusFormat(void);
    bool setFileStatusFormat(const char*);
    const fxStr& getFileStatusFormat(void);
    void getJobStatusHeader(fxStr& header);
    void getRecvStatusHeader(fxStr& header);
    void getModemStatusHeader(fxStr& header);
    void getFileStatusHeader(fxStr& header);
};

inline const fxStr& FaxClient::getSenderName(void) const{ return senderName; }
inline const fxStr& FaxClient::getUserName(void) const	{ return userName; }
inline const fxStr& FaxClient::getHost(void) const	{ return host; }
inline const fxStr& FaxClient::getModem(void) const	{ return modem; }
inline const fxStr& FaxClient::getProtoName() const	{ return proto; }
inline bool FaxClient::getVerbose(void) const
    { return (state&FS_VERBOSE) != 0; }
inline int FaxClient::getPort(void) const		{ return port; }
inline FILE* FaxClient::getCtrlFd(void) const		{ return fdOut; }
inline int FaxClient::getDataFd(void) const		{ return fdData; }
inline const fxStr& FaxClient::getLastResponse(void) const
    { return lastResponse; }
inline const fxStr& FaxClient::getLastContinuation(void) const
    { return lastContinuation; }
inline int FaxClient::getLastCode(void) const		{ return code; }
inline bool FaxClient::isLoggedIn(void) const
    { return (state&FS_LOGGEDIN) != 0; }
inline bool FaxClient::isConnected(void) const	{ return fdIn != NULL; }
inline bool FaxClient::isPassive(void) const	{ return pasv; }
inline u_int FaxClient::getType(void) const		{ return type; }
inline u_int FaxClient::getStruct(void) const		{ return stru; }
inline u_int FaxClient::getMode(void) const		{ return mode; }
inline u_int FaxClient::getFormat(void) const		{ return format; }
inline u_int FaxClient::getTimeZone(void) const		{ return tzone; }
inline const fxStr& FaxClient::getCurrentJob(void) const{ return curjob; }

extern void fxFatal(const char* fmt, ...);
#endif /* _FaxClient_ */
