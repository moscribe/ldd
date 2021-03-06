#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "scull.h"

// Parameters
int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset = SCULL_QSET;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

MODULE_AUTHOR("Liu Miao");
MODULE_LICENSE("Dual BSD/GPL");

struct scull_dev *scull_devices;

/*
Empty out the scull device; must be called with the device
semaphore held.
*/
int scull_trim(struct scull_dev *dev)
{
	struct scull_qset *next, *dptr;
	int qset = dev->qset;
	int i;

	for (dptr = dev->data; dptr; dptr = next)
	{
		if (dptr->data)
		{
			for (i = 0; i < qset; i++)
			{
				kfree(dptr->data[i]);
			}
			kfree(dptr->data);
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}
	dev->size = 0;
	dev->quantum = scull_quantum;
	dev->qset = scull_qset;
	dev->data = NULL;
	return 0;
}

// Open and Close
int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *dev;

	dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	filp->private_data = dev;

	if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
	{
		scull_trim(dev);
	}

	return 0;
}

/*
Follow the list
*/
struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
	struct scull_qset *qs = dev->data;

	// Allocate first qset explicitly if need be
	if (!qs)
	{
		qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (qs == NULL)
			return NULL;
		memset(qs, 0, sizeof(struct scull_qset));
	}

	// Follow the list
	while (n--)
	{
		if (!qs->next)
		{
			qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (qs->next == NULL)
				return NULL;
			memset(qs->next, 0, sizeof(struct scull_qset));
		}
		qs = qs->next;
		continue;
	}

	return qs;
}

/*
 Data management: read and write
 */
ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum, qset = dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos, rest;
	ssize_t retval = 0;

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	if (*f_pos >= dev->size)
		goto out;
	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;

	/* ??????????????????????????????,qset????????????????????? */
	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item);

	if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
		goto out;

	if (count > quantum - q_pos)
		count = quantum - q_pos;

	// ??????copy_to_user?????????????????????
	if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count))
	{
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;

out:
	mutex_unlock(&dev->mutex);
	return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum, qset = dev->qset;
	int itemsize = quantum * qset;
	int item, q_pos, s_pos, rest;
	ssize_t retval = -ENOMEM;

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;

	item = (long)*f_pos / itemsize; // ??????????????????????????????
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum; // ???????????????????????????
	q_pos = rest % quantum; // ?????????????????????????????????

	dptr = scull_follow(dev, item);

	if (dptr == NULL)
		goto out;
	if (!dptr->data)
	{
		dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
		if (!dptr->data)
			goto out;
		memset(dptr->data, 0, qset * sizeof(char *));
	}
	if (!dptr->data[s_pos])
	{
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if (!dptr->data[s_pos])
			goto out;
	}

	if (count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count))
	{
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;
	// ??????????????????
	if (dev->size < *f_pos)
		dev->size = *f_pos;

out:
	mutex_unlock(&dev->mutex);
	return retval;
}

