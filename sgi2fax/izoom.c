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
/*
 *      izoom- 
 *              Magnify or minify a picture with or without filtering.  The 
 *      filtered method is one pass, uses 2-d convolution, and is optimized 
 *      by integer arithmetic and precomputation of filter coeffs.
 *
 *                              Paul Haeberli - 1988
 */
#include "stdio.h"
#include "math.h"
#include "assert.h"
#include "izoom.h"

#define SHIFT           12
#define ONE             (1<<SHIFT)
#define EPSILON         0.0001

static FILTER *makefilt();
static float filterrad();
static float filterinteg();
static float mitchell();
static int (*xfiltfunc)(); 

static makexmap();
static xscalebuf();
static addrow();
static divrow();
static freefilt();
static applyxfilt();

static int filtershape;
static float blurfactor;
static mults;

#define GRIDTOFLOAT(pos,n)      (((pos)+0.5)/(n))
#define FLOATTOGRID(pos,n)      ((pos)*(n))

copyimage(getfunc,putfunc,nx,ny)
int (*getfunc)(), (*putfunc)();
int nx ,ny;
{
    int y;
    short *abuf;

    abuf = (short *)malloc(nx*sizeof(short));
    for(y=0; y<ny; y++) {
        getfunc(abuf,y);
        putfunc(abuf,y);
    }
    free(abuf);
}

/*
 *      general zoom follows
 *
 */
zoom *newzoom(getfunc,anx,any,bnx,bny,filttype,blur)
int (*getfunc)();
int anx,any,bnx,bny;
int filttype;
float blur;
{
    zoom *z;
    int i;
    int xmults, ymults;

    z = (zoom *)malloc(sizeof(zoom));
    z->getfunc = getfunc;
    z->abuf = (short *)malloc(anx*sizeof(short));
    z->bbuf = (short *)malloc(bnx*sizeof(short));
    z->anx = anx;
    z->any = any;
    z->bnx = bnx;
    z->bny = bny;
    z->curay = -1;
    z->y = 0;
    z->type = filttype;
    if(filttype == IMPULSE) {
        if(z->anx != z->bnx) {
            z->xmap = (short **)malloc(z->bnx*sizeof(short *));
            makexmap(z->abuf,z->xmap,z->anx,z->bnx);
        }
    } else {
        filtershape = filttype;
        blurfactor = blur;
        if(filtershape == MITCHELL) 
            z->clamp = 1;
        else
            z->clamp = 0;
        z->tbuf = (short *)malloc(bnx*sizeof(short));
        z->xfilt = makefilt(z->abuf,anx,bnx,&z->nrows);
        xmults = any*mults;
        z->yfilt = makefilt(0,any,bny,&z->nrows);
        ymults = bnx*mults;
#ifdef notdef
        fprintf(stderr,"TOTAL MULTS: %d\n",xmults+ymults);
#endif
        z->filtrows = (short **)malloc(z->nrows * sizeof(short *));
        for(i=0; i<z->nrows; i++) 
            z->filtrows[i] = (short *)malloc(z->bnx*sizeof(short));
        z->accrow = (int *)malloc(z->bnx*sizeof(int));
        z->ay = 0;
    }
    return z;
}

