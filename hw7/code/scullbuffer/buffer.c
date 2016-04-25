/*============================================================================
 CSci5103 Spring 2016
 Assignment#        : 7
 Name               : John Erickson
 Student id         : 2336359
 x500 id            : eric0870
 CSELABS machine    : csel-kh4250-03.cselabs.umn.edu
 Virtual machine    : csel-x34-umh.cselabs.umn.edu
 ============================================================================*/

/**********************************************************************************************
 * Requirements
 *  - implement buffer device driver
 *    -- buffer needs to hold objects of size 32 bytes
 *  - accept command line input to specify number of items to contain in buffer
 *    -- implement as module parameter
 *  - supply scripts to load and unload the device driver
 *  - implement open(), release(), read() and write() functions
 *
 * Considerations
 *  - none
 *
 * Code reuse
 *  - bare scull and scullpipe drivers
 *    -- from the book "Linux Device Drivers" by Alessandro Rubini and Jonathan Corbet,
 *       published by O'Reilly & Associates.
 *    -- complete reuse of the bare scull driver
 *    -- substantial reuse of the scullpipe driver, tailoring for the assignment as needed
 *
 **********************************************************************************************/

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>	/* printk(), min() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/proc_fs.h>
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include "scull.h"		/* local definitions */

#define init_MUTEX(_m) sema_init(_m, 1);


struct scull_buffer {
        wait_queue_head_t inq, outq;       /* read and write queues */
        char *buffer, *end;                /* begin of buf, end of buf */
        int buffersize;                    /* used in pointer arithmetic */
        char *rp, *wp;                     /* where to read, where to write */
        int nreaders, nwriters;            /* number of openings for r/w */
        struct semaphore sem;              /* mutual exclusion semaphore */
        struct cdev cdev;                  /* Char device structure */
};

/* parameters */
static int scull_p_nr_devs    = SCULL_P_NR_DEVS;	    /* number of devices */
static int scullbuffer_nitems = SCULLBUFFER_DNITEMS;    /* num items, static allocation because its a module parameter */
dev_t scull_p_devno;			                        /* Our first device number */
static struct scull_buffer *scull_p_devices;

module_param(scull_p_nr_devs, int, S_IRUGO);
module_param(scullbuffer_nitems, int, S_IRUGO);

/* prototypes */
static int spacefree(struct scull_buffer *dev);
/*
 * Open and close
 */
static int scull_p_open(struct inode *inode, struct file *filp)
{
    struct scull_buffer *dev;

    // create scullbuffer with size equal to item size * number of items
    int scullbuffer_size = SCULLBUFFER_ITEM_SIZE * scullbuffer_nitems;  /* buffer size */

    // DEBUG
    printk( KERN_ALERT "scullbuffer size = %d", scullbuffer_size );

	dev = container_of(inode->i_cdev, struct scull_buffer, cdev);
	filp->private_data = dev;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	if (!dev->buffer) {
		/* allocate the buffer */
		dev->buffer = kmalloc(scullbuffer_size, GFP_KERNEL);
		if (!dev->buffer) {
			up(&dev->sem);
			return -ENOMEM;
		}
	}
	dev->buffersize = scullbuffer_size;
	dev->end = dev->buffer + dev->buffersize;
	dev->rp = dev->wp = dev->buffer; /* rd and wr from the beginning */

	/* use f_mode,not  f_flags: it's cleaner (fs/open.c tells why) */
	if ( (filp->f_mode & FMODE_READ) & !(filp->f_mode & FMODE_WRITE))
		dev->nreaders++;

	if (filp->f_mode & FMODE_WRITE)
		dev->nwriters++;

	up(&dev->sem);

	return nonseekable_open(inode, filp);
}

