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
#ifndef _HylaClient_
#define	_HylaClient_
/*
 * HylaFAX Client Support.
 */
#include "Str.h"
#include "IOHandler.h"
#include "Dictionary.h"

typedef unsigned short tseq_t;
class HylaClient;

fxDECLARE_StrKeyDictionary(HylaClientDict, HylaClient*)

class HylaClient {
public:
    class SchedReaper : public IOHandler {
    private:
	bool	started;
    public:
	SchedReaper();
	~SchedReaper();
	void timerExpired(long, long);

	void start();
    };
private:
    int		fifo;		// cached open file descriptor
    fxStr	fifoName;	// associated FIFO filename
    u_short	refs;		// # of Triggers referencing this client
    tseq_t	seqnum;		// per-client message sequence number
    tseq_t	lrunum;		// sequence number for LRU reclaim of fd's
    bool	reap;		// client should be reaped

    static tseq_t lruseq;		// master sequence # generator for LRU
    static HylaClientDict clients;	// master table of clients
    static SchedReaper schedReaper;	// hook for reclaiming dead clients

    friend class HylaClient::SchedReaper;
    friend class Trigger;

    HylaClient(const fxStr& fifoName);

    static bool reapFIFO();
    void schedReap();
public:
    ~HylaClient();

    static HylaClient& getClient(const fxStr& fifoName);
    static void purge();

    void inc();
    void dec();

    tseq_t getSeqnum() const;
    bool send(const char* msg, u_int msgLen);
};
inline void HylaClient::inc()		{ refs++; }
inline void HylaClient::dec()		{ if (--refs == 0) delete this; }
inline tseq_t HylaClient::getSeqnum() const { return seqnum; }
#endif /* _HylaClient_ */
