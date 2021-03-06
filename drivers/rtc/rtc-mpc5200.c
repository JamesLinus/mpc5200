/*
 * MPC5200 On-Chip RTC Support
 *
 * Copyright (C) 2008 Jon Smirl <jonsmirl@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <asm/rtc.h>
#include <asm/mpc52xx.h>

#define DRV_NAME	"mpc5200-rtc"
#define DRV_VERSION	"0.1.0"

#define PAUSE_TIME 0x01000000
#define SET_TIME 0x02000000

struct mpc5200_rtc {
	struct mpc52xx_rtc __iomem *regs;
	unsigned int alarm_irq, periodic_irq;
	struct rtc_device *rtc_dev;
	spinlock_t lock;
	unsigned short periodic_freq;
};

static irqreturn_t mpc5200_rtc_alarm(int irq, void *dev_id)
{
	struct mpc5200_rtc *rtc = dev_id;
	unsigned int tmp;

	spin_lock(&rtc->lock);

	tmp = readb(rtc->regs + RCR1);
	tmp &= ~(RCR1_AF | RCR1_AIE);
		writeb(tmp, rtc->regs + RCR1);

	rtc_update_irq(rtc->rtc_dev, 1, RTC_AF | RTC_IRQF);

	spin_unlock(&rtc->lock);

	return IRQ_HANDLED;
}

static irqreturn_t mpc5200_rtc_periodic(int irq, void *dev_id)
{
	struct mpc5200_rtc *rtc = dev_id;
	struct rtc_device *rtc_dev = rtc->rtc_dev;
	unsigned int tmp;

	spin_lock(&rtc->lock);

	tmp = readb(rtc->regs + RCR2);
	tmp &= ~RCR2_PEF;
	writeb(tmp, rtc->regs + RCR2);

	/* Half period enabled than one skipped and the next notified */
	if ((rtc->periodic_freq & PF_HP) && (rtc->periodic_freq & PF_COUNT))
		rtc->periodic_freq &= ~PF_COUNT;
	else {
		if (rtc->periodic_freq & PF_HP)
			rtc->periodic_freq |= PF_COUNT;
		if (rtc->periodic_freq & PF_KOU) {
			spin_lock(&rtc_dev->irq_task_lock);
			if (rtc_dev->irq_task)
				rtc_dev->irq_task->func(rtc_dev->irq_task->private_data);
			spin_unlock(&rtc_dev->irq_task_lock);
		} else
			rtc_update_irq(rtc->rtc_dev, 1, RTC_PF | RTC_IRQF);
	}

	spin_unlock(&rtc->lock);

	return IRQ_HANDLED;
}

static inline void mpc5200_rtc_setpie(struct device *dev, unsigned int enable)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(dev);
	unsigned int tmp;

	spin_lock_irq(&rtc->lock);

	tmp = readb(rtc->regs + RCR2);

	if (enable) {
		tmp &= ~RCR2_PEF;	/* Clear PES bit */
		tmp |= (rtc->periodic_freq & ~PF_HP);	/* Set PES2-0 */
	} else
		tmp &= ~(RCR2_PESMASK | RCR2_PEF);

	writeb(tmp, rtc->regs + RCR2);

	spin_unlock_irq(&rtc->lock);
}

static inline int mpc5200_rtc_setfreq(struct device *dev, unsigned int freq)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(dev);
	int tmp, ret = 0;

	spin_lock_irq(&rtc->lock);
	tmp = rtc->periodic_freq & PF_MASK;

	switch (freq) {
	case 0:
		rtc->periodic_freq = 0x00;
		break;
	case 1:
		rtc->periodic_freq = 0x60;
		break;
	case 2:
		rtc->periodic_freq = 0x50;
		break;
	case 4:
		rtc->periodic_freq = 0x40;
		break;
	case 8:
		rtc->periodic_freq = 0x30 | PF_HP;
		break;
	case 16:
		rtc->periodic_freq = 0x30;
		break;
	case 32:
		rtc->periodic_freq = 0x20 | PF_HP;
		break;
	case 64:
		rtc->periodic_freq = 0x20;
		break;
	case 128:
		rtc->periodic_freq = 0x10 | PF_HP;
		break;
	case 256:
		rtc->periodic_freq = 0x10;
		break;
	default:
		ret = -ENOTSUPP;
	}

	if (ret == 0) {
		rtc->periodic_freq |= tmp;
		rtc->rtc_dev->irq_freq = freq;
	}

	spin_unlock_irq(&rtc->lock);
	return ret;
}

