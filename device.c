/*
*
*  Copyright (c) 2008  Guillemot Corporation S.A. 
*
*  Philip Lukidis plukidis@guillemot.com
*  Alexis Rousseau-Dupuis
*
*  Partly based on usbaudio by Takashi Iwai
*  Partly based on usb-skeleton.c by Greg Kroah-Hartman 
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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/version.h>	/* For LINUX_VERSION_CODE */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <asm/uaccess.h>
#include <linux/netlink.h>
#include <net/sock.h>
#include <linux/usb.h>
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,24) )
#include <sound/driver.h>
#endif
#include <sound/core.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/rawmidi.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include "djdevioctls.h"
#include "device.h"
#include "callback.h"
#include "bulk.h"
#include "midi.h"
#include "configuration_manager.h"

/* Taken from usbaudio.c */
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int vid[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = -1 }; /* Vendor ID for this card */
static int pid[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = -1 }; /* Product ID for this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the Hercules DJ Series adapter.");

module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the Hercules DJ Series adapter.");

static DECLARE_MUTEX(register_mutex);
static struct snd_hdj_chip *usb_chip[SNDRV_CARDS];

/* reference count for the socket */
atomic_t 			netlink_ref_count = ATOMIC_INIT(0);
/* socket over which netlink notifications are sent */
struct sock			*nl_sk;
/* netlink unit used */
int					netlink_unit = NETLINK_UNIT_INVALID_VALUE;

/* table of devices that work with this driver- look for vendor specific interfaces with
 *  our VID */
static struct usb_device_id hdj_table [] = {
	{ .match_flags = (USB_DEVICE_ID_MATCH_DEVICE),
		.idVendor = (USB_HDJ_VENDOR_ID)	},
	{ .match_flags = (USB_DEVICE_ID_MATCH_INT_CLASS),
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC},
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, hdj_table);

#ifdef CONFIG_PM
static int hdj_probe(struct usb_interface *interface, const struct usb_device_id *uid);
static void hdj_disconnect(struct usb_interface *interface);
static int hdj_suspend(struct usb_interface *intf, pm_message_t message);
static int hdj_resume (struct usb_interface *intf);
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23) )
static int hdj_reset_resume(struct usb_interface *intf);
#endif
#endif


#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18) )
#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
static int hdj_pre_reset(struct usb_interface *intf);
static int hdj_post_reset(struct usb_interface *intf);
#else
static void hdj_pre_reset(struct usb_interface *intf);
static void hdj_post_reset(struct usb_interface *intf);
#endif
#endif

struct usb_driver hdj_driver = {
	.name =		"hdj_mod",
	.probe =	hdj_probe,
	.disconnect =	hdj_disconnect,
#ifdef CONFIG_PM
	.suspend =	hdj_suspend,
	.resume =	hdj_resume,
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23) )
	.reset_resume = hdj_reset_resume,
#endif
#endif
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18) )
	.pre_reset =	hdj_pre_reset,
	.post_reset =	hdj_post_reset,
#endif
	.id_table =	hdj_table,
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
	.supports_autosuspend = 0,
#endif
};

/* used for debugging only! 
void write_to_file(const char* fmt, ...)
{
	struct file *dstf;
	int orgfsuid,orgfsgid;
	mm_segment_t orgfs;
	int retval;
	char buffer[255];
	int index=0,bufsize;
	va_list vargs;
	char *dst = "/root/log";

	orgfsuid=current->fsuid;
	orgfsgid=current->fsgid;
	current->fsuid=current->fsgid=0;
	// save FS register and set FS register to kernel
	// space, needed for read and write to accept
	// buffer in kernel space.
	orgfs=get_fs();
	set_fs(KERNEL_DS);

	dstf = filp_open(dst, O_WRONLY| O_CREAT | O_APPEND, 0666);
	if(dstf!=NULL) {
		// The object must have a write method 
		if (dstf->f_op&&dstf->f_op->write) {
			memset(buffer,0,sizeof(buffer));
			va_start(vargs, fmt);
			vsnprintf(buffer,sizeof(buffer),fmt,vargs);
			va_end(vargs);
			index=0;
			bufsize=strlen(buffer);
			// Continue writing until error or everything written.
			while ((index<bufsize)&&
				((retval=dstf->f_op->write(dstf,buffer+index,bufsize-index,&dstf->f_pos))>0)
				) 
				index+=retval;
			if (index<bufsize) 
				printk("kcp: Write error %d\n",-retval);
		} else {
			printk("kcp: %s does not have a write method\n",dst);
		}
		retval=filp_close(dstf,NULL);
		if (retval) 
			printk("kcp: Error %d closing %s\n",-retval,dst);
	}
	set_fs(orgfs);
	current->fsuid=orgfsuid;
	current->fsgid=orgfsgid;
}*/

void hdj_kill_chip_urbs(struct snd_hdj_chip *chip)
{
	if (chip->ctrl_urb!=NULL) {
		usb_kill_urb(chip->ctrl_urb);
	}
}

/* MARK: PRODCHANGE */
static int bulk_intf_num_check(struct snd_hdj_chip *chip, int ifnum)
{
	if (ifnum == DJ_BULK_IFNUM && (chip->product_code==DJCONSOLE_PRODUCT_CODE ||
	    chip->product_code==DJCONSOLE2_PRODUCT_CODE ||
	    chip->product_code==DJCONSOLERMX_PRODUCT_CODE ||
	    chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE)) {
	 	return 1;   	
	}
	return 0;
}

/* MARK: PRODCHANGE */
static int midi_intf_num_full_controller_num_check(struct snd_hdj_chip *chip, int ifnum)
{
	if ((ifnum == DJ_MIDI_IF_NUM && (chip->product_code==DJCONSOLE_PRODUCT_CODE ||
	    chip->product_code==DJCONSOLE2_PRODUCT_CODE ||
	    chip->product_code==DJCONSOLERMX_PRODUCT_CODE)) ||
		(ifnum == DJ_MIDI_STEEL_IF_NUM &&
			chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE)) {
		return 1;
	}
	return 0;
}

/* MARK: PRODCHANGE */
static int midimp3_intf_num_full_controller_num_check(struct snd_hdj_chip *chip, int ifnum)
{
	if (ifnum==DJ_MP3_IF_NUM && chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		return 1;	
	}
	return 0;
}

static void proc_fw_version_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	int chip_index = (int)(unsigned long)entry->private_data;
	u16 fw_version=-1;
	int rc;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		/* force a hw read- for those fw update related checks */
		rc = get_firmware_version(chip,&fw_version,1);
		snd_iprintf(buffer, "%hx\n",fw_version);
		if (rc!=0) {
			printk(KERN_WARNING"%s() get_firmware_version() failed, rc:%d\n",
					__FUNCTION__,rc);	
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_location_id_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct snd_hdjmidi* umidi;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		umidi = midi_from_chip(chip);
		if (umidi!=NULL) {
			snd_iprintf(buffer, "%s\n",&umidi->chip->usb_device_path[0]);
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_driver_version_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "%u\n",driver_version);
}

static void proc_netlink_unit_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "%d\n",netlink_unit);
}

