/*
*
*  Copyright (c) 2008  Guillemot Corporation S.A. 
*
*  Philip Lukidis plukidis@guillemot.com
*  Alexis Rousseau-Dupuis
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software
*   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
*
*/
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <linux/usb.h>
#include <linux/delay.h>
#include <linux/version.h>	/* For LINUX_VERSION_CODE */
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,24) )
#include <sound/driver.h>
#endif
#include <sound/core.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/rawmidi.h>
#include "djdevioctls.h"
#include "device.h"
#include "bulk.h"
#include "configuration_manager.h"
#include "callback.h"

u8 TUSB_full[] = "FTUS";
u8 TUSB_partial[] = "TUSB";
u8 PSOCH_full[] = "PSOC";
u8 PSCU_full[] = "PSCU";

extern struct semaphore register_mutex;
extern struct snd_hdj_chip *usb_chip[SNDRV_CARDS];

int buffer_queue_depth = 0;

/* BCD, currently 1.28.0.0 */
u32 driver_version = 0x1280000;

static int can_send_urbs(struct snd_hdj_chip* chip)
{
	if (atomic_read(&chip->no_urb_submission)!=0 ||
		atomic_read(&chip->shutdown)!=0) {
		return -EBUSY;
	}
	return 0;
}

static long hdjbulk_ioctl_entry(struct file *file,	
								 unsigned int ioctl_num,	
								 unsigned long ioctl_param)
{
	return hdjbulk_ioctl(file, ioctl_num, ioctl_param, 0);
}

#ifdef CONFIG_COMPAT
static long hdjbulk_ioctl_entry_compat(struct file *file,	
										 unsigned int ioctl_num,	
										 unsigned long ioctl_param)
{
	return hdjbulk_ioctl(file, ioctl_num, ioctl_param, 1);
}
#endif

static const struct file_operations hdjbulk_fops = {
	.owner =	THIS_MODULE,
	.open =		hdjbulk_open,
	.release =	hdjbulk_release,
	.unlocked_ioctl = 	hdjbulk_ioctl_entry,
#ifdef CONFIG_COMPAT
	.compat_ioctl = hdjbulk_ioctl_entry_compat,
#endif
	.read =		hdjbulk_read,
	.poll =		hdjbulk_poll
};

/*
* usb class driver info in order to get a minor number from the usb core,
* and to have the device registered with the driver core
*/
struct usb_class_driver hdjbulk_class = {
	.name =		"hdjbulk%d",
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,16) )
	.fops =		(struct file_operations*)&hdjbulk_fops,
#else
	.fops =		&hdjbulk_fops,
#endif
	.minor_base =	USB_HDJ_MINOR_BASE,
};

/* MARK: PRODCHANGE */
u8 is_continuous_reader_supported(struct snd_hdj_chip* chip)
{
	if (chip->product_code != DJCONSOLE_PRODUCT_CODE &&
	    chip->product_code != DJCONSOLE2_PRODUCT_CODE &&
	    chip->product_code != DJCONSOLERMX_PRODUCT_CODE &&
	    chip->product_code != DJCONTROLSTEEL_PRODUCT_CODE) {
		return 0;
	}
	return 1;
}

/* MARK: PRODCHANGE */
int has_bulk_interface(struct snd_hdj_chip* chip)
{
	if (chip->product_code==DJCONSOLE_PRODUCT_CODE) {
		return 1;
	} else if 	(chip->product_code==DJCONSOLE2_PRODUCT_CODE) {
		return 1;
	} else if 	(chip->product_code==DJCONSOLERMX_PRODUCT_CODE) {
		return 1;
	} else if 	(chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		return 1;
	}
	return 0;
}

/* MARK: PRODCHANGE */
static int get_hid_polling_interface_number(struct usb_hdjbulk *ubulk)
{
	if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		return DJC_HID_INTERFACE_NUMBER;
	} else if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		return DJMK2_HID_INTERFACE_NUMBER;
	} else if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		return DJRMX_HID_INTERFACE_NUMBER;	
	}
	return -1;
}

/* Warning- this has not been tested, USE AT YOUR OWN RISK */
static int update_device_firmware(struct usb_hdjbulk *ubulk, u8* file_data, u16 file_size)
{
	return 0;
}

