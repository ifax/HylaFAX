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
#ifndef _OldProtocol_
#define	_OldProtocol_

#include "InetFaxServer.h"

struct passwd;
struct code_ent;
struct stat;

class OldProtocolServer : public InetFaxServer {
public:
    struct protoCmd {
	const char* cmd;		// command to match
	bool	check;			// if true, checkPermission first
	void (OldProtocolServer::*cmdFunc)(const char*);
    };
private:
    faxRequestArray reqs;
    char	line[1024];		// current input line
    int		version;
    bool	alreadyChecked;
    code_ent*	codetab;
    fxStr	modem;			// selected modem

    static const protoCmd cmds[];

    bool checkHostIdentity(hostent*& hp);
    void setupNetwork(int fd);
    void doProtocol(void);
    void getCommandLine(char line[1024], char*& tag);
    u_long cvtTime(const char* spec, const struct tm* ref, const char* what);
    void vsendClient(const char* tag, const char* fmt, va_list ap);
    void sendClient(const char* tag, const char* fmt, ...);
    void sendError(const char* fmt, ...);
    void sendAndLogError(const char* fmt, ...);
    void protocolBotch(const char* fmt, ...);
    void setUserID(const char* tag);
    void newPollID(const char* tag);
    void setProtoVersion(const char* tag);
    void ackPermission(const char*);
    bool _checkUser(const char* requestor, struct passwd* pwd);
    bool isAdmin(const char* requestor);
    void applyToJob(const char* tag, const char* op,
	void (OldProtocolServer::*f)(Job&, const char*));
    void applyToJobGroup(const char* tag, const char* op,
	void (OldProtocolServer::*f)(Job&, const char*));
    bool alterSuspend(Job&);
    void alterResubmit(Job&);
    void alterJobTTS(const char*);
    void alterJobGroupTTS(const char*);
    void reallyAlterJobTTS(Job& job, const char* spec);
    void alterJobKillTime(const char*);
    void alterJobGroupKillTime(const char*);
    void reallyAlterJobKillTime(Job& job, const char* spec);
    void alterJobModem(const char*);
    void alterJobGroupModem(const char*);
    void reallyAlterJobModem(Job& job, const char* device);
    void alterJobPriority(const char*);
    void alterJobGroupPriority(const char*);
    void reallyAlterJobPriority(Job& job, const char* priority);
    void alterJobMaxDials(const char*);
    void alterJobGroupMaxDials(const char*);
    void reallyAlterJobMaxDials(Job& job, const char* max);
    void alterJobNotification(const char*);
    void alterJobGroupNotification(const char*);
    void reallyAlterJobNotification(Job& job, const char* note);
    void reallyRemoveJob(const char* op, Job& job);
    void removeJob(const char*);
    void removeJobGroup(const char*);
    void killJob(const char*);
    void killJobGroup(const char*);
    void doremove(Job& job, const char*);
    void dokill(Job& job, const char*);
    void submitJob(const char*);
    void coverProtocol(int isLZW, int cc);
    void setupData(void);
    const char* fileDesc(FaxSendOp type);
    fxStr dataTemplate(FaxSendOp type, int&);
    void getData(FaxSendOp type, long cc);
    void getLZWData(FaxSendOp type, long cc);
    void setupLZW();
    long decodeLZW(FILE* fin, FILE* fout);
    void getTIFFData(const char* tag);
    void getPostScriptData(const char* tag);
    void getOpaqueData(const char* tag);
    void getZPostScriptData(const char* tag);
    void getZOpaqueData(const char* tag);
    void dologout(int status);
    bool modemMatch(const fxStr& a, const fxStr& b);
    void sendServerStatus(const char*);
    void sendServerInfo1(const char* name);
    void sendServerInfo(const char*);
    void sendClientJobStatus(const Job& job);
    void sendClientJobLocked(const Job& job);
    void sendJobStatus(Job& job);
    void sendAllStatus(const char*);
    void sendJobStatus(const char* jobid);
    void sendUserStatus(const char* sender);
    void sendRecvStatus(const char*);
public:
    OldProtocolServer();
    OldProtocolServer(const char* port);
    virtual ~OldProtocolServer();

    static OldProtocolServer& instance();

    virtual void open(void);
};

#include "SuperServer.h"

class OldProtocolSuperServer : public SuperServer {
private:
    fxStr port;
protected:
    bool startServer(void);
    HylaFAXServer* newChild(void);
public:
    OldProtocolSuperServer(const char* port, int backlog = 5);
    ~OldProtocolSuperServer();
};
#endif /* _OldProtocolServer_ */
