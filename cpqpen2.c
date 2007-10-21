/* 

cpqpen2.c

Driver for the Compaq Concerto's pen interface.

Copyright (C) 1995 by Joseph J. Pfeiffer, Jr.
   Pen_select and multiple open code by  Lawrence S. Brakmo <brakmo@cs.arizona.edu>
   Ported to 2.6.x kernel by Michael Fincham <michael@unleash.co.nz>, 15/10/07

Use, redistribution, and modification of this code is permitted without
fee subject to the following conditions:

1.  Redistribution of this code must preserve this copyright notice.

2.  Binary distributions must include this notice and disclaimer.

3.  Advertising materials that refer to specific features of this
    product must acknowledge the author.

4.  The author's name may not be used to endorse or promote any
    product derived from this software without written permission.

*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* O_ACCMODE */
#include <asm/system.h> /* cli(), *_flags */
#include <asm/uaccess.h> /* copy_from/to_user */
#include <linux/ioport.h>
#include <linux/poll.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <linux/interrupt.h>

#include "cpqpen2.h"

/* Some PC ports I need */
#define	INT_CTRL	0x20
#define	INT_CTRL2	0xa0
#define	EOI		0x20
#define INT_MASK	0xa1

/* Following are all the pen-related IO registers I have docs on.
   Some of them also control non-pen functions, hence the non-pen
   bit definitions */

#define	PEN_ADDR	0x2065	/* pen control address reg */
#define	PEN_DATA	0x2465	/* pen control data reg */

#define	PACKET_SIZE_REG	0x9d	/* read/write */
#define	FIFO_EMPTY	0x80
#define	NOT_FIFO_W_SPC	0x40
#define	FIFO_PTR_RESET	0x20
#define	PACKET_SIZE_MSK	0x1f
#define	BUFFER_SIZE	5
#define	PACKET_SIZE	1	/* size in bytes; max is 31.  5 is the
                                   actual length of a coord info packet,
                                   so it seemed a good size to pick; but,
                                   when I check, it looks like I can only
                                   set to 3, 7, 11, etc. so I'll pick
                                   the first one that's bigger than a
                                   packet */

#define	DIGITIZER_DATA_REG	0x9e	/* read only */

#define	CTL_PNT_DETECT_REG	0x9f	/* read only */

#define	CTL_PNT_DATA_REG	0xa0	/* read only */

#define	SEC_SYS_2_KBD_DATA_REG	0xa1	/* read/write */

#define	MBX_BFR_FULL_MT_REG	0xa8	/* read only */
#define	SEC_SYS_TO_KBD_BUF	0x08
#define	PRM_SYS_TO_KBD_BUF	0x04
#define KBD_TO_SYS_BUF	0x02
#define	CTL_PNT_DATA	0x01

#define	EXT_SMI_SRC_MSK_REG1	0x1465	/* Read/write */
#define EXTSMI_PNDNG	0x80
#define	POPUP_TIMEOUT	0x40
#define	EBOX_ACT	0x20
#define	KEY_SEQ		0x10
#define	PWR_CHG		0x08
#define	LOW_BAT_2	0x04
#define	LOW_BAT_1	0x02
#define	CTL_PNT_DTCT	0x01

/*  Note:  0 means masked on mask bits */
#define	EXT_SMI_SRC_MSK_REG2	0x3865	/* Read/write clear only */
#define	SMI_REQ_MSK	0x80
#define	POPUP_TIMEOUT_MSK	0x40
#define	EBOX_ACT_MSK	0x20
#define KEY_SEQ_MSK	0x10
#define	PWR_CHG_MSK	0x08
#define	LOW_BAT_2_MSK	0x04
#define	LOW_BAT_1_MSK	0x02
#define	CTL_PNT_DET_MSK	0x01

#define	MISC_OPTS_REG	0x3465	/* Read/write */
#define	IRQ_MSK	(3 << 6)
#define	IRQ9	(0 << 6)
#define	IRQ10	(1 << 6)
#define	IRQ11	(2 << 6)
#define	IRQ15	(3 << 6)
#define	INTEN	0x20
#define	DSK_CHG	0x08
#define	SLOT_ON	0x04
#define	MSE_INT_EN	0x02
#define	PM_BEEP	0x01

#define	IRQ_REQ_MASK	0x8c00

/* OK, now some variables we need */
#ifdef __NO_VERSION__
char kernel_version[] = UTS_RELEASE;
#endif

#define VERSION_MAJOR 1
#define VERSION_MINOR 9

#define NONE 0

MODULE_LICENSE("See COPYING in source package");

/* Declare functions */
int cpqpen2_open(struct inode *inode, struct file *filp);
int cpqpen2_release(struct inode *inode, struct file *filp);
ssize_t cpqpen2_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
void cpqpen2_exit(void);
int cpqpen2_init(void);
unsigned int cpqpen2_poll(struct file *filp, poll_table *wait);

