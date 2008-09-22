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
#ifndef _HylaFAXServer_
#define	_HylaFAXServer_

#include "FaxConfig.h"
#include "IOHandler.h"
#include "Dictionary.h"
#include "StrArray.h"
#include "FaxRequest.h"
#include "FaxRecvInfo.h"
#include "manifest.h"
#include "FileCache.h"
#include "Trace.h"
#include "Trigger.h"
#include "StackBuffer.h"
#include "SystemLog.h"

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <dirent.h>

#include <setjmp.h>
#include <errno.h>

/*
 * In-memory copy of a job description file.
 */
struct Job : public FaxRequest {
    time_t	lastmod;		// last file modify time, for updates
    bool	queued;			// for SNPP

    Job(const fxStr& qf, int fd = -1);
    ~Job();

    bool checkDocument(const char* pathname);
};
fxDECLARE_StrKeyDictionary(JobDict, Job*)

/*
 * Received facsimile information.
 */
struct RecvInfo : public FaxRecvInfo {
    bool	beingReceived;		// currently being received
    time_t	recvTime;		// time receive operation started

    RecvInfo();
    RecvInfo(const char* qfile);
    ~RecvInfo();
};
fxDECLARE_StrKeyDictionary(RecvInfoDict, RecvInfo*)

/*
 * An array of content that can sort on a different key
 */
class KeyString
 : public fxStr
{
    public:
	KeyString ();
	KeyString (const fxStr& key, const fxStr& data);

	int compare (const KeyString* that) const;

    private:
	fxStr key;
};
fxDECLARE_ObjArray(KeyStringArray, KeyString);


#ifdef T_USER
#undef T_USER				// XXX FreeBSD defines this
#endif

/*
 * Parser tokens; also used to identify protocol commands/operations.
 */
enum Token {
    /*
     * Syntactic items.
     */
    T_SP,	T_CRLF,		T_COMMA,	T_STRING,	T_NUMBER,
    T_NIL,	T_LEXERR,
    /*
     * Commands.
     */
    T_ABOR,	 T_ACCT,	T_ADDMODEM,	T_ADDUSER,	T_ADMIN,
    T_ALLO,	 T_ANSWER,	T_APPE,		T_CHMOD,	T_CHOWN,
    T_CONFIG,	 T_CWD, 	T_CDUP,		T_DELE,		T_DELUSER,
    T_DELMODEM,	 T_DISABLE,	T_ENABLE,	T_FILEFMT,	T_FILESFMT,	T_FORM,
    T_HELP,	 T_IDLE,	T_LOCKWAIT,	T_JDELE,	T_JGDELE,	T_JGINTR,
    T_JGKILL,	 T_JGNEW,	T_JGPARM,	T_JGREST,	T_JGRP,
    T_JGSUB,	 T_JGSUSP,	T_JGWAIT,	T_JINTR,	T_JKILL,
    T_JNEW,	 T_JOB,		T_JOBFMT,	T_JOBSFMT,	T_JPARM,	T_JREST,
    T_JSUB,	 T_JSUSP,	T_JWAIT,	T_LIST,	 	T_MDTM,
    T_MODE,	 T_MODEMFMT,	T_MODEMSFMT,	T_NLST,		T_NOOP,		T_PASS,
    T_PASV,	 T_PORT,	T_PWD,		T_QUIT,		T_RCVFMT,	T_RCVSFMT,
    T_REIN,	 T_REST,	T_RETP,		T_RETR,		T_RNFR,
    T_RNTO,	 T_SHUT,	T_SITE,		T_SIZE,		T_STAT,
    T_STOR,	 T_STOT,	T_STOU,		T_STRU,		T_SYST,
    T_TRIGGER,	 T_TYPE,	T_TZONE,	T_USER,		T_VRFY,
    /*
     * Job state parameters.
     */
    T_ACCTINFO,	 T_BEGBR,	T_BEGST,	T_CHOPTHRESH,	T_CLIENT,
    T_COMMENTS,	 T_COMMID,	T_COVER,	T_DATAFORMAT,	T_DIALSTRING,
    T_DOCUMENT,	 T_DONEOP,	T_EXTERNAL,	T_FAXNUMBER,	T_FROM_COMPANY,	T_FROM_LOCATION,
    T_FROM_USER, T_FROM_VOICE,	T_GROUPID,	T_HRES,		T_JOBID,
    T_JOBINFO,	 T_JOBTYPE,	T_LASTTIME,	T_MAXDIALS,	T_MAXPAGES,
    T_MAXTRIES,	 T_MINBR,	T_MODEM,	T_NDIALS,	T_NOTIFY,
    T_NOTIFYADDR,T_NPAGES,	T_NTRIES,	T_OWNER,	T_PAGECHOP,
    T_PAGELENGTH,T_PAGEWIDTH,	T_PAGERANGE,	T_PASSWD,	T_POLL,		T_REGARDING,
    T_RETRYTIME, T_SCHEDPRI,	T_SENDTIME,	T_STATE,	T_STATUS,	T_STATUSCODE, T_ERRORCODE,
    T_SUBADDR,	 T_TAGLINE,	T_TOTDIALS,	T_TOTPAGES,	T_TOTTRIES,
    T_TO_COMPANY,T_TO_LOCATION,	T_TO_USER,	T_TO_VOICE,	T_TSI,		T_USE_CONTCOVER,
    T_USE_ECM,	 T_USE_TAGLINE,	T_USE_XVRES,	T_USRKEY,	T_VRES,
    /*
     * SNPP tokens.
     */
    T_2WAY,	 T_ACKREAD,	T_ALERT,	T_CALLERID,	T_COVERAGE,
    T_DATA,	 T_EXPTAG,	T_HOLDUNTIL,	T_JQUEUE,	T_KTAG,
    T_LEVEL,	 T_LOGIN,	T_MCRESPONSE,	T_MESSAGE,	T_MSTATUS,
    T_NOQUEUEING,T_PAGER,	T_PING,		T_RTYPE,	T_SEND,
    T_SUBJECT
};

