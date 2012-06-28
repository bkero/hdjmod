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
#include <linux/usb.h>
#include <linux/version.h>	/* For LINUX_VERSION_CODE */
#include <asm/atomic.h>
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
#include "bulk.h"
#include "midi.h"
#include "configuration_manager.h"
#include "callback.h"
#include "hdjmp3.h"

/* The firmware is used for verification purposes
 * set midi_channel to MIDI_INVALID_CHANNEL if you want it to be
 * queried from the hardware
 * set serial_number to STEEL_DEFAULT_SERIAL_NUMBER if you want it to be
 * queried from the hardware */
static int set_djcontrolsteel_non_volatile_data(struct usb_hdjbulk *ubulk,
						u8 firmware_version,
						u32 serial_number,
						u16 midi_channel)
{
	int ret = 0;
	u8 bulk_data_write[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];
	u8 bulk_data_read [DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		/* get the midi channel if it's set to MIDI_INVALID_CHANNEL */
		if (midi_channel == MIDI_INVALID_CHANNEL) {
			ret = get_midi_channel(ubulk->chip, &midi_channel);
		}

		if (ret == 0) {
			/*get the serial number if it's set to 0*/
			if (serial_number == STEEL_DEFAULT_SERIAL_NUMBER) {
				ret = get_serial_number(ubulk, &serial_number);
			}

			if (ret == 0) {
				/*prepare the set non volatile data request*/
				memset(bulk_data_write, 0, sizeof(bulk_data_write));
				bulk_data_write[0] = DJ_STEEL_SET_NON_VOLATILE_DATA;
				bulk_data_write[1] = firmware_version;
				bulk_data_write[2] = (u8)(serial_number >> 24);
				bulk_data_write[3] = (u8)(serial_number >> 16);
				bulk_data_write[4] = (u8)(serial_number >> 8);
				bulk_data_write[5] = (u8)(serial_number);
				bulk_data_write[6] = (u8)midi_channel;

				/*set the non volatile data request*/
				ret = send_bulk_write(ubulk, 
							bulk_data_write, 
							sizeof(bulk_data_write),
							0 /* force_send */);
				if (ret!=0) {
					printk(KERN_ERR"%s() send_bulk_write() failed, rc:%d\n",__FUNCTION__,ret);
					return ret;
				}

				/* 100ms delay...gives the device time to write	*/
				msleep(100);

				/*verify if the data was written successfully*/
				ret = get_bulk_data(ubulk, bulk_data_read, sizeof(bulk_data_read), 0 /*force_send*/);
				if (ret == 0) {
					if ( (bulk_data_read[DJ_STEEL_EP_81_SERIAL_NUM_BITS_31_TO_24] != bulk_data_write[2]) ||
						(bulk_data_read[DJ_STEEL_EP_81_SERIAL_NUM_BITS_23_TO_16] != bulk_data_write[3]) ||
						(bulk_data_read[DJ_STEEL_EP_81_SERIAL_NUM_BITS_15_TO_08] != bulk_data_write[4]) ||
						(bulk_data_read[DJ_STEEL_EP_81_SERIAL_NUM_BITS_07_TO_00] != bulk_data_write[5]) ||
						(bulk_data_read[DJ_STEEL_EP_81_MIDI_CHANNEL] != bulk_data_write[6]) ) {
						printk(KERN_ERR"%s(): Data Read From Device != Data Sent. Serial: 0x%X != 0x%X Midi: %u != %u\n",
							__FUNCTION__,
							serial_number, (bulk_data_read[DJ_STEEL_EP_81_SERIAL_NUM_BITS_31_TO_24] << 24) + 
							(bulk_data_read[DJ_STEEL_EP_81_SERIAL_NUM_BITS_23_TO_16] << 16) +
							(bulk_data_read[DJ_STEEL_EP_81_SERIAL_NUM_BITS_15_TO_08] << 8) +
							(bulk_data_read[DJ_STEEL_EP_81_SERIAL_NUM_BITS_07_TO_00]), 
							midi_channel, bulk_data_read[DJ_STEEL_EP_81_MIDI_CHANNEL]);
						ret = -EIO;
					}
				}
			}
		}
	} else {
		printk(KERN_WARNING"%s(): invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	if (ret != 0) {
		printk(KERN_ERR"%s(): ret: %d\n",__FUNCTION__,ret);
	}

	return ret;
}

/*
 * Set the jog wheel Parameters for DJ Control Steel- requires jog wheel
 *  parameters in native format.
 *jog_wheel_parameters:	Byte0:	Bit7 to 2: Don't care
 *				Bit1: JOG_LockB
 *				Bit0: JOG_LockA
 *			Byte1:	JogWheel Sensitivity
 */
static int steel_set_jogwheel_parameters(struct usb_hdjbulk * ubulk,
				       		u16 jog_wheel_parameters)
{
	int ret = 0;
	u8 bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {		
		/* prepare request */
		memset(bulk_data, 0, sizeof(bulk_data));
		bulk_data[0] = DJ_STEEL_SET_JOG_WHEEL_PARAMETER;
		bulk_data[1] = (u8)(jog_wheel_parameters >> 8);
		bulk_data[2] = (u8)jog_wheel_parameters;

		ret = send_bulk_write(ubulk, bulk_data, sizeof(bulk_data), 0 /* force_send */);
		if (ret!=0) {
			printk(KERN_ERR"%s() send_bulk_write failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s(): invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	return ret;
}

/*
 * Get the jog wheel Parameters for DJ Control Steel- requires jog wheel
 *  parameters in native format.
 *jogwheel_parameters:	Byte0:	Bit7 to 2: Don't care
 *				Bit1: JOG_LockB
 *				Bit0: JOG_LockA
 *			Byte1:	JogWheel Sensitivity
 */
static int steel_get_jogwheel_parameters(struct usb_hdjbulk * ubulk,
			       		u16* jog_wheel_parameters,
			       		u8 query_hardware)
{
	int ret = 0;
	u8 bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {	
		if (query_hardware!=0) {
			ret = get_bulk_data(ubulk, bulk_data, sizeof(bulk_data),0/*force_send*/);
			if (ret == 0) {
				/*get the jog wheel parameter*/
				*jog_wheel_parameters = bulk_data[DJ_STEEL_EP_81_JOG_WHEEL_SETTINGS_1];
				*jog_wheel_parameters = ((bulk_data[DJ_STEEL_EP_81_JOG_WHEEL_SETTINGS_0] & 0x7F) << 8) + (*jog_wheel_parameters & 0xFF);
			} else {
				printk(KERN_ERR"%s() get_bulk_data failed, rc:%d\n",__FUNCTION__,ret);
			}
		} else {
			*jog_wheel_parameters = atomic_read(&(((struct hdj_steel_context *)ubulk->device_context)->jog_wheel_parameters));
		}
	} else {
		printk(KERN_WARNING"%s(): invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	/*printk(KERN_INFO"%s(): ret: %d, state: 0x%04X\n",
		__FUNCTION__,ret, *jog_wheel_parameter);*/

	return ret;
}

/* Get the Jog Wheel Lock Status for the DJ Console Rmx- requires lock state in native format */
static int rmx_get_jogwheel_lock_status(struct usb_hdjbulk *ubulk, u16 * lock_state, u8 query_hardware)
{
	int ret = -EINVAL;
	struct hdj_mk2_rmx_context* dc;
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		if (query_hardware) {
			ret = send_vendor_request(ubulk->chip->index, REQT_READ, 
						DJ_GET_JOG_WHEEL_LOCK_SETTING, 0, 0, lock_state, 0);
			if (ret == 0) {
				atomic_set(&dc->jog_wheel_lock_status,*lock_state);
			} else {
				printk(KERN_ERR"%s() send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
			}
		} else {
			*lock_state = atomic_read(&dc->jog_wheel_lock_status);
			ret = 0;
		}
	} else {
		printk(KERN_WARNING"%s(): invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
	}
	
	return ret;
}

/* Set the Jog Wheel Lock Status for the DJ Console Rmx- requires lock state in native format */
static int rmx_set_jogwheel_lock_status(struct usb_hdjbulk *ubulk, u16 lock_state)
{
	int ret = -EINVAL;
	struct hdj_mk2_rmx_context* dc;
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, DJ_SET_JOG_WHEEL_LOCK_SETTING, 
						lock_state, 0, NULL, 0);
		if (ret == 0) {
			atomic_set(&dc->jog_wheel_lock_status,lock_state);
		} else {
			printk(KERN_ERR"%s() send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s(): invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
	}
	
	return ret;
}

/* get the Jog Wheel sensitivity for the DJ Console RMX */
static int rmx_get_jogwheel_sensitivity(struct usb_hdjbulk *ubulk, u16 * jogwheel_sensitivity, u8 query_hardware)
{
	int ret = -EINVAL;
	struct hdj_mk2_rmx_context* dc;
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		if (query_hardware) {
			ret = send_vendor_request(ubulk->chip->index, REQT_READ, 
						DJ_GET_JOG_WHEEL_SENSITIVITY, 0, 0, jogwheel_sensitivity, 0);
			if (ret == 0) {
				atomic_set(&dc->jog_wheel_sensitivity,*jogwheel_sensitivity);
			} else {
				printk(KERN_ERR"%s() send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
			}
		} else {
			*jogwheel_sensitivity = atomic_read(&dc->jog_wheel_sensitivity);
			ret = 0;
		}
	} else {
		printk(KERN_WARNING"%s(): invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
	}
	
	return ret;
}

/*Set the Jog Wheel ensitivity for the DJ Console RMX*/
static int rmx_set_jogwheel_sensitivity(struct usb_hdjbulk *ubulk, u16 jogwheel_sensitivity)
{
	int ret = -EINVAL;
	struct hdj_mk2_rmx_context* dc;
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, DJ_SET_JOG_WHEEL_SENSITIVITY, 
					jogwheel_sensitivity, 0, NULL, 0);
		if (ret == 0) {
			atomic_set(&dc->jog_wheel_sensitivity,jogwheel_sensitivity);

			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_JOG_WHEEL_SENS,
							jogwheel_sensitivity);
		}
	} else {
		printk(KERN_WARNING"%s(): invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
	}
	
	return ret;
}

/* supports Rmx and Steel */
int get_jogwheel_lock_status(struct usb_hdjbulk *ubulk, u16 * lock_state, u8 query_hardware, u8 native)
{
	int ret;
	u16 lock_state_native = 0;
	u16 decka_lock_mask=0, deckb_lock_mask=0;
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		ret = rmx_get_jogwheel_lock_status(ubulk, &lock_state_native, query_hardware);
		if (ret!=0) {
			printk(KERN_ERR"%s() rmx_get_jogwheel_lock_status() failed, rc:%d\n",
				__FUNCTION__,ret);
		} else {
			*lock_state = 0;
			decka_lock_mask = DJRMX_JOGWHEEL_LOCK_DECK_A;
			deckb_lock_mask = DJRMX_JOGWHEEL_LOCK_DECK_B;
		}
	} else if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		ret = steel_get_jogwheel_parameters(ubulk, &lock_state_native, query_hardware);
		if (ret!=0) {
			printk(KERN_ERR"%s() steel_get_jogwheel_lock_status() failed, rc:%d\n",
				__FUNCTION__,ret);
		} else {
			*lock_state = 0;
			decka_lock_mask = DJSTEEL_JOGWHEEL_LOCK_DECK_A;
			deckb_lock_mask = DJSTEEL_JOGWHEEL_LOCK_DECK_B;
		}
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	if (ret==0) {
		if (native==0) {
			/* convert from native format to common format */
			if (lock_state_native & decka_lock_mask) {
				*lock_state |= COMMON_JOGWHEEL_LOCK_DECK_A;
			}
			if (lock_state_native & deckb_lock_mask) {
				*lock_state |= COMMON_JOGWHEEL_LOCK_DECK_B;
			}
		} else {
			*lock_state = lock_state_native;
		}
	}
	return ret;
}

/* supports Rmx and Steel */
int set_jogwheel_lock_status(struct usb_hdjbulk *ubulk, u16 lock_state)
{
	int ret;
	struct hdj_steel_context *dc;
	u16 lock_state_native = 0;

	lock_state &= COMMON_JOGWHEEL_LOCK_DECK_BOTH;
	
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		if (lock_state & COMMON_JOGWHEEL_LOCK_DECK_A) {
			lock_state_native |= DJRMX_JOGWHEEL_LOCK_DECK_A;
		}
		if (lock_state & COMMON_JOGWHEEL_LOCK_DECK_B) {
			lock_state_native |= DJRMX_JOGWHEEL_LOCK_DECK_B;
		}
		ret = rmx_set_jogwheel_lock_status(ubulk,lock_state_native);
		if (ret!=0) {
			printk(KERN_ERR"%s() rmx_set_jogwheel_lock_status() failed, rc:%d\n",
				__FUNCTION__,ret);
		}
	} else if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		dc = (struct hdj_steel_context *)ubulk->device_context;
		lock_state_native = atomic_read(&dc->jog_wheel_parameters);

		if (lock_state & COMMON_JOGWHEEL_LOCK_DECK_A) {
			lock_state_native |= DJSTEEL_JOGWHEEL_LOCK_DECK_A;
		} else {
			lock_state_native &= ~DJSTEEL_JOGWHEEL_LOCK_DECK_A;	
		}
		if (lock_state & COMMON_JOGWHEEL_LOCK_DECK_B) {
			lock_state_native |= DJSTEEL_JOGWHEEL_LOCK_DECK_B;
		} else {
			lock_state_native &= ~DJSTEEL_JOGWHEEL_LOCK_DECK_B;
		}
		ret = steel_set_jogwheel_parameters(ubulk, lock_state_native);
		if (ret!=0) {
			printk(KERN_ERR"%s() steel_set_jogwheel_lock_status() failed, rc:%d\n",
				__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	if (ret==0) {
		/* send control change notification to clients */
		send_control_change_over_netlink(ubulk->chip,
						ubulk->chip->product_code,
						CTRL_CHG_JOG_WHEEL_LOCK,
						lock_state);
	}
	return ret;
}

/* supports Rmx and Steel */
int get_jogwheel_sensitivity(struct usb_hdjbulk *ubulk, u16 * jogwheel_sensitivity, u8 query_hardware, u8 native)
{
	int ret;
	u16 sensitivity_native=0;
	
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		ret = rmx_get_jogwheel_sensitivity(ubulk, &sensitivity_native, query_hardware);
		if (ret!=0) {
			printk(KERN_ERR"%s() rmx_get_jogwheel_sensitivity failed, rc:%d\n",
				__FUNCTION__,ret);
		} else {
			if (native==0) {
				*jogwheel_sensitivity = (sensitivity_native&DJRMX_JOGWHEEL_SENSITIVITY_MASK)&
							COMMON_JOGWHEEL_SENSITIVITY_MASK;
			} else {
				*jogwheel_sensitivity = sensitivity_native;
			}
		}
	} else if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		ret = steel_get_jogwheel_parameters(ubulk, &sensitivity_native, query_hardware);
		if (ret!=0) {
			printk(KERN_ERR"%s() steel_get_jogwheel_parameters failed, rc:%d\n",
				__FUNCTION__,ret);
		} else {
			if (native==0) {
				*jogwheel_sensitivity = 
				((sensitivity_native&DJSTEEL_JOGWHEEL_SENSITIVITY_MASK)>>
					DJSTEEL_TO_COMMON_JOGWHEEL_SENSITIVITY_SF)&
					COMMON_JOGWHEEL_SENSITIVITY_MASK;
			} else {
				*jogwheel_sensitivity = sensitivity_native;
			}
		}
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	return ret;
}

/* supports Rmx and Steel */
int set_jogwheel_sensitivity(struct usb_hdjbulk *ubulk, u16 jogwheel_sensitivity)
{
	int ret;
	struct hdj_steel_context *dc;
	int sensitivity_native=0;
	
	jogwheel_sensitivity &= COMMON_JOGWHEEL_SENSITIVITY_MASK;

	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		sensitivity_native = jogwheel_sensitivity&DJRMX_JOGWHEEL_SENSITIVITY_MASK;
		ret = rmx_set_jogwheel_sensitivity(ubulk, sensitivity_native);
		if (ret!=0) {
			printk(KERN_ERR"%s() rmx_set_jogwheel_sensitivity failed, rc:%d\n",
				__FUNCTION__,ret);
		}
	} else if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		dc = (struct hdj_steel_context *)ubulk->device_context;
		sensitivity_native = atomic_read(&dc->jog_wheel_parameters);
		sensitivity_native &= ~DJSTEEL_JOGWHEEL_SENSITIVITY_MASK;
		sensitivity_native |= 
					(jogwheel_sensitivity<<COMMON_TO_DJSTEEL_JOGWHEEL_SENSITIVITY_SF)&
						DJSTEEL_JOGWHEEL_SENSITIVITY_MASK;
					
		ret = steel_set_jogwheel_parameters(ubulk, sensitivity_native);
		if (ret!=0) {
			printk(KERN_ERR"%s() steel_set_jogwheel_parameters() failed, rc:%d\n",
				__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	if (ret==0) {
		/* send control change notification to clients */
		send_control_change_over_netlink(ubulk->chip,
						ubulk->chip->product_code,
						CTRL_CHG_JOG_WHEEL_SENS,
						jogwheel_sensitivity);
	}

	return ret;
}

int set_midi_channel(struct snd_hdj_chip* chip, u16 *channel)
{
	int channel_actually_set = 0;
	int ret = -EINVAL;
	
	/* ask the midi driver to set the midi channel */
	ret = snd_hdjmidi_set_midi_channel(chip->index, *channel, &channel_actually_set);
	if (ret==0) {
		*channel = channel_actually_set;
		/* send control change notification to clients */
		send_control_change_over_netlink(chip,
						chip->product_code,
						CTRL_CHG_MIDI_CHANNEL,
						*channel);
	} else {
		printk(KERN_ERR"%s snd_hdjmidi_set_midi_channel failed, rc:%d\n",
			__FUNCTION__,ret);
	}
	
	return ret;
}


int __set_midi_channel(int chip_index, u16 channel)
{
	int ret = -EINVAL;
	struct usb_hdjbulk *ubulk;
	struct snd_hdj_chip* chip;

	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s no context, bailing!\n",__FUNCTION__);
		return -EINVAL;
	}
	
	ubulk = bulk_from_chip(chip);
	if (ubulk!=NULL) {
		ret = set_djcontrolsteel_non_volatile_data(ubulk, 
					atomic_read(&ubulk->hdj_common.firmware_version), 
					STEEL_DEFAULT_SERIAL_NUMBER, channel);
			if (ret!=0) {
				printk(KERN_ERR"%s set_djcontrolsteel_non_volatile_data failed, rc:%d\n",
					__FUNCTION__,
					ret);
			}
	}
	
	dec_chip_ref_count(chip_index);

	return ret;
}

int get_midi_channel(struct snd_hdj_chip* chip, u16 * channel)
{
	int current_channel = 0;
	int ret = -EINVAL;
	
	/* ask the midi driver for the current midi channel */
	ret = snd_hdjmidi_get_current_midi_channel(chip->index, &current_channel);
	if (ret==0) {
		*channel = current_channel;
	} else {
		printk(KERN_ERR"%s snd_hdjmidi_get_current_midi_channel failed, rc:%d\n",
			__FUNCTION__,
			ret);
	}

	return ret;
}

int __get_midi_channel(int chip_index, u16 * channel)
{
	u8 bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];
	int ret = -EINVAL;
	struct usb_hdjbulk *ubulk;
	struct snd_hdj_chip* chip;

	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s no context, bailing!\n",__FUNCTION__);
		return -EINVAL;
	}

	ubulk = bulk_from_chip(chip);
	if (ubulk!=NULL) {
		ret = get_bulk_data(ubulk, bulk_data, sizeof(bulk_data),0/*force_send*/);
		if (ret == 0) {
			*channel = bulk_data[DJ_STEEL_EP_81_MIDI_CHANNEL];
		} else {
			printk(KERN_ERR"%s get_bulk_data failed, rc:%d\n",__FUNCTION__,ret);
		}
	}

	dec_chip_ref_count(chip_index);

	return ret;
}

int get_serial_number(struct usb_hdjbulk *ubulk, u32* serial_number)
{
	u8 bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];
	u16 serial_number_word1 = 0;
	u16 serial_number_word2 = 0;
	int ret = 0;
	struct hdj_mk2_rmx_context* dc;
	struct hdj_steel_context* dcs;
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		ret = send_vendor_request(ubulk->chip->index, REQT_READ, 
					DJ_GET_SERIAL_NUMBER_WORD_1, 0, 0, &serial_number_word1, 0);
		if (ret == 0) {
			ret = send_vendor_request(ubulk->chip->index, REQT_READ, 
					DJ_GET_SERIAL_NUMBER_WORD_2, 0, 0, &serial_number_word2, 0);
			if (ret == 0) {
				atomic_set(&dc->serial_number,
					(serial_number_word1 << 16) + serial_number_word2);
				*serial_number = atomic_read(&dc->serial_number);
			} else {
				printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
			}
		}
	} else if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		dcs = ((struct hdj_steel_context *)ubulk->device_context);
		ret = get_bulk_data(ubulk, bulk_data, sizeof(bulk_data),0 /*force_send*/);
		if (ret == 0) {
			*serial_number = 
				(bulk_data[DJ_STEEL_EP_81_SERIAL_NUM_BITS_31_TO_24] << 24) + 
				(bulk_data[DJ_STEEL_EP_81_SERIAL_NUM_BITS_23_TO_16] << 16) +
				(bulk_data[DJ_STEEL_EP_81_SERIAL_NUM_BITS_15_TO_08] << 8) +
				(bulk_data[DJ_STEEL_EP_81_SERIAL_NUM_BITS_07_TO_00]);

			atomic_set(&dcs->serial_number,*serial_number);
		} else {
			printk(KERN_ERR"%s get_bulk_data failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	if (ret != 0) {
		printk(KERN_ERR"%s: ret: %d, serial_number: 0x%X\n",
			__FUNCTION__,
			ret,
			*serial_number);
	}
	return ret;
}

int set_serial_number(struct usb_hdjbulk *ubulk, u32 serial_number)
{
	u16 serial_number_word1 = 0;
	u16 serial_number_word2 = 0;
	u32 actual_serial_number = 0;
	int ret = 0;

	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
		serial_number_word1 = (u16)(serial_number >> 16);
		serial_number_word2 = (u16)(serial_number);

		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, DJ_SET_SERIAL_NUMBER_WORD_1,
					 serial_number_word1, 0, NULL, 0);
		if (ret == 0) {
			ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, DJ_SET_SERIAL_NUMBER_WORD_2, 
							serial_number_word2, 0, NULL, 0);
			if (ret == 0) {
				/*give the device enough time to write the number*/
				msleep(100);

				/*verify if the serial number was properly set- also saves it in context*/
				ret = get_serial_number(ubulk, &actual_serial_number);
				if (ret == 0 && actual_serial_number != serial_number) {
					printk(KERN_ERR"%s failure, tried to set serial:%x, actually set:%x\n",
						__FUNCTION__,serial_number,actual_serial_number);
					ret = -EIO;
				}
			} else {
				printk(KERN_ERR"%s send_vendor_request(0) failed, rc:%d\n",__FUNCTION__,ret);
			}
		} else {
			printk(KERN_ERR"%s send_vendor_request failed(1), rc:%d\n",__FUNCTION__,ret);
		}

		if (ret != 0) {
			printk(KERN_ERR"%s: Error setting the serial number: %d\n", __FUNCTION__,ret);
		}
	} else if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		ret = set_djcontrolsteel_non_volatile_data(ubulk,
					atomic_read(&ubulk->hdj_common.firmware_version), 
					serial_number, MIDI_INVALID_CHANNEL);
		if (ret!=0) {
			printk(KERN_ERR"%s set_djcontrolsteel_non_volatile_data failed, rc:%d\n",
				__FUNCTION__,
				ret);
		} else {
			/*give the device enough time to write the number*/
			msleep(100);

			/*verify if the serial number was properly set- also saves it in context*/
			ret = get_serial_number(ubulk, &actual_serial_number);
			if (ret == 0 && actual_serial_number != serial_number) {
				printk(KERN_ERR"%s failure, tried to set serial:%x, actually set:%x\n",
						__FUNCTION__,serial_number,actual_serial_number);
				ret = -EIO;
			}
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	if (ret != 0) {
		printk(KERN_ERR"%s: Error setting the serial number: %d\n", __FUNCTION__,ret);
	}
	return ret;
}

