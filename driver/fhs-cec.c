// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * FHS CEC dongle: an STM32 HDMI-CEC adapter on USB.
 */

#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <media/cec.h>

#include "fhs-cec-proto.h"

#define FHS_CEC_CTRL_TIMEOUT_MS 1000
#define FHS_CEC_EVENT_FIFO_SIZE 32

#define FHS_CEC_REQ_OUT (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE)
#define FHS_CEC_REQ_IN (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE)

static int phys_addr = -1;
module_param(phys_addr, int, 0444);
MODULE_PARM_DESC(phys_addr,
		 "Physical address to use instead of the one stored in the dongle, e.g. 0x1000 for 1.0.0.0");

struct fhs_cec_event {
	ktime_t ts;
	u8 len;
	u8 data[FHS_CEC_EVENT_PACKET_SIZE];
};

struct fhs_cec {
	struct usb_device *udev;
	struct usb_interface *intf;
	struct cec_adapter *adap;
	u8 ifnum;

	struct urb *urb;
	u8 *urb_buf;

	/* Taken around every control request so none can start after disconnect. */
	struct mutex io_lock;
	bool disconnected;

	DECLARE_KFIFO(events, struct fhs_cec_event, FHS_CEC_EVENT_FIFO_SIZE);
	spinlock_t events_lock; /* events: URB completion vs event_work */
	struct work_struct event_work;

	spinlock_t tx_lock; /* tx_pending, tx_seq: transmit vs event_work */
	bool tx_pending;
	u8 tx_seq;

	u16 log_addr_mask;
};

static int fhs_cec_out_locked(struct fhs_cec *fc, u8 request, u16 value,
			      const void *data, u16 size)
{
	lockdep_assert_held(&fc->io_lock);

	if (fc->disconnected)
		return -ENODEV;
	return usb_control_msg_send(fc->udev, 0, request, FHS_CEC_REQ_OUT,
				    value, fc->ifnum, data, size,
				    FHS_CEC_CTRL_TIMEOUT_MS, GFP_KERNEL);
}

static int fhs_cec_out(struct fhs_cec *fc, u8 request, u16 value,
		       const void *data, u16 size)
{
	int ret;

	mutex_lock(&fc->io_lock);
	ret = fhs_cec_out_locked(fc, request, value, data, size);
	mutex_unlock(&fc->io_lock);
	return ret;
}

static int fhs_cec_in(struct fhs_cec *fc, u8 request, void *data, u16 size)
{
	int ret;

	mutex_lock(&fc->io_lock);
	if (fc->disconnected)
		ret = -ENODEV;
	else
		ret = usb_control_msg_recv(fc->udev, 0, request,
					   FHS_CEC_REQ_IN, 0, fc->ifnum,
					   data, size, FHS_CEC_CTRL_TIMEOUT_MS,
					   GFP_KERNEL);
	mutex_unlock(&fc->io_lock);
	return ret;
}

static int fhs_cec_get_info(struct fhs_cec *fc, struct fhs_cec_info *info)
{
	return fhs_cec_in(fc, FHS_CEC_REQ_GET_INFO, info, sizeof(*info));
}

static u16 fhs_cec_info_phys_addr(const struct fhs_cec_info *info)
{
	return le16_to_cpu((__force __le16)info->phys_addr);
}

static void fhs_cec_clear_tx(struct fhs_cec *fc)
{
	spin_lock(&fc->tx_lock);
	fc->tx_pending = false;
	spin_unlock(&fc->tx_lock);
}

static int fhs_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct fhs_cec *fc = cec_get_drvdata(adap);
	int ret;

	fhs_cec_clear_tx(fc);
	if (!enable)
		fc->log_addr_mask = 0;

	ret = fhs_cec_out(fc, FHS_CEC_REQ_SET_ENABLE, enable, NULL, 0);
	return enable ? ret : 0;
}

