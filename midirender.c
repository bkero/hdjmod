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
#include <linux/kthread.h>
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
#include "hdjmp3.h"

#ifdef RENDER_DUMP_URB_THROTTLE
static unsigned long render_dump_urb_throttle = 0;
#endif

#ifdef RENDER_DUMP_PACKETS
static void dump_urb(const char *type, const u8 *data, int length)
{
#ifdef RENDER_DUMP_URB_THROTTLE
	if (++render_dump_urb_throttle >= RENDER_DUMP_URB_THROTTLE_LEVEL)
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

void snd_hdjmidi_output_kill_tasklet(struct snd_hdjmidi_out_endpoint* ep)
{
	if (ep) {
		tasklet_kill(&ep->tasklet);
	}
}

void snd_hdjmidi_output_initialize_tasklet(struct snd_hdjmidi_out_endpoint* ep)
{
	if (ep) {
		tasklet_init(&ep->tasklet, snd_hdjmidi_out_tasklet, (unsigned long)ep);
	}
}

void snd_hdjmidi_output_kill_urbs(struct snd_hdjmidi_out_endpoint* ep)
{
	if (ep) {
		if (ep->urb!=NULL) {
			usb_kill_urb(ep->urb);
		}
		if (ep->controller_state!=NULL && ep->controller_state->urb_kt!=NULL) {
			usb_kill_urb(ep->controller_state->urb_kt);
		}
	}
}

/*
 * Converts MIDI commands to USB MIDI packets.
 */
static void snd_hdjmidi_transmit_byte(struct hdjmidi_out_port* port,
				      uint8_t b, struct urb* urb,
				      struct snd_hdjmidi_out_endpoint* ep)
{
	int midi_channel;
	void (*output_packet)(struct urb*, uint8_t, uint8_t, uint8_t, uint8_t) =
		port->ep->umidi->usb_protocol_ops->output_packet;
	
	if (b >= 0xf8) {
		output_packet(urb, b, 0, 0, 1);
	} else if (b >= 0xf0) {
		switch (b) {
		case 0xf0:
			port->data[0] = b;
			port->state = STATE_SYSEX_1;
			break;
		case 0xf1:
		case 0xf3:
			port->data[0] = b;
			port->state = STATE_1PARAM;
			break;
		case 0xf2:
			port->data[0] = b;
			port->state = STATE_2PARAM_1;
			break;
		case 0xf4:
		case 0xf5:
			port->state = STATE_UNKNOWN;
			break;
		case 0xf6:
			output_packet(urb, 0xf6, 0, 0, 1);
			port->state = STATE_UNKNOWN;
			break;
		case 0xf7:
			switch (port->state) {
			case STATE_SYSEX_0:
				output_packet(urb, 0xf7, 0, 0, 1);
				break;
			case STATE_SYSEX_1:
				output_packet(urb, port->data[0], 0xf7, 0, 2);
				break;
			case STATE_SYSEX_2:
				output_packet(urb, port->data[0], port->data[1], 0xf7, 3);
				break;
			}
			port->state = STATE_UNKNOWN;
			break;
		}
	} else if (b >= 0x80) {
		port->data[0] = b;
		midi_channel = atomic_read(&ep->umidi->channel);
		/* Put in MIDI channel for the case of the devices with MIDI channel non-volatile storage:
		 *  The firmware expects the same channel which it has been submitted to it during
		 *  MIDI initialization (through the correct vendor request).  If we have failed to 
		 *  set the MIDI channel in the device for some reason, then we do nothing.  
		 */
		if (midi_channel!=MIDI_INVALID_CHANNEL &&
		   ep->umidi->chip->caps.non_volatile_channel==1) {
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
			output_packet(urb, port->data[0], b, 0, 2);
			break;
		case STATE_2PARAM_1:
			port->data[1] = b;
			port->state = STATE_2PARAM_2;
			break;
		case STATE_2PARAM_2:
			if (port->data[0] < 0xf0) {
				port->state = STATE_2PARAM_1;
			} else {
				port->state = STATE_UNKNOWN;
			}
			output_packet(urb, port->data[0], port->data[1], b, 3);
			break;
		case STATE_SYSEX_0:
			port->data[0] = b;
			port->state = STATE_SYSEX_1;
			break;
		case STATE_SYSEX_1:
			port->data[1] = b;
			port->state = STATE_SYSEX_2;
			break;
		case STATE_SYSEX_2:
			output_packet(urb, port->data[0], port->data[1], b, 3);
			port->state = STATE_SYSEX_0;
			break;
		}
	}
}

void snd_hdjmidi_standard_output(struct snd_hdjmidi_out_endpoint* ep)
{
	struct urb* urb = ep->urb;
	int p;

	/* Note: Hercules DJ products only have one output port thus far, so this is not urgent.
	 *       The only exception is the DJ Console "Mac Ed.", which is USBMIDI, and so is managed by
  	 *       system modules and not us.
	 */
	/* FIXME: lower-numbered ports can starve higher-numbered ports */
	for (p = 0; p < 0x10; ++p) {
		struct hdjmidi_out_port* port = &ep->ports[p];
		if (!port->active)
			continue;
		while (urb->transfer_buffer_length + 3 < ep->max_transfer) {
			uint8_t b;
			if (snd_rawmidi_transmit(port->substream, &b, 1) != 1) {
				port->active = 0;
				break;
			}
			snd_hdjmidi_transmit_byte(port, b, urb, ep);
		}
	}
}

void snd_hdjmidi_output_standard_packet(struct urb* urb, 
					uint8_t b0, 
					uint8_t b1, 
					uint8_t b2,
					uint8_t len)
{
	uint8_t* buf = (uint8_t*)urb->transfer_buffer + urb->transfer_buffer_length;
	
	buf[0] = b0;
	buf[1] = b1;
	buf[2] = b2;
	urb->transfer_buffer_length += len;
}

static void snd_hdjmp3_flush_analogs(struct snd_hdjmidi_out_endpoint* out_ep)
{
	int control_num;
	int midi_message_to_send;
	struct snd_hdjmidi_in_endpoint* in_ep;
	struct hdjmidi_in_port* in_port;
	if (out_ep->endpoint_number >= MIDI_MAX_ENDPOINTS) {
		snd_printk(KERN_INFO"%s(): endpoint number too high:%d, max:%d",
			__FUNCTION__,
			out_ep->endpoint_number,
			MIDI_MAX_ENDPOINTS-1);
		return;
	}
	in_ep = out_ep->umidi->endpoints[out_ep->endpoint_number].in;
	if (in_ep==NULL || in_ep->controller_state==NULL) {
		return;
	}
	in_port = &in_ep->ports[0]; /* only 1 port */
	if (in_port==NULL) {
		return;
	}
	for(control_num = 0; control_num < DJ_MP3_NUM_INPUT_CONTROLS ; control_num++) {
		if (in_ep->controller_state->control_details[control_num].type==TYPE_LINEAR) {
			midi_message_to_send = atomic_read(&in_ep->controller_state->control_details[control_num].value);
			
			if (midi_message_to_send &&
			    test_bit(in_port->substream->number, &in_ep->umidi->input_triggered)) {
				snd_rawmidi_receive(in_port->substream, 
						(unsigned char*)&midi_message_to_send, 
						3);
			}
		}	
	}
}

/* 
 There are two points to check for:
  a) When a play/pause bank is requested to blink, the device caches the current play/pause
     bank status for later restoration.  However, if the play/pause bank is set to another
	   value while the blink is on, it does not cache the new value for restoration.
  b) The device will not do an update to its state if the requested set is the same as its
     internal state.

 Therefore, if we request a play/pause bank to blink, then we first blank out that play/pause
  bank, and force to 0 any further sets to that play/pause bank

 Note: Currently can run in completion context (which is in interrupt context), or in a tasklet,
       so we use spin locks which disable interrupts
*/
static void check_led_blink_case(struct snd_hdjmidi_out_endpoint* ep,
				  u8 set_left_play_blink,
				  u8 set_right_play_blink)
{
	unsigned int bytepos=0;
	unsigned int bitpos=0;
	unsigned long flags;

	/* check for left play/pause blink */ 
	if ((atomic_read(&ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_BLINK_L].value)==1 &&
	    atomic_read(&ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_L].value)!=0) || set_left_play_blink) {    
		bytepos = ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_L].byte_number;
		bitpos = ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_L].bit_number;
		atomic_set(&ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_L].value, 0);
		spin_lock_irqsave(&ep->controller_state->hid_buffer_lock, flags);
		ep->controller_state->current_hid_report_data[bytepos] &= ~(1<<bitpos);
		spin_unlock_irqrestore(&ep->controller_state->hid_buffer_lock, flags);
	}

	/* check for right play/pause blink */ 
	if ((atomic_read(&ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_BLINK_R].value)==1 &&
	    atomic_read(&ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_R].value)!=0) || set_right_play_blink) {    
		bytepos = ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_R].byte_number;
		bitpos = ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_R].bit_number;
		atomic_set(&ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_R].value, 0);
		spin_lock_irqsave(&ep->controller_state->hid_buffer_lock, flags);
		ep->controller_state->current_hid_report_data[bytepos] &= ~(1<<bitpos);
		spin_unlock_irqrestore(&ep->controller_state->hid_buffer_lock, flags);
	}
}