static int scull_p_release(struct inode *inode, struct file *filp)
{
	struct scull_buffer *dev = filp->private_data;
	int wake_cons=false, wake_prod=false;

	down(&dev->sem);

    if ( (filp->f_mode & FMODE_READ) & !(filp->f_mode & FMODE_WRITE))
	{
	    PDEBUG("DEBUG: consumer process releasing device \n");
		dev->nreaders--;
	}

	if (filp->f_mode & FMODE_WRITE)
	{
	    PDEBUG("DEBUG: producer process releasing device \n");
	    dev->nwriters--;
	}

	if (dev->nreaders + dev->nwriters == 0)
	{
	    PDEBUG("DEBUG: last process to release device, freeing up buffer \n");
		kfree(dev->buffer);
		dev->buffer = NULL; /* the other fields are not checked on open */
	}
	else if ( dev->nreaders == 0 )
    {
        // if no consumers, signal any sleeping producers
	    wake_prod = true;
    }
	else if ( dev->nwriters == 0 )
    {
        // if no producers, signal any sleeping consumers
	    wake_cons = true;
    }

	up(&dev->sem);

	if ( wake_prod )
	{
	    // if no consumers, signal any sleeping producers
	    PDEBUG("DEBUG: no remaining consumers, waking sleeping producer \n");
	    wake_up_interruptible(&dev->outq);
	}
    if ( wake_cons )
    {
        // if no producers, signal any sleeping consumers
        PDEBUG("DEBUG: no remaining producers, waking sleeping consumer \n");
        wake_up_interruptible(&dev->inq);
    }

	return 0;
}

/*
 * Data management: read and write
*/
static ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_buffer *dev = filp->private_data;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	PDEBUG("\" (scull_p_read) dev->wp:%p    dev->rp:%p\" \n",dev->wp,dev->rp);

	while (dev->rp == dev->wp) { /* nothing to read */
		up(&dev->sem); /* release the lock */
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if ( dev->nwriters == 0 )
		    return 0; // buffer is empty and
		              // there are no producer processes that currently have device open for writing

		PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
		if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
		/* otherwise loop, but first reacquire the lock */
		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	/* ok, data is there, return something */
	if (dev->wp > dev->rp)
		count = min(count, (size_t)(dev->wp - dev->rp));
	else /* the write pointer has wrapped, return data up to dev->end */
		count = min(count, (size_t)(dev->end - dev->rp));
	if (copy_to_user(buf, dev->rp, count)) {
		up (&dev->sem);
		return -EFAULT;
	}
	dev->rp += count;
	if (dev->rp == dev->end)
		dev->rp = dev->buffer; /* wrapped */
	up (&dev->sem);

	/* finally, awake any writers and return */
	wake_up_interruptible(&dev->outq);
	PDEBUG("\"%s\" did read %li bytes\n",current->comm, (long)count);
	return count;
}

/* Wait for space for writing; caller must hold device semaphore.  On
 * error the semaphore will be released before returning. */
static int scull_getwritespace(struct scull_buffer *dev, struct file *filp)
{
	while ( spacefree(dev) < SCULLBUFFER_ITEM_SIZE ) { /* full */
		DEFINE_WAIT(wait);
		
		// release semaphore
		up(&dev->sem);

		// confirm ok to block
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		// we're here because the buffer is full, any consumers out there?
		if ( dev->nreaders == 0 )
            return 1;   // buffer is full and
                        // there are no consumer processes that currently have device open for reading

		PDEBUG("\"%s\" writing: going to sleep\n",current->comm);
		prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
		if ( spacefree(dev) < SCULLBUFFER_ITEM_SIZE )
			schedule();

		// awake, make sure its because there is room in the buffer
		finish_wait(&dev->outq, &wait);

		// if woken, but there are no consumers, don't bother writing any more items to buffer
		if ( dev->nreaders == 0 )
            return 1;

		if (signal_pending(current))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */

		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	return 0;
}	

/* How much space is free? */
static int spacefree(struct scull_buffer *dev)
{
	if (dev->rp == dev->wp)
		return dev->buffersize - 1;
	return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

static ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_buffer *dev = filp->private_data;
	int result;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	/* Make sure there's space to write */
	result = scull_getwritespace(dev, filp);

	// getwritespace returns 1 when buffer full and no consumers
	if ( result == 1 )
	    return 0;

	// getwritespace returns error code if error occured, or zero otherwise
	if (result)
		return result; /* scull_getwritespace called up(&dev->sem) */

	/* ok, space is there, accept something */
	count = min(count, (size_t)spacefree(dev));

	if (dev->wp >= dev->rp)
		count = min(count, (size_t)(dev->end - dev->wp)); /* to end-of-buf */
	else /* the write pointer has wrapped, fill up to rp-1 */
		count = min(count, (size_t)(dev->rp - dev->wp - 1));

	PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);
	if (copy_from_user(dev->wp, buf, count)) {
		up (&dev->sem);
		return -EFAULT;
	}
	dev->wp += count;
	if (dev->wp == dev->end)
		dev->wp = dev->buffer; /* wrapped */
	PDEBUG("\" (scull_p_write) dev->wp:%p    dev->rp:%p\" \n",dev->wp,dev->rp);
	up(&dev->sem);

	/* finally, awake any reader */
	wake_up_interruptible(&dev->inq);  /* blocked in read() and select() */

	PDEBUG("\"%s\" did write %li bytes\n",current->comm, (long)count);

	PDEBUG("Wrote item: %s\n", buf);
	return count;
}