static int fhs_cec_adap_log_addr(struct cec_adapter *adap, u8 log_addr)
{
	struct fhs_cec *fc = cec_get_drvdata(adap);
	int ret;

	if (log_addr == CEC_LOG_ADDR_INVALID)
		fc->log_addr_mask = 0;
	else
		fc->log_addr_mask |= BIT(log_addr);

	ret = fhs_cec_out(fc, FHS_CEC_REQ_SET_LOG_ADDRS, fc->log_addr_mask,
			  NULL, 0);
	return log_addr == CEC_LOG_ADDR_INVALID ? 0 : ret;
}

static int fhs_cec_adap_monitor_all_enable(struct cec_adapter *adap,
					   bool enable)
{
	struct fhs_cec *fc = cec_get_drvdata(adap);
	int ret;

	ret = fhs_cec_out(fc, FHS_CEC_REQ_SET_MONITOR_ALL, enable, NULL, 0);
	return enable ? ret : 0;
}

/*
 * One attempt per call: the core retries on NACK and arbitration loss.
 * signal_free_time is unused, the STM32 peripheral applies the CEC spec
 * values itself.
 */
static int fhs_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				 u32 signal_free_time, struct cec_msg *msg)
{
	struct fhs_cec *fc = cec_get_drvdata(adap);
	u8 seq;
	int ret;

	spin_lock(&fc->tx_lock);
	seq = ++fc->tx_seq;
	fc->tx_pending = true;
	spin_unlock(&fc->tx_lock);

	ret = fhs_cec_out(fc, FHS_CEC_REQ_TRANSMIT, seq, msg->msg, msg->len);
	if (ret)
		fhs_cec_clear_tx(fc);
	return ret;
}

static void fhs_cec_adap_status(struct cec_adapter *adap,
				struct seq_file *file)
{
	struct fhs_cec *fc = cec_get_drvdata(adap);
	struct fhs_cec_status st;
	struct fhs_cec_info info;

	if (fhs_cec_get_info(fc, &info) ||
	    fhs_cec_in(fc, FHS_CEC_REQ_GET_STATUS, &st, sizeof(st))) {
		seq_puts(file, "device not responding\n");
		return;
	}

	seq_printf(file, "firmware: %u.%u.%u, protocol %u.%u\n",
		   info.fw_major, info.fw_minor, info.fw_patch,
		   info.proto_major, info.proto_minor);
	seq_printf(file, "rx frames: %u, errors: %u, dropped: %u\n",
		   le32_to_cpu((__force __le32)st.rx_frames),
		   le32_to_cpu((__force __le32)st.rx_errors),
		   le32_to_cpu((__force __le32)st.rx_dropped));
	seq_printf(file, "tx frames: %u, nack: %u, arb lost: %u, low drive: %u, error: %u\n",
		   le32_to_cpu((__force __le32)st.tx_frames),
		   le32_to_cpu((__force __le32)st.tx_nack),
		   le32_to_cpu((__force __le32)st.tx_arb_lost),
		   le32_to_cpu((__force __le32)st.tx_low_drive),
		   le32_to_cpu((__force __le32)st.tx_error));
	seq_printf(file, "events dropped: %u, recoveries: %u\n",
		   le32_to_cpu((__force __le32)st.events_dropped),
		   le32_to_cpu((__force __le32)st.recoveries));
}

static void fhs_cec_free(struct fhs_cec *fc)
{
	usb_free_urb(fc->urb);
	kfree(fc->urb_buf);
	kfree(fc);
}

static void fhs_cec_adap_free(struct cec_adapter *adap)
{
	struct fhs_cec *fc = cec_get_drvdata(adap);

	cancel_work_sync(&fc->event_work);
	fhs_cec_free(fc);
}

static const struct cec_adap_ops fhs_cec_adap_ops = {
	.adap_enable = fhs_cec_adap_enable,
	.adap_log_addr = fhs_cec_adap_log_addr,
	.adap_monitor_all_enable = fhs_cec_adap_monitor_all_enable,
	.adap_transmit = fhs_cec_adap_transmit,
	.adap_status = fhs_cec_adap_status,
	.adap_free = fhs_cec_adap_free,
};