int get_device_mode(struct usb_hdjbulk *ubulk, u8 *device_mode)
{
	int ret = -EINVAL;

	if (ubulk->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		*device_mode = atomic_read(&((struct hdj_steel_context*)ubulk->device_context)->device_mode);	
		if (*device_mode != DJ_STEEL_IN_UNKNOWN_MODE) {
			ret = 0;
		}
	}
	return ret;
}

int get_talkover_state(struct usb_hdjbulk *ubulk,u16 * talkover_att, u8 query_hardware)
{
	int ret = -EINVAL;
	struct hdj_mk2_rmx_context *dc;
	
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
		ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context*)ubulk->device_context);
		
		if (query_hardware) {
			ret = send_vendor_request(ubulk->chip->index, 
						REQT_READ, DJ_GET_TALKOVER, 0, 0, talkover_att, 0);
			if (ret == 0) {
				atomic_set(&dc->talkover_atten,*talkover_att); 
			} else {
				printk(KERN_ERR"%s send_vendor_request() failed, rc:%d\n",__FUNCTION__,ret);
			}
		} else {
			*talkover_att = atomic_read(&dc->talkover_atten);
			ret = 0;
		}
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
	}
	
	return ret;
}

int get_talkover_att(struct usb_hdjbulk *ubulk, u16 * talkover_att, u8 query_hardware)
{
	int ret = -EINVAL;
	u16 talkover_mask, talkover_threshold, device_config, talkover_divisor;
	struct hdj_mk2_rmx_context *dc;
	struct hdj_console_context* dc0;
	
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
		ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context*)ubulk->device_context);
		
		if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
			talkover_mask = DJMK2_TALKOVER_ATT_MASK_VALUE;
			talkover_threshold = 1;
			talkover_divisor = 2;
		} else if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
			talkover_mask = DJRMX_TALKOVER_ATT_MASK_VALUE;
			talkover_threshold = 0;
			talkover_divisor = 1;
		} else {
			printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
			return -EINVAL;
		}
		/* if talkover is disabled, returned the cached attenuation */
		if ((atomic_read(&dc->talkover_atten)&talkover_mask)==talkover_threshold) {
				*talkover_att = (atomic_read(&dc->cached_talkover_atten)&talkover_mask)/talkover_divisor;
				return 0;
		}
		
		if (query_hardware) {
			ret = send_vendor_request(ubulk->chip->index, 
						REQT_READ, DJ_GET_TALKOVER, 0, 0, talkover_att, 0);
			if (ret == 0) {
				atomic_set(&dc->talkover_atten,*talkover_att);
				*talkover_att &= talkover_mask;
				*talkover_att /= talkover_divisor;
			} else {
				printk(KERN_ERR"%s send_vendor_request() failed, rc:%d\n",__FUNCTION__,ret);
			}
		} else {
			*talkover_att = (atomic_read(&dc->talkover_atten)&talkover_mask)/talkover_divisor;
			ret = 0;
		}
	} else if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		dc0 = ((struct hdj_console_context*)ubulk->device_context);
		talkover_threshold = 0;
		talkover_divisor = 2;
		talkover_mask = DJC_AUDIOCFG_TALKOVER_ATT_VAL;
		
		device_config = atomic_read(&dc0->device_config);
		device_config &= talkover_mask;
		device_config >>= 8;
		device_config /= talkover_divisor;
		/* if talkover is disabled, returned the cached attenuation */
		if (device_config==talkover_threshold) {
				*talkover_att = ((atomic_read(&dc0->cached_talkover_atten)&talkover_mask)>>8)/talkover_divisor;
				return 0;
		}
		
		*talkover_att = (device_config&talkover_mask)>>8;
		*talkover_att /= talkover_divisor; /* hw units in 0.5 dB increments */
		ret = 0;
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
	}
	
	return ret;
}