/* Structure for file operations */
struct file_operations cpqpen2_fops = {
  read: cpqpen2_read,
  open: cpqpen2_open,
  release: cpqpen2_release,
  poll: cpqpen2_poll
};

/* Declaration of the init and exit functions */
module_init(cpqpen2_init);
module_exit(cpqpen2_exit);

/* Global variables */
/* Major number */
int cpqpen2_major = 70;

/* Codes to send to pen to set IRQ -- NONE signifies an IRQ the pen can't
   use */
static unsigned char pen_irq_code[16] =
    {NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
     NONE, IRQ9, IRQ10, IRQ11, NONE, NONE, NONE, IRQ15};

static int pen_irq, MOD_IN_USE;
static unsigned int pen_pos;
/* struct wait_queue *pen_wait_q = NULL; old! */
wait_queue_head_t pen_wait_q;

static unsigned char penbuf[5];

/* The following values are actually tuneable parameters. */
int c81 = 0x05;  /* tdconcrt.exe uses 03.  bios passes 05 | incremental */
int c82 = 0x01;  /* tdconcrt.exe uses 01.  bios passes an undoc parameter */
int c83 = 0x01;  /* tdconcrt.exe uses 01.  bios passes an undoc parameter */

/* If the pen should return a negative value, it instead returns a value that
   is (near as I can tell) 12,798 too high.  There should never be a valid
   y value anywhere near 8000 */
int ycorrect = 12798;
int ymax = 8000;

unsigned int cpqpen2_poll(struct file *filp, poll_table *wait)
{
        /* used to be: if (filp->sel_type != SEL_IN)
        CHECKME: make sure this is actually equivalent!
        */
        if (filp->f_mode != FMODE_READ)
                return 0; 
        if (filp->f_pos != pen_pos)
                return 1;
        poll_wait(filp,&pen_wait_q, wait);
        return 0;
}

static void pen_out(int val)
{
    /* loop waiting for buffer to be empty */
    outb_p(MBX_BFR_FULL_MT_REG, PEN_ADDR);
    while (inb_p(PEN_DATA) & SEC_SYS_TO_KBD_BUF);

    /* and write data to buffer */
    outb_p(SEC_SYS_2_KBD_DATA_REG, PEN_ADDR);
    outb_p(val, PEN_DATA);
}


void pen_int(int irq, struct pt_regs *regs)
{
    short peny;

    /*	Find out if this is a real interrupt and handle if so (I don't
        know if the pen generates false interrupts or not -- might as
        well not take any chances */
    outb_p(PACKET_SIZE_REG, PEN_ADDR);
    if (!(inb_p(PEN_DATA) & FIFO_EMPTY)) {

        outb_p(DIGITIZER_DATA_REG, PEN_ADDR);
		penbuf[0] = inb_p(PEN_DATA);
		penbuf[1] = inb_p(PEN_DATA);
		penbuf[2] = inb_p(PEN_DATA);
		penbuf[3] = inb_p(PEN_DATA);
		penbuf[4] = inb_p(PEN_DATA);

		if (!(penbuf[0] & 0x40)) {
			peny = (penbuf[3] << 8) | penbuf[4];
			
			peny |= inb_p(PEN_DATA);
			
			if (peny > ymax)
				peny -= ycorrect;
			
			penbuf[3] = (peny >> 8) & 0xff;
			penbuf[4] = peny & 0xff;
			
			pen_pos++;
			
			wake_up(&pen_wait_q);
		}
	} /*else
			printk("Pen status info %02x %02x %02x %02x %02x\n",
				   penbuf[0], penbuf[1], penbuf[2], penbuf[3],
				   penbuf[4]);*/

}


int cpqpen2_init(void) {
  int result;
  
  /* reset the counter of number of times the module has been opened FIXME not really a constant. this is a silly way to do this. */
  MOD_IN_USE = 0;

  printk(KERN_NOTICE "Compaq Contura Pen Driver %d.%d, by Joseph J. Pfeiffer, Lawrence S. Brakmo and Michael Fincham.\n",VERSION_MAJOR,VERSION_MINOR);

  /* Register with the kernel*/
  result = register_chrdev(cpqpen2_major, PEN_NAME, &cpqpen2_fops);
  if (result < 0) {
    printk(KERN_ERR "%s: (E) Unable to register with kernel on device major number %d. Giving up.\n",PEN_NAME, cpqpen2_major);
    return result;
  }

    printk(KERN_DEBUG "%s: (D) Attempting to secure an interrupt vector.\n",PEN_NAME);
   /* see if I can get an interrupt vector */
    for (pen_irq = 0; pen_irq < 16; pen_irq++)
        if ((1 << pen_irq) & IRQ_REQ_MASK)
            if (!request_irq(pen_irq, (void *) pen_int, 0, PEN_NAME, NULL))
                break;

    if (pen_irq != 16)
        printk(KERN_INFO "%s: (I) IRQ secured is %d\n", PEN_NAME, pen_irq);
    else {
        printk(KERN_ERR "%s: (E) Unable to secure an IRQ. Giving up.\n", PEN_NAME);
        unregister_chrdev(cpqpen2_major, PEN_NAME);
	return -1;
    }

    request_region(PEN_DATA, 1, PEN_NAME);
    request_region(PEN_ADDR, 1, PEN_NAME);
    request_region(MISC_OPTS_REG, 1, PEN_NAME);
    
    printk(KERN_INFO "%s: (I) Module loaded.\n",PEN_NAME);
  return 0;
}

