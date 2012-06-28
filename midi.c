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
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/kthread.h>
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35) )
#include <linux/slab.h>
#endif
#include <asm/byteorder.h>
#include <asm/atomic.h>
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,24) )
#include <sound/driver.h>
#endif
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/rawmidi.h>
#include <sound/control.h>
#include <sound/asequencer.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include "djdevioctls.h"
#include "device.h"
#include "bulk.h"
#include "midi.h"
#include "midicapture.h"
#include "midirender.h"
#include "configuration_manager.h"
#include "hdjmp3.h"

unsigned long channel_list_initialized = 0;
struct midi_channel_elem channel_list[NUM_MIDI_CHANNELS];
/* spinlock_t channel_list_lock = SPIN_LOCK_UNLOCKED; */
DEFINE_SPINLOCK(channel_list_lock);

static struct usb_protocol_ops snd_hdjmidi_standard_ops = {
	.input = snd_hdjmidi_standard_input,
	.output = snd_hdjmidi_standard_output,
	.output_packet = snd_hdjmidi_output_standard_packet,
};

static struct usb_protocol_ops snd_hdjmp3_standard_ops = {
	.input = snd_hdjmp3_standard_input,
	.output = snd_hdjmidi_standard_output,
	.output_packet = snd_hdjmp3_output_standard_packet,
};

static struct usb_protocol_ops snd_hdjmp3_non_weltrend_standard_ops = {
	.input = snd_hdjmp3_nonweltrend_input,
	.output = snd_hdjmidi_standard_output,
	.output_packet = snd_hdjmp3_output_standard_packet,
};

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
static struct snd_rawmidi_ops snd_hdjmidi_output_ops = {
	.open = snd_hdjmidi_output_open,
	.close = snd_hdjmidi_output_close,
	.trigger = snd_hdjmidi_output_trigger,
};

static struct snd_rawmidi_ops snd_hdjmidi_input_ops = {
	.open = snd_hdjmidi_input_open,
	.close = snd_hdjmidi_input_close,
	.trigger = snd_hdjmidi_input_trigger
};
#else
static snd_rawmidi_ops_t snd_hdjmidi_output_ops = {
	.open = snd_hdjmidi_output_open,
	.close = snd_hdjmidi_output_close,
	.trigger = snd_hdjmidi_output_trigger,
};

static snd_rawmidi_ops_t snd_hdjmidi_input_ops = {
	.open = snd_hdjmidi_input_open,
	.close = snd_hdjmidi_input_close,
	.trigger = snd_hdjmidi_input_trigger,
};
#endif

/*
 * This list specifies names for ports that do not fit into the standard
 * "(product) MIDI (n)" schema because they aren't external MIDI ports,
 * such as internal control or synthesizer ports.
 */
struct port_info {
	u32 id;
	short int port;
	short int voices;
	const char *name;
	unsigned int seq_flags;
};

/*
 * Submits the URB, with error handling.
 */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14) )
int snd_hdjmidi_submit_urb(struct snd_hdjmidi* umidi, struct urb* urb, gfp_t flags)
#else
int snd_hdjmidi_submit_urb(struct snd_hdjmidi* umidi, struct urb* urb, int flags)
#endif
{
	int err;
	/* see if we should ceases sending out URBs */
	if (atomic_read(&umidi->chip->no_urb_submission)!=0 || 
		atomic_read(&umidi->chip->shutdown)!=0) {
		return -EBUSY;
	}

	err = usb_submit_urb(urb, flags);
	if (err < 0) {
		if (err != -ENODEV) {
			snd_printk(KERN_ERR "%s() usb_submit_urb: %d\n", __FUNCTION__,err);
		}
	} 
	return err;
}

/* weltrend chip or not */
static int is_mp3_weltrend(u32 usbid)
{
	if (usbid == USB_ID(USB_HDJ_VENDOR_ID,   DJ_CONTROL_MP3W_PID)) {
		return 1;
	} else {
		return 0;
	}
}

/* Note ordering fixed in djmp3.h which lists control indices */
/* Mapping for MIDI commands to HID report for output, for the MP3 */
static struct controller_control_details mp3_output_control_details[] = {
	{ MP3_OUT_MASTER_TEMPO_L,"MASTER_TEMPO_L", TYPE_LED, 1, 0, 0xb0167f00, 0xb0160000, ATOMIC_INIT(0) },
	{ MP3_OUT_MASTER_TEMPO_R,"MASTER_TEMPO_R", TYPE_LED, 1, 1, 0xb01a7f00, 0xb01a0000, ATOMIC_INIT(0) },
	{ MP3_OUT_FX_L,"FX_L", TYPE_LED, 1, 2, 0xb00f7f00, 0xb00f0000, ATOMIC_INIT(0) },
	{ MP3_OUT_FX_R,"FX_R", TYPE_LED, 1, 3, 0xb0107f00, 0xb0100000, ATOMIC_INIT(0) },
	{ MP3_OUT_CUE_123_R,"CUE_123_R", TYPE_LED, 1, 4, 0xb0117f00, 0xb0110000, ATOMIC_INIT(0) },
	{ MP3_OUT_CUE_R,"CUE_R", TYPE_LED, 1, 5, 0xb0037f00, 0xb0030000, ATOMIC_INIT(0) },
	{ MP3_OUT_PLAY_PAUSE_R,"PLAY_PAUSE_R", TYPE_LED, 1, 6, 0xb0027f00, 0xb0020000, ATOMIC_INIT(0) },
	{ MP3_OUT_CUE_123_L,"CUE_123_L", TYPE_LED, 1, 7, 0xb00e7f00, 0xb00e0000, ATOMIC_INIT(0) },
	{ MP3_OUT_PLAY_PAUSE_L,"PLAY_PAUSE_L", TYPE_LED, 2, 0, 0xb0087f00, 0xb0080000, ATOMIC_INIT(0) },
	{ MP3_OUT_MONITOR_L,"MONITOR_L", TYPE_LED, 2, 1, 0xb07e7f00, 0xb07e0000, ATOMIC_INIT(0) },
	{ MP3_OUT_MONITOR_R,"MONITOR_R", TYPE_LED, 2, 2, 0xb07d7f00, 0xb07d0000, ATOMIC_INIT(0) },
	{ MP3_OUT_CUE_L,"CUE_L", TYPE_LED, 2, 3, 0xb0097f00, 0xb0090000, ATOMIC_INIT(0) },
	{ MP3_OUT_AUTOBEAT_L,"AUTOBEAT_L", TYPE_LED, 2, 4, 0xb00a7f00, 0xb00a0000, ATOMIC_INIT(0) },
	{ MP3_OUT_AUTOBEAT_R,"AUTOBEAT_R", TYPE_LED, 2, 5, 0xb0047f00, 0xb0040000, ATOMIC_INIT(0) },
	{ MP3_OUT_LOOP_L,"LOOP_L", TYPE_LED, 2, 6, 0xb00d7f00, 0xb00d0000, ATOMIC_INIT(0) },
	{ MP3_OUT_LOOP_R,"LOOP_R", TYPE_LED, 2, 7, 0xb0127f00, 0xb0120000, ATOMIC_INIT(0) },
	{ MP3_OUT_PLAY_PAUSE_BLINK_L,"PLAY_PAUSE_BLINK_L", TYPE_LED, 3, 0, 0xb0007f00, 0xb0000000, ATOMIC_INIT(0) },
	{ MP3_OUT_PLAY_PAUSE_BLINK_R,"PLAY_PAUSE_BLINK_R", TYPE_LED, 3, 1, 0xb0057f00, 0xb0050000, ATOMIC_INIT(0) },
	{ MP3_OUT_DISABLE_MOUSE,"DISABLE_DJ_MOUSE", TYPE_SETTING, 3, 2, 0xb0207f00, 0xb0200000, ATOMIC_INIT(0) },
	{ MP3_OUT_FLUSH_ANALOGS,"FLUSH_ANALOGS", TYPE_LED, 3, 3, 0xb07f7f00, 0xb07f0000, ATOMIC_INIT(0) }
};	

/* Mapping from HID report to MIDI commands for input, for the MP3 */
static struct controller_control_details mp3_input_control_details[] = {
	{ MP3_IN_FX_SELECT_R,"FX_SELECT_R", TYPE_BUTTON, 1, 0, 0xb0017f00, 0xb0010000, ATOMIC_INIT(0) },
	{ MP3_IN_PLAY_PAUSE_R,"PLAY_PAUSE_R", TYPE_BUTTON, 1, 1, 0xb0027f00, 0xb0020000, ATOMIC_INIT(0) },
	{ MP3_IN_CUE_R,"CUE_R", TYPE_BUTTON, 1, 2 , 0xb0037f00, 0xb0030000, ATOMIC_INIT(0) },
	{ MP3_IN_AUTOBEAT_R,"AUTOBEAT_R", TYPE_BUTTON, 1, 3, 0xb0047f00, 0xb0040000, ATOMIC_INIT(0) },
	{ MP3_IN_PREV_TRACK_R,"PREV_TRACK_R", TYPE_BUTTON, 1, 4, 0xb0057f00, 0xb0050000, ATOMIC_INIT(0) },
	{ MP3_IN_NEXT_TRACK_R,"NEXT_TRACK_R", TYPE_BUTTON, 1, 5, 0xb0067f00, 0xb0060000, ATOMIC_INIT(0) },
	{ MP3_IN_FX_SELECT_L,"FX_SELECT_L", TYPE_BUTTON, 1, 6, 0xb0077f00, 0xb0070000, ATOMIC_INIT(0) },
	{ MP3_IN_PLAY_PAUSE_L,"PLAY_PAUSE_L", TYPE_BUTTON, 1, 7, 0xb0087f00, 0xb0080000, ATOMIC_INIT(0) },
	{ MP3_IN_CUE_L,"CUE_L", TYPE_BUTTON, 2, 0, 0xb0097f00, 0xb0090000, ATOMIC_INIT(0) },
	{ MP3_IN_AUTOBEAT_L,"AUTOBEAT_L", TYPE_BUTTON, 2, 1, 0xb00a7f00, 0xb00a0000, ATOMIC_INIT(0) },
	{ MP3_IN_PREV_TRACK_L,"PREV_TRACK_L", TYPE_BUTTON, 2, 2, 0xb00b7f00, 0xb00b0000, ATOMIC_INIT(0) },
	{ MP3_IN_NEXT_TRACK_L,"NEXT_TRACK_L", TYPE_BUTTON, 2, 3, 0xb00c7f00, 0xb00c0000, ATOMIC_INIT(0) },
	{ MP3_IN_FX3_L,"FX3_L", TYPE_BUTTON, 2, 4, 0xb00d7f00, 0xb00d0000, ATOMIC_INIT(0) },
	{ MP3_IN_FX2_L,"FX2_L", TYPE_BUTTON, 2, 5, 0xb00e7f00, 0xb00e0000, ATOMIC_INIT(0) },
	{ MP3_IN_FX1_L,"FX1_L", TYPE_BUTTON, 2, 6, 0xb00f7f00, 0xb00f0000, ATOMIC_INIT(0) },
	{ MP3_IN_FX1_R,"FX1_R", TYPE_BUTTON, 2, 7, 0xb0107f00, 0xb0100000, ATOMIC_INIT(0) },
	{ MP3_IN_FX2_R,"FX2_R", TYPE_BUTTON, 3, 0, 0xb0117f00, 0xb0110000, ATOMIC_INIT(0) },
	{ MP3_IN_FX3_R,"FX3_R", TYPE_BUTTON, 3, 1, 0xb0127f00, 0xb0120000, ATOMIC_INIT(0) },
	{ MP3_IN_PITCH_BEND_PLUS_L,"PITCH_BEND_PLUS_L", TYPE_BUTTON, 3, 2, 0xb0137f00, 0xb0130000, ATOMIC_INIT(0) },
	{ MP3_IN_PITCH_BEND_MINUS_L,"PITCH_BEND_MINUS_L", TYPE_BUTTON, 3, 3, 0xb0147f00, 0xb0140000, ATOMIC_INIT(0) },
	{ MP3_IN_MONITOR_L,"MONITOR_L", TYPE_BUTTON, 3, 4, 0xb0157f00, 0xb0150000, ATOMIC_INIT(0) },
	{ MP3_IN_MASTER_TEMPO_L,"MASTER_TEMPO_L", TYPE_BUTTON, 3, 5, 0xb0167f00, 0xb0160000, ATOMIC_INIT(0) },
	{ MP3_IN_PITCH_BEND_PLUS_R,"PITCH_BEND_PLUS_R", TYPE_BUTTON, 3, 6, 0xb0177f00, 0xb0170000, ATOMIC_INIT(0) },
	{ MP3_IN_PITCH_BEND_MINUS_R,"PITCH_BEND_MINUS_R", TYPE_BUTTON, 3, 7, 0xb0187f00, 0xb0180000, ATOMIC_INIT(0) },
	{ MP3_IN_MONITOR_R,"MONITOR_R", TYPE_BUTTON, 4, 0, 0xb0197f00, 0xb0190000, ATOMIC_INIT(0) },
	{ MP3_IN_MASTER_TEMPO_R,"MASTER_TEMPO_R", TYPE_BUTTON, 4, 1, 0xb01a7f00, 0xb01a0000, ATOMIC_INIT(0) },
	{ MP3_IN_MOUSE_L,"MOUSE_L", TYPE_BUTTON, 4, 2, 0xb01b7f00, 0xb01b0000, ATOMIC_INIT(0) },
	{ MP3_IN_MOUSE_R,"MOUSE_R", TYPE_BUTTON, 4, 3, 0xb01c7f00, 0xb01c0000, ATOMIC_INIT(0) },
	{ MP3_IN_BASS_R,"BASS_R", TYPE_LINEAR, 5, 0, 0xb02b7f00, 0xb02b0000, ATOMIC_INIT(0) },
	{ MP3_IN_MEDIUM_R,"MEDIUM_R", TYPE_LINEAR, 6, 0, 0xb02c7f00, 0xb02c0000, ATOMIC_INIT(0) },
	{ MP3_IN_TREBLE_R,"TREBLE_R", TYPE_LINEAR, 7, 0, 0xb02d7f00, 0xb02d0000, ATOMIC_INIT(0) },
	{ MP3_IN_BASS_L,"BASS_L", TYPE_LINEAR, 8, 0, 0xb02e7f00, 0xb02e0000, ATOMIC_INIT(0) },
	{ MP3_IN_MEDIUM_L,"MEDIUM_L", TYPE_LINEAR, 9, 0, 0xb02f7f00, 0xb02f0000, ATOMIC_INIT(0) },
	{ MP3_IN_TREBLE_L,"TREBLE_L", TYPE_LINEAR, 10, 0, 0xb0307f00, 0xb0300000, ATOMIC_INIT(0) },
	{ MP3_IN_XFADER,"XFADER", TYPE_LINEAR, 11, 0, 0xb0317f00, 0xb0310000, ATOMIC_INIT(0) },
	{ MP3_IN_PITCH_L,"PITCH_L", TYPE_INCREMENTAL, 14, 0, 0xb0347f00, 0xb0340000, ATOMIC_INIT(0) },
	{ MP3_IN_PITCH_R,"PITCH_R", TYPE_INCREMENTAL, 15, 0, 0xb0357f00, 0xb0350000, ATOMIC_INIT(0) },
	{ MP3_IN_VOLUME_L,"VOLUME_L", TYPE_LINEAR, 12, 0, 0xb0327f00, 0xb0320000, ATOMIC_INIT(0) },
	{ MP3_IN_VOLUME_R,"VOLUME_R", TYPE_LINEAR, 13, 0, 0xb0337f00, 0xb0330000, ATOMIC_INIT(0) },
	{ MP3_IN_JOG_L,"JOG_L", TYPE_INCREMENTAL, 16, 0, 0xb0367f00, 0xb0360000, ATOMIC_INIT(0) },
	{ MP3_IN_JOG_R,"JOG_R", TYPE_INCREMENTAL, 17, 0, 0xb0377f00, 0xb0370000, ATOMIC_INIT(0) },
	{ MP3_IN_MOUSE_X,"MOUSE_X", TYPE_LINEAR, 18, 0, 0xb0387f00, 0xb0380000, ATOMIC_INIT(0) },
	{ MP3_IN_MOUSE_Y,"MOUSE_Y", TYPE_LINEAR, 19, 0, 0xb0397f00, 0xb0390000, ATOMIC_INIT(0) }
};