static unsigned int scull_p_poll(struct file *filp, poll_table *wait)
{
	struct scull_buffer *dev = filp->private_data;
	unsigned int mask = 0;

	/*
	 * The buffer is circular; it is considered full
	 * if "wp" is right behind "rp" and empty if the
	 * two are equal.
	 */
	down(&dev->sem);
	poll_wait(filp, &dev->inq,  wait);
	poll_wait(filp, &dev->outq, wait);
	if (dev->rp != dev->wp)
		mask |= POLLIN | POLLRDNORM;	/* readable */
	if (spacefree(dev))
		mask |= POLLOUT | POLLWRNORM;	/* writable */
	up(&dev->sem);
	return mask;
}

/*
 * The file operations for the buffer device
 * (some are overlayed with bare scull)
 */
struct file_operations scull_buffer_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		scull_p_read,
	.write =	scull_p_write,
	.poll =		scull_p_poll,
	.open =		scull_p_open,
	.release =	scull_p_release,
};

/*
 * Set up a cdev entry.
 */
static void scull_p_setup_cdev(struct scull_buffer *dev, int index)
{
	int err, devno = scull_p_devno + index;
    
	cdev_init(&dev->cdev, &scull_buffer_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add (&dev->cdev, devno, 1);
	/* Fail gracefully if need be */
	if (err)
		printk(KERN_NOTICE "Error %d adding scullbuffer%d", err, index);
}

/*
 * Initialize the buffer devs; return how many we did.
 */
int scull_p_init(dev_t firstdev)
{
	int i, result;

	result = register_chrdev_region(firstdev, scull_p_nr_devs, "scullp");
	if (result < 0) {
		printk(KERN_NOTICE "Unable to get scullp region, error %d\n", result);
		return 0;
	}
	scull_p_devno = firstdev;
	scull_p_devices = kmalloc(scull_p_nr_devs * sizeof(struct scull_buffer), GFP_KERNEL);
	if (scull_p_devices == NULL) {
		unregister_chrdev_region(firstdev, scull_p_nr_devs);
		return 0;
	}
	memset(scull_p_devices, 0, scull_p_nr_devs * sizeof(struct scull_buffer));
	for (i = 0; i < scull_p_nr_devs; i++) {
		init_waitqueue_head(&(scull_p_devices[i].inq));
		init_waitqueue_head(&(scull_p_devices[i].outq));
		init_MUTEX(&scull_p_devices[i].sem);
		scull_p_setup_cdev(scull_p_devices + i, i);
	}
	return scull_p_nr_devs;
}

/*
 * This is called by cleanup_module or on failure.
 * It is required to never fail, even if nothing was initialized first
 */
void scull_p_cleanup(void)
{
	int i;

	if (!scull_p_devices)
		return; /* nothing else to release */

	for (i = 0; i < scull_p_nr_devs; i++) {
		cdev_del(&scull_p_devices[i].cdev);
		kfree(scull_p_devices[i].buffer);
	}
	kfree(scull_p_devices);
	unregister_chrdev_region(scull_p_devno, scull_p_nr_devs);
	scull_p_devices = NULL; /* pedantic */
}