static void proc_drivername_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		if (chip->card->driver[0] != '\0') {
			snd_iprintf(buffer, "%s\n",chip->card->driver);
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_shortname_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	int chip_index = (int)(unsigned long)entry->private_data;
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		if (chip->card->shortname[0] != '\0') {
			snd_iprintf(buffer, "%s\n",chip->card->shortname);
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_longname_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	int chip_index = (int)(unsigned long)entry->private_data;
	chip = inc_chip_ref_count(chip_index);
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		if (chip->card->longname[0] != '\0') {
			snd_iprintf(buffer, "%s\n",chip->card->longname);
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_product_code_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		snd_iprintf(buffer, "%u\n",chip->product_code);
		dec_chip_ref_count(chip_index);
	}
}

static void proc_jog_lock_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	char line[64];
	u16 jog_lock;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hx", &jog_lock)) != 1) {
                	break;	
                }    	   
				if ((rc=set_jogwheel_lock_status(ubulk, jog_lock))!=0) {
					printk(KERN_WARNING"%s() set_jogwheel_lock_status failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_jog_lock_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u16 jog_lock;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			if ((rc = get_jogwheel_lock_status(ubulk,&jog_lock,1,0))==0) {
				snd_iprintf(buffer, "%hu\n",jog_lock);
			} else {
				printk(KERN_WARNING"%s() get_jogwheel_lock_status() failed, rc:%d\n",
							__FUNCTION__,rc);	
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_jog_sens_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	char line[64];
	u16 jog_sens;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hx", &jog_sens)) != 1) {
                	break;	
                }    	   
				if ((rc=set_jogwheel_sensitivity(ubulk, jog_sens))!=0) {
					printk(KERN_WARNING"%s() set_jogwheel_sensitivity failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_jog_sens_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u16 jog_sens;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			if ((rc = get_jogwheel_sensitivity(ubulk,&jog_sens,1,0))==0) {
				snd_iprintf(buffer, "%hu\n",jog_sens);
			} else {
				printk(KERN_WARNING"%s() get_jogwheel_sensitivity() failed, rc:%d\n",
							__FUNCTION__,rc);	
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_talkover_atten_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	char line[64];
	u16 talkover_atten;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hx", &talkover_atten)) != 1) {
                	break;	
                }    	   
				if ((rc=set_talkover_att(ubulk, talkover_atten))!=0) {
					printk(KERN_WARNING"%s() set_talkover_att failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_talkover_atten_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u16 talkover_atten;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			if ((rc = get_talkover_att(ubulk,&talkover_atten,1))==0) {
				snd_iprintf(buffer, "%hx\n",talkover_atten);
			} else {
				printk(KERN_WARNING"%s() get_talkover_att() failed, rc:%d\n",
							__FUNCTION__,rc);	
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_talkover_enable_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	char line[64];
	u8 talkover_enable;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hhu", &talkover_enable)) != 1) {
                	break;	
                }    	   
				if ((rc=set_talkover_enable(ubulk, talkover_enable))!=0) {
					printk(KERN_WARNING"%s() set_talkover_enable failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_talkover_enable_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u8 talkover_enable;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			if ((rc = get_talkover_enable(ubulk,&talkover_enable))==0) {
				snd_iprintf(buffer, "%hu\n",talkover_enable);
			} else {
				printk(KERN_WARNING"%s() get_talkover_enable() failed, rc:%d\n",
							__FUNCTION__,rc);	
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_djconfig_word_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	char line[64];
	u32 djconfig;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		while (!snd_info_get_line(buffer, line, sizeof(line))) {
			if ((num=sscanf(line, "%x", &djconfig)) != 1) {
				break;	
			}    	   
			if ((rc=set_djconsole_device_config(chip->index, djconfig, 0))!=0) {
				printk(KERN_WARNING"%s() set_djconsole_device_config failed, rc:%d\n",
							__FUNCTION__,rc);
			}
			break;
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_djconfig_word_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u16 djconfig;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		if ((rc = get_djconsole_device_config(chip->index,&djconfig,1))==0) {
			snd_iprintf(buffer, "%hx\n",djconfig);
		} else {
			printk(KERN_WARNING"%s() get_djconsole_device_config() failed, rc:%d\n",
						__FUNCTION__,rc);	
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_audio_config_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	char line[64];
	u16 audio_config;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hx", &audio_config)) != 1) {
                	break;	
                }    	   
				if ((rc=set_audio_config(ubulk, audio_config))!=0) {
					printk(KERN_WARNING"%s() set_audio_config failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_audio_config_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u16 audio_config;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			if ((rc = get_audio_config(ubulk,&audio_config,1))==0) {
				snd_iprintf(buffer, "%hx\n",audio_config);
			} else {
				printk(KERN_WARNING"%s() get_audio_config() failed, rc:%d\n",
							__FUNCTION__,rc);	
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_mouse_enable_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	char line[64];
	u8 mouse_enable;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		while (!snd_info_get_line(buffer, line, sizeof(line))) {
        	if ((num=sscanf(line, "%hhu", &mouse_enable)) != 1) {
               	break;	
            }    	   
			if ((rc=set_mouse_state(chip, mouse_enable))!=0) {
				printk(KERN_WARNING"%s() set_mouse_state failed, rc:%d\n",
						__FUNCTION__,rc);
			}
			break;
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_mouse_enable_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u16 mouse_state;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		if ((rc = get_mouse_state(chip,&mouse_state))==0) {
			snd_iprintf(buffer, "%hu\n",mouse_state);
		} else {
			printk(KERN_WARNING"%s() get_mouse_state() failed, rc:%d\n",
						__FUNCTION__,rc);	
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_sample_rate_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	char line[64];
	u16 sample_rate;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hx", &sample_rate)) != 1) {
                	break;	
                }    	   
				if ((rc=set_sample_rate(ubulk, sample_rate))!=0) {
					printk(KERN_WARNING"%s() set_sample_rate failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_sample_rate_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u16 sample_rate;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			if ((rc = get_sample_rate(ubulk,&sample_rate,1))==0) {
				snd_iprintf(buffer, "%hu\n",sample_rate);
			} else {
				printk(KERN_WARNING"%s() get_sample_rate() failed, rc:%d\n",
							__FUNCTION__,rc);	
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_xfader_lock_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	char line[64];
	u16 xfader_lock;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hx", &xfader_lock)) != 1) {
                	break;	
                }    	   
				if ((rc=set_crossfader_lock(ubulk, xfader_lock))!=0) {
					printk(KERN_WARNING"%s() set_crossfader_lock failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_xfader_lock_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u16 xfader_lock;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			if ((rc = get_crossfader_lock(ubulk,&xfader_lock,1))==0) {
				snd_iprintf(buffer, "%hx\n",xfader_lock);
			} else {
				printk(KERN_WARNING"%s() get_crossfader_lock() failed, rc:%d\n",
							__FUNCTION__,rc);	
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_xfader_curve_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	char line[64];
	u16 xfader_curve;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hx", &xfader_curve)) != 1) {
                	break;	
                }    	   
				if ((rc=set_crossfader_style(ubulk, xfader_curve))!=0) {
					printk(KERN_WARNING"%s() set_crossfader_style failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_xfader_curve_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u16 xfader_curve;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			if ((rc = get_crossfader_style(ubulk,&xfader_curve))==0) {
				snd_iprintf(buffer, "%hx\n",xfader_curve);
			} else {
				printk(KERN_WARNING"%s() get_crossfader_style() failed, rc:%d\n",
							__FUNCTION__,rc);	
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_shift_mode_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	char line[64];
	u8 shift_mode;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hhu", &shift_mode)) != 1) {
                	break;	
                }    	   
				if ((rc=set_mode_shift_state(ubulk, shift_mode))!=0) {
					printk(KERN_WARNING"%s() set_mode_shift_state failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_shift_mode_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u8 shift_mode;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			if ((rc = get_mode_shift_state(ubulk,&shift_mode))==0) {
				snd_iprintf(buffer, "%hhu\n",shift_mode);
			} else {
				printk(KERN_WARNING"%s() get_crossfader_style() failed, rc:%d\n",
							__FUNCTION__,rc);	
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_fx_state_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	char line[64];
	u16 fx_state;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hx", &fx_state)) != 1) {
                	break;	
                }    	   
				if ((rc=set_fx_state(ubulk, fx_state))!=0) {
					printk(KERN_WARNING"%s() set_fx_state failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_fx_state_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk* ubulk;
	int chip_index = (int)(unsigned long)entry->private_data;
	int rc;
	u16 fx_state;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		ubulk = bulk_from_chip(chip);
		if (ubulk!=NULL) {
			if ((rc = get_fx_state(ubulk,&fx_state))==0) {
				snd_iprintf(buffer, "%hx\n",fx_state);
			} else {
				printk(KERN_WARNING"%s() get_fx_state() failed, rc:%d\n",
							__FUNCTION__,rc);	
			}
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_midi_channel_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct snd_hdjmidi* umidi;
	char line[64];
	u16 channel_to_set;
	int rc, num;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		umidi = midi_from_chip(chip);
		if (umidi!=NULL) {
			while (!snd_info_get_line(buffer, line, sizeof(line))) {
        		if ((num=sscanf(line, "%hx", &channel_to_set)) != 1) {
                	break;	
                }    	   
				if ((rc=set_midi_channel(chip, (u16*)&channel_to_set))!=0) {
					printk(KERN_WARNING"%s() set_midi_channel failed, rc:%d\n",
							__FUNCTION__,rc);
				}
				break;
			}
		}
		dec_chip_ref_count(chip_index);
	}
}


static void proc_midi_channel_read(struct snd_info_entry *entry, 
									struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	struct snd_hdjmidi* umidi;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		umidi = midi_from_chip(chip);
		if (umidi!=NULL) {
			snd_iprintf(buffer, "%x\n",atomic_read(&umidi->channel));
		}
		dec_chip_ref_count(chip_index);
	}
}

static void proc_chip_usbid_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		snd_iprintf(buffer, "%04x:%04x\n", 
			    USB_ID_VENDOR(chip->usb_id),
			    USB_ID_PRODUCT(chip->usb_id));
		dec_chip_ref_count(chip_index);
	}
}

/* Common proc files to show the usb device info. */
static void proc_chip_usbbus_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_hdj_chip *chip;
	int chip_index = (int)(unsigned long)entry->private_data;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip!=NULL) {
		snd_iprintf(buffer, "%03d/%03d\n", chip->dev->bus->busnum, chip->dev->devnum);
		dec_chip_ref_count(chip_index);
	}
}

/* for scripts or debugging */
static void snd_hdj_chip_create_proc(struct snd_hdj_chip *chip)
{
	struct snd_info_entry *entry;
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18) )
	if (! snd_card_proc_new(chip->card, "usbbus", &entry))
		snd_info_set_text_ops(entry, (void*)(unsigned long)chip->index, proc_chip_usbbus_read);
	if (! snd_card_proc_new(chip->card, "usbid", &entry))
		snd_info_set_text_ops(entry, (void*)(unsigned long)chip->index, proc_chip_usbid_read);
	
	if (! snd_card_proc_new(chip->card, "driver_name", &entry)) 
		snd_info_set_text_ops(entry, (void*)(unsigned long)chip->index, proc_drivername_read);
		
	if (! snd_card_proc_new(chip->card, "short_name", &entry)) 
		snd_info_set_text_ops(entry, (void*)(unsigned long)chip->index, proc_shortname_read);
		
	if (! snd_card_proc_new(chip->card, "long_name", &entry)) 
		snd_info_set_text_ops(entry, (void*)(unsigned long)chip->index, proc_longname_read);
		
	if ( chip->caps.midi==1 &&
		snd_card_proc_new(chip->card, "midi_channel", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_midi_channel_read);
		entry->c.text.write = proc_midi_channel_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (! snd_card_proc_new(chip->card, "fw_version", &entry)) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_fw_version_read);
		entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	if (! snd_card_proc_new(chip->card, "location_id", &entry)) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_location_id_read);
		entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	if (! snd_card_proc_new(chip->card, "driver_version", &entry)) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_driver_version_read);
		entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	if (! snd_card_proc_new(chip->card, "product_code", &entry)) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_product_code_read);
		entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	if (! snd_card_proc_new(chip->card, "netlink_unit", &entry)) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_netlink_unit_read);
		entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	
	if (chip->caps.jog_locking==1 &&
		snd_card_proc_new(chip->card, "jog_lock", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_jog_lock_read);
		entry->c.text.write = proc_jog_lock_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.jog_sensitivity==1 &&
		snd_card_proc_new(chip->card, "jog_sensitivity", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_jog_sens_read);
		entry->c.text.write = proc_jog_sens_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.talkover_atten==1 &&
		snd_card_proc_new(chip->card, "talkover_atten", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_talkover_atten_read);
		entry->c.text.write = proc_talkover_atten_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.talkover_atten==1 &&
		snd_card_proc_new(chip->card, "talkover_enable", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_talkover_enable_read);
		entry->c.text.write = proc_talkover_enable_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.djconfig_word==1 &&
		snd_card_proc_new(chip->card, "djconfig_word", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_djconfig_word_read);
		entry->c.text.write = proc_djconfig_word_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.audio_config==1 &&
		snd_card_proc_new(chip->card, "audio_config", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_audio_config_read);
		entry->c.text.write = proc_audio_config_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.mouse==1 &&
		snd_card_proc_new(chip->card, "mouse_enable", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_mouse_enable_read);
		entry->c.text.write = proc_mouse_enable_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.sample_rate_readable==1 &&
		snd_card_proc_new(chip->card, "sample_rate", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_sample_rate_read);
		if (chip->caps.sample_rate_writable==1) {
			entry->c.text.write = proc_sample_rate_write;
			entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
		} else {
			entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
		}
	}
	if (chip->caps.xfader_lock==1 &&
		snd_card_proc_new(chip->card, "xfader_lock", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_xfader_lock_read);
		entry->c.text.write = proc_xfader_lock_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.xfader_curve==1 &&
		snd_card_proc_new(chip->card, "xfader_curve", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_xfader_curve_read);
		entry->c.text.write = proc_xfader_curve_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.shift_mode==1 &&
		snd_card_proc_new(chip->card, "shift_mode", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_shift_mode_read);
		entry->c.text.write = proc_shift_mode_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.shift_mode==1 &&
		snd_card_proc_new(chip->card, "fx_state", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						proc_fx_state_read);
		entry->c.text.write = proc_fx_state_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
#else
	if (! snd_card_proc_new(chip->card, "usbbus", &entry))
		snd_info_set_text_ops(entry, (void*)(unsigned long)chip->index, 1024, proc_chip_usbbus_read);
	if (! snd_card_proc_new(chip->card, "usbid", &entry))
		snd_info_set_text_ops(entry, (void*)(unsigned long)chip->index, 1024, proc_chip_usbid_read);
		
	if ( chip->caps.midi==1 &&
		snd_card_proc_new(chip->card, "midi_channel", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_midi_channel_read);
		entry->c.text.write = proc_midi_channel_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (! snd_card_proc_new(chip->card, "fw_version", &entry)) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_fw_version_read);
		entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	if (! snd_card_proc_new(chip->card, "location_id", &entry)) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_location_id_read);
		entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	if (! snd_card_proc_new(chip->card, "driver_version", &entry)) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_driver_version_read);
		entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	if (! snd_card_proc_new(chip->card, "product_code", &entry)) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_product_code_read);
		entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	if (! snd_card_proc_new(chip->card, "netlink_unit", &entry)) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_netlink_unit_read);
		entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	
	if (chip->caps.jog_locking==1 &&
		snd_card_proc_new(chip->card, "jog_lock", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_jog_lock_read);
		entry->c.text.write = proc_jog_lock_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.jog_sensitivity==1 &&
		snd_card_proc_new(chip->card, "jog_sensitivity", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_jog_sens_read);
		entry->c.text.write = proc_jog_sens_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.talkover_atten==1 &&
		snd_card_proc_new(chip->card, "talkover_atten", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_talkover_atten_read);
		entry->c.text.write = proc_talkover_atten_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.talkover_atten==1 &&
		snd_card_proc_new(chip->card, "talkover_enable", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_talkover_enable_read);
		entry->c.text.write = proc_talkover_enable_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.djconfig_word==1 &&
		snd_card_proc_new(chip->card, "djconfig_word", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_djconfig_word_read);
		entry->c.text.write = proc_djconfig_word_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.audio_config==1 &&
		snd_card_proc_new(chip->card, "audio_config", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_audio_config_read);
		entry->c.text.write = proc_audio_config_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.mouse==1 &&
		snd_card_proc_new(chip->card, "mouse_enable", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_mouse_enable_read);
		entry->c.text.write = proc_mouse_enable_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.sample_rate_readable==1 &&
		snd_card_proc_new(chip->card, "sample_rate", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_sample_rate_read);
		if (chip->caps.sample_rate_writable==1) {
			entry->c.text.write = proc_sample_rate_write;
			entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
		} else {
			entry->mode |= S_IRUSR | S_IRGRP | S_IROTH;
		}
	}
	if (chip->caps.xfader_lock==1 &&
		snd_card_proc_new(chip->card, "xfader_lock", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_xfader_lock_read);
		entry->c.text.write = proc_xfader_lock_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.xfader_curve==1 &&
		snd_card_proc_new(chip->card, "xfader_curve", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_xfader_curve_read);
		entry->c.text.write = proc_xfader_curve_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.shift_mode==1 &&
		snd_card_proc_new(chip->card, "shift_mode", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_shift_mode_read);
		entry->c.text.write = proc_shift_mode_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
	if (chip->caps.shift_mode==1 &&
		snd_card_proc_new(chip->card, "fx_state", &entry)==0) {
		snd_info_set_text_ops(entry, 
						(void*)(unsigned long)chip->index, 
						1024,
						proc_fx_state_read);
		entry->c.text.write = proc_fx_state_write;
        entry->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	}
#endif
}

/*
 * Currently we only manage 1 interface, so this is much simplified from USBAUDIO.  If this changes in the
 *  future, then this code must be updated to reflect this.
 */
static int snd_hdj_create_streams(struct snd_hdj_chip *chip, int ifnum)
{
	struct usb_interface* intf = NULL, *original_intf = NULL;

	if (midi_intf_num_full_controller_num_check(chip,ifnum)==1) {
		/* this will be referenced with a call to usb_get_intf */
		intf = usb_ifnum_to_if(chip->dev,ifnum);
		if(intf==NULL) {
			printk(KERN_WARNING"%s(): usb_ifnum_to_if(): returned NULL, bailing\n",
					__FUNCTION__);
			return -EINVAL;
		}
		/* We are ready to create the MIDI interface */
		return snd_hdj_create_midi_interface(chip, intf, NULL); 
	} else if (bulk_intf_num_check(chip,ifnum)==1) {
		/* this will be referenced with a call to usb_get_intf */
		intf = usb_ifnum_to_if(chip->dev,ifnum);
		if(intf==NULL) {
			snd_printk(KERN_WARNING"%s(): usb_ifnum_to_if(): returned NULL, bailing\n",
					__FUNCTION__);
			return -EINVAL;
		}
		/* We are ready to create the Bulk interface */
		return hdj_create_bulk_interface(chip, intf);
	} else if (midimp3_intf_num_full_controller_num_check(chip,ifnum)==1) {
		/* reference the original intf */
		original_intf = usb_ifnum_to_if(chip->dev,ifnum);
		if (original_intf==NULL) {
			snd_printk(KERN_WARNING"%s(): usb_ifnum_to_if(): returned NULL (original mp3 intf), bailing\n",
					__FUNCTION__);
			return -EINVAL;
		}
		
		/* we need to refer to interface 0 for the control data */
		ifnum = DJ_MP3_HID_IF_NUM;
		/* this will be referenced with a call to usb_get_intf */
		intf = usb_ifnum_to_if(chip->dev,ifnum);
		if (intf==NULL) {
			snd_printk(KERN_WARNING"snd_hdj_create_streams(): usb_ifnum_to_if(): returned NULL, bailing\n");
			return -EINVAL;
		}
		/* We are ready to create the MIDI interface */
		return snd_hdj_create_midi_interface(chip, intf, original_intf); 
	}
	else if ((ifnum==DJ_ASIO_DJ1_IF_NUM && chip->product_code==DJCONSOLE_PRODUCT_CODE)	||
		(ifnum==DJ_ASIO_MK2_RMX_IF_NUM && chip->product_code==DJCONSOLE2_PRODUCT_CODE)	||
		(ifnum==DJ_ASIO_MK2_RMX_IF_NUM && chip->product_code==DJCONSOLERMX_PRODUCT_CODE)) {
		/* The ASIO interface isn't used- but return success so that the user does not
		 *   see an error in the log */
		return 0;
	}
	else{
		snd_printk(KERN_WARNING"snd_hdj_create_streams(): nothing found for interface: %d and product: %d, bailing\n", ifnum, chip->product_code);
	}

	return -EINVAL;
}

/* MARK: PRODCHANGE */
/* sets up the capabilities of our DJ device */
static void snd_hdj_enter_caps(struct snd_hdj_chip *chip)
{
	memset(&chip->caps,0,sizeof(chip->caps));
	if (chip->product_code==DJCONSOLE_PRODUCT_CODE) {
		chip->caps.port_mode = 1;
		chip->caps.num_out_ports = 1;
		chip->caps.num_in_ports = 1;
		chip->caps.talkover_atten = 1;
		chip->caps.djconfig_word = 1;
		chip->caps.mouse = 1;
		chip->caps.sample_rate_readable = 1;
		chip->caps.sample_rate_writable = 1;
		chip->caps.midi = 1;
		chip->caps.leds_hid_controlled = 1;
		chip->caps.leds_report_len = DJC_SET_REPORT_LEN;
		chip->caps.leds_report_id = DJC_SET_REPORT_ID;
		
		chip->caps.audio_board_present = 1;
		chip->caps.audio_board_upgradeable = 1;
		chip->caps.audio_board_upgradeable_full = 1;
		chip->caps.audio_board_upgradeable_partial = 0;
		chip->caps.audio_board_version = 0; /* to be filled by bulk later */ 
		
		chip->caps.controller_board_present = 1;
		chip->caps.controller_board_upgradeable = 0;
		chip->caps.controller_board_version = 0; /* to be filled by bulk later */ 
		chip->caps.controller_type = CONTROLLER_TYPE_PSOC_26; 
	} else if (chip->product_code==DJCONSOLE2_PRODUCT_CODE) {
		chip->caps.num_out_ports = 1;
		chip->caps.num_in_ports = 1;
		chip->caps.talkover_atten = 1;
		chip->caps.audio_config = 1;
		chip->caps.mouse = 1;
		chip->caps.xfader_lock = 1;
		chip->caps.xfader_curve = 1;
		chip->caps.midi = 1;
		chip->caps.sample_rate_readable = 1;
		chip->caps.leds_hid_controlled = 1;
		chip->caps.leds_report_len = DJMK2_SET_REPORT_LEN;
		chip->caps.leds_report_id = DJMK2_SET_REPORT_ID;
		
		chip->caps.audio_board_present = 1;
		chip->caps.audio_board_upgradeable = 1;
		chip->caps.audio_board_upgradeable_full = 1;
		chip->caps.audio_board_upgradeable_partial = 1;
		chip->caps.audio_board_version = 0; /* to be filled by bulk later */
		chip->caps.audio_board_in_boot_mode = 0; /* to be filled by bulk later */ 
		
		chip->caps.controller_board_present = 1;
		chip->caps.controller_board_upgradeable = 0; /* to be filled by bulk later */ 
		chip->caps.controller_board_version = 0; /* to be filled by bulk later */ 
		chip->caps.controller_type = CONTROLLER_TYPE_UNKNOWN; /* to be filled by bulk later */ 
		chip->caps.controller_board_in_boot_mode = 0; /* to be filled by bulk later */ 
		
	} else if (chip->product_code==DJCONSOLERMX_PRODUCT_CODE) {
		chip->caps.non_volatile_channel = 1;
		chip->caps.num_out_ports = 1;
		chip->caps.num_in_ports = 1;
		chip->caps.jog_locking = 1;
		chip->caps.jog_sensitivity = 1;
		chip->caps.talkover_atten = 1;
		chip->caps.audio_config = 1;
		chip->caps.midi = 1;
		chip->caps.sample_rate_readable = 1;
		chip->caps.leds_hid_controlled = 1;
		chip->caps.leds_report_len = DJRMX_SET_REPORT_LEN;
		chip->caps.leds_report_id = DJRMX_SET_REPORT_ID;
		
		chip->caps.audio_board_present = 1;
		chip->caps.audio_board_upgradeable = 1;
		chip->caps.audio_board_upgradeable_full = 1;
		chip->caps.audio_board_upgradeable_partial = 1;
		chip->caps.audio_board_version = 0; /* to be filled by bulk later */
		chip->caps.audio_board_in_boot_mode = 0; /* to be filled by bulk later */ 
		
		chip->caps.controller_board_present = 1;
		chip->caps.controller_board_upgradeable = 0; /* to be filled by bulk later */ 
		chip->caps.controller_board_version = 0; /* to be filled by bulk later */ 
		chip->caps.controller_type = CONTROLLER_TYPE_UNKNOWN; /* to be filled by bulk later */ 
		chip->caps.controller_board_in_boot_mode = 0; /* to be filled by bulk later */
	} else if (chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		chip->caps.non_volatile_channel = 1;
		chip->caps.num_out_ports = 1;
		chip->caps.num_in_ports = 1;
		chip->caps.jog_locking = 1;
		chip->caps.jog_sensitivity = 1;
		chip->caps.shift_mode = 1;
		chip->caps.midi = 1;
		chip->caps.leds_bulk_controlled = 1;
		chip->caps.leds_report_len = DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE;
		chip->caps.leds_report_id = DJ_STEEL_STANDARD_SET_LED_REPORT;
		
		chip->caps.controller_board_present = 1;
		chip->caps.controller_board_upgradeable = 1; 
		chip->caps.controller_board_version = 0; /* to be filled by bulk later */ 
		chip->caps.controller_type = CONTROLLER_TYPE_UNKNOWN; /* to be filled by bulk later */ 
	} else if (chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		chip->caps.hid_support_only = 1;
		chip->caps.hid_interface_to_poll = DJ_MP3_HID_IF_NUM;
		chip->caps.num_out_ports = 1;
		chip->caps.num_in_ports = 1;
		chip->caps.midi = 1;
		chip->caps.leds_hid_controlled = 1;
		chip->caps.leds_report_len = DJ_MP3_HID_OUTPUT_REPORT_LEN;
		chip->caps.leds_report_id = DJ_MP3_HID_REPORT_ID;
		
		chip->caps.controller_board_present = 1;
		chip->caps.controller_board_upgradeable = 0;  
		chip->caps.controller_board_version = le16_to_cpu(chip->dev->descriptor.bcdDevice); 
		if (chip->usb_id==USB_ID(USB_HDJ_VENDOR_ID,   DJ_CONTROL_MP3W_PID)) {
			chip->caps.controller_type = CONTROLLER_TYPE_WELTREND; 
		} else {
			chip->caps.controller_type = CONTROLLER_TYPE_PSOC_26;
		}
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,chip->product_code);
	}
}

