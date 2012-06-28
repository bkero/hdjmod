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

#if !defined(_MIDI_H_)
#define _MIDI_H_

/* Maximum number of endpoints per interface */
#define MIDI_MAX_ENDPOINTS 2

/* for snd_printk to display file and line number- commented out it reverts to printk
#define CONFIG_SND_VERBOSE_PRINTK
*/

/* comment below in for channel allocation/free printk */
//#define CHANNEL_PRINTK
#ifdef CHANNEL_PRINTK
	#define channel_printk(message... ) \
		do {snd_printk(message); } while(0)
#else
	#define channel_printk(message... ) 
#endif

/*
 Taken from usbaudio.h:
 retrieve usb_interface descriptor from the host interface
 (conditional for compatibility with the older API)
*/
#ifndef get_iface_desc
#define get_iface_desc(iface)   (&(iface)->desc)
#define get_endpoint(alt,ep)    (&(alt)->endpoint[ep].desc)
#define get_ep_desc(ep)         (&(ep)->desc)
#define get_cfg_desc(cfg)       (&(cfg)->desc)
#endif

/* Taken from usbaudio.h */
#ifndef snd_usb_get_speed
#define snd_usb_get_speed(dev) ((dev)->speed)
#endif

/*
 Taken from usbaudio.h
  handling of USB vendor/product ID pairs as 32-bit numbers 
*/
#define USB_ID(vendor, product) (((vendor) << 16) | (product))
#define USB_ID_VENDOR(id) ((id) >> 16)
#define USB_ID_PRODUCT(id) ((u16)(id))

/* For control requests, including hid reports */
#define REQT_READ       				0xc0
#define REQT_WRITE      				0x40
#define USB_REQ_SET_REPORT 				0x09
#define USB_HID_INPUT_REPORT				1
#define USB_HID_OUTPUT_REPORT				2
#define USB_HID_FEATURE_REPORT				3

/* DJ MP3 HID report information */
#define DJ_MP3_HID_REPORT_ID				1
#define DJ_MP3_HID_OUTPUT_REPORT_LEN		4
#define DJ_MP3_HID_INPUT_REPORT_LEN			20

/* control numbers for MP3- for MIDI to HID conversion and vice versa */
#define DJ_MP3_NUM_INPUT_CONTROLS			43
#define DJ_MP3_NUM_OUTPUT_CONTROLS			20

/*
 * how long to wait after some USB errors, so that khubd can disconnect() us
 * without too many spurious errors
 */
#define ERROR_DELAY_JIFFIES (HZ / 10)

/* for reserving MIDI channels */
#define FREE_MIDI_CHANNEL			0xffff
#define MIDI_CHANNEL_ANY			0xf001
#define MIDI_CHANNEL_SPECIFIC			0xf002

/* meaningless for all devices except DJC */
#define MIDI_MODE_CONTROLLER			0
#define MIDI_MODE_PHYSICAL_PORT			1			

struct snd_hdjmidi;

/* MIDI reserve list element- as many elements as MIDI channels (16) */
struct midi_channel_elem
{
	/* the actual channel */
	u16 channel;

	/* MIDI device which has reserved this channel */
	struct snd_hdjmidi* umidi;
};

#define NUM_MIDI_CHANNELS			16
#define STANDARD_MAP_SIZE			(NUM_MIDI_CHANNELS*sizeof(struct midi_channel_elem))
#define MAX_MIDI_CHANNEL			(NUM_MIDI_CHANNELS-1)
#define MIDI_INVALID_CHANNEL			NUM_MIDI_CHANNELS

/* used for temporarily storing information to create pipes and URBs */
struct snd_hdjmidi_endpoint_info {
	int8_t   out_ep;	/* ep number, 0 autodetect */
	uint8_t  out_interval;	/* interval for interrupt endpoints */
	int8_t   in_ep;	
	uint8_t  in_interval;
	uint16_t out_cables;	/* bitmask */
	uint16_t in_cables;	/* bitmask */
};

