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

// Recommended by the manufacturer.
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

	int last_pump_rpm;
	int last_pump_duty;
	int last_liquid_temp;
	bool suspended;
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
			*ret = READ_ONCE(krdev->last_liquid_temp);
			break;
		default:
			r = -EOPNOTSUPP;
		}
		break;
	case hwmon_fan:
		*ret = READ_ONCE(krdev->last_pump_rpm);
		break;

	case hwmon_pwm:
		*ret = READ_ONCE(krdev->last_pump_duty) * 255 / 100;
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
	int r;
	int i;
	uint64_t value;
	struct krkn_device *krdev;
	char *buf;
	uint64_t *buf64;

	krdev = (struct krkn_device*) dev_get_drvdata(device);
	buf = kcalloc(DEV_RECVBUFSZ, 1, GFP_KERNEL);
	buf64 = (uint64_t*) &buf[4];

	if (val < 0 || val > 255) {
		return -EINVAL;
	}
	val = (100 * val) / 255; /* Scale between 0 and 100 */

	value = (val << 8) | val;
	value |= value << 16;
	value |= value << 32;

	buf[0] = 0x72; /* Header */
	buf[1] = 0x1;  /* Speed control address (this device has only one) */

	for (i = 0; i < 5; i++) {
		buf64[i] = value;
	}

	if ((r = hid_hw_output_report(krdev->hdev, buf, DEV_RECVBUFSZ)) < 0) {
		goto clean;
	}

	if (r != DEV_RECVBUFSZ) {
		hid_warn(krdev->hdev, "Expected to write %d bytes into device, but %d were written.", DEV_RECVBUFSZ, r);
	}

	r = 0;

 clean:
	kfree(buf);
	return r;
}

static void krknmon_urb_resubmit(struct krkn_device *krdev)
{
	int r;

	switch (r = usb_submit_urb(krdev->urb, GFP_ATOMIC)) {
	case 0:
	case -EPERM:		/* Returned if usb_kill_urb is called */
	case -ENODEV:		/* Returned if device detached */
	case -ESHUTDOWN:	/* Returned if device has been suspended */
		break;

	default:
		hid_err(krdev->hdev, "Unable to resubmit sensor update request. The sensor information might not be updated again unless the module is reloaded. Beware. Error: %d", r);
	}
}

static void krknmon_usb_isr(struct urb *urb)
{
	struct krkn_device *krdev;
	u8 *buf;
	unsigned int pumpspd;
	unsigned int pumpduty;
	unsigned int ltemp;
	bool suspnd;

	krdev = (struct krkn_device*) urb->context;

	switch (urb->status) {
	case 0:
		break;
	case -ESHUTDOWN:	/* Interface shutted down. Suspended? */
	case -ECONNRESET:	/* Device unlinked */
	case -ENOENT:
		return;		/* Don't resubmit in this cases */
	default:
		goto resubmit;
	}

	buf = krdev->recvbuf;

	pumpspd = (buf[18] << 8) | buf[17];
	pumpduty = buf[19];
	ltemp = buf[15] * 1000 + buf[16] * 100;

	WRITE_ONCE(krdev->last_liquid_temp, ltemp);
	WRITE_ONCE(krdev->last_pump_rpm, pumpspd);
	WRITE_ONCE(krdev->last_pump_duty, pumpduty);
	suspnd = READ_ONCE(krdev->suspended);

	if (suspnd) {
		return;
	}

 resubmit:
	krknmon_urb_resubmit(krdev);
}

#ifdef CONFIG_PM
static int krknmon_suspend(struct hid_device *hdev, pm_message_t msg)
{
	struct krkn_device *krdev;

	krdev = hid_get_drvdata(hdev);

	if (PMSG_IS_AUTO(msg)) {
		// Just for debug, I still not sure about how autosuspend works.
		hid_err(hdev, "Attempt to autosuspend device. This must not happen");
	} else {
		hid_info(hdev, "Device suspended");
		WRITE_ONCE(krdev->suspended, true);
		usb_kill_urb(krdev->urb);
	}
	return 0;
}

static int krknmon_resume(struct hid_device *hdev)
{
	struct krkn_device *krdev;

	krdev = hid_get_drvdata(hdev);
	WRITE_ONCE(krdev->suspended, false);

	hid_info(hdev, "Device resumed");

	krknmon_urb_resubmit(krdev);
	return 0;
}
#endif	/* CONFIG_PM */

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

	if ((recvsz = usb_maxpacket(usb_dev, pipe)) != DEV_RECVBUFSZ) {
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

	// Init HID device and hidraw connection
	if (hid_parse(hdev)) {
		goto error;
	}

	if (hid_hw_start(hdev, HID_CONNECT_HIDRAW)) {
		goto error_clhid;
	}

	if ((krdev->recvbuf = usb_alloc_coherent(
				usb_dev, DEV_RECVBUFSZ, GFP_KERNEL,
				&krdev->recvbuf_dma)) == NULL) {

		err = -ENOMEM;
		goto error_clhid;
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

	if (IS_ENABLED(CONFIG_PM))
		usb_disable_autosuspend(usb_dev);

	// Fill and submit first USB URB.
	usb_fill_int_urb(inturb, usb_dev, pipe, krdev->recvbuf,
			DEV_RECVBUFSZ, &krknmon_usb_isr, krdev,
			usb_endp->bInterval);

	inturb->transfer_dma = krdev->recvbuf_dma;
	inturb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	if ((err = usb_submit_urb(inturb, GFP_KERNEL)) < 0) {
		goto error_clurb;
	}

	hid_info(hdev, "%s: Successfully probed device %s.", dev_name(hwmon_dev), hdev->name);

	return 0;

 error_clurb:
	usb_free_urb(inturb);

 error_clrecv:
	usb_free_coherent(usb_dev, DEV_RECVBUFSZ, krdev->recvbuf,
			krdev->recvbuf_dma);

 error_clhid:
	hid_hw_stop(hdev);

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

	hid_hw_stop(hdev);
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
	.is_visible  = krknmon_is_visible,
	.read        = krknmon_read,
	.read_string = krknmon_readstr,
	.write       = krknmon_write
};

static struct hwmon_chip_info krknmon_chip_info = {
	.ops  = &krknmon_hwops,
	.info = krknmon_chinfo
};

/*
 * We are using a HID driver to prevent conflicts with generic HID drivers
 * present on the kernel, but it should work also fine directly using a plain
 * usb driver.
 */

static struct hid_driver krknmon_driver = {
	.name     = "krknmon",
	.id_table = krknmon_dev_tbl,
	.remove   = krknmon_remove,
	.probe    = krknmon_probe,
#ifdef CONFIG_PM
	.suspend  = krknmon_suspend,
	.resume   = krknmon_resume
#endif
};

module_hid_driver(krknmon_driver);
MODULE_VERSION("0.4");
MODULE_AUTHOR("devcexx");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NZXT Kraken 4th generation AIO cooler (X53, X63, X73) HWMon driver");