struct tab {			// protocol command table entry
    const char*	name;
    Token token;
    bool checklogin;	// if true, must be logged in first
    bool implemented;	// false if command is not implemented
    const char*	help;
};

class SpoolDir;
struct ParamProtection;

struct stat;
typedef struct tiff TIFF;
class JobExt;
class ModemExt;
class ModemConfig;
class IDCache;

extern const char* fmtTime(time_t t);

/*
 * An instance of a fax server process.
 */
class HylaFAXServer : public SystemLog, public FaxConfig, public IOHandler {
public:
    struct stringtag {
        const char*	 name;
        fxStr HylaFAXServer::* p;
        const char*	 def;		// NULL is shorthand for ""
    };
    struct numbertag {
        const char*	 name;
        u_int HylaFAXServer::*p;
        u_int		 def;
    };
    struct booltag {
        const char*	 name;
        bool HylaFAXServer::*p;
        bool		 def;
    };
    static int _debugSleep;
protected:
    u_int	state;
#define	S_LOGGEDIN	0x0001		// client is logged in
#define	S_PRIVILEGED	0x0002		// client has administrative privileges
#define	S_LREPLIES	0x0004		// using continuation msgs in replies
#define	S_WAITFIFO	0x0008		// waiting on response to FIFO msg
#define	S_USEGMT	0x0010		// process times in GMT or local TZ
#define	S_WAITPASS	0x0020		// had user command, waiting for passwd
#define	S_TRANSFER	0x0040		// actively transferring data
#define	S_WAITDATA	0x0080		// scanner is waiting for data
#define	S_WAITTRIG	0x0100		// trigger is active
#define	S_LOGTRIG	0x0200		// write trigger events to data conn
#define	S_CHECKGID	0x0400		// check if file GID is set correctly
#define	S_SETGID	0x0800		// must explicitly force GID on files
    u_int	tracingLevel;		// server tracing control flags
    fxStr	logFacility;		// name of syslog facility for logging
    fxStr	userAccessFile;		// user access control file
    fxStr	xferfaxLogFile;		// log file for data transfers
    fxStr	faxContact;		// email account for inquiries
    fxStr	systemType;		// system ID string returned for SYST
    fxStr	faxqFIFOName;		// faxq FIFO name
    fxStr	clientFIFOName;		// client FIFO name
    int		faxqFd;			// faxq FIFO open descriptor
    int		clientFd;		// client FIFO open descriptor
    fxStr	fifoResponse;		// response received to FIFO msg
    u_int	idleTimeout;		// client inactivity timeout
    u_int	maxIdleTimeout;		// upper bound on idle timeout
    u_int	lockTimeout;		// qfile lock timeout
    u_int	maxLockTimeout;		// upper bound on qfile lock timeout
    int		data;			// current data connection (socket)
    int		pdata;			// passive mode data connect (socket)
    fxStr	hostname;		// name of machine server is running on
    fxStr	hostaddr;		// primary address for hostname
    fxStr	remotehost;		// name of peer's machine
    fxStr	remoteaddr;		// address of peer's machine
    fxStr	autospout;		// text to send with next reply
    fxStr	shutdownFile;		// file with shutdown information
    fxStr	shutdownMsg;		// text of shutdown message
    time_t	lastModTime;		// last mod. time of shutdown file
    time_t	lastTime;		// time of last shutdown notification
    time_t	discTime;		// time to disconnect service
    time_t	denyTime;		// time to deny service
    u_int	jobProtection;		// Protection to use on Jobs
    bool	allowSorting;		// Allow client to make us sort
    /*
     * User authentication and login-related state.
     */
    fxStr	passwd;			// encrypted user password
    fxStr	adminwd;		// encrypted passwd for admin privileges
    u_int	uid;			// client's ID
    u_int	loginAttempts;		// number of failed login attempts
    u_int	maxLoginAttempts;	// login failures before server exits
    u_int	adminAttempts;		// number of failed admin attempts
    u_int	maxAdminAttempts;	// admin failures before server exits
    fxStr	the_user;		// name of user
    fxStr	admingroup;			// name of local user group that is allowed
								// to administer the fax server
    IDCache*	idcache;		// fax UID -> name mapping table
    /*
     * File and file-transfer related state.
     */
    off_t	restart_point;		// file offset for restarting transfers
    jmp_buf	urgcatch;		// throw location for transfer interrupt
    off_t	file_size;		// size of file being transferred
    off_t	byte_count;		// amount of data currently sent
    int		xferfaxlog;		// open transfer log file
    int		mode;			// data transfer mode
    int		form;			// data transfer format
    int		type;			// data transfer type
    int		stru;			// file structure
    SpoolDir*	cwd;			// current working directory
    fxStrArray	tempFiles;		// files created with STOT
    fxStr	fileFormat;		// format string for directory listings
    fxStr	fileSortFormat;		// format string for directory listings
    TIFF*	cachedTIFF;		// cached open TIFF file
    /*
     * Parser-related state.
     */
    Token	pushedToken;		// lexical token push back
    fxStr	tokenBody;		// string body of current lexical token
    char	cbuf[512];		// current input line
    int		cpos;			// position in cbuf
    int		ctrlFlags;		// file descriptor flags for control
    int		recvCC;			// amount of data remaining in recvBuf
    int		recvNext;		// next byte for scanner
    char	recvBuf[1024];		// input data buffer
    u_int	consecutiveBadCmds;	// # consecutive invalid control cmds
    u_int	maxConsecutiveBadCmds;	// max # before forced disconnect
    /*
     * Job-related state.
     */
    Job		defJob;			// default job state information
    JobDict	jobs;			// non-default jobs
    Job*	curJob;			// current job
    fxStr	jobFormat;		// job status format string
    fxStr	jobSortFormat;		// job status format string
    JobDict	blankJobs;		// jobs created during this session but not submitted
    /*
     * Receive queue-related state.
     */
    RecvInfoDict recvq;			// cache of info about received fax
    fxStr	recvFormat;		// received fax status format string
    fxStr	recvSortFormat;		// received fax status format sort key
    /*
     * Trigger-related state.
     */
    fxStr	trigSpec;		// specification for active trigger
    u_int	tid;			// current active trigger ID
    /*
     * Modem-related state.
     */
    fxStr	modemFormat;		// modem status format string
    fxStr	modemSortFormat;		// modem status format string