static int snd_djc_start_midi(struct snd_hdjmidi *umidi, int midi_mode_to_set, u8 stop_first)
{
	int retval = 0;
	__u8 request;
	int current_midi_mode = atomic_read(&umidi->midi_mode);
	
	printk(KERN_WARNING"%s() midi_mode_to_set:%d stop_first:%d\n",
			__FUNCTION__,midi_mode_to_set,stop_first); /*T*/

	/* this is only valid for the DJC */
	if (umidi->chip->product_code!=DJCONSOLE_PRODUCT_CODE) {
		snd_printk(KERN_WARNING"%s() invalid product:%d\n",
			__FUNCTION__,umidi->chip->product_code);
		return -EINVAL;
	}

	/* validate midi mode to set */
	if (midi_mode_to_set!=MIDI_MODE_CONTROLLER &&
	   midi_mode_to_set!=MIDI_MODE_PHYSICAL_PORT) {
		snd_printk(KERN_WARNING"%s() invalid MIDI mode:%d\n",
			__FUNCTION__,midi_mode_to_set);
		return -EINVAL;
	}

	/* requested to stop first, normal for all cases except startup */
	if (stop_first!=0) {
		if (current_midi_mode==MIDI_MODE_CONTROLLER) {
			request = STOP_VIRTUAL_MIDI_IN_REQUEST;
		} else {
			request = STOP_PHYSICAL_MIDI_IN_REQUEST;
		}
		retval = send_vendor_request(umidi->chip->index,
						REQT_WRITE,
						request,
						0,			
						0,		
						NULL,
						0);
		if (retval!=0) {
			snd_printk(KERN_ERR"%s() request %d failed for stop, current_midi_mode:%d, midi_mode_to_set:%d\n",
				__FUNCTION__,
				request,
				current_midi_mode,
				midi_mode_to_set);
			return retval;
		}
	}
	
	/* now choose correct MIDI mode to start */
	if (midi_mode_to_set==MIDI_MODE_CONTROLLER) {
		request = START_VIRTUAL_MIDI_IN_REQUEST;	
	} else {
		request = START_PHYSICAL_MIDI_IN_REQUEST;
	} 

	retval = send_vendor_request(umidi->chip->index,
					REQT_WRITE,
					request,
					0,			
					0,		
					NULL,
					0);
	if (retval!=0) {
		snd_printk(KERN_ERR"%s() request %d failed for start, current_midi_mode:%d, midi_mode_to_set:%d\n",
			__FUNCTION__,
			request,
			current_midi_mode,
			midi_mode_to_set);
		return retval;
	}
	
	/* success- now record new MIDI mode */
	atomic_set(&umidi->midi_mode, midi_mode_to_set);
					
	return retval;
}

/*
 * Error handling for URB completion functions.
 */
int snd_hdjmidi_urb_error(int status)
{
	switch (status) {
	/* manually unlinked, or device gone */
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
	case -ENODEV:
		return -ENODEV;
	/* errors that might occur during unplugging */
	case -EPROTO:
	case -ETIME:
	case -EILSEQ:
		return -EIO;
	default:
		snd_printk(KERN_ERR "%s() urb status %d\n", __FUNCTION__,status);
		return 0; /* continue */
	}
}

/* called after transfers had been interrupted due to some USB error */
static void snd_hdjmidi_error_timer(unsigned long data)
{
	struct snd_hdjmidi *umidi = (struct snd_hdjmidi *)data;
	int i;
	
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_hdjmidi_in_endpoint *in = umidi->endpoints[i].in;
		if (in && in->error_resubmit) {
			in->error_resubmit = 0;
			in->urb->dev = umidi->chip->dev;
			snd_hdjmidi_submit_urb(umidi, in->urb, GFP_ATOMIC);
		}
		if (umidi->endpoints[i].out)
			snd_hdjmidi_do_output(umidi->endpoints[i].out);
	}
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
static void output_control_ctrl_complete(struct urb* urb)
#else
static void output_control_ctrl_complete(struct urb* urb, struct pt_regs *junk)
#endif
{
	struct controller_output_hid *controller_out_state = urb->context;

	if (controller_out_state==NULL) {
		snd_printk(KERN_WARNING"%s(): context NULL, bailing, may not wake up client!\n",__FUNCTION__);
		return;
	}
	complete(&controller_out_state->output_control_ctl_completion);
} 

/*
 * Frees an input endpoint.
 * May be called when ep hasn't been initialized completely.
 */
static void snd_hdjmidi_in_endpoint_delete(struct snd_hdjmidi_in_endpoint* ep)
{
	if (ep->urb) {
		if (ep->urb->transfer_buffer) {
			usb_free_coherent(ep->umidi->chip->dev,
					ep->urb->transfer_buffer_length,
					ep->urb->transfer_buffer,
					ep->urb->transfer_dma);
		}
		usb_free_urb(ep->urb);
	}

	if (ep->controller_state) {
		if (ep->controller_state->current_hid_report_data!=NULL) {
			kfree(ep->controller_state->current_hid_report_data);	
		}
		
		if (ep->controller_state->control_details!=NULL) {
			kfree(ep->controller_state->control_details);	
		}
		
		kfree(ep->controller_state);
	}
	kfree(ep);
}

static int controller_input_init(struct controller_input_hid *controller_state,
									struct snd_hdjmidi_in_endpoint* ep)
{
	int i;

	if (ep->umidi->chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		ep->controller_state->is_weltrend = is_mp3_weltrend(ep->umidi->chip->usb_id);
		controller_state->num_controls = DJ_MP3_NUM_INPUT_CONTROLS;
		controller_state->current_hid_report_data_len = DJ_MP3_HID_INPUT_REPORT_LEN;
		atomic_set(&controller_state->buffered_hid_data,0);
		if ( sizeof(mp3_input_control_details)/sizeof(struct controller_control_details) != 
			DJ_MP3_NUM_INPUT_CONTROLS) {
			snd_printk(KERN_WARNING"%s() invalid mp3 controls number, expected:%d, got:%zd\n",
				__FUNCTION__,
				DJ_MP3_NUM_INPUT_CONTROLS,
				sizeof(mp3_input_control_details)/sizeof(struct controller_control_details));
			return -EINVAL;
		}
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",__FUNCTION__,
				ep->umidi->chip->product_code);
		return -EINVAL;	
	}

	spin_lock_init(&controller_state->buffer_lock);
	controller_state->control_details = 
		zero_alloc((controller_state->num_controls)*sizeof(struct controller_control_details),
					GFP_KERNEL);
	if (controller_state->control_details==NULL) {
		printk(KERN_WARNING"%s() kmalloc failed for controller details\n",__FUNCTION__);
		return -ENOMEM;	
	}
	
	controller_state->current_hid_report_data = 
		zero_alloc(controller_state->current_hid_report_data_len,GFP_KERNEL);
	if (controller_state->current_hid_report_data==NULL) {
		printk(KERN_WARNING"%s() kmalloc failed for hid control buffer\n",__FUNCTION__);
		return -ENOMEM;	
	}
	
	controller_state->last_hid_report_data = 
		zero_alloc(controller_state->current_hid_report_data_len,GFP_KERNEL);
	if (controller_state->last_hid_report_data==NULL) {
		printk(KERN_WARNING"%s() kmalloc failed for last hid control buffer\n",__FUNCTION__);
		return -ENOMEM;	
	}
	
	if (ep->umidi->chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		memcpy(controller_state->control_details, 
			mp3_input_control_details,
			sizeof(struct controller_control_details)*DJ_MP3_NUM_INPUT_CONTROLS);
		controller_state->current_hid_report_data[0] = DJ_MP3_HID_REPORT_ID;
		controller_state->last_hid_report_data[0] = DJ_MP3_HID_REPORT_ID;
		for (i = 0; i < DJ_MP3_NUM_INPUT_CONTROLS; i++) {
			controller_state->control_details[i].midi_message_pressed = 
				__cpu_to_be32(controller_state->control_details[i].midi_message_pressed);
			controller_state->control_details[i].midi_message_released =
				__cpu_to_be32(controller_state->control_details[i].midi_message_released);
		}
	}
	
	return 0;
}

/*
 * Creates an input endpoint.
 */
static int snd_hdjmidi_in_endpoint_create(struct snd_hdjmidi* umidi,
					  struct snd_hdjmidi_endpoint_info* ep_info,
					  struct snd_hdjmidi_endpoint* rep,
					  int endpoint_number)
{
	struct snd_hdjmidi_in_endpoint* ep;
	void* buffer;
	unsigned int pipe;
	int length;

	rep->in = NULL;
	ep = kmalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep) {
		snd_printk(KERN_WARNING"%s() failed to allocate input endpoint\n",__FUNCTION__);
		return -ENOMEM;
	}
	memset(ep,0,sizeof(*ep));
	ep->umidi = umidi;
	ep->endpoint_number = endpoint_number;

	if (umidi->chip->product_code == DJCONTROLLER_PRODUCT_CODE) {
		ep->controller_state = kmalloc(sizeof(*ep->controller_state),GFP_KERNEL);
		if (ep->controller_state==NULL) {
			snd_printk(KERN_WARNING"%s() failed to allocate MP3 state, bailing\n",__FUNCTION__);
			snd_hdjmidi_in_endpoint_delete(ep);
			return -ENOMEM;
		}
		memset(ep->controller_state,0,sizeof(*ep->controller_state));
		if (controller_input_init(ep->controller_state, ep)!=0) {
			snd_printk(KERN_WARNING"%s() controller_input_init() failed, bailing\n",__FUNCTION__);
			return -EINVAL;
		}
	}

	ep->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ep->urb) {
		snd_printk(KERN_WARNING"%s() usb_alloc_urb failed\n",__FUNCTION__);
		snd_hdjmidi_in_endpoint_delete(ep);
		return -ENOMEM;
	}
	
	if (ep_info->in_interval) {
		pipe = usb_rcvintpipe(umidi->chip->dev, ep_info->in_ep);
	} else {
		pipe = usb_rcvbulkpipe(umidi->chip->dev, ep_info->in_ep);
	}
	length = usb_maxpacket(umidi->chip->dev, pipe, 0);
	buffer = usb_alloc_coherent(umidi->chip->dev, length, GFP_KERNEL,
				  &ep->urb->transfer_dma);
	if (!buffer) {
		snd_printk(KERN_WARNING"%s() usb_alloc_coherent failed\n",__FUNCTION__);
		snd_hdjmidi_in_endpoint_delete(ep);
		return -ENOMEM;
	}
	
	if (ep_info->in_interval) {
		usb_fill_int_urb(ep->urb, umidi->chip->dev, pipe, buffer,
				 length, snd_hdjmidi_in_urb_complete, ep,
				 ep_info->in_interval);
	} else {
		usb_fill_bulk_urb(ep->urb, umidi->chip->dev, pipe, buffer,
				  length, snd_hdjmidi_in_urb_complete, ep);
	}
	ep->urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;

	rep->in = ep;
	return 0;
}