getzoomrow(z,buf,y)
zoom *z;
short *buf;
int y;
{
    float fy;
    int ay;
    FILTER *f;
    int i, max;
    short *row;

    if(y==0) {
        z->curay = -1;
        z->y = 0;
        z->ay = 0;
    }
    if(z->type == IMPULSE) {
        fy = GRIDTOFLOAT(z->y,z->bny);
        ay = FLOATTOGRID(fy,z->any);
        if(z->anx == z->bnx) {
            if(z->curay != ay) {
                z->getfunc(z->abuf,ay);
                z->curay = ay;
                if(xfiltfunc) 
                    xfiltfunc(z->abuf,z->bnx);
            }
            memcpy(buf,z->abuf,z->bnx*sizeof(short)); 
        } else {
            if(z->curay != ay) {
                z->getfunc(z->abuf,ay);
                xscalebuf(z->xmap,z->bbuf,z->bnx);
                z->curay = ay;
                if(xfiltfunc)
                    xfiltfunc(z->bbuf,z->bnx);
            }
            memcpy(buf,z->bbuf,z->bnx*sizeof(short)); 
        }
    } else if(z->any == 1 && z->bny == 1) {
            z->getfunc(z->abuf,z->ay++);
            applyxfilt(z->filtrows[0],z->xfilt,z->bnx);
            if(xfiltfunc)
                xfiltfunc(z->filtrows[0],z->bnx);
            if(z->clamp) {
                clamprow(z->filtrows[0],z->tbuf,z->bnx);
                memcpy(buf,z->tbuf,z->bnx*sizeof(short)); 
            } else {
                memcpy(buf,z->filtrows[0],z->bnx*sizeof(short)); 
            }
    } else {
        f = z->yfilt+z->y;
        max = ((int)f->dat)/sizeof(short)+(f->n-1);
        while(z->ay<=max) {
            z->getfunc(z->abuf,z->ay++);
            row = z->filtrows[0];
            for(i=0; i<(z->nrows-1); i++) 
                z->filtrows[i] = z->filtrows[i+1];
            z->filtrows[z->nrows-1] = row;
            applyxfilt(z->filtrows[z->nrows-1],z->xfilt,z->bnx);
            if(xfiltfunc)
                xfiltfunc(z->filtrows[z->nrows-1],z->bnx);
        }
        if(f->n == 1) {
            if(z->clamp) {
                clamprow(z->filtrows[z->nrows-1],z->tbuf,z->bnx);
                memcpy(buf,z->tbuf,z->bnx*sizeof(short)); 
            } else {
                memcpy(buf,z->filtrows[z->nrows-1],z->bnx*sizeof(short)); 
            }
        } else {
            memset(z->accrow,0,z->bnx*sizeof(int));
            for(i=0; i<f->n; i++) 
                addrow(z->accrow, z->filtrows[i+(z->nrows-1)-(f->n-1)],
                                                          f->w[i],z->bnx);
            divrow(z->accrow,z->bbuf,f->totw,z->bnx);
            if(z->clamp) {
                clamprow(z->bbuf,z->tbuf,z->bnx);
                memcpy(buf,z->tbuf,z->bnx*sizeof(short)); 
            } else {
                memcpy(buf,z->bbuf,z->bnx*sizeof(short)); 
            }
        }
    }
    z->y++;
}

freezoom(z)
zoom *z;
{
    int i;

    if(z->type == IMPULSE) {
        if(z->anx != z->bnx)
            free(z->xmap);
    } else {
        freefilt(z->xfilt,z->bnx);
        freefilt(z->yfilt,z->bny);
        free(z->tbuf);
        for(i=0; i<z->nrows; i++)
            free(z->filtrows[i]);
        free(z->filtrows);
        free(z->accrow);
    }
    free(z->abuf);
    free(z->bbuf);
    free(z);

}

filterzoom(getfunc,putfunc,anx,any,bnx,bny,filttype,blur)
int (*getfunc)(), (*putfunc)();
int anx, any;
int bnx, bny;
int filttype;
float blur;
{
    zoom *z;
    int y;
    short *buf;

    buf = (short *)malloc(bnx*sizeof(short));
    z = newzoom(getfunc,anx,any,bnx,bny,filttype,blur);
    for(y=0; y<bny; y++) {
        getzoomrow(z,buf,y);
        putfunc(buf,y);
    }
    freezoom(z);
    free(buf);
}

/*
 *      impulse zoom utilities
 *
 */
static makexmap(abuf,xmap,anx,bnx)
short *abuf;
short *xmap[];
int anx, bnx;
{
    int x, ax;
    float fx;

    for(x=0; x<bnx; x++) {
       fx = GRIDTOFLOAT(x,bnx);
       ax = FLOATTOGRID(fx,anx);
       xmap[x] = abuf+ax;
    }
}

