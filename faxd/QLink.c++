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
#include "QLink.h"
#include "Types.h"

QLink::QLink() { next = prev = this; }
QLink::QLink(const QLink& other) { next = other.next; prev = other.prev; }
QLink::~QLink() {}

/*
 * Insert a QLink item in a doubly-linked list
 * immediately after the specified item.
 */
void
QLink::insert(QLink& after)
{
    fxAssert(next == this, "QLink::insert: item already on a list");
    next = &after;
    prev = after.prev;
    after.prev->next = this;
    after.prev = this;
}

/*
 * Remove an item from a doubly-linked list.
 */
void
QLink::remove(void)
{
    fxAssert(next != this, "QLink::remove: item not on a list");
    next->prev = prev;
    prev->next = next;
    next = this;			// indicates job is not on list
}