/* Note: there is no mask passed along with the talkover attenuation, just the 
 *  talkover attenuation */
int set_talkover_att(struct usb_hdjbulk *ubulk, u16 talkover_att)
{
	int ret = -EINVAL;
	struct hdj_mk2_rmx_context *dc;
	struct hdj_console_context* dc0;
	u16 tover_to_set, to_prop_change, device_config;
	
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
		ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context*)ubulk->device_context);
		
		if (ubulk->chip->product_code==DJCONSOLE2_PRODUCT_CODE &&
			(((talkover_att&DJMK2_TALKOVER_ATT_MASK_VALUE)==0) ||
			 ((talkover_att&DJMK2_TALKOVER_ATT_MASK_VALUE)==1))) {
				printk(KERN_WARNING"%s() set_talkover_att: Mk2: attenuation of 0 or 1 forbidden\n",
					__FUNCTION__);
				return -EINVAL;	
		}
		
		/* read the talkover byte (which contains other settings as well as the
		 *  attenuation */
		tover_to_set = atomic_read(&dc->talkover_atten);
		
		/* put in the new attenuation setting into the talkover byte */
		if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
				to_prop_change = talkover_att&DJMK2_TALKOVER_ATT_MASK_VALUE;
				talkover_att *= 2; /* hw units in 0.5 dB increments */
				tover_to_set &= ~DJMK2_TALKOVER_ATT_MASK_VALUE;
				tover_to_set |= talkover_att&DJMK2_TALKOVER_ATT_MASK_VALUE;
				/* set the correct talkover attenuation mask */
				tover_to_set = (tover_to_set<<8)|DJMK2_TALKOVER_ATT_MASK_VALUE;
		} else if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
				to_prop_change = talkover_att&DJRMX_TALKOVER_ATT_MASK_VALUE;
				tover_to_set &= ~DJRMX_TALKOVER_ATT_MASK_VALUE;
				tover_to_set |= talkover_att&DJRMX_TALKOVER_ATT_MASK_VALUE;
				/* set the correct talkover attenuation mask */
				tover_to_set = (tover_to_set<<8)|DJRMX_TALKOVER_ATT_MASK_VALUE;
		} else {
			printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
			return -EINVAL;
		}
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, 
						DJ_SET_TALKOVER, tover_to_set, 0, NULL, 0);
		if (ret == 0) {
			atomic_set(&dc->talkover_atten,(tover_to_set>>8)&0xff);
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_TALKOVER_ATTEN,
							to_prop_change);
		}
	} else if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		dc0 = ((struct hdj_console_context*)ubulk->device_context);
		talkover_att &= (DJC_AUDIOCFG_TALKOVER_ATT_VAL>>8);
		to_prop_change = talkover_att;
		talkover_att *= 2; /* hw units in 0.5 dB increments */
		talkover_att <<= 8;
		device_config = atomic_read(&dc0->device_config);
		device_config &= ~DJC_AUDIOCFG_TALKOVER_ATT_VAL;
		device_config |= talkover_att;
		ret = set_djconsole_device_config(ubulk->chip->index,
								(device_config<<16)|DJC_AUDIOCFG_TALKOVER_ATT_VAL, 1);
		if (ret == 0) {
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_TALKOVER_ATTEN,
							to_prop_change);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	return ret;
}

/* Turns on talkover in hardware */
int activate_talkover(struct usb_hdjbulk *ubulk)
{
	int ret = -EINVAL;
	u16 tover_to_set;
	u16 device_config;
	struct hdj_mk2_rmx_context *dc;
	struct hdj_console_context *dc2;
	
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
		ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context*)ubulk->device_context);
		
		if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
			tover_to_set = DJMK2_TALKOVER_ATT_ENABLE | (DJMK2_TALKOVER_ATT_ENABLE<<8);
		} else if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
			tover_to_set = DJRMX_TALKOVER_ATT_ENABLE | (DJRMX_TALKOVER_ATT_ENABLE<<8);
		} else {
			printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,
						ubulk->chip->product_code);
			return -EINVAL;
		}
		
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, 
								DJ_SET_TALKOVER, tover_to_set, 0, NULL, 0);
		if (ret == 0) {
			atomic_set(&dc->talkover_atten,tover_to_set);
		}
	} else if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		dc2 = ((struct hdj_console_context*)ubulk->device_context);
		device_config = atomic_read(&dc2->device_config);
		device_config |= DJC_AUDIOCFG_TALKOVER_ATT_ENABLE;
		ret = set_djconsole_device_config(ubulk->chip->index,
					(device_config<<16)|DJC_AUDIOCFG_TALKOVER_ATT_ENABLE,1);
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
	}
	
	return ret;
}