/* Free the chip instance. */
static int snd_hdj_chip_free(struct snd_hdj_chip *chip)
{	
	struct list_head *p;
	struct list_head *next;
	struct snd_hdjmidi* umidi;
	struct usb_hdjbulk* ubulk;
	
	/* frees resources of all MIDI interfaces attached to this chip */
	if (!list_empty(&chip->midi_list)) {
		list_for_each_safe(p,next,&chip->midi_list) {
			umidi = list_entry(p, struct snd_hdjmidi, list);
			list_del(p);
			snd_hdj_free(umidi);
		}
	}

	/* frees resources of all bulk interfaces attached to this chip */
	if (!list_empty(&chip->bulk_list)) {
		list_for_each_safe(p,next,&chip->bulk_list) {
			ubulk = list_entry(p, struct usb_hdjbulk, list);
			list_del(p);
			/* decrement our bulk usage count */
			kref_put(&ubulk->kref, hdj_delete);
		}
	}

	hdj_kill_chip_urbs(chip);

	if(chip->ctrl_req_buffer != NULL)
	{
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_free_coherent(
#else
		usb_buffer_free(
#endif
				chip->dev,
				chip->ctrl_urb->transfer_buffer_length,
				chip->ctrl_req_buffer,
				chip->ctrl_urb->transfer_dma);
		chip->ctrl_req_buffer = NULL;
	}
	
	if(chip->ctrl_urb != NULL)
	{
		usb_free_urb(chip->ctrl_urb);
		chip->ctrl_urb = NULL;
	}

	
	if(chip->ctl_req != NULL)
	{
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_free_coherent(
#else
		usb_buffer_free(
#endif
			chip->dev,
			sizeof(*(chip->ctl_req)),
			chip->ctl_req,
			chip->ctl_req_dma);
		chip->ctl_req = NULL;
	}

	/* Since we called usb_get_dev when creating the chip, we must
	 *  call usb_put_dev here.  This was done because the device might be accessed briefly if
	 *  MIDI being open prevents the chip destructor from being called right away. 
	 *  We associate the chip destructor with snd_card_free_when_closed
	 */
	usb_put_dev(chip->dev);
	kfree(chip);
	
	return 0; 
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
static int snd_hdj_chip_dev_free(struct snd_device *device)
#else
static int snd_hdj_chip_dev_free(snd_device_t *device)
#endif
{
	struct snd_hdj_chip *chip = device->device_data;
	return snd_hdj_chip_free(chip);
}

/* MARK: PRODCHANGE */
static void create_id(char *card_id, u32 id_buf_len, int product_code, int card_num)
{
	if (product_code==DJCONSOLE_PRODUCT_CODE) {
		snprintf(card_id,id_buf_len-1,"herc_djc%d",card_num);
	} else if (product_code==DJCONSOLE2_PRODUCT_CODE) {
		snprintf(card_id,id_buf_len-1,"herc_djmk2%d",card_num);
	} else if (product_code==DJCONSOLERMX_PRODUCT_CODE) {
		snprintf(card_id,id_buf_len-1,"herc_djrmx%d",card_num);
	} else if (product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		snprintf(card_id,id_buf_len-1,"herc_djst%d",card_num);
	} else if (product_code==DJCONTROLLER_PRODUCT_CODE) {
		snprintf(card_id,id_buf_len-1,"herc_djmp3%d",card_num);
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,product_code);
	}
}

/* Create a chip instance and set its names. *
 *  NOTE: we must clean up here if something goes wrong, because the chip 
 *         cleanup routine will not fire */
static int snd_hdj_chip_create(struct usb_device *dev, 
				int idx,
				int product_code,
				struct snd_hdj_chip **rchip)
{
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
	struct snd_card *card = NULL;
#else
	snd_card_t *card = NULL;
#endif
	struct snd_hdj_chip *chip = NULL;
	int err=-ENOMEM, len;
	char component[36];
	char card_id[36];
	char shortname[50];
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
	static struct snd_device_ops ops = {
		.dev_free =	snd_hdj_chip_dev_free,
	};
#else
	static snd_device_ops_t ops = {
		.dev_free =	snd_hdj_chip_dev_free,
	};
#endif

	*rchip = NULL;
	
	/* if an id was supplied from the command line, put it in, otherwise hard coded one */
	memset(card_id,0,sizeof(card_id));
	if (id[idx]==NULL) {
		/* create a custom id */
		create_id(card_id, sizeof(card_id), product_code, idx);
	} else {
		/* let the kernel option override custom id */
		strncpy(card_id,id[idx],sizeof(card_id)-1);
	}
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30) )
    err = snd_card_create(index[idx], card_id/*id[idx]*/, THIS_MODULE, 0, &card );
    if (err) {
        snd_printk(KERN_WARNING "snd_hdj_chip_create(): cannot create card instance %d\n", idx);
        return err;
    }
#else
	card = snd_card_new(index[idx], card_id/*id[idx]*/, THIS_MODULE, 0);
	if (card == NULL) {
		snd_printk(KERN_WARNING "snd_hdj_chip_create(): cannot create card instance %d\n", idx);
		return -ENOMEM;
	}
#endif

	/* save the index, so people who have the card can reference the chip */
	card->private_data = (void*)(unsigned long)idx;

	chip = kmalloc(sizeof(*chip), GFP_KERNEL);
	if (! chip) {
		snd_printk(KERN_WARNING "snd_hdj_chip_create(): cannot allocate memory for context %d\n", idx);
		return -ENOMEM;
	}
	
	memset(chip,0,sizeof(*chip));
	chip->index = idx;
	chip->ref_count = 0;
	chip->dev = dev;
	chip->card = card;
	chip->product_code = product_code;

	init_MUTEX(&chip->vendor_request_mutex);

	/* initialise the atomic variables */
	atomic_set(&chip->locked_io, 0);
	atomic_set(&chip->vendor_command_in_progress, 0);
	atomic_set(&chip->shutdown, 0);
	atomic_set(&chip->no_urb_submission, 0);
	atomic_set(&chip->num_suspended_intf, 0);
	atomic_set(&chip->next_midi_device, 0);
	atomic_set(&chip->next_bulk_device, 0);
	
	INIT_LIST_HEAD(&chip->midi_list);
	INIT_LIST_HEAD(&chip->bulk_list);
	chip->usb_id = USB_ID(le16_to_cpu(dev->descriptor.idVendor),
			      le16_to_cpu(dev->descriptor.idProduct));
	init_MUTEX(&chip->netlink_list_mutex);
	INIT_LIST_HEAD(&chip->netlink_registered_processes);
	
	/* fill in DJ capabilities for this device */
	snd_hdj_enter_caps(chip);
	
	/* Set this right away because caller might have to cleanup.  Caller cleans up
	 *  because the registration mutex is held now */
	*rchip = chip;
	
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		printk(KERN_WARNING"%s() snd_device_new() failed, rc:%d\n",__FUNCTION__,err);
		return err;
	}
	
	strcpy(card->driver, "hdj_mod");
	sprintf(component, "USB%04x:%04x",
		USB_ID_VENDOR(chip->usb_id), USB_ID_PRODUCT(chip->usb_id));
	snd_component_add(card, component);

	/* init chip completion */
	init_completion(&chip->ctrl_completion);

	/* allocate the urb */
	chip->ctrl_urb = usb_alloc_urb(0,GFP_KERNEL);
	if(chip->ctrl_urb==NULL) {
		printk(KERN_ERR"snd_hdj_chip_create(): usb_alloc_urb() returned NULL, bailing\n");
		return err;
	}

	/* allocate memory for setup packet for our control requests */
	chip->ctl_req =
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_alloc_coherent(
#else
		usb_buffer_alloc(
#endif
					 chip->dev, 
					 sizeof(*(chip->ctl_req)),
					 GFP_KERNEL, 
					 &chip->ctl_req_dma);
	if(chip->ctl_req == NULL) {
		printk(KERN_WARNING"snd_hdj_chip_create(): usb_buffer_alloc() failed for setup DMA\n");
		return err;
	}
	
	/* get the control pipes */
	chip->ctrl_out_pipe = usb_sndctrlpipe(chip->dev, 0);
	chip->ctrl_in_pipe = usb_rcvctrlpipe(chip->dev, 0);

	chip->ctrl_req_buffer_len =  sizeof(u16);
	chip->ctrl_urb->transfer_buffer_length = chip->ctrl_req_buffer_len;
	chip->ctrl_req_buffer = 
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) )
		usb_alloc_coherent(
#else
		usb_buffer_alloc(
#endif
						 chip->dev, 
						 chip->ctrl_urb->transfer_buffer_length,
						 GFP_KERNEL, 
						 &chip->ctrl_urb->transfer_dma);
	if (chip->ctrl_req_buffer == NULL) {
		printk(KERN_WARNING"snd_hdj_chip_create(): usb_buffer_alloc() failed\n");
		return err;
	}

	/* Retrieve the device string as shortname. */
 	if (!dev->descriptor.iProduct ||
	    	usb_string(dev, dev->descriptor.iProduct,
      			card->shortname, sizeof(card->shortname)) <= 0) {
      	memset(card->shortname,0,sizeof(card->shortname));
		/* no name available from anywhere, so use ID. */
		snprintf(card->shortname, sizeof(card->shortname)-1,
			"Hercules MIDI Dev %#04x:%#04x",
			USB_ID_VENDOR(chip->usb_id),
			USB_ID_PRODUCT(chip->usb_id));
	}
	
	/* prepend company name if not present */
	if (strstr(card->shortname,"Hercules")==NULL) {
		memset(shortname,0,sizeof(shortname));
		snprintf(shortname,sizeof(shortname)-1,"Hercules ");
		strncpy(shortname+strlen(shortname),
				card->shortname,sizeof(shortname)+strlen(shortname)-1);
		memset(card->shortname,0,sizeof(card->shortname));
		strncpy(card->shortname,shortname,sizeof(card->shortname)-1);
	}

	/* Retrieve the vendor and device strings as longname. */
	if (dev->descriptor.iManufacturer) {
		len = usb_string(dev, dev->descriptor.iManufacturer,
				 card->longname, sizeof(card->longname));
	} else {
		len = 0;
		/* We don't really care if there isn't any vendor string. */
	}
	
	if (len > 0) {
		strlcat(card->longname, " ", sizeof(card->longname));
	}

	strlcat(card->longname, card->shortname, sizeof(card->longname));

	len = strlcat(card->longname, " at ", sizeof(card->longname));

	if (len < sizeof(card->longname)) {
		usb_make_path(dev, card->longname + len, sizeof(card->longname) - len);
	}

	strlcat(card->longname,
		snd_usb_get_speed(dev) == USB_SPEED_LOW ? ", low speed" :
		snd_usb_get_speed(dev) == USB_SPEED_FULL ? ", full speed" :
		", high speed",
		sizeof(card->longname));

	usb_make_path(dev, &chip->usb_device_path[0],sizeof(chip->usb_device_path));
	snd_hdj_chip_create_proc(chip);
	
	/* initialize netlink in order to notifications to usermode */
	if (init_netlink_state(chip)!=0) {
		printk(KERN_WARNING"%s() init_netlink_state() failed\n",__FUNCTION__);
		/* We do not make this fatal, because it is possible that there are
		 *  no more netlink units available to us */
	}

	return 0;
}

