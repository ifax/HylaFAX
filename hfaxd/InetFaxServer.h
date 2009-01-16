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
#ifndef _InetFaxServer_
#define	_InetFaxServer_

#include "HylaFAXServer.h"
extern "C" {
#include <netinet/in.h>
}

#include "Socket.h"

struct hostent;

class InetFaxServer : public HylaFAXServer {
protected:
    Socket::Address	ctrl_addr;		// local address of control
    Socket::Address	peer_addr;		// remote address of control
    Socket::Address	data_source;		// source of data connection
    Socket::Address	data_dest;		// destination of data connection
    Socket::Address	pasv_addr;		// local end of passive connections
    bool	usedefault;		// for data transfers
    /*
     * Timeout intervals for retrying connections
     * to hosts that don't accept PORT cmds.
     */
    int		swaitmax;		// wait at most 90 seconds
    int		swaitint;		// interval between retries

    bool isLocalDomain(const fxStr& h);
    bool checkHostIdentity(const char* hostname);
    void setupNetwork(int fd);
    void handleUrgentData(void);
    void passiveCmd(void);

    static void sigURG(int);
    static void sigPIPE(int);

    virtual void initServer(void);

    void netStatus(FILE*);
    void printaddr(FILE*, const char* leader, const struct sockaddr_in& sin);
    bool hostPort();
    void portCmd(Token);

    bool dataConnect(void);
    FILE* getDataSocket(const char* mode);
    FILE* openDataConn(const char* mode, int& code);

    void lostConnection(void);

    static InetFaxServer* _instance;
public:
    InetFaxServer();
    virtual ~InetFaxServer();

    static InetFaxServer& instance();

    virtual void open(void);
};

#include "SuperServer.h"

class InetSuperServer : public SuperServer {
private:
    fxStr port;

    /*
     * The address to bind the port on.
     */
    const char *bindaddress;

protected:
    bool startServer(void);
    HylaFAXServer* newChild(void);
public:
    InetSuperServer(const char* port, int backlog = 5);
    ~InetSuperServer();
    void setBindAddress(const char *bindaddress = NULL);
};
#endif /* _InetFaxServer_ */