int firmware_start_bulk(struct usb_hdjbulk *ubulk, u16 index, u8 full_update)
{
	int ret = 0;
	struct hdj_steel_context* dc;
	if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, DJ_DRV_START_BULK, 0, index, NULL, 1);
	} else if ((ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE)||
		(ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE)) {
		if (full_update) {
			ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, DJ_DRV_START_BULK, 0, index, NULL, 1);
		} else {
			ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, DJ_UPDATE_UPPER_SECTION, 0, index, NULL, 1);
		}
	} else if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		dc = ((struct hdj_steel_context*)ubulk->device_context);
		if (atomic_read(&dc->device_mode) == DJ_STEEL_IN_BOOT_MODE) {
			ret = send_boot_loader_command(ubulk, DJ_STEEL_ENTER_BOOT_LOADER);
		} else {
			printk(KERN_WARNING"%s(): The device is not in boot mode.\n",__FUNCTION__);
			ret = -EINVAL;
		}
	} else {
		printk(KERN_WARNING"%s(): Invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	return ret;
}

int firmware_stop_bulk(struct usb_hdjbulk *ubulk, u16 index)
{
	int ret = 0;

	if ((ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) ||
		(ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE)||
		(ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE)) {
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, DJ_DRV_STOP_BULK, 0, index, NULL, 1);
	} else if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		ret = 0;
	} else {
		printk(KERN_WARNING"%s(): Invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	return ret;
}

/* This is for all wrapped firmware */
int check_crc(u8 * file_data, u32 size)
{
	int ret = 0;
	int i = 0;
	u32 sum=0;
	int file_size = size;
	u32* u32_file_data = (u32*)file_data;

	if ((file_size%sizeof(u32))!=0) {
		file_size = (file_size/4)+1;
	} else {
		file_size/=4;
	}

	/* Since the checksum is within the file, calculating the checksum
	 *  should always overflow exactly at 0 */
	for (i = 0 ; i < file_size ; i++) {
		sum += u32_file_data[i];
	}

	/* the sum isn't 0, the file is incorrect */
	if (sum != 0) {
		return -EINVAL;
	}

	return ret;
}

int parse_bulk_file(struct usb_hdjbulk *ubulk, 
		  u8* file_data, 
		  struct FIRMWARE_HEADER** firmware_header, 
		  u8 * wrapped)
{
	int ret = 0;
	
	/* assume not wrapped up first */
	*wrapped = 0;

	/* copy in the firmware wrapper */
	*firmware_header = (struct FIRMWARE_HEADER*) file_data;

	if (memcmp((*firmware_header)->id,TUSB_full,4) == 0) {
		*wrapped = 1;
	} else if (memcmp((*firmware_header)->id,TUSB_partial,4) == 0) {
		*wrapped = 1;
	} else if (memcmp((*firmware_header)->id,PSOCH_full,4) == 0) {
		*wrapped = 1;
	} else if (memcmp((*firmware_header)->id,PSCU_full,4) == 0) {
		*wrapped = 1;
	}

	return ret;
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
static void bulk_out_callback(struct urb *urb/*, struct pt_regs *regs*/)
#else
static void bulk_out_callback(struct urb *urb, struct pt_regs *regs)
#endif
{
	struct usb_hdjbulk *ubulk = urb->context;

	if (!ubulk) {
		printk(KERN_WARNING"%s() no context, can't call wake_up(), bailing!\n",__FUNCTION__);
    		return;
	}

	complete(&ubulk->bulk_out_completion);
}

/* Warning- this has not been tested, USE AT YOUR OWN RISK */
int firmware_send_steel_upgrade_bulk(struct usb_hdjbulk *ubulk,
				     u8* buffer,
				     u32 buffer_size,
				     u32 block_number)
{
	return 0;
}

int firmware_send_bulk(struct usb_hdjbulk *ubulk,
		     u8* buffer,
		     u32 buffer_size,
		     u8 force_send)
{
	DEFINE_WAIT(wait);
	int ret=0;
	long timeout = 0;

	/* Have to do a sanity check as we do a buffer copy */
	if (buffer_size>ubulk->bulk_out_size) {
		printk(KERN_WARNING"%s() buffer overflow (provided:%d, max:%d)\n",
			__FUNCTION__,buffer_size,ubulk->bulk_out_size);
		return -EINVAL;
	}

	if (force_send == 0 && 
		atomic_read(&ubulk->chip->locked_io) > 0) {
		printk(KERN_WARNING"%s() I/O is locked, force_send==0, bailing\n",__FUNCTION__);
		return -EPERM;
	}

	/* constrain to one request at a time */
	down(&ubulk->bulk_out_buffer_mutex);

	/*indicate that a bulk output request is in progress.*/
	atomic_inc(&ubulk->bulk_out_command_in_progress);

	/* Since we allocated our buffer with usb_buffer_alloc, do a copy- surely less of a penalty than using
 	 *  a kmalloc buffer which DMA setup for it, especially with our small buffer sizes */
	memcpy(ubulk->bulk_out_buffer,buffer,buffer_size);

	ubulk->bulk_out_urb->transfer_buffer_length = buffer_size;

	usb_fill_bulk_urb(ubulk->bulk_out_urb, 
				ubulk->chip->dev, 
				ubulk->bulk_out_pipe,
				ubulk->bulk_out_buffer, 
				buffer_size,
				bulk_out_callback, 
				ubulk);

	ret = hdjbulk_submit_urb(ubulk->chip, ubulk->bulk_out_urb, GFP_KERNEL);
	if (ret!=0) {
		if (ret != -ENODEV) {
			printk(KERN_ERR"%s() hdjbulk_submit_urb() error, ret:%d\n\n",__FUNCTION__,ret);
		}

		goto firmware_send_bulk_bail;
	}

	/*wait for the completion of the urb*/
	timeout = wait_for_completion_interruptible_timeout(&ubulk->bulk_out_completion, HZ);
	
	if (timeout <= 0) {
		printk(KERN_ERR"%s() timed out: %ld\n", __FUNCTION__,timeout);
		ret = -EIO;

		/*kill the urb since it timed out*/
		usb_kill_urb(ubulk->bulk_out_urb);
	}

	if (signal_pending(current)) {
		printk(KERN_WARNING"%s() signal pending\n",__FUNCTION__);
		/* we have been woken up by a signal- reflect this in the return code */
		ret = -ERESTARTSYS;
	}

	if (ubulk->bulk_out_urb->status == -EPIPE) {
		printk(KERN_ERR"%s() ubulk->bulk_out_urb->status == -EPIPE\n",__FUNCTION__);

		/*clear the pipe */
		usb_clear_halt(ubulk->chip->dev, ubulk->bulk_out_pipe);

		ret = -EPIPE;
	}

firmware_send_bulk_bail:
	atomic_dec(&ubulk->bulk_out_command_in_progress);
	/*release the lock contraining bulk operation */
	up(&ubulk->bulk_out_buffer_mutex);
	return ret;
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
static void ctrl_callback(struct urb *urb/*, struct pt_regs *regs*/)
#else
static void ctrl_callback(struct urb *urb, struct pt_regs *regs)
#endif
{
	struct snd_hdj_chip* chip = urb->context;

	if (!chip) {
		printk(KERN_WARNING"ctrl_callback() no context, can't call wake_up(), bailing!\n");
    		return;
	}

	complete(&chip->ctrl_completion);
}

int send_vendor_request(int chip_index, u32 bmRequestIn, u32 bmRequest, u16 value, 
			u16 index, u16 *TransferBuffer, u8 force_send)
{
	DEFINE_WAIT(wait);
	int ret=0;
	int pipe = 0;
	long timeout = 0;
	struct snd_hdj_chip* chip;

	chip = inc_chip_ref_count(chip_index);

	if (!chip) {
		printk(KERN_WARNING"%s() no context, can't call wake_up(), bailing!\n",__FUNCTION__);
		return -EINVAL;
	}

	if (force_send == 0 && 
		atomic_read(&chip->locked_io) > 0) {
		printk(KERN_WARNING"%s() I/O is locked due to an update in progress, bailing!\n",__FUNCTION__);
		dec_chip_ref_count(chip_index);
		return -EPERM;
	}

	/*indicate that a vendor request is in progress.*/
	atomic_inc(&chip->vendor_command_in_progress);
	
	/* constrain to one request at a time*/
	down(&chip->vendor_request_mutex);

	if (chip->ctl_req == NULL)  {
		printk(KERN_WARNING"%s() chip->ctl_req is NULL, bailing\n",__FUNCTION__);
		dec_chip_ref_count(chip_index);
		atomic_dec(&chip->vendor_command_in_progress);
		return -ENOMEM;
	}

	if (chip->ctrl_req_buffer == NULL) {
		printk(KERN_WARNING"%s() chip->ctrl_req_buffer is NULL, bailing\n",__FUNCTION__);
		dec_chip_ref_count(chip_index);
		atomic_dec(&chip->vendor_command_in_progress);
		return -ENOMEM;
	}

	memset(chip->ctl_req,0,sizeof(*(chip->ctl_req)));
	memset(chip->ctrl_req_buffer, 0, chip->ctrl_req_buffer_len);

	chip->ctl_req->bRequestType = bmRequestIn;
	chip->ctl_req->bRequest = bmRequest;
	chip->ctl_req->wValue = cpu_to_le16(value);
	chip->ctl_req->wIndex = cpu_to_le16(index);
	chip->ctl_req->wLength = cpu_to_le16p(&chip->ctrl_req_buffer_len);

	/* fill the control urb */
	if (bmRequestIn == REQT_WRITE) {
		pipe = chip->ctrl_out_pipe;
		if (TransferBuffer!=NULL) {
			memcpy(chip->ctrl_req_buffer,TransferBuffer,sizeof(u16));
		}
	} else if (bmRequestIn == REQT_READ) {
		pipe = chip->ctrl_in_pipe;
	} else {
		printk(KERN_WARNING"%s() bmRequestIn is invalid: 0x%X, bailing!\n", __FUNCTION__,bmRequestIn);
		dec_chip_ref_count(chip_index);
		atomic_dec(&chip->vendor_command_in_progress);
		return -EINVAL;
	}

	usb_fill_control_urb(chip->ctrl_urb, 
				chip->dev, 
				pipe,
				(unsigned char *)chip->ctl_req, 
				chip->ctrl_req_buffer, 
				chip->ctrl_req_buffer_len,
				ctrl_callback, 
				(void *)chip);

	chip->ctrl_urb->setup_dma = chip->ctl_req_dma;
	/* NOTE: transfer_dma setup above in call to usb_buffer_alloc() */
	chip->ctrl_urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;

	ret = hdjbulk_submit_urb(chip, chip->ctrl_urb, GFP_KERNEL);
	if (ret!=0) {
		printk(KERN_ERR"%s(): hdjbulk_submit_urb() failed, ret:%d\n\n",__FUNCTION__,ret);
		goto send_vendor_request_bail;
	}

	/* wait for the completion of the task */
	timeout = wait_for_completion_interruptible_timeout(&chip->ctrl_completion, HZ);
	if (timeout <= 0) {
		printk(KERN_ERR"%s() wait_for_completion_interruptible_timeout timed out: %ld\n",
				__FUNCTION__, timeout);
		/* we have been woken up by a signal- reflect this in the return code */
		ret = -ERESTARTSYS;

		/*kill the urb since it timedout*/
		usb_kill_urb(chip->ctrl_urb);
	} 

	if (signal_pending(current)) {
		printk(KERN_WARNING"%s() signal pending\n",__FUNCTION__);
		/* we have been woken up by a signal- reflect this in the return code */
		ret = -ERESTARTSYS;
	}

	if (chip->ctrl_urb->status == -EPIPE) {
		printk(KERN_ERR"%s() chip->ctrl_urb->status == -EPIPE\n",__FUNCTION__);

		/*clear the pipe*/
		usb_clear_halt(chip->dev, pipe);

		ret = -EPIPE;
	}

send_vendor_request_bail:

	/*save the return value*/
	if (bmRequestIn == REQT_READ && TransferBuffer != NULL) {
		/* this is the way our firmware sends it, so convert...I know, it's opposite */
		*(u16*)(chip->ctrl_req_buffer) = be16_to_cpu(*(u16*)(chip->ctrl_req_buffer));

		*TransferBuffer = *(u16*)(chip->ctrl_req_buffer);
	}

	/*release the lock */
	up(&chip->vendor_request_mutex);

	/*indicate that a vendor request is complete.*/
	atomic_dec(&chip->vendor_command_in_progress);

	dec_chip_ref_count(chip_index);

	return ret;
}

static void output_control_callback(struct urb *urb/*, struct pt_regs *regs*/)
{
        struct completion *com = urb->context;
 
        if (!com) {
		printk(KERN_WARNING"output_control_callbackk() no context, can't call wake_up(), bailing!\n");
        	return;
	}
 	
	complete(com);
}

/* Sends output report already prepared in output_control_buffer, caller must synchronize with
 *  output_control_mutex */
int usb_set_report(struct usb_hdjbulk *ubulk, u8 type, u8 id)
{
	int rc;
	
	memset(ubulk->output_control_ctl_req,0,sizeof(*(ubulk->output_control_ctl_req)));
	ubulk->output_control_ctl_req->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	ubulk->output_control_ctl_req->bRequest = USB_REQ_SET_REPORT;
	ubulk->output_control_ctl_req->wValue = cpu_to_le16((type << 8) + id);
	ubulk->output_control_ctl_req->wIndex = 
			cpu_to_le16(ubulk->control_interface->cur_altsetting->desc.bInterfaceNumber);
	ubulk->output_control_ctl_req->wLength = cpu_to_le16p(&ubulk->output_control_buffer_size);

	usb_fill_control_urb(ubulk->output_control_urb,
				interface_to_usbdev(ubulk->control_interface),
				usb_sndctrlpipe(interface_to_usbdev(ubulk->control_interface), 0),
				(unsigned char *)ubulk->output_control_ctl_req,
				ubulk->output_control_buffer,
				ubulk->output_control_buffer_size,
				output_control_callback,
				&ubulk->output_control_completion);
	ubulk->output_control_urb->setup_dma = ubulk->output_control_dma;
	ubulk->output_control_urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	if ((rc =  hdjbulk_submit_urb(ubulk->chip,ubulk->output_control_urb, GFP_KERNEL))!=0) {
		printk(KERN_WARNING"%s hdjbulk_submit_urb() failed, rc:%d\n",__FUNCTION__,rc);
	} else {
		wait_for_completion(&ubulk->output_control_completion);
	}
	/* some devices need this */
	msleep(10);
	return rc;
} 

long hdjbulk_ioctl(struct file *file,	
					 unsigned int ioctl_num,	
					 unsigned long ioctl_param,
					 u8 compat_mode)
{
	struct usb_hdjbulk *ubulk = NULL;
	int result = 0;
	int access=0;
	u16 value = 0;
	unsigned long cfromuser=0;
	unsigned long ctouser=0;
	struct snd_hdj_chip* chip;
	int chip_index;
	unsigned long context=0; 
	u32 value32 = 0;
	u8 value8 = 0;
	void *firmware_data=NULL;
	void *bulk_write=NULL;
	void *control_data_and_mask=NULL;
	s32 size;
	u32 __user *value32p_user;
	u16 __user *value16p_user;
	u8 __user *value8p_user;
	unsigned long __user *valueulp_user;
#ifdef CONFIG_COMPAT
	compat_long_t __user *valueclp_user;
	compat_long_t context_compat;
#endif
	int __user * valueip_user;
	struct hdj_steel_context* dc;

	chip_index = (int)(unsigned long)file->private_data;

	/*increment the chip reference count for safety*/
	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s() no context, bailing!\n",__FUNCTION__);
		return -EINVAL;
	}

	ubulk = bulk_from_chip(chip);
	if (ubulk==NULL) {
		printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
		dec_chip_ref_count(chip_index);
		return -EINVAL;
	}

	switch (ioctl_num) {
	case DJ_IOCTL_GET_VERSION:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_VERSION\n",
						__FUNCTION__);
		access = access_ok(VERIFY_WRITE,ioctl_param, sizeof(u32));
		if (access!=0) {
			value32p_user = (u32 __user *)ioctl_param;
			result = __put_user(driver_version,value32p_user);
			if (result!=0) {
				printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
			} else {
				ioctl_trace_printk(KERN_INFO"%s() returning version:%x\n",
							__FUNCTION__,driver_version);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
	break;
	case DJ_IOCTL_GET_JOG_WHEEL_LOCK_SETTING:

		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_JOG_WHEEL_LOCK_SETTING\n",
			__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_jogwheel_lock_status(ubulk, &value, 1, 0);
			if (result==0) {
				/* copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value,value16p_user);
				if (result!=0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_jogwheel_lock_status failed, rc:%d\n",
					__FUNCTION__,
					result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_JOG_WHEEL_LOCK_SETTING:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_JOG_WHEEL_LOCK_SETTING\n",__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(u16));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __get_user(value,value16p_user);
			if (result == 0) {
				result = set_jogwheel_lock_status(ubulk, value);
				if (result!=0) {
					printk(KERN_ERR"%s() set_jogwheel_lock_status failed, rc:%d\n",
						__FUNCTION__,result);
				}
			} else {
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
					__FUNCTION__,
					result);
			}
		} else{
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_JOG_WHEEL_SENSITIVITY:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_JOG_WHEEL_SENSITIVITY\n",__FUNCTION__);

		/*verify that the address isn't in kernel mode */
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_jogwheel_sensitivity(ubulk, &value, 1, 0);
			if (result == 0) {
				/*copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_jogwheel_sensitivity failed, rc:%d\n",
					__FUNCTION__,
					result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_JOG_WHEEL_SENSITIVITY:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_JOG_WHEEL_SENSITIVITY\n",__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(u16));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __get_user(value, value16p_user);
			if (result == 0) {
				result = set_jogwheel_sensitivity(ubulk, value);
				if (result!=0) {
					printk(KERN_ERR"%s() set_jogwheel_sensitivity failed, rc:%d\n",
						__FUNCTION__,
						result);
				}
			} else 	{
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
					__FUNCTION__,
					result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_MIDI_CHANNEL:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_MIDI_CHANNEL\n",
						__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __get_user(value, value16p_user);
			if (result == 0) {
				result = set_midi_channel(chip, (u16*)&value);
				if (result==0) {
					/* Since another channel could have been applied, copy back the
					 *  actual channel applied to user mode
					 */
					result = __put_user(value, value16p_user);
					if (result != 0) {
						printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
							__FUNCTION__,
							result);
					} 
				} else {
					printk(KERN_ERR"%s() set_midi_channel failed, rc:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_WARNING"%s() __get_user failed, result:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_MIDI_CHANNEL:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_MIDI_CHANNEL\n",
						__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_midi_channel(chip, &value);
			if (result==0) {
				/*copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_midi_channel failed, rc:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_PRODUCT_CODE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_PRODUCT_CODE\n",
						__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u32));
		if (access) {
			value32 = ubulk->chip->product_code;
			result = 0;

			/*copy the kernel mode buffer to usermode*/
			value32p_user = (u32 __user *)ioctl_param;
			result = __put_user(value32, value32p_user);
			if (result != 0) {
				printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
					__FUNCTION__,
					result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n", __FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_LOCK_IO:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_LOCK_IO\n",
					__FUNCTION__);
		lock_vendor_io(ubulk);
		break;
	case DJ_IOCTL_UNLOCK_IO:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_UNLOCK_IO\n",
					__FUNCTION__);
		unlock_vendor_io(ubulk);
		break;
	case DJ_IOCTL_GET_TALKOVER_ATT:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_TALKOVER_ATT\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_talkover_att(ubulk, &value, 1);

			if (result==0) {
				/*copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result != 0) {
					printk(KERN_WARNING"hdjbulk_ioctl() ioctl received(), __put_user failed, result:%d\n",
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_talkover_att failed, rc:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_TALKOVER_ATT:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_TALKOVER_ATT\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(u16));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __get_user(value, value16p_user);
			if (result == 0) {
				result = set_talkover_att(ubulk, value);
				if (result!=0) {
					printk(KERN_ERR"%s() set_talkover_att failed, rc:%d\n",
						__FUNCTION__,
						result);
				}
			} else 	{
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
							__FUNCTION__,
							result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_TALKOVER_ENABLE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_TALKOVER_ENABLE\n",
					__FUNCTION__);
		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(u8));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value8p_user = (u8 __user *)ioctl_param;
			result = __get_user(value8, value8p_user);
			if (result == 0) {
				result = set_talkover_enable(ubulk, value8);
				if (result!=0) {
					printk(KERN_ERR"%s() set_talkover_enable failed, rc:%d\n",
						__FUNCTION__,
						result);
				}
			} else 	{
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
							__FUNCTION__,
							result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
	break;
	case DJ_IOCTL_GET_TALKOVER_ENABLE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_TALKOVER_ENABLE\n",
						__FUNCTION__);
		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u8));
		if (access) {
			result = get_talkover_enable(ubulk, &value8);

			if (result==0) {
				/*copy the kernel mode buffer to usermode*/
				value8p_user = (u8 __user *)ioctl_param;
				result = __put_user(value8, value8p_user);
				if (result != 0) {
					printk(KERN_WARNING"hdjbulk_ioctl() ioctl received(), __put_user failed, result:%d\n",
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_talkover_att failed, rc:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
		}
	break;
	case DJ_IOCTL_GET_FIRMWARE_VERSION:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_FIRMWARE_VERSION\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_firmware_version(chip, &value, 1);
			if (result==0) {
				/*copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_firmware_version failed, rc:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_DJCONSOLE_CONFIG: 
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_DJCONSOLE_CONFIG\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access)
		{
			result = get_djconsole_device_config(ubulk->chip->index, &value, 0);
			if (result==0) {
				/*copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_djconsole_device_config failed, rc:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_DJCONSOLE_CONFIG: 
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_DJCONSOLE_CONFIG\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(u32));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value32p_user = (u32 __user *)ioctl_param;
			result = __get_user(value32, value32p_user);
			if (result == 0) {
				result = set_djconsole_device_config(chip_index, value32, 0);
				if (result!=0) {
					printk(KERN_ERR"%s() set_djconsole_device_config failed, rc:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
					__FUNCTION__,
				 	result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_AUDIO_CONFIG: 
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_AUDIO_CONFIG\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_audio_config(ubulk, &value, 1);
			if (result==0) {
				/*copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_audio_config failed, rc:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_AUDIO_CONFIG: 
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_AUDIO_CONFIG\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(u16));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __get_user(value, value16p_user);
			if (result == 0) {
				result = set_audio_config(ubulk, value);
				if (result!=0) {
					printk(KERN_ERR"%s() set_audio_config() failed, rc:%d\n",
						__FUNCTION__,
						result);
				}
			} else 	{
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
							__FUNCTION__,
							result);
			}
		}
		else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_DISABLE_MOUSE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_DISABLE_MOUSE\n",
					__FUNCTION__);

		result = set_mouse_state(chip, 0);
		if (result!=0) {
			printk(KERN_ERR"%s() set_mouse_state() (disable) failed, rc:%d\n",
						__FUNCTION__,
						result);
		}
		break;
	case DJ_IOCTL_ENABLE_MOUSE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_ENABLE_MOUSE\n",
					__FUNCTION__);

		result = set_mouse_state(chip, 1);
		if (result!=0) {
			printk(KERN_ERR"%s() set_mouse_state() (enable) failed, rc:%d\n",
						__FUNCTION__,
						result);
		}
		break;
	case DJ_IOCTL_GET_MOUSE_STATE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_MOUSE_STATE\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_mouse_state(chip, &value);
			if (result==0) {
				/*copy the kernel mode buffer to usermode */
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_mouse_state() failed, rc:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_SAMPLE_RATE: 
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_SAMPLE_RATE\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_sample_rate(ubulk, &value, 0);
			if (result == 0) {
				/*copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_sample_rate() failed, rc:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_SAMPLE_RATE: /* based on test results this may be removed */
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_SAMPLE_RATE\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(u16));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __get_user(value, value16p_user);
			if (result == 0) {
				result = set_sample_rate(ubulk, value);
				if (result!=0) {
					printk(KERN_ERR"%s() set_sample_rate() failed, rc:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_CFADER_LOCK:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_CFADER_LOCK\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(u16));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __get_user(value, value16p_user);
			if (result == 0) {
				result = set_crossfader_lock(ubulk, value);
				if (result!=0) {
					printk(KERN_ERR"%s() set_crossfader_lock() failed, rc:%d",
						__FUNCTION__,result);
				}
			} else {
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_CFADER_LOCK:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_CFADER_LOCK\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_crossfader_lock(ubulk, &value, 0);
			if (result==0) {
				/*copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result!=0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_crossfader_lock() failed, rc:%d",
						__FUNCTION__,result);
			}

		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_CROSSFADER_STYLE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_CROSSFADER_STYLE\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __get_user(value, value16p_user);
			if (result == 0) {
				result = set_crossfader_style(ubulk, value);
				if (result!=0) {
					printk(KERN_ERR"%s() set_crossfader_style() failed, rc:%d",
						__FUNCTION__,result);
				}
			} else {
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
							__FUNCTION__,
							result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_CROSSFADER_STYLE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_CROSSFADER_STYLE\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_crossfader_style(ubulk, &value);
			if (result==0) {
				/* copy the kernel mode buffer to usermode */
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_crossfader_style() failed, rc:%d",
						__FUNCTION__,result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_DJBULK_UPGRADE_FIRMWARE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_DJBULK_UPGRADE_FIRMWARE\n",
					__FUNCTION__);
		
		/* Warning- this path has not been tested, USE AT YOUR OWN RISK */
		
		/* we forbid this for now- use the native updater */
		if (compat_mode==1) {
			printk(KERN_WARNING"%s() firmware upgrade supported only with native updater\n",
					__FUNCTION__);
			result = -EINVAL;	
			break;
		}
		
		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(struct FIRMWARE_FILE));
		if (access) {
			/*allocate the kernel mode buffer*/
			firmware_data = zero_alloc(sizeof(struct FIRMWARE_FILE),GFP_KERNEL);
			if (firmware_data!=NULL) {
				/*copy the usermode buffer to kernel mode*/
				cfromuser = copy_from_user(firmware_data,(void*)ioctl_param,sizeof(struct FIRMWARE_FILE));
				if (cfromuser == 0) {
					result = update_device_firmware(ubulk, ((struct FIRMWARE_FILE*)firmware_data)->file, ((struct FIRMWARE_FILE*)firmware_data)->size);
					if (result!=0) {
						printk(KERN_ERR"%s() update_device_firmware() failed, rc:%d",
							__FUNCTION__,result);
					}
				} else {
					printk(KERN_WARNING"%s() ioctl received(), copy_from_user failed, cfromuser:%lu\n",
							__FUNCTION__,
							cfromuser);
					result = -EFAULT;
				}
				
				/*free the kernel mode buffer*/
				kfree(firmware_data);
			} else {
				printk(KERN_WARNING"%s() ioctl zero_alloc failed\n",__FUNCTION__);
				result = -ENOMEM;
			}
		} else {
			printk(KERN_WARNING"hdjbulk_ioctl() ioctl access_ok failed\n");
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_MODE_SHIFT_STATE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_MODE_SHIFT_STATE\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(u8));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value8p_user = (u8 __user *)ioctl_param;
			result = __get_user(value8, value8p_user);
			if (result == 0) {
				result = set_mode_shift_state(ubulk, value8);
				if (result!=0) {
					printk(KERN_ERR"%s() set_mode_shift_state() failed, rc:%d",
						__FUNCTION__,result);
				}
			} else {
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
						__FUNCTION__,
						result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_MODE_SHIFT_STATE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_MODE_SHIFT_STATE\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode */
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u8));
		if (access) {
			result = get_mode_shift_state(ubulk, (u8*)&value8);
			if (result==0) {
				/*copy the kernel mode buffer to usermode*/
				value8p_user = (u8 __user *)ioctl_param;
				result = __put_user(value8, value8p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_ERR"%s() get_mode_shift_state() failed, rc:%d",
						__FUNCTION__,result);
			}
		}
		else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_SET_FX_STATE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_FX_STATE\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,sizeof(u16));
		if (access) {
			/*copy the usermode buffer to kernel mode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __get_user(value, value16p_user);
			if (result == 0) {
				result = set_fx_state(ubulk, value);
				if (result!=0) {
					printk(KERN_ERR"%s() set_fx_state() failed, rc:%d",
						__FUNCTION__,result);
				}
			} else {
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
						__FUNCTION__,
						result);
			}
		} else  {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_FX_STATE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_FX_STATE\n",
					__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			result = get_fx_state(ubulk, &value);
			if (result==0) {
				/*copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				result = __put_user(value, value16p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				} 
			} else {
				printk(KERN_ERR"%s() get_fx_stat() failed, rc:%d",
						__FUNCTION__,result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;

	case DJ_IOCTL_DJBULK_GOTO_BOOT_MODE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_DJBULK_GOTO_BOOT_MODE\n",
					__FUNCTION__);

		/* this is only for the steel */
		if (ubulk->chip->product_code!=DJCONTROLSTEEL_PRODUCT_CODE) {
			printk(KERN_WARNING"%s() invalid product:%d\n",
					__FUNCTION__,ubulk->chip->product_code);
			result = -EINVAL;
			break;
		}
		dc = ((struct hdj_steel_context*)ubulk->device_context);
		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			if (atomic_read(&dc->device_mode) == DJ_STEEL_IN_NORMAL_MODE) {
				result = reboot_djcontrolsteel_to_boot_mode(ubulk);
				if (result!=0) {
					printk(KERN_ERR"%s() reboot_djcontrolsteel_to_boot_mode() failed, rc:%d",
							__FUNCTION__,result);
				}
				/* if successful, then device will depart and arrive again, but in boot mode. But
				 *  for now, we are in an "unknown" mode.
				 */
				value = DJ_STEEL_IN_UNKNOWN_MODE;	
			} else if (atomic_read(&dc->device_mode) ==DJ_STEEL_IN_BOOT_MODE) {
				result = 0;
				value = DJ_STEEL_IN_BOOT_MODE;
			} else {
				result = 0;
				value = DJ_STEEL_IN_UNKNOWN_MODE;
			}

			/*copy the kernel mode buffer to usermode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __put_user(value, value16p_user);
			if (result != 0) {
				printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
					__FUNCTION__,
					result);
			} else {
				/* Even if we failed, return success, so the usermode app will examine
				 * the buffer*/
				result = 0;
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_DJBULK_EXIT_BOOT_MODE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_DJBULK_EXIT_BOOT_MODE\n",
					__FUNCTION__);

		/* this is only for the steel */
		if (ubulk->chip->product_code!=DJCONTROLSTEEL_PRODUCT_CODE) {
			printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
			result = -EINVAL;
			break;
		}
		dc = ((struct hdj_steel_context*)ubulk->device_context);
		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			if (atomic_read(&dc->device_mode) == DJ_STEEL_IN_BOOT_MODE)
			{
				result = reboot_djcontrolsteel_to_normal_mode(ubulk);
				if (result!=0) {
					printk(KERN_ERR"%s() reboot_djcontrolsteel_to_normal_mode() failed, rc:%d",
							__FUNCTION__,result);
				}
				value = DJ_STEEL_IN_UNKNOWN_MODE;
			} else if (atomic_read(&dc->device_mode) == DJ_STEEL_IN_NORMAL_MODE) {
				result = 0;
				value = DJ_STEEL_IN_NORMAL_MODE;
			} else {
				result = 0;
				value = DJ_STEEL_IN_UNKNOWN_MODE;
			}

			/*copy the kernel mode buffer to usermode*/
			value16p_user = (u16 __user *)ioctl_param;
			result = __put_user(value, value16p_user);
			if (result != 0) {
				printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
					__FUNCTION__,
					result);
			} else {
				/* Even if we failed, return success, so the usermode app will examine
				 * the buffer*/
				result = 0;
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_LOCATION_ID:
#ifdef CONFIG_COMPAT
	case DJ_IOCTL_GET_LOCATION_ID32:
#endif
		if (compat_mode==0) {
			ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_LOCATION_ID\n",
					__FUNCTION__);
		} else {
			ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_LOCATION_ID32\n",
					__FUNCTION__);
		}

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,LOCATION_ID_LEN);
		if (access) {
			/*copy the kernel mode buffer to usermode*/
			ctouser = copy_to_user((void*)ioctl_param,(void*)&chip->usb_device_path[0],LOCATION_ID_LEN);
			if (ctouser == 0) {
				result = 0;
			} else {
				printk(KERN_WARNING"%s() ioctl received(), copy_to_user failed, ctouser:%lu\n",
					__FUNCTION__,
					ctouser);
				result = -EFAULT;
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_DJBULK_SEND_BULK_WRITE:
#ifdef CONFIG_COMPAT
	case DJ_IOCTL_DJBULK_SEND_BULK_WRITE32:
#endif
		if (compat_mode==0) {
			ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_DJBULK_SEND_BULK_WRITE\n",
					__FUNCTION__);
		} else {
			ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_DJBULK_SEND_BULK_WRITE32\n",
					__FUNCTION__);
		}

		/* this is only for the steel */
		if (ubulk->chip->product_code!=DJCONTROLSTEEL_PRODUCT_CODE) {
			printk(KERN_WARNING"%s() invalid product:%d\n",
					__FUNCTION__,ubulk->chip->product_code);
			result = -EINVAL;
			break;
		}

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_READ,ioctl_param,DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE);
		if (access) {
			/*allocate the kernel mode buffer*/
			bulk_write = zero_alloc(DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE,GFP_KERNEL);
			if (bulk_write!=NULL) {
				/*copy the usermode buffer to kernel mode*/
				cfromuser = copy_from_user(bulk_write,
								(void*)ioctl_param,
								DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE);
				if (cfromuser == 0) {
					/* Only support LED change requests and force report
					 * requests through this IOCTL */
					if (((u8*)bulk_write)[0] == DJ_STEEL_STANDARD_SET_LED_REPORT ||
						((u8*)bulk_write)[0] == DJ_STEEL_FORCE_REPORT_IN) {
						result = send_bulk_write(ubulk, 
								bulk_write, 
								DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE,
								0 /* force_send */);
						if (result !=0) {
							printk(KERN_ERR"%s() send_bulk_write() failed, rc:%d",
									__FUNCTION__,result);
						}
					} else {
						printk(KERN_WARNING"%s() ioctl received(), IOCTL_DJBULK_SEND_BULK_WRITE\
							, invalid command: %d\n",
							__FUNCTION__,
							((u8*)bulk_write)[0]);
						result = -EINVAL;
					}
				} else {
					printk(KERN_WARNING"%s() ioctl received(), copy_from_user failed, \
							cfromuser:%lu\n",
							__FUNCTION__,
							cfromuser);
					result = -EFAULT;
				}
				
				/*free the kernel mode buffer*/
				kfree(bulk_write);
			} else {
				printk(KERN_WARNING"%s() ioctl zero_alloc failed\n",__FUNCTION__);
				result = -ENOMEM;
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_IS_DEVICE_ALIVE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_IS_DEVICE_ALIVE\n",
					__FUNCTION__);
		/* check if device is still present on bus */
		result = is_device_present(chip);
		if (result!=0) {
			printk(KERN_ERR"%s() is_device_present() failed, rc:%d\n",
				__FUNCTION__,result);
		}
		break;
	case DJ_IOCTL_REGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS:
#ifdef CONFIG_COMPAT
	case DJ_IOCTL_REGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS32:
#endif
		if (compat_mode==0) {
			ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_REGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS\n",
					__FUNCTION__);
			access = access_ok(VERIFY_READ,ioctl_param,sizeof(unsigned long));
		} else {
			ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_REGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS32\n",
					__FUNCTION__);
#ifndef CONFIG_COMPAT
			/* this should be unreachable */
			printk(KERN_WARNING"%s CONFIG_COMPAT not defined, yet compat_mode!=0\n",
						__FUNCTION__);
			result = -EINVAL;
			break;
#else
			access = access_ok(VERIFY_READ,ioctl_param,sizeof(compat_long_t));
#endif
		}
		if (access) {
			if (compat_mode==0) {
				valueulp_user = (unsigned long __user *)ioctl_param;
				result = __get_user(context, valueulp_user);
			} else {
#ifdef CONFIG_COMPAT
				valueclp_user = (compat_long_t __user *)ioctl_param;
				result = __get_user(context_compat, valueclp_user);
				if (result==0) {
					context = (unsigned long)context_compat;
				}
#else
				/* this should be unreachable */
				printk(KERN_WARNING"%s CONFIG_COMPAT not defined, yet compat_mode!=0\n",
							__FUNCTION__);
				result = -EINVAL;
				break;
#endif
			}
			if (result == 0) {
				result = register_for_netlink(chip, file, (void*)context,compat_mode);
				if (result!=0) {
					printk(KERN_ERR"%s() register_for_netlink() failed, rc:%d",
							__FUNCTION__,result);
				}
			} else {
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
							__FUNCTION__,
							result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
		break;
	case DJ_IOCTL_UNREGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  IOCTL_UNREGISTER_FOR_NETLINK\n",
					__FUNCTION__);
		result = unregister_for_netlink(chip, file);
		if (result!=0) {
			printk(KERN_ERR"%s() unregister_for_netlink() failed, rc:%d",
					__FUNCTION__,result);
		}
		break;
	case DJ_IOCTL_GET_CONTROL_DATA_INPUT_PACKET_SIZE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_CONTROL_DATA_INPUT_PACKET_SIZE\n",
					__FUNCTION__);
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u32));
		if (access) {
			result = get_input_control_data_len(chip,&value32);
			if (result==0) {
				/*copy the kernel mode buffer to usermode*/
				value32p_user = (u32 __user *)ioctl_param;
				result = __put_user(value32, value32p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,result);
				}
			} else {
				printk(KERN_WARNING"%s() get_input_control_data_len() failed, rc:%d\n",
						__FUNCTION__, result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
	break;
	case DJ_IOCTL_GET_CONTROL_DATA_OUTPUT_PACKET_SIZE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_CONTROL_DATA_OUTPUT_PACKET_SIZE\n",
					__FUNCTION__);
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u32));
		if (access) {
			result = get_output_control_data_len(chip,&value32);
			if (result==0) {
				value32p_user = (u32 __user *)ioctl_param;
				result = __put_user(value32, value32p_user);
				if (result != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						result);
				}
			} else {
				printk(KERN_WARNING"%s() get_output_control_data_len() failed, rc:%d\n",
						__FUNCTION__, result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
	break;
	case DJ_IOCTL_SET_OUTPUT_CONTROL_DATA:
#ifdef CONFIG_COMPAT
	case DJ_IOCTL_SET_OUTPUT_CONTROL_DATA32:
#endif
		if (compat_mode==0) {
			ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_OUTPUT_CONTROL_DATA\n",
					__FUNCTION__);
		} else {
			ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_SET_OUTPUT_CONTROL_DATA32\n",
					__FUNCTION__);
		}
		result = get_output_control_data_len(chip,&value32);
		if (result!=0) {
			printk(KERN_WARNING"%s() get_output_control_data_len() failed, rc:%d\n",
					__FUNCTION__,result);
			break;	
		}
		/* We receive the control data and a bit mask, so we expect twice the control data length, not
		 *   counting the report ID */
		size = value32*2;
		if (size <= 0) {
			printk(KERN_WARNING"%s calculated size invalid:%d\n",__FUNCTION__,size);
			result = -EINVAL;
			break;	
		}
		access = access_ok(VERIFY_READ,ioctl_param,size);
		if (access) {
			control_data_and_mask = kmalloc(size,GFP_KERNEL);
			if (control_data_and_mask!=NULL) {
				cfromuser = copy_from_user(control_data_and_mask,
							(void*)ioctl_param,
							size);
				if (cfromuser == 0) {
					result = send_control_output_report(chip,
														control_data_and_mask,
														size);
					if (result!=0) {
						printk(KERN_WARNING"%s() send_control_output_report failed(), rc:%d\n",
							__FUNCTION__,result);
					}
				} else {
					printk(KERN_WARNING"%s() ioctl received(), copy_from_user failed, \
							cfromuser:%lu\n",__FUNCTION__,cfromuser);
					result = -EFAULT;
				}
				kfree(control_data_and_mask);
			} else {
				printk(KERN_WARNING"%s() kmalloc failed\n",__FUNCTION__);
				result = -ENOMEM;
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
	break;
	case DJ_IOCTL_GET_OUTPUT_CONTROL_DATA:
#ifdef CONFIG_COMPAT
	case DJ_IOCTL_GET_OUTPUT_CONTROL_DATA32:
#endif
		if (compat_mode==0) {
			ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_OUTPUT_CONTROL_DATA\n",
						__FUNCTION__);
		} else {
			ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_OUTPUT_CONTROL_DATA32\n",
						__FUNCTION__);
		}
		result = get_output_control_data_len(chip,&value32);
		if (result!=0) {
			printk(KERN_WARNING"%s() get_output_control_data_len() failed, rc:%d\n",
					__FUNCTION__,result);
			break;	
		}
		access = access_ok(VERIFY_WRITE,ioctl_param,value32);
		if (access) {
			result = get_control_output_report(chip,(u8 __user *)ioctl_param,value32);
			if (result != 0) {
				printk(KERN_WARNING"%s() get_control_output_report() failed, rc:%d\n",
					__FUNCTION__,result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
	break;
	case DJ_IOCTL_ACQUIRE_NETLINK_UNIT:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_ACQUIRE_NETLINK_UNIT\n",
					__FUNCTION__);
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(int));
		if (access) {
			valueip_user = (int __user *)ioctl_param;
			result = __put_user(netlink_unit, valueip_user);
			if (result != 0) {
				printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
					__FUNCTION__,
					result);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
	break;
	case DJ_IOCTL_GET_DEVICE_CAPS:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_DEVICE_CAPS\n",
					__FUNCTION__);
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(struct snd_hdj_caps));
		if (access) {
			/*copy the kernel mode buffer to usermode*/
			ctouser = copy_to_user((void*)ioctl_param,(void*)&chip->caps,sizeof(struct snd_hdj_caps));
			if (ctouser == 0) {
				result = 0;
			} else {
				printk(KERN_WARNING"%s() ioctl received(), copy_to_user failed, ctouser:%lu\n",
					__FUNCTION__,
					ctouser);
				result = -EFAULT;
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			result = -EFAULT;
		}
	break;
	default:
		printk(KERN_INFO"%s(): INVALID ioctl received: ioctl_num:0x%x, ioctl_param:0x%lx\n",
			__FUNCTION__,
			ioctl_num,
			ioctl_param);
		result = -ENOTTY;
		break;
	}

	dec_chip_ref_count(chip->index);
	return result;
}

/* ALERT: read_list_lock needs to be acquired before calling */
static void free_open_list(struct list_head *open_list, struct file * file)
{
	struct list_head *p_read_item;
	struct list_head *next_read_item;
	struct hdj_read_list * read_list_item;

	struct list_head *p_open_item;
	struct list_head *next_open_item;
	struct hdj_open_list * open_list_item;

	struct list_head *p_buffer_item;
	struct list_head *next_buffer_item;
	struct hdj_read_buffers* read_buffer_item = NULL;

	/* free all elements of the list */
	if (!list_empty(open_list)) {
		list_for_each_safe(p_open_item,next_open_item,open_list) {
			open_list_item = list_entry(p_open_item, struct hdj_open_list, list);

			/* free the list for the current file */
			if (open_list_item->file == file) {
				if (!list_empty(&open_list_item->read_list)) {
					list_for_each_safe(p_read_item, next_read_item, &open_list_item->read_list) {
						read_list_item = list_entry(p_read_item, struct hdj_read_list, list);
						
						if (!list_empty(&read_list_item->list_buffers)) 						{
							list_for_each_safe(p_buffer_item, next_buffer_item, &read_list_item->list_buffers) {
								read_buffer_item = list_entry(p_buffer_item, struct hdj_read_buffers, list);

								list_del(p_buffer_item);
								kfree(read_buffer_item);
							}
						}

						list_del(p_read_item);
						kfree(read_list_item);
					}
				}

				list_del(p_open_item);
				kfree(open_list_item);
			}
		}
	}
}

/* ALERT: read_list_lock needs to be acquired before calling */
static struct hdj_open_list * alloc_and_init_open_list_item(struct file *file) 
{
	struct hdj_open_list * open_list_item = NULL;
	open_list_item = zero_alloc(sizeof(struct hdj_open_list), GFP_ATOMIC);
	if (open_list_item != NULL) {
		open_list_item->access_count = 1;
		open_list_item->file = file;
		open_list_item->is_releasing = 0;
		INIT_LIST_HEAD(&open_list_item->read_list);
		/*printk(KERN_INFO"%s() adding element to read_list\n",__FUNCTION__);*/
	}
	return open_list_item;
}

/* ALERT: read_list_lock needs to be acquired before calling */
static int alloc_and_init_read_list_item(struct hdj_read_list** read_list_item, 
										long current_thread_id)
{
	struct hdj_read_buffers* read_buffer_item = NULL;
	int i = 0, rc = 0;
	struct list_head *p_buffer_item;
	struct list_head *next_buffer_item;

	if (read_list_item == NULL) {
		printk(KERN_WARNING"%s() NULL read_list_item\n",__FUNCTION__);
		return -EINVAL;
	}

	/* create a new element for this thread */
	*read_list_item = zero_alloc(sizeof(struct hdj_read_list), GFP_ATOMIC);
	if (*read_list_item == NULL) {
		printk(KERN_WARNING"%s() memory allocation failed.\n",__FUNCTION__);
		return -ENOMEM;
	}
	init_completion(&((*read_list_item)->read_completion));
	(*read_list_item)->thread_id = current_thread_id;
	INIT_LIST_HEAD(&((*read_list_item)->list_buffers));
	atomic_set(&((*read_list_item)->num_pending_waits),0);
	for (i = 0; i < HDJ_READ_BUFFERS_COUNT; i++) {
		read_buffer_item = zero_alloc(sizeof(struct hdj_read_buffers), GFP_ATOMIC);
		if (read_buffer_item != NULL) {
			read_buffer_item->is_ready = 0;
			list_add_tail(&read_buffer_item->list,&((*read_list_item)->list_buffers));
		} else {
			printk(KERN_WARNING"%s failed to allocate buffer queue element number:%d\n",
					__FUNCTION__,i);
			rc = -ENOMEM;
			break;
		}
	}

	/* cleanup */
	if (rc!=0 && *read_list_item!=NULL) {
		/* free any queued buffers, then free element */
		if (!list_empty(&(*read_list_item)->list_buffers)) {
			list_for_each_safe(p_buffer_item, next_buffer_item, &(*read_list_item)->list_buffers) {
				read_buffer_item = list_entry(p_buffer_item, struct hdj_read_buffers, list);
				list_del(p_buffer_item);
				kfree(read_buffer_item);
			}
		}
		kfree(*read_list_item);
	}
	return rc;
}

/* ALERT: read_list_lock needs to be acquired before calling */
static struct hdj_read_list *get_read_list_element(struct list_head *read_list, long thread_id)
{
	struct list_head *p_read_item;
	struct list_head *next_read_item;
	struct hdj_read_list * read_list_item;

	if (!list_empty(read_list)) {
		list_for_each_safe(p_read_item, next_read_item, read_list) {
			read_list_item = list_entry(p_read_item, struct hdj_read_list, list);
			if (read_list_item->thread_id == thread_id) {
				return read_list_item;
			}
		}
	}
	return NULL;
}

/* ALERT: read_list_lock needs to be acquired before calling */
static struct hdj_open_list *get_open_list_element(struct list_head *open_list, 
													struct file * file)
{
	struct list_head *p_open_item;
	struct list_head *next_open_item;
	struct hdj_open_list * open_list_item;

	if (!list_empty(open_list))
	{
		list_for_each_safe(p_open_item,next_open_item,open_list) {
			open_list_item = list_entry(p_open_item, struct hdj_open_list, list);
			if (open_list_item->file == file) {
				return open_list_item;
			}
		}
	}
	return NULL;
}

int hdjbulk_open(struct inode *inode, struct file *file)
{
	int chip_index = - 1;
	struct snd_hdj_chip *chip = NULL;
	struct usb_hdjbulk *ubulk = NULL;
	struct usb_interface *interface;
	int subminor;
	unsigned long flags;
	struct hdj_open_list * open_list_item;
	int retval = 0;

	subminor = iminor(inode);

	/*printk(KERN_INFO"%s() tgid: %d pid: %d\n", __FUNCTION__, current->tgid, current->pid);*/

	interface = usb_find_interface(&hdj_driver, subminor);
	if (!interface) {
		printk(KERN_WARNING"%s() - error, can't find device for minor %d\n",
		     __FUNCTION__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	chip_index = (int)(unsigned long)usb_get_intfdata(interface);

	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s() first inc_chip_ref_count failed\n",__FUNCTION__);
		return -ENODEV;
	}

	/* increment the chip again to make sure it stays alive until release */
	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s() second inc_chip_ref_count failed\n",__FUNCTION__);
		dec_chip_ref_count(chip_index);
		return -ENODEV;
	}

	ubulk = bulk_from_chip(chip);
	if (ubulk==NULL) {
		printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
		retval = -ENODEV;
		goto exit;
	}

	if (can_send_urbs(chip)!=0) {
		printk(KERN_INFO"%s() I/O forbidden, bailing\n",__FUNCTION__);
		retval = -ENODEV;
		goto exit;
	}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
	if (1==atomic_inc_return(&ubulk->open_count)) {
		retval = usb_autopm_get_interface(interface);
			if (retval) {
				atomic_dec(&ubulk->open_count);
				goto exit;
			}
	} /* else { uncomment this block if you want exclusive open
		retval = -EBUSY;
		atomic_dec(&ubulk->open_count);
		goto exit;
	} */
#endif

	/* save our object's index in the file's private structure */
	file->private_data = (void*)(unsigned long)ubulk->chip->index;

	if (is_continuous_reader_supported(ubulk->chip)==1) {
		spin_lock_irqsave(&ubulk->read_list_lock, flags);
		if (list_empty(&ubulk->open_list)) {
			buffer_queue_depth=0;
		}
		if (get_open_list_element(&ubulk->open_list, file) == NULL) {
			open_list_item = alloc_and_init_open_list_item(file);
			if (open_list_item!=NULL) {
				list_add_tail(&open_list_item->list,&ubulk->open_list);
			} else {
				printk(KERN_WARNING"%s init_open_queue_item() failed, bailing\n", __FUNCTION__);
				retval = -ENOMEM;
			}
		}
		spin_unlock_irqrestore(&ubulk->read_list_lock, flags);
	} 

exit:
	if (chip) {
		dec_chip_ref_count(chip_index);

		/* in case of error, the caller will NOT be calling close, so we have to release our
		 *  extra reference count */
		if (retval!=0) {
			dec_chip_ref_count(chip_index);
		}
	}
	return retval;
}

/* ALERT: read_list_lock needs to be acquired before calling */
void signal_all_waiting_readers(struct list_head *open_list)
{
	struct list_head *p_read_item;
	struct list_head *next_read_item;
	struct hdj_read_list * read_list_item;

	struct list_head *p_open_item;
	struct list_head *next_open_item;
	struct hdj_open_list * open_list_item;

	/* free all elements of the list */
	if (!list_empty(open_list)) {
		list_for_each_safe(p_open_item,next_open_item,open_list) {
			open_list_item = list_entry(p_open_item, struct hdj_open_list, list);
			/* This will tell signalled readers to bail with an error */
			open_list_item->is_releasing = 1;

			if (!list_empty(&open_list_item->read_list)) {
				list_for_each_safe(p_read_item, next_read_item, &open_list_item->read_list) {
					read_list_item = list_entry(p_read_item, struct hdj_read_list, list);
					complete(&read_list_item->read_completion);
				}
			}
		}
	}
}

/* ALERT: read_list_lock needs to be acquired before calling */
static void fill_queued_buffers(struct list_head *open_list, void * buffer, unsigned long size)
{
	struct list_head *p_read_item;
	struct list_head *next_read_item;
	struct hdj_read_list * read_list_item;

	struct list_head *p_open_item;
	struct list_head *next_open_item;
	struct hdj_open_list * open_list_item;

	struct list_head *p_buffer_item;
	struct list_head *next_buffer_item;
	struct hdj_read_buffers* read_buffer_item = NULL;
	int buffer_depth=0;
	int size_to_copy;

	if (size > HDJ_POLL_INPUT_BUFFER_SIZE) {
		printk(KERN_DEBUG"%s buffer too large (%lu), max:%lu, will truncate\n",
				__FUNCTION__,size,HDJ_POLL_INPUT_BUFFER_SIZE);
		size_to_copy = HDJ_POLL_INPUT_BUFFER_SIZE;
	} else {
		size_to_copy = size;	
	}

	/* copy data to first available buffer, or if none are free, reuse the oldest one */
	if (!list_empty(open_list)) {
		list_for_each_safe(p_open_item,next_open_item,open_list) {
			open_list_item = list_entry(p_open_item, struct hdj_open_list, list);

			if (!list_empty(&open_list_item->read_list)) {
				list_for_each_safe(p_read_item, next_read_item, &open_list_item->read_list) {
					read_list_item = list_entry(p_read_item, struct hdj_read_list, list);
					
					if (!list_empty(&read_list_item->list_buffers)) {
						buffer_depth=0;
						list_for_each_safe(p_buffer_item, next_buffer_item, 
											&read_list_item->list_buffers) {
							read_buffer_item = list_entry(p_buffer_item, 
													struct hdj_read_buffers, list);
							if (read_buffer_item->is_ready == 0) {
								break;
							} else {
								buffer_depth++;
								read_buffer_item = NULL;
							}
						}

						if (buffer_depth>buffer_queue_depth) {
							buffer_queue_depth = buffer_depth;
							/*printk(KERN_INFO"%s() new max depth:%d\n",
								__FUNCTION__,buffer_queue_depth);*/
						}

						/* if no elements are free, reuse the oldest one and move it to the end of the list*/
						if (read_buffer_item == NULL) {
							/*printk(KERN_DEBUG"%s(), reusing element\n",__FUNCTION__);*/
							read_buffer_item = list_entry(read_list_item->list_buffers.next, struct hdj_read_buffers, list);
							list_del(&read_buffer_item->list);
							list_add_tail(&read_buffer_item->list,&read_list_item->list_buffers);
						}
						
						/* copy the buffer and pad the rest with zeros */
						memcpy(read_buffer_item->buffer, buffer, size_to_copy);
						memset(&read_buffer_item->buffer[size_to_copy], 0, 
								sizeof(read_buffer_item->buffer) - size_to_copy);
						read_buffer_item->is_ready = 1;

						/*
						 * We are under lock, so we don't worry about barriers 
						 *  Check if someone is waiting for this data, and if so,
						 *   wake them up.  
						 */
						if (atomic_cmpxchg(&read_list_item->num_pending_waits,
											1,0)==1) {
							/* We copied the data, wake up the client- if multiple clients
							 *  are waiting only one of them will be woken up */
							complete(&read_list_item->read_completion);
						}
					}
				}
			}
		}
	}
}


ssize_t hdjbulk_read(struct file * file, char __user *buf, size_t len, loff_t *ppos)
{
	int ret = -EINVAL;
	int chip_index;
	struct usb_hdjbulk *ubulk=NULL;
	struct snd_hdj_chip* chip=NULL;
	struct list_head *p_buffer_item;
	struct list_head *next_buffer_item;
	struct hdj_open_list* open_list_item = NULL;
	struct hdj_read_list* read_list_item = NULL;
	struct hdj_read_buffers* read_buffer_item = NULL;
	unsigned long flags;
	long is_empty = 1;
	long current_thread_id = 0;
	struct completion *read_completion = NULL;
	unsigned long ctouser = 0;

	chip_index = (int)(unsigned long)file->private_data;

	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s() no context, bailing!\n",__FUNCTION__);
		ret = -ENODEV;
		return ret;
	}

	ubulk = bulk_from_chip(chip);
	if (ubulk==NULL) {
		printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
		ret = -ENODEV;
		goto hdjbulk_read_bail;
	}
		

	if (can_send_urbs(chip)!=0) {
		printk(KERN_INFO"%s() I/O forbidden, bailing\n",__FUNCTION__);
		ret = -ENODEV;
		goto hdjbulk_read_bail;
	}

	/* The DJ Control Steel will return bulk control data through read.  Other devices will return
	 *  HID control data through read.  O_NONBLOCK is supported, but if not specified, and if no 
	 *  data is available, the caller will be blocked.  However, in the event of USB disconnect, 
	 *  the caller is unblocked */
	if (is_continuous_reader_supported(ubulk->chip)==1) {
		/* check if the size is valid */
		if (len < ubulk->continuous_reader_packet_size) {
			ret = -EINVAL;
			printk(KERN_WARNING"%s() Invalid Parameters: len: %zd is smaller then \
					ubulk->continuous_reader_packet_size: %d\n",
					__FUNCTION__,len, ubulk->continuous_reader_packet_size);
			goto hdjbulk_read_bail;
		}

		spin_lock_irqsave(&ubulk->read_list_lock, flags);

		/* get the open queue list item matching the file */
		open_list_item = get_open_list_element(&ubulk->open_list, file);
		if (open_list_item == NULL) {
			ret = -EINVAL;
			printk(KERN_WARNING"%s() no elements matching file:%p are found.\n",__FUNCTION__,file);
			spin_unlock_irqrestore(&ubulk->read_list_lock, 
						flags);
			goto hdjbulk_read_bail;
		}

		/* increment the count */
		++open_list_item->access_count;

		/* if release was called, bail */
		if (open_list_item->is_releasing) {
			ret = -ENODEV;
			/*printk(KERN_WARNING"%s() error, release was called.\n",__FUNCTION__);*/
			goto hdjbulk_read_in_progress_bail;
		}

		/* get the thread id */
		current_thread_id = current->pid;

		/* get the read queue list element for this thread */
		read_list_item = get_read_list_element(&open_list_item->read_list, current_thread_id);
		if (read_list_item == NULL) {
			/* create a new element for this thread */
			ret = alloc_and_init_read_list_item(&read_list_item, current_thread_id);
			if (ret != 0) {
				printk(KERN_WARNING"%s() alloc_and_init_read_list_item failed.\n",__FUNCTION__);
				goto hdjbulk_read_in_progress_bail;
			}

			/* we will have to wait for data to arrive, reference the completion */
			read_completion = &read_list_item->read_completion;

			list_add_tail(&read_list_item->list,&open_list_item->read_list);
		} else {
			/* verify if a buffer is ready */
			if (list_empty(&read_list_item->list_buffers)) {
				ret = -ENOMEM;
				printk(KERN_WARNING"%s() buffer list is empty.\n",__FUNCTION__);
				goto hdjbulk_read_in_progress_bail;
			}
			is_empty = 1;
			list_for_each_safe(p_buffer_item, next_buffer_item, &read_list_item->list_buffers) {
				read_buffer_item = list_entry(p_buffer_item, struct hdj_read_buffers, list);
				if (read_buffer_item->is_ready == 1) {
					is_empty = 0;
					break;
				}
			}
			if (is_empty == 1) {
				/* we will have to wait for data to arrive, reference the completion */
				read_completion = &read_list_item->read_completion;
			}
		}
		spin_unlock_irqrestore(&ubulk->read_list_lock, flags);

		if (read_completion != NULL) {
			/* there is no data presently, so we'll have to wait for some */
			
			/* but don't wait if the O_NON_BLOCK flag is set */
			if (file->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				/*printk(KERN_INFO"%s() the data isn't ready, but the O_NONBLOCK flag is set, so bail\n",
						__FUNCTION__);*/
				spin_lock_irqsave(&ubulk->read_list_lock, flags);
				goto hdjbulk_read_in_progress_bail;
			}

			/* Signal that we are about to wait- use the lock instead of barriers, as 
			 *  the completion routine will use this lock */
			spin_lock_irqsave(&ubulk->read_list_lock, flags);
			atomic_set(&read_list_item->num_pending_waits,1);
			spin_unlock_irqrestore(&ubulk->read_list_lock, flags);

			/* wait for the buffer */
			wait_for_completion_interruptible(read_completion);
			if (signal_pending(current)) {
				printk(KERN_INFO"%s() signal pending, will break\n",__FUNCTION__);
				/* we have been woken up by a signal- reflect this in the return code */
				ret = -ERESTARTSYS;
				spin_lock_irqsave(&ubulk->read_list_lock, flags);
				goto hdjbulk_read_in_progress_bail;
			}
		} else {
			/*printk(KERN_INFO"%s() NOT waiting for the notification\n",__FUNCTION__);*/
		}

		spin_lock_irqsave(&ubulk->read_list_lock, flags);

		/* if release was called, bail */
		if (open_list_item->is_releasing) {
			ret = -ENODEV;
			printk(KERN_WARNING"%s() error, release was called.\n",__FUNCTION__);
			goto hdjbulk_read_in_progress_bail;
		}
		
		if (list_empty(&read_list_item->list_buffers)) {
			ret = -ENOMEM;
			printk(KERN_WARNING"%s() buffer list is empty.\n",__FUNCTION__);
			goto hdjbulk_read_in_progress_bail;
		}
		/* get the buffer */
		read_buffer_item = list_entry(read_list_item->list_buffers.next, struct hdj_read_buffers, list);
		if (read_buffer_item->is_ready == 0) {
			ret = -EINVAL;
			printk(KERN_WARNING"%s() signal received, but the list is empty.\n",__FUNCTION__);
			goto hdjbulk_read_in_progress_bail;
		}

		/* remove the buffer from the list while it is in use */
		list_del(&read_buffer_item->list);
		spin_unlock_irqrestore(&ubulk->read_list_lock, flags);

		/* copy the buffer to usermode */
		ctouser = copy_to_user((void*)buf,read_buffer_item->buffer,ubulk->continuous_reader_packet_size);
		if (ctouser != 0) {
			printk(KERN_WARNING"%s() copy_to_user failed, ctouser:%lu\n",
				__FUNCTION__,
				ctouser);
		}

		/* set the return value to the amount of bytes read */
		ret = ubulk->continuous_reader_packet_size;

		/* mark the buffer as not ready, so that it can be reused */
		read_buffer_item->is_ready = 0;

		spin_lock_irqsave(&ubulk->read_list_lock, flags);
		/* place the buffer at the end of the list to be reused */
		list_add_tail(&read_buffer_item->list,&read_list_item->list_buffers);

hdjbulk_read_in_progress_bail:
		/* decrement the usage count and free the memory if it reaches 0 */
		--open_list_item->access_count;
		if (open_list_item->access_count == 0) {
			free_open_list(&ubulk->open_list, file);
		}
		spin_unlock_irqrestore(&ubulk->read_list_lock, flags);
	} else {
		/* This product has no continuous reader */
		ret = -ENXIO;
	}

hdjbulk_read_bail:
	dec_chip_ref_count(chip_index);
	return ret;
}

unsigned int hdjbulk_poll(struct file * file, struct poll_table_struct * wait)
{
	unsigned int ret = 0;
	int chip_index;
	struct usb_hdjbulk *ubulk=NULL;
	struct snd_hdj_chip* chip=NULL;
	struct list_head *p_buffer_item;
	struct list_head *next_buffer_item;
	struct hdj_open_list* open_list_item = NULL;
	struct hdj_read_list* read_list_item = NULL;
	struct hdj_read_buffers* read_buffer_item = NULL;
	unsigned long flags;
	long is_empty = 1;
	long current_thread_id = 0;

	chip_index = (int)(unsigned long)file->private_data;

	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s() no context, bailing!\n",__FUNCTION__);
		return -ENODEV;
	}

	ubulk = bulk_from_chip(chip);
	if (ubulk==NULL) {
		printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
		ret = -ENODEV;
		goto hdjbulk_poll_bail;
	}

	if (can_send_urbs(chip)!=0) {
		printk(KERN_INFO"%s() I/O forbidden, bailing\n",__FUNCTION__);
		ret = -ENODEV;
		goto hdjbulk_poll_bail;
	}

	poll_wait(file, &ubulk->read_poll_wait, wait);

	/* Only supported for those products which have continuous readers */
	if (is_continuous_reader_supported(ubulk->chip)==1) {
		spin_lock_irqsave(&ubulk->read_list_lock, flags);

		/* get the open queue list item matching the file */
		open_list_item = get_open_list_element(&ubulk->open_list, file);
		if (open_list_item == NULL) {
			ret = 0;
			printk(KERN_WARNING"%s() no elements matching file:%p are found.\n",__FUNCTION__,file);
			spin_unlock_irqrestore(&ubulk->read_list_lock, flags);
			goto hdjbulk_poll_bail;
		}

		/* increment the count */
		++open_list_item->access_count;

		/* if release was called, bail */
		if (open_list_item->is_releasing) {
			ret = -ENODEV;
			printk(KERN_WARNING"%s() error, release was called.\n",__FUNCTION__);
			goto hdjbulk_poll_in_progress_bail;
		}

		/* get the thread id */
		current_thread_id = current->pid;

		/* get the read queue list element for this thread */
		read_list_item = get_read_list_element(&open_list_item->read_list, current_thread_id);
		if (read_list_item == NULL) {
			/* create a new element for this thread */
			ret = alloc_and_init_read_list_item(&read_list_item, current_thread_id);
			if (ret != 0) {
				printk(KERN_WARNING"%s() alloc_and_init_read_list_item failed.\n",__FUNCTION__);
				goto hdjbulk_poll_in_progress_bail;
			}

			list_add_tail(&read_list_item->list,&open_list_item->read_list);

			/* a structure was allocated, but it isn't filled yet */
			ret = 0;
		} else {
			/* verify if a buffer is ready */
			if (list_empty(&read_list_item->list_buffers)) {
				/* no data available */
				ret = 0;
				goto hdjbulk_poll_in_progress_bail;
			}
			is_empty = 1;
			list_for_each_safe(p_buffer_item, next_buffer_item, &read_list_item->list_buffers) {
				read_buffer_item = list_entry(p_buffer_item, struct hdj_read_buffers, list);
				if (read_buffer_item->is_ready == 1) {
					is_empty = 0;
					break;
				}
			}
			if (is_empty == 1) {
				/* the structure is empty */
				ret = 0;	
			} else {
				/* the structure contains elements for read */
				ret = POLLIN | POLLRDNORM;
			}
		}
hdjbulk_poll_in_progress_bail:
		/* decrement the usage count and free the memory if it reaches 0 */
		--open_list_item->access_count;
		if (open_list_item->access_count == 0) {
			free_open_list(&ubulk->open_list, file);

			/* since the object was freed, read is no longer available */
			ret = 0;
		}
		spin_unlock_irqrestore(&ubulk->read_list_lock, flags);
	}

hdjbulk_poll_bail:
	dec_chip_ref_count(chip_index);
	return ret;
}