/* Note: Currently can run in completion context (which is in interrupt context), or in a tasklet,
       so we use spin locks which disable interrupts*/
void snd_hdjmp3_output_standard_packet(struct urb* urb, 
					uint8_t b0, 
					uint8_t b1, 
					uint8_t b2,
					uint8_t len)
{
	int control_num;
	uint8_t* buf = (uint8_t*)urb->transfer_buffer;
	struct snd_hdjmidi_out_endpoint* ep = (struct snd_hdjmidi_out_endpoint*)urb->context;
	u32 message=0;
	unsigned long flags;
	unsigned int bytepos=0;
	unsigned int bitpos=0;
	unsigned int bitmask=0;
	unsigned int max_index = DJ_MP3_HID_OUTPUT_REPORT_LEN-1;
	u8 set_left_play_blink = 0;
	u8 set_right_play_blink = 0;

#ifdef RENDER_DATA_PRINTK
	snd_printk(KERN_INFO"%s(): b0:%x, b1:%x, b2:%x, len:%d\n",
			__FUNCTION__,b0,b1,b2,len);
#endif

	if (ep->controller_state==NULL) {
		return;
	}
	
	/* check the buffer */
	if ( (buf+DJ_MP3_HID_OUTPUT_REPORT_LEN) > 
	    ((uint8_t*)urb->transfer_buffer + ep->max_transfer) ) {
		return;
	}
	
	/* We only service control change messages, anything else is silently drained and dropped.  We
	 *  map appropriate control change messages to HID output report calls (which are control requests) 
	 */
	if (len==3) {
		/* remove the channel from b0 */
		message = (b0&0xf0) | (b1 << 8);
		
		for(control_num=0; control_num < DJ_MP3_NUM_OUTPUT_CONTROLS; control_num++) {
			if (memcmp(&ep->controller_state->control_details[control_num].midi_message_pressed,
				  &message,
				  2)==0) {
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
				if (b2==((uint8_t*)&ep->controller_state->control_details[control_num].midi_message_pressed)[2]) {
					spin_lock_irqsave(&ep->controller_state->hid_buffer_lock, flags);
					ep->controller_state->current_hid_report_data[bytepos] |= bitmask;
					spin_unlock_irqrestore(&ep->controller_state->hid_buffer_lock, flags);
					/* we may have other routines checking values via atomic API */
					atomic_set(&ep->controller_state->control_details[control_num].value, 1);
					/* check LED blink case- left and right */
					if (ep->controller_state->control_details[control_num].control_id==MP3_OUT_PLAY_PAUSE_BLINK_L) {	
						set_left_play_blink = 1;
					}	
					if (ep->controller_state->control_details[control_num].control_id==MP3_OUT_PLAY_PAUSE_BLINK_R) {	
						set_right_play_blink = 1;
					}
				}
				else if (b2==((uint8_t*)&ep->controller_state->control_details[control_num].midi_message_released)[2]) {
					spin_lock_irqsave(&ep->controller_state->hid_buffer_lock, flags);
					ep->controller_state->current_hid_report_data[bytepos] &= ~bitmask;
					spin_unlock_irqrestore(&ep->controller_state->hid_buffer_lock, flags);
					/* we may have other routines checking values via atomic API */
					atomic_set(&ep->controller_state->control_details[control_num].value, 0);
				} else {

					continue;
				}
				/* for FLUSH Analogs */
				if (ep->controller_state->control_details[control_num].control_id==MP3_OUT_FLUSH_ANALOGS) {
					snd_hdjmp3_flush_analogs(ep);
					break;
				}

				if (atomic_read(&ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_BLINK_L].value)==1 ||
				   atomic_read(&ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_BLINK_L].value)==1) {
					check_led_blink_case(ep,
							      set_left_play_blink,
							      set_right_play_blink);
				}

				spin_lock(&ep->controller_state->hid_buffer_lock);
				/* copy the data into the URB's buffer */
				memcpy(buf,
					ep->controller_state->current_hid_report_data,
					DJ_MP3_HID_OUTPUT_REPORT_LEN);
				spin_unlock(&ep->controller_state->hid_buffer_lock);
				urb->transfer_buffer_length = DJ_MP3_HID_OUTPUT_REPORT_LEN;
				break;
			}
		}
	}

	/* call helper- if necessary- and check if we need to maintain polling */
	if (le16_to_cpu(ep->umidi->chip->dev->descriptor.bcdDevice) <= POLL_VERSION) {
		if (mp3w_check_led_state(ep,0)) {
			atomic_set(&ep->controller_state->poll_period_jiffies,msecs_to_jiffies(POLL_PERIOD_MS));

			/* wake up the polling thread */
			wake_up(&ep->controller_state->mp3w_kthread_wait);
		} else {
			atomic_set(&ep->controller_state->poll_period_jiffies,0);
		}
	}
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
int snd_hdjmidi_output_open(struct snd_rawmidi_substream *substream)
#else
int snd_hdjmidi_output_open(snd_rawmidi_substream_t *substream)
#endif
{
	struct snd_hdjmidi* umidi = substream->rmidi->private_data;
	struct hdjmidi_out_port* port = NULL;
	int i, j;
#ifdef RENDER_DUMP_URB_THROTTLE
	render_dump_urb_throttle = 0;
#endif
	snd_printk(KERN_DEBUG"%s()\n",__FUNCTION__);

	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		if (umidi->endpoints[i].out) {
			for (j = 0; j < 0x10; ++j) {
				if (umidi->endpoints[i].out->ports[j].substream == substream) {
					port = &umidi->endpoints[i].out->ports[j];
#ifdef THROTTLE_MP3_RENDER
					umidi->endpoints[i].out->last_send_time = jiffies + 
							(THROTTLE_MP3_RENDER_RATE*HZ)/1000;
#endif
					break;
				}
			}
		}
	}
	if (!port) {
		snd_BUG();
		return -ENXIO;
	}
	substream->runtime->private_data = port;
	port->state = STATE_UNKNOWN;
	return 0;
}