static u8 fhs_cec_tx_status(u8 status)
{
	switch (status) {
	case FHS_CEC_TX_OK:
		return CEC_TX_STATUS_OK;
	case FHS_CEC_TX_NACK:
		return CEC_TX_STATUS_NACK;
	case FHS_CEC_TX_ARB_LOST:
		return CEC_TX_STATUS_ARB_LOST;
	case FHS_CEC_TX_LOW_DRIVE:
		return CEC_TX_STATUS_LOW_DRIVE;
	default:
		return CEC_TX_STATUS_ERROR;
	}
}

static void fhs_cec_tx_done(struct fhs_cec *fc, const struct fhs_cec_event *ev)
{
	const struct fhs_cec_ev_tx_done *done = (const void *)ev->data;
	bool current_tx;

	if (ev->len < sizeof(*done))
		return;

	/* A completion that arrives after the core gave up on it belongs to no one. */
	spin_lock(&fc->tx_lock);
	current_tx = fc->tx_pending && done->seq == fc->tx_seq;
	if (current_tx)
		fc->tx_pending = false;
	spin_unlock(&fc->tx_lock);

	if (!current_tx) {
		dev_dbg(&fc->intf->dev, "stale tx done, seq %u\n", done->seq);
		return;
	}
	cec_transmit_attempt_done_ts(fc->adap, fhs_cec_tx_status(done->status),
				     ev->ts);
}

static void fhs_cec_rx(struct fhs_cec *fc, const struct fhs_cec_event *ev)
{
	const struct fhs_cec_ev_rx *rx = (const void *)ev->data;
	struct cec_msg msg = {};

	if (ev->len < offsetof(struct fhs_cec_ev_rx, frame))
		return;
	if (rx->len == 0 || rx->len > FHS_CEC_FRAME_MAX ||
	    ev->len < offsetof(struct fhs_cec_ev_rx, frame) + rx->len)
		return;

	msg.len = rx->len;
	memcpy(msg.msg, rx->frame, rx->len);
	cec_received_msg_ts(fc->adap, &msg, ev->ts);
}

static void fhs_cec_event_work(struct work_struct *work)
{
	struct fhs_cec *fc = container_of(work, struct fhs_cec, event_work);
	struct fhs_cec_event ev;

	while (kfifo_out_spinlocked(&fc->events, &ev, 1, &fc->events_lock)) {
		switch (ev.data[0]) {
		case FHS_CEC_EV_TX_DONE:
			fhs_cec_tx_done(fc, &ev);
			break;
		case FHS_CEC_EV_RX:
			fhs_cec_rx(fc, &ev);
			break;
		default:
			dev_dbg(&fc->intf->dev, "unknown event %#x\n",
				ev.data[0]);
			break;
		}
	}
}

static void fhs_cec_queue_event(struct fhs_cec *fc, u32 len)
{
	struct fhs_cec_event ev;

	ev.ts = ktime_get();
	ev.len = min_t(u32, len, sizeof(ev.data));
	memcpy(ev.data, fc->urb_buf, ev.len);
	if (kfifo_in_spinlocked(&fc->events, &ev, 1, &fc->events_lock))
		schedule_work(&fc->event_work);
	else
		dev_warn_ratelimited(&fc->intf->dev,
				     "event queue full, event dropped\n");
}

static void fhs_cec_urb_complete(struct urb *urb)
{
	struct fhs_cec *fc = urb->context;
	int ret;

	switch (urb->status) {
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return;
	case 0:
		if (urb->actual_length > 0)
			fhs_cec_queue_event(fc, urb->actual_length);
		break;
	default:
		dev_dbg_ratelimited(&fc->intf->dev, "event urb status %d\n",
				    urb->status);
		break;
	}

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -EPERM && ret != -ENODEV)
		dev_err_ratelimited(&fc->intf->dev,
				    "event urb resubmit failed: %d\n", ret);
}

static int fhs_cec_parse_phys_addr(const char *buf, u16 *pa)
{
	unsigned int a, b, c, d;

	if (sscanf(buf, "%x.%x.%x.%x", &a, &b, &c, &d) == 4) {
		if (a > 0xf || b > 0xf || c > 0xf || d > 0xf)
			return -EINVAL;
		*pa = a << 12 | b << 8 | c << 4 | d;
		return 0;
	}
	return kstrtou16(buf, 16, pa);
}