    static gid_t faxuid;		// system gid of fax user = our uid
#if HAS_TM_ZONE
    const char*	tzname[2];		// local timezone name
#endif
    time_t	gmtoff;			// time_t offset btwn GMT and local time

    void userCmd(const char* name);	// USER
    void adminCmd(const char* name);	// ADMIN
    void passCmd(const char* passwd);	// PASS
    void statusCmd(void);		// STAT

    void formCmd(const char* name);	// FORM
    void formHelpCmd(void);		// FORM
    void typeCmd(const char* name);	// TYPE
    void modeCmd(const char* name);	// MODE
    void struCmd(const char* name);	// STRU
    void deleCmd(const char* name);	// DELE
    void mdtmCmd(const char* name);	// MDTM
    void cwdCmd(const char *path);	// CWD
    void pwdCmd(void);			// PWD
    void retrieveCmd(const char* name);	// RETR
    void retrievePageCmd(const char* name);// RETP
    void listCmd(const char* name);	// LIST
    void nlstCmd(const char* name);	// NLST
    void storeCmd(const char*, const char*);// STOR+APPE
    void storeUniqueCmd(bool isTemp);	// STOU+STOT
    void statFileCmd(const char* name);	// STAT
    void chownCmd(const char*, const char*);// CHOWN
    void chmodCmd(const char*, u_int);	// CHMOD