static xscalebuf(xmap,bbuf,bnx)
short *xmap[];
short *bbuf;
int bnx;
{
    while(bnx>=8) {
        bbuf[0] = *(xmap[0]);
        bbuf[1] = *(xmap[1]);
        bbuf[2] = *(xmap[2]);
        bbuf[3] = *(xmap[3]);
        bbuf[4] = *(xmap[4]);
        bbuf[5] = *(xmap[5]);
        bbuf[6] = *(xmap[6]);
        bbuf[7] = *(xmap[7]);
        bbuf += 8;
        xmap += 8;
        bnx -= 8;
    }
    while(bnx--) 
        *bbuf++ = *(*xmap++);
}

zoomxfilt(filtfunc)
int (*filtfunc)(); 
{
    xfiltfunc = filtfunc;
}

/*
 *      filter zoom utilities
 *
 */
static addrow(iptr,sptr,w,n)
int *iptr;
short *sptr;
int w, n;
{
    while(n>=8) {
        iptr[0] += (w*sptr[0]);
        iptr[1] += (w*sptr[1]);
        iptr[2] += (w*sptr[2]);
        iptr[3] += (w*sptr[3]);
        iptr[4] += (w*sptr[4]);
        iptr[5] += (w*sptr[5]);
        iptr[6] += (w*sptr[6]);
        iptr[7] += (w*sptr[7]);
        iptr += 8;
        sptr += 8;
        n -= 8;
    }
    while(n--) 
        *iptr++ += (w * *sptr++);
}

static divrow(iptr,sptr,tot,n)
int *iptr;
short *sptr;
int tot, n;
{
    while(n>=8) {
        sptr[0] = iptr[0]/tot;
        sptr[1] = iptr[1]/tot;
        sptr[2] = iptr[2]/tot;
        sptr[3] = iptr[3]/tot;
        sptr[4] = iptr[4]/tot;
        sptr[5] = iptr[5]/tot;
        sptr[6] = iptr[6]/tot;
        sptr[7] = iptr[7]/tot;
        sptr += 8;
        iptr += 8;
        n -= 8;
    }
    while(n--)
        *sptr++ = (*iptr++)/tot;
}

static FILTER *makefilt(abuf,anx,bnx,maxn)
short *abuf;
int anx, bnx;
int *maxn;
{
    FILTER *f, *filter;
    int x, n;
    float bmin, bmax, bcent, brad;
    float fmin, fmax, acent, arad;
    int amin, amax;
    float cover;

    f = filter = (FILTER *)malloc(bnx*sizeof(FILTER));
    *maxn = 0;
    mults = 0;
    for(x=0; x<bnx; x++) {
        if(bnx<anx) {
            brad = filterrad()/bnx;
            bcent = ((float)x+0.5)/bnx;
            amin = floor((bcent-brad)*anx+EPSILON);
            amax = floor((bcent+brad)*anx-EPSILON);
            if(amin<0)
               amin = 0;
            if(amax>=anx)
               amax = anx-1;
            f->n = 1+amax-amin;
            mults += f->n;
            f->dat = abuf+amin;
            f->w = (short *)malloc(f->n*sizeof(short));
            f->totw = 0;
            for(n=0; n<f->n; n++) {
                bmin = bnx*((((float)amin+n)/anx)-bcent);
                bmax = bnx*((((float)amin+n+1)/anx)-bcent);
                cover = filterinteg(bmax,1)-filterinteg(bmin,0);  
                f->w[n] = (ONE*cover)+0.5;
                f->totw += f->w[n];
            }
        } else {
            arad = filterrad()/anx;
            bmin = ((float)x)/bnx;
            bmax = ((float)x+1.0)/bnx;
            amin = floor((bmin-arad)*anx+0.5+EPSILON);
            amax = floor((bmax+arad)*anx-0.5-EPSILON);
            if(amin<0)
               amin = 0;
            if(amax>=anx)
               amax = anx-1;
            f->n = 1+amax-amin;
            mults += f->n;
            f->dat = abuf+amin;
            f->w = (short *)malloc(f->n*sizeof(short));
            f->totw = 0;
            for(n=0; n<f->n; n++) {
                acent = (amin+n+0.5)/anx;
                fmin = anx*(bmin-acent);
                fmax = anx*(bmax-acent);
                cover = filterinteg(fmax,1)-filterinteg(fmin,0);  
                f->w[n] = (ONE*cover)+0.5;
                f->totw += f->w[n];
            }
        }
        if(f->n>*maxn)
            *maxn = f->n;
        f++;
    }
    return filter;
}