/* MARK: PRODCHANGE */
static int usbid_to_product_code(u32 usbid)
{
	if (usbid == USB_ID(USB_HDJ_VENDOR_ID,  DJ_CONSOLE_PID)) {
		return DJCONSOLE_PRODUCT_CODE;
	} else if (usbid == USB_ID(USB_HDJ_VENDOR_ID,   DJ_CONSOLE_MK2_PID)) {
		return DJCONSOLE2_PRODUCT_CODE;
	} else if (usbid == USB_ID(USB_HDJ_VENDOR_ID,   DJ_CONSOLE_RMX_PID)) {
		return DJCONSOLERMX_PRODUCT_CODE;
	} else if (usbid == USB_ID(USB_HDJ_VENDOR_ID,   DJ_CONSOLE_STEEL_PID)) {
		return DJCONTROLSTEEL_PRODUCT_CODE;
	} else if (usbid == USB_ID(USB_HDJ_VENDOR_ID,   DJ_CONTROL_MP3_PID)) {
		return DJCONTROLLER_PRODUCT_CODE;
	} else if (usbid == USB_ID(USB_HDJ_VENDOR_ID,   DJ_CONTROL_MP3W_PID)) {
		return DJCONTROLLER_PRODUCT_CODE;
	} else {
		return DJCONSOLE_PRODUCT_UNKNOWN;
	}
}