int set_talkover_enable(struct usb_hdjbulk *ubulk, u8 enable)
{
	int ret = -EINVAL;
	u16 tover_to_set, device_config;
	u16 talkover_mask, talkover_threshold, to_3db, to_divisor=1;
	struct hdj_mk2_rmx_context *dc;
	struct hdj_console_context* dc0;
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
		ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context*)ubulk->device_context);
		
		if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
			talkover_mask = DJMK2_TALKOVER_ATT_MASK_VALUE;
			talkover_threshold = 1;
			to_divisor = 2;
			to_3db = 6;
		} else if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
			talkover_mask = DJRMX_TALKOVER_ATT_MASK_VALUE;
			talkover_threshold = 0;
			to_divisor = 1;
			to_3db = 3;
		} else {
			printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
			return -EINVAL;
		}
		
		tover_to_set = atomic_read(&dc->talkover_atten);
		
		if (enable==0) {
			if ((tover_to_set&talkover_mask) == talkover_threshold) {
				/* already disabled, bail */
				return 0;
			}
			
			/* save talkover attenuation for restore if we reenable talkover */
			atomic_set(&dc->cached_talkover_atten, tover_to_set&talkover_mask);
					
			/* Mk2: setting a talkover attenenuation of 1 effectively means 0 dB 
			 *       to the device */
			/* Rmx: set 0 */
			tover_to_set &= ~talkover_mask;
			tover_to_set <<= 8; 
			tover_to_set |= (talkover_threshold<<8) | talkover_mask; 
		} else {
			if ((tover_to_set&talkover_mask) > talkover_threshold) {
				/* already enabled, bail */
				return 0;
			}
			
			/* reenable the last value */
			if (atomic_read(&dc->cached_talkover_atten)==0) {
					atomic_set(&dc->cached_talkover_atten,to_3db);
			}
			tover_to_set &= ~talkover_mask;
			tover_to_set <<= 8;
			tover_to_set |= (atomic_read(&dc->cached_talkover_atten)<<8)|talkover_mask;
		} 
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, 
								DJ_SET_TALKOVER, tover_to_set, 0, NULL, 0);
		if (ret == 0) {
			atomic_set(&dc->talkover_atten,(tover_to_set>>8)&0xff);
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_TALKOVER_ENABLE,
							enable==0?0:1);
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_TALKOVER_ATTEN,
							((tover_to_set>>8)&talkover_mask)/to_divisor);
		}
	} else if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		dc0 = ((struct hdj_console_context*)ubulk->device_context);
		talkover_threshold = 0;
		to_divisor = 2;
		talkover_mask = DJC_AUDIOCFG_TALKOVER_ATT_VAL;
		
		device_config = atomic_read(&dc0->device_config);
		if (enable==0) {
			if (((device_config&talkover_mask)>>8)==talkover_threshold) {
				/* already disabled, bail */
				return 0;
			}
						
			/* save talkover attenuation for restoration if we reenable talkover */
			atomic_set(&dc0->cached_talkover_atten, device_config&talkover_mask);
			
			/* this will turn off TA */
			device_config &= ~talkover_mask;	
		} else {
			if ((device_config&talkover_mask)!=0) {
				/* already enabled, bail */
				return 0;
			}
			to_3db = ((3*to_divisor)<<8); 
			/* reenable the last value */
			if (atomic_read(&dc0->cached_talkover_atten)==0) {
					atomic_set(&dc0->cached_talkover_atten,to_3db);
			}
			device_config &= ~talkover_mask;
			device_config |= atomic_read(&dc0->cached_talkover_atten);	
		}
		ret = set_djconsole_device_config(ubulk->chip->index,
											(device_config<<16)|talkover_mask,1);
		if (ret == 0) {
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_TALKOVER_ENABLE,
							enable==0?0:1);
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_TALKOVER_ATTEN,
							((device_config&DJC_AUDIOCFG_TALKOVER_ATT_VAL)>>8)/to_divisor);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
	}
	
	return ret;
}

int get_talkover_enable(struct usb_hdjbulk *ubulk, u8 *enable)
{
	int ret = 0;
	u16 tover;
	u16 talkover_mask, talkover_threshold, device_config;
	struct hdj_mk2_rmx_context *dc;
	struct hdj_console_context* dc0;
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
		ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context*)ubulk->device_context);
		
		if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
			talkover_mask = DJMK2_TALKOVER_ATT_MASK_VALUE;
			talkover_threshold = 1;
		} else if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
			talkover_mask = DJRMX_TALKOVER_ATT_MASK_VALUE;
			talkover_threshold = 0;
		} else {
			printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
			return -EINVAL;
		}
		
		tover = atomic_read(&dc->talkover_atten);
		if ((tover&talkover_mask) == talkover_threshold) {
			*enable = 0;
		} else {
			*enable = 1;	
		}
	} else if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		dc0 = ((struct hdj_console_context*)ubulk->device_context);
		device_config = atomic_read(&dc0->device_config);
		if (device_config&DJC_AUDIOCFG_TALKOVER_ATT_ENABLE) {
			*enable = 1;
		} else {
			*enable = 0;
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	return ret;
}

int get_firmware_version(struct snd_hdj_chip* chip, 
							u16 * firmware_version, 
							u8 query_hardware)
{
	int ret = -EINVAL; 
	struct usb_hdjbulk *ubulk;
	u8 bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];
	struct hdj_steel_context* dc;
	
	if (query_hardware) {
		if (chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
			ubulk = bulk_from_chip(chip);
			if (ubulk!=NULL) {
				dc = ((struct hdj_steel_context*)ubulk->device_context);
				if (atomic_read(&dc->device_mode) == DJ_STEEL_IN_NORMAL_MODE) {
					ret = get_bulk_data(ubulk, bulk_data, sizeof(bulk_data),0/*force_send*/);
					if (ret == 0) {
						*firmware_version = bulk_data[DJ_STEEL_EP_81_FIRMWARE_VERSION];
						atomic_set(&ubulk->hdj_common.firmware_version,*firmware_version);
					} else {
						printk(KERN_ERR"%s get_bulk_data failed, rc:%d\n",
							__FUNCTION__,ret);
					}
				}
			} else {
				printk(KERN_WARNING"%s() bulk_from_chip failed\n",__FUNCTION__);
				ret = -EINVAL;
			}
		} else if (chip->product_code == DJCONSOLE_PRODUCT_CODE ||
					chip->product_code == DJCONSOLE2_PRODUCT_CODE ||
					chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
			ubulk = bulk_from_chip(chip);
			if (ubulk!=NULL) {
				ret = send_vendor_request(ubulk->chip->index, REQT_READ, 
							DJ_VERSION_REQUEST, 0, 0, firmware_version, 0);
				if (ret == 0) {
					atomic_set(&ubulk->hdj_common.firmware_version,*firmware_version);
				} else {
					printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",
						__FUNCTION__,ret);
				}
			} else {
				printk(KERN_WARNING"%s() bulk_from_chip failed\n",__FUNCTION__);
				ret = -EINVAL;
			}
		} else if (chip->product_code == DJCONTROLLER_PRODUCT_CODE) {
			*firmware_version = le16_to_cpu(chip->dev->descriptor.bcdDevice);
			ret = 0;
		} else {
			printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,chip->product_code);
			ret = -EINVAL;
		}
	} else {
		if (chip->product_code == DJCONSOLE_PRODUCT_CODE ||
					chip->product_code == DJCONSOLE2_PRODUCT_CODE ||
					chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
					chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
			ubulk = bulk_from_chip(chip);
			if (ubulk!=NULL) {
				*firmware_version = atomic_read(&ubulk->hdj_common.firmware_version);
				ret = 0;
			} else {
				printk(KERN_WARNING"%s() bulk_from_chip failed\n",__FUNCTION__);
				ret = -EINVAL;
			}
		} else if (chip->product_code == DJCONTROLLER_PRODUCT_CODE) {
			*firmware_version = le16_to_cpu(chip->dev->descriptor.bcdDevice);
			ret = 0;
		} else {
			printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,chip->product_code);
			ret = -EINVAL;
		}
	}
	
	return ret;
}

/*
 * Set the mode shift state 
 *mode_shift_state:	Bit7: Mstate_DA
 *			Bit6: Mstate_DB
 *			Bit 5 to 0: Don't care
 */