/* holds details for hid controls, input or output */
struct controller_control_details {
	u16 control_id;
	const char * name;
	unsigned int type;
	unsigned int byte_number;
	unsigned int bit_number;
	u32 midi_message_pressed; /* len is 3 */
	u32 midi_message_released; /* len is 3 */
	atomic_t value;
};

struct controller_output_hid {
	/* ctl_req and ctl_req_dma fields below only used by the MP3 for HID reports, which are
	 * control requests.  This is used for converting MIDI output received from rawmidi
	 * to HID set report calls which the MP3 device understands
	 */
	struct usb_ctrlrequest *ctl_req;
	dma_addr_t ctl_req_dma;

	/* input control details */
	struct controller_control_details *control_details;
	u32		num_controls;

	/* is mp3 based on weltrend chip or not */
	u8 is_weltrend;

	/* guards current_hid_output_report_data, because we may need to access it at multiple points */
	spinlock_t hid_buffer_lock;
	
	/* we cache the HID output report state */
	u8 *current_hid_report_data;
	u32 current_hid_report_data_len;
	
	/* used for misc purposes- sending HID control data */
	/*  not reserved for use by anyone, but currently used for sending LED/control
	 *  requests via HID reports- for example, mouse enable/disable */
	struct urb* output_control_ctl_urb; 
	struct usb_ctrlrequest *output_control_ctl_req;
	dma_addr_t output_control_ctl_dma;
	struct semaphore output_control_ctl_mutex; /* for serializing */
	struct completion output_control_ctl_completion;
	int output_control_ctl_pipe;
	
	/* polling kthread for use with some devices only */
	struct task_struct 	*mp3w_kthread;
	wait_queue_head_t 	mp3w_kthread_wait;
	struct completion	mp3w_kthread_started;
	
	/* reserved for polling kthread, if present */
	struct urb* urb_kt;
	struct usb_ctrlrequest *ctl_req_kt;
	dma_addr_t ctl_req_dma_kt;
	struct completion ctl_req_completion_kt;
	atomic_t poll_period_jiffies;
};

/* This is recommended, because the mp3 is actually HID, with a 8ms period- so
 *  therefore it is highly recommended not to exceed this.  Our other devices are
 *  MIDI, and NAK us if we exceed the MIDI rate */
#define THROTTLE_MP3_RENDER
#define THROTTLE_MP3_RENDER_RATE			8 /* ms */

struct snd_hdjmidi_out_endpoint {
	struct snd_hdjmidi* umidi;
	struct urb* urb; /* this is used for MIDI output*/
	int urb_active; /* relates to MIDI output URB */
	int max_transfer;		/* size of urb buffer */
	struct tasklet_struct tasklet;
	
	/* Used for turning off LEDs when MIDI is being closed.  Unforunately, that
	 *  code is not atomic, and therefore I cannot wait on MIDI send operations
	 *  to complete, so it is easiest to use another URB and USB buffer. */
	struct urb* urb_led;
	struct usb_ctrlrequest *ctrl_req_led;
	dma_addr_t ctrl_req_led_dma;

	spinlock_t buffer_lock;
	
	int endpoint_number;
	
#ifdef THROTTLE_MP3_RENDER
	struct timer_list render_delay_timer;
	unsigned long last_send_time;
#endif

	struct hdjmidi_out_port {
		struct snd_hdjmidi_out_endpoint* ep;
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
		struct snd_rawmidi_substream *substream;
#else
		snd_rawmidi_substream_t* substream;
#endif
		int active;
		uint8_t state;
#define STATE_UNKNOWN	0
#define STATE_1PARAM	1
#define STATE_2PARAM_1	2
#define STATE_2PARAM_2	3
#define STATE_SYSEX_0	4
#define STATE_SYSEX_1	5
#define STATE_SYSEX_2	6
		uint8_t data[2];
	} ports[0x10];
	int current_port;

	/* will only be used for MP3 and other HID only controlers, otherwise is NULL */
	struct controller_output_hid *controller_state;
};

/* DJ Control types */
#define TYPE_UNKNOWN		0
#define TYPE_LED			1
#define TYPE_BUTTON			2
#define TYPE_LINEAR			3
#define TYPE_INCREMENTAL	4
#define TYPE_SETTING		5