    virtual void passiveCmd(void) = 0;	// PASV: depends on transport
    virtual void portCmd(void) = 0;	// PORT: depends on transport

    void triggerCmd(const char*, ...);	// TRIGGER

    /*
     * Administrative commands (experimental).
     */
    void abortCallCmd(const char*);
    void addUserCmd(const char* spec, const char* pass, const char* apass);
    void delUserCmd(const char* spec);
    void answerCallCmd(const char* modem, const char* how);
    void disableModemCmd(const char* modem, const char* reason);
    void enableModemCmd(const char* mode);
    void shutCmd(const struct tm& when, const char* reason);
    void addModemCmd(const char* modem);
    void delModemCmd(const char* modem);
    void configQueryCmd(const char* where);
    void configCmd(const char* where, const char* info);

    virtual void initServer(void);
    bool readShutdownFile(void);
    bool isShutdown(bool quiet);
    void fatal(const char *fmt, ...);
    void reply(int code, const char* fmt, ...);
    void vreply(int code, const char* fmt, va_list ap);
    void lreply(int code, const char* fmt, ...);
    void vlreply(int code, const char* fmt, va_list ap);
    void perror_reply(int code, const char* string, int errnum);
    void ack(int code, const char*);
    void printTransferStatus(FILE* fd);
    struct tm* cvtTime(const time_t&) const;
    void setFileOwner(const char* filename);

    void loginRefused(const char* why);

    bool checkUser(const char*);
    bool checkPasswd(const char*);

    bool checkuserHosts(FILE*, const char *name);
    bool checkuserPAM(const char *name);

    bool checkpasswdHosts(const char* passwd);
    bool checkpasswdPAM(const char* passwd);

    bool isAdminGroup(const char* user=NULL);

    void login(int code);
    void end_login(void);
    virtual void dologout(int status);
    const char* fixPathname(const char* file);
    const char* userName(u_int uid);
    bool userID(const char*, u_int& id);
    void fillIDCache(void);