/*
 * Frees an output endpoint.
 * May be called when ep hasn't been initialized completely.
 */
static void snd_hdjmidi_out_endpoint_delete(struct snd_hdjmidi_out_endpoint* ep)
{
#ifdef THROTTLE_MP3_RENDER
	del_timer_sync(&ep->render_delay_timer);
#endif
	if (ep->urb) {
		if (ep->urb->transfer_buffer) {
			usb_free_coherent(ep->umidi->chip->dev, ep->max_transfer,
					ep->urb->transfer_buffer,
					ep->urb->transfer_dma);
		}
		usb_free_urb(ep->urb);
	}
	if (ep->urb_led) {
		if (ep->urb_led->transfer_buffer) {
			usb_free_coherent(ep->umidi->chip->dev, ep->max_transfer,
					ep->urb_led->transfer_buffer,
					ep->urb_led->transfer_dma);
		}
		usb_free_urb(ep->urb_led);	
	}
	if (ep->ctrl_req_led) {
		usb_free_coherent(ep->umidi->chip->dev, sizeof(*(ep->ctrl_req_led)),
						ep->ctrl_req_led, ep->ctrl_req_led_dma);
	}
	if (ep->controller_state) {
		if (ep->controller_state->output_control_ctl_urb &&
			 ep->controller_state->output_control_ctl_urb->transfer_buffer &&
			 ep->controller_state->output_control_ctl_urb->transfer_dma) {
			usb_free_coherent(ep->umidi->chip->dev, ep->max_transfer,
					ep->controller_state->output_control_ctl_urb->transfer_buffer,
					ep->controller_state->output_control_ctl_urb->transfer_dma);
		}
		if (ep->controller_state->output_control_ctl_req &&
			 ep->controller_state->output_control_ctl_dma) {
			usb_free_coherent(ep->umidi->chip->dev, 
					sizeof(*(ep->controller_state->output_control_ctl_req)),
					ep->controller_state->output_control_ctl_req,
					ep->controller_state->output_control_ctl_dma);
		}
		if (ep->controller_state->output_control_ctl_urb) {
			usb_free_urb(ep->controller_state->output_control_ctl_urb);
		}
		if (ep->controller_state->ctl_req) {
			usb_free_coherent(ep->umidi->chip->dev, 
					sizeof(*(ep->controller_state->ctl_req)),
					ep->controller_state->ctl_req,
					ep->controller_state->ctl_req_dma);
		}
		if (ep->controller_state->mp3w_kthread!=NULL) {
			kthread_stop(ep->controller_state->mp3w_kthread);	
			ep->controller_state->mp3w_kthread = NULL;
		}
		if (ep->controller_state->urb_kt) {
			if (ep->controller_state->urb_kt->transfer_buffer) {
				usb_free_coherent(ep->umidi->chip->dev, ep->max_transfer,
						ep->controller_state->urb_kt->transfer_buffer,
						ep->controller_state->urb_kt->transfer_dma);
			}
			usb_free_urb(ep->controller_state->urb_kt);
		}
		if (ep->controller_state->ctl_req_kt) {
			usb_free_coherent(ep->umidi->chip->dev, 
					sizeof(*(ep->controller_state->ctl_req_kt)),
					ep->controller_state->ctl_req_kt,
					ep->controller_state->ctl_req_dma_kt);
		}
		if (ep->controller_state->control_details) {
			kfree(ep->controller_state->control_details);	
		}
		if (ep->controller_state->current_hid_report_data) {
			kfree(ep->controller_state->current_hid_report_data);	
		}
		kfree(ep->controller_state);
	}
	kfree(ep);
}

static int controller_output_init(struct controller_output_hid *controller_state, 
									struct snd_hdjmidi_out_endpoint* ep)
{
	int i;
	u16 max_transfer;
	void* buffer;

	spin_lock_init(&controller_state->hid_buffer_lock);
	atomic_set(&controller_state->poll_period_jiffies, 0);
	if (ep->umidi->chip->product_code==DJCONTROLLER_PRODUCT_CODE) { 
		controller_state->num_controls = DJ_MP3_NUM_OUTPUT_CONTROLS;
		controller_state->current_hid_report_data_len = DJ_MP3_HID_OUTPUT_REPORT_LEN;
		if ( sizeof(mp3_output_control_details)/sizeof(struct controller_control_details) != 
			DJ_MP3_NUM_OUTPUT_CONTROLS) {
			snd_printk(KERN_WARNING"%s() invalid mp3 controls number, expected:%d, got:%zd\n",
				__FUNCTION__,
				DJ_MP3_NUM_OUTPUT_CONTROLS,
				sizeof(mp3_output_control_details)/sizeof(struct controller_control_details));
			return -EINVAL;
		}
	} else {
		printk(KERN_WARNING"%s() invalid product:%d\n",
				__FUNCTION__,ep->umidi->chip->product_code);
		return -EINVAL;
	}

	
	controller_state->control_details = 
		zero_alloc((controller_state->num_controls)*sizeof(struct controller_control_details),
					GFP_KERNEL);
	if (controller_state->control_details==NULL) {
		printk(KERN_WARNING"%s() kmalloc failed for controller details\n",__FUNCTION__);
		return -ENOMEM;	
	}
	
	controller_state->current_hid_report_data = 
		zero_alloc(controller_state->current_hid_report_data_len,GFP_KERNEL);
	if (controller_state->current_hid_report_data==NULL) {
		printk(KERN_WARNING"%s() kmalloc failed for hid control buffer\n",__FUNCTION__);
		return -ENOMEM;	
	}
			
	if (ep->umidi->chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		memcpy(controller_state->control_details, 
			mp3_output_control_details,
			sizeof(struct controller_control_details)*DJ_MP3_NUM_OUTPUT_CONTROLS);

		for (i = 0; i < DJ_MP3_NUM_OUTPUT_CONTROLS; i++) {
			controller_state->control_details[i].midi_message_pressed = 
				__cpu_to_be32(controller_state->control_details[i].midi_message_pressed);
			controller_state->control_details[i].midi_message_released =
				__cpu_to_be32(controller_state->control_details[i].midi_message_released);
		}

		/* setup the HID report ID */
		controller_state->current_hid_report_data[0] = DJ_MP3_HID_REPORT_ID;
		controller_state->is_weltrend = is_mp3_weltrend(ep->umidi->chip->usb_id);
	}
	
	controller_state->ctl_req = usb_alloc_coherent(ep->umidi->chip->dev, 
							sizeof(*(controller_state->ctl_req)),
							GFP_KERNEL, 
							&controller_state->ctl_req_dma);
	if (controller_state->ctl_req==NULL) {
		snd_printk(KERN_WARNING"%s() usb_alloc_coherent() failed for setup DMA\n",__FUNCTION__);
		return -ENOMEM;
	}
	
	/* this buffer and URB below are for general control requests, like changing the
	 *  mouse setting or setting LEDs */
	/* init_MUTEX(&controller_state->output_control_ctl_mutex); */
    sema_init(&controller_state->output_control_ctl_mutex, 1);
	init_completion(&controller_state->output_control_ctl_completion);
	controller_state->output_control_ctl_req = usb_alloc_coherent(ep->umidi->chip->dev, 
							sizeof(*(controller_state->output_control_ctl_req)),
							GFP_KERNEL, 
							&controller_state->output_control_ctl_dma);
	if (controller_state->output_control_ctl_req==NULL) {
		snd_printk(KERN_WARNING"%s() usb_alloc_coherent() failed for general setup DMA\n",
				__FUNCTION__);
		return -ENOMEM;
	}
	
	controller_state->output_control_ctl_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (controller_state->output_control_ctl_urb==NULL) {
			snd_printk(KERN_WARNING"%s() usb_alloc_urb() failed for general URB\n",
				__FUNCTION__);
		return -ENOMEM;
	}
	
	controller_state->output_control_ctl_pipe = usb_sndctrlpipe(ep->umidi->chip->dev, 0);
	max_transfer = usb_maxpacket(ep->umidi->chip->dev, 
					controller_state->output_control_ctl_pipe, 1);
	
	buffer = usb_alloc_coherent(ep->umidi->chip->dev, max_transfer,
				  GFP_KERNEL, &controller_state->output_control_ctl_urb->transfer_dma);
	if (buffer==NULL) {
		snd_printk(KERN_WARNING"%s() usb_alloc_coherent failed (general URB buffer)\n",
					__FUNCTION__);
		return -ENOMEM;	
	}
	usb_fill_control_urb(controller_state->output_control_ctl_urb, 
				ep->umidi->chip->dev, 
				controller_state->output_control_ctl_pipe,
				(unsigned char *)controller_state->output_control_ctl_req, 
				buffer, 
				controller_state->current_hid_report_data_len,
				output_control_ctrl_complete, 
				controller_state);

	/*
	 *  Fill in the setup packet for HID set report, which will the only type of call which
	 *   we will be making.  The HID set report call will set the LED state, or other state.
	 */
	controller_state->output_control_ctl_req->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	controller_state->output_control_ctl_req->bRequest = USB_REQ_SET_REPORT;
	controller_state->output_control_ctl_req->wValue = cpu_to_le16((USB_HID_OUTPUT_REPORT << 8) + DJ_MP3_HID_REPORT_ID);
	controller_state->output_control_ctl_req->wIndex = cpu_to_le16(ep->umidi->iface->cur_altsetting->desc.bInterfaceNumber);
	controller_state->output_control_ctl_req->wLength = cpu_to_le16(DJ_MP3_HID_OUTPUT_REPORT_LEN);
	controller_state->output_control_ctl_urb->setup_dma = controller_state->output_control_ctl_dma;
	/* NOTE: transfer_dma setup above in call to usb_alloc_coherent() */
	controller_state->output_control_ctl_urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	
	return 0;
}

/*
 * Creates an output endpoint, and initializes output ports.
 */
