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
#ifndef _Trigger_
#define	_Trigger_
/*
 * HylaFAX Trigger Support.
 */
#include "Str.h"
#include "Dictionary.h"
#include "QLink.h"

#define	TRIGGER_MAXTID	1024	// at most 1024 triggers can exist at one time
#define	TRIGGER_BPW	32	// we assume 8*sizeof (u_int) >= 32
#define	TRIGGER_MAXWDS	((TRIGGER_MAXTID+TRIGGER_BPW-1)/TRIGGER_BPW)

typedef struct {
    char	magic;		// ``!''
    char	reserved;	// reserved for future use
    u_short	seqnum;		// per-client message sequence number
    u_short	length;		// message length in bytes, including header
    u_short	event;		// event number
    time_t	tstamp;		// time message was crafted
} TriggerMsgHeader;

typedef unsigned int TriggerEvent;
typedef unsigned int trid_t;
class HylaClient;
class Job;
class Modem;
class fxStackBuffer;

/*
 * Triggers represent client interest for events
 * that occur within central scheduler (or which
 * are passed back from subprocesses for distribution).
 *
 * Events are broken into classes and interest can
 * be specified for specific events or for any events
 * within a class.  Similarly, interest can be constrained
 * to be specific to a particlar job or modem or for
 * any job or modem.
 *
 * Clients create triggers and receive notification
 * messages through FIFO special files whenever events
 * occur that match the event interests specified.
 */
class Trigger {
public:
    enum {
#define	JOB_BASE	0
	JOB_CREATE	= JOB_BASE+0,	// creation
	JOB_SUSPEND	= JOB_BASE+1,	// suspended
	JOB_READY	= JOB_BASE+2,	// ready to send
	JOB_SLEEP	= JOB_BASE+3,	// sleeping awaiting time-to-send
	JOB_DEAD	= JOB_BASE+4,	// marked dead
	JOB_PROCESS	= JOB_BASE+5,	// processed by scheduler
	JOB_REAP	= JOB_BASE+6,	// corpus reaped
	JOB_ACTIVE	= JOB_BASE+7,	// activated
	JOB_REJECT	= JOB_BASE+8,	// rejected
	JOB_KILL	= JOB_BASE+9,	// killed
	JOB_BLOCKED	= JOB_BASE+10,	// blocked by other job
	JOB_DELAYED	= JOB_BASE+11,	// delayed by tod restriction or similar
	JOB_ALTERED	= JOB_BASE+12,	// parameters altered
	JOB_TIMEDOUT	= JOB_BASE+13,	// kill timer expired
	JOB_PREP_BEGIN	= JOB_BASE+14,	// preparation started
	JOB_PREP_END	= JOB_BASE+15,	// preparation finished

#define	SEND_BASE	16
	SEND_BEGIN	= SEND_BASE+0,	// fax, send attempt started
	SEND_CALL	= SEND_BASE+1,	// fax, call placed
	SEND_CONNECTED	= SEND_BASE+2,	// fax, call answered by fax
	SEND_PAGE	= SEND_BASE+3,	// fax, page done
	SEND_DOC	= SEND_BASE+4,	// fax, document done
	SEND_POLLRCVD	= SEND_BASE+5,	// fax, document retrieved by poll op
	SEND_POLLDONE	= SEND_BASE+6,	// fax, poll op complete
	SEND_END	= SEND_BASE+7,	// fax, send attempt finished
	SEND_REFORMAT	= SEND_BASE+8,	// fax, job being reformatted
	SEND_REQUEUE	= SEND_BASE+9,	// fax, job requeued
	SEND_DONE	= SEND_BASE+10,	// fax, send job done

#define	RECV_BASE	32
	RECV_BEGIN	= RECV_BASE+0,	// fax, inbound call started
	RECV_START	= RECV_BASE+1,	// fax, session started
	RECV_PAGE	= RECV_BASE+2,	// fax, page done
	RECV_DOC	= RECV_BASE+3,	// fax, document done
	RECV_END	= RECV_BASE+4,	// fax, inbound call finished

#define	MODEM_BASE	48
	MODEM_ASSIGN	= MODEM_BASE+0,	// modem assigned to job
	MODEM_RELEASE	= MODEM_BASE+1,	// modem released by job
	MODEM_DOWN	= MODEM_BASE+2,	// modem marked down
	MODEM_READY	= MODEM_BASE+3,	// modem marked ready
	MODEM_BUSY	= MODEM_BASE+4,	// modem marked busy
	MODEM_WEDGED	= MODEM_BASE+5,	// modem considered wedged
	MODEM_INUSE	= MODEM_BASE+6,	// modem inuse for outbound work
	MODEM_DATA_BEGIN= MODEM_BASE+7,	// inbound data call begun
	MODEM_DATA_END	= MODEM_BASE+8,	// inbound data call finished
	MODEM_VOICE_BEGIN= MODEM_BASE+9,// inbound voice call begun
	MODEM_VOICE_END	= MODEM_BASE+10,// inbound voice call finished
	MODEM_CID	= MODEM_BASE+11	// inbound caller-ID information
    };
#define	TRIGGER_MAXEVENT (MODEM_BASE+16)
private:
    trid_t	tid;			// trigger ID
    u_int	refs;			// count of references
    u_short	interests[TRIGGER_MAXEVENT>>4];// bitmask of event interests
    HylaClient*	client;			// reference to client

    static Trigger* triggers[TRIGGER_MAXTID];
    static QLink wildcards[TRIGGER_MAXEVENT>>4];// for wildcard matches

    static u_int tidFree[TRIGGER_MAXWDS];
    static u_int tidRotor;
    static trid_t tidNextFree();

    friend class TriggerRef;		// for access to reference count

    Trigger(trid_t, const fxStr& fifoName);

    bool parse(const char*);		// parse trigger spec
    static void syntaxError(const char* spec, const char* msg);

    void purgeWildRefs();		// purge all wildcard references
    bool cancel();

    static void post(TriggerEvent, const QLink&, const QLink&, fxStackBuffer&);
    void send(const char* fmt ...);
public:
    ~Trigger();

    static void purgeClient(HylaClient*);// purge all triggers for client

    static void create(const fxStr& id, const char* spec);
    static bool cancel(const char* tid);

    static void post(TriggerEvent, const Job&, const char* = NULL);
    static void post(TriggerEvent, const Modem&, const char* = NULL);
};
#endif /* _Trigger_ */