static inline void mpc5200_rtc_setaie(struct device *dev, unsigned int enable)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(dev);
	unsigned int tmp;

	spin_lock_irq(&rtc->lock);

	tmp = readb(rtc->regs + RCR1);

	if (!enable)
		tmp &= ~RCR1_AIE;
	else
		tmp |= RCR1_AIE;

	writeb(tmp, rtc->regs + RCR1);

	spin_unlock_irq(&rtc->lock);
}

static void mpc5200_rtc_release(struct device *dev)
{
	mpc5200_rtc_setpie(dev, 0);
	mpc5200_rtc_setaie(dev, 0);
}

static int mpc5200_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(dev);
	unsigned int tmp;

	tmp = readb(rtc->regs + RCR1);
	seq_printf(seq, "carry_IRQ\t: %s\n", (tmp & RCR1_CIE) ? "yes" : "no");

	tmp = readb(rtc->regs + RCR2);
	seq_printf(seq, "periodic_IRQ\t: %s\n",
		   (tmp & RCR2_PESMASK) ? "yes" : "no");

	return 0;
}

static int mpc5200_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(dev);
	unsigned int ret = 0;

	switch (cmd) {
	case RTC_PIE_OFF:
	case RTC_PIE_ON:
		mpc5200_rtc_setpie(dev, cmd == RTC_PIE_ON);
		break;
	case RTC_AIE_OFF:
	case RTC_AIE_ON:
		mpc5200_rtc_setaie(dev, cmd == RTC_AIE_ON);
		break;
	case RTC_UIE_OFF:
		rtc->periodic_freq &= ~PF_OXS;
		break;
	case RTC_UIE_ON:
		rtc->periodic_freq |= PF_OXS;
		break;
	case RTC_IRQP_READ:
		ret = put_user(rtc->rtc_dev->irq_freq,
			       (unsigned long __user *)arg);
		break;
	case RTC_IRQP_SET:
		ret = mpc5200_rtc_setfreq(dev, arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

static int mpc5200_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(dev);
	unsigned int time, date;

	spin_lock_irq(&rtc->lock);

	time = in_32be(&rtc->regs->current_time);
	date = in_32be(&rtc->regs->current_date);

	spin_unlock_irq(&rtc->lock);

	tm->tm_sec	= time & 0xFF;
	tm->tm_min	= (time >> 8) & 0xFF;
	tm->tm_hour	= (time >> 16) & 0xFF;

	tm->tm_year	= (date & 0xFFFF) - 1900;
	tm->tm_mday	= (date >> 16) & 0x1F;
	tm->tm_wday	= (date >> 21) & 0x7;
	tm->tm_mon	= (date >> 24) & 0xF;

	dev_dbg(dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon + 1, tm->tm_year, tm->tm_wday);

	if (rtc_valid_tm(tm) < 0) {
		dev_err(dev, "invalid date\n");
		rtc_time_to_tm(0, tm);
	}

	return 0;
}

static int mpc5200_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(dev);
	unsigned int time, date;

	spin_lock_irq(&rtc->lock);

	time = tm->tm_hour << 16 | tm->tm_min << 8 | tm->tm_sec;
	date = tm->tm_mon + 1 << 16 | tm->tm_wday << 8 | tm->tm_mday;

	out_be32(&rtc->regs->time_set, PAUSE_TIME | time);
	out_be32(&rtc->regs->time_set, PAUSE_TIME | SET_TIME | time);
	out_be32(&rtc->regs->time_set, PAUSE_TIME | time);
	out_be32(&rtc->regs->time_set, time);

	out_be32(&rtc->regs->stopwatch, tm->tm_year + 1900);

	out_be32(&rtc->regs->date_set, PAUSE_TIME | date);
	out_be32(&rtc->regs->date_set, PAUSE_TIME | SET_TIME | date);
	out_be32(&rtc->regs->date_set, PAUSE_TIME | date);
	out_be32(&rtc->regs->date_set, date);

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static inline int mpc5200_rtc_read_alarm_value(struct mpc5200_rtc *rtc, int reg_off)
{
	unsigned int byte;
	int value = 0xff;	/* return 0xff for ignored values */

	byte = readb(rtc->regs + reg_off);
	if (byte & AR_ENB) {
		byte &= ~AR_ENB;	/* strip the enable bit */
		value = BCD2BIN(byte);
	}

	return value;
}

static int mpc5200_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &wkalrm->time;

	spin_lock_irq(&rtc->lock);

	tm->tm_sec	= mpc5200_rtc_read_alarm_value(rtc, RSECAR);
	tm->tm_min	= mpc5200_rtc_read_alarm_value(rtc, RMINAR);
	tm->tm_hour	= mpc5200_rtc_read_alarm_value(rtc, RHRAR);
	tm->tm_wday	= mpc5200_rtc_read_alarm_value(rtc, RWKAR);
	tm->tm_mday	= mpc5200_rtc_read_alarm_value(rtc, RDAYAR);
	tm->tm_mon	= mpc5200_rtc_read_alarm_value(rtc, RMONAR);
	if (tm->tm_mon > 0)
		tm->tm_mon -= 1; /* RTC is 1-12, tm_mon is 0-11 */
	tm->tm_year     = 0xffff;

	wkalrm->enabled = (readb(rtc->regs + RCR1) & RCR1_AIE) ? 1 : 0;

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static inline void mpc5200_rtc_write_alarm_value(struct mpc5200_rtc *rtc,
					    int value, int reg_off)
{
	/* < 0 for a value that is ignored */
	if (value < 0)
		writeb(0, rtc->regs + reg_off);
	else
		writeb(BIN2BCD(value) | AR_ENB,  rtc->regs + reg_off);
}

static int mpc5200_rtc_check_alarm(struct rtc_time *tm)
{
	/*
	 * The original rtc says anything > 0xc0 is "don't care" or "match
	 * all" - most users use 0xff but rtc-dev uses -1 for the same thing.
	 * The original rtc doesn't support years - some things use -1 and
	 * some 0xffff. We use -1 to make out tests easier.
	 */
	if (tm->tm_year == 0xffff)
		tm->tm_year = -1;
	if (tm->tm_mon >= 0xff)
		tm->tm_mon = -1;
	if (tm->tm_mday >= 0xff)
		tm->tm_mday = -1;
	if (tm->tm_wday >= 0xff)
		tm->tm_wday = -1;
	if (tm->tm_hour >= 0xff)
		tm->tm_hour = -1;
	if (tm->tm_min >= 0xff)
		tm->tm_min = -1;
	if (tm->tm_sec >= 0xff)
		tm->tm_sec = -1;

	if (tm->tm_year > 9999 ||
		tm->tm_mon >= 12 ||
		tm->tm_mday == 0 || tm->tm_mday >= 32 ||
		tm->tm_wday >= 7 ||
		tm->tm_hour >= 24 ||
		tm->tm_min >= 60 ||
		tm->tm_sec >= 60)
		return -EINVAL;

	return 0;
}

static int mpc5200_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(dev);
	unsigned int rcr1;
	struct rtc_time *tm = &wkalrm->time;
	int mon, err;

	err = mpc5200_rtc_check_alarm(tm);
	if (unlikely(err < 0))
		return err;

	spin_lock_irq(&rtc->lock);

	/* disable alarm interrupt and clear the alarm flag */
	rcr1 = readb(rtc->regs + RCR1);
	rcr1 &= ~(RCR1_AF | RCR1_AIE);
	writeb(rcr1, rtc->regs + RCR1);

	/* set alarm time */
	mpc5200_rtc_write_alarm_value(rtc, tm->tm_sec,  RSECAR);
	mpc5200_rtc_write_alarm_value(rtc, tm->tm_min,  RMINAR);
	mpc5200_rtc_write_alarm_value(rtc, tm->tm_hour, RHRAR);
	mpc5200_rtc_write_alarm_value(rtc, tm->tm_wday, RWKAR);
	mpc5200_rtc_write_alarm_value(rtc, tm->tm_mday, RDAYAR);
	mon = tm->tm_mon;
	if (mon >= 0)
		mon += 1;
	mpc5200_rtc_write_alarm_value(rtc, mon, RMONAR);

	if (wkalrm->enabled) {
		rcr1 |= RCR1_AIE;
		writeb(rcr1, rtc->regs + RCR1);
	}

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static int mpc5200_rtc_irq_set_state(struct device *dev, int enabled)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(dev);

	if (enabled) {
		rtc->periodic_freq |= PF_KOU;
		return mpc5200_rtc_ioctl(dev, RTC_PIE_ON, 0);
	} else {
		rtc->periodic_freq &= ~PF_KOU;
		return mpc5200_rtc_ioctl(dev, RTC_PIE_OFF, 0);
	}
}