    bool cvtPasswd(const char* type, const char* pass, fxStr& result);
    bool findUser(FILE* db, const char* user, u_int& newuid);
    bool addUser(FILE* db, const char* user, u_int uid,
	const char* upass, const char* apass);
    bool deleteUser(FILE* db, const char* user);

    /*
     * Configuration file support.
     */
    static stringtag strings[];
    static numbertag numbers[];
    static booltag booleans[];

    void resetConfig();
    void setupConfig();
    void configError(const char* fmt, ...);
    void configTrace(const char* fmt, ...);
    bool setConfigItem(const char* tag, const char* value);

    bool restartSend(FILE* fd, off_t marker);

    static SpoolDir dirs[];

    bool checkFileRights(int op, const struct stat&);

    void dirSetup(void);
    static SpoolDir* dirLookup(const char* path);
    static SpoolDir* dirLookup(ino_t ino);
    SpoolDir* dirAccess(const char* path);
    SpoolDir* fileAccess(const char* path, int op, struct stat&);
    bool fileVisible(const SpoolDir&, const char*, const struct stat&);

    bool isVisibleRecvQFile(const char*, const struct stat&);
    void listRecvQ(FILE* fd, const SpoolDir& sd, DIR* dir);
    void listRecvQFile(FILE*, const SpoolDir&, const char*, const struct stat&);

    bool isVisibleSendQFile(const char*, const struct stat&);
    void listSendQ(FILE* fd, const SpoolDir& sd, DIR* dir);
    void listSendQFile(FILE*, const SpoolDir&, const char*, const struct stat&);
    void nlstSendQ(FILE* fd, const SpoolDir& sd, DIR* dir);
    void nlstSendQFile(FILE*, const SpoolDir&, const char*, const struct stat&);

    void listStatus(FILE* fd, const SpoolDir& sd, DIR* dir);
    void listStatusFile(FILE*, const SpoolDir&, const char*, const struct stat&);
    void nlstStatus(FILE* fd, const SpoolDir& sd, DIR* dir);

    bool isVisibletrue(const char*, const struct stat&);
    bool isVisibleDocQFile(const char*, const struct stat&);
    bool isVisibleRootFile(const char*, const struct stat&);

    void listDirectory(FILE* fd, const SpoolDir& sd, DIR* dir);
    void listUnixFile(FILE*, const SpoolDir&, const char*, const struct stat&);
    void makeProt(const struct stat& sb, bool withGrp, char prot[10]);
    void Fprintf(FILE*, const char* fmt, const char*, const struct stat&);
    void Fprintf(fxStackBuffer&, const char* fmt, const char*, const struct stat&);

    void nlstDirectory(FILE* fd, const SpoolDir& sd, DIR* dir);
    void nlstUnixFile(FILE*, const SpoolDir&, const char*, const struct stat&);

    virtual FILE* openDataConn(const char* mode, int& code) = 0;
    static const char* dataConnMsg(int code);
    virtual void closeDataConn(FILE*);

    bool sendData(FILE* fdin, FILE* fdout);
    bool sendIData(int fdin, int fdout);
    bool sendZData(int fdin, int fdout);
    bool recvData(FILE* instr, FILE* outstr);
    bool recvIData(int fdin, int fdout);
    bool recvZData(int fdin, int fdout);

    TIFF* openTIFF(const char* name);
    bool sendTIFFData(TIFF* tif, FILE* fdout);
    bool sendTIFFHeader(TIFF* tif, int fdout);
    bool sendITIFFData(TIFF* tif, int fdout);

    void logTransfer(const char*, const SpoolDir&, const char*, time_t);

