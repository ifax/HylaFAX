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
 *      hipass -
 *              Hi pass filter rows using a 3x3 kernel.
 *
 *                              Paul Haeberli - 1989
 *
 */
#include "stdio.h"
#include "hipass.h"

highpass *newhp(getfunc,xsize,ysize,mag)
int (*getfunc)();
int xsize, ysize;
float mag;
{
    highpass *hp; 
    int i;

    hp = (highpass *)malloc(sizeof(highpass));
    hp->getfunc = getfunc;
    hp->xsize = xsize;
    hp->ysize = ysize;
    hp->y = 0;
    hp->extrapval = 1024*mag;
    for(i=0; i<3; i++)
        hp->blurrows[i] = (short *)malloc(xsize*sizeof(short));
    for(i=0; i<2; i++)
        hp->pastrows[i] = (short *)malloc(xsize*sizeof(short));
    hp->acc = (short *)malloc(xsize*sizeof(short));
    if(xsize<3 || ysize<3 || hp->extrapval == 0) 
        hp->active = 0;
    else {
        hp->active = 1;
    }
    return hp;
}

freehp(hp)
highpass *hp; 
{
    int i;

    for(i=0; i<3; i++)
        free(hp->blurrows[i]);
    for(i=0; i<2; i++)
        free(hp->pastrows[i]);
    free(hp->acc);
    free(hp);
}

/*
 *      extrap -
 *              Extrapolate from the blurred image beyond the original
 *
 */
static extrap(pix,acc,div,n,extrapval)
unsigned short *pix;
short *acc;
int div, n;
int extrapval;
{
    int delta, val;
    int mul, ext;

    mul = div;
    div = div*256;
    ext = extrapval;
    while(n--) {
        delta = (*pix * mul)-*acc++;
        val = *pix+(ext*delta)/div;
        if(val<0)
            val = 0;
        if(val>255)
            val = 255;
        *pix++ = val;
    }
}

/*
 *      xblur -
 *              Blur a row in the x direction
 *
 */
static xblur(sptr,cptr,n)
unsigned short *sptr, *cptr;
int n;
{
    short acc;

    if (n<3) {
        fprintf(stderr,"xblur: n may not be less than 3\n");
        exit(1);
    }
    acc = cptr[0] + cptr[1];
    *sptr++ = (3*acc+1)>>1;
    acc += cptr[2];
    n-=3;
    while(n--) {
        *sptr++ = acc;
        acc -= cptr[0];
        acc += cptr[3];
        cptr++;
    }
    *sptr++ = acc;
    acc -= cptr[0];
    *sptr++ = (3*acc+1)>>1;
}

hpgetrow(hp,buf,y)
highpass *hp;
short *buf;
int y;
{
    short *temp;
    int iy;

    if(y == 0)
        hp->y = 0;
    if(hp->y != y) {
        fprintf(stderr,"hpgetrow: y error\n");
        exit(1);
    }
    if(hp->active) {
        if (y == 0) {
            memset(hp->acc,0,hp->xsize*sizeof(short));
            for(iy=0; iy<2; iy++) {
                temp = hp->pastrows[0];
                hp->pastrows[0] = hp->pastrows[1];
                hp->pastrows[1] = temp;
                hp->getfunc(hp->pastrows[1],iy);
                xblur(hp->blurrows[iy],hp->pastrows[1],hp->xsize);
                addsrow(hp->acc,hp->blurrows[iy],hp->xsize);
            }
            copyrow(hp->pastrows[0],buf,hp->xsize);
            extrap(buf,hp->acc,6,hp->xsize,hp->extrapval);
        } else if (y == hp->ysize-1) {
            copyrow(hp->pastrows[1],buf,hp->xsize);
            extrap(buf,hp->acc,6,hp->xsize,hp->extrapval);
        } else {
            temp = hp->pastrows[0];
            hp->pastrows[0] = hp->pastrows[1];
            hp->pastrows[1] = temp;
            hp->getfunc(hp->pastrows[1],y+1);
            xblur(hp->blurrows[2],hp->pastrows[1],hp->xsize);
            addsrow(hp->acc,hp->blurrows[2],hp->xsize);
            copyrow(hp->pastrows[0],buf,hp->xsize);
            extrap(buf,hp->acc,9,hp->xsize,hp->extrapval);
            subsrow(hp->acc,hp->blurrows[0],hp->xsize);
            temp = hp->blurrows[0];
            hp->blurrows[0] = hp->blurrows[1];
            hp->blurrows[1] = hp->blurrows[2];
            hp->blurrows[2] = temp;
        }
    } else {
        hp->getfunc(buf,y);
    }
    hp->y++;
}
