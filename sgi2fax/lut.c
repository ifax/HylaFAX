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
#include "lut.h"

float frand();

lut *makelut(func,insteps,outsteps,stoclut)
float (*func)();
int insteps, outsteps, stoclut;
{
    lut *l;
    int i;
    float low, high, inspace;

    l = (lut *)malloc(sizeof(lut));
    l->insteps = insteps;
    l->outsteps = outsteps;
    l->stoclut = stoclut;
    if(stoclut) {
        l->flow = (float *)malloc(insteps*sizeof(float));
        l->fhigh = (float *)malloc(insteps*sizeof(float));
        inspace = insteps-1.0;
        for(i=0; i<insteps; i++) {
            low = (i-0.5)/inspace;
            high = (i+0.5)/inspace;
            if(low<0.0)
                low = 0.0;
            if(high>1.0)
                high = 1.0;
            l->flow[i] = (func)(low);
            l->fhigh[i] = (func)(high);
        }
    } else { 
        l->stab = (unsigned short *)malloc(insteps*sizeof(unsigned short));
        inspace = insteps-1.0;
        for(i=0; i<insteps; i++) 
            l->stab[i] = (l->outsteps-1)*(func)(i/inspace)+0.5;
    }
    return l;
}

applylut(l,sptr,n)
lut *l;
unsigned short *sptr;
int n;
{
    float delta, val;
    float *fhigh, *flow;
    unsigned short *stab;
    float outspace;
    int ival;

    if(l->stoclut) {
        fhigh = l->fhigh;
        flow = l->flow;
        outspace = l->outsteps-1;
        while(n--) {
            delta = fhigh[*sptr]-flow[*sptr];
            val = outspace*(flow[*sptr]+frand()*delta);
            ival = val;
            if((val-ival)<frand()) 
                *sptr++ = ival;
            else
                *sptr++ = ival+1;
        }
    } else {
        stab = l->stab;
        while(n--) {
            *sptr = stab[*sptr];
            sptr++;
        }
    }
}

void freelut(lut* l)
{
    if(l->stoclut) {
        free((char*)l->flow);
        free((char*)l->fhigh);
    } else { 
        free((char*)l->stab);
    }
    free((char*)l);
}