struct controller_input_hid {
	/*
	 * Holds last input HID report information.  If the current report and the last HID report are
	 *  not identical, then a control has changed.
	 */
	u8 *last_hid_report_data;

	/* snapshot of latest hid input control data.  If clients access it then do so under
	 *  buffer_lock spin_lock.  It is also used for buffering by the (non mass produced)
	 *  PSOC which is low speed, which sends an incomplete control snapshot during each
	 *  USB input completion */ 
	u8 *current_hid_report_data;
	u32 current_hid_report_data_len;
	
	/* Based on callback, may guard buffering and current hid report data buffer */
	spinlock_t buffer_lock;

	/* have we buffered at least one full hid control buffer- in order to detect control
	 *  changes we compare current buffer with the last one */
	atomic_t buffered_hid_data;

	/* for the MP3 non-weltrend, a low speed device (not in mass production) */
	int last_hid_byte_num;

	/* for mp3: whether based on weltrend chip or not (all mass produced mp3s are
	 *  based on the weltrend, we have a few PSOC samples here) */
	u8 is_weltrend;

	/* input control details */
	struct controller_control_details *control_details;
	u32 num_controls;
};

struct snd_hdjmidi_in_endpoint {
	struct snd_hdjmidi* umidi;
	struct urb* urb;

	struct hdjmidi_in_port {
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
		struct snd_rawmidi_substream *substream;
#else
		snd_rawmidi_substream_t *substream;
#endif
		/* We need to parse input in order to place our MIDI channel in the status byte where 
		 *  applicable.  This is required mostly for the DJC, which has physical MIDI ports
	 	 *  and can send fragmented MIDI messages.
		 */
		uint8_t state;
#define STATE_UNKNOWN	0
#define STATE_1PARAM	1
#define STATE_2PARAM_1	2
#define STATE_2PARAM_2	3
#define STATE_SYSEX_0	4
#define STATE_SYSEX_1	5
#define STATE_SYSEX_2	6
		uint8_t data[3];
		uint8_t midi_message_ready;
		uint8_t midi_message_len;
	} ports[0x10];

	u8 seen_f5;
	u8 error_resubmit;
	int current_port;
	int endpoint_number;

	/* will only be used for MP3 and other HID only controlers, otherwise is NULL */
	struct controller_input_hid *controller_state;
};

struct usb_protocol_ops {
	void (*input)(struct snd_hdjmidi_in_endpoint*, uint8_t*, int);
	void (*output)(struct snd_hdjmidi_out_endpoint*);
	void (*output_packet)(struct urb*, uint8_t, uint8_t, uint8_t, uint8_t);
	void (*init_out_endpoint)(struct snd_hdjmidi_out_endpoint*);
	void (*finish_out_endpoint)(struct snd_hdjmidi_out_endpoint*);
};

/*
 * This was originally used during development in order to retry certain operations which
 * failed on the DJ Control Steel.  It is not normally useful anymore.  A kthread would be
 * created, which would receive directives to retry certain failed tasks.  This was useful
 * when there were device communication issues, but since these have been ironed out, there
 * is no normal need to create this polling thread.  Simply uncomment define below to
 * reactivate.
 */
/*#define USE_STEEL_POLL_KTHREAD*/

#ifdef USE_STEEL_POLL_KTHREAD
#define STEEL_POLL_PERIOD_MS		1000
struct snd_hdjmidi;
struct snd_hdjmidi_steel_device_context {
	/* This is a polling kthread for communication with the bulk continuous reader code.  This
	 *  is due to the fact that we rely on the bulk driver (code managing interface 0) to communicate
	 *  with the device for getting the firmware version and getting/setting MIDI channels.  Since
	 *  we rely on the bulk device code, and since it can fail, or perhaps be loaded out of order,
	 *  we need to maintain a polling thread to acquire the MIDI channel from the bulk driver (which
	 *  is really the code which manages interface 0).  The device would reject any MIDI messages 
	 *  (control changes) originating from the host which do not have the proper MIDI channel encoded
	 *  within.  So if the bulk driver fails a request we must poll it.
	 */
	struct task_struct 	*steel_kthread;
	wait_queue_head_t 	steel_kthread_wait;
	struct completion 	steel_kthread_started;
	atomic_t 			poll_period_jiffies;
	struct snd_hdjmidi *umidi;