static int snd_hdjmidi_out_endpoint_create(struct snd_hdjmidi* umidi,
					   struct snd_hdjmidi_endpoint_info* ep_info,
			 		   struct snd_hdjmidi_endpoint* rep,
					   int endpoint_number)
{
	struct snd_hdjmidi_out_endpoint* ep;
	int i;
	unsigned int pipe;
	void* buffer, *buffer_led;

	rep->out = NULL;
	ep = kmalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep) {
		snd_printk(KERN_WARNING"%s() failed to allocate output endpoint\n",__FUNCTION__);
		return -ENOMEM;
	}
	memset(ep,0,sizeof(*ep));
	ep->umidi = umidi;
	ep->endpoint_number = endpoint_number;

	ep->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ep->urb) {
		snd_printk(KERN_WARNING"%s() failed to allocate URB\n",__FUNCTION__);
		snd_hdjmidi_out_endpoint_delete(ep);
		return -ENOMEM;
	}
	
	/* used for clearing LEDs in midi close- no possibility to wait there, so
     *  another URB is the simplest solution */
	ep->urb_led = usb_alloc_urb(0, GFP_KERNEL);
	if (!ep->urb_led) {
		snd_printk(KERN_WARNING"%s() failed to allocate LED URB\n",__FUNCTION__);
		snd_hdjmidi_out_endpoint_delete(ep);
		return -ENOMEM;
	}
	
	if (ep->umidi->chip->caps.leds_hid_controlled) {
		ep->ctrl_req_led = usb_alloc_coherent(ep->umidi->chip->dev, 
								sizeof(*(ep->ctrl_req_led)),
								GFP_KERNEL, 
								&ep->ctrl_req_led_dma);
		if (ep->ctrl_req_led==NULL) {
			snd_printk(KERN_WARNING"%s() usb_alloc_coherent() failed for setup DMA\n",__FUNCTION__);
			return -ENOMEM;
		}
	}

	/* The MP3 has no bulk/int out pipe, just the control pipe for set report calls */
	if (umidi->chip->product_code!=DJCONTROLLER_PRODUCT_CODE) {
		if (ep_info->out_interval) {
			pipe = usb_sndintpipe(umidi->chip->dev, ep_info->out_ep);
		} else {
			pipe = usb_sndbulkpipe(umidi->chip->dev, ep_info->out_ep);
		}
	} else {
		ep->controller_state = zero_alloc(sizeof(*ep->controller_state),GFP_KERNEL);
		if (ep->controller_state==NULL) {
			snd_printk(KERN_WARNING"%s() failed to allocate controller state, bailing\n",
						__FUNCTION__);
			snd_hdjmidi_out_endpoint_delete(ep);
			return -ENOMEM;
		}
		if (controller_output_init(ep->controller_state,ep)!=0) {
			snd_printk(KERN_WARNING"%s() snd_hdjmp3_output_init() failed, bailing\n",
						__FUNCTION__);
			snd_hdjmidi_out_endpoint_delete(ep);
			return -ENOMEM;
		}
		pipe = usb_sndctrlpipe(umidi->chip->dev, 0);
	}
	ep->max_transfer = usb_maxpacket(umidi->chip->dev, pipe, 1);
	buffer = usb_alloc_coherent(umidi->chip->dev, ep->max_transfer,
				  GFP_KERNEL, &ep->urb->transfer_dma);
	if (!buffer) {
		snd_printk(KERN_WARNING"%s() usb_alloc_coherent() failed\n",__FUNCTION__);
		snd_hdjmidi_out_endpoint_delete(ep);
		return -ENOMEM;
	}
	
	buffer_led = usb_alloc_coherent(umidi->chip->dev, ep->max_transfer,
				  GFP_KERNEL, &ep->urb_led->transfer_dma);
	if (!buffer_led) {
		snd_printk(KERN_WARNING"%s() usb_alloc_coherent() failed for LED buffer\n",
					__FUNCTION__);
		snd_hdjmidi_out_endpoint_delete(ep);
		return -ENOMEM;
	}
	
	if (umidi->chip->product_code!=DJCONTROLLER_PRODUCT_CODE) {
		if (ep_info->out_interval) {
			usb_fill_int_urb(ep->urb, umidi->chip->dev, pipe, buffer,
					 ep->max_transfer, snd_hdjmidi_out_urb_complete,
					 ep, ep_info->out_interval);
		} else {
			usb_fill_bulk_urb(ep->urb, umidi->chip->dev,
					  pipe, buffer, ep->max_transfer,
					  snd_hdjmidi_out_urb_complete, ep);
		}
		ep->urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	} else {
		usb_fill_control_urb(ep->urb, 
				umidi->chip->dev, 
				pipe,
				(unsigned char *)ep->controller_state->ctl_req, 
				buffer, 
				ep->controller_state->current_hid_report_data_len,
				hid_ctrl_complete, 
				ep);
		/*
		 *  Fill in the setup packet for HID set report, which will the only type of call which
		 *   we will be making.  The HID set report call will set the LED state.
		 */
		ep->controller_state->ctl_req->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
		ep->controller_state->ctl_req->bRequest = USB_REQ_SET_REPORT;
		ep->controller_state->ctl_req->wValue = cpu_to_le16((USB_HID_OUTPUT_REPORT << 8) + DJ_MP3_HID_REPORT_ID);
		ep->controller_state->ctl_req->wIndex = cpu_to_le16(umidi->iface->cur_altsetting->desc.bInterfaceNumber);
		ep->controller_state->ctl_req->wLength = cpu_to_le16(DJ_MP3_HID_OUTPUT_REPORT_LEN);
		ep->urb->setup_dma = ep->controller_state->ctl_req_dma;
		/* NOTE: transfer_dma setup above in call to usb_alloc_coherent() */
		ep->urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	}
	
	if (ep->umidi->chip->caps.leds_hid_controlled) {
		usb_fill_control_urb(ep->urb_led, 
					umidi->chip->dev, 
					pipe,
					(unsigned char *)ep->ctrl_req_led, 
					buffer_led, 
					ep->umidi->chip->caps.leds_report_len,
					midi_led_clear_complete, 
					ep);
		/* used for clearing LEDs in midi close- no possibility to wait there, so
		 *  another URB is the simplest solution */
		ep->ctrl_req_led->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
		ep->ctrl_req_led->bRequest = USB_REQ_SET_REPORT;
		ep->ctrl_req_led->wValue = cpu_to_le16((USB_HID_OUTPUT_REPORT << 8) + DJ_MP3_HID_REPORT_ID);
		ep->ctrl_req_led->wIndex = cpu_to_le16(umidi->iface->cur_altsetting->desc.bInterfaceNumber);
		ep->ctrl_req_led->wLength = cpu_to_le16(DJ_MP3_HID_OUTPUT_REPORT_LEN);
		ep->urb_led->setup_dma = ep->ctrl_req_led_dma;
		/* NOTE: transfer_dma setup above in call to usb_alloc_coherent() */
		ep->urb_led->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	}
	
	if (ep->umidi->chip->caps.leds_bulk_controlled) {
		usb_fill_bulk_urb(ep->urb_led, umidi->chip->dev,
					  pipe, buffer_led, ep->max_transfer,
					  midi_led_clear_complete, ep);
	}

	spin_lock_init(&ep->buffer_lock);
	snd_hdjmidi_output_initialize_tasklet(ep);
#ifdef THROTTLE_MP3_RENDER
	init_timer(&ep->render_delay_timer);
	ep->render_delay_timer.data = (unsigned long)ep;
	ep->render_delay_timer.function = midi_render_throttle_timer;
#endif

	for (i = 0; i < 0x10; ++i)
		if (ep_info->out_ep) {
			ep->ports[i].ep = ep;
		}

	if (umidi->usb_protocol_ops->init_out_endpoint)
		umidi->usb_protocol_ops->init_out_endpoint(ep);

	rep->out = ep;

	if (umidi->chip->product_code==DJCONTROLLER_PRODUCT_CODE &&
	   ep->controller_state->is_weltrend==1 &&
	   le16_to_cpu(ep->umidi->chip->dev->descriptor.bcdDevice) <= POLL_VERSION ) {
	
		ep->controller_state->urb_kt = usb_alloc_urb(0, GFP_KERNEL);
		if (!ep->controller_state->urb_kt) {
			snd_printk(KERN_WARNING"%s() failed to allocate URB for wq\n",__FUNCTION__);
			snd_hdjmidi_out_endpoint_delete(ep);
			return -ENOMEM;
		}

		buffer = usb_alloc_coherent(umidi->chip->dev, ep->max_transfer,
				  GFP_KERNEL, &ep->controller_state->urb_kt->transfer_dma);
		if (!buffer) {
			snd_printk(KERN_WARNING"%s() usb_alloc_coherent() for wq failed\n",__FUNCTION__);
			snd_hdjmidi_out_endpoint_delete(ep);
			return -ENOMEM;
		}

		ep->controller_state->ctl_req_kt = usb_alloc_coherent(umidi->chip->dev, 
							sizeof(*(ep->controller_state->ctl_req_kt)),
							GFP_KERNEL, 
							&ep->controller_state->ctl_req_dma_kt);
		if (!ep->controller_state->ctl_req_kt) {
			snd_printk(KERN_WARNING"%s() usb_alloc_coherent() failed for setup DMA for wq\n",__FUNCTION__);
			snd_hdjmidi_out_endpoint_delete(ep);
			return -ENOMEM;
		}

		pipe = usb_sndctrlpipe(umidi->chip->dev, 0);
		usb_fill_control_urb(ep->controller_state->urb_kt, 
				umidi->chip->dev, 
				pipe,
				(unsigned char *)ep->controller_state->ctl_req, 
				buffer, 
				ep->controller_state->current_hid_report_data_len,
				hid_ctrl_complete_kt, 
				ep->controller_state);
		ep->controller_state->ctl_req_kt->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
		ep->controller_state->ctl_req_kt->bRequest = USB_REQ_SET_REPORT;
		ep->controller_state->ctl_req_kt->wValue = cpu_to_le16((USB_HID_OUTPUT_REPORT << 8) + DJ_MP3_HID_REPORT_ID);
		ep->controller_state->ctl_req_kt->wIndex = cpu_to_le16(umidi->iface->cur_altsetting->desc.bInterfaceNumber);
		ep->controller_state->ctl_req_kt->wLength = cpu_to_le16(DJ_MP3_HID_OUTPUT_REPORT_LEN);
		ep->controller_state->urb_kt->setup_dma = ep->controller_state->ctl_req_dma_kt;
		/* NOTE: transfer_dma setup above in call to usb_alloc_coherent() */
		ep->controller_state->urb_kt->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	
		init_completion(&ep->controller_state->ctl_req_completion_kt);
		init_completion(&ep->controller_state->mp3w_kthread_started);
		init_waitqueue_head(&ep->controller_state->mp3w_kthread_wait);
		ep->controller_state->mp3w_kthread = kthread_create(mp3w_kthread_entry,ep,"mp3w_kthread%d",
						atomic_read(&ep->umidi->chip->next_midi_device)-1);
		if (ep->controller_state->mp3w_kthread==NULL) {
			snd_printk(KERN_WARNING"%s() kthread_create() returned NULL\n",__FUNCTION__);
			snd_hdjmidi_out_endpoint_delete(ep);
			return -ENOMEM;
		} else {
			wake_up_process(ep->controller_state->mp3w_kthread);
			/* after this returns we can call kstop without fear */
			wait_for_completion(&ep->controller_state->mp3w_kthread_started);
		}
	}
	
	return 0;
}

static struct port_info *find_port_info(struct snd_hdjmidi* umidi, int number)
{
	/*int i;*/
	
	/* TODO- see if we need an entry here
	for (i = 0; i < ARRAY_SIZE(snd_usbmidi_port_info); ++i) {
		if (snd_usbmidi_port_info[i].id == umidi->chip->usb_id &&
		    snd_usbmidi_port_info[i].port == number)
			return &snd_usbmidi_port_info[i];
	}*/
	return NULL;
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
static struct snd_rawmidi_substream *snd_hdjmidi_find_substream(struct snd_hdjmidi* umidi,
							   	int stream, 
								int number)
#else
static snd_rawmidi_substream_t *snd_hdjmidi_find_substream(struct snd_hdjmidi* umidi,
							   	int stream, 
								int number)
#endif
{
	struct list_head* list;
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
	list_for_each(list, &umidi->rmidi->streams[stream].substreams) {
		struct snd_rawmidi_substream *substream = list_entry(list, struct snd_rawmidi_substream, list);
		if (substream->number == number)
			return substream;
	}
#else
	list_for_each(list, &umidi->rmidi->streams[stream].substreams) {
		snd_rawmidi_substream_t* substream = list_entry(list, snd_rawmidi_substream_t, list);
		if (substream->number == number)
			return substream;
	}
#endif
	return NULL;
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
static void snd_hdjmidi_init_substream(struct snd_hdjmidi* umidi,
				       int stream, 
				       int number,
				       struct snd_rawmidi_substream ** rsubstream)
#else
static void snd_hdjmidi_init_substream(struct snd_hdjmidi* umidi,
				       int stream, 
				       int number,
				       snd_rawmidi_substream_t ** rsubstream)
#endif
{
	struct port_info *port_info;
	const char *name_format;
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
	struct snd_rawmidi_substream *substream;
#else
	snd_rawmidi_substream_t *substream;
#endif
	
	*rsubstream = NULL;
	
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
	substream = snd_hdjmidi_find_substream(umidi, stream, number);
#else
	substream = snd_hdjmidi_find_substream(umidi, stream, number);
#endif
	
	if (!substream) {
		snd_printd(KERN_ERR "snd_hdjmidi_init_substream(): substream %d:%d not found\n", stream, number);
		return;
	}

	port_info = find_port_info(umidi, number);
	/* TODO- check name */
	name_format = port_info ? port_info->name : "%s MIDI %d";
	snprintf(substream->name, sizeof(substream->name),
		 name_format, umidi->chip->card->shortname, number + 1);

	*rsubstream = substream;
}

/*
 * Creates the endpoints and their ports.
 */
static int snd_hdjmidi_create_endpoints(struct snd_hdjmidi* umidi,
					struct snd_hdjmidi_endpoint_info* endpoints)
{
	int i, j, err = 0;
	int out_ports = 0, in_ports = 0;

	/*
	 * Since we do not drive usbmidi devices I have replaced the cable references with endpoint
	 *  numbers, which serve adequately here.
	 */
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		if (endpoints[i].out_ep) {
			err = snd_hdjmidi_out_endpoint_create(umidi, &endpoints[i],
							      &umidi->endpoints[i],
							      i);
			if (err < 0)
				return err;
		}
		if (endpoints[i].in_ep) {
			err = snd_hdjmidi_in_endpoint_create(umidi, &endpoints[i],
							     &umidi->endpoints[i],
							     i);
			if (err < 0)
				return err;
		}

		/* we only have 1 input and 1 output stream for all of our devices */
		for (j = 0; j < 1; ++j) {
			if (endpoints[i].out_ep) {
				snd_hdjmidi_init_substream(umidi, SNDRV_RAWMIDI_STREAM_OUTPUT, out_ports,
							   &umidi->endpoints[i].out->ports[j].substream);
				if (umidi->endpoints[i].out->ports[j].substream==NULL) {
					printk(KERN_WARNING"%s failed to initialize output stream#:%d\n",
							__FUNCTION__, out_ports);
					return -EINVAL;
				}
				++out_ports;
			}
			if (endpoints[i].in_ep) {
				snd_hdjmidi_init_substream(umidi, SNDRV_RAWMIDI_STREAM_INPUT, in_ports,
							   &umidi->endpoints[i].in->ports[j].substream);	
				if (umidi->endpoints[i].in->ports[j].substream==NULL) {
					printk(KERN_WARNING"%s failed to initialize input stream#:%d\n",
							__FUNCTION__, in_ports);
					return -EINVAL;	
				}
				++in_ports;
			}
		}
	}
	snd_printdd(KERN_INFO "snd_hdjmidi_create_endpoints(): created %d output and %d input ports\n",
		    out_ports, in_ports);
	return 0;
}

/*
 * Returns MIDIStreaming device capabilities- currently, only the endpoint addresses.
 */
static int snd_hdjmidi_get_ms_info(struct snd_hdjmidi* umidi,
			   	   struct snd_hdjmidi_endpoint_info* endpoints)
{
	struct usb_interface* intf;
	struct usb_host_interface *hostif;
	struct usb_interface_descriptor* intfd;
	struct usb_host_endpoint *hostep;
	struct usb_endpoint_descriptor* ep;
	int i, epidx;

	intf = umidi->iface;
	if (!intf) {
		snd_printk(KERN_WARNING"%s() umidi->iface==NULL\n",__FUNCTION__);
		return -ENXIO;
	}
	hostif = &intf->altsetting[0];
	intfd = get_iface_desc(hostif);
	