static int hdj_probe(struct usb_interface *interface, const struct usb_device_id *uid)
{
	int failed_retval = -ENODEV; 
	int i, product_code = DJCONSOLE_PRODUCT_UNKNOWN;
	struct snd_hdj_chip *chip = NULL;
	struct usb_host_interface *alts = NULL;
	int ifnum = -1;
	u32 usbid = 0;
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
	struct snd_card *card = NULL;
#else
	snd_card_t *card = NULL;
#endif
	struct usb_device *usb_dev = NULL;

	usb_dev = interface_to_usbdev(interface);
	alts = &interface->altsetting[0];
	ifnum = get_iface_desc(alts)->bInterfaceNumber;
	usbid = USB_ID(le16_to_cpu(usb_dev->descriptor.idVendor),
		    le16_to_cpu(usb_dev->descriptor.idProduct));

	if((le16_to_cpu(usb_dev->descriptor.idVendor)!=USB_HDJ_VENDOR_ID)&&
		(le16_to_cpu(usb_dev->descriptor.idProduct)!=DJ_CONSOLE_PID)&&
		(le16_to_cpu(usb_dev->descriptor.idProduct)!=DJ_CONSOLE_MK2_PID)&&
		(le16_to_cpu(usb_dev->descriptor.idProduct)!=DJ_CONSOLE_RMX_PID)&&
		(le16_to_cpu(usb_dev->descriptor.idProduct)!=DJ_CONTROL_MP3_PID)&&
		(le16_to_cpu(usb_dev->descriptor.idProduct)!=DJ_CONTROL_MP3W_PID)&&
		(le16_to_cpu(usb_dev->descriptor.idProduct)!=DJ_CONSOLE_STEEL_PID))
	{
		printk(KERN_INFO"hdj_probe() unsupported device, idVendor%lx, idProduct:%lx\n",
				(unsigned long)le16_to_cpu(usb_dev->descriptor.idVendor),
				(unsigned long)le16_to_cpu(usb_dev->descriptor.idProduct));
		return failed_retval;
	}

	/* Extract the product code, and bail if the product is unsupported */
	product_code = usbid_to_product_code(usbid);
	if (product_code == DJCONSOLE_PRODUCT_UNKNOWN) {
		snd_printk(KERN_ERR "hdj_probe(): unsupported usbid:%x\n",usbid); 
		return failed_retval;
	}

	/* TODO- settle the wait time with Mark */
	/* TODO- settle which devices require wait- ask Mark */
	/* We need to wait until the TUSB loads the topboard information*/
	msleep(500);
	
	/*
	 * Check whether it's already registered.  I kept this here from usbaudio just in case in the future
	 * we have multiple interfaces to manage on the same device.  Unlikely, but then this does not add too
	 * much complication, nor does it make for unreadable code, I think.  Currently, all devices which
	 * this module drives only have 1 MIDI/bulk interface to manage.  If this changes, then 
	 * snd_hdj_create_streams will have to look for multiple interfaces.
	 */
	chip = NULL;
	down(&register_mutex);
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (usb_chip[i]!=NULL && usb_chip[i]->dev == usb_dev) {
			if (atomic_read(&usb_chip[i]->shutdown)) {
				snd_printk(KERN_WARNING"hdj_probe(): USB device is in the shutdown state, cannot create a card instance\n");
				up(&register_mutex);
				goto __error_no_dec;
			}
			chip = usb_chip[i];
			break;
		}
	}
	 
	if(chip==NULL) {
		/*
		 it's a fresh one.
		 Now look for an empty slot and create a new card instance.
		*/
		for (i = 0; i < SNDRV_CARDS; i++) {
			if (enable[i] && ! usb_chip[i] &&
			    (vid[i] == -1 || vid[i] == USB_ID_VENDOR(usbid)) &&
			    (pid[i] == -1 || pid[i] == USB_ID_PRODUCT(usbid))) {
			    /* we do this here instead of in snd_hdj_chip_create because of
			     *  cleanup issues */
			    usb_dev = usb_get_dev(usb_dev);
			    if (usb_dev==NULL) {
			    	printk(KERN_WARNING"%s() usb_get_dev() failed, bailing\n",
			    			__FUNCTION__);
			    	up(&register_mutex);
			    	return failed_retval;
			    }	
				if (snd_hdj_chip_create(usb_dev, i, product_code, &chip) < 0) {
					snd_printk(KERN_WARNING"hdj_probe(): snd_hdj_chip_create failed\n");
					if (chip!=NULL && chip->card!=NULL) {
						/* for cleanup */
						snd_card_set_dev(chip->card, &interface->dev);	
					}
					up(&register_mutex);
					goto __error_no_dec;
				}
				snd_card_set_dev(chip->card, &interface->dev);
				break;
			}
		}
		
		if (! chip) {
			snd_printk(KERN_WARNING "hdj_probe(): no available usb midi device entry\n");
			up(&register_mutex);
			goto __error_no_dec;
		} else {
			/* for the goto __error path */
			card = chip->card;
		}
	}
	usb_chip[chip->index] = chip;
	atomic_inc(&chip->num_interfaces);
	usb_set_intfdata(interface, (void *)(unsigned long)chip->index);
	card = chip->card;
	up(&register_mutex);

	/* each supported interface must increment the chip reference count */
	inc_chip_ref_count(chip->index);

	if (snd_hdj_create_streams(chip, ifnum) < 0) {
		snd_printk(KERN_WARNING"hdj_probe(): snd_usb_create_streams() failed\n");
		goto __error;
	}

	/* we are allowed to call snd_card_register() many times */
	if (snd_card_register(chip->card) < 0) {
		snd_printk(KERN_WARNING"hdj_probe(): snd_card_register() failed\n");
		goto __error;
	}

	return 0;
