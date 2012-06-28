/*
*
*  Copyright (c) 2008  Guillemot Corporation S.A.
*
*  Philip Lukidis plukidis@guillemot.com
*
*  Based on usbmidi ALSA driver by Clemens Ladisch
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
#include <linux/version.h>	/* For LINUX_VERSION_CODE */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <asm/atomic.h>
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,24) )
#include <sound/driver.h>
#endif
#include <sound/core.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/rawmidi.h>
#include <sound/asequencer.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include "djdevioctls.h"
#include "device.h"
#include "midi.h"
#include "midicapture.h"
#include "midirender.h"

#ifdef CAPTURE_DUMP_URB_THROTTLE
static unsigned long capture_dump_urb_throttle = 0;
#endif

#ifdef CAPTURE_DUMP_PACKETS
static void dump_urb(const char *type, const u8 *data, int length)
{
#ifdef CAPTURE_DUMP_URB_THROTTLE
	if (++capture_dump_urb_throttle >= CAPTURE_DUMP_URB_THROTTLE_LEVEL)
	{
		return;
	}
#endif
	snd_printk(KERN_INFO "%s packet: [", type);
	for (; length > 0; ++data, --length)
		snd_printk(" %02x", *data);
	snd_printk(" ]\n");
}
#else
#define dump_urb(type, data, length) /* nothing */
#endif

/*
 * Receives a chunk of MIDI data.
 */
static void snd_hdjmidi_input_data(struct snd_hdjmidi_in_endpoint* ep, 
					int portidx,
				   	uint8_t* data, 
					int length)
{
	struct hdjmidi_in_port* port = &ep->ports[portidx];
	
	if (!port->substream) {
		snd_printd("unexpected port %d!\n", portidx);
		return;
	}
	if (!test_bit(port->substream->number, &ep->umidi->input_triggered)) {
		return;
	}

	snd_rawmidi_receive(port->substream, data, length);
}

/*
 * Converts MIDI bytes to MIDI messages, and implants current MIDI channel if applicable 
 */
static void snd_hdjmidi_capture_byte(struct snd_hdjmidi* umidi,
				     struct hdjmidi_in_port* port,
				     uint8_t b)
{
	int midi_channel;
	if (b >= 0xf8) {
		port->data[0] = b;
		port->midi_message_len = 1;
		port->midi_message_ready = 1;
	} else if (b >= 0xf0) {
		switch (b) {
		case 0xf0:
			port->midi_message_len = 1;
			port->data[0] = b;
			port->state = STATE_SYSEX_1;
			break;
		case 0xf1:
		case 0xf3:
			port->midi_message_len = 1;
			port->data[0] = b;
			port->state = STATE_1PARAM;
			break;
		case 0xf2:
			port->midi_message_len = 1;
			port->data[0] = b;
			port->state = STATE_2PARAM_1;
			break;
		case 0xf4:
		case 0xf5:
			port->state = STATE_UNKNOWN;
			break;
		case 0xf6:
			port->data[0] = b;
			port->midi_message_len = 1;
			port->midi_message_ready = 1;
			port->state = STATE_UNKNOWN;
			break;
		case 0xf7:
			switch (port->state) {
			case STATE_SYSEX_0:
				port->data[0] = b;
				port->midi_message_len = 1;
				port->midi_message_ready = 1;
				break;
			case STATE_SYSEX_1:
				port->data[1] = b;
				port->midi_message_len = 2;
				port->midi_message_ready = 1;
				break;
			case STATE_SYSEX_2:
				port->data[2] = b;
				port->midi_message_len = 3;
				port->midi_message_ready = 1;
				break;
			}
			port->state = STATE_UNKNOWN;
			break;
		}
	} else if (b >= 0x80) {
		port->data[0] = b;
		port->midi_message_len = 1;
		midi_channel = atomic_read(&umidi->channel);
		/* Put in MIDI channel except for:
		 * - Device with non-volatile storage for MIDI channel, as the firmware sends it already.
		 * - We have no valid MIDI channel to assign.
		 * - We are in physical port mode, in which case we are a simple conduit, and don't
		 *    touch the data.
		 */
		if (midi_channel!=MIDI_INVALID_CHANNEL &&
		   umidi->chip->caps.non_volatile_channel!=1 &&
		   atomic_read(&umidi->midi_mode)!=MIDI_MODE_PHYSICAL_PORT) {
			port->data[0] &= 0xf0;
			port->data[0] |= midi_channel&0xf;
		}
		if (b >= 0xc0 && b <= 0xdf)
			port->state = STATE_1PARAM;
		else
			port->state = STATE_2PARAM_1;
	} else { /* b < 0x80 */
		switch (port->state) {
		case STATE_1PARAM:
			if (port->data[0] >= 0xf0) {
				port->state = STATE_UNKNOWN;
			}
			port->data[1] = b;
			port->midi_message_len = 2;
			port->midi_message_ready = 1;
			break;
		case STATE_2PARAM_1:
			port->data[1] = b;
			port->midi_message_len = 2;
			port->state = STATE_2PARAM_2;
			break;
		case STATE_2PARAM_2:
			if (port->data[0] < 0xf0) {
				port->state = STATE_2PARAM_1;
			} else {
				port->state = STATE_UNKNOWN;
			}
			port->data[2] = b;
			port->midi_message_len = 3;
			port->midi_message_ready = 1;
			break;
		case STATE_SYSEX_0:
			port->data[0] = b;
			port->midi_message_len = 1;
			port->state = STATE_SYSEX_1;
			break;
		case STATE_SYSEX_1:
			port->data[1] = b;
			port->midi_message_len = 2;
			port->state = STATE_SYSEX_2;
			break;
		case STATE_SYSEX_2:
			port->data[2] = b;
			port->midi_message_len = 3;
			port->midi_message_ready = 1;
			port->state = STATE_SYSEX_0;
			break;
		}
	}
}