static int mpc5200_rtc_irq_set_freq(struct device *dev, int freq)
{
	return mpc5200_rtc_ioctl(dev, RTC_IRQP_SET, freq);
}

static struct rtc_class_ops mpc5200_rtc_ops = {
	.release	= mpc5200_rtc_release,
	.ioctl		= mpc5200_rtc_ioctl,
	.read_time	= mpc5200_rtc_read_time,
	.set_time	= mpc5200_rtc_set_time,
	.read_alarm	= mpc5200_rtc_read_alarm,
	.set_alarm	= mpc5200_rtc_set_alarm,
	.irq_set_state	= mpc5200_rtc_irq_set_state,
	.irq_set_freq	= mpc5200_rtc_irq_set_freq,
	.proc		= mpc5200_rtc_proc,
};

static int __devinit mpc5200_rtc_of_probe(struct of_device *op,
					  const struct of_device_id *match)
{
	struct mpc5200_rtc *rtc;
	int rc = -ENODEV;

	dev_dbg(&op->dev, "probing mpc5200 RTC device\n");

	rtc = kzalloc(sizeof(struct mpc5200_rtc), GFP_KERNEL);
	if (unlikely(!rtc))
		return -ENOMEM;

	/* MMIO registers */
	rtc->regs = of_iomap(op->node, 0);
	if (!rtc->regs)
		goto err_free;

	spin_lock_init(&rtc->lock);

	/* get periodic/alarm irqs */
	rc = rtc->periodic_irq = irq_of_parse_and_map(op->node, 0);
	if (unlikely(rc < 0)) {
		dev_err(&op->dev, "No IRQ for period\n");
		goto err_unmap;
	}
	rc = rtc->alarm_irq = irq_of_parse_and_map(op->node, 1);
	if (unlikely(rc < 0)) {
		dev_err(&op->dev, "No IRQ for alarm\n");
		free_irq(rtc->periodic_irq, rtc);
		goto err_unmap;
	}
	rtc->rtc_dev->max_user_freq = 256;
	rtc->rtc_dev->irq_freq = 1;
	rtc->periodic_freq = 0x60;

	dev_set_drvdata(&op->dev, rtc);

	/* Decide if interrupts can be used */
	if ((rtc->periodic_irq != NO_IRQ) && (rtc->alarm_irq != NO_IRQ)) {
		rc = request_irq(rtc->periodic_irq, mpc5200_rtc_periodic, IRQF_DISABLED,
				  "mpc5200-rtc periodic", rtc);
		rc |= request_irq(rtc->alarm_irq, mpc5200_rtc_alarm, IRQF_DISABLED,
				  "mpc5200-rtc alarm", rtc);
		if (rc) {
			free_irq(rtc->periodic_irq, rtc);
			free_irq(rtc->alarm_irq, rtc);
			rtc->periodic_irq = rtc->alarm_irq = NO_IRQ;
			dev_info(&op->dev, "using polled mode\n");
		}
	} else {
		/* operate in polled mode */
		rtc->periodic_irq = rtc->alarm_irq = NO_IRQ;
		dev_info(&op->dev, "using polled mode\n");
	}
	return 0;

err_unmap:
	iounmap(rtc->regs);
err_free:
	kfree(rtc);
	return rc;
}

