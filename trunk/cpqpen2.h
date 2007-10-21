/* Copyright (C) 1995 by Joseph J. Pfeiffer, Jr.
Use, redistribution, and modification of this code is permitted without
fee subject to the following conditions:

1.  Redistribution of this code must preserve this copyright notice.

2.  Binary distributions must include this notice and disclaimer.

3.  Advertising materials that refer to specific features of this
    product must acknowledge the author.

4.  The author's name may not be used to endorse or promote any
    product derived from this software without written permission.
*/

/* 

Driver name changed by Michael Fincham <michael@unleash.co.nz>, 15/10/07

*/

/* Constants used for pen ioctls... assuming we end up with some! */
#define	PEN_SET			1			/* set bits in pen control word */
#define	PEN_CLEAR		2			/* clear bits in pen control word */
#define	PEN_SCALE		3			/* set x and y scales for relative mode */

/* Pen mode bits */
#define	PEN_NOSTATBRK	1			/* don't stop reading on status change */
#define	PEN_RELATIVE	2			/* relative coordinates */
#define	PEN_DNONLY		4			/* only send data if pen is down */

/* default scale factors for relative mode */
#define	PEN_SCL_XDFLT	0.10
#define	PEN_SCL_YDFLT	0.10

#define	PEN_NO			1			/* number of pens */

#define	PEN_OPEN		1			/* pen is open */
#define	PEN_DATAVAIL	2			/* there is a packet of pen data */

#define	PEN_NAME		"cpqpen2"	/* name of pen special file */
#define PEN_DIR         "/dev/"        /* directory for pen special file */

#define	PEN_BUF_SIZE	5	/* pen packet size */