int set_mode_shift_state(struct usb_hdjbulk * ubulk,
			   u8 mode_shift_state)
{
	int ret = 0;
	u8 bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {		
		/* prepare request */
		memset(bulk_data, 0, sizeof(bulk_data));
		bulk_data[0] = DJ_STEEL_SET_MODE_SHIFT;
		bulk_data[1] = mode_shift_state;

		ret = send_bulk_write(ubulk, bulk_data, sizeof(bulk_data), 0 /* force_send */);
		if (ret==0) {
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_SHIFT_MODE_STATE,
							mode_shift_state);
		} else {
			printk(KERN_ERR"%s send_bulk_write failed, rc:%d\n",
				__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	return ret;
}

/*
 *Get the mode shift state 
 *mode_shift_state:	Bit7: Mstate_DA
 *			Bit6: Mstate_DB
 *			Bit 5 to 0: Don't care
 */
int get_mode_shift_state(struct usb_hdjbulk * ubulk,
			   u8* mode_shift_state)
{
	int ret = 0;
	u8 bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		ret = get_bulk_data(ubulk, bulk_data, sizeof(bulk_data),0/*force_send*/);
		if (ret == 0) {
			/*get the mode shift state*/
			*mode_shift_state = bulk_data[DJ_STEEL_EP_81_MODE_SHIFT_STATE];
		} else {
			printk(KERN_ERR"%s get_bulk_data failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	/*printk(KERN_INFO"%s: ret: %d, Mode Shift State: 0x%02X\n",
		__FUNCTION__,ret, *mode_shift_state);*/

	return ret;
}

/*
 *set the FX state
 *fx_state:	Byte0:	Bit7: STEEL_Deck_B
 *			Bit6: STEEL_Deck_A
 *			Bit5 to 0: Don't care
 *		Byte1:	Bit7: STEEL_Lock
 *			Bit6: STEEL_Master
 *			Bit5 to 0: Don't care
 */
int set_fx_state(struct usb_hdjbulk * ubulk,
		    u16 fx_state)
{
	int ret = 0;
	u8 bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {		
		/* prepare request */
		memset(bulk_data, 0, sizeof(bulk_data));
		bulk_data[0] = DJ_STEEL_SET_FX_STATE;
		bulk_data[1] = (u8)(fx_state >> 8);
		bulk_data[2] = (u8)fx_state;

		ret = send_bulk_write(ubulk, bulk_data, sizeof(bulk_data), 0 /* force send */);
		if (ret==0) {
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_FX_STATE,
							fx_state);
		} else {
			printk(KERN_ERR"%s send_bulk_write failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	return ret;
}

/*
 *get the FX state
 *fx_state:	Byte0:	Bit7: STEEL_Deck_B
 *			Bit6: STEEL_Deck_A
 *			Bit5 to 0: Don't care
 *		Byte1:	Bit7: STEEL_Lock
 *			Bit6: STEEL_Master
 *			Bit5 to 0: Don't care
 */
int get_fx_state(struct usb_hdjbulk * ubulk,
		    u16* fx_state)
{
	int ret = 0;
	u8 bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {	
		ret = get_bulk_data(ubulk, bulk_data, sizeof(bulk_data),0 /*force_send*/);
		if (ret == 0) {
			/*get the fx state*/
			*fx_state = bulk_data[DJ_STEEL_EP_81_FX_STATE_1];
			*fx_state = (bulk_data[DJ_STEEL_EP_81_FX_STATE_0] << 8) + (*fx_state & 0xFF);
		} else {
			printk(KERN_ERR"%s get_bulk_data failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}

	/*printk(KERN_INFO"%s): ret: %d, state: 0x%04X\n",
		__FUNCTION__,ret, *fx_state);*/

	return ret;
}

int get_djconsole_device_config(int chip_index, u16 * device_config, u8 include_to)
{
	int ret;
	struct hdj_console_context* dc;
	struct usb_hdjbulk *ubulk;
	struct snd_hdj_chip* chip;
	
	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s no context, bailing!\n",__FUNCTION__);
		return -EINVAL;
	}
	
	if (chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip() returned NULL\n",__FUNCTION__);
			ret = -EINVAL;
		} else {	
			dc = ((struct hdj_console_context *)ubulk->device_context);
			/* there is no hardware get.  So acquire the current state from the state variable*/
			*device_config = atomic_read(&dc->device_config);
			
			/* We make these available through standard IOCTL for all devices which support
			 *  talkover attenuation/ talkover enable- blank out here */
			if (include_to==0) {
				*device_config &= ~DJC_AUDIOCFG_TALKOVER_ATT_VAL;
				*device_config &= ~DJC_AUDIOCFG_TALKOVER_ATT_ENABLE;
			}
			ret = 0;
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,chip->product_code);
		ret = -EINVAL;
	}
	dec_chip_ref_count(chip_index);
	
	return ret;
}

int set_djconsole_device_config(int chip_index, u32 device_config, u8 include_to)
{
	int ret = -EINVAL;
	u16 old_device_config=0, curr_device_config;
	u16 new_device_config_mask = device_config & 0xffff;
	u16 new_device_config = (device_config >> 16) & 0xffff;
	u16 curr_bit;
	int i;
	struct hdj_console_context *dc;
	struct usb_hdjbulk *ubulk;
	struct snd_hdj_chip* chip;
	
	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s no context, bailing!\n",__FUNCTION__);
		return -EINVAL;
	}
	
	if (chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip() returned NULL\n",__FUNCTION__);
			ret = -EINVAL;
			goto set_djconsole_device_config_bail;
		}
		/* acquire the DJ Console context */
		dc = (struct hdj_console_context *)ubulk->device_context;
		
		/* We make these available through standard IOCTL for all devices which support
		 *  talkover attenuation/ talkover enable- blank out here, if asked to */
		if (include_to==0) {
			new_device_config &= ~DJC_AUDIOCFG_TALKOVER_ATT_VAL;
			new_device_config &= ~DJC_AUDIOCFG_TALKOVER_ATT_ENABLE;
			new_device_config_mask &= ~DJC_AUDIOCFG_TALKOVER_ATT_VAL;
			new_device_config_mask &= ~DJC_AUDIOCFG_TALKOVER_ATT_ENABLE;
		}

		/* since we operate with bitmask operations on our device config, we must serialize */
		down(&dc->device_config_mutex);

		/* cache the old device config*/
		curr_device_config = old_device_config = atomic_read(&dc->device_config);

		/* update device config */
		for (i=0;i<16;i++) {
			curr_bit = 1<<i;
			if ( (new_device_config_mask & curr_bit)) {
				if (new_device_config & curr_bit) {
					curr_device_config |= curr_bit;	
				} else {
					curr_device_config &= ~curr_bit;
				}
			}
		}

		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, 
						DJ_CONFIG_REQUEST, curr_device_config, 0, NULL, 0);		
		if (ret == 0) {
			atomic_set(&dc->device_config,curr_device_config);
			
			/* If the MIDI mode for the input has changed, then inform the MIDI driver- it will 
				then take care of updating its state and setting this in the hardware */
			if ((old_device_config&DJC_AUDIOCFG_MIDI_IN_BUTTONS) != (curr_device_config&DJC_AUDIOCFG_MIDI_IN_BUTTONS)) {
				if ((ret=snd_hdjmidi_set_midi_mode(ubulk->chip->index,
						(curr_device_config&DJC_AUDIOCFG_MIDI_IN_BUTTONS)!=0?
						MIDI_MODE_CONTROLLER:MIDI_MODE_PHYSICAL_PORT))!=0) {
					printk(KERN_ERR"%s snd_hdjmidi_set_midi_mode failed, mode:%d, rc:%d\n",
						__FUNCTION__,
						((curr_device_config)&DJC_AUDIOCFG_MIDI_IN_BUTTONS),
						ret);
				}
			}
			
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_DJ1_DEVICE_CONFIG,
							curr_device_config);
		} else {
			printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);

			/* restore old device config */
			atomic_set(&dc->device_config, old_device_config);
		}

		up(&dc->device_config_mutex);
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,chip->product_code);
		ret = -EINVAL;
	}
set_djconsole_device_config_bail:
	dec_chip_ref_count(chip_index);
	return ret;
}

int get_audio_config(struct usb_hdjbulk *ubulk, u16 * audio_config, u8 query_hardware)
{
	int ret;
	struct hdj_mk2_rmx_context *dc;
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
		ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context*)ubulk->device_context);
		if (query_hardware) {
			ret = send_vendor_request(ubulk->chip->index, REQT_READ, 
						DJ_GET_AUDIO_CONFIG, 0, 0, audio_config, 0);
			if (ret == 0) {
				atomic_set(&dc->audio_config,*audio_config);
			} else {
				printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
			}
		} else {
			*audio_config = atomic_read(&dc->audio_config);
			ret = 0;
		}
	} else {
		printk(KERN_WARNING"%s invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	return ret;
}

int set_audio_config(struct usb_hdjbulk *ubulk, u16 audio_config)
{
	int ret;
	struct hdj_mk2_rmx_context *dc;
	u16 i, curr_bit;
	u16 ac_to_set, ac_data_received=(audio_config >> 8)&0xff, 
		ac_mask_received=audio_config&0xff;
	if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
		ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context*)ubulk->device_context);
		
		ac_to_set = atomic_read(&dc->audio_config);
		for (i=0;i<8;i++) {
			curr_bit = 1 << i;
			if (ac_mask_received&curr_bit) {
					if (ac_data_received&curr_bit) {
						ac_to_set |= curr_bit;
					} else {
						ac_to_set &= ~curr_bit;
					}
			} 
		}
		
		/* this will be the new requested setting, with the value in the upper byte, and
		 *  the mask in the lower byte */
		ac_to_set = (ac_to_set << 8) | ac_mask_received;
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, DJ_SET_AUDIO_CONFIG, 
					ac_to_set, 0, NULL, 0);
		if (ret == 0) {
			atomic_set(&dc->audio_config,(ac_to_set>>8)&0xff);
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_AUDIO_CONFIG,
							(ac_to_set>>8)&0xff);
		} else {
			printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	return ret;
}

/* attempts to clear all LEDs based on product type */
int clear_leds(struct snd_hdj_chip* chip)
{
	int rc=0;
	struct usb_hdjbulk *ubulk;
	struct snd_hdjmidi* umidi;
	struct snd_hdjmidi_out_endpoint* ep;
	unsigned long flags;
	u32 data_len;
	long timeout = 0;
	if (chip->product_code==DJCONSOLE_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
			return -EINVAL;	
		}
		down(&ubulk->output_control_mutex);
		ubulk->output_control_buffer[0] = DJC_SET_REPORT_ID;
		ubulk->output_control_buffer[1] = 0;
		ubulk->output_control_buffer[2] = 0;	
		ubulk->output_control_buffer[3] &= ~3; /*preserves mouse*/
		rc = usb_set_report(ubulk,USB_HID_OUTPUT_REPORT,DJC_SET_REPORT_ID);
		up(&ubulk->output_control_mutex);	
	} else if (chip->product_code==DJCONSOLE2_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
			return -EINVAL;	
		}
		down(&ubulk->output_control_mutex);
		ubulk->output_control_buffer[0] = DJMK2_SET_REPORT_ID;
		ubulk->output_control_buffer[1] = 0;
		ubulk->output_control_buffer[2] = 0;	
		ubulk->output_control_buffer[3] &= ~3; /*preserves mouse*/
		rc = usb_set_report(ubulk,USB_HID_OUTPUT_REPORT,DJMK2_SET_REPORT_ID);
		up(&ubulk->output_control_mutex);
	} else if (chip->product_code==DJCONSOLERMX_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
			return -EINVAL;	
		}
		down(&ubulk->output_control_mutex);
		ubulk->output_control_buffer[0] = DJRMX_SET_REPORT_ID;
		ubulk->output_control_buffer[1] = 0;
		ubulk->output_control_buffer[2] = 0;	
		ubulk->output_control_buffer[3] = 0;
		ubulk->output_control_buffer[4] = 0;
		rc = usb_set_report(ubulk,USB_HID_OUTPUT_REPORT,DJRMX_SET_REPORT_ID);
		up(&ubulk->output_control_mutex);
	} else if (chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
			return -EINVAL;	
		}
		down(&ubulk->output_control_mutex);
		memset(ubulk->output_control_buffer,0,ubulk->output_control_buffer_size);
		ubulk->output_control_buffer[0] = DJ_STEEL_STANDARD_SET_LED_REPORT;
		rc = send_bulk_write(ubulk,
					ubulk->output_control_buffer, 
					ubulk->output_control_buffer_size,
					0 /* force_send */);
		up(&ubulk->output_control_mutex);
	} else if (chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		umidi = midi_from_chip(chip);
		if (umidi==NULL) {
			printk(KERN_WARNING"%s() midi_from_chip() returned NULL\n",__FUNCTION__);
			return EAGAIN;	
		}
		/* the mp3 only has 1 endpoint */
		ep = umidi->endpoints[0].out;
		if (ep==NULL) {
			printk(KERN_WARNING"%s() NULL endpoint bailing\n",__FUNCTION__);
			return -EINVAL;	
		}
		
		if (ep->controller_state!=NULL && ep->controller_state->output_control_ctl_urb!=NULL) {				
			/* serialize access to output control urb and its USB buffer */
			down(&ep->controller_state->output_control_ctl_mutex);
			
			/* here we must synchronize with our tasklet to acccess the hid control buffer-
			 *  just set state and copy the buffer */
			spin_lock_irqsave(&ep->buffer_lock, flags);
			ep->controller_state->current_hid_report_data[0] = DJ_MP3_HID_REPORT_ID;
			ep->controller_state->current_hid_report_data[1] = 0;
			ep->controller_state->current_hid_report_data[2] = 0;
			ep->controller_state->current_hid_report_data[3] &= ~3; /*preserves mouse*/
			data_len = ep->controller_state->current_hid_report_data_len > ep->max_transfer ?
				ep->max_transfer:ep->controller_state->current_hid_report_data_len;
			memcpy(ep->controller_state->output_control_ctl_urb->transfer_buffer,
				ep->controller_state->current_hid_report_data, data_len);
			spin_unlock_irqrestore(&ep->buffer_lock, flags);
			
			ep->controller_state->output_control_ctl_urb->dev = ep->umidi->chip->dev;
			ep->controller_state->output_control_ctl_urb->transfer_buffer_length = data_len;
			
			rc = snd_hdjmidi_submit_urb(ep->umidi, 
					ep->controller_state->output_control_ctl_urb, GFP_KERNEL);
			if (rc!=0) {
				printk(KERN_WARNING"%s snd_hdjmidi_submit_urb() failed, rc:%d\n",
						__FUNCTION__,rc);
			} else {
				/*wait for the completion of the urb*/
				timeout = wait_for_completion_interruptible_timeout(&ep->controller_state->output_control_ctl_completion, HZ);	
				if (timeout <= 0) {
					printk(KERN_ERR"%s() timed out: %ld\n", __FUNCTION__,timeout);
					rc = -EIO;

					/*kill the urb since it timed out*/
					usb_kill_urb(ep->controller_state->output_control_ctl_urb);
				}

				if (signal_pending(current)) {
					printk(KERN_WARNING"%s() signal pending\n",__FUNCTION__);
					/* we have been woken up by a signal- reflect this in the return code */
					rc = -ERESTARTSYS;
				}

				if (ep->controller_state->output_control_ctl_urb->status == -EPIPE) {
					printk(KERN_ERR"%s() urb->status == -EPIPE\n",__FUNCTION__);

					/*clear the pipe */
					usb_clear_halt(ep->umidi->chip->dev, 
								ep->controller_state->output_control_ctl_pipe);

					rc = -EPIPE;
				}
				/* required or next access may fail */
				msleep(10);	
			}
			up(&ep->controller_state->output_control_ctl_mutex);
		} else {
			printk(KERN_WARNING"%s() Invalid state\n",__FUNCTION__);
			rc = -EINVAL;	
		}
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,chip->product_code);
		rc = -EINVAL;
	}
	return rc;
}

