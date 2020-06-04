/*
 * This file is part of krknmon.
 *
 * krknmon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * krknmon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with krknmon.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hid.h>
#include <linux/usb.h>

#undef pr_fmt
#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt
#define STR_PROBE_ERROR "Failed to probe device: "

// Recommended by the recommendations of the manufacturer.
// See https://blog.nzxt.com/does-aio-liquid-evaporate/
#define KRKN_TEMP_CRIT 60000
#define KRKN_TEMP_MAX 58000

#define DEV_RECVBUFSZ 64

static struct hwmon_chip_info krknmon_chip_info;

struct krkn_device {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	struct usb_device *usb_dev;

	struct urb *urb;
	u8 *recvbuf;
	dma_addr_t recvbuf_dma;

	long last_pump_rpm;
	long last_liquid_temp;

	spinlock_t lock;
};

static umode_t krknmon_is_visible(const void *drvdata,
					enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	if (type == hwmon_pwm && attr == hwmon_pwm_input) {
		return 0644;
	}

	return 0444;
}

static int krknmon_read(struct device *hwmon_dev,
			enum hwmon_sensor_types type,
			u32 attr, int channel, long *ret)
{
	struct krkn_device *krdev;
	unsigned long flags;
	int r;
	
	krdev = (struct krkn_device*) dev_get_drvdata(hwmon_dev);
	r = 0;

	switch (type) {
	case hwmon_temp:
		switch(attr) {
		case hwmon_temp_crit:
			*ret = KRKN_TEMP_CRIT;
			break;
		case hwmon_temp_max:
			*ret = KRKN_TEMP_MAX;
			break;
		case hwmon_temp_input:
			spin_lock_irqsave(&krdev->lock, flags);
			*ret = krdev->last_liquid_temp;
			spin_unlock_irqrestore(&krdev->lock, flags);
			break;
		default:
			r = -EOPNOTSUPP;
		}
		break;
	case hwmon_fan:
		spin_lock_irqsave(&krdev->lock, flags);
		*ret = krdev->last_pump_rpm;
		spin_unlock_irqrestore(&krdev->lock, flags);
		break;

	default:
		r = -EOPNOTSUPP;
	}

	return r;
}

static int krknmon_readstr(struct device *device,
			enum hwmon_sensor_types type,
			u32 attr, int channel, const char **str)
{

	switch (type) {
	case hwmon_temp:
		*str = "Liquid";
		break;
	case hwmon_fan:
		*str = "Pump";
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int krknmon_write(struct device *device,
			enum hwmon_sensor_types type,
			u32 attr, int channel, long val)
{
	// TODO Not implemented yet.
	return -ENOSYS;
}

static void krknmon_usb_isr(struct urb *urb) {
	unsigned long flags;
	struct krkn_device *krdev;
	u8 *buf;
	unsigned int pumpspd;
	unsigned int ltemp;
	int r;

	krdev = (struct krkn_device*) urb->context;
	buf = krdev->recvbuf;

	pumpspd = (buf[18] << 8) | buf[17];
	ltemp = buf[15] * 1000 + buf[16] * 100;

	spin_lock_irqsave(&krdev->lock, flags);

	krdev->last_liquid_temp = ltemp;
	krdev->last_pump_rpm = pumpspd;

	spin_unlock_irqrestore(&krdev->lock, flags);

	//EPERM errors are produced by a call to usb_kill_urb. Ignore them.
	//ENODEV errors are most likely produced by a disconnection of the
	//device. Ignore too.
	if ((r = usb_submit_urb(urb, GFP_ATOMIC)) && r != -EPERM && r != -ENODEV)
		hid_err(krdev->hdev, "Unable to resubmit sensor update request. The sensor information might not be updated again unless the module is reloaded. Beware.");
}

static int krknmon_probe(struct hid_device *hdev,
			const struct hid_device_id *id)
{
	struct krkn_device *krdev;
	struct device *hwmon_dev;
	struct usb_interface *usb_intf;
	struct usb_device *usb_dev;
	struct usb_host_interface *usb_alt;
	struct usb_endpoint_descriptor *usb_endp;
	struct urb *inturb;

	int pipe;
	int recvsz;
	int err;

	err = 0;

	usb_intf = to_usb_interface(hdev->dev.parent);
	usb_dev = interface_to_usbdev(usb_intf);
	usb_alt = usb_intf->cur_altsetting;

	if (usb_alt->desc.bNumEndpoints < 1) {
		hid_err(hdev, "Expected HID interface to have at least 1 endpoint (?).");
		return -ENODEV;
	}

	usb_endp = &usb_alt->endpoint[0].desc;

	if (!usb_endpoint_is_int_in(usb_endp)) {
		hid_err(hdev, "Expected first HID interface (address %#x) to be an IN, INT interface.",
			usb_endp->bEndpointAddress);
		return -ENODEV;
	}

	pipe = usb_rcvintpipe(usb_dev, usb_endp->bEndpointAddress);

	if ((recvsz = usb_maxpacket(usb_dev, pipe, 0)) != DEV_RECVBUFSZ) {
		hid_err(hdev, "Expected USB endpoint to have a max packet size of %d, but got %d.",
			DEV_RECVBUFSZ, recvsz);
		return -ENODEV;
	}

	if (!(krdev = kzalloc(sizeof(*krdev), GFP_KERNEL))) {
		return -ENOMEM;
	}

	if (IS_ERR(hwmon_dev = hwmon_device_register_with_info(
				&hdev->dev, "krknmon", krdev,
				&krknmon_chip_info, NULL))) {

		pr_err("Could not register hwmon device.");
		err = PTR_ERR(hwmon_dev);
		goto error;
	}

	if ((krdev->recvbuf = usb_alloc_coherent(
				usb_dev, DEV_RECVBUFSZ, GFP_KERNEL,
				&krdev->recvbuf_dma)) == NULL) {

		err = -ENOMEM;
		goto error;
	}

	inturb = usb_alloc_urb(0, GFP_KERNEL);
	if (inturb == NULL) {
		err = -ENOMEM;
		goto error_clrecv;
	}

	hid_set_drvdata(hdev, krdev);

	// Init Kraken dev struct fields.
	krdev->urb = inturb;
	krdev->hdev = hdev;
	krdev->hwmon_dev = hwmon_dev;
	krdev->usb_dev = usb_dev;
	spin_lock_init(&krdev->lock);

	// Fill and submit first USB URB.
	usb_fill_int_urb(inturb, usb_dev, pipe, krdev->recvbuf,
			DEV_RECVBUFSZ, &krknmon_usb_isr, krdev,
			usb_endp->bInterval);

	inturb->transfer_dma = krdev->recvbuf_dma;
	inturb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	if ((err = usb_submit_urb(inturb, GFP_KERNEL)) < 0) {
		goto error_clurb;
	}

	hid_info(hdev, "%s: Successfully probed device.", dev_name(hwmon_dev));

	return 0;

 error_clurb:
	usb_free_urb(inturb);

 error_clrecv:
	usb_free_coherent(usb_dev, DEV_RECVBUFSZ, krdev->recvbuf,
			krdev->recvbuf_dma);

 error:
	kfree(krdev);
	return err;
}

static void krknmon_remove(struct hid_device *hdev)
{
	struct krkn_device *krdev;

	krdev = (struct krkn_device*) hid_get_drvdata(hdev);
	hwmon_device_unregister(krdev->hwmon_dev);

	usb_kill_urb(krdev->urb);
	usb_free_urb(krdev->urb);
	usb_free_coherent(krdev->usb_dev, DEV_RECVBUFSZ, krdev->recvbuf,
			krdev->recvbuf_dma);
	kfree(krdev);

	hid_info(hdev, "Device released.");
}

static struct hid_device_id krknmon_dev_tbl[] = {
	{ HID_USB_DEVICE(0x1e71, 0x2007) },
	{ }
};

static const struct hwmon_channel_info *krknmon_chinfo[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_LABEL | HWMON_T_CRIT | HWMON_T_MAX |
			HWMON_T_INPUT),

	HWMON_CHANNEL_INFO(fan, HWMON_F_LABEL | HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT),
	NULL
};

static struct hwmon_ops krknmon_hwops = {
 is_visible: krknmon_is_visible,
 read: krknmon_read,
 read_string: krknmon_readstr,
 write: krknmon_write
};

static struct hwmon_chip_info krknmon_chip_info =
{
 ops: &krknmon_hwops,
 info: krknmon_chinfo
};

/*
 * We are using a HID driver to prevent conflicts with generic HID drivers
 * present on the kernel, but it should work also fine directly using a plain
 * usb driver.
 */

static struct hid_driver krknmon_driver = {
	.name = "krknmon",
	.id_table = krknmon_dev_tbl,
	.remove = krknmon_remove,
	.probe = krknmon_probe,
};

module_hid_driver(krknmon_driver);
MODULE_VERSION("0.1");
MODULE_AUTHOR("devcexx");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NZXT Kraken 4th generation AIO cooler (X53, X63, X73) HWMon driver");