	epidx = 0;
	for (i = 0; i < intfd->bNumEndpoints; ++i) {
		hostep = &hostif->endpoint[i];
		ep = get_ep_desc(hostep);
		if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_BULK &&
		    (ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_INT)
			continue;
		
		if ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT) {
			if (endpoints[epidx].out_ep) {
				if (++epidx >= MIDI_MAX_ENDPOINTS) {
					snd_printk(KERN_WARNING "%s() too many endpoints\n",__FUNCTION__);
					break;
				}
			}
			endpoints[epidx].out_ep = ep->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
			if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
				endpoints[epidx].out_interval = ep->bInterval;
		} else {
			if (endpoints[epidx].in_ep) {
				if (++epidx >= MIDI_MAX_ENDPOINTS) {
					snd_printk(KERN_WARNING "%s() too many endpoints\n",__FUNCTION__);
					break;
				}
			}
			endpoints[epidx].in_ep = ep->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
			if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
				endpoints[epidx].in_interval = ep->bInterval;

			if (umidi->chip->caps.hid_support_only==1) {
				/* The MP3 devices don't have bulk out endpoints, so we will convert 
`				 * appropriate MIDI messages to HID set report calls.  If a MIDI message 
				 * cannnot be mapped to an HID set report call, then it will be silently
				 * drained and dropped.  Thus, we force a bogus endpoint as below so we we can 
				 * store our state information for this virtual endpoint.  Note that
				 * snd_hdjmidi_create_endpoints will initialize the endpoint only if
				 * marked as non-zero.  So we do so.
				 */
				endpoints[epidx].out_ep = 0xff;
			}
		}
	}
	return 0;
}

/*
 * Frees everything.
 */
struct usb_class_driver hdjmidi_class;
void snd_hdj_free(struct snd_hdjmidi* umidi)
{
	int i;
	
	if (umidi==NULL) {
		return;	
	}

	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_hdjmidi_endpoint* ep = &umidi->endpoints[i];
		if (ep && ep->out)
			snd_hdjmidi_out_endpoint_delete(ep->out);
		if (ep && ep->in)
			snd_hdjmidi_in_endpoint_delete(ep->in);
	}
	
	/* give back our minor- now no new I/O is possible */
	if (umidi->registered_usb_dev!=0 && umidi->iface_original!=NULL) {
		usb_deregister_dev(umidi->iface_original, &hdjmidi_class);
		umidi->registered_usb_dev = 0;
	}
	
	/* deref our interface and devices */
	if (umidi->iface!=NULL) {
		dereference_usb_intf_and_devices(umidi->iface);
	}
	
	if (umidi->iface_original!=NULL) {
		dereference_usb_intf_and_devices(umidi->iface_original);
	}
	
	kfree(umidi);
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
static void snd_hdj_rawmidi_free(struct snd_rawmidi *rmidi)
#else
static void snd_hdj_rawmidi_free(snd_rawmidi_t *rmidi)
#endif
{
	/* now done in callback to chip free */
	/*struct snd_hdjmidi* umidi = rmidi->private_data;*/
	/*snd_hdj_free(umidi);*/
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18) )
static void snd_hdj_get_port_info(struct snd_rawmidi *rmidi, 
				int number,
				struct snd_seq_port_info *seq_port_info)
{
	/*snd_printk(KERN_INFO"%s() is called but we are not reporting caps\n",__FUNCTION__);*/
	/*
		TODO- see if we need to add anything to the port caps- the USBMIDI driver does not
			for the DJ Console Mac version (which follows the usbmidi spec).

	struct snd_hdjmidi *umidi = rmidi->private_data;
	struct port_info *port_info;

	port_info = find_port_info(umidi, number);
	if (port_info) {
		seq_port_info->type = port_info->seq_flags;
		seq_port_info->midi_voices = port_info->voices;
	}*/
}

static struct snd_rawmidi_global_ops snd_hdjmidi_ops = {
	.get_port_info = snd_hdj_get_port_info
};
#endif

static void snd_init_midi_channel_list(void)
{
	int i;

	/* mark all channels as free */
	for (i=0; i < NUM_MIDI_CHANNELS; i++) {
		channel_list[i].channel = FREE_MIDI_CHANNEL;
		channel_list[i].umidi = NULL;
	}
}

static int hdjmidi_open(struct inode *inode, struct file *file)
{
	int chip_index;
	struct snd_hdj_chip* chip=NULL;
	struct usb_interface *interface;
	int subminor;
	
	subminor = iminor(inode);
	interface = usb_find_interface(&hdj_driver, subminor);
	if (!interface) {
		printk(KERN_WARNING"%s() - error, can't find device for minor %d\n",
		     __FUNCTION__, subminor);
		return -ENODEV;
	}
	chip_index = (int)(unsigned long)usb_get_intfdata(interface);
	
	/* we don't need access to the chip here, but will deref it in release */
	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s() inc_chip_ref_count() returned NULL, bailing!\n",__FUNCTION__);
		return -EINVAL;
	}
	
	/* increment the chip again to make sure it stays alive until release */
	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s() second inc_chip_ref_count failed\n",__FUNCTION__);
		dec_chip_ref_count(chip_index);
		return -ENODEV;
	}
	
	/* other fops can access the chip through the index */
	file->private_data = (void*)(unsigned long)chip_index;
	
	/* only deref once, to keep chip alive until release is called */
	dec_chip_ref_count(chip_index);
	
	return 0;
}

static int hdjmidi_release(struct inode *inode, struct file *file)
{
	int chip_index;
	struct snd_hdj_chip* chip=NULL;
	
	chip_index = (int)(unsigned long)file->private_data;
	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s() no context, bailing!\n",__FUNCTION__);
		return -ENODEV;
	}
	
 	/* if a notification is attached to this file object, unregister it */
	unregister_for_netlink(chip,file);

	if (chip) {
		/* This balances the increment which we performed in this function */
		dec_chip_ref_count(chip_index);

		/* decrement the chip again to balance the count with open */
		dec_chip_ref_count(chip_index);
	}
	return 0;
}

/* This interface is only available when bulk IOCTLs are not available */
static long hdjmidi_ioctl(struct file *file,	
							 unsigned int ioctl_num,	
							 unsigned long ioctl_param,
							 u8 compat_mode)
{
	int err=0,chip_index;
	struct snd_hdj_chip* chip=NULL;
	int access=0;
	u32 size;
	u16 value16 = 0;
	u32 value32 = 0;
	unsigned long ctouser=0;
	unsigned long cfromuser=0;
	unsigned long context=0; 
	void *control_data_and_mask=NULL;
	u32 __user *value32p_user;
	u16 __user *value16p_user;
	unsigned long __user *valueulp_user;
#ifdef CONFIG_COMPAT
	compat_long_t __user *valueclp_user;
	compat_long_t context_compat;
#endif
	int __user * valueip_user;
	chip_index = (int)(unsigned long)file->private_data;
	chip = inc_chip_ref_count(chip_index);
	if (!chip) {
		printk(KERN_WARNING"%s() no context, bailing!\n",__FUNCTION__);
		return -ENODEV;
	}
	
	switch (ioctl_num) {
	case DJ_IOCTL_GET_VERSION:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_VERSION\n",
						__FUNCTION__);
		access = access_ok(VERIFY_WRITE,ioctl_param, sizeof(u32));
		if (access!=0) {
			value32p_user = (u32 __user *)ioctl_param;
			err = __put_user(driver_version,value32p_user);
			if (err!=0) {
				printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						err);
			} else {
				ioctl_trace_printk(KERN_INFO"%s() returning version:%x\n",
							__FUNCTION__,driver_version);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			err = -EFAULT;
		}
	break;
	case DJ_IOCTL_GET_FIRMWARE_VERSION:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_FIRMWARE_VERSION\n",
				__FUNCTION__);
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			err = get_firmware_version(chip,&value16,1);
			if (err==0) {
				value16p_user = (u16 __user *)ioctl_param;
				err = __put_user(value16, value16p_user);
				if (err != 0) {
						printk(KERN_WARNING"%s() ioctl received(), __put_user failed, \
								rc:%d\n",__FUNCTION__,err);
				}
			} else {
				printk(KERN_WARNING"%s get_firmware_version() failed, rc:%d\n",
						__FUNCTION__,err);	
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			err = -EFAULT;	
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
			ctouser = copy_to_user((void*)ioctl_param,(void*)&chip->usb_device_path[0],
									LOCATION_ID_LEN);
			if (ctouser == 0) {
				err = 0;
			} else {
				printk(KERN_WARNING"%s() ioctl received(), copy_to_user failed, ctouser:%lu\n",
					__FUNCTION__,
					ctouser);
				err = -EFAULT;
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			err = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_PRODUCT_CODE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_PRODUCT_CODE\n",
						__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u32));
		if (access) {
			value32 = chip->product_code;

			/*copy the kernel mode buffer to usermode*/
			value32p_user = (u32 __user *)ioctl_param;
			err = __put_user(value32, value32p_user);
			if (err != 0) {
				printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
					__FUNCTION__,
					err);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n", __FUNCTION__);
			err = -EFAULT;
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
			err = __get_user(value16, value16p_user);
			if (err == 0) {
				err = set_midi_channel(chip, (u16*)&value16);
				if (err==0) {
					/* Since another channel could have been applied, copy back the
					 *  actual channel applied to user mode
					 */
					err = __put_user(value16, value16p_user);
					if (err != 0) {
						printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
							__FUNCTION__,
							err);
					} 
				} else {
					printk(KERN_ERR"%s() set_midi_channel failed, rc:%d\n",
						__FUNCTION__,
						err);
				}
			} else {
				printk(KERN_WARNING"%s() __get_user failed, result:%d\n",
						__FUNCTION__,
						err);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			err = -EFAULT;
		}
		break;
	case DJ_IOCTL_GET_MIDI_CHANNEL:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_MIDI_CHANNEL\n",
						__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			err = get_midi_channel(chip, &value16);
			if (err==0) {
				/*copy the kernel mode buffer to usermode*/
				value16p_user = (u16 __user *)ioctl_param;
				err = __put_user(value16, value16p_user);
				if (err != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						err);
				}
			} else {
				printk(KERN_ERR"%s() get_midi_channel failed, rc:%d\n",
						__FUNCTION__,
						err);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			err = -EFAULT;
		}
		break;
	case DJ_IOCTL_DISABLE_MOUSE:
		printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_DISABLE_MOUSE\n",__FUNCTION__);
		err = set_mouse_state(chip,0);
		if (err!=0) {
			printk(KERN_ERR"%s() set_dj_mouse_state_ioctl() failed, rc:%d",
					__FUNCTION__,err);
		}
	break;
	case DJ_IOCTL_ENABLE_MOUSE:
		printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_ENABLE_MOUSE\n",__FUNCTION__);
		err = set_mouse_state(chip,1);
		if (err!=0) {
			printk(KERN_ERR"%s() set_dj_mouse_state_ioctl() failed, rc:%d",
					__FUNCTION__,err);
		}
	break;
	case DJ_IOCTL_GET_MOUSE_STATE:
		printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_MOUSE_STATE\n",__FUNCTION__);

		/*verify that the address isn't in kernel mode*/
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u16));
		if (access) {
			err = get_mouse_state(chip, &value16);
			if (err==0) {
				/*copy the kernel mode buffer to usermode */
				value16p_user = (u16 __user *)ioctl_param;
				err = __put_user(value16, value16p_user);
				if (err != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,
						err);
				}
			} else {
				printk(KERN_ERR"%s() get_mouse_state() failed, rc:%d\n",
						__FUNCTION__,
						err);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			err = -EFAULT;
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
			err = -EINVAL;
			break;
#else
			access = access_ok(VERIFY_READ,ioctl_param,sizeof(compat_long_t));
#endif
		}
		if (access) {
			if (compat_mode==0) {
				valueulp_user = (unsigned long __user *)ioctl_param;
				err = __get_user(context, valueulp_user);
			} else {
#ifdef CONFIG_COMPAT
				valueclp_user = (compat_long_t __user *)ioctl_param;
				err = __get_user(context_compat, valueclp_user);
				if (err==0) {
					context = (unsigned long)context_compat;
				}
#else
				/* this should be unreachable */
				printk(KERN_WARNING"%s CONFIG_COMPAT not defined, yet compat_mode!=0\n",
							__FUNCTION__);
				err = -EINVAL;
				break;
#endif
			}
			if (err == 0) {
				err = register_for_netlink(chip, file, (void*)context, compat_mode);
				if (err!=0) {
					printk(KERN_ERR"%s() register_for_netlink() failed, rc:%d",
							__FUNCTION__,err);
				}
			} else {
				printk(KERN_WARNING"%s() ioctl received(), __get_user failed, result:%d\n",
							__FUNCTION__,
							err);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			err = -EFAULT;
		}
		break;	
	case DJ_IOCTL_UNREGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS:
		printk(KERN_INFO"%s() received IOCTL:  IOCTL_UNREGISTER_FOR_NETLINK\n",__FUNCTION__);
		err = unregister_for_netlink(chip, file);
		if (err!=0) {
			printk(KERN_ERR"%s() unregister_for_netlink() failed, rc:%d",
					__FUNCTION__,err);
		}
	break; 
	case DJ_IOCTL_ACQUIRE_NETLINK_UNIT:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_ACQUIRE_NETLINK_UNIT\n",
					__FUNCTION__);
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(int));
		if (access) {
			valueip_user = (int __user *)ioctl_param;
			err = __put_user(netlink_unit, valueip_user);
			if (err != 0) {
				printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
					__FUNCTION__,
					err);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			err = -EFAULT;
		}
	break;
	case DJ_IOCTL_IS_DEVICE_ALIVE:
		printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_IS_DEVICE_ALIVE\n",__FUNCTION__);
		/* check if device is still present on bus */
		err = is_device_present(chip);
		if (err!=0) {
			printk(KERN_ERR"%s() is_device_present() failed, rc:%d\n",
				__FUNCTION__,err);
		}
	break;
	case DJ_IOCTL_GET_CONTROL_DATA_INPUT_PACKET_SIZE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_CONTROL_DATA_INPUT_PACKET_SIZE\n",
					__FUNCTION__);
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u32));
		if (access) {
			err = get_input_control_data_len(chip,&value32);
			if (err==0) {
				/*copy the kernel mode buffer to usermode*/
				value32p_user = (u32 __user *)ioctl_param;
				err = __put_user(value32,value32p_user);
				if (err != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,err);
				}
			} else {
				printk(KERN_WARNING"%s() get_input_control_data_len() failed, rc:%d\n",
						__FUNCTION__, err);
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			err = -EFAULT;
		}
	break;
	case DJ_IOCTL_GET_CONTROL_DATA_OUTPUT_PACKET_SIZE:
		ioctl_trace_printk(KERN_INFO"%s() received IOCTL:  DJ_IOCTL_GET_CONTROL_DATA_OUTPUT_PACKET_SIZE\n",
					__FUNCTION__);
		access = access_ok(VERIFY_WRITE,ioctl_param,sizeof(u32));
		if (access) {
			err = get_output_control_data_len(chip,&value32);
			if (err==0) {
				/*copy the kernel mode buffer to usermode*/
				value32p_user = (u32 __user *)ioctl_param;
				err = __put_user(value32, value32p_user);
				if (err != 0) {
					printk(KERN_WARNING"%s() ioctl received(), __put_user failed, result:%d\n",
						__FUNCTION__,err);
				}
			} else {
				printk(KERN_WARNING"%s() get_output_control_data_len() failed, rc:%d\n",
						__FUNCTION__, err);	
			}
		} else {
			printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
			err = -EFAULT;
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
		err = get_output_control_data_len(chip,&value32);
		if (err==0) {
			/* We receive the control data and a bit mask, so we expect twice the control data length, not
			 *   counting the report ID */
			size = (value32-1)*2;
			if (size <= 0) {
				printk(KERN_WARNING"%s calculated size invalid:%d\n",__FUNCTION__,size);
				err = -EINVAL;
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
						err = send_control_output_report(chip,
														control_data_and_mask,
														size);
						if (err!=0) {
							printk(KERN_WARNING"%s() send_control_output_report failed(), rc:%d\n",
								__FUNCTION__,err);
						}
					} else {
						printk(KERN_WARNING"%s() ioctl received(), copy_from_user failed, \
								cfromuser:%lu\n",__FUNCTION__,cfromuser);
						err = -EFAULT;
					}
					kfree(control_data_and_mask);
				} else {
					printk(KERN_WARNING"%s() kmalloc failed\n",__FUNCTION__);
					err = -ENOMEM;
				}
			} else {
				printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
				err = -EFAULT;
			}
		} else {
			printk(KERN_WARNING"%s() get_output_control_data_len() failed, rc:%d\n",
						__FUNCTION__, err);
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
		err = get_output_control_data_len(chip,&value32);
		if (err==0) {
			access = access_ok(VERIFY_WRITE,ioctl_param,value32);
			if (access) {
				err = get_control_output_report(chip,(u8 __user *)ioctl_param,value32);
				if (err != 0) {
					printk(KERN_WARNING"%s() get_control_output_report() failed, rc:%d\n",
						__FUNCTION__,err);
				}
			} else {
				printk(KERN_WARNING"%s() ioctl access_ok failed\n",__FUNCTION__);
				err = -EFAULT;
			}
		} else {
			printk(KERN_WARNING"%s() get_output_control_data_len() failed, rc:%d\n",
					__FUNCTION__,err);
		}
	break;
	default:
		printk(KERN_INFO"%s(): INVALID ioctl received: ioctl_num:0x%x, ioctl_param:0x%lx\n",
			__FUNCTION__,
			ioctl_num,
			ioctl_param);
		err = -ENOTTY;
		break;	
	}
	dec_chip_ref_count(chip_index);
	return err;
}

