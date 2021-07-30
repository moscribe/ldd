#ifndef _SCULL_H
#define _SCULL_H

#ifndef SCULL_QUANTUM
#define SCULL_QUANTUM 4000
#endif

#ifndef SCULL_QSET
#define SCULL_QSET 1000
#endif

#ifndef SCULL_MAJOR
#define SCULL_MAJOR 0
#endif

#ifndef SCULL_NR_DEVS
#define SCULL_NR_DEVS 4
#endif

struct scull_qset
{
	void **data;
	struct scull_qset *next;
};

struct scull_dev
{
	struct scull_qset *data; /* 指向第一个量子集的指针*/
	int quantum;			 /* 当前量子的大小 */
	int qset;				 /* 当前数组的大小 */
	unsigned long size;		 /* 保存的数据总量 */
	unsigned int access_key; /* 由sculluid和scullpriv使用 */
	struct mutex mutex;	 /* 互斥信号量 */
	struct cdev cdev;		 /* 字符设备结构 */
};

/* The different configurale parameters */
extern int scull_major;
extern int scull_nr_devs;
extern int scull_quantum;
extern int scull_qset;

int scull_trim(struct scull_dev *dev);
ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
long scull_ioctl(struct file* filp, unsigned int cmd, unsigned long arg);

/**
 * ioctl definitions
 */
// magic number
#define SCULL_IOC_MAGIC 'l'

#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC, 0)

/**
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting throuth a pointer
 * Q means "Query": response is on the return value
 * X meas "eXchange": switch G and S atomically
 * H means "sHift":switch T and Q atomically
 */
#define SCULL_IOCSQUANTUM	_IOW(SCULL_IOC_MAGIC, 1, int)
#define SCULL_IOCSQSET		_IOW(SCULL_IOC_MAGIC, 2, int)
#define SCULL_IOCTQUANTUM	_IO(SCULL_IOC_MAGIC, 3)
#define SCULL_IOCTQSET		_IO(SCULL_IOC_MAGIC, 4)
#define SCULL_IOCGQUANTUM	_IOR(SCULL_IOC_MAGIC, 5, int)
#define SCULL_IOCGQSET		_IOR(SCULL_IOC_MAGIC, 6, int)
#define SCULL_IOCQQUANTUM	_IO(SCULL_IOC_MAGIC, 7)
#define SCULL_IOCQQSET		_IO(SCULL_IOC_MAGIC, 8)
#define SCULL_IOCXQUANTUM	_IOWR(SCULL_IOC_MAGIC, 9, int)
#define SCULL_IOCXQSET		_IOWR(SCULL_IOC_MAGIC, 10, int)
#define SCULL_IOCHQUANTUM	_IO(SCULL_IOC_MAGIC, 11)
#define SCULL_IOCHQSET		_IO(SCULL_IOC_MAGIC, 12)

#define SCULL_IOC_MAXNR 14


#endif