/* Once a port is 0, the ones below it must be too: 1.0.2.0 is not a valid address. */
static bool fhs_cec_phys_addr_valid(u16 pa)
{
	bool seen_zero = false;
	int shift;

	if (pa == CEC_PHYS_ADDR_INVALID)
		return true;
	for (shift = 12; shift >= 0; shift -= 4) {
		bool zero = !((pa >> shift) & 0xf);

		if (seen_zero && !zero)
			return false;
		seen_zero |= zero;
	}
	return true;
}

static ssize_t stored_phys_addr_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct fhs_cec *fc = usb_get_intfdata(to_usb_interface(dev));
	struct fhs_cec_info info;
	u16 pa;
	int ret;

	ret = fhs_cec_get_info(fc, &info);
	if (ret)
		return ret;

	pa = fhs_cec_info_phys_addr(&info);
	return sysfs_emit(buf, "%x.%x.%x.%x\n", cec_phys_addr_exp(pa));
}

static ssize_t stored_phys_addr_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct fhs_cec *fc = usb_get_intfdata(to_usb_interface(dev));
	u16 pa;
	int ret;

	ret = fhs_cec_parse_phys_addr(buf, &pa);
	if (ret)
		return ret;
	if (!fhs_cec_phys_addr_valid(pa))
		return -EINVAL;

	ret = fhs_cec_out(fc, FHS_CEC_REQ_SET_PHYS_ADDR, pa, NULL, 0);
	if (ret)
		return ret;

	if (phys_addr < 0)
		cec_s_phys_addr(fc->adap, pa, false);
	return count;
}
static DEVICE_ATTR_RW(stored_phys_addr);

static struct attribute *fhs_cec_attrs[] = {
	&dev_attr_stored_phys_addr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(fhs_cec);

static u16 fhs_cec_initial_phys_addr(struct fhs_cec *fc,
				     const struct fhs_cec_info *info)
{
	if (phys_addr < 0)
		return fhs_cec_info_phys_addr(info);
	if (phys_addr > CEC_PHYS_ADDR_INVALID ||
	    !fhs_cec_phys_addr_valid(phys_addr)) {
		dev_warn(&fc->intf->dev,
			 "phys_addr %#x is not a valid address, using the stored one\n",
			 phys_addr);
		return fhs_cec_info_phys_addr(info);
	}
	return phys_addr;
}

static struct fhs_cec *fhs_cec_create(struct usb_interface *intf,
				      const struct usb_endpoint_descriptor *ep)
{
	struct fhs_cec *fc;

	fc = kzalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return ERR_PTR(-ENOMEM);

	fc->udev = interface_to_usbdev(intf);
	fc->intf = intf;
	fc->ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	mutex_init(&fc->io_lock);
	spin_lock_init(&fc->tx_lock);
	spin_lock_init(&fc->events_lock);
	INIT_KFIFO(fc->events);
	INIT_WORK(&fc->event_work, fhs_cec_event_work);

	fc->urb = usb_alloc_urb(0, GFP_KERNEL);
	fc->urb_buf = kmalloc(FHS_CEC_EVENT_PACKET_SIZE, GFP_KERNEL);
	if (!fc->urb || !fc->urb_buf) {
		fhs_cec_free(fc);
		return ERR_PTR(-ENOMEM);
	}
	usb_fill_int_urb(fc->urb, fc->udev,
			 usb_rcvintpipe(fc->udev, usb_endpoint_num(ep)),
			 fc->urb_buf, FHS_CEC_EVENT_PACKET_SIZE,
			 fhs_cec_urb_complete, fc, ep->bInterval);
	return fc;
}

static int fhs_cec_alloc_adapter(struct fhs_cec *fc, struct fhs_cec_info *info)
{
	u32 caps = CEC_CAP_DEFAULTS | CEC_CAP_PHYS_ADDR;
	int ret;

	ret = fhs_cec_get_info(fc, info);
	if (ret) {
		dev_err(&fc->intf->dev, "cannot read device info: %d\n", ret);
		return ret;
	}
	if (info->proto_major != FHS_CEC_PROTO_MAJOR) {
		dev_err(&fc->intf->dev,
			"firmware speaks protocol %u.%u, driver needs %u.x: update the %s\n",
			info->proto_major, info->proto_minor, FHS_CEC_PROTO_MAJOR,
			info->proto_major < FHS_CEC_PROTO_MAJOR ? "firmware" : "driver");
		return -ENODEV;
	}

	if (le32_to_cpu((__force __le32)info->caps) & FHS_CEC_CAP_MONITOR_ALL)
		caps |= CEC_CAP_MONITOR_ALL;

	fc->adap = cec_allocate_adapter(&fhs_cec_adap_ops, fc,
					dev_name(&fc->intf->dev), caps,
					CEC_MAX_LOG_ADDRS);
	return PTR_ERR_OR_ZERO(fc->adap);
}

static int fhs_cec_start(struct fhs_cec *fc)
{
	int ret;

	ret = usb_submit_urb(fc->urb, GFP_KERNEL);
	if (ret)
		return ret;

	ret = cec_register_adapter(fc->adap, &fc->intf->dev);
	if (ret)
		usb_kill_urb(fc->urb);
	return ret;
}

static int fhs_cec_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep;
	struct fhs_cec_info info;
	struct fhs_cec *fc;
	u16 pa;
	int ret;