long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0, tmp;
	int retval = 0;

	/**
	 * Extract the type and number bitfields, and don't decode incorrect
	 * cmds: return ENOTTY (inappropriate ioctl) before access_ok(). 
	 */
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > SCULL_IOC_MAXNR)
		return -ENOTTY;

	/**
	 * The direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while access_ok is
	 * kernel-oriented, so the concept of "read" and "write" is reversed.
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));

	switch (cmd)
	{
	case SCULL_IOCRESET:
		scull_quantum = SCULL_QUANTUM;
		scull_qset = SCULL_QSET;
		break;

	case SCULL_IOCSQUANTUM: /* Set: arg points to the value. */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		retval = __get_user(scull_quantum, (int __user *)arg);
		break;

	case SCULL_IOCTQUANTUM: /* Tell: arg is the value */
		if(!capable(CAP_SYS_ADMIN)) return -EPERM;
		scull_quantum = arg;
		break;
	
	case SCULL_IOCGQUANTUM: /* Get: arg is ptr to result. */
		retval = __put_user(scull_quantum, (int __user*)arg);
		break;
	
	case SCULL_IOCQQUANTUM: /* Query: return result(positive) */
		return scull_quantum;
	
	case SCULL_IOCXQUANTUM: /* eXchange: use arg as pointer */
		if(!capable(CAP_SYS_ADMIN)) return -EPERM;
		tmp = scull_quantum;
		retval = __get_user(scull_quantum, (int __user*) arg);
		if(retval == 0) retval = __put_user(tmp, (int __user*)arg);
		break;
	
	case SCULL_IOCHQUANTUM: /* sHift: like Tell + Query */
		if(!capable(CAP_SYS_ADMIN)) return -EPERM;
		tmp = scull_quantum;
		scull_quantum = arg;
		return tmp;
	
	case SCULL_IOCSQSET:
		if(!capable(CAP_SYS_ADMIN)) return -EPERM;
		retval = __get_user(scull_qset, (int __user *)arg);
		break;

	case SCULL_IOCTQSET:
		if(!capable(CAP_SYS_ADMIN)) return -EPERM;
		scull_qset = arg;
		break;
	
	case SCULL_IOCGQSET:
		retval = __put_user(scull_qset, (int __user *)arg);
		break;

	case SCULL_IOCQQSET:
		return scull_qset;

	case SCULL_IOCXQSET:
		if(!capable(CAP_SYS_ADMIN)) return -EPERM;
		tmp = scull_qset;
		retval = __get_user(scull_qset, (int __user *) arg);
		if(retval == 0) retval = __put_user(scull_qset, (int __user *)arg);
		break;

	case SCULL_IOCHQSET:
		if(!capable(CAP_SYS_ADMIN)) return -EPERM;
		tmp = scull_qset;
		scull_qset = arg;
		return tmp;

	default:
		break;
	}

	return retval;
}

int scull_release(struct inode *inode, struct file *filp)
{
	return 0;
}

struct file_operations scull_fops = {
	.owner = THIS_MODULE,
	.read = scull_read,
	.write = scull_write,
	.open = scull_open,
	.release = scull_release,
};

void scull_cleanup_module(void)
{
	int i;
	dev_t devno = MKDEV(scull_major, scull_minor);

	if (scull_devices)
	{
		for (i = 0; i < scull_nr_devs; i++)
		{
			scull_trim(scull_devices + i);
			cdev_del(&scull_devices[i].cdev);
		}
		kfree(scull_devices);
	}

	// cleanup_module is never called if registering failed
	unregister_chrdev_region(devno, scull_nr_devs);

	printk(KERN_NOTICE "Close scull");
}

static void
scull_setup_cdev(struct scull_dev *dev, int index)
{
	int err, devno = MKDEV(scull_major, scull_minor + index);

	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &scull_fops;
	err = cdev_add(&dev->cdev, devno, 1);

	if (err)
		printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

int scull_init_module(void)
{
	int result, i;
	dev_t dev = 0;

	/* Get a range of minor numbers to work with, asking for a dynaimc
major unless directed otherwise at load time.
*/
	if (scull_major)
	{
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, scull_nr_devs, "scull");
	}
	else
	{
		result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scull");
		scull_major = MAJOR(dev);
	}

	if (result < 0)
	{
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

	/*
Allocate the devices. we can't have them static, as the number can
be specified at load time
*/
	scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
	if (!scull_devices)
	{
		result = -ENOMEM;
		goto fail;
	}
	memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

	for (i = 0; i < scull_nr_devs; i++)
	{
		scull_devices[i].quantum = scull_quantum;
		scull_devices[i].qset = scull_qset;
		mutex_init(&scull_devices[i].mutex);
		scull_setup_cdev(&scull_devices[i], i);
	}

	dev = MKDEV(scull_major, scull_minor + scull_nr_devs);

	printk(KERN_NOTICE "Open scull");
	return 0;

fail:
	scull_cleanup_module();
	return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);