int hdjbulk_release(struct inode *inode, struct file *file)
{
	int ret = 0;
	int chip_index;
	struct usb_hdjbulk *ubulk=NULL;
	struct snd_hdj_chip* chip=NULL;
	struct hdj_open_list* open_list_item = NULL;
	unsigned long flags;

	/*printk(KERN_INFO"%s(): tgid: %d pid: %d\n", __FUNCTION__, current->tgid, current->pid);*/

	chip_index = (int)(unsigned long)file->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s() no context, bailing!\n",__FUNCTION__);
		return -ENODEV;
	}

	ubulk = bulk_from_chip(chip);
	if (ubulk==NULL) {
		printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
		ret = -ENODEV;
		goto hdjbulk_release_bail;
	}

	/* Note: even if I/O is forbidden, allow clients to close their descriptors */

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
	if (0==atomic_dec_return(&ubulk->open_count)) {
		usb_autopm_put_interface(ubulk->iface);
	}
#endif
	
	/* if a notification is attached to this file object, unregister it */
	unregister_for_netlink(chip,file);

	/* This code is to unblock any blocked readers */
	if (is_continuous_reader_supported(ubulk->chip)==1) {
		spin_lock_irqsave(&ubulk->read_list_lock, flags);

		/* get the open queue list item matching the file */
		open_list_item = get_open_list_element(&ubulk->open_list, file);
		if (open_list_item == NULL) {
			ret = -EINVAL;
			printk(KERN_WARNING"%s() no elements matching file:%p are found.\n",__FUNCTION__,file);
			spin_unlock_irqrestore(&ubulk->read_list_lock, flags);
			goto hdjbulk_release_bail;
		}

		/* mark this item as having release being called on it */
		open_list_item->is_releasing = 1;

		/* decrement the usage count and free the memory if it reaches 0 */
		--open_list_item->access_count;
		if (open_list_item->access_count == 0) {
			free_open_list(&ubulk->open_list, file);
		}	

		spin_unlock_irqrestore(&ubulk->read_list_lock,flags);
	}