	/* these contain the directives to the polling kthread */
	atomic_t poll_assign_midi_channel;
	atomic_t poll_set_specific_midi_channel;
	atomic_t poll_acquire_fw_version;

	/* midi channel to assign is poller_set_specific_midi_channel>0 */
	atomic_t poll_specific_channel_to_assign;
};
#endif

struct snd_hdjmidi {
	struct list_head list;
	struct snd_hdj_chip *chip;
	struct usb_interface *iface;
	struct usb_interface *iface_original;
	u16 fw_version;
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
	struct snd_rawmidi *rmidi;
#else
	snd_rawmidi_t *rmidi;
#endif

	u8 registered_usb_dev;

	struct usb_protocol_ops* usb_protocol_ops;
	struct timer_list error_timer;

	/* channel over which we report on input, and send on output- depending on the device in some cases */
	atomic_t channel;

	/* Applies for the DJC only, where there are physical ports as well 
	 * Valid values: 
	 *   -MIDI_MODE_CONTROLLER: controller board, not physical ports.
	 *   -MIDI_MODE_PHYSICAL_PORT: physical ports.
	 */
	atomic_t midi_mode;

	struct snd_hdjmidi_endpoint {
		struct snd_hdjmidi_out_endpoint *out;
		struct snd_hdjmidi_in_endpoint *in;
	} endpoints[MIDI_MAX_ENDPOINTS];
	unsigned long input_triggered;

	/* may be NULL, may hold device specific context state- based on chip->product_code */
	void * device_specific_context;
};

/*
 * Creates and registers everything needed for a MIDI streaming interface.
 */
int snd_hdj_create_midi_interface(struct snd_hdj_chip* chip,
 			  	  struct usb_interface* target_iface,
 			  	  struct usb_interface* original_iface);

void snd_hdjmidi_disconnect(struct list_head* p);

/*
 * Frees everything.
 */
void snd_hdj_free(struct snd_hdjmidi* umidi);

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18) )
#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22) )
int snd_hdjmidi_pre_reset(struct list_head* p);
int snd_hdjmidi_post_reset(struct list_head* p);
#else
void snd_hdjmidi_pre_reset(struct list_head* p);
void snd_hdjmidi_post_reset(struct list_head* p);
#endif
#endif

#ifdef CONFIG_PM
/* suspend support */
void snd_hdjmidi_suspend(struct list_head* p);
void snd_hdjmidi_resume(struct list_head* p);
#endif

/*
 * Error handling for URB completion functions.
 */
int snd_hdjmidi_urb_error(int status);

/*
 * Submits the URB, with error handling.
 */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14) )
int snd_hdjmidi_submit_urb(struct snd_hdjmidi* umidi, struct urb* urb, gfp_t flags);
#else
int snd_hdjmidi_submit_urb(struct snd_hdjmidi* umidi, struct urb* urb, int flags);
#endif

int snd_hdjmidi_get_current_midi_channel(int chip_index, int* channel);

/* only called from bulk driver code */
int snd_hdjmidi_set_midi_channel(int chip_index, 
					int channel_to_set, 
					int *channel_actually_set);
int snd_hdjmidi_set_midi_channel_helper(int chip_index, int channel_to_set, 
					int *channel_actually_set, 
					u8 fast_retry);
/* only called from bulk driver code */
int snd_hdjmidi_get_current_midi_mode(int chip_index, int* midi_mode);
/* only called from bulk driver code */
int snd_hdjmidi_set_midi_mode(int chip_index, int midi_mode);

/* assigns MIDI channel to device */
int assign_midi_channel(struct snd_hdjmidi* umidi, u8 request_fast_retry);

/* to be called from module_init function */
void midi_init(void);

/* to be called from module_exit function */
void midi_cleanup(void);
#endif