    virtual int parse(void);
    bool cmd(Token t);
    bool site_cmd(Token t);
    bool param_cmd(Token t);
    bool string_param(fxStr&, const char* what = NULL);
    bool number_param(long&);
    bool boolean_param(bool&);
    bool file_param(fxStr& pathname);
    bool pwd_param(fxStr& s);
    bool timespec_param(int ndigits, time_t& t);
    bool pathname_param(fxStr& pathname);
    bool job_param(fxStr& jid);
    bool jgrp_param(fxStr& jgid);
    bool pathname(fxStr& s);
    bool CRLF();
    bool SPACE();
    bool COMMA();
    bool TIMESPEC(u_int len, time_t& result);
    bool BOOLEAN(bool& b);
    bool STRING(fxStr& s, const char* what = NULL);
    bool NUMBER(long& n);
    bool checkNUMBER(const char* s);
    bool opt_CRLF();
    bool opt_STRING(fxStr& s);
    bool multi_STRING(fxStr& s);
    static u_int twodigits(const char* cp, u_int range);
    static u_int fourdigits(const char* cp);
    virtual void syntaxError(const char* msg);

    virtual void netStatus(FILE*) = 0;	// depends on transport
    virtual bool hostPort() = 0;	// depends on transport

    int getChar(bool waitForInput);
    void pushCmdData(const char* data, int n);
    bool getCmdLine(char* s, int n, bool waitForInput = false);
    void pushToken(Token t);
    Token nextToken(void);
    bool checkToken(Token);
    bool getToken(Token, const char*);
    void helpCmd(const tab* ctab, const char* s);
    void logcmd(Token t, const char* fmt = NULL, ...);
    void cmdFailure(Token t, const char* why);
    bool checklogin(Token);
    bool checkadmin(Token);

    static const char* version;

    virtual const char* cmdToken(Token t);
    virtual const char* siteToken(Token t);
    static const char* parmToken(Token t);

    bool initClientFIFO(fxStr& emsg);
    int FIFOInput(int fd);
    void FIFOMessage(const char* cp, u_int len);
    bool sendModem(const char* modem, fxStr& emsg, const char* fmt ...);
    bool sendQueuerMsg(fxStr& emsg, const fxStr& msg);
    bool sendQueuer(fxStr& emsg, const char* fmt ...);
    bool sendQueuerACK(fxStr& emsg, const char* fmt, ...);
    bool vsendQueuerACK(fxStr& emsg, const char* fmt, va_list ap);

    bool newTrigger(fxStr& emsg, const char* fmt, ...);
    bool vnewTrigger(fxStr& emsg, const char* fmt, va_list ap);
    bool loadTrigger(fxStr& emsg);
    bool cancelTrigger(fxStr& emsg);
    void triggerEvent(const TriggerMsgHeader& h, const char* data);
    void logEventMsg(const TriggerMsgHeader&h, fxStr& msg);
    void logJobEventMsg(const TriggerMsgHeader&, const JobExt&);
    void logSendEventMsg(const TriggerMsgHeader&, const JobExt&, const char*);
    void logModemEventMsg(const TriggerMsgHeader&,
	const ModemExt&, const char*);
    void logRecvEventMsg(const TriggerMsgHeader&,
	const FaxRecvInfo&, const char*);