void snd_hdjmidi_standard_input(struct snd_hdjmidi_in_endpoint* ep,
				uint8_t* buffer, 
				int buffer_length)
{
	int i;
	for (i=0;i<buffer_length;i++) {
		/* all our devices have 1 port- no port indicator sent in buffer from device */
		snd_hdjmidi_capture_byte(ep->umidi,&ep->ports[0],buffer[i]);
		if (ep->ports[0].midi_message_ready==1) {
			snd_hdjmidi_input_data(ep, 0, ep->ports[0].data,  ep->ports[0].midi_message_len);
			ep->ports[0].midi_message_ready = 0;
			ep->ports[0].midi_message_len = 0;
		}
	}
}

/* This is called by PSOC and weltrend clients, and always with a full buffer */
static void snd_hdjmp3_core_parse_input(struct snd_hdjmidi_in_endpoint* ep,
		   			 uint8_t* buffer, 
		 			 int buffer_length)
{
	unsigned int control_num = 0;
	unsigned int bytepos=0;
	unsigned int bitpos=0;
	unsigned int bitmask=0;
	unsigned int max_index = DJ_MP3_HID_INPUT_REPORT_LEN-1;
	unsigned char inc_value = 0;
	int midi_channel;
	u32 midi_code_to_send = 0;
	struct hdjmidi_in_port* port = &ep->ports[0]; /* only 1 port */
	
	for (control_num = 0 ; control_num < DJ_MP3_NUM_INPUT_CONTROLS; control_num++) {
		bytepos = ep->controller_state->control_details[control_num].byte_number;
		if (bytepos>max_index) {
			snd_printk(KERN_INFO"%s(): bad bytepos:%s, %d\n",
				__FUNCTION__,
				ep->controller_state->control_details[control_num].name,
				bytepos);
			continue;
		}
		bitpos = ep->controller_state->control_details[control_num].bit_number;
		bitmask = 1 << bitpos;
		if ( (ep->controller_state->control_details[control_num].type==TYPE_BUTTON) &&
		     ((buffer[bytepos]&bitmask) != 
		     (ep->controller_state->last_hid_report_data[bytepos]&bitmask)) ) {
			if ( (buffer[bytepos]&bitmask) != 0 ) {
				midi_code_to_send = ep->controller_state->control_details[control_num].midi_message_pressed;
			} else {
				midi_code_to_send = ep->controller_state->control_details[control_num].midi_message_released;
			}
			
			midi_channel = atomic_read(&ep->umidi->channel);
			((u8*)&midi_code_to_send)[0] &= 0xf0;
			((u8*)&midi_code_to_send)[0] |= midi_channel&0xf;
			atomic_set(&ep->controller_state->control_details[control_num].value,midi_code_to_send);
			
			if (test_bit(port->substream->number, &ep->umidi->input_triggered)) {
				snd_rawmidi_receive(port->substream, 
						(unsigned char*)&midi_code_to_send, 
						3);
			}
		} else if (ep->controller_state->control_details[control_num].type==TYPE_LINEAR &&
			   (buffer[bytepos]!=ep->controller_state->last_hid_report_data[bytepos])) {
			midi_code_to_send = ep->controller_state->control_details[control_num].midi_message_released;
			midi_channel = atomic_read(&ep->umidi->channel);
			((u8*)&midi_code_to_send)[0] &= 0xf0;
			((u8*)&midi_code_to_send)[0] |= midi_channel&0xf;
			memset(((unsigned char*)&midi_code_to_send)+2,buffer[bytepos] >> 1,1);
			atomic_set(&ep->controller_state->control_details[control_num].value,midi_code_to_send);
			
			if (test_bit(port->substream->number, &ep->umidi->input_triggered)) {
				snd_rawmidi_receive(port->substream, 
					(unsigned char*)&midi_code_to_send, 
					3);
			}
		}
		else if (ep->controller_state->control_details[control_num].type==TYPE_INCREMENTAL &&
			   (buffer[bytepos]!=ep->controller_state->last_hid_report_data[bytepos])) {
			inc_value = buffer[bytepos]-ep->controller_state->last_hid_report_data[bytepos];
			if (inc_value > 0x7f) {
				inc_value &= 0x7f;
			}
			midi_code_to_send = ep->controller_state->control_details[control_num].midi_message_released;
			midi_channel = atomic_read(&ep->umidi->channel);
			((u8*)&midi_code_to_send)[0] &= 0xf0;
			((u8*)&midi_code_to_send)[0] |= midi_channel&0xf;
			memset(((unsigned char*)&midi_code_to_send)+2,inc_value,1);
			atomic_set(&ep->controller_state->control_details[control_num].value,midi_code_to_send);

			if (test_bit(port->substream->number, &ep->umidi->input_triggered)) {
				snd_rawmidi_receive(port->substream, 
					(unsigned char*)&midi_code_to_send, 
					3);
			}
		}
	}
}