	ret = usb_find_int_in_endpoint(intf->cur_altsetting, &ep);
	if (ret)
		return ret;

	fc = fhs_cec_create(intf, ep);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

	ret = fhs_cec_alloc_adapter(fc, &info);
	if (ret) {
		fhs_cec_free(fc);
		return ret;
	}

	usb_set_intfdata(intf, fc);
	ret = fhs_cec_start(fc);
	if (ret) {
		usb_set_intfdata(intf, NULL);
		/* Frees fc through adap_free. */
		cec_delete_adapter(fc->adap);
		return ret;
	}

	pa = fhs_cec_initial_phys_addr(fc, &info);
	if (pa != FHS_CEC_PHYS_ADDR_INVALID)
		cec_s_phys_addr(fc->adap, pa, false);

	dev_info(&intf->dev, "firmware %u.%u.%u, physical address %x.%x.%x.%x\n",
		 info.fw_major, info.fw_minor, info.fw_patch,
		 cec_phys_addr_exp(pa));
	return 0;
}

static void fhs_cec_disconnect(struct usb_interface *intf)
{
	struct fhs_cec *fc = usb_get_intfdata(intf);

	usb_kill_urb(fc->urb);
	cancel_work_sync(&fc->event_work);

	mutex_lock(&fc->io_lock);
	/* An unbind leaves the dongle powered, so stop it ACKing; fails on unplug. */
	fhs_cec_out_locked(fc, FHS_CEC_REQ_SET_ENABLE, 0, NULL, 0);
	fc->disconnected = true;
	mutex_unlock(&fc->io_lock);

	usb_set_intfdata(intf, NULL);
	/* fc lives on in adap_free until the last open file handle closes. */
	cec_unregister_adapter(fc->adap);
}

static const struct usb_device_id fhs_cec_ids[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(FHS_CEC_USB_VID, FHS_CEC_USB_PID,
					FHS_CEC_USB_CLASS, FHS_CEC_USB_SUBCLASS,
					FHS_CEC_USB_PROTOCOL) },
	{ }
};
MODULE_DEVICE_TABLE(usb, fhs_cec_ids);

static struct usb_driver fhs_cec_driver = {
	.name = "fhs-cec",
	.probe = fhs_cec_probe,
	.disconnect = fhs_cec_disconnect,
	.id_table = fhs_cec_ids,
	.dev_groups = fhs_cec_groups,
};
module_usb_driver(fhs_cec_driver);

MODULE_AUTHOR("FUTO");
MODULE_DESCRIPTION("FHS USB HDMI-CEC adapter");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION(FHS_CEC_VERSION);