hdjbulk_release_bail:
	if (chip) {
		/* This balances the increment which we performed in this function */
		dec_chip_ref_count(chip_index);

		/* decrement the chip again to balance the count with open */
		dec_chip_ref_count(chip_index);
	}
	return ret;
}

static void kill_continuous_reader_urbs(struct usb_hdjbulk *ubulk, u8 free_urbs)
{
	int i;
	if (free_urbs) {
		if (ubulk->reader_cached_buffer!=NULL) {
			kfree(ubulk->reader_cached_buffer);
			ubulk->reader_cached_buffer = NULL;	
		}
	}
	for (i = 0; i < DJ_POLL_INPUT_URB_COUNT; i++) {
		if (ubulk->bulk_in_endpoint[i] != NULL) {
			hdjbulk_input_kill_urbs(ubulk->bulk_in_endpoint[i]);
			if (free_urbs!=0) {
				hdjbulk_in_endpoint_delete(ubulk->bulk_in_endpoint[i]);
				ubulk->bulk_in_endpoint[i] = NULL;
			}
		}
	}
}

static void uninit_output_control_state(struct usb_hdjbulk *ubulk)
{
	if (ubulk->chip->product_code!=DJCONTROLSTEEL_PRODUCT_CODE) {
		if (ubulk->output_control_ctl_req!=NULL && ubulk->control_interface!=NULL) {
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_free_coherent(
#else
		usb_buffer_free(
#endif
					interface_to_usbdev(ubulk->control_interface),
					sizeof(*(ubulk->output_control_ctl_req)),
					ubulk->output_control_ctl_req,
					ubulk->output_control_dma);
			ubulk->output_control_ctl_req = NULL;
		}

		if (ubulk->output_control_buffer!=NULL && ubulk->control_interface!=NULL &&
		    ubulk->output_control_urb!=NULL) {
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_free_coherent(
#else
		usb_buffer_free(
#endif
					interface_to_usbdev(ubulk->control_interface),
					ubulk->output_control_urb->transfer_buffer_length,
					ubulk->output_control_buffer,
					ubulk->output_control_urb->transfer_dma);
			ubulk->output_control_buffer = NULL;
		}
		if (ubulk->output_control_urb!=NULL) {
			usb_free_urb(ubulk->output_control_urb);
			ubulk->output_control_urb = NULL;
		}
	} else {
		if (ubulk->output_control_buffer!=NULL) {
			kfree(ubulk->output_control_buffer);
			ubulk->output_control_buffer = NULL;
		}
	}
	if (ubulk->control_interface!=NULL) {
		usb_put_intf(ubulk->control_interface);
		ubulk->control_interface = NULL;
	}
}

void kill_bulk_urbs(struct usb_hdjbulk *ubulk, u8 free_urbs)
{
	if (ubulk->bulk_out_urb != NULL) {
		usb_kill_urb(ubulk->bulk_out_urb);
		if (free_urbs!=0) {
			if (ubulk->bulk_out_buffer!=NULL) {
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_free_coherent(
#else
		usb_buffer_free(
#endif
						ubulk->chip->dev, ubulk->bulk_out_size,
						ubulk->bulk_out_urb->transfer_buffer,
						ubulk->bulk_out_urb->transfer_dma);
				ubulk->bulk_out_buffer = NULL;
			}
			usb_free_urb(ubulk->bulk_out_urb);
			ubulk->bulk_out_urb = NULL;
		}
	}
	kill_continuous_reader_urbs(ubulk,free_urbs);
	if (ubulk->output_control_urb!=NULL) {
		usb_kill_urb(ubulk->output_control_urb);
		if(free_urbs!=0) {
			/* this will free some dependent state as well as the URB */
			uninit_output_control_state(ubulk);
		}
	}
}

static int uninit_continuous_reader(struct usb_hdjbulk *ubulk)
{
	int i, old_state;
	if (is_continuous_reader_supported(ubulk->chip)==0) {
		printk(KERN_WARNING"%s invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		return -EINVAL;
	}
	old_state = atomic_cmpxchg(&ubulk->continuous_reader_state,CR_STOPPED,CR_UNINIT);
	smp_mb(); /* docs say this is required */
	if (old_state!=CR_STOPPED) {
		printk(KERN_INFO"%s() operation already in progress, or bad state\n",__FUNCTION__);
		return -EINVAL;
	}
	for (i = 0; i < DJ_POLL_INPUT_URB_COUNT; i++) {
		if (ubulk->bulk_in_endpoint[i] != NULL) {
			if (ubulk->bulk_in_endpoint[i]->iface!=NULL) {
				usb_put_intf(ubulk->bulk_in_endpoint[i]->iface);
			}
		}
	}
	return 0;
}

void hdj_delete(struct kref *kref)
{
	struct usb_hdjbulk *ubulk = to_hdjbulk_dev(kref);
	
	stop_continuous_reader(ubulk);
	uninit_continuous_reader(ubulk);

	/* note that this will clean the output control state as well */
	kill_bulk_urbs(ubulk,1);
	
	/* give back our minor- now no new I/O is possible */
	if (ubulk->registered_usb_dev!=0) {
		usb_deregister_dev(ubulk->iface, &hdjbulk_class);
		ubulk->registered_usb_dev = 0;
	}

	/* decrement the ref count on our interface and device */
	dereference_usb_intf_and_devices(ubulk->iface);

	if (ubulk->device_context){
		kfree(ubulk->device_context);
		ubulk->device_context = NULL;
	}
	kfree(ubulk);
}

#ifdef CONFIG_PM
void snd_hdjbulk_pre_reset(struct list_head* p)
{
	struct usb_hdjbulk *ubulk;

	ubulk = list_entry(p, struct usb_hdjbulk, list);

	if (ubulk != NULL) {
		return;
	}

	/* stop I/O */
	snd_hdjbulk_suspend(p);	
}

void snd_hdjbulk_post_reset(struct list_head* p)
{
	struct usb_hdjbulk *ubulk = list_entry(p, struct usb_hdjbulk, list);

	if (ubulk != NULL) {
		return;
	}
	
	/* resume I/O */
	hdjbulk_resume(p);
}

void hdjbulk_resume(struct list_head* p)
{
	struct usb_hdjbulk *ubulk = list_entry(p, struct usb_hdjbulk, list);
	int rc;

	if (ubulk == NULL) {
		return;
	}

	if ((rc=start_continuous_reader(ubulk))!=0) {
		printk(KERN_WARNING"%s start_continuous_reader failed rc:%d\n",
			__FUNCTION__,rc);
	}
}

void snd_hdjbulk_suspend(struct list_head* p)
{
	struct usb_hdjbulk *ubulk = list_entry(p, struct usb_hdjbulk, list);
	int rc;
	if (ubulk == NULL) {
		return;
	}

	kill_bulk_urbs(ubulk,0);
	if ((rc=stop_continuous_reader(ubulk))!=0) {
		printk(KERN_WARNING"%s stop_continuous_reader failed rc:%d\n",
			__FUNCTION__,rc);
	}
}
#endif

void hdjbulk_disconnect(struct list_head* p)
{
	struct usb_hdjbulk *ubulk;
	ubulk = list_entry(p, struct usb_hdjbulk, list);

	if (ubulk != NULL) {
		stop_continuous_reader(ubulk);
	}
}

/* Locks the configuration manager, so that NO vendor I/O can be sent down the stack.  
 *  Typically used for firmware upgrade */
void lock_vendor_io(struct usb_hdjbulk *ubulk)
{
	atomic_inc(&ubulk->chip->locked_io);

	/* if a current command is in progress, wait for it to be done */
	while(atomic_read(&ubulk->chip->vendor_command_in_progress) != 0) {
		/* delay for 10 ms so that the device is ready*/
		msleep(10);
	}
}

/* Unlocks the configuration manager, so that vendor I/O can once again be sent down the stack.  
 *  Typically after a firmware upgrade */
void unlock_vendor_io(struct usb_hdjbulk *ubulk)
{
	if (atomic_read(&ubulk->chip->locked_io) > 0) {
		atomic_dec(&ubulk->chip->locked_io);
	}
}

void lock_bulk_output_io(struct usb_hdjbulk *ubulk) 
{
	atomic_inc(&ubulk->chip->locked_io);
	/* wait for current requests to drain */
	while(atomic_read(&ubulk->bulk_out_command_in_progress) != 0) {
		/* delay for 10 ms so that the device is ready*/
		msleep(10);
	}
}

void unlock_bulk_output_io(struct usb_hdjbulk *ubulk) 
{
	if (atomic_read(&ubulk->chip->locked_io) > 0) {
		atomic_dec(&ubulk->chip->locked_io);
	}
}

static void setup_upgrade_and_controller_caps(struct usb_hdjbulk *ubulk)
{
	int rc;
	u16 firmware_version, audio_config, psoc_code=0;
	u8 audio_version, controller_version;
	rc = get_firmware_version(ubulk->chip,&firmware_version,1);
	if (rc!=0) {
		printk(KERN_WARNING"%s() get_firmware_version() failed, rc:%x\n",
			__FUNCTION__,rc);
		return;
	}
	audio_version = firmware_version&0xff;
	controller_version = (firmware_version>>8)&0xf;

	if (ubulk->chip->caps.audio_config && firmware_version!=0) {
		rc = get_audio_config(ubulk,&audio_config,1);
		if (rc!=0) {
			printk(KERN_WARNING"%s() get_audio_config() failed, rc:%x\n",
				__FUNCTION__,rc);
			return;	
		}
		psoc_code = (audio_config>>8)&0xff;
	}
	switch(ubulk->chip->product_code) {
	case DJCONSOLE_PRODUCT_CODE:
		ubulk->chip->caps.audio_board_version = audio_version;
		ubulk->chip->caps.controller_board_version = controller_version;
	break;
	case DJCONSOLE2_PRODUCT_CODE:
		ubulk->chip->caps.audio_board_version = audio_version;
		ubulk->chip->caps.audio_board_in_boot_mode = audio_version==0?1:0;
		ubulk->chip->caps.controller_board_version = controller_version;
		ubulk->chip->caps.controller_board_in_boot_mode = controller_version==0?1:0;
		if(firmware_version==0) {
			ubulk->chip->caps.controller_type = CONTROLLER_TYPE_UNKNOWN; 	
		} else if(psoc_code==PSOC_26_CODE) {
			ubulk->chip->caps.controller_type=CONTROLLER_TYPE_PSOC_26;
		} else if(psoc_code==PSOC_27_CODE) {
			ubulk->chip->caps.controller_type=CONTROLLER_TYPE_PSOC_27;
		} else if(psoc_code==WELTREND_CODE) {
			ubulk->chip->caps.controller_type=CONTROLLER_TYPE_WELTREND;
		} else if(controller_version==FIRST_PSOC1_VERSION) {
			ubulk->chip->caps.controller_type=CONTROLLER_TYPE_PSOC_26;
		} else if(controller_version==FIRST_PSOC2_VERSION) {
			ubulk->chip->caps.controller_type=CONTROLLER_TYPE_PSOC_27;
		} else {
			/* If audio version < 11, we do not know the controller type- 
			 * Or, we could be in controller boot mode */
			ubulk->chip->caps.controller_type=CONTROLLER_TYPE_UNKNOWN;
		}
		if((psoc_code==PSOC_26_CODE || psoc_code==PSOC_27_CODE) ||
				 (controller_version==FIRST_PSOC1_VERSION || 
				 controller_version==FIRST_PSOC2_VERSION) ||
				 controller_version==0) {
			ubulk->chip->caps.controller_board_upgradeable = 1;	
			ubulk->chip->caps.controller_upgrade_requires_USB_reenumeration = 1;
		}
	break;
	case DJCONSOLERMX_PRODUCT_CODE:
		ubulk->chip->caps.audio_board_version = audio_version;
		ubulk->chip->caps.audio_board_in_boot_mode = audio_version==0?1:0;
		ubulk->chip->caps.controller_board_version = controller_version;
		ubulk->chip->caps.controller_board_in_boot_mode = controller_version==0?1:0;
		if(firmware_version==0) {
			ubulk->chip->caps.controller_type = CONTROLLER_TYPE_UNKNOWN; 
		} else if(psoc_code==PSOC_RMX_UPGRADEABLE || psoc_code==PSOC_RMX_BOOTMODE) {
			ubulk->chip->caps.controller_type=CONTROLLER_TYPE_PSOC_26;
		} else {
			ubulk->chip->caps.controller_type=CONTROLLER_TYPE_ETOMS;
		}
		if(psoc_code==PSOC_RMX_UPGRADEABLE||
			psoc_code==PSOC_RMX_BOOTMODE) {
			ubulk->chip->caps.controller_board_upgradeable = 1;
		}
	break;
	case DJCONTROLSTEEL_PRODUCT_CODE:
		ubulk->chip->caps.controller_board_version = firmware_version;
		ubulk->chip->caps.controller_type = CONTROLLER_TYPE_PSCU;
		ubulk->chip->caps.controller_board_upgradeable = 1;
	break;
	}
}

int hdj_create_bulk_interface(struct snd_hdj_chip* chip,
 			      struct usb_interface* iface)
{
	struct usb_hdjbulk *ubulk;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *usb_device=NULL;
	u32 buffer_size;
	int i;
	int retval = -ENOMEM;
	struct hdj_steel_context* dc;
	
	/* allocate memory for our device state and initialize it */
	ubulk = zero_alloc(sizeof(*ubulk), GFP_KERNEL);
	if (!ubulk) {
		retval = -ENOMEM;
		printk(KERN_WARNING"%s() Out of memory\n",__FUNCTION__);
		goto hdj_create_bulk_interface_error;
	}

	ubulk->chip = chip;

	kref_init(&ubulk->kref);
	INIT_LIST_HEAD(&ubulk->open_list);
	atomic_set(&ubulk->continuous_reader_state,CR_UNINIT);
	atomic_set(&ubulk->open_count,0);
	spin_lock_init(&ubulk->read_list_lock); 
	init_completion(&ubulk->bulk_out_completion);
	atomic_set(&ubulk->bulk_out_command_in_progress,0);
	usb_device = interface_to_usbdev(iface);
	ubulk->iface = iface;
	
	/* increment count- will deref in hdj_delete, called when chip is destroyed */
	reference_usb_intf_and_devices(ubulk->iface);

	init_waitqueue_head(&ubulk->read_poll_wait);

	atomic_inc(&ubulk->chip->next_bulk_device);

	/* Add the bulk device to the bulk chip list- when the chip is deleted, cleanup of all bulk devices
	    on the bulk list will take place */
	list_add(&ubulk->list, &ubulk->chip->bulk_list);

	/* set up the endpoint information */
	/* use only the first bulk-out endpoint */
	iface_desc = iface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!ubulk->bulk_out_endpoint_addr && 
			(((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) && 
			((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT))) {

				/* we found a bulk out endpoint */
				ubulk->bulk_out_endpoint_addr = endpoint->bEndpointAddress;
				buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
				ubulk->bulk_out_size = buffer_size;
		} else if (!ubulk->bulk_in_endpoint_addr && 
			(((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) && 
			((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN))) {
			/* we found a bulk in endpoint */
			ubulk->bulk_in_endpoint_addr = endpoint->bEndpointAddress;
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			ubulk->bulk_in_size = buffer_size;
		}
	}
	if (!(ubulk->bulk_out_endpoint_addr)) {
		printk(KERN_WARNING"%s() Could not find bulk-out endpoint\n",__FUNCTION__);
		retval = -EINVAL;
		goto hdj_create_bulk_interface_error;
	}

	/* the steel has an input endpoint */
	if (ubulk->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE &&
	    ubulk->bulk_in_endpoint_addr==0) {
		printk(KERN_WARNING"%s() Could not find bulk-input endpoint (product:%d)\n",
			__FUNCTION__,ubulk->chip->product_code);
		retval = -EINVAL;
		goto hdj_create_bulk_interface_error;
	}

	/* Get the bulk pipe- when we initialize the continuous reader we will acquire the bulk input pipe,
	    if applicable (it may turn out to be an HID in pipe depending on the product */
	ubulk->bulk_out_pipe = usb_sndbulkpipe(ubulk->chip->dev, ubulk->bulk_out_endpoint_addr);

	ubulk->bulk_out_urb = usb_alloc_urb(0,GFP_KERNEL);
	if (ubulk->bulk_out_urb==NULL) {
		printk(KERN_WARNING"%s() usb_alloc_urb() returned NULL, bailing\n",__FUNCTION__);
		retval = -ENOMEM;
		goto hdj_create_bulk_interface_error;
	}
	/* allocate the buffer for bulk_out_urb */
	init_MUTEX(&ubulk->bulk_out_buffer_mutex);
	
	ubulk->bulk_out_buffer =
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_alloc_coherent(
#else
		usb_buffer_alloc(
#endif
			ubulk->chip->dev, ubulk->bulk_out_size,
			GFP_KERNEL, &ubulk->bulk_out_urb->transfer_dma);

	if (ubulk->bulk_out_buffer==NULL) {
		printk(KERN_WARNING"%s() usb_buffer_alloc() failed\n",__FUNCTION__);

		retval = -ENOMEM;
		goto hdj_create_bulk_interface_error;
	}
	ubulk->bulk_out_urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;

	/* Detect the correct device and initialize */
	switch(ubulk->chip->product_code) {
	case DJCONSOLE_PRODUCT_CODE:
		ubulk->device_context = zero_alloc(sizeof(struct hdj_console_context), GFP_KERNEL);
		if (!ubulk->device_context)  {
			printk(KERN_WARNING"%s() context allocation failure\n",__FUNCTION__);
			retval = -ENOMEM;
			goto hdj_create_bulk_interface_error;
		}
		retval = hdjbulk_init_dj_console(ubulk);
		break;
	case DJCONSOLE2_PRODUCT_CODE:
		ubulk->device_context = zero_alloc(sizeof(struct hdj_mk2_rmx_context), GFP_KERNEL);
		if (!ubulk->device_context)  {
			printk(KERN_WARNING"%s() context allocation failure\n",__FUNCTION__);
			retval = -ENOMEM;
			goto hdj_create_bulk_interface_error;
		}

		retval = hdjbulk_init_dj_mk2(ubulk);
		break;
	case DJCONSOLERMX_PRODUCT_CODE:
		ubulk->device_context = zero_alloc(sizeof(struct hdj_mk2_rmx_context), GFP_KERNEL);
		if (!ubulk->device_context)  {
			printk(KERN_WARNING"%s() context allocation failure\n",__FUNCTION__);
			retval = -ENOMEM;
			goto hdj_create_bulk_interface_error;
		}

		retval = hdjbulk_init_dj_rmx(ubulk);
		break;
	case DJCONTROLSTEEL_PRODUCT_CODE:
		ubulk->device_context = zero_alloc(sizeof(struct hdj_steel_context), GFP_KERNEL);
		if (!ubulk->device_context) {
			printk(KERN_WARNING"%s() context allocation failure\n",__FUNCTION__);
			retval = -ENOMEM;
			goto hdj_create_bulk_interface_error;
		}
		dc = ((struct hdj_steel_context*)ubulk->device_context);
		if (usb_device->config->desc.bNumInterfaces > 1) {
			atomic_set(&dc->device_mode, DJ_STEEL_IN_NORMAL_MODE);
		} else if (usb_device->config->desc.bNumInterfaces == 1) {
			atomic_set(&dc->device_mode, DJ_STEEL_IN_BOOT_MODE);
		} else {
			atomic_set(&dc->device_mode, DJ_STEEL_IN_UNKNOWN_MODE);
			printk(KERN_WARNING"%s() Invalid Number of interfaces: %d\n", 
					__FUNCTION__,
					usb_device->config->desc.bNumInterfaces);
		}

		retval = hdjbulk_init_dj_steel(ubulk);
		
		break;
	default:
		printk(KERN_WARNING"%s() unknown product found\n",__FUNCTION__);
		retval = -EINVAL;
		goto hdj_create_bulk_interface_error;
	}

	if (retval != 0) {
		printk(KERN_WARNING"%s(): hdjbulk_init failed\n",__FUNCTION__);
		goto hdj_create_bulk_interface_error;
	}

	/* we can register the device now, as it is ready-we are now ready to receive user requests */
	retval = usb_register_dev(iface, &hdjbulk_class);
	if (retval) {
		/* something prevented us from registering this driver */
		printk(KERN_ERR"%s() usb_register_dev failed.\n",__FUNCTION__);
		goto hdj_create_bulk_interface_error;
	}
	ubulk->registered_usb_dev = 1;

	setup_upgrade_and_controller_caps(ubulk);
	
	/* success */
	dump_product_name_to_console(ubulk->chip,1,0);
	return 0;

hdj_create_bulk_interface_error:
	printk(KERN_WARNING"%s() Failed, rc:%d\n",__FUNCTION__,retval);

	return retval;
}

/*
 * Submits the URB, with error handling.
 */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14) )
int hdjbulk_submit_urb(struct snd_hdj_chip* chip, struct urb* urb, gfp_t flags)
#else
int hdjbulk_submit_urb(struct snd_hdj_chip* chip, struct urb* urb, int flags)
#endif
{
	int err;
	/* see if we should cease sending out URBs- typically disconnect, or standby */
	if (can_send_urbs(chip)!=0) {
		return -EBUSY;
	}

	err = usb_submit_urb(urb, flags);
	if (err < 0) {
		switch(err) {
		case -ENOMEM:
			printk(KERN_ERR "%s(): usb_submit_urb: -ENOMEM\n",__FUNCTION__);
			break;
		case -ENODEV:
			/*printk(KERN_ERR "%s() usb_submit_urb: -ENODEV\n",__FUNCTION__);*/
			break;
		case -EPIPE:
			printk(KERN_ERR "%s() usb_submit_urb: -EPIPE\n",__FUNCTION__);
			break;
		case -EAGAIN:
			printk(KERN_ERR "%s() usb_submit_urb: -EAGAIN\n",__FUNCTION__);
			break;
		case -EFBIG:
			printk(KERN_ERR "%s() usb_submit_urb: -EFBIG\n",__FUNCTION__);
			break;
		case -EINVAL:
			printk(KERN_ERR "%s() usb_submit_urb: -EINVAL\n",__FUNCTION__);
			break;
		default:
			printk(KERN_ERR "%s() usb_submit_urb: err: %d\n", __FUNCTION__,err);
			break;
		}

	} 

	return err;
}

static int hdjbulk_in_urb_complete_steel(struct hdjbulk_in_endpoint *ep, struct urb* urb)
{	
	int index = 0;
	struct hdj_steel_context * dc = (struct hdj_steel_context *)ep->ubulk->device_context;
	
	if (atomic_read(&dc->device_mode) == DJ_STEEL_IN_NORMAL_MODE) {
		if (urb->actual_length < DJ_CONTROL_STEEL_BULK_TRANSFER_MIN_SIZE) {
			printk(KERN_WARNING"%s() Invalid Buffer Length: %d\n", __FUNCTION__, urb->actual_length);
			return -EINVAL;
		}

		/*check the device's sequence number*/
		if ((atomic_inc_return(&dc->sequence_number)&0xff) != 
				((u8*)urb->transfer_buffer)[DJ_STEEL_EP_81_SEQ_NUM]) {
			printk(KERN_INFO"%s() Invalid sequence number: 0x%02x != 0x%02x\n", 
				__FUNCTION__,
				atomic_read(&dc->sequence_number), 
				((u8*)urb->transfer_buffer)[DJ_STEEL_EP_81_SEQ_NUM]);
			atomic_set(&dc->sequence_number, 
					(((u8*)urb->transfer_buffer)[DJ_STEEL_EP_81_SEQ_NUM]));
		}

		/* Save these states */
		/*get the fx state*/
		atomic_set(&dc->fx_state,
			(((u8*)urb->transfer_buffer)[DJ_STEEL_EP_81_FX_STATE_0] << 8) + 
			(((u8*)urb->transfer_buffer)[DJ_STEEL_EP_81_FX_STATE_1] & 0xFF));

		/*save the setting*/
		atomic_set(&dc->mode_shift_state,
			((u8*)urb->transfer_buffer)[DJ_STEEL_EP_81_MODE_SHIFT_STATE]);

		/*get the jog wheel parameters*/
		atomic_set(&dc->jog_wheel_parameters,
				(((u8*)urb->transfer_buffer)[DJ_STEEL_EP_81_JOG_WHEEL_SETTINGS_0] << 8) + 
				(((u8*)urb->transfer_buffer)[DJ_STEEL_EP_81_JOG_WHEEL_SETTINGS_1] & 0xFF));
	} else if (atomic_read(&dc->device_mode) == DJ_STEEL_IN_BOOT_MODE) {
		if (urb->actual_length < 2) {
			printk(KERN_ERR"%s() Invalid Buffer Length: %d\n", __FUNCTION__,urb->actual_length);
			return -EINVAL;
		}
		if (((u8*)urb->transfer_buffer)[0] != ((u8*)urb->transfer_buffer)[1]) {
			printk(KERN_ERR"%s() Invalid Buffer\n",__FUNCTION__);
			return -EINVAL;
		}
	}

	/* check if there is a request pending, and if so, fullfill it */
	if (atomic_read(&dc->is_bulk_read_request_pending)== 1) {
		spin_lock(&dc->bulk_buffer_lock);

		/* copy the buffer to the context */
		/* IOCTLs that use this buffer are in a sequential queue, so this is safe */
		memcpy(dc->bulk_data, urb->transfer_buffer, urb->actual_length);

		/*pad the rest with zeros*/
		for(index = urb->actual_length; index < ep->max_transfer; index++) {
			dc->bulk_data[index] = 0;
		}

		atomic_set(&dc->is_bulk_read_request_pending, 0);
		complete(&dc->bulk_request_completion);
		spin_unlock(&dc->bulk_buffer_lock);
	}
	return 0;
}

/* fix for mk2 fw hm issue */
static void hdjmk2_hm_fwfix(struct usb_hdjbulk *ubulk, u8* urb_transfer_buffer)
{
	u8 prev_fifth_byte_hm_only;
	u8 fifth_byte_hm_only;
	
	prev_fifth_byte_hm_only=(ubulk->reader_cached_buffer[5])&0xf;
	fifth_byte_hm_only=(urb_transfer_buffer[5])&0xf;
	
	if ( ((fifth_byte_hm_only!=0x1) && 
		(fifth_byte_hm_only!=0x2) &&
		(fifth_byte_hm_only!=0x4) &&
		(fifth_byte_hm_only!=0x8)) ||
		(fifth_byte_hm_only==0) ||
		( ((fifth_byte_hm_only==1) && (prev_fifth_byte_hm_only!=2)) &&	 /*deckB to deckA*/
		((fifth_byte_hm_only==2) && (prev_fifth_byte_hm_only!=1)) && /*deckA to deckB*/
		((fifth_byte_hm_only==2) && (prev_fifth_byte_hm_only!=8)) && /*mix to deck B*/
		((fifth_byte_hm_only==4) && (prev_fifth_byte_hm_only!=8)) && /*mix to split*/
		((fifth_byte_hm_only==8) && (prev_fifth_byte_hm_only!=4)) && /*split to mix*/
		((fifth_byte_hm_only==8) && (prev_fifth_byte_hm_only!=2))) ) { /* deckB to mix */
		/* ignore bad values for HM */
		urb_transfer_buffer[5] &= 0xf0;
		urb_transfer_buffer[5] |= prev_fifth_byte_hm_only;
		if ((urb_transfer_buffer[5] & 0xf0)==0) {
				urb_transfer_buffer[5] |= 1; /* set to deck A if still invalid */
		}
	}
}

/*
 * Processes the data read from the device.
 */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void hdjbulk_in_urb_complete(struct urb* urb)
#else
void hdjbulk_in_urb_complete(struct urb* urb, struct pt_regs *junk)
#endif
{
	struct hdjbulk_in_endpoint *ep = urb->context;
	
	if (urb->status == 0) {
		
		/* check the urb sequence number */
		if (atomic_inc_return(&ep->ubulk->expected_urb_sequence_number) != 
			atomic_read(&ep->urb_sequence_number)) {
			printk(KERN_INFO"%s(): len:%d sequence num: %d != %d\n",
				__FUNCTION__,
				urb->actual_length, atomic_read(&ep->urb_sequence_number), 
				atomic_read(&ep->ubulk->expected_urb_sequence_number) - 1);

			atomic_set(&ep->ubulk->expected_urb_sequence_number,
					atomic_read(&ep->urb_sequence_number));
		}

		/* handle steel specific processing */
		if (ep->ubulk->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
			if (hdjbulk_in_urb_complete_steel(ep,urb)!=0) {
				goto hdjbulk_in_urb_complete_bail;
			}
		}
		
		/* This is for all products which use the continuous reader, and access the date through read */
		spin_lock(&ep->ubulk->read_list_lock);
	
		/* fw fix for hm monitor */
		if (ep->ubulk->chip->product_code==DJCONSOLE2_PRODUCT_CODE) {
			hdjmk2_hm_fwfix(ep->ubulk,urb->transfer_buffer);	
			memcpy(ep->ubulk->reader_cached_buffer,
				urb->transfer_buffer,
				urb->actual_length);
		}

		/* fill the queued elements- this services read */
		fill_queued_buffers(&ep->ubulk->open_list, 
					urb->transfer_buffer, urb->actual_length);

		spin_unlock(&ep->ubulk->read_list_lock);

	} else if (atomic_read(&ep->ubulk->chip->shutdown)!=0){
		printk(KERN_ERR"%s(): error:%d\n",__FUNCTION__,urb->status);
	}

hdjbulk_in_urb_complete_bail:
	atomic_set(&ep->urb_sequence_number,
			 (atomic_inc_return(&ep->ubulk->current_urb_sequence_number) & 0xFFFF));
	
	urb->dev = ep->ubulk->chip->dev;
	/* this will not try to resubmit if we are shutting down, or suspend has forbidden us to
	 *  to send requests */
	hdjbulk_submit_urb(ep->ubulk->chip, urb, GFP_ATOMIC);
}

int send_boot_loader_command(struct usb_hdjbulk *ubulk, u8 boot_loader_command)
{
	int ret = 0;
	int retry_count = DJ_STEEL_MAX_RETRY_UPGRADE;
	u8 bulk_data_receive[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];
	u8 bulk_data_send[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];

	printk(KERN_INFO"%s() boot_loader_command: 0x%02X\n", __FUNCTION__,boot_loader_command);

	do {
		down(&((struct hdj_steel_context*)ubulk->device_context)->bulk_request_mutex);

		if (ubulk->bulk_out_buffer == NULL || DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE > ubulk->bulk_out_size) {
			printk(KERN_WARNING"%s(): Invalid URB Buffer\n",__FUNCTION__);
			ret = -ENOMEM;
			
			break;
		}
		memset(bulk_data_send, 0, DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE);
		bulk_data_send[0] = DJ_STEEL_BOOT_LOADER_MODE;
		bulk_data_send[1] = boot_loader_command;

		/* tell the continuous reader that we will be waiting for a request */
		atomic_set(&((struct hdj_steel_context*)ubulk->device_context)->is_bulk_read_request_pending, 1);

		/* transfer the buffer to the device */
		ret = firmware_send_bulk(ubulk, 
					bulk_data_send, 
					DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE,
					1 /* force _send */);

		if (ret == 0) {
			
			memset(bulk_data_receive, 0, sizeof(bulk_data_receive));
			ret = wait_for_bulk_answer(ubulk, bulk_data_receive, 
							sizeof(bulk_data_receive), 
							BULK_READ_TIMEOUT);
			if (ret == 0) {
				if ((bulk_data_receive[DJ_STEEL_BOOT_LOADER_RESPONSE] != 
					DJ_STEEL_BOOT_LOADER_COMPLETE &&
				    bulk_data_receive[DJ_STEEL_BOOT_LOADER_RESPONSE] != 
					DJ_STEEL_BOOT_LOADER_SUCCESS &&
				    bulk_data_receive[DJ_STEEL_BOOT_LOADER_RESPONSE] != 
					DJ_STEEL_BOOT_LOADER_SUCCESS_2)) {
					printk(KERN_ERR"%s() wait_for_bulk_answer failed: Response: 0x%02X, retry count: %d\n", 
						__FUNCTION__,
						bulk_data_receive[DJ_STEEL_BOOT_LOADER_RESPONSE], 
						retry_count);
					ret = -EINVAL;
				} else {
					/*success*/
					retry_count = 0;
				}
			} else {
				if (ret != -ENODEV) 	{
					printk(KERN_ERR"%s() wait_for_bulk_answer failed: %d, retry count: %d\n", 
						__FUNCTION__,ret, retry_count);
				}
			}
		} else {
			if (ret != -ENODEV) {
				printk(KERN_ERR"%s() firmware_send_bulk failed: %d, retry count: %d\n", 
					__FUNCTION__,ret, retry_count);
			}
		}
		up(&((struct hdj_steel_context*)ubulk->device_context)->bulk_request_mutex);
	} while (--retry_count > 0 && (ret != 0));

	return ret;
}

int get_bulk_data(struct usb_hdjbulk *ubulk,
			u8* bulk_data,
			u32 size,
			u8 force_send)
{
	int	ret = 0;
	u32	retry_count = DJ_MAX_RETRY;
	u8 bulk_data_send[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		down(&((struct hdj_steel_context*)ubulk->device_context)->bulk_request_mutex);
		do {
			/* tell the continuous reader that we will be waiting for a request */
			atomic_set(&((struct hdj_steel_context*)ubulk->device_context)->is_bulk_read_request_pending, 
					1);

			/* prepare request to force a bulk input packet to be sent */
			memset(bulk_data_send,0,sizeof(bulk_data_send));
			bulk_data_send[0] = DJ_STEEL_FORCE_REPORT_IN;

			/*request a report */
			ret = send_bulk_write(ubulk, bulk_data_send, sizeof(bulk_data_send),force_send);
			if (ret == 0) {
				ret = wait_for_bulk_answer(ubulk, bulk_data, size, BULK_READ_TIMEOUT);
			}

			if (ret != 0) {
				printk(KERN_ERR"%s() Failed, retries left: %d\n", __FUNCTION__,retry_count - 1);
			}
		} while ((ret != 0) && (--retry_count > 0));
		up(&((struct hdj_steel_context*)ubulk->device_context)->bulk_request_mutex);
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	return ret;
}

int send_bulk_write(struct usb_hdjbulk *ubulk,
			u8* bulk_data,
			u32 size,
			u8 force_send)
{
	int	ret = 0;
	u32	retry_count = DJ_MAX_RETRY;
	struct hdj_steel_context* dc;
	dc = ((struct hdj_steel_context*)ubulk->device_context);
	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		if (atomic_read(&dc->device_mode) != DJ_STEEL_IN_NORMAL_MODE) {
			printk(KERN_WARNING"%s() I/O is locked since the device is in BOOT mode, bailing!\n",
				__FUNCTION__);
			return -EINVAL;
		}

		do {
			/*send the buffer to the device*/
			ret = firmware_send_bulk(ubulk, bulk_data, size,force_send);
			if (ret != 0 && ret != -ENODEV) {
				printk(KERN_ERR"%s() firmware_send_bulk failed, retries left: %d\n", 
					__FUNCTION__,retry_count - 1);
			}
		} while ((ret != 0) && (--retry_count > 0));
	} else {
		printk(KERN_WARNING"%s() Not supported for this Device\n",__FUNCTION__);
		ret = -EINVAL;
	}

	return ret;
}

int wait_for_bulk_answer(struct usb_hdjbulk *ubulk,
				u8* bulk_data,
				u32 size, 
				u32 timeout)
{
	int	ret = 0;
	unsigned long flags;

	if (ubulk->chip->product_code != DJCONTROLSTEEL_PRODUCT_CODE) {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		return -EINVAL;
	}

	if (size > DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE) {
		printk(KERN_WARNING"%s() Invalid Buffer size: %u > %u\n", 
				__FUNCTION__,size, DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE);
		return -EINVAL;
	}

	/* wait for the completion of the task */
	ret = wait_for_completion_timeout(&((struct hdj_steel_context*)ubulk->device_context)->bulk_request_completion, 
						timeout * HZ/1000);
	
	if (ret > 0) {
		ret = 0;
		spin_lock_irqsave(&((struct hdj_steel_context*)ubulk->device_context)->bulk_buffer_lock, flags);
		memcpy(	bulk_data,
			((struct hdj_steel_context*)ubulk->device_context)->bulk_data,
			size);
		spin_unlock_irqrestore(&((struct hdj_steel_context*)ubulk->device_context)->bulk_buffer_lock, flags);
	} else {
		printk(KERN_ERR"%s() Wait Failed (timed out): %d\n", __FUNCTION__,ret);

		ret = -EIO;
	}

	return ret;
}

/* WARNING: requires that hdjbulk_init_continuous_reader was called first */
static int init_output_control_state(struct usb_hdjbulk *ubulk)
{
	int old_state;
	int ret=0;
	/* Every product which supports a continuous reader for input control data supports the
	 *  setting of control output data though an IOCTL */
	if (is_continuous_reader_supported(ubulk->chip)==0) {
		printk(KERN_WARNING"%s invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		return -EINVAL;
	}

	/* Check to see if the continuous reader has been initialized- if not, bail. */
	if ((old_state=atomic_read(&ubulk->continuous_reader_state))!=CR_STOPPED) {
		printk(KERN_INFO"%s() bad state (%d) for continuous reader bailing\n",
			__FUNCTION__,old_state);
		return 0;
	}

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		/* The steel directs control data to the bulk output endpoint */
		ubulk->output_control_buffer_size = ubulk->bulk_out_size;
		/* this is just the bulk interface, there is no HID interface */
		if ((ubulk->control_interface = usb_get_intf(ubulk->iface))==NULL) {
			printk(KERN_WARNING"%s() usb_get_intf failed bailing\n",__FUNCTION__);
			return -EINVAL;
		}
	} else if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		/* This is HID-  hdjbulk_init_continuous_reader has referenced the HID interface */
		ubulk->output_control_buffer_size = DJC_SET_REPORT_LEN;
		if ((ubulk->control_interface = usb_get_intf(ubulk->bulk_in_endpoint[0]->iface))==NULL) {
			printk(KERN_WARNING"%s() usb_get_intf failed bailing\n",__FUNCTION__);
			return -EINVAL;
		}
	} else if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		/* This is HID-  hdjbulk_init_continuous_reader has referenced the HID interface */
		ubulk->output_control_buffer_size = DJMK2_SET_REPORT_LEN;
		if ((ubulk->control_interface = usb_get_intf(ubulk->bulk_in_endpoint[0]->iface))==NULL) {
			printk(KERN_WARNING"%s() usb_get_intf failed bailing\n",__FUNCTION__);
			return -EINVAL;
		}
	} else if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		/* This is HID-  hdjbulk_init_continuous_reader has referenced the HID interface */
		ubulk->output_control_buffer_size = DJRMX_SET_REPORT_LEN;
		if ((ubulk->control_interface = usb_get_intf(ubulk->bulk_in_endpoint[0]->iface))==NULL) {
			printk(KERN_WARNING"%s() usb_get_intf failed bailing\n",__FUNCTION__);
			return -EINVAL;
		}
	}  else {
		/* invalid product */
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		return -EINVAL;
	}

	init_MUTEX(&ubulk->output_control_mutex);
	init_completion(&ubulk->output_control_completion);

	/* Every product here except the Steel targets HID.  Since the steel does not target HID, we don't
	 *  need to allocate a URB, and USB buffer.  We'll just allocate a buffer with kmalloc for the
	 *  control state */
	if (ubulk->chip->product_code != DJCONTROLSTEEL_PRODUCT_CODE) {	
		/* allocate memory for setup packet for our control requests */
		ubulk->output_control_ctl_req = 
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_alloc_coherent(
#else
		usb_buffer_alloc(
#endif
								interface_to_usbdev(ubulk->control_interface), 
						 		sizeof(*(ubulk->output_control_ctl_req)),
						 		GFP_KERNEL, 
								 &ubulk->output_control_dma);
		if (ubulk->output_control_ctl_req==NULL) {
			printk(KERN_WARNING"%s() usb_buffer_alloc failed (ctl req)\n",__FUNCTION__);
			ret = -ENOMEM;
			goto hdjbulk_init_output_control_state_error;
		} else {
			memset(ubulk->output_control_ctl_req,0,sizeof(*(ubulk->output_control_ctl_req)));
		}
	
		ubulk->output_control_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (ubulk->output_control_urb==NULL) {
			printk(KERN_WARNING"%s() failed to allocate output control urb\n",__FUNCTION__);
			ret = -ENOMEM;
			goto hdjbulk_init_output_control_state_error;
		}

		ubulk->output_control_buffer = 
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_alloc_coherent(
#else
		usb_buffer_alloc(
#endif
								interface_to_usbdev(ubulk->control_interface),
								ubulk->output_control_buffer_size, 
								GFP_KERNEL,
								&ubulk->output_control_urb->transfer_dma);
		if (ubulk->output_control_buffer==NULL) {
			printk(KERN_WARNING"%s() failed to allocate output control usb buffer\n",__FUNCTION__);
			ret = -ENOMEM;
			goto hdjbulk_init_output_control_state_error;
		} else {
			memset(ubulk->output_control_buffer,0,ubulk->output_control_buffer_size);
		}
	} else {
		ubulk->output_control_buffer = zero_alloc(ubulk->output_control_buffer_size,GFP_KERNEL);
		if (ubulk->output_control_buffer==NULL) {
			printk(KERN_WARNING"%s() failed to allocate output control usb buffer\n",__FUNCTION__);
			ret = -ENOMEM;
			goto hdjbulk_init_output_control_state_error;
		}
	}

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		/* this is bulk not HID */
		ubulk->output_control_buffer[0] = DJ_STEEL_STANDARD_SET_LED_REPORT;
	} else if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		/* this is HID */
		ubulk->output_control_buffer[0] = DJC_SET_REPORT_ID;
	} else if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		/* this is HID */
		ubulk->output_control_buffer[0] = DJMK2_SET_REPORT_ID;
	} else if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		/* this is HID */
		ubulk->output_control_buffer[0] = DJRMX_SET_REPORT_ID;
	}

	return ret;
hdjbulk_init_output_control_state_error:
	/* cleanup */
	uninit_output_control_state(ubulk);
	return ret;	
}