/* Here we have to buffer our HID report buffer up to 20 bytes, as we only receive up to 8
 *  bytes at a time, because the PSOC version is low speed.  Note that no customer has this
 *  version (there never was a mass production run), but since we here do have some sample, 
 *  and since the workaround is quick, here it is.
 */
void snd_hdjmp3_nonweltrend_input(struct snd_hdjmidi_in_endpoint* ep,
 	   			  uint8_t* buffer, 
		 		  int buffer_length)
{
	if (buffer_length > DJ_MP3_HID_INPUT_REPORT_LEN) {
		snd_printk(KERN_INFO"snd_hdjmp3_nonweltrend_input(): got buffer_len:%d, max:%d\n",
			buffer_length,
			DJ_MP3_HID_INPUT_REPORT_LEN);
		return;
	}
	
	spin_lock(&ep->controller_state->buffer_lock);
	if (atomic_read(&ep->controller_state->buffered_hid_data)==0) {
		if ( (ep->controller_state->last_hid_byte_num+buffer_length) <= 
			  DJ_MP3_HID_INPUT_REPORT_LEN) {
			memcpy((unsigned char*)&ep->controller_state->last_hid_report_data[0]+
				ep->controller_state->last_hid_byte_num,
				buffer, 
				buffer_length);
			ep->controller_state->last_hid_byte_num += buffer_length;
			if (ep->controller_state->last_hid_byte_num >= DJ_MP3_HID_INPUT_REPORT_LEN) {
				atomic_set(&ep->controller_state->buffered_hid_data,1);
				ep->controller_state->last_hid_byte_num = 0;
			}
		}
		spin_unlock(&ep->controller_state->buffer_lock);
		return;
	} 
	
	if ((ep->controller_state->last_hid_byte_num+buffer_length) <= 
		 DJ_MP3_HID_INPUT_REPORT_LEN) {
		memcpy((unsigned char *)&ep->controller_state->current_hid_report_data[0]+
			ep->controller_state->last_hid_byte_num,
			buffer, 
			buffer_length);
		ep->controller_state->last_hid_byte_num += buffer_length;
	}
	
	/* see if we buffered up enough data */
	if (ep->controller_state->last_hid_byte_num >= DJ_MP3_HID_INPUT_REPORT_LEN) {
		/* we have- reset counter and process */
		ep->controller_state->last_hid_byte_num = 0;
	} else {
		/* still buffering */
		spin_unlock(&ep->controller_state->buffer_lock);
		return;
	}

	/* For the case of the PSOC (which was never in production, we just have some samples
	*  here) the buffer_lock protects the current hid buffer state as in the weltrend, and 
	*  because the URB has only a partial snapshot of the HID data the PSOC is low speed, we
	*  need to protect the current hid buffer during the buffering process.  So we need 
	*  to bracket the parsing process total under spinlock protection. */
	if (memcmp(ep->controller_state->last_hid_report_data, 
			ep->controller_state->current_hid_report_data, 
			DJ_MP3_HID_INPUT_REPORT_LEN)!=0) {
		snd_hdjmp3_core_parse_input(ep, ep->controller_state->current_hid_report_data, DJ_MP3_HID_INPUT_REPORT_LEN);
		memcpy(ep->controller_state->last_hid_report_data,ep->controller_state->current_hid_report_data, DJ_MP3_HID_INPUT_REPORT_LEN);
	}
	spin_unlock(&ep->controller_state->buffer_lock);
}