/* MARK: PRODCHANGE */
/*
static void clear_leds_async(struct snd_hdjmidi_out_endpoint* ep)
{
	uint8_t * buffer;
	int rc;
	buffer = (uint8_t*)ep->urb_led->transfer_buffer;
	
	if (ep->umidi->chip->product_code==DJCONSOLE_PRODUCT_CODE ||
			ep->umidi->chip->product_code==DJCONSOLE2_PRODUCT_CODE ||
			ep->umidi->chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		buffer[1] = 0;
		buffer[2] = 0;	
		buffer[3] &= ~3; //preserves mouse
	} else if (ep->umidi->chip->product_code==DJCONSOLERMX_PRODUCT_CODE) {
		buffer[1] = 0;
		buffer[2] = 0;	
		buffer[3] = 0; //preserves mouse	
		buffer[4] = 0;
	} else if (ep->umidi->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		memset(buffer,0,ep->umidi->chip->caps.leds_report_len);
	} else {
		printk(KERN_WARNING"%s unknown product:%d\n",
			__FUNCTION__,ep->umidi->chip->product_code);
		return;	
	}
	// put in report ID
	buffer[0] = ep->umidi->chip->caps.leds_report_id;
	rc = snd_hdjmidi_submit_urb(ep->umidi,ep->urb_led,GFP_ATOMIC);
	if (rc!=0) {
		printk(KERN_WARNING"%s snd_hdjmidi_submit_urb failed rc:%x\n",
				__FUNCTION__,rc);
	}
}*/

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
int snd_hdjmidi_output_close(struct snd_rawmidi_substream *substream)
#else
int snd_hdjmidi_output_close(snd_rawmidi_substream_t *substream)
#endif
{
	/*
	struct snd_hdjmidi* umidi = substream->rmidi->private_data;
	struct hdjmidi_out_port* port = NULL;
	int i, j;*/
	snd_printk(KERN_DEBUG"%s()\n",__FUNCTION__);
	
	/*
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		if (umidi->endpoints[i].out) {
			for (j = 0; j < 0x10; ++j) {
				if (umidi->endpoints[i].out->ports[j].substream == substream) {
					port = &umidi->endpoints[i].out->ports[j];
					break;
				}
			}
		}
	}
	if (port!=NULL) {
		clear_leds_async(port->ep);
	} else {
		printk(KERN_WARNING"%s WARNING: unable to clear LEDs, could not find port\n",
				__FUNCTION__);	
	}*/

	return 0;
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
void snd_hdjmidi_output_trigger(struct snd_rawmidi_substream *substream, int up_param)
#else
void snd_hdjmidi_output_trigger(snd_rawmidi_substream_t *substream, int up_param)
#endif
{
	struct hdjmidi_out_port* port = (struct hdjmidi_out_port*)substream->runtime->private_data;
	
#ifdef RENDER_DATA_PRINTK
	snd_printk(KERN_INFO"%s(): up:%d\n",__FUNCTION__,up_param);
#endif
	port->active = up_param;
	if (up_param) {
		if (atomic_read(&port->ep->umidi->chip->shutdown)==1) {
			/* gobble up remaining bytes to prevent wait in
			 * snd_rawmidi_drain_output */
			while (!snd_rawmidi_transmit_empty(substream))
				snd_rawmidi_transmit_ack(substream, 1);
			return;
		}
		if (atomic_read(&port->ep->umidi->chip->no_urb_submission)!=0) {
			return;
		}
		tasklet_hi_schedule(&port->ep->tasklet);
	}
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void snd_hdjmidi_out_urb_complete(struct urb* urb)
#else
void snd_hdjmidi_out_urb_complete(struct urb* urb, struct pt_regs *junk)
#endif
{
	struct snd_hdjmidi_out_endpoint* ep = urb->context;
	
	spin_lock(&ep->buffer_lock);
	ep->urb_active = 0;
	spin_unlock(&ep->buffer_lock);

	if (urb->status < 0) {
		int err = snd_hdjmidi_urb_error(urb->status);
		if (err < 0) {
			if (err != -ENODEV && atomic_read(&ep->umidi->chip->no_urb_submission)!=0)
				mod_timer(&ep->umidi->error_timer,
					  jiffies + ERROR_DELAY_JIFFIES);
			return;
		}
	}
	snd_hdjmidi_do_output(ep);
}

#ifdef THROTTLE_MP3_RENDER
void midi_render_throttle_timer(unsigned long data)
{
	struct snd_hdjmidi_out_endpoint* ep = (struct snd_hdjmidi_out_endpoint*)data;
	snd_hdjmidi_do_output(ep);
}
#endif

/*
 * This is called when some data should be transferred to the device
 * (from one or more substreams).
 */
void snd_hdjmidi_do_output(struct snd_hdjmidi_out_endpoint* ep)
{
	struct urb* urb = ep->urb;
	unsigned long flags;
	
#ifdef RENDER_DATA_PRINTK
	snd_printk(KERN_INFO"%s()\n",__FUNCTION__);
#endif
	spin_lock_irqsave(&ep->buffer_lock, flags);
	if (ep->urb_active || atomic_read(&ep->umidi->chip->shutdown)==1) {
		spin_unlock_irqrestore(&ep->buffer_lock, flags);
		return;
	}
	
#ifdef THROTTLE_MP3_RENDER
	/* Only the mp3 need to throttled because it is HID, and we don't want to send
	 *  faster than every 8 ms */
	if (ep->umidi->chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
			if (time_before(jiffies, ep->last_send_time)) {
				mod_timer(&ep->render_delay_timer,
							(long)ep->last_send_time-
							(long)jiffies);
				spin_unlock_irqrestore(&ep->buffer_lock, flags);
				return;
			}
	}
#endif

	urb->transfer_buffer_length = 0;
	ep->umidi->usb_protocol_ops->output(ep);

	if (urb->transfer_buffer_length > 0) {
		dump_urb("sending", urb->transfer_buffer,
			 urb->transfer_buffer_length);
		urb->dev = ep->umidi->chip->dev;
		ep->urb_active = snd_hdjmidi_submit_urb(ep->umidi, urb, GFP_ATOMIC) >= 0;

		/* new send time allowed */
		ep->umidi->endpoints[0].out->last_send_time = jiffies + 
			(THROTTLE_MP3_RENDER_RATE*HZ)/1000;
	}
	spin_unlock_irqrestore(&ep->buffer_lock, flags);
}

void snd_hdjmidi_out_tasklet(unsigned long data)
{
	struct snd_hdjmidi_out_endpoint* ep = (struct snd_hdjmidi_out_endpoint *) data;

	snd_hdjmidi_do_output(ep);
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void hid_ctrl_complete(struct urb* urb)
#else
void hid_ctrl_complete(struct urb* urb, struct pt_regs *junk)
#endif
{
	struct snd_hdjmidi_out_endpoint* ep = urb->context;

	spin_lock(&ep->buffer_lock);
	ep->urb_active = 0;
	spin_unlock(&ep->buffer_lock);

	if (urb->status < 0) {
		int err = snd_hdjmidi_urb_error(urb->status);
		if (err < 0) {
			if (err != -ENODEV && atomic_read(&ep->umidi->chip->no_urb_submission)!=0)
				mod_timer(&ep->umidi->error_timer,jiffies + ERROR_DELAY_JIFFIES);
			return;
		}
	}
	snd_hdjmidi_do_output(ep);
}

static u8 is_loop_cure_applied(struct controller_output_hid *controller_state)
{
	if (atomic_read(&controller_state->control_details[MP3_OUT_CUE_123_R].value)==1 ||
	   atomic_read(&controller_state->control_details[MP3_OUT_CUE_123_L].value)==1 ||
	   atomic_read(&controller_state->control_details[MP3_OUT_FX_L].value)==1 ||
	   atomic_read(&controller_state->control_details[MP3_OUT_FX_R].value)==1 ||
	   atomic_read(&controller_state->control_details[MP3_OUT_PLAY_PAUSE_R].value)==1 ||
	   atomic_read(&controller_state->control_details[MP3_OUT_CUE_R].value)==1) {
		return 1;
	}
	
	return 0;
}

static u8 is_monitor_cure_applied(struct controller_output_hid *controller_state)
{
	if (atomic_read(&controller_state->control_details[MP3_OUT_MONITOR_R].value)==1 ||
	   atomic_read(&controller_state->control_details[MP3_OUT_AUTOBEAT_R].value)==1 ||
	   atomic_read(&controller_state->control_details[MP3_OUT_AUTOBEAT_L].value)==1 ||
	   atomic_read(&controller_state->control_details[MP3_OUT_LOOP_L].value)==1 ||
	   atomic_read(&controller_state->control_details[MP3_OUT_LOOP_R].value)==1) {
		return 1;
	}
	
	return 0;
}

u8 mp3w_check_led_state(struct snd_hdjmidi_out_endpoint* ep, u8 called_from_kthread)
{
	int refire = 0, value_to_send;
	u8 toggle_master_r = 0, toggle_master_l = 0, toggle_monitor_l = 0;
	u8 loop_cure_applied = 0;
	u8 monitor_cure_applied = 0;
	unsigned long flags;
	unsigned int bytepos=0;
	unsigned int bitpos=0;
	unsigned int control_num=0;
	unsigned int control_id=0;
	unsigned int toggle_control=0;
	unsigned int max_index = DJ_MP3_HID_OUTPUT_REPORT_LEN-1;

	loop_cure_applied = is_loop_cure_applied(ep->controller_state);
	monitor_cure_applied = is_monitor_cure_applied(ep->controller_state);

	if (loop_cure_applied==0) {
		if (atomic_read(&ep->controller_state->control_details[MP3_OUT_MASTER_TEMPO_R].value)==1 &&
		   (atomic_read(&ep->controller_state->control_details[MP3_OUT_LOOP_R].value)==1 ||
		    atomic_read(&ep->controller_state->control_details[MP3_OUT_LOOP_L].value)==1) ) {
			toggle_master_r = 1;
			refire = 1;
		}
	
		if (atomic_read(&ep->controller_state->control_details[MP3_OUT_MASTER_TEMPO_L].value)==1 &&
		   atomic_read(&ep->controller_state->control_details[MP3_OUT_LOOP_R].value)==1) {
			toggle_master_l = 1;
			refire = 1;
		}
	}
	
	if (monitor_cure_applied==0) {
		if (atomic_read(&ep->controller_state->control_details[MP3_OUT_MASTER_TEMPO_R].value)==1 &&
		   atomic_read(&ep->controller_state->control_details[MP3_OUT_MONITOR_L].value)==1 &&
		   atomic_read(&ep->controller_state->control_details[MP3_OUT_PLAY_PAUSE_L].value)==1) {
			toggle_monitor_l = 1;
			refire = 1;
		}
	}

	/* return either if we determine no action is necessary, or if we are called from interrupt/tasklet
		context, and will signal the work queue to engage and do the work later */
	if ( refire==0 || called_from_kthread==0) {
		return refire;
	}
		
	/* from here on we have been called from the kthread and are thus in process context */
	spin_lock_irqsave(&ep->controller_state->hid_buffer_lock, flags);
	memcpy(ep->controller_state->urb_kt->transfer_buffer,
		ep->controller_state->current_hid_report_data,
		sizeof(ep->controller_state->current_hid_report_data));
	/*controller_state->current_hid_report_data_wq[1] = (~(controller_state->current_hid_report_data_wq[1]&0x1)&1);*/
	spin_unlock_irqrestore(&ep->controller_state->hid_buffer_lock, flags);

	/* clear to proceed */
	for(value_to_send = 0 ; value_to_send < 2 ; value_to_send++) {
		for(control_num = 0; control_num < 3 ; control_num++) {
			if (control_num==0) {
				control_id = MP3_OUT_MASTER_TEMPO_R;
				if (toggle_master_r==1) {
					toggle_control = 1;
				} else {
					toggle_control = 0;
				}
			} else if (control_num==1) {
				control_id = MP3_OUT_MASTER_TEMPO_L;
				if (toggle_master_l==1) {
					toggle_control = 1;
				} else {
					toggle_control = 0;
				}
			} else {
				control_id = MP3_OUT_MONITOR_L;
				if (toggle_monitor_l==1) {
					toggle_control = 1;
				} else {
					toggle_control = 0;
				}
			}
			if (toggle_control==1) {
				bytepos = ep->controller_state->control_details[control_id].byte_number;
				if (bytepos>max_index) {
					snd_printk(KERN_INFO"%s(): bad bytepos:%s, %d\n",
						__FUNCTION__,
						ep->controller_state->control_details[control_id].name,
						bytepos);
					continue;
				}
				bitpos = ep->controller_state->control_details[control_id].bit_number;	
				if (value_to_send==1) {
					((char*)ep->controller_state->urb_kt->transfer_buffer)[bytepos] |= 1<<bitpos;
				} else {
					((char*)ep->controller_state->urb_kt->transfer_buffer)[bytepos] &= ~(1<<bitpos);
				}	
			}
		}

		/* set urb dev and length and fire- synchronously */
		ep->controller_state->urb_kt->dev = ep->umidi->chip->dev;
		ep->controller_state->urb_kt->transfer_buffer_length = DJ_MP3_HID_OUTPUT_REPORT_LEN;
		dump_urb("hid_kt",ep->controller_state->urb_kt->transfer_buffer,ep->controller_state->urb_kt->transfer_buffer_length);
		
		if (snd_hdjmidi_submit_urb(ep->umidi, ep->controller_state->urb_kt, GFP_KERNEL)==0) {
			wait_for_completion(&ep->controller_state->ctl_req_completion_kt);
		}

		/* sleep a bit */
		if (value_to_send==0) {
			/* sleep for the timer period */
			msleep(POLL_PERIOD_MS);
		}
	}

	return refire;
}

int mp3w_kthread_entry(void *arg)
{
	struct snd_hdjmidi_out_endpoint* ep = NULL;
	struct controller_output_hid *controller_state = NULL;
	int timeout;
	
	/*snd_printk(KERN_INFO"%s()\n",__FUNCTION__);*/
	ep = (struct snd_hdjmidi_out_endpoint*)arg;
	controller_state = ep->controller_state;
	
	/* let code which set up us know that we are running, so it can continue */
	complete(&ep->controller_state->mp3w_kthread_started);
	
	while (!kthread_should_stop()) {
		timeout = atomic_read(&controller_state->poll_period_jiffies);
		
		if (timeout!=0) {
			wait_event_interruptible_timeout(controller_state->mp3w_kthread_wait,
												kthread_should_stop(),
												timeout);
		} else {
			wait_event_interruptible(controller_state->mp3w_kthread_wait,
						kthread_should_stop()||
						atomic_read(&ep->controller_state->poll_period_jiffies)!=0);
		}
		
		if (kthread_should_stop()) {
			/*printk(KERN_INFO"%s() asked to bail\n",__FUNCTION__);*/
			break;	
		}
		
		/* call helper and check if we need to maintain polling */
		if (mp3w_check_led_state(ep,1)) {
			atomic_set(&controller_state->poll_period_jiffies,
						msecs_to_jiffies(POLL_PERIOD_MS));
		} else {
			atomic_set(&controller_state->poll_period_jiffies,0);
		}
	}
	
	return 0;
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void hid_ctrl_complete_kt(struct urb* urb)
#else
void hid_ctrl_complete_kt(struct urb* urb, struct pt_regs *junk)
#endif
{
	struct controller_output_hid *controller_state = urb->context;

	if (controller_state==NULL) {
		snd_printk(KERN_WARNING"%s(): context NULL, bailing, may not wake up client!\n",__FUNCTION__);
		return;
	}
	complete(&controller_state->ctl_req_completion_kt);
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void midi_led_clear_complete(struct urb* urb)
#else
void midi_led_clear_complete(struct urb* urb, struct pt_regs *junk)
#endif
{
	/* nothing to do */
}