int set_mouse_state(struct snd_hdj_chip* chip, u8 enable_mouse)
{
	int ret;
	u32 bmRequest;
	struct usb_hdjbulk *ubulk = NULL;
	struct snd_hdjmidi* umidi=NULL;
	struct hdj_mk2_rmx_context *dc;
	unsigned int byte_number, bit_number;
	struct snd_hdjmidi_out_endpoint* ep;
	unsigned long flags;
	u32 data_len;
	long timeout = 0;
	if (chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip() returned NULL\n",__FUNCTION__);
			return EAGAIN;	
		}
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		if (enable_mouse != 0) {
			/*enable the dj mouse*/
			bmRequest = DJ_ENABLE_MOUSE;
		} else {
			/*disable the dj mouse*/
			bmRequest = DJ_DISABLE_MOUSE;
		}
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, bmRequest, 0, 0, NULL, 0);
		if (ret == 0) {
			atomic_set(&dc->mouse_enabled, enable_mouse==1?1:0);
		} else {
			printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else if (chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip() returned NULL\n",__FUNCTION__);
			return EAGAIN;	
		}
		down(&ubulk->output_control_mutex);
		ubulk->output_control_buffer[0] = DJC_SET_REPORT_ID;
		if (enable_mouse) {
			ubulk->output_control_buffer[DJC_HID_OUT_DISABLE_MOUSE_BYTE_POS] &= 
					~(1 << DJC_HID_OUT_DISABLE_MOUSE_BIT_POS);
		} else {
			ubulk->output_control_buffer[DJC_HID_OUT_DISABLE_MOUSE_BYTE_POS] |= 
				(1 << DJC_HID_OUT_DISABLE_MOUSE_BIT_POS);
		}
		ret = usb_set_report(ubulk,USB_HID_OUTPUT_REPORT,DJC_SET_REPORT_ID);
		up(&ubulk->output_control_mutex);
		if (ret!=0) {
			printk(KERN_ERR"%s usb_set_report() failed, rc:%d\n",__FUNCTION__,ret);
		} 
	} else if (chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		umidi = midi_from_chip(chip);
		if (umidi==NULL) {
			printk(KERN_WARNING"%s() midi_from_chip() returned NULL\n",__FUNCTION__);
			return EAGAIN;	
		}
		/* the mp3 only has 1 endpoint */
		ep = umidi->endpoints[0].out;
		if (ep==NULL) {
			printk(KERN_WARNING"%s() NULL endpoint bailing\n",__FUNCTION__);
			return -EINVAL;	
		}
		if(atomic_read(&ep->umidi->chip->shutdown)==1) {
			return -ENODEV;
		}
		
		if (ep->controller_state!=NULL && ep->controller_state->output_control_ctl_urb!=NULL) {
			byte_number = ep->controller_state->control_details[MP3_OUT_DISABLE_MOUSE].byte_number;
			bit_number = ep->controller_state->control_details[MP3_OUT_DISABLE_MOUSE].bit_number;
				
			/* serialize access to output control urb and its USB buffer */
			down(&ep->controller_state->output_control_ctl_mutex);
			
			/* here we must synchronize with our tasklet to acccess the hid control buffer-
			 *  just set state and copy the buffer */
			spin_lock_irqsave(&ep->buffer_lock, flags);
			if (enable_mouse!=0) {
				ep->controller_state->current_hid_report_data[byte_number] &= ~(1<<bit_number);
			} else {
				ep->controller_state->current_hid_report_data[byte_number] |= 1<<bit_number;
			}
			data_len = ep->controller_state->current_hid_report_data_len > ep->max_transfer ?
				ep->max_transfer:ep->controller_state->current_hid_report_data_len;
			memcpy(ep->controller_state->output_control_ctl_urb->transfer_buffer,
				ep->controller_state->current_hid_report_data, data_len);
			spin_unlock_irqrestore(&ep->buffer_lock, flags);
			
			ep->controller_state->output_control_ctl_urb->dev = ep->umidi->chip->dev;
			ep->controller_state->output_control_ctl_urb->transfer_buffer_length = data_len;
			
			ret = snd_hdjmidi_submit_urb(ep->umidi, ep->controller_state->output_control_ctl_urb, GFP_KERNEL);
			if (ret!=0) {
				printk(KERN_WARNING"%s snd_hdjmidi_submit_urb() failed, rc:%d\n",__FUNCTION__,ret);
			} else {
				/*wait for the completion of the urb*/
				timeout = wait_for_completion_interruptible_timeout(&ep->controller_state->output_control_ctl_completion, HZ);	
				if (timeout <= 0) {
					printk(KERN_ERR"%s() timed out: %ld\n", __FUNCTION__,timeout);
					ret = -EIO;

					/*kill the urb since it timed out*/
					usb_kill_urb(ep->controller_state->output_control_ctl_urb);
				}

				if (signal_pending(current)) {
					printk(KERN_WARNING"%s() signal pending\n",__FUNCTION__);
					/* we have been woken up by a signal- reflect this in the return code */
					ret = -ERESTARTSYS;
				}

				if (ep->controller_state->output_control_ctl_urb->status == -EPIPE) {
					printk(KERN_ERR"%s() urb->status == -EPIPE\n",__FUNCTION__);

					/*clear the pipe */
					usb_clear_halt(ep->umidi->chip->dev, 
								ep->controller_state->output_control_ctl_pipe);

					ret = -EPIPE;
				}
				/* required or next access may fail */
				msleep(10);	
			}
			up(&ep->controller_state->output_control_ctl_mutex);
		} else {
			printk(KERN_WARNING"%s() Invalid state\n",__FUNCTION__);
			ret = -EINVAL;	
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	if (ret==0) {
		/* send control change notification to clients */
		send_control_change_over_netlink(chip,
						chip->product_code,
						CTRL_CHG_DJ_MOUSE_ENABLE,
						enable_mouse!=0?1:0);
	}
	return ret;
}

int get_mouse_state(struct snd_hdj_chip* chip, u16 * enable_mouse)
{
	int ret = 0;
	struct usb_hdjbulk *ubulk = NULL;
	struct snd_hdjmidi* umidi=NULL;
	struct hdj_mk2_rmx_context *dc;
	struct snd_hdjmidi_out_endpoint* ep;
	unsigned int byte_number, bit_number;
	unsigned long flags;
	if (chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip() returned NULL\n",__FUNCTION__);
			return EAGAIN;	
		}
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		/* get the mouse state from the device context (there is no hardware get)*/
		*enable_mouse = atomic_read(&dc->mouse_enabled);
	} else if (chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip() returned NULL\n",__FUNCTION__);
			return EAGAIN;	
		}
		down(&ubulk->output_control_mutex);
		if ((ubulk->output_control_buffer[DJC_HID_OUT_DISABLE_MOUSE_BYTE_POS] & 
		    (1<<DJC_HID_OUT_DISABLE_MOUSE_BIT_POS))) {
			*enable_mouse = 0;
		} else {
			*enable_mouse = 1;
		}
		up(&ubulk->output_control_mutex);
	} else if (chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		umidi = midi_from_chip(chip);
		if (umidi==NULL) {
			printk(KERN_WARNING"%s() midi_from_chip() returned NULL\n",__FUNCTION__);
			return EAGAIN;	
		}
		/* the mp3 only has 1 endpoint */
		ep = umidi->endpoints[0].out;
		if (ep==NULL || ep->controller_state==NULL) {
			printk(KERN_WARNING"%s() bad endpoint state\n",__FUNCTION__);
			return -EINVAL;	
		}
		
		/* we synchronize with our tasklet to access the HID control buffer */
		spin_lock_irqsave(&ep->buffer_lock, flags);
		if (ep->controller_state!=NULL) {
			byte_number = ep->controller_state->control_details[MP3_OUT_DISABLE_MOUSE].byte_number;
			bit_number = ep->controller_state->control_details[MP3_OUT_DISABLE_MOUSE].bit_number;
			
			if ((ep->controller_state->current_hid_report_data[byte_number]&(1<<bit_number))) {
				*enable_mouse = 0;
			} else {
				*enable_mouse = 1;
			}
			ret = 0;
		} 
		spin_unlock_irqrestore(&ep->buffer_lock, flags);
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	return ret;
}

int get_output_control_data_len(struct snd_hdj_chip* chip, u32* len)
{
	struct snd_hdjmidi* umidi=NULL;
	struct usb_hdjbulk *ubulk=NULL;
	if (chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		umidi = midi_from_chip(chip);
		if(umidi==NULL) {
			printk(KERN_WARNING"%s() midi_from_chip() returned NULL\n",__FUNCTION__);
			return -ENODEV;
		}
		/* assume 1 output endpoint- true for all of our devices */
		if (umidi->endpoints[0].out!=NULL &&
			 umidi->endpoints[0].out->controller_state!=NULL) {
			/* subtract 1 for the report ID */
			*len = umidi->endpoints[0].out->controller_state->current_hid_report_data_len-1;
			return 0;
		}
	} else if (chip->product_code == DJCONSOLE_PRODUCT_CODE || 
				chip->product_code == DJCONSOLE2_PRODUCT_CODE ||
				chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
				chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
			return -EINVAL;	
		}	
		/* subtract 1 for the report ID */	
		*len = ubulk->output_control_buffer_size-1;
		return 0;
	} else {
		printk(KERN_WARNING"%s() unsupported product:%d\n",
					__FUNCTION__,chip->product_code);
	}
	return -EINVAL;
}