static long hdjmidi_ioctl_entry(struct file *file,	
								 unsigned int ioctl_num,	
								 unsigned long ioctl_param)
{
	return hdjmidi_ioctl(file, ioctl_num, ioctl_param, 0);
}

#ifdef CONFIG_COMPAT
static long hdjmidi_ioctl_entry_compat(struct file *file,	
										 unsigned int ioctl_num,	
										 unsigned long ioctl_param)
{
	return hdjmidi_ioctl(file, ioctl_num, ioctl_param, 1);
}
#endif

static const struct file_operations snd_hdjmidi_fops =
{
			.owner =        THIS_MODULE,
	        .open =         hdjmidi_open,
	        .release =      hdjmidi_release,
#ifdef CONFIG_COMPAT
			.compat_ioctl = hdjmidi_ioctl_entry_compat,
#endif
			.unlocked_ioctl =       hdjmidi_ioctl_entry
};

/*
* usb class driver info in order to get a minor number from the usb core,
* and to have the device registered with the driver core
*/
struct usb_class_driver hdjmidi_class = {
	.name =		"hdjbulk%d",
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,16) )
	.fops =		(struct file_operations*)&snd_hdjmidi_ops,
#else
	.fops =		&snd_hdjmidi_fops,
#endif
	.minor_base =	USB_HDJ_MINOR_BASE,
};

static int snd_hdj_create_rawmidi(struct snd_hdjmidi* umidi,
				      	int out_ports, 
					int in_ports)
{
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
	struct snd_rawmidi *rmidi;
#else
	snd_rawmidi_t *rmidi;
#endif
	int err;
	
	err = snd_rawmidi_new(umidi->chip->card, 
				"HDJMidi", 
			    atomic_inc_return(&umidi->chip->next_midi_device)-1,
				out_ports, 
				in_ports, 
				&rmidi);
	if (err < 0) {
		snd_printk(KERN_WARNING"%s() snd_rawmidi_new() failed, err:%d\n",__FUNCTION__,err);
		return err;
	}
	
	strcpy(rmidi->name, umidi->chip->card->shortname);

	rmidi->info_flags = (out_ports!=0 ? SNDRV_RAWMIDI_INFO_OUTPUT : 0) |
			    (in_ports!=0 ? SNDRV_RAWMIDI_INFO_INPUT : 0) |
			    (out_ports!=0 && in_ports!=0 ? SNDRV_RAWMIDI_INFO_DUPLEX : 0);
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18) )
	rmidi->ops = &snd_hdjmidi_ops;
#endif
	rmidi->private_data = umidi;
	rmidi->private_free = snd_hdj_rawmidi_free;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_hdjmidi_output_ops);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_hdjmidi_input_ops);

	umidi->rmidi = rmidi;

	return 0;
}

/* ALERT: channel_list_lock must be acquired while calling this */
static int assign_first_midi_channel_on_array (struct snd_hdjmidi* umidi,
	   				       struct midi_channel_elem *channel_map,
			        	       u16 channel_map_length)
{
	int i, rc = 0;
	for (i=0;i<channel_map_length;i++) {
		if (channel_map[i].channel == FREE_MIDI_CHANNEL ||
		   channel_map[i].umidi == umidi) {
			channel_map[i].channel = i;
			channel_map[i].umidi = umidi;
			atomic_set(&umidi->channel,i);
			break;
		}
	}
	if (i>=channel_map_length) {
		channel_printk(KERN_WARNING"%s() error in assigning channel to product:%u- ran out of free channels!\n",
			__FUNCTION__,
			umidi->chip->product_code);
		atomic_set(&umidi->channel,MIDI_INVALID_CHANNEL);
		/* we have run out of channels */
		rc = -1;
	} else {
		channel_printk(KERN_WARNING"%s() assigned channel:%d to product:%u\n",
			__FUNCTION__,
			i,
			umidi->chip->product_code);
	}
	return rc;
}

static void assign_midi_channel_helper(struct snd_hdjmidi* umidi,
				       struct midi_channel_elem* channel_map,
			               u16 channel_map_length,
			               u16 channel_reservation_disposition,
			               u16 channel_to_reserve)														 
{
	struct snd_hdjmidi* bumped_midi_device = NULL;
	channel_printk(KERN_INFO"%s() channel_reservation_disposition:%x, channel_to_reserve:%u\n",
		__FUNCTION__,
		channel_reservation_disposition,
		channel_to_reserve);
	
	spin_lock(&channel_list_lock);
	if ((channel_reservation_disposition==MIDI_CHANNEL_SPECIFIC)&&
	   (channel_to_reserve < channel_map_length)) {
		if (channel_list[channel_to_reserve].channel == FREE_MIDI_CHANNEL ||
		   channel_list[channel_to_reserve].umidi == umidi)
		{
			channel_list[channel_to_reserve].channel = channel_to_reserve;
			channel_list[channel_to_reserve].umidi = umidi;
			atomic_set(&umidi->channel,channel_to_reserve);
			spin_unlock(&channel_list_lock);
			channel_printk(KERN_INFO"%s() product:%u, assigned channel:%u as requested\n",
				__FUNCTION__,
				umidi->chip->product_code,
				channel_to_reserve);
			return;
		} else if ( umidi->chip->caps.non_volatile_channel==1 &&
			   channel_list[channel_to_reserve].umidi->chip->caps.non_volatile_channel==0) {
			/* we can bump the target, but we must save its extension */
			bumped_midi_device = channel_map[channel_to_reserve].umidi;

			/* now reserve the specified channel */
			channel_list[channel_to_reserve].channel = channel_to_reserve;
			channel_list[channel_to_reserve].umidi = umidi;
			atomic_set(&umidi->channel,channel_to_reserve);

			/* Now reserve another channel for the bumped one */
			assign_first_midi_channel_on_array(bumped_midi_device, channel_map, channel_map_length);
			spin_unlock(&channel_list_lock);

			channel_printk(KERN_INFO"%s() bumped product:%u from channel:%u\n",
				__FUNCTION__,
				bumped_midi_device->chip->product_code,
				channel_to_reserve);
			return;
		}
		/* We cannot bump the owned channel, so assign the first available one */
		assign_first_midi_channel_on_array(umidi, channel_map, channel_map_length);
	} else {
		/* caller asked for any channel (or an invalid one), so just assign first free one */
		assign_first_midi_channel_on_array(umidi, channel_map, channel_map_length);
	}
	spin_unlock(&channel_list_lock);
}

static void free_midi_channel(struct snd_hdjmidi* umidi);

static int read_midi_channel_device(struct snd_hdjmidi* umidi, u16* midi_channel)
{
	int rc = -ENXIO; /* means not implemented- best match I could find */
	if (umidi->chip->product_code==DJCONSOLERMX_PRODUCT_CODE) {
		/* check to see if a channel has been assigned */
		rc = send_vendor_request(umidi->chip->index,
					REQT_READ,
					DJ_GET_MIDI_CHANNEL,
					0,			
					0,		
					midi_channel,
					0);
	} else if (umidi->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		rc = __get_midi_channel(umidi->chip->index,midi_channel);
	}

	return rc;
}

static int write_midi_channel_device(struct snd_hdjmidi* umidi, u16 midi_channel)
{
	int rc = -ENXIO; /* means not implemented- best match I could find */
	if (umidi->chip->product_code==DJCONSOLERMX_PRODUCT_CODE) {
		rc = send_vendor_request(umidi->chip->index,
					REQT_WRITE,
					DJ_SET_MIDI_CHANNEL,
					midi_channel,			
					0,		
					NULL,
					0);
	} else if (umidi->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		rc = __set_midi_channel(umidi->chip->index,midi_channel);
	} else if (umidi->chip->product_code==DJCONSOLE_PRODUCT_CODE || 
				umidi->chip->product_code==DJCONSOLE2_PRODUCT_CODE ||
				umidi->chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
					rc = 0; /* nothing to do here */
	} else {
		printk(KERN_WARNING"%s invalid product:%d\n",__FUNCTION__,umidi->chip->product_code);
	}
	return rc;
}

#ifdef USE_STEEL_POLL_KTHREAD
static void request_steel_retry_assign_midi_channel(struct snd_hdjmidi* umidi, u8 request_fast_retry)
{
	struct snd_hdjmidi_steel_device_context* steel_context = NULL;
	if (umidi->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		steel_context = (struct snd_hdjmidi_steel_device_context*)umidi->device_specific_context;
		if (steel_context!=NULL) {
			atomic_inc(&steel_context->poll_assign_midi_channel);
			atomic_set(&steel_context->poll_period_jiffies, 
						msecs_to_jiffies(STEEL_POLL_PERIOD_MS));
			if (request_fast_retry!=0) {
				wake_up(&steel_context->steel_kthread_wait);
			}
		}
	}
}
#else
static inline void request_steel_retry_assign_midi_channel(struct snd_hdjmidi* umidi, u8 request_fast_retry) {}
#endif

