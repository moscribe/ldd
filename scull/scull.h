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
	struct semaphore sem;	 /* 互斥信号量 */
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

#endif