/* Note that we include the report ID */
int get_input_control_data_len(struct snd_hdj_chip* chip, u32* len)
{
	struct snd_hdjmidi* umidi=NULL;
	struct usb_hdjbulk *ubulk=NULL;
	if (chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		umidi = midi_from_chip(chip);
		if(umidi==NULL) {
			printk(KERN_WARNING"%s() midi_from_chip() returned NULL\n",__FUNCTION__);
			return -ENODEV;
		}
		/* assume 1 input endpoint- true for all of our devices */
		if (umidi->endpoints[0].in!=NULL &&
			 umidi->endpoints[0].in->controller_state!=NULL) {
			*len = umidi->endpoints[0].in->controller_state->current_hid_report_data_len;
			return 0;
		}
	} else if (chip->product_code == DJCONSOLE_PRODUCT_CODE || 
				chip->product_code == DJCONSOLE2_PRODUCT_CODE ||
				chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
				chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		ubulk = bulk_from_chip(chip);
		if (ubulk==NULL) {
			printk(KERN_WARNING"%s() bulk_from_chip returned NULL\n",__FUNCTION__);
			return -EINVAL;	
		}
		*len = ubulk->continuous_reader_packet_size;
		return 0;
	} else {
		printk(KERN_WARNING"%s() unsupported product:%d\n",
					__FUNCTION__,chip->product_code);
	}
	return -EINVAL;
}

int send_control_output_report(struct snd_hdj_chip* chip, 
								u8 *masked_buffer,
								u32 buffer_len) 
{
	u8 * data_to_set_mask;
	u8 * data_to_set;
	u16 curr_byte, curr_bit, curr_bit_mask;
	u32 data_len, control_len;
	long timeout=0;
	struct usb_hdjbulk *ubulk=NULL;
	struct snd_hdjmidi* umidi;
	struct snd_hdjmidi_out_endpoint* ep; 
	struct controller_output_hid *controller_state;
	unsigned long flags;
	int rc=-EINVAL;
	
	rc = get_output_control_data_len(chip,&control_len);
	if (rc!=0) {
		printk(KERN_WARNING"%s() get_output_control_data_len() failed, rc:%d\n",
				__FUNCTION__,rc);
		return rc;	
	}
	
	if (buffer_len<(control_len*2)) {
		printk(KERN_WARNING"%s() buffer too short:%u, minimum required:%d\n",
				__FUNCTION__,buffer_len,control_len*2);
		return -EINVAL;	
	}

	if (chip->product_code == DJCONSOLE_PRODUCT_CODE || 
	    chip->product_code == DJCONSOLE2_PRODUCT_CODE ||
	    chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
	    chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
	    ubulk = bulk_from_chip(chip);
	    if (ubulk==NULL) {
	    	printk(KERN_WARNING"%s() bulk_from_chip() returned NULL\n",__FUNCTION__);
	    	return -ENODEV;	
	    }
		down(&ubulk->output_control_mutex);
		data_to_set_mask = masked_buffer + control_len;
		data_to_set = masked_buffer;
		for (curr_byte=0;curr_byte<control_len;curr_byte++) {
			for (curr_bit=0;curr_bit<8;curr_bit++) {
				curr_bit_mask = 1<<curr_bit ;
				if (data_to_set_mask[curr_byte]&curr_bit_mask) {
					if (data_to_set[curr_byte]&curr_bit_mask) {
						/* +1 to byte num because we store the report ID, and the client does
						 *  not send it */
						ubulk->output_control_buffer[curr_byte+1] |= curr_bit_mask;
					} else {
						/* +1 to byte num because we store the report ID, and the client does
						 *  not send it */
						ubulk->output_control_buffer[curr_byte+1] &= ~curr_bit_mask;
					}
				}
			}
		}
		
		/* now send the data */
		if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
			rc = usb_set_report(ubulk,USB_HID_OUTPUT_REPORT,DJC_SET_REPORT_ID);
		} else if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
			rc = usb_set_report(ubulk,USB_HID_OUTPUT_REPORT,DJMK2_SET_REPORT_ID);
		} else if (ubulk->chip->product_code == DJCONSOLERMX_PRODUCT_CODE) {
			rc = usb_set_report(ubulk,USB_HID_OUTPUT_REPORT,DJRMX_SET_REPORT_ID);
		} else if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
			rc = send_bulk_write(ubulk,
						ubulk->output_control_buffer,
						ubulk->output_control_buffer_size,
						0 /*force_send*/);
		} else {
			printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
			rc = -EINVAL;
		}
		up(&ubulk->output_control_mutex);	
	}  else if (chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		umidi = midi_from_chip(chip);
		if (umidi==NULL) {
			printk(KERN_WARNING"%s() midi_from_chip() returned NULL\n",__FUNCTION__);
	    	return -ENODEV;	
		} 
		/* assume 1 output endpoint- true for all of our devices */
		if (umidi->endpoints[0].out==NULL ||
			 umidi->endpoints[0].out->controller_state==NULL) {
			printk(KERN_WARNING"%s() invalid state\n",__FUNCTION__);
	    	return -EINVAL;		 	
		}
		ep = umidi->endpoints[0].out;
		controller_state = ep->controller_state;
		
		data_to_set_mask = masked_buffer + control_len;
		data_to_set = masked_buffer;
		
		/* serialize access to output control urb and its USB buffer */
		down(&controller_state->output_control_ctl_mutex);
		data_len = controller_state->current_hid_report_data_len > ep->max_transfer ?
				ep->max_transfer:controller_state->current_hid_report_data_len;
		
		/* here we must synchronize with our tasklet- just copy the buffer to the
		 *  URB's USB buffer and continue */
		spin_lock_irqsave(&ep->buffer_lock, flags);
		memcpy(controller_state->output_control_ctl_urb->transfer_buffer,
				controller_state->current_hid_report_data,data_len);
		spin_unlock_irqrestore(&ep->buffer_lock, flags);
		
		for (curr_byte=0;curr_byte<data_len-1;curr_byte++) {
			for (curr_bit=0;curr_bit<8;curr_bit++) {
				curr_bit_mask = 1<<curr_bit ;
				if (data_to_set_mask[curr_byte]&curr_bit_mask) {
					if (data_to_set[curr_byte]&curr_bit_mask) {
						/* +1 to byte num because we store the report ID, and the client does
						 *  not send it */
						((u8*)(controller_state->output_control_ctl_urb->transfer_buffer))[curr_byte+1] 
								|= curr_bit_mask;
					} else {
						/* +1 to byte num because we store the report ID, and the client does
						 *  not send it */
						((u8*)(controller_state->output_control_ctl_urb->transfer_buffer))[curr_byte+1] 
								&= ~curr_bit_mask;
					}
				}
			}
		}
		controller_state->output_control_ctl_urb->dev = ep->umidi->chip->dev;
		controller_state->output_control_ctl_urb->transfer_buffer_length = data_len;
		rc = snd_hdjmidi_submit_urb(umidi, controller_state->output_control_ctl_urb, GFP_KERNEL);
		if (rc!=0) {
			printk(KERN_WARNING"%s snd_hdjmidi_submit_urb() failed, rc:%d\n",__FUNCTION__,rc);
		} else {
			/*wait for the completion of the urb*/
			timeout = wait_for_completion_interruptible_timeout(&controller_state->output_control_ctl_completion, HZ);	
			if (timeout <= 0) {
				printk(KERN_ERR"%s() timed out: %ld\n", __FUNCTION__,timeout);
				rc = -EIO;
				/*kill the urb since it timed out*/
				usb_kill_urb(controller_state->output_control_ctl_urb);
			}

			if (signal_pending(current)) {
				printk(KERN_WARNING"%s() signal pending\n",__FUNCTION__);
				/* we have been woken up by a signal- reflect this in the return code */
				rc = -ERESTARTSYS;
			}

			if (controller_state->output_control_ctl_urb->status == -EPIPE) {
				printk(KERN_ERR"%s() urb->status == -EPIPE\n",__FUNCTION__);

				/*clear the pipe */
				usb_clear_halt(umidi->chip->dev, 
								controller_state->output_control_ctl_pipe);
				rc = -EPIPE;
			}	
			/* required or next access may fail */
			msleep(10);
		}
		up(&controller_state->output_control_ctl_mutex);
	} else {
		/* invalid product */
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,chip->product_code);
		rc = -EINVAL;
	}
	if (rc==0) {
		/* send control change notification to clients */
		send_control_change_over_netlink(chip,
										chip->product_code,
										CTRL_CHG_OUTPUT_CONTROL,
										0 /* client must query entire state */);	
	}
	return rc;
}

int get_control_output_report(struct snd_hdj_chip* chip, 
								u8 __user *buffer,
								u32 buffer_len)
{
	struct usb_hdjbulk *ubulk=NULL;
	struct snd_hdjmidi* umidi;
	struct snd_hdjmidi_out_endpoint* ep; 
	struct controller_output_hid *controller_state;
	unsigned long flags, ctouser;
	u8 *hid_buffer_copy=NULL;
	u32 control_len;
	int rc=-EINVAL;
	
	rc = get_output_control_data_len(chip,&control_len);
	if (rc!=0) {
		printk(KERN_WARNING"%s() get_output_control_data_len() failed, rc:%d\n",
				__FUNCTION__,rc);
		return rc;	
	}
	
	if (chip->product_code == DJCONSOLE_PRODUCT_CODE || 
	    chip->product_code == DJCONSOLE2_PRODUCT_CODE ||
	    chip->product_code == DJCONSOLERMX_PRODUCT_CODE ||
	    chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
	    ubulk = bulk_from_chip(chip);
	    if (ubulk==NULL) {
	    	printk(KERN_WARNING"%s() bulk_from_chip() returned NULL\n",__FUNCTION__);
	    	return -ENODEV;	
	    }
		down(&ubulk->output_control_mutex);
		/* don't copy the report ID element- skip the first byte */
		ctouser = copy_to_user((void*)buffer,
							ubulk->output_control_buffer+1,
							buffer_len <= control_len? buffer_len : control_len);
		up(&ubulk->output_control_mutex);
		if (ctouser != 0) {
			printk(KERN_WARNING"%s() copy_to_user failed, ctouser:%lu\n",
					__FUNCTION__,
					ctouser);
			return -EFAULT;	
		}
	} else if (chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		umidi = midi_from_chip(chip);
		if (umidi==NULL) {
			printk(KERN_WARNING"%s() midi_from_chip() returned NULL\n",__FUNCTION__);
	    	return -ENODEV;	
		} 
		/* assume 1 output endpoint- true for all of our devices */
		if (umidi->endpoints[0].out==NULL ||
			 umidi->endpoints[0].out->controller_state==NULL) {
			printk(KERN_WARNING"%s() invalid state\n",__FUNCTION__);
	    	return -EINVAL;		 	
		}
		ep = umidi->endpoints[0].out;
		controller_state = ep->controller_state;
		
		hid_buffer_copy = zero_alloc(control_len,GFP_KERNEL);
		if (hid_buffer_copy==NULL) {
			printk(KERN_WARNING"%s() kmalloc failed\n",__FUNCTION__);
			return -ENOMEM;	
		}
		
		/* we synchronize with our tasklet for the HID control buffer */
		spin_lock_irqsave(&ep->buffer_lock, flags);
		/* +1 on buffer to avoid the report ID */
		memcpy(hid_buffer_copy,controller_state->current_hid_report_data+1,control_len);
		spin_unlock_irqrestore(&ep->buffer_lock, flags);
		/* don't copy the report ID element- skip the first byte */
		ctouser = copy_to_user((void*)buffer,
							hid_buffer_copy,
							buffer_len <= control_len? buffer_len : control_len);
		
		kfree(hid_buffer_copy);
		if (ctouser != 0) {
			printk(KERN_WARNING"%s() copy_to_user failed, ctouser:%lu\n",
					__FUNCTION__,
					ctouser);
			return -EFAULT;	
		}
	} else {
		printk(KERN_WARNING"%s Invalid product:%d\n",__FUNCTION__,chip->product_code);
		rc = -EINVAL;	
	}
	return rc;
}