/* Sets up continuous reader- for some products, targets bulk input endpoint, for others
 *  targets HID input endpoint.*/
static int init_continuous_reader(struct usb_hdjbulk *ubulk)
{
	unsigned int pipe;
	int i, interface_number, ret=0;
	struct hdjbulk_in_endpoint *ep[DJ_POLL_INPUT_URB_COUNT];
	void* buffer;
	struct usb_host_interface *interface_desc;
	struct usb_interface *interface=NULL;
	struct usb_endpoint_descriptor *endpoint;

	if (is_continuous_reader_supported(ubulk->chip)==0) {
		printk(KERN_WARNING"%s invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		return -EINVAL;
	}

	for (i = 0; i < DJ_POLL_INPUT_URB_COUNT; i++) {
		ep[i] = NULL;
	}

	spin_lock_init(&ubulk->read_list_lock);
	INIT_LIST_HEAD(&ubulk->open_list);
	atomic_set(&ubulk->continuous_reader_state,CR_UNINIT);

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		interface_number = DJSTEEL_BULK_INTERFACE_NUMBER;
		interface_desc = ubulk->iface->cur_altsetting;
		interface = ubulk->iface;
		endpoint = &interface_desc->endpoint[0].desc;
		pipe = usb_rcvbulkpipe(ubulk->chip->dev, ubulk->bulk_in_endpoint_addr);
		/* the continuous reader uses the max packet size on the bulk interface */
		ubulk->continuous_reader_packet_size = ubulk->bulk_in_size;
	} else {
		interface_number = get_hid_polling_interface_number(ubulk);
		if (interface_number==-1) {
			printk(KERN_WARNING"%s() get_hid_polling_interface_number() returned invalid interface \
					for product:%d\n",__FUNCTION__,ubulk->chip->product_code);
			return -EINVAL;
		}
		interface = usb_ifnum_to_if(ubulk->chip->dev,interface_number);
		if (interface==NULL) {
			printk(KERN_WARNING"%s() usb_ifnum_to_if() failed for product:%d, int_num:%d\n",
					__FUNCTION__,ubulk->chip->product_code,interface_number);
			return -EINVAL;
		}
		interface_desc = interface->cur_altsetting;
		if (interface_desc->desc.bNumEndpoints != 1) {
			printk(KERN_WARNING"%s() bNumEndpoint!=1 (%d) bailing\n",
					__FUNCTION__,interface_desc->desc.bNumEndpoints);
			return -EINVAL;
		}
		endpoint = &interface_desc->endpoint[0].desc;
		if (((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_INT) ||
		     ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) != USB_DIR_IN)) {
			printk(KERN_WARNING"%s() unexpected endpoint attributes (require INTERRUPT, INPUT)\n",
				__FUNCTION__);
			return -EINVAL;
		}
		pipe = usb_rcvintpipe(ubulk->chip->dev,	
					interface->cur_altsetting->endpoint->desc.bEndpointAddress);
		/* the continuous reader uses the max packet size on the hid interface */
		ubulk->continuous_reader_packet_size = usb_maxpacket(ubulk->chip->dev, pipe, usb_pipeout(pipe));
	}
	if (ubulk->continuous_reader_packet_size > HDJ_POLL_INPUT_BUFFER_SIZE) {
		printk(KERN_WARNING"%s() continuous_reader_packet_size (%d) > hid poll packet size (%lu), bailing\n",
				__FUNCTION__,
				ubulk->continuous_reader_packet_size,
				HDJ_POLL_INPUT_BUFFER_SIZE);
		return -EINVAL;
	}
	
	ubulk->reader_cached_buffer = zero_alloc(ubulk->continuous_reader_packet_size,
													GFP_KERNEL);
	if (ubulk->reader_cached_buffer==NULL) {
		printk(KERN_WARNING"%s() failed to alloc reader buffer\n",__FUNCTION__);
		ret = -ENOMEM;
		goto init_continuous_reader_error;	
	}
	for (i = 0; i < DJ_POLL_INPUT_URB_COUNT; i++) {
		ep[i] = zero_alloc(sizeof(struct hdjbulk_in_endpoint), GFP_KERNEL);
		if (!ep[i]) {
			snd_printk(KERN_WARNING"%s() failed to allocate output endpoint\n",__FUNCTION__);
			ret = -ENOMEM;
			goto init_continuous_reader_error;
		}
		ep[i]->interface_number = interface_number;
		ep[i]->iface = usb_get_intf(interface); 
		if (ep[i]->iface==NULL) {
			printk(KERN_WARNING"%s() usb_get_intf failed bailing\n",__FUNCTION__);
			ret = -ENOMEM;
			goto init_continuous_reader_error;
		}
		ep[i]->ubulk = ubulk;
		
		ep[i]->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!ep[i]->urb) {
			printk(KERN_WARNING"%s() failed to allocate URB\n",__FUNCTION__);
			
			ret = -ENOMEM;
			goto init_continuous_reader_error;
		}

		ep[i]->max_transfer = ubulk->continuous_reader_packet_size;
		buffer = 
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_alloc_coherent(
#else
		usb_buffer_alloc(
#endif
					ubulk->chip->dev, ep[i]->max_transfer,
					GFP_KERNEL, &ep[i]->urb->transfer_dma);
		if (!buffer) {
			printk(KERN_WARNING"%s() usb_buffer_alloc() failed\n",__FUNCTION__);
			
			ret = -ENOMEM;
			goto init_continuous_reader_error;
		}

		atomic_set(&ep[i]->urb_sequence_number,
			(atomic_inc_return(&ubulk->current_urb_sequence_number) & 0xFFFF));

		if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT) {
			usb_fill_int_urb(ep[i]->urb, 
				ubulk->chip->dev,
				pipe,
				buffer, 
				ep[i]->max_transfer,
				hdjbulk_in_urb_complete, 
				ep[i],
				interface->cur_altsetting->endpoint->desc.bInterval);
		} else {
			usb_fill_bulk_urb(ep[i]->urb, 
				ubulk->chip->dev,
				pipe,
				buffer, 
				ep[i]->max_transfer,
				hdjbulk_in_urb_complete, 
				ep[i]);
		}

		ep[i]->urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;

		/*save the endpoint's address*/
		ep[i]->ubulk->bulk_in_endpoint[i] = ep[i];
	}

	atomic_set(&ubulk->continuous_reader_state,CR_STOPPED);

	return 0;