    virtual void initDefaultJob(void);
    void parmBotch(Token t);
    bool checkAccess(const Job& job, Token t, u_int op);
    bool checkParm(Job&, Token t, u_int op);
    bool checkJobState(Job*);
    void replyJobParamValue(Job&, int code, Token t);
    void replyBoolean(int code, bool b);
    bool setValue(u_short& v, const char* value, const char* what,
	const char* valNames[], u_int nValNames);
    void flushPreparedDocuments(Job& job);
    bool setJobParameter(Job&, Token t, const fxStr& value);
    bool setJobParameter(Job&, Token t, u_short value);
    bool setJobParameter(Job&, Token t, time_t value);
    bool setJobParameter(Job&, Token t, bool b);
    bool setJobParameter(Job&, Token t, float value);
    bool docType(const char* docname, FaxSendOp& op);
    bool checkAddDocument(Job&, Token type, const char* docname, FaxSendOp&);
    void addCoverDocument(Job&, const char* docname);
    void addDocument(Job&, const char* docname);
    void addPollOp(Job&, const char* sep, const char* pwd);
    void newJobCmd(void);
    bool newJob(fxStr& emsg);
    Job* findJob(const char* jobid, fxStr& emsg);
    Job* findJobInMemmory(const char* jobid);
    Job* findJobOnDisk(const char* jobid, fxStr& emsg);
    bool updateJobFromDisk(Job& job);
    void replyCurrentJob(const char* leader);
    void setCurrentJob(const char* jobid);
    Job* preJobCmd(const char* op, const char* jobid, fxStr& emsg);
    void operateOnJob(const char* jobid, const char* what, const char* op);
    void deleteJob(const char* jobid);
    void killJob(const char* jobid);
    void replyBadJob(const Job& job, Token t);
    void resetJob(const char* jobid);
    void interruptJob(const char* jobid);
    void suspendJob(const char* jobid);
    void submitJob(const char* jobid);
    void waitForJob(const char* jobid);
    bool updateJobOnDisk(Job& req, fxStr& emsg);
    bool lockJob(Job& job, int how, fxStr& emsg);
    bool lockJob(Job& job, int how);
    void unlockJob(Job& job);
    void purgeJobs(void);
    void jstatCmd(const Job&);
    void jstatLine(Token t, const char* fmt ...);

    const char* compactTime(time_t t);
    void Jprintf(FILE* fd, const char* fmt, const Job& job);
    void Jprintf(fxStackBuffer& buf, const char* fmt, const Job& job);

    u_int getJobNumber(fxStr&);
    u_int getDocumentNumber(fxStr&);

    bool getRecvDocStatus(RecvInfo& ri);
    RecvInfo* getRecvInfo(const fxStr& qfile, const struct stat& sb);
    const char* compactRecvTime(time_t t);
    void Rprintf(FILE*, const char*, const RecvInfo&, const struct stat&);
    void Rprintf(fxStackBuffer&, const char*, const RecvInfo&, const struct stat&);

    void getServerStatus(const char* fileName, fxStr& status);
    void Mprintf(FILE*, const char*, const ModemConfig&);
    void Mprintf(fxStackBuffer&, const char*, const ModemConfig&);
public:
    HylaFAXServer();
    virtual ~HylaFAXServer();

    static void setupPermissions(void);
    static void closeAllBut(int fd);
    static void closeAllDispatched();

    static void sanitize(fxStr& s);
    static void canonModem(fxStr& s);
    static void canonDevID(fxStr& s);

    virtual void open(void);
    virtual void close(void);

    virtual int inputReady(int);
    void timerExpired(long, long);
};
inline void HylaFAXServer::pushToken(Token t)		{ pushedToken = t; }

/*
 * Directories in the spooling area are treated
 * specially to hide implementation details and
 * privileged information that clients have no
 * business seeing.  Also we implement an access
 * control system that is built on top of the
 * normal UNIX protection mechanisms.
 */ 
struct SpoolDir {
    const char*	pathname;
    bool adminOnly;	// accessible by unprivileged clients
    bool storAble;	// unprivileged clients may STOR files
    bool deleAble;	// unprivileged clients may DELE files
    ino_t ino;		// directory inode number
    bool (HylaFAXServer::*isVisibleFile)(const char*, const struct stat&);
    void (HylaFAXServer::*listDirectory)(FILE*, const SpoolDir&, DIR*);
    void (HylaFAXServer::*listFile)(FILE*, const SpoolDir&,
        const char*, const struct stat&);
    void (HylaFAXServer::*nlstDirectory)(FILE*, const SpoolDir&, DIR*);
    void (HylaFAXServer::*nlstFile)(FILE*, const SpoolDir&,
        const char*, const struct stat&);
    void (HylaFAXServer::*delFile)(const SpoolDir&, const char*);
    void (HylaFAXServer::*retrFile)(const SpoolDir&, const char*);
    void (HylaFAXServer::*storFile)(const SpoolDir&, const char*);
};

#define	IS(x)	((state & (S_##x)) != 0)

#endif /* _HylaFAXServer_ */