static void set_sample_rate_in_context(struct usb_hdjbulk *ubulk, u16 sample_rate)
{
	struct hdj_console_context *dc_djc;
	struct hdj_mk2_rmx_context *dc_mk2_rmx;
	
	if (ubulk->chip->product_code==DJCONSOLE_PRODUCT_CODE) {
		dc_djc = (struct hdj_console_context *)ubulk->device_context;
		atomic_set(&dc_djc->sample_rate,sample_rate);
	} else if (ubulk->chip->product_code==DJCONSOLE2_PRODUCT_CODE ||
				ubulk->chip->product_code==DJCONSOLERMX_PRODUCT_CODE) {
		dc_mk2_rmx = (struct hdj_mk2_rmx_context *)ubulk->device_context;
		atomic_set(&dc_mk2_rmx->sample_rate,sample_rate);
	} else {
		printk(KERN_WARNING"%s unknown product:%d- not saving sample rate\n",
				__FUNCTION__,ubulk->chip->product_code);	
	}
}

static u16 read_sample_rate_from_context(struct usb_hdjbulk *ubulk)
{
	struct hdj_console_context *dc_djc;
	struct hdj_mk2_rmx_context *dc_mk2_rmx;
	
	if (ubulk->chip->product_code==DJCONSOLE_PRODUCT_CODE) {
		dc_djc = (struct hdj_console_context *)ubulk->device_context;
		return atomic_read(&dc_djc->sample_rate);
	} else if (ubulk->chip->product_code==DJCONSOLE_PRODUCT_CODE ||
				ubulk->chip->product_code==DJCONSOLERMX_PRODUCT_CODE) {
		dc_mk2_rmx = (struct hdj_mk2_rmx_context *)ubulk->device_context;
		return atomic_read(&dc_mk2_rmx->sample_rate);
	} else {
		printk(KERN_WARNING"%s unknown product:%d- not saving sample rate\n",
				__FUNCTION__,ubulk->chip->product_code);	
		return 0;
	}
}

int get_sample_rate(struct usb_hdjbulk *ubulk, u16 * sample_rate, u8 query_hardware)
{
	int ret = -EINVAL;

	if (ubulk->chip->caps.sample_rate_readable!=1) {
		printk(KERN_WARNING"%s invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		return -EINVAL;
	}

	if (query_hardware) {
		ret = send_vendor_request(ubulk->chip->index, REQT_READ, 
							DJ_GET_SAMPLE_RATE, 0, 0, sample_rate, 0);
		if (ret == 0) {
			set_sample_rate_in_context(ubulk,*sample_rate);
		} else {
			printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else {
		*sample_rate = read_sample_rate_from_context(ubulk);
		ret = 0;
	}
	
	return ret;
}

int set_sample_rate(struct usb_hdjbulk *ubulk, u16 sample_rate)
{
	int ret = -EINVAL;
	u16 sr_request_native = 0;

	/* Well, setting the sample rate requires that the device be reenumerated by the 
	 *  system as the descriptors change.  The DJ Console has a custom command to 
	 *  ask the device to detach and reattach.  The other devices do not, because their
	 *  USB audio sample rates do not change.  Their ASIO sample rates do change, but 
	 *  that is a Windows issue only.  Under Windows and OSX there is a USB DDI/API which
	 *  requests that the device reenumerate the device, but I did not find it yet
	 *  under Linux.  It's not an issue, though, because we do it ourselves with our
	 *  custom device command.
	 */
	if (ubulk->chip->caps.sample_rate_writable!=1) {
		printk(KERN_WARNING"%s invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		return -EINVAL;
	}

	if (sample_rate==48000) {
		sr_request_native = REQUEST_SET_SAMPLE_RATE_48000;
	} else if(sample_rate==44100) {
		sr_request_native = REQUEST_SET_SAMPLE_RATE_44100;
	} else {
		printk(KERN_WARNING"%s received invalid sample_rate:%u\n",
				__FUNCTION__,sample_rate);
		return -EINVAL;
	}
	
	/* ask the device to detach and reattach itself, of course with the changed
	 *  descriptors */
	if (ubulk->chip->product_code == DJCONSOLE_PRODUCT_CODE) {
		/* only the original DJ Console understands this */
		sr_request_native |= DJ_REQUEST_RESET;
	}
	
	ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, 
				DJ_SET_SAMPLE_RATE, sr_request_native, 0, NULL, 0);
	if (ret == 0) {
		set_sample_rate_in_context(ubulk,sample_rate);
		/* send control change notification to clients */
		send_control_change_over_netlink(ubulk->chip,
						ubulk->chip->product_code,
						CTRL_CHG_SAMPLE_RATE,
						sample_rate);
	} else {
		printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
	}
	
	return ret;
}

int get_crossfader_lock(struct usb_hdjbulk *ubulk, u16 * cross_fader_lock, u8 query_hardware)
{
	int ret;
	struct hdj_mk2_rmx_context* dc;
	if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		if (query_hardware) {
			ret = send_vendor_request(ubulk->chip->index, REQT_READ, 
						DJ_GET_CFADER_LOCK, 0, 0, cross_fader_lock, 0);
			if (ret == 0) {
				atomic_set(&dc->crossfader_lock,*cross_fader_lock);
			} else {
				printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
			}
		} else {
			*cross_fader_lock = atomic_read(&dc->crossfader_lock);
			ret = 0;
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	return ret;
}

int set_crossfader_lock(struct usb_hdjbulk *ubulk, u16 cross_fader_lock)
{
	int ret;
	struct hdj_mk2_rmx_context* dc;
	if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, DJ_SET_CFADER_LOCK, 
						cross_fader_lock, 0, NULL, 0);
		if (ret == 0) {
			atomic_set(&dc->crossfader_lock,cross_fader_lock);
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_XFADER_LOCK,
							((atomic_read(&dc->crossfader_style)&0xffff)<<16)|cross_fader_lock);
		} else {
			printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	return ret;
}

int get_crossfader_style(struct usb_hdjbulk *ubulk, u16 * crossfader_style)
{
	int ret;
	struct hdj_mk2_rmx_context* dc;
	if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		/*get the crossfader style from the device context*/
		*crossfader_style = atomic_read(&dc->crossfader_style);
		ret = 0;
	} else{
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	return ret;
}

int set_crossfader_style(struct usb_hdjbulk *ubulk, u16 crossfader_style)
{
	int ret;
	struct hdj_mk2_rmx_context* dc;
	if (ubulk->chip->product_code == DJCONSOLE2_PRODUCT_CODE) {
		dc = ((struct hdj_mk2_rmx_context *)ubulk->device_context);
		ret = send_vendor_request(ubulk->chip->index, REQT_WRITE, 
					DJ_SET_CROSSFADER_STYLE, crossfader_style, 0, NULL, 0);
		if (ret == 0) {
			atomic_set(&dc->crossfader_style, crossfader_style);
			/* send control change notification to clients */
			send_control_change_over_netlink(ubulk->chip,
							ubulk->chip->product_code,
							CTRL_CHG_XFADER_STYLE,
							((atomic_read(&dc->crossfader_lock)&0xffff)<<16)|crossfader_style);
		} else {
			printk(KERN_ERR"%s send_vendor_request failed, rc:%d\n",__FUNCTION__,ret);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	return ret;
}

int reboot_djcontrolsteel_to_boot_mode(struct usb_hdjbulk *ubulk)
{
	int ret = 0;
	u8 bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];
	struct hdj_steel_context* dc;
	
	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		dc = ((struct hdj_steel_context*)ubulk->device_context);
		if (atomic_read(&dc->device_mode) == DJ_STEEL_IN_BOOT_MODE) {
			/*we already are in boot mode*/
			ret = -EINVAL;
		} else {
			/*prepare the reboot to boot mode request*/
			memset(bulk_data, 0, sizeof(bulk_data));
			bulk_data[0] = DJ_STEEL_REBOOT_TO_BOOT_MODE;

			/*reboot to boot mode*/
			ret = send_bulk_write(ubulk, bulk_data, sizeof(bulk_data), 1 /* force_send */);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	if (ret != 0 && ret != -ENODEV) {
		printk(KERN_ERR"%s: ret: %d\n",__FUNCTION__,ret);
	}

	return ret;
}

int reboot_djcontrolsteel_to_normal_mode(struct usb_hdjbulk *ubulk)
{
	int ret = 0;
	struct hdj_steel_context* dc;

	if (ubulk->chip->product_code == DJCONTROLSTEEL_PRODUCT_CODE) {
		dc = ((struct hdj_steel_context*)ubulk->device_context);
		if (atomic_read(&dc->device_mode) == DJ_STEEL_IN_NORMAL_MODE) {
			/*we already are in normal mode*/
			ret = -EINVAL;
		} else {
			ret = send_boot_loader_command(ubulk, DJ_STEEL_EXIT_BOOT_LOADER);
		}
	} else {
		printk(KERN_WARNING"%s: invalid product:%d\n",__FUNCTION__,ubulk->chip->product_code);
		ret = -EINVAL;
	}
	
	if (ret != 0 && ret != -ENODEV) {
		printk(KERN_ERR"%s: ret: %d\n",__FUNCTION__,	ret);
	}

	return ret;
}


int set_alternate_setting(struct usb_hdjbulk *ubulk, int alt_setting)
{
	int ret = 0;
	
	switch(ubulk->chip->product_code)
	{
	case DJCONSOLE_PRODUCT_CODE:
		ret = usb_set_interface ( ubulk->chip->dev,  
					5,  
					alt_setting); 
		break;
	case DJCONSOLE2_PRODUCT_CODE:
	case DJCONSOLERMX_PRODUCT_CODE:
		ret = usb_set_interface ( ubulk->chip->dev,  
					7,  
					alt_setting); 
		break;
	case DJCONTROLSTEEL_PRODUCT_CODE:
		/* This product doesn't have AUDIO */
		ret = 0;
		break;
	default:
		printk(KERN_WARNING"%s: Unknown product: 0x%X\n",__FUNCTION__, ubulk->chip->product_code);
		ret = -EINVAL;
		break;
	}

	return ret;
}