static freefilt(filt,n)
FILTER *filt;
int n;
{
    FILTER *f;

    f = filt;
    while(n--) {
        free(f->w);
        f++;
    }
    free(filt);
}

static applyxfilt(bbuf,xfilt,bnx)
short *bbuf;
FILTER *xfilt;
int bnx;
{
    short *w;
    short *dptr;
    int n, val;

    while(bnx--) {
        if((n=xfilt->n) == 1) {
            *bbuf++ = *xfilt->dat;
        } else {
            w = xfilt->w;
            dptr = xfilt->dat;
            val = 0;
            n = xfilt->n;
            while(n--) 
                 val += *w++ * *dptr++;
            *bbuf++ = val / xfilt->totw;
        }
        xfilt++;
    }
}

static float filterrad()
{
    switch(filtershape) {
        case BOX:
            return 0.5*blurfactor;
        case TRIANGLE:
            return 1.0*blurfactor;
        case QUADRATIC:
            return 1.0*blurfactor;
        case MITCHELL:
            return 2.0*blurfactor;
    }
    /*NOTREACHED*/
}

static float quadinteg(x)
float x;
{
   if(x<-1.0)
       return 0.0;
   if(x<-0.5)
       return 2.0*((1.0/3.0)*(x*x*x+1.0) + x*x + x);
   else
       return -(2.0/3.0)*x*x*x + x + 0.5;
}

static float filterinteg(x,side)
float x;
int side;
{
    float val;

    x = x/blurfactor;
    switch(filtershape) {
        case BOX:
            if(x<-0.5)
                return 0.0;
            else if(x>0.5)
                return 1.0;
            else
                return x+0.5;
        case TRIANGLE:
            if(x<-1.0)
                return 0.0;
            else if(x>1.0)
                return 1.0;
            else if(x<0.0) {
                val = x+1.0;
                return 0.5*val*val;
            } else {
                val = 1.0-x;
                return 1.0-0.5*val*val;
            }
        case QUADRATIC:
            if(x<0.0)
                return quadinteg(x);
            else
                return 1.0-quadinteg(-x);
        case MITCHELL:
            if(side == 0)
                return 0.0;
            return mitchell(x);
    }
    /*NOTREACHED*/
}

static float p0, p2, p3, q0, q1, q2, q3;

/*
 * see Mitchell&Netravali, "Reconstruction Filters in Computer Graphics",
 * SIGGRAPH 88.  Mitchell code provided by Paul Heckbert.
 */
static mitchellinit(b,c)
float b, c;
{

    p0 = (  6. -  2.*b        ) / 6.;
    p2 = (-18. + 12.*b +  6.*c) / 6.;
    p3 = ( 12. -  9.*b -  6.*c) / 6.;
    q0 = (           8.*b + 24.*c) / 6.;
    q1 = (        - 12.*b - 48.*c) / 6.;
    q2 = (           6.*b + 30.*c) / 6.;
    q3 = (     -     b -  6.*c) / 6.;
}

static float mitchell(x)        /* Mitchell & Netravali's two-param cubic */
float x;
{
    static int firsted;

    if(!firsted) {
        mitchellinit(1.0/3.0,1.0/3.0);
        firsted = 1;
    }
    if (x<-2.) return 0.0;
    if (x<-1.) return 2.0*(q0-x*(q1-x*(q2-x*q3)));
    if (x<0.) return 2.0*(p0+x*x*(p2-x*p3));
    if (x<1.) return 2.0*(p0+x*x*(p2+x*p3));
    if (x<2.) return 2.0*(q0+x*(q1+x*(q2+x*q3)));
    return 0.0;
}