init_continuous_reader_error:

	/* cleanup */
	if (ubulk->reader_cached_buffer) {
		kfree(ubulk->reader_cached_buffer);
		ubulk->reader_cached_buffer = NULL;
	}
	for (i = 0; i < DJ_POLL_INPUT_URB_COUNT; i++) {
		if (ep[i] != NULL) {
			if (ep[i]->iface!=NULL) {
				usb_put_intf(ep[i]->iface);
				ep[i]->iface = NULL;
			}
			hdjbulk_in_endpoint_delete(ep[i]);
			ep[i]->ubulk->bulk_in_endpoint[i] = NULL;
		}
	}
	return ret;
}

static int hdjbulk_init_common_context(struct usb_hdjbulk *ubulk, struct hdj_common_context * cc)
{
	int ret = 0;
	u16 value = 0;
	
	ret = get_firmware_version(ubulk->chip, &value, 1);
	if (ret != 0) {
		printk(KERN_ERR"%s() get_firmware_version failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&cc->firmware_version,value);
	}
	
	return ret;
}

int hdjbulk_init_dj_console(struct usb_hdjbulk *ubulk)
{
	int ret = 0;
	u16 value = 0;
	struct hdj_console_context *dc = ((struct hdj_console_context *)ubulk->device_context);

	init_MUTEX(&dc->device_config_mutex);
	
	ret = hdjbulk_init_common_context(ubulk,&ubulk->hdj_common);
	if (ret!=0) {
		printk(KERN_WARNING"%s() hdjbulk_init_common_context failed, rc:%d",
			__FUNCTION__,ret);
	}

	ret = get_djconsole_device_config(ubulk->chip->index, &value, 1);
	if (ret != 0) {
		printk(KERN_ERR"%s() get_djconsole_device_config failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->device_config,value);
	}
	
	ret = get_sample_rate(ubulk, &value, 1);
	if (ret != 0) {
		printk(KERN_ERR"%s() get_sample_rate failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->sample_rate,value);
	}

	if ((ret = init_continuous_reader(ubulk))!=0) {
		printk(KERN_WARNING"%s() hdjbulk_init_continuous_reader() failed, rc:%d\n",
			__FUNCTION__,ret);
		return ret;
	}

	if ((ret = init_output_control_state(ubulk))!=0) {
		printk(KERN_WARNING"%s() hdjbulk_init_output_control_state() failed, rc:%d\n",
			__FUNCTION__,ret);
		return ret;
	}
	
	if ((ret = start_continuous_reader(ubulk))!=0) {
		printk(KERN_ERR"%s() start_continuous_reader failed, rc:%d\n",
				__FUNCTION__,ret);
		return ret;
	}
	
	/* Just like the Mk2, we need to activate talkover and never turn it off */
	ret = activate_talkover(ubulk);
	if (ret!=0) {
		printk(KERN_ERR"%s() activate_talkover failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
	}
	
	ret = set_talkover_enable(ubulk,0);
	if (ret !=0) {
			printk(KERN_ERR"%s() set_talkover_enable failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
	}

	clear_leds(ubulk->chip); 
	set_mouse_state(ubulk->chip,0);

	return ret;
}

int hdjbulk_init_dj_mk2(struct usb_hdjbulk *ubulk)
{
	int ret = 0;
	u16 value = 0;
	struct hdj_mk2_rmx_context *dc = ((struct hdj_mk2_rmx_context*)ubulk->device_context);
	
	ret = hdjbulk_init_common_context(ubulk,&ubulk->hdj_common);
	if (ret!=0) {
		printk(KERN_WARNING"%s() hdjbulk_init_common_context failed, rc:%d",
			__FUNCTION__,ret);
	} 
	
	/* Note we must turn on talkover enable for the Mk2, and NEVER allow it to be
	 *  turned off.  Both talkover enable and talkover attenuation are in the same
	 *  maskable byte (see Mk2 firmware spec).  Talkover enable is bit 6 (0 indexed)
	 *  and talkover attenuation is in bits 0-5.  Talkover attenuation is in units of
	 *  0.5 dB.  NEVER set talkover attenuation to 0,  because it turns off talkover
	 *  attenuation, and results in device issues.  Setting talkover attenuation to 1 (-0.5 dB)
	 *  actually in the device sets talkover attenuation to 0.  This is to be exposed as an
	 *  enable/disable option.  Therefore, talkover attenuation should be allowed in the
	 *  range of 2-63 (1-32.5 dB attenuation)*/
	 /* So as mentioned above this merely turns on talkover attenuation (with 0 
	  *  attenuation), and must never be turned off */
	ret = activate_talkover(ubulk);
	if (ret!=0) {
		printk(KERN_ERR"%s() activate_talkover failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
	}
	
	ret = get_talkover_state(ubulk, &value, 1);
	if (ret != 0) 	{
		printk(KERN_ERR"%s() get_talkover_state failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->talkover_atten,value);
	}
	
	ret = set_talkover_enable(ubulk,0);
	if (ret !=0) {
			printk(KERN_ERR"%s() set_talkover_enable failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
	}

	ret = get_audio_config(ubulk, &value, 1);
	if (ret != 0) {
		printk(KERN_ERR"%s() get_audio_config failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->audio_config,value);	
	}
	
	ret = get_sample_rate(ubulk, &value, 1);
	if (ret != 0) {
		printk(KERN_ERR"%s() get_sample_rate failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->sample_rate,value);
	}

	/* this was requested */
	ret = set_mouse_state(ubulk->chip, 0);
	if (ret != 0) {
		printk(KERN_ERR"%s() set_mouse_state failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	}
	
	ret = get_crossfader_lock(ubulk,&value,1);
	if (ret != 0) {
		printk(KERN_ERR"%s() get_crossfader_lock failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->crossfader_lock,value);	
	}

	/* since there is no crossfader curve hardware get, hard code */
	value = DECKA_ATTENUATION_START_POSITION;
	value <<= 8;
	value |=CROSSFADER_ATTENUATION_MULTIPLIER_DEFAULT;
	atomic_set(&dc->crossfader_style,value);

	/* There is no jogwheel sensitivity setting for the Mk2 */

	/* There is no jogwheel lock which we can implement under Linux as of yet */

	/* There is no private serial number for the Mk2 */
	if ((ret = init_continuous_reader(ubulk))!=0) {
		printk(KERN_WARNING"%s() hdjbulk_init_continuous_reader() failed, rc:%d\n",
			__FUNCTION__,ret);
		return ret;
	}
	
	if ((ret = init_output_control_state(ubulk))!=0) {
		printk(KERN_WARNING"%s() hdjbulk_init_output_control_state() failed, rc:%d\n",
			__FUNCTION__,ret);
		return ret;
	}
	
	if ((ret = start_continuous_reader(ubulk))!=0) {
		printk(KERN_ERR"%s() start_continuous_reader failed, rc:%d\n",
				__FUNCTION__,ret);
		return ret;
	}
	clear_leds(ubulk->chip); 
	
	return ret;
}

int hdjbulk_init_dj_rmx(struct usb_hdjbulk *ubulk)
{
	int ret = 0;
	u16 value = 0;
	u32 serial_number=0;
	struct hdj_mk2_rmx_context *dc = ((struct hdj_mk2_rmx_context*)ubulk->device_context);

	ret = hdjbulk_init_common_context(ubulk,&ubulk->hdj_common);
	if (ret!=0) {
		printk(KERN_WARNING"%s() hdjbulk_init_common_context failed, rc:%d",
			__FUNCTION__,ret);
	} 

	ret = get_jogwheel_lock_status(ubulk, &value, 1, 1);
	if (ret != 0) {
		printk(KERN_ERR"%s() get_jogwheel_lock_status failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->jog_wheel_lock_status,value);
	}

	ret = get_jogwheel_sensitivity(ubulk, &value, 1, 1);
	if (ret != 0) {
		printk(KERN_ERR"%s() get_jogwheel_sensitivity failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->jog_wheel_sensitivity, value);
	}
	
	/* Note we must turn on talkover enable for the Rmx, and don't allow it to be
	 *  turned off.  Both talkover enable and talkover attenuation are in the same
	 *  maskable byte (see Rmx firmware spec).  Talkover enable is bit 7 (0 indexed)
	 *  and talkover attenuation is in bits 0-6.  Talkover attenuation is in units of
	 *  1 dB. 
	 */
	ret = activate_talkover(ubulk);
	if (ret!=0) {
		printk(KERN_ERR"%s() activate_talkover failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
	}

	ret = get_talkover_state(ubulk, &value, 1);
	if (ret != 0) 	{
		printk(KERN_ERR"%s() get_talkover_state failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->talkover_atten,value);
	}
			
	ret = set_talkover_enable(ubulk,0);
	if (ret !=0) {
			printk(KERN_ERR"%s() get_talkover_att failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
	}

	ret = get_audio_config(ubulk, &value, 1);
	if (ret != 0) {
		printk(KERN_ERR"%s() get_audio_config failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->audio_config,value);
	}
	
	ret = get_sample_rate(ubulk, &value, 1);
	if (ret != 0) {
		printk(KERN_ERR"%s() get_sample_rate failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->sample_rate,value);
	}

	ret = get_serial_number(ubulk, &serial_number);
	if (ret!=0) {
		printk(KERN_ERR"%s() get_serial_number failed, rc:%d\n",__FUNCTION__,ret);
		return ret;
	} else {
		atomic_set(&dc->serial_number,serial_number);	
	}

	if ((ret = init_continuous_reader(ubulk))!=0) {
		printk(KERN_WARNING"%s() init_continuous_reader() failed, rc:%d\n",
			__FUNCTION__,ret);
		return ret;
	}

	if ((ret = init_output_control_state(ubulk))!=0) {
		printk(KERN_WARNING"%s() hdjbulk_init_output_control_state() failed, rc:%d\n",
			__FUNCTION__,ret);
		return ret;
	}

	if ((ret = start_continuous_reader(ubulk))!=0) {
		printk(KERN_ERR"%s() start_continuous_reader failed, rc:%d\n",
				__FUNCTION__,ret);
		return ret;
	}

	clear_leds(ubulk->chip); 

	return ret;
}

int hdjbulk_init_dj_steel(struct usb_hdjbulk *ubulk)
{
	int ret = 0;
	u32 value = 0;
	struct hdj_steel_context *dc = ((struct hdj_steel_context*)ubulk->device_context);

	spin_lock_init(&dc->bulk_buffer_lock);
	init_completion(&dc->bulk_request_completion);
	init_MUTEX(&dc->bulk_request_mutex);

	if ((ret = init_continuous_reader(ubulk))!=0) {
		printk(KERN_WARNING"%s() init_continuous_reader() failed, rc:%d\n",
			__FUNCTION__,ret);
		return ret;
	}

	if ((ret = init_output_control_state(ubulk))!=0) {
		printk(KERN_WARNING"%s() hdjbulk_init_output_control_state() failed, rc:%d\n",
			__FUNCTION__,ret);
		return ret;
	}

	if ((ret = start_continuous_reader(ubulk))!=0) {
		printk(KERN_ERR"%s() start_continuous_reader failed, rc:%d\n",
				__FUNCTION__,ret);
		return ret;
	}

	if (atomic_read(&dc->device_mode) == DJ_STEEL_IN_NORMAL_MODE) {
		ret = get_firmware_version(ubulk->chip, (u16*)&value, 1);
		if (ret != 0) {
			printk(KERN_ERR"%s() get_firmware_version failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
		} 
		
		ret = get_serial_number(ubulk, &value); 
		if (ret != 0) {
			printk(KERN_ERR"%s() get_serial_number failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
		} else {
			atomic_set(&dc->serial_number,value);
		}

		ret = get_fx_state(ubulk,(u16*)&value); 
		if (ret != 0) {
			printk(KERN_ERR"%s() get_fx_state failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
		} else {
			atomic_set(&dc->fx_state,value);
		}

		ret = get_mode_shift_state(ubulk,(u8*)&value);
		if (ret != 0) {
			printk(KERN_ERR"%s() get_mode_shift_state failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
		} else {
			atomic_set(&dc->mode_shift_state,value);
		}

		/* Note: since we ask in native format, we get both the jog wheel sensitivity and
	 	 *  the jog wheel lock settings- don't worry, usermode clients get jogwheel
		 *  parameters in a common format */
		ret = get_jogwheel_sensitivity(ubulk,(u16*)&value,1, 1);
		if (ret != 0) {
			printk(KERN_ERR"%s() get_jogwheel_sensitivity failed, rc:%d\n",__FUNCTION__,ret);
			return ret;
		} else {
			/* save it away in native format- all jog wheel parameters */
			atomic_set(&dc->jog_wheel_parameters,value);
		}

		clear_leds(ubulk->chip); 
	}

	return ret;
}

int start_continuous_reader(struct usb_hdjbulk *ubulk)
{
	int i,rc, old_state;
	if (is_continuous_reader_supported(ubulk->chip)==0) {
		printk(KERN_WARNING"%s invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		return -EINVAL;
	}
	old_state = atomic_cmpxchg(&ubulk->continuous_reader_state,CR_STOPPED,CR_STARTED);
	smp_mb(); /* docs say this is required */
	if (old_state != CR_STOPPED) {
		printk(KERN_INFO"%s() operation already in progress, or bad state\n",__FUNCTION__);
		return 0;
	}
	for (i = 0; i < DJ_POLL_INPUT_URB_COUNT; i++) {
		rc = hdjbulk_input_start_ep(ubulk->bulk_in_endpoint[i]);
		if (rc!=0) {
			printk(KERN_ERR"%s() hdjbulk_input_start_ep failed, rc:%d",
				__FUNCTION__,rc);
		}
	}
	return rc;
}

int stop_continuous_reader(struct usb_hdjbulk *ubulk)
{
	int old_state;
	if (is_continuous_reader_supported(ubulk->chip)==0) {
		printk(KERN_WARNING"%s invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		return -EINVAL;
	}
	old_state = atomic_cmpxchg(&ubulk->continuous_reader_state,CR_STARTED,CR_STOPPED);
	smp_mb(); /* docs say this is required */
	if (old_state!=CR_STARTED) {
		/*printk(KERN_INFO"%s() operation already in progress, or bad state\n",__FUNCTION__);*/
		return 0;
	}
	kill_continuous_reader_urbs(ubulk,0);
	return 0;
}

/*
 * Frees an output endpoint.
 * May be called when ep hasn't been initialized completely.
 */
void hdjbulk_in_endpoint_delete(struct hdjbulk_in_endpoint* ep)
{
	if (ep->urb) {
		if (ep->urb->transfer_buffer) {
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_free_coherent(
#else
		usb_buffer_free(
#endif
					ep->ubulk->chip->dev, ep->max_transfer,
					ep->urb->transfer_buffer,
					ep->urb->transfer_dma);
		}
		usb_free_urb(ep->urb);
		ep->urb = NULL;
	}
	kfree(ep);
}

int hdjbulk_input_start_ep(struct hdjbulk_in_endpoint* ep)
{
	int ret = 0;
	if (ep) {
		struct urb* urb = ep->urb;
		urb->dev = ep->ubulk->chip->dev;
		ret = hdjbulk_submit_urb(ep->ubulk->chip, urb, GFP_KERNEL);
	}
	if (ret != 0) {
		printk(KERN_WARNING"%s() Exit, ret: 0x%x\n", __FUNCTION__,ret);
	}

	return ret;
}

void hdjbulk_input_kill_urbs(struct hdjbulk_in_endpoint* ep)
{
	if (ep) {
		if (ep->urb!=NULL) {
			usb_kill_urb(ep->urb);
		}
	}
}
