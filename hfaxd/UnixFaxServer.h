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
#ifndef _UnixFaxServer_
#define	_UnixFaxServer_

#include "HylaFAXServer.h"

#include <sys/un.h>

class UnixFaxServer : public HylaFAXServer {
private:
    sockaddr_un	ctrl_addr;		// control socket connection
    sockaddr_un	data_source;		// source of data connection
    sockaddr_un	data_dest;		// destination of data connection
    sockaddr_un	peer_addr;		// destination of control connection
    bool	usedefault;		// for data transfers

    void passiveCmd(void);
    void netStatus(FILE*);
    bool hostPort();
    void portCmd(void);

    virtual void initServer(void);

    FILE* getDataSocket(const char* mode);
    bool dataConnect(void);
    FILE* openDataConn(const char* mode, int& code);
    void send_data(FILE* instr, FILE* outstr, off_t blksize);
    bool receive_data(FILE* instr, FILE* outstr);
public:
    UnixFaxServer();
    virtual ~UnixFaxServer();

    virtual void open(void);
};

#include "SuperServer.h"

class UnixSuperServer : public SuperServer {
private:
    fxStr	fileName;		// UNIX domain socket filename
protected:
    bool startServer(void);
    HylaFAXServer* newChild(void);
public:
    UnixSuperServer(const char* filename, int backlog = 5);
    ~UnixSuperServer();
};
#endif /* _UnixFaxServer_ */