void cpqpen2_exit(void) {
  /* Unregister with kernel */

 if (MOD_IN_USE > 0)
        printk(KERN_WARNING "%s: (W) Module busy - not removed.\n", PEN_NAME);
    else {
        unregister_chrdev(cpqpen2_major, PEN_NAME);
        free_irq(pen_irq, NULL);

        release_region(PEN_DATA, 1);
        release_region(PEN_ADDR, 1);
        release_region(MISC_OPTS_REG, 1);

        printk(KERN_INFO "%s: (I) Module unloaded.\n", PEN_NAME);
    }


}


/* called on fopen of device */
int cpqpen2_open(struct inode *inode, struct file *filp) {
    unsigned char opts;

	/*printk("opening pen\n");*/
	
    /* if I'm the first to start up the pen, I need to initialize it */
    if (MOD_IN_USE < 1) {
      /* Set pen irq and enable pen interrupt */
      opts = inb_p(MISC_OPTS_REG);
      opts &= ~IRQ_MSK;
      opts |= pen_irq_code[pen_irq] | INTEN;

      outb_p(opts, MISC_OPTS_REG);
/* pull out up to five bytes */

/* don't do this next one */	  
      /* Clear out pen digitizer buffer */
      pen_out(0x80);

      /* This seems like a really weird bit to toggle on -- but
		 both drivers I've got do this */
      outb_p(PACKET_SIZE_REG, PEN_ADDR);
      while (!(inb_p(PEN_DATA) & FIFO_PTR_RESET));
      while (inb_p(PEN_DATA) & FIFO_PTR_RESET);
    
      /* Now send ``magic intialization sequence'' to pen */
      pen_out(0x81); pen_out(c81);
	  if (c81 & 2) {
		  pen_out(0x82); pen_out(c82);
		  pen_out(0x83); pen_out(c83);
	  }
	  pen_out(0x84);
	  
      /* keep track of how many pen packets I've read */
      pen_pos = 0;
    }

    MOD_IN_USE++;

    return 0;
}

/* called on close/release */
int cpqpen2_release(struct inode *inode, struct file *filp) {
   /*  used to be a "cli();" replace with a spinlock! CHECKME: is this equivalent? */
    local_irq_disable();
    
    MOD_IN_USE=MOD_IN_USE-1;

    if (MOD_IN_USE < 1)
      /* shut down pen interrupts */
      printk(KERN_DEBUG "%s: (D) Shutting down pen interrupts.\n", PEN_NAME);
      outb_p(inb_p(MISC_OPTS_REG) & ~(IRQ_MSK | INTEN), MISC_OPTS_REG);

   /* replace with a spinlock! used to be an "sti();" CHECKME: is this equivalent? */
   local_irq_enable();
   return 0;
}

/* called when the block device is read */
ssize_t cpqpen2_read(struct file *filp, char *buf, size_t count, loff_t *f_pos) { 
    int i;

    /* Find out if there is data in the pen buffer */
    if (filp->f_pos == pen_pos) {

        /*  If the pen is open non-blocking, we just return failure now. */
        if (filp->f_flags & O_NONBLOCK)
	    return(-EAGAIN);

        /*  Looks like we're not non-blocking -- so we need to wait to get
            data before returning.  */
	interruptible_sleep_on(&pen_wait_q);
    }

    /* OK, now we transfer a packet to the caller.  If the user requested
       less than a packet I guess we give less...  but we never give more.
       The driver is also written so if the user makes two calls we start
       over -- you can't use five one-byte calls to get a packet.  The idea
       is that you can't end up with your reads out of sync with the pen
       packets */

    if (count > 5) count = 5;
	
    if (pen_pos != filp->f_pos) {
      for (i = 0; i < count; i++)
        /* put_fs_byte(penbuf[i], buf++); old! */
        put_user(penbuf[i], buf++);
      filp->f_pos = pen_pos;
    } else
		i = -EIO; /* something's gone badly wrong if I get here... */
	
    return(i);
 
}