static int __devexit mpc5200_rtc_of_remove(struct of_device *op)
{
	struct mpc5200_rtc *rtc = dev_get_drvdata(&op->dev);

	if (likely(rtc->rtc_dev))
		rtc_device_unregister(rtc->rtc_dev);

	mpc5200_rtc_setpie(&op->dev, 0);
	mpc5200_rtc_setaie(&op->dev, 0);

	free_irq(rtc->periodic_irq, rtc);
	free_irq(rtc->alarm_irq, rtc);

	iounmap(rtc->regs);

	dev_set_drvdata(&op->dev, NULL);

	kfree(rtc);

	return 0;
}

static struct of_device_id mpc5200_rtc_of_match[] __devinitdata = {
	{ .compatible = "fsl,mpc5200-rtc", },
	{}
};
MODULE_DEVICE_TABLE(of, mpc5200_rtc_of_match);

static struct of_platform_driver mpc5200_rtc_of_driver = {
	.owner = THIS_MODULE,
	.name = "rtc-mpc5200",
	.match_table = mpc5200_rtc_of_match,
	.probe = mpc5200_rtc_of_probe,
	.remove = __exit_p(mpc5200_rtc_of_remove),
};

static int __init mpc5200_rtc_init(void)
{
	return of_register_platform_driver(&mpc5200_rtc_of_driver);
}
module_init(mpc5200_rtc_init);

static void __exit mpc5200_rtc_exit(void)
{
	of_unregister_platform_driver(&mpc5200_rtc_of_driver);
}
module_exit(mpc5200_rtc_exit);

MODULE_DESCRIPTION("MPC5200 RTC driver");
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("Jon Smirl <jonsmirl@gmail.com>");
MODULE_LICENSE("GPL");