__error:
	snd_printk(KERN_WARNING"hdj_probe(): reached __error\n");
	chip = dec_chip_ref_count(chip->index);
	return failed_retval;
__error_no_dec:	
	snd_printk(KERN_WARNING"hdj_probe(): reached __error_no_dec\n");
	/* cleanup, if we have to do so */
	if (chip!=NULL) {
		if (chip->card!=NULL) {
			snd_card_disconnect(card);
			snd_card_free(card);
			chip->card = NULL;
		}
		snd_hdj_chip_free(chip);
	}
	return failed_retval;
}

struct snd_hdj_chip* inc_chip_ref_count(int chip_index)
{
	struct snd_hdj_chip* chip = NULL;
	if(chip_index >= SNDRV_CARDS || chip_index < 0) {
		/*invalid index*/
		return NULL;
	}

	down(&register_mutex);
	if(usb_chip[chip_index] != NULL && atomic_read(&usb_chip[chip_index]->shutdown)!=1) {
		chip = usb_chip[chip_index];
		++(chip->ref_count);
	}
	
	up(&register_mutex);

	return chip;
}

struct snd_hdj_chip* dec_chip_ref_count(int chip_index)
{
	struct snd_hdj_chip* chip = NULL;
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
	struct snd_card *card;
#else
	snd_card_t *card;
#endif
	struct list_head *p;
	struct list_head *next;

	if(chip_index >= SNDRV_CARDS || chip_index < 0) {
		/*invalid index*/
		return NULL;
	}
	down(&register_mutex);
	if(usb_chip[chip_index] != NULL) {
		chip = usb_chip[chip_index];
		--(chip->ref_count);
		
		if(chip->ref_count <= 0) {
			card = chip->card;
			/* remove the chip from the list here- no one else can now access it 
			 *   except this routine and the chip destructor, associated with the card's
			 *   "low level device" */
			usb_chip[chip_index] = NULL; 
			atomic_set(&chip->shutdown, 1);
			
			/* we have no callback associated with disconnect */
			snd_card_disconnect(card);

			/* release the midi resources */
			if (!list_empty(&chip->midi_list)) {
				list_for_each_safe(p,next,&chip->midi_list) {
					snd_hdjmidi_disconnect(p);
				}
			}

			/* release the bulk resources */
			if (!list_empty(&chip->bulk_list)) {
				list_for_each_safe(p,next,&chip->bulk_list) {
					hdjbulk_disconnect(p);
				}
			}
			
			/* Free all remaining registered processes- no one is allowed to register
			 *   anymore, because the chip is being destroyed and cannot be reacquired */
			uninit_netlink_state(chip);

			/* Freeing the card results in the chip cleanup routine (called the
			 *  chip's destructor) being executed, but only when no client has
			 *  the MIDI device open through ALSA */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
			snd_card_free_when_closed(card);
#else
			snd_card_free(card);
#endif
			up(&register_mutex);
			
			/* the chip has been destroyed */
			return NULL; 
		}
	}

	up(&register_mutex);
	return chip;
}

/* since we could be "alive" for a bit of time after disconnect if a usermode
 *  client maintains a handle, we reference interface and devices */
void reference_usb_intf_and_devices(struct usb_interface *intf)
{
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13) )
	usb_get_intf(intf);
	get_device(&intf->dev);
	usb_get_dev(interface_to_usbdev(intf));
#else
	/* this is the only recourse */
	get_device(&ubulk->iface->dev);
	usb_get_dev(interface_to_usbdev(ubulk->iface));
#endif
}

void dereference_usb_intf_and_devices(struct usb_interface *intf)
{
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13) )
	put_device(&intf->dev);
	usb_put_dev(interface_to_usbdev(intf));
	usb_put_intf(intf);
#else
	usb_put_dev(interface_to_usbdev(intf));
	/* this is the only recourse */
	put_device(&intf->dev);
#endif
}

/* warning: assumes that inc_chip_ref_count() has been called on the chip */
struct usb_hdjbulk * bulk_from_chip(struct snd_hdj_chip* chip)
{
	struct usb_hdjbulk *ubulk = NULL;
	/* assume only one MIDI device per chip- otherwise expand function parameters */
	if (list_empty(&chip->bulk_list)) {
		printk(KERN_WARNING"%s() bulk_list empty, bailing\n",__FUNCTION__);
		return NULL;
	}
	ubulk =  list_entry(chip->bulk_list.next, struct usb_hdjbulk, list);
	return ubulk;
}

/* warning: assumes that inc_chip_ref_count() has been called on the chip */
struct snd_hdjmidi* midi_from_chip(struct snd_hdj_chip* chip)
{
	struct snd_hdjmidi* umidi=NULL;
	/* assume only one MIDI device per chip- otherwise expand function parameters */
	if (list_empty(&chip->midi_list)) {
		printk(KERN_WARNING"%s() midi_list empty, bailing\n",__FUNCTION__);
		return NULL;
	}
	umidi = list_entry(chip->midi_list.next, struct snd_hdjmidi, list);
	return umidi;
}

/*
 * we need to take care of counter, since disconnection can be called also
 * many times as well as hdj_probe() returned success.
 */
static void hdj_disconnect(struct usb_interface *interface)
{
	struct snd_hdj_chip *chip;
	struct usb_hdjbulk *ubulk;
	struct list_head *p;
	struct list_head *next;
	int chip_index;
	unsigned long flags;
	int ifnum;
	struct usb_host_interface *alts = NULL;
	alts = &interface->altsetting[0];
	ifnum = get_iface_desc(alts)->bInterfaceNumber;
	chip_index = (int)(unsigned long)usb_get_intfdata(interface);

	chip = inc_chip_ref_count(chip_index);
	if (chip==NULL) {
		printk(KERN_ERR"%s WARNING: inc_chip_ref_count() returned NULL, bailing!\n",
			__FUNCTION__);
		return;
	}

	/* this operation is for bulk only */
	if (bulk_intf_num_check(chip,ifnum)==1) {
		list_for_each_safe(p,next,&chip->bulk_list) {
			ubulk = list_entry(p, struct usb_hdjbulk, list);
			if(ubulk != NULL) {
				if (is_continuous_reader_supported(ubulk->chip)==1) {
					/* make sure to unblock all readers who are waiting for data, and let them fail with
					 *  an error code
					 */
					spin_lock_irqsave(&ubulk->read_list_lock, flags);			
					signal_all_waiting_readers(&ubulk->open_list);
					spin_unlock_irqrestore(&ubulk->read_list_lock, flags);
				}
			}
		}
	}
	
	/* this decrement balances with the one above */
	dec_chip_ref_count(chip_index);

	/* this decrement balances with the one in probe */
	dec_chip_ref_count(chip_index);
}

#ifdef CONFIG_PM

