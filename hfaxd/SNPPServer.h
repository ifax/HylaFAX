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
#ifndef _SNPPServer_
#define	_SNPPServer_

#include "InetFaxServer.h"

/*
 * An instance of an SNPP process.
 */
class SNPPServer : public InetFaxServer {
private:
    fxStr	msgFile;		// file with message text
    bool	haveText;		// client specified message text
    fxStrArray	msgs;			// jobs pending SEND (jobids)
#define	S_SENDWAIT	0x10000		// in SEND waiting for jobs

    fxStr	pagerIDMapFile;		// file with pager ID mapping info
    u_int	maxMsgLength;		// maximum length of pager message text
    u_short	priMap[12];		// service level -> priority map
    time_t	retryMap[12];		// service level -> retrytime map
    time_t	killMap[12];		// service level -> killtime map

    void initSNPPJob(void);
protected:
    virtual void initServer(void);
    virtual void initDefaultJob(void);
    virtual void resetState(void);
    virtual void dologout(int status);

    virtual void resetConfig();
    void setupConfig();
    virtual bool setConfigItem(const char* tag, const char* value);

    int parse(void);
    bool cmd(Token t);
    bool site_cmd(Token t);
    bool checklogin(Token);
    bool SNPPTime(time_t& result);
    virtual void syntaxError(const char* msg);

    virtual const char* cmdToken(Token t);
    virtual const char* siteToken(Token t);

    void dataCmd(void);
    void helpCmd(const tab* ctab, const char* s);
    void holdCmd(time_t when);
    void loginCmd(const char* loginID, const char* pwd = "");
    void messageCmd(const char* msg);
    void pagerCmd(const char* pagerID, const char* pin = NULL);
    void pingCmd(const char* pagerID);
    void sendCmd(void);
    void serviceLevel(u_int level);
    void statusCmd(void);
    void subjectCmd(const char*);

    bool mapPagerID(const char* pagerID,
	fxStr& number, fxStr& pid, fxStr& emsg);
public:
    SNPPServer();
    SNPPServer(const char* port);
    virtual ~SNPPServer();

    virtual void open(void);
};

class SNPPSuperServer : public SuperServer {
private:
    fxStr port;
protected:
    bool startServer(void);
    HylaFAXServer* newChild(void);
public:
    SNPPSuperServer(const char* port, int backlog = 5);
    ~SNPPSuperServer();
};
#endif /* _SNPPServer_ */