void snd_hdjmp3_standard_input(struct snd_hdjmidi_in_endpoint* ep,
				uint8_t* buffer, 
				int buffer_length)
{
	if (buffer_length != DJ_MP3_HID_INPUT_REPORT_LEN) {
		snd_printk(KERN_DEBUG"%s(): got buffer_len:%d, expected:%d\n",
			__FUNCTION__,
			buffer_length,
			DJ_MP3_HID_INPUT_REPORT_LEN);
		return;
	}
	if (atomic_cmpxchg(&ep->controller_state->buffered_hid_data,0,1)==0) {
		memcpy(ep->controller_state->last_hid_report_data,buffer, buffer_length);
	} else {
		/* keep a copy of the current state */
		spin_lock(&ep->controller_state->buffer_lock);
		memcpy(ep->controller_state->current_hid_report_data,buffer, buffer_length);
		spin_unlock(&ep->controller_state->buffer_lock);
		snd_hdjmp3_core_parse_input(ep, buffer, buffer_length);
		memcpy(ep->controller_state->last_hid_report_data,buffer, buffer_length);
	}
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
int snd_hdjmidi_input_open(struct snd_rawmidi_substream *substream)
#else
int snd_hdjmidi_input_open(snd_rawmidi_substream_t *substream)
#endif
{
	snd_printk(KERN_DEBUG"%s()\n",__FUNCTION__);
#ifdef CAPTURE_DUMP_URB_THROTTLE
	capture_dump_urb_throttle = 0;
#endif
	return 0;
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
int snd_hdjmidi_input_close(struct snd_rawmidi_substream *substream)
#else
int snd_hdjmidi_input_close(snd_rawmidi_substream_t *substream)
#endif
{
	snd_printk(KERN_DEBUG"%s()\n",__FUNCTION__);
	return 0;
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
void snd_hdjmidi_input_trigger(struct snd_rawmidi_substream *substream, int up_param)
#else
void snd_hdjmidi_input_trigger(snd_rawmidi_substream_t *substream, int up_param)
#endif
{
	struct snd_hdjmidi* umidi = substream->rmidi->private_data;
#ifdef CAPTURE_DATA_PRINTK
	snd_printk(KERN_INFO"snd_hdjmidi_input_trigger(): up:%d\n",up_param);
#endif
	if (up_param) {
		set_bit(substream->number, 
			&umidi->input_triggered);
	} else {
		clear_bit(substream->number, 
			&umidi->input_triggered);
	}
}

/*
 * Processes the data read from the device.
 */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void snd_hdjmidi_in_urb_complete(struct urb* urb)
#else
void snd_hdjmidi_in_urb_complete(struct urb* urb, struct pt_regs *junk)
#endif
{
	struct snd_hdjmidi_in_endpoint* ep = urb->context;

	if (urb->status == 0) {
		dump_urb("received", urb->transfer_buffer, urb->actual_length);
		
#ifdef CAPTURE_DATA_PRINTK
		snd_printk(KERN_DEBUG"%s(): buffer_len:%d\n",__FUNCTION__,urb->actual_length);
#endif
		ep->umidi->usb_protocol_ops->input(ep, urb->transfer_buffer,
						   urb->actual_length);
	} else {
		int err = snd_hdjmidi_urb_error(urb->status);
		
#ifdef CAPTURE_DATA_PRINTK
		snd_printk(KERN_INFO"%s(): error:%d\n",__FUNCTION__,urb->status);
#endif
		if (err < 0) {
			if (err != -ENODEV && atomic_read(&ep->umidi->chip->no_urb_submission)!=0 ) {
				ep->error_resubmit = 1;
				mod_timer(&ep->umidi->error_timer,
					  jiffies + ERROR_DELAY_JIFFIES);
			}
			return;
		}
	}

	urb->dev = ep->umidi->chip->dev;
	snd_hdjmidi_submit_urb(ep->umidi, urb, GFP_ATOMIC);
}

int snd_hdjmidi_input_start_ep(struct snd_hdjmidi_in_endpoint* ep)
{
	int rc=0;
	if (ep) {
		struct urb* urb = ep->urb;
		urb->dev = ep->umidi->chip->dev;
		rc = snd_hdjmidi_submit_urb(ep->umidi, urb, GFP_KERNEL);
	}
	return rc;
}

void snd_hdjmidi_input_kill_urbs(struct snd_hdjmidi_in_endpoint* ep)
{
	if (ep) {
		if (ep->urb!=NULL) {
			usb_kill_urb(ep->urb);
		}
	}
}