static int hdj_suspend(struct usb_interface *intf, pm_message_t message)
{
	int chip_index = (int)(unsigned long)usb_get_intfdata(intf);
	struct snd_hdj_chip *chip;
	struct list_head *p;

	chip = inc_chip_ref_count(chip_index);

	printk(KERN_INFO"hdj_suspend()\n");
	
	if (!chip) {
		printk(KERN_WARNING"hdj_suspend() inc_chip_ref_count returned NULL bailing\n");
		return 0;
	}

	snd_power_change_state(chip->card, SNDRV_CTL_POWER_D3hot);
	if (atomic_inc_return(&chip->num_suspended_intf)==1) {
		/* this will prevent us from sending down more urbs */
		atomic_inc(&chip->no_urb_submission);
		
		if (!list_empty(&chip->midi_list)) {
			list_for_each(p, &chip->midi_list) {
				snd_hdjmidi_suspend(p);
			}
		}

		if (!list_empty(&chip->bulk_list)) {
			list_for_each(p, &chip->bulk_list) {
				snd_hdjbulk_suspend(p);
			}
		}
		hdj_kill_chip_urbs(chip);
	}

	dec_chip_ref_count(chip_index);

	printk(KERN_INFO"hdj_suspend() EXIT\n");
	
	return 0;
}

static int hdj_resume (struct usb_interface *intf)
{
	int chip_index = (int)(unsigned long)usb_get_intfdata(intf);
	struct snd_hdj_chip *chip=NULL;
	struct list_head *p;

	printk(KERN_INFO"hdj_resume()\n");
	chip = inc_chip_ref_count(chip_index);

	if (!chip) {
		printk(KERN_WARNING"hdj_resume() inc_chip_ref_count returned NULL bailing\n");
		return 0;
	}
	
	if (atomic_dec_return(&chip->num_suspended_intf)!=0) {
		dec_chip_ref_count(chip_index);
		return 0;
	}
	
	/* this will allow us to send down more urbs */
	atomic_dec(&chip->no_urb_submission);

	if (!list_empty(&chip->midi_list)) {
		list_for_each(p, &chip->midi_list) {
			snd_hdjmidi_resume(p);
		}
	}

	if (!list_empty(&chip->bulk_list)) {
		list_for_each(p, &chip->bulk_list) {
			hdjbulk_resume(p);
		}
	}

	/*
	 * ALSA leaves material resumption to user space
	 * we just notify
	 */

	snd_power_change_state(chip->card, SNDRV_CTL_POWER_D0);

	dec_chip_ref_count(chip_index);

	printk(KERN_INFO"hdj_resume() EXIT\n");

	return 0;
}

static int hdj_reset_resume (struct usb_interface *intf)
{
	printk(KERN_WARNING"%s() WARNING- device has been reset\n",__FUNCTION__);
	/* TODO: this has to be looked into more */
	hdj_resume(intf);
	return 0;
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18) )
#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
static int hdj_pre_reset(struct usb_interface *intf)
#else
static void hdj_pre_reset(struct usb_interface *intf)
#endif
{
	int chip_index = (int)(unsigned long)usb_get_intfdata(intf);
	struct snd_hdj_chip *chip = NULL;
	struct list_head *p;

	printk(KERN_INFO"hdj_pre_reset()\n");
	chip = inc_chip_ref_count(chip_index);

	if (!chip) {
		printk(KERN_INFO"hdj_pre_reset(): inc_chip_ref_count returned NULL bailing\n");
#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
		return 0;
#else
		return;
#endif
	}
	
	/* This is checked by our wrapper hdjbulk_submit_urb before it calls usb_submit_urb.  
	 *  open and poll will be forbidden as well */
	atomic_inc(&chip->no_urb_submission);

	/* ask MIDI to stop I/O */
	if (!list_empty(&chip->midi_list)) {
		list_for_each(p, &chip->midi_list) {
			snd_hdjmidi_pre_reset(p);
		}
	}

	/* ask bulk to stop I/O */
	if (!list_empty(&chip->bulk_list)) {
		list_for_each(p, &chip->bulk_list) {
			snd_hdjbulk_pre_reset(p);
		}
	}

	hdj_kill_chip_urbs(chip);
	dec_chip_ref_count(chip_index);

#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
	return 0;
#endif
}
#endif

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18) )
#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
static int hdj_post_reset(struct usb_interface *intf)
#else
static void hdj_post_reset(struct usb_interface *intf)
#endif
{
	int chip_index = (int)(unsigned long)usb_get_intfdata(intf);
	struct snd_hdj_chip *chip = NULL;
	struct list_head *p;

	printk(KERN_INFO"hdj_post_reset()\n");
	chip = inc_chip_ref_count(chip_index);

	if (!chip) {
		printk(KERN_INFO"hdj_post_reset(): inc_chip_ref_count returned NULL bailing\n");
#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
		return 0;
#else
		return;
#endif
	}
	
	/* allow I/O to be sent */
	atomic_dec(&chip->no_urb_submission);

	if (!list_empty(&chip->midi_list)) {
		list_for_each(p, &chip->midi_list) {
			snd_hdjmidi_post_reset(p);
		}
	}
	
	if (!list_empty(&chip->bulk_list)) {
		list_for_each(p, &chip->bulk_list) {
			snd_hdjbulk_post_reset(p);
		}
	}

	dec_chip_ref_count(chip_index);

#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
	return 0;
#endif
}
#endif
#endif

/* crude way to test if device is connected or not */
int is_device_present(struct snd_hdj_chip* chip)
{
	int rc=-ENODEV;
	u16     devstatus;
	rc = usb_get_status(chip->dev, USB_RECIP_DEVICE, 0, &devstatus);
	if (rc >= 0) {
			  rc = (rc > 0 ? 0 : -ENODEV);
	}
	return rc;
}

static int netlink_init(void)
{
	int unit, ref_count;
	
	ref_count = atomic_inc_return(&netlink_ref_count);
	smp_mb();
	if (ref_count>1) {
		return 0;	
	}
	
	/* Try to allocate a netlink socket minimizing the risk of collision, 
	 *  by starting at the max unit number and counting down */
	for (unit=MAX_LINKS-1;unit>MIN_NETLINK_UNIT;unit--) {
		nl_sk = netlink_kernel_create(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
									&init_net,
#endif
									unit,
									0,
									NULL, /* we do not receive messages, only unicast them */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
									NULL,
#endif
									THIS_MODULE);
		if (nl_sk!=NULL) {
			netlink_unit = unit;
			return 0;
		}
	}
	
	/* failure, so we must decrement ref count */
	atomic_dec(&netlink_ref_count);
	smp_mb();

	return -ENOMEM;
}

int init_netlink_state(struct snd_hdj_chip* chip)
{
	if (netlink_init()!=0) {
		printk(KERN_WARNING"%s() netlink_init() failed\n",__FUNCTION__);
		return -ENOMEM;	
	}
	return 0;
}

static void netlink_release(void)
{
	int ref_count;
	
	ref_count = atomic_dec_return(&netlink_ref_count);
	smp_mb();
	if (ref_count==0) {
		if (nl_sk != NULL) {
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,24) )
			sock_release(nl_sk->sk_socket);
#else
			netlink_kernel_release(nl_sk);
#endif
			nl_sk = NULL;
			netlink_unit = NETLINK_UNIT_INVALID_VALUE;
		} else {
			printk(KERN_INFO"%s() already deleted\n",__FUNCTION__);
		}
	}
}

void uninit_netlink_state(struct snd_hdj_chip* chip)
{
	unregister_for_netlink(chip,NULL);
	netlink_release();
}

/* This enter netlink message */
static struct sk_buff *netlink_make_reply(int target_pid, int seq, int type, int done,
                                   			int multi, void *payload, int size)
{
	struct sk_buff  *skb;
	struct nlmsghdr *nlh;
	int             len = NLMSG_SPACE(size);
	void            *data;
	int             flags = multi ? NLM_F_MULTI : 0;
	int             t     = done  ? NLMSG_DONE  : type;
	
	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb) {
		return NULL;
	}

	nlh              = NLMSG_PUT(skb, target_pid, seq, t, size);
	nlh->nlmsg_flags = flags;
	data             = NLMSG_DATA(nlh);
	if (size > 0) {
		memcpy(data, payload, size);
	}
	return skb;

nlmsg_failure:                  /* Used by NLMSG_PUT */
	if (skb) {
		kfree_skb(skb);
	}
	return NULL;
}

int register_for_netlink(struct snd_hdj_chip* chip, 
							struct file* file, 
							void* context,
							u8 compat_mode)
{
	struct netlink_list * netlink_item = NULL;
	int tgid = current->tgid;
	struct list_head *p;
	struct list_head *next;

	down(&chip->netlink_list_mutex);
	/* verify if the process was already registered */
	if (!list_empty(&chip->netlink_registered_processes)) {
		list_for_each_safe(p,next,&chip->netlink_registered_processes) {
			netlink_item = list_entry(p, struct netlink_list, list);
			if (netlink_item->pid == tgid) {
				printk(KERN_ERR"%s() already registered.\n",__FUNCTION__);
				up(&chip->netlink_list_mutex);
				return -EINVAL;
			} 
		}
	}

	netlink_item = zero_alloc(sizeof(struct netlink_list),GFP_KERNEL);
	if (netlink_item == NULL) {
		printk(KERN_ERR"%s() failed to allocate memory.\n",__FUNCTION__);
		return -ENOMEM;
	}

	netlink_item->pid = tgid;
	netlink_item->context = context;
	netlink_item->file = file;
	netlink_item->compat_mode = compat_mode;

	/* add the process information to the list */
	list_add_tail(&netlink_item->list,&chip->netlink_registered_processes);
	up(&chip->netlink_list_mutex);

	return 0;
}