int assign_midi_channel(struct snd_hdjmidi* umidi, u8 request_fast_retry)
{
	u16 channel_device=MIDI_INVALID_CHANNEL;
	int temp_channel_assigned;
	int rc=0;

	/* get the MIDI channel stored in the device, if possible */
	if (umidi->chip->caps.non_volatile_channel==1) {
		rc = read_midi_channel_device(umidi, &channel_device);
		if (rc==0) {
			channel_printk(KERN_INFO"%s() MIDI channel on Device:%u\n",
				__FUNCTION__,
				channel_device);
			if ((channel_device&0xff)>0xf) {
				/* device has no channel assigned to it...assign first free channel now. 
				 *  Of course the user can override this from the CPL
				 */
				assign_midi_channel_helper(umidi,
					channel_list,
					sizeof(channel_list)/sizeof(struct midi_channel_elem),
					MIDI_CHANNEL_ANY,
					0);
			} else {
				/* Check if it is free...and assign it if so.  Otherwise grab another one. */
				assign_midi_channel_helper(umidi,
					channel_list,
					sizeof(channel_list)/sizeof(struct midi_channel_elem),
					MIDI_CHANNEL_SPECIFIC,
					channel_device);
			}

			/* send whichever channel we could get */
			temp_channel_assigned = atomic_read(&umidi->channel);
			if (temp_channel_assigned!=MIDI_INVALID_CHANNEL && 
			   temp_channel_assigned!=channel_device) {
				channel_device = temp_channel_assigned;
				/* assign the channel if required */
				rc = write_midi_channel_device(umidi, channel_device);
				if (rc!=0) {
					/* we must free the channel from the map */
					free_midi_channel(umidi);
					/* mark the channel as invalid */
					atomic_set(&umidi->channel, MIDI_INVALID_CHANNEL);
					/* we failed to read the channel from the device- try again later if supported*/
					request_steel_retry_assign_midi_channel(umidi,request_fast_retry);
				}
			} else {
				if (atomic_read(&umidi->channel)==MIDI_INVALID_CHANNEL) {
					channel_printk(KERN_INFO"%s() invalid channel acquired\n",__FUNCTION__);
				} else {
					channel_printk(KERN_INFO"%s() successfully assigned channel from device to channel map:%u\n",
						__FUNCTION__,
						channel_device);
				}
			}
			channel_printk(KERN_INFO"%s() set chan:%u on device, status:%d\n",
				__FUNCTION__,
				channel_device,
				rc);
		} else {
			channel_printk(KERN_ERR"%s() read_midi_channel_device() failed rc:%d\n",__FUNCTION__,rc);
			/* we failed to read the channel from the device- try again later */
			request_steel_retry_assign_midi_channel(umidi, request_fast_retry);
		}
	} else  {
		/* This device does not support channel storing, so just grab any channel */
		assign_midi_channel_helper(umidi,
					   channel_list,
					   sizeof(channel_list)/sizeof(struct midi_channel_elem),
					   MIDI_CHANNEL_ANY,
					   0);
	} 
	return rc;
}

/* only called from bulk driver code */
int snd_hdjmidi_get_current_midi_channel(int chip_index, int* channel)
{
	struct snd_hdj_chip *chip=NULL;
	struct snd_hdjmidi* umidi=NULL;
	int rc = -EINVAL;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip==NULL) {
		printk(KERN_WARNING"%s() inc_chip_ref_count() returned NULL\n",__FUNCTION__);
		return rc;
	}
	
	umidi = midi_from_chip(chip);
	if (umidi==NULL) {
		printk(KERN_WARNING"%s() umidi NULL!\n",__FUNCTION__);
		rc = -EAGAIN;
	} else {
		*channel = atomic_read(&umidi->channel);
		rc = 0;
	}

	dec_chip_ref_count(chip_index);
	return rc;
}

/* disabled for now */
/*
static void request_steel_retry_assign_specific_midi_channel(struct snd_hdjmidi* umidi, 
								u16 midi_channel, 
								u8 request_fast_retry)
{
#ifdef USE_STEEL_POLL_KTHREAD
	struct snd_hdjmidi_steel_device_context* steel_context = NULL;
	if (umidi->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		steel_context = (struct snd_hdjmidi_steel_device_context*)umidi->device_specific_context;
		if (steel_context!=NULL) {
			atomic_set(&steel_context->poll_specific_channel_to_assign,midi_channel);
			atomic_inc(&steel_context->poll_set_specific_midi_channel);
			atomic_set(&steel_context->poll_period_jiffies, 
						msecs_to_jiffies(STEEL_POLL_PERIOD_MS));
			if (request_fast_retry!=0) {
				wake_up(&steel_context->steel_kthread_wait);
			}
		}
	}
#endif
}*/

/* only called from bulk driver code */
int snd_hdjmidi_set_midi_channel(int chip_index, 
				int channel_to_set, 
				int *channel_actually_set)
{
	return snd_hdjmidi_set_midi_channel_helper(chip_index,channel_to_set,channel_actually_set,1);
}

int snd_hdjmidi_set_midi_channel_helper(int chip_index, int channel_to_set, 
					int *channel_actually_set, 
					u8 fast_retry)
{
	struct snd_hdj_chip *chip=NULL;
	struct snd_hdjmidi* umidi=NULL;
	int rc = -EINVAL;
	u16 new_channel;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip==NULL) {
		printk(KERN_WARNING"%s() inc_chip_ref_count() returned NULL\n",__FUNCTION__);
		return rc;
	}
	
	umidi = midi_from_chip(chip);
	if (umidi==NULL) {
		printk(KERN_WARNING"%s() umidi NULL!\n",__FUNCTION__);
		rc = -EAGAIN;
		goto snd_hdjmidi_set_midi_channel_bail;
	} 

	if (channel_to_set >= MIDI_INVALID_CHANNEL) {
		printk(KERN_WARNING"%s() invalid channel requested:%d\n",
			__FUNCTION__,
			channel_to_set);
		return -EINVAL;
	}

	/* free the current MIDI channel */
	free_midi_channel(umidi);

	/* Check if requested channel is free...and assign it if so.  
	 *  Otherwise grab another one. */
	assign_midi_channel_helper(umidi,
			channel_list,
			sizeof(channel_list)/sizeof(struct midi_channel_elem),
			MIDI_CHANNEL_SPECIFIC,
			channel_to_set);
	/* extract actually reserved channel */
	new_channel = atomic_read(&umidi->channel);
	if (new_channel==MIDI_INVALID_CHANNEL) {
			printk(KERN_WARNING"%s() assign_midi_channel_helper() returned failure\n",
					__FUNCTION__);
			rc = -EINVAL;
	} else {
		rc = write_midi_channel_device(umidi, new_channel);
		if (rc!=0) {
			printk(KERN_WARNING"%s() write_midi_channel_device() failed, channel_to_set:%d, new_channel%d rc:%d\n",
					__FUNCTION__, channel_to_set, new_channel, rc);
			
			/* since we failed, we must free the MIDI channel */
			free_midi_channel(umidi);

			/* As this might confuse the CPL for now disable this */
			/* request_steel_retry_assign_specific_midi_channel(umidi,new_channel, fast_retry); */
		} else {
			*channel_actually_set = new_channel;
		}
	}
	printk(KERN_INFO"%s() requested MIDI channel:%d, reserved:%d, rc:%x\n",
		__FUNCTION__,
		channel_to_set,new_channel,rc);
snd_hdjmidi_set_midi_channel_bail:
	
	dec_chip_ref_count(chip_index);
	return rc;
}

/* only called from bulk driver code */
int snd_hdjmidi_get_current_midi_mode(int chip_index, int* midi_mode)
{
	struct snd_hdj_chip *chip=NULL;
	struct snd_hdjmidi* umidi=NULL;
	int rc = -EINVAL;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip==NULL) {
		printk(KERN_WARNING"%s() inc_chip_ref_count() returned NULL\n",__FUNCTION__);
		return rc;
	}
	
	umidi = midi_from_chip(chip);
	if (umidi==NULL) {
		printk(KERN_WARNING"%s() umidi NULL!\n",__FUNCTION__);
		rc = -EAGAIN;
	} else {
		*midi_mode = atomic_read(&umidi->midi_mode);
		rc = 0;
	}

	dec_chip_ref_count(chip_index);
	return rc;
}

/* only called from bulk driver code */
int snd_hdjmidi_set_midi_mode(int chip_index, int midi_mode)
{
	struct snd_hdj_chip *chip=NULL;
	struct snd_hdjmidi* umidi=NULL;
	int rc = -EINVAL;
	
	chip = inc_chip_ref_count(chip_index);
	if (chip==NULL) {
		printk(KERN_WARNING"%s() inc_chip_ref_count() returned NULL\n",__FUNCTION__);
		return rc;
	}
	if (chip->product_code!=DJCONSOLE_PRODUCT_CODE) {
		printk(KERN_WARNING"%s() unsupported product:%u\n",
			__FUNCTION__,
			chip->product_code);
		goto snd_hdjmidi_set_midi_mode_bail;
	}
	
	umidi = midi_from_chip(chip);
	if (umidi==NULL) {
		printk(KERN_WARNING"%s() umidi NULL!\n",__FUNCTION__);
		rc = -EAGAIN;
	} else {
		rc = snd_djc_start_midi(umidi, midi_mode, 1);
	}
	
	printk(KERN_INFO"%s() set midi mode:%d, rc:%x\n",
					__FUNCTION__,
					midi_mode,rc);

snd_hdjmidi_set_midi_mode_bail:
	
	dec_chip_ref_count(chip_index);
	return rc;
}

#ifdef USE_STEEL_POLL_KTHREAD
static void request_steel_retry_get_fw_version(struct snd_hdjmidi* umidi, u8 request_fast_retry)
{
	struct snd_hdjmidi_steel_device_context* steel_context = NULL;
	if (umidi->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		steel_context = (struct snd_hdjmidi_steel_device_context*)umidi->device_specific_context;
		if (steel_context!=NULL) {
			atomic_inc(&steel_context->poll_acquire_fw_version);
			atomic_set(&steel_context->poll_period_jiffies, 
						msecs_to_jiffies(STEEL_POLL_PERIOD_MS));
			if (request_fast_retry!=0) {
				wake_up(&steel_context->steel_kthread_wait);
			}
		}
	}
}
#else
static inline void request_steel_retry_get_fw_version(struct snd_hdjmidi* umidi, u8 request_fast_retry) {}
#endif

#ifdef USE_STEEL_POLL_KTHREAD
int steel_kthread_entry(void *arg)
{
	struct snd_hdjmidi* umidi = NULL;
	struct snd_hdjmidi_steel_device_context *steel_context=NULL;
	int timeout, midi_channel_to_set, actual_midi_channel_set;
	/*snd_printk(KERN_INFO"%s()\n",__FUNCTION__);*/
	
	steel_context = (struct snd_hdjmidi_steel_device_context *)arg;
	umidi = steel_context->umidi;
	
	/* let code which set up us know that we are running, so it can continue */
	complete(&steel_context->steel_kthread_started);
	
	while (!kthread_should_stop()) {
		timeout=atomic_read(&steel_context->poll_period_jiffies);
		if (timeout!=0) {
			wait_event_interruptible_timeout(steel_context->steel_kthread_wait,
												kthread_should_stop(),
												timeout);
		} else {
			wait_event_interruptible(steel_context->steel_kthread_wait,
						kthread_should_stop()||
						atomic_read(&steel_context->poll_period_jiffies)!=0);
		}
		
		if (kthread_should_stop()) {
			/*snd_printk(KERN_INFO"%s() asked to bail\n",__FUNCTION__);*/
			break;
		}

		/* check to see if we are requested to assign any midi channel to the device */
		if (atomic_read(&steel_context->poll_assign_midi_channel)!=0) {
			/* We have been instructed to assign the MIDI channel. */
			if (assign_midi_channel(umidi,0)==0) {
				atomic_set(&steel_context->poll_assign_midi_channel,0);
			}
		}
		
		/* check to see if we are requested to assign a specific midi channel to the device */
		if (atomic_read(&steel_context->poll_set_specific_midi_channel)!=0) {
			midi_channel_to_set = atomic_read(&steel_context->poll_specific_channel_to_assign);
			actual_midi_channel_set = MIDI_INVALID_CHANNEL;
			/* We have been instructed to assign a specific MIDI channel. */
			if (snd_hdjmidi_set_midi_channel_helper(umidi->chip->index, 
								midi_channel_to_set, 
								&actual_midi_channel_set, 
								0)==0) {
				atomic_set(&steel_context->poll_set_specific_midi_channel,0);
			}
		}

		/* check to see if we are requested to acquire the firmware version */
		if (atomic_read(&steel_context->poll_acquire_fw_version)!=0) {
			/* We have been instructed to assign the MIDI channel. */
			if (get_firmware_version(umidi,&umidi->firmware_version,1)==0) {
				atomic_set(&steel_context->poll_acquire_fw_version,0);
			} else {
				request_steel_retry_get_fw_version(umidi,0);	
			}
		}

		/* Check to see if we should sleep indefinitely or continue in the curent state, which
		 *  could mean wake up at some interval for a retry, or sleep indefinitely.  We request to
		 *  sleep indefinitely if there is no more work to do 
		 */
		if (atomic_read(&steel_context->poll_assign_midi_channel)==0 &&
			atomic_read(&steel_context->poll_set_specific_midi_channel)==0 &&
			atomic_read(&steel_context->poll_acquire_fw_version)==0) {
			
			/* this will stop polling */
			atomic_set(&steel_context->poll_period_jiffies,0);
		}
	}
	return 0;
}
#endif