/* Note: if file==NULL unregister all clients */
int unregister_for_netlink(struct snd_hdj_chip* chip, struct file* file)
{
	struct sk_buff  *skb;
	struct netlink_list * netlink_item = NULL;
	struct list_head *p;
	struct list_head *next;
	/*int tgid = current->tgid;*/
	int item_freed = 0;
	/*printk(KERN_INFO"%s() received pid:%d, file:%p\n",__FUNCTION__,tgid,file);*/
	down(&chip->netlink_list_mutex);
	/* verify if the process was already registered */
	if (!list_empty(&chip->netlink_registered_processes)) {
		list_for_each_safe(p,next,&chip->netlink_registered_processes) {
			netlink_item = list_entry(p, struct netlink_list, list);
			if (netlink_item->file == file || file == NULL) {
				/*printk(KERN_INFO"unregister_for_netlink(): unregistering TGID: %d, file:%p\n", 
					netlink_item->pid,
					file);*/
				/* send a 0 length buffer- this will unblock usermode clients */
				skb = netlink_make_reply(netlink_item->pid, 0, 0, 1, 0, NULL, 0);
				if (skb != NULL && nl_sk!=NULL) {
					netlink_unicast(nl_sk, skb, netlink_item->pid, MSG_DONTWAIT);
				}

				list_del(p);
				kfree(netlink_item);

				item_freed = 1;
			}
		}
	}
	up(&chip->netlink_list_mutex);

	if (item_freed == 0 && file != NULL) {
		/*printk(KERN_INFO"%s() returning error\n",__FUNCTION__);*/
		return -EINVAL;
	} else {	
		/*printk(KERN_INFO"%s() returning success\n",__FUNCTION__);*/
		return 0;
	}
}

/* This sends a raw message over netlink to listeners (in usermode) */ 
static int netlink_send_raw_msg(struct snd_hdj_chip* chip, 
								struct netlink_msg_header* msg, 
								int len
								#ifdef CONFIG_COMPAT
								,struct netlink_msg_header32* msg_compat, 
								int len_compat
								#endif
								) 
{
	struct sk_buff	*skb;
	struct list_head *p;
	struct list_head *next;
	struct netlink_list * netlink_item = NULL;
	int ret = 0;

	if (nl_sk == NULL) {
		printk(KERN_INFO"%s() Invalid Socket!\n",__FUNCTION__);
		return -EINVAL;
	}
	
	down(&chip->netlink_list_mutex);
	/* verify if the process was already registered */
	if (!list_empty(&chip->netlink_registered_processes)) {
		list_for_each_safe(p,next,&chip->netlink_registered_processes) {
			netlink_item = list_entry(p, struct netlink_list, list);
			
			/*printk(KERN_INFO"%s() making message...: tgid: %d pid: %d\n", 
				__FUNCTION__,current->tgid, current->pid);*/
#ifdef CONFIG_COMPAT
			if (netlink_item->compat_mode==1) {
				msg_compat->context = (compat_ulong_t)netlink_item->context;
				skb = netlink_make_reply(netlink_item->pid, 0, 0, 1, 0, 
								msg_compat, len_compat);
			} else {
				msg->context = netlink_item->context;
				skb = netlink_make_reply(netlink_item->pid, 0, 0, 1, 0, msg, len);
			}
#else
			msg->context = netlink_item->context;
			skb = netlink_make_reply(netlink_item->pid, 0, 0, 1, 0, msg, len);
#endif

			if (skb != NULL && nl_sk!=NULL) {
				ret = netlink_unicast(nl_sk, skb, netlink_item->pid, MSG_DONTWAIT);
			} else {
				ret = -EINVAL;
			}
		}
	}
	up(&chip->netlink_list_mutex);

	return ret;
}

/* This creates a buffer in which a netlink header and the message parameter, and request that
 *  it be sent over netlink to listeners (in usermode) */
static int netlink_send_msg(struct snd_hdj_chip* chip, 
							unsigned long msg_id, 
							void* data,
							unsigned long data_len
							#ifdef CONFIG_COMPAT
							,void* data_compat,
							unsigned long data_len_compat
							#endif
							)
{
	struct netlink_msg_header *msg=NULL;
	int msg_size, rc = 0;
#ifdef CONFIG_COMPAT
	struct netlink_msg_header32 *msg_compat=NULL;
	int msg_size_compat;
#endif
	
	msg_size = sizeof(struct netlink_msg_header)+data_len; 
	msg = (struct netlink_msg_header *)zero_alloc(msg_size, GFP_KERNEL);
#ifdef CONFIG_COMPAT
	msg_size_compat = sizeof(struct netlink_msg_header32)+data_len_compat;
	msg_compat = (struct netlink_msg_header32 *)zero_alloc(msg_size_compat, GFP_KERNEL);
#endif

#ifdef CONFIG_COMPAT
	if (msg!=NULL && msg_compat!=NULL) {
#else
	if (msg!=NULL) {
#endif
		msg->msg_magic_number = MAGIC_NUM;
		msg->header_size = sizeof(struct netlink_msg_header);
		msg->msg_id = msg_id;
		msg->bytes_to_follow = data_len;
		memcpy((char*)msg+sizeof(struct netlink_msg_header),data,data_len);
		
#ifdef CONFIG_COMPAT
		msg_compat->msg_magic_number = (compat_ulong_t)MAGIC_NUM;
		msg_compat->header_size = (compat_ulong_t)sizeof(struct netlink_msg_header32);
		msg_compat->msg_id = (compat_ulong_t)msg_id;
		msg_compat->bytes_to_follow = (compat_ulong_t)data_len_compat;
		memcpy((char*)msg_compat+sizeof(struct netlink_msg_header32),
				data_compat,data_len_compat);
#endif
		/* will send async, so we can free the buffer */
		rc = netlink_send_raw_msg(chip,
									msg,
									msg_size
									#ifdef CONFIG_COMPAT
									,msg_compat,
									msg_size_compat
									#endif
									);
	}
	
#ifdef CONFIG_COMPAT
	if (msg!=NULL) {
		kfree(msg);	
	}
	if (msg_compat!=NULL) {
		kfree(msg_compat);	
	}
#else
	if (msg!=NULL) {
		kfree(msg);	
	}
#endif
	
	return rc;
}

/* This sends a control change to all listeners (over netlink in usermode), and formats
 *  the control change data into a control change message */
int send_control_change_over_netlink(struct snd_hdj_chip* chip,  
									unsigned long product_code,
									unsigned long control_id,
									unsigned long control_value) 
{
	struct control_change_data control_msg;
#ifdef CONFIG_COMPAT
	struct control_change_data32 control_msg_compat;
#endif
	int rc=0;

	control_msg.product_code = product_code;
	control_msg.control_id = control_id;
	control_msg.control_value = control_value;
	memcpy(&control_msg.location_id,chip->usb_device_path,LOCATION_ID_LEN);
	
#ifdef CONFIG_COMPAT
	control_msg_compat.product_code = (compat_ulong_t)product_code;
	control_msg_compat.control_id = (compat_ulong_t)control_id;
	control_msg_compat.control_value = (compat_ulong_t)control_value;
	memcpy(&control_msg_compat.location_id,chip->usb_device_path,LOCATION_ID_LEN);
#endif
	rc = netlink_send_msg(chip, 
							MSG_CONTROL_CHANGE, 
							&control_msg, 
							sizeof(control_msg)
							#ifdef CONFIG_COMPAT
							,&control_msg_compat,
							sizeof(control_msg_compat)
							#endif
							);

	return rc;
}

/* MARK: PRODCHANGE */
/* just does a printk of the product name and IDs, and driver version */
void dump_product_name_to_console(struct snd_hdj_chip* chip,
									u8 output_bulk_info,
									u8 output_midi_info)
{
	if (output_midi_info!=0) {
		printk(KERN_INFO"MIDI state successfully created, driver version:%x\n",
			driver_version);
	}
	if (output_bulk_info!=0) {
		printk(KERN_INFO"Bulk state sucessfully created, driver version:%x\n",
			driver_version);	
	}
	if (chip->product_code==DJCONSOLE_PRODUCT_CODE) {
		printk(KERN_INFO"DJ Console, vid:%x, pid:%x\n",
				 USB_ID_VENDOR(chip->usb_id), USB_ID_PRODUCT(chip->usb_id));
	} else if 	(chip->product_code==DJCONSOLE2_PRODUCT_CODE) {
		printk(KERN_INFO"DJ Console Mk2, vid:%x, pid:%x\n",
				 USB_ID_VENDOR(chip->usb_id), USB_ID_PRODUCT(chip->usb_id));
	} else if 	(chip->product_code==DJCONSOLERMX_PRODUCT_CODE) {
		printk(KERN_INFO"DJ Console Rmx, vid:%x, pid:%x\n",
				 USB_ID_VENDOR(chip->usb_id), USB_ID_PRODUCT(chip->usb_id));
	} else if 	(chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		printk(KERN_INFO"DJ Control Steel, vid:%x, pid:%x\n",
				 USB_ID_VENDOR(chip->usb_id), USB_ID_PRODUCT(chip->usb_id));
	} else if (chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		printk(KERN_INFO"DJ Control MP3, vid:%x, pid:%x\n",
				 USB_ID_VENDOR(chip->usb_id), USB_ID_PRODUCT(chip->usb_id));
	} else {
		printk(KERN_WARNING"%s unknown product:%d, vid:%x, pid:%x\n",
				__FUNCTION__,chip->product_code,
				USB_ID_VENDOR(chip->usb_id),USB_ID_PRODUCT(chip->usb_id));
	}
}

static int __init usb_hdj_init(void)
{
	int result;

	/* initialize MIDI */
	midi_init();
	
	/* register this driver with the USB subsystem */
	result = usb_register(&hdj_driver);
	if (result)
		printk(KERN_ERR"usb_hdj_init(): usb_register failed. Error number %d\n", result);

	return result;
}

static void __exit usb_hdj_exit(void)
{
	/* cleanup MIDI */
	midi_cleanup();
	
	/* deregister this driver with the USB subsystem */
	usb_deregister(&hdj_driver);
}

module_init(usb_hdj_init);
module_exit(usb_hdj_exit);
MODULE_DESCRIPTION("DJ Series Kernel Module");
MODULE_SUPPORTED_DEVICE("DJ Series Devices");
MODULE_AUTHOR("Philip Lukidis <plukidis@guillemot.com>");
MODULE_LICENSE("GPL");