#ifdef USE_STEEL_POLL_KTHREAD
static int create_steel_kthread(struct snd_hdjmidi_steel_device_context *steel_context)
{
	/* Create the polling kthread for communication with the bulk continuous reader code.  This
	 *  is due to the fact that we rely on the bulk driver (code managing interface 0) to communicate
	 *  with the device for getting the firmware version and getting/setting MIDI channels.  Since
	 *  we rely on the bulk device code, and since it can fail, or perhaps be loaded out of order,
	 *  we need to maintain a polling thread to acquire the MIDI channel from the bulk driver (which
	 *  is really the code which manages interface 0).  The device would reject any MIDI messages 
	 *  (control changes) originating from the host which do not have the proper MIDI channel encoded
	 *  within.  So if the bulk driver fails a request we must poll it.
	 */
	if (steel_context->steel_kthread==NULL) {
		init_completion(&steel_context->steel_kthread_started);
		atomic_set(&steel_context->poll_period_jiffies, 0);
		init_waitqueue_head(&steel_context->steel_kthread_wait);
		steel_context->steel_kthread = 
			kthread_create(steel_kthread_entry,steel_context,"steel_kthread%d",
				atomic_read(&steel_context->umidi->chip->next_bulk_device)-1);
		if (steel_context->steel_kthread==NULL) {
			/* caller will cleanup, we have been added to umidi's list */
			snd_printk(KERN_WARNING"%s() kthread_create() failed\n",__FUNCTION__);
			return -ENOMEM;
		} else {
			wake_up_process(steel_context->steel_kthread);
			/* after this returns we can call kthread_stop without fear */
			wait_for_completion(&steel_context->steel_kthread_started);
		}
	} else {
		printk(KERN_INFO"%s() polling kthread already created\n",__FUNCTION__);	
	}
	return 0;
}
#endif

#ifdef USE_STEEL_POLL_KTHREAD
static int create_and_init_steel_context(struct snd_hdjmidi* umidi)
{
	struct snd_hdjmidi_steel_device_context *steel_context=NULL;
	if (umidi->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		umidi->device_specific_context = kmalloc(sizeof(struct snd_hdjmidi_steel_device_context),GFP_KERNEL);
		steel_context = (struct snd_hdjmidi_steel_device_context *)umidi->device_specific_context;
		if (umidi->device_specific_context==NULL) {
			printk(KERN_WARNING"%s() kmalloc failed\n",__FUNCTION__);
			return -ENOMEM;
		}
		memset(umidi->device_specific_context,0,sizeof(struct snd_hdjmidi_steel_device_context));
		steel_context->umidi = umidi;
		atomic_set(&steel_context->poll_assign_midi_channel,0);
		atomic_set(&steel_context->poll_set_specific_midi_channel,0);
		atomic_set(&steel_context->poll_specific_channel_to_assign,MIDI_INVALID_CHANNEL);
		atomic_set(&steel_context->poll_acquire_fw_version,0);
		return create_steel_kthread(steel_context);
	}
	return 0;
}
#endif

/*
 * Creates and registers everything needed for a MIDI streaming interface.
 */
int snd_hdj_create_midi_interface(struct snd_hdj_chip* chip,
 			  	  struct usb_interface* target_iface,
 			  	  struct usb_interface* original_iface)
{
	struct snd_hdjmidi* umidi;
	struct snd_hdjmidi_endpoint_info endpoints[MIDI_MAX_ENDPOINTS];
	int i, err, rc;

	/* We do this here because module_init can be called after probe() in some cases */
	if (test_and_set_bit(0,&channel_list_initialized)==0) {
		snd_init_midi_channel_list();
	}

	umidi = kmalloc(sizeof(*umidi), GFP_KERNEL);
	if (!umidi) {
		snd_printk(KERN_WARNING"%s() failed to allocate MIDI interface memory\n",__FUNCTION__);
		return -ENOMEM;
	}
	memset(umidi,0,sizeof(*umidi));
	umidi->chip = chip;
	umidi->iface = target_iface;
	umidi->iface_original = original_iface;

	atomic_set(&umidi->channel, MIDI_INVALID_CHANNEL); 
	atomic_set(&umidi->midi_mode,MIDI_MODE_CONTROLLER);
	if (umidi->chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		if (is_mp3_weltrend(umidi->chip->usb_id)) {
			umidi->usb_protocol_ops = &snd_hdjmp3_standard_ops;
		} else {
			umidi->usb_protocol_ops = &snd_hdjmp3_non_weltrend_standard_ops;
		}
	} else {
		umidi->usb_protocol_ops = &snd_hdjmidi_standard_ops;
	}
	
	init_timer(&umidi->error_timer);

	umidi->error_timer.function = snd_hdjmidi_error_timer;
	umidi->error_timer.data = (unsigned long)umidi;

	/* detect the endpoint(s) to use */
	memset(endpoints, 0, sizeof(endpoints));
	err = snd_hdjmidi_get_ms_info(umidi, endpoints);
	if (err < 0) {
		snd_printk(KERN_WARNING"%s() snd_hdjmidi_get_ms_info() failed \n",__FUNCTION__);
		kfree(umidi);
		return err;
	}
	
	/* create rawmidi device */
	err = snd_hdj_create_rawmidi(umidi, 
							umidi->chip->caps.num_out_ports, 
							umidi->chip->caps.num_in_ports);
	if (err < 0) {
		kfree(umidi);
		return err;
	}
	
	/* increment count- will deref in snd_hdj_free, called when chip is destroyed, which
	 *  is called when the card is destroyed */
	reference_usb_intf_and_devices(umidi->iface);
	
	if (umidi->iface_original!=NULL) {
		reference_usb_intf_and_devices(umidi->iface_original);
	}
	
	/* since we add the midi context to the chip midi list, we are guaranteed that
	 *  cleanup for midi will ensue when the chip is destroyed */
	list_add(&umidi->list, &umidi->chip->midi_list);
	
	/* create endpoint/port structures */
	err = snd_hdjmidi_create_endpoints(umidi, endpoints);
	if (err < 0) {
		printk(KERN_WARNING"%s snd_hdjmidi_create_endpoints() failed, status:%d\n",
			__FUNCTION__,err);
		return err;
	}
	
	/* Since the MIDI driver takes charge for the mp3, we must do this- bulk will
	 *  disable the mouse for other products. We call the mutiple device function so
	 *  it will send out device property notifications to usermode clients */
	if (umidi->chip->product_code==DJCONTROLLER_PRODUCT_CODE) {
		set_mouse_state(umidi->chip,0);
	}

#ifdef USE_STEEL_POLL_KTHREAD
	if (umidi->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		rc = create_and_init_steel_context(umidi);
		if (rc!=0) {
			/* caller will cleanup */
			printk(KERN_WARNING"%s() create_and_init_steel_context() failed, rc:%x\n",
				__FUNCTION__,
				rc);
			return rc;
		}
	}
#endif

	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		rc = snd_hdjmidi_input_start_ep(umidi->endpoints[i].in);
		/* will not propagate the error up in the hope of later recovery */
		if (rc!=0) {
			printk(KERN_WARNING"%s() enp_num:%d, submit urb failed, rc:%x\n",
					__FUNCTION__,
					i,rc);
		}
	}

	/* for DJC- set the MIDI mode in the hardware */
	if (umidi->chip->product_code==DJCONSOLE_PRODUCT_CODE) {
		if (set_djconsole_device_config(umidi->chip->index,
					(DJC_AUDIOCFG_MIDI_IN_BUTTONS<<16)|DJC_AUDIOCFG_MIDI_IN_BUTTONS,0)!=0) {
			printk(KERN_WARNING"%s() set_djconsole_device_config failed\n",__FUNCTION__);
			/* stopgap... */
			snd_djc_start_midi(umidi,MIDI_MODE_CONTROLLER,0);
		}
	}
	
	/* acquire the device firmware version */
	if (get_firmware_version(umidi->chip,&umidi->fw_version,1)!=0) {
			/* currently inlined out- was for development, but can be renabled */
			request_steel_retry_get_fw_version(umidi, 1);
	}
	
	/* clear all the LEDs */
	clear_leds(umidi->chip);

	/* acquire our midi channel */
	assign_midi_channel(umidi,1);
	
	/* now we are ready to receive requests from usermode- register our control device- but
	 *  do so only for control MP3, because the other products have the bulk interface
	 *  already */
	if (has_bulk_interface(chip)==0 && umidi->iface_original!=NULL) {
		/* we can register the device now, as it is ready-we are now ready to receive user requests */
		err = usb_register_dev(umidi->iface_original, &hdjmidi_class);
		if (err) {
			/* something prevented us from registering this driver */
			printk(KERN_ERR"%s() usb_register_dev failed.\n",__FUNCTION__);
		} else {
			umidi->registered_usb_dev = 1;
		}
	}

	dump_product_name_to_console(umidi->chip,0,1);

	return 0;
}

static void free_midi_channel_helper(struct snd_hdjmidi* umidi,
				     struct midi_channel_elem* channel_map,
			             u16 channel_map_length)
{
	int channel;
	spin_lock(&channel_list_lock);
	channel = atomic_read(&umidi->channel);
	if ( channel!=MIDI_INVALID_CHANNEL &&
	    channel<channel_map_length &&
	    channel_map[channel].umidi==umidi) {
		channel_map[channel].channel = FREE_MIDI_CHANNEL;
		channel_map[channel].umidi = NULL;
		atomic_set(&umidi->channel,MIDI_INVALID_CHANNEL);
	}
	spin_unlock(&channel_list_lock);
}

static void free_midi_channel(struct snd_hdjmidi* umidi)
{
	free_midi_channel_helper(umidi,
	 		 	 channel_list,
				 sizeof(channel_list)/sizeof(struct midi_channel_elem));
}

static void kill_all_urbs(struct snd_hdjmidi* umidi)
{
	int i;
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_hdjmidi_endpoint* ep = &umidi->endpoints[i];
		snd_hdjmidi_output_kill_urbs(ep->out);
		if (umidi->usb_protocol_ops->finish_out_endpoint) {
			umidi->usb_protocol_ops->finish_out_endpoint(ep->out);
		}
		snd_hdjmidi_input_kill_urbs(ep->in);
	}
}

#ifdef USE_STEEL_POLL_KTHREAD
static void kill_steel_kthread(struct snd_hdjmidi_steel_device_context *steel_context)
{
	if (steel_context->steel_kthread!=NULL) {
		kthread_stop(steel_context->steel_kthread);
		steel_context->steel_kthread = NULL;
	}
}

static void cleanup_steel_context(struct snd_hdjmidi* umidi)
{
	struct snd_hdjmidi_steel_device_context *steel_context=NULL;
	if (umidi->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		steel_context = (struct snd_hdjmidi_steel_device_context *)umidi->device_specific_context;
		if (steel_context==NULL) {
			printk(KERN_WARNING"%s() steel context was NULL, bailing\n",__FUNCTION__);		
			return;
		}
		kill_steel_kthread(steel_context);
		kfree(umidi->device_specific_context);
		umidi->device_specific_context = NULL;
	}
}
#endif

/*
 * Unlinks all URBs (must be done before the usb_device is deleted).
 */
void snd_hdjmidi_disconnect(struct list_head* p)
{
	struct snd_hdjmidi* umidi;
	int i;
	umidi = list_entry(p, struct snd_hdjmidi, list);
	del_timer_sync(&umidi->error_timer);

	/* free the midi channel */
	free_midi_channel(umidi);

	/* kill all of our urbs and tasklets */
	kill_all_urbs(umidi);

#ifdef USE_STEEL_POLL_KTHREAD
	if (umidi->chip->product_code==DJCONTROLSTEEL_PRODUCT_CODE) {
		cleanup_steel_context(umidi);
	}
#endif

	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_hdjmidi_endpoint* ep = &umidi->endpoints[i];
		snd_hdjmidi_output_kill_tasklet(ep->out);
	}
}

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18) )
#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
int snd_hdjmidi_pre_reset(struct list_head* p)
#else
void snd_hdjmidi_pre_reset(struct list_head* p)
#endif
{
	/* stop all I/O, and prevent more from starting */
	snd_hdjmidi_suspend(p);

#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
	return 0;
#endif
}
#endif

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18) )
#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
int snd_hdjmidi_post_reset(struct list_head* p)
#else
void snd_hdjmidi_post_reset(struct list_head* p)
#endif
{
	/* allow I/O to resume */
	snd_hdjmidi_resume(p);

#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
	return 0;
#endif
}
#endif

/* to be called from module_init function */
void midi_init(void)
{
	/* nothing to do */
}

/* to be called from module_exit function */
void midi_cleanup(void)
{
	/* nothing to do */
}

#ifdef CONFIG_PM
/* suspend support */
void snd_hdjmidi_suspend(struct list_head* p)
{
	struct snd_hdjmidi* umidi;
	int i;
	
	printk(KERN_INFO"%s()\n",__FUNCTION__);
	umidi = list_entry(p, struct snd_hdjmidi, list);

	/* kill the error timer */
	del_timer_sync(&umidi->error_timer);

	/* take care of the MIDI output tasklet */
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_hdjmidi_endpoint* ep = &umidi->endpoints[i];
		snd_hdjmidi_output_kill_tasklet(ep->out);
	}

	/* kill all of our urbs, now that the tasklet and timer are suppressed */
	kill_all_urbs(umidi);
}

void snd_hdjmidi_resume(struct list_head* p)
{
	struct snd_hdjmidi* umidi;
	int i;
	umidi = list_entry(p, struct snd_hdjmidi, list);
	
	printk(KERN_INFO"%s()\n",__FUNCTION__);

	/* reinitialize the output tasklet(s) */
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_hdjmidi_endpoint* ep = &umidi->endpoints[i];
		snd_hdjmidi_output_initialize_tasklet(ep->out);
	}

	/* start the polling again */
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_hdjmidi_endpoint* ep = &umidi->endpoints[i];
		snd_hdjmidi_input_start_ep(ep->in);
	}
}
#endif

