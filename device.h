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

#if !defined(_HDJDEVICE2_H_)
#define _HDJDEVICE_H_

extern int					netlink_unit;

#define							MIN_NETLINK_UNIT		22

/* Define these values to match your devices */
#define USB_HDJ_VENDOR_ID		0x6f8
#define DJ_CONSOLE_PID			0xb000
#define DJ_CONSOLE_MK2_PID		0xb100
#define DJ_CONSOLE_RMX_PID		0xb101
#define DJ_CONTROL_MP3_PID		0xd000
#define DJ_CONTROL_MP3W_PID		0xd001
#define DJ_CONSOLE_STEEL_PID	0xb102

#define DJ_BULK_IFNUM			0
#define DJ_MIDI_IF_NUM			4
#define DJ_MIDI_STEEL_IF_NUM	1
#define DJ_MP3_IF_NUM			2
#define DJ_ASIO_DJ1_IF_NUM		5
#define DJ_ASIO_MK2_RMX_IF_NUM	7
#define DJ_MP3_HID_IF_NUM		0

extern struct usb_driver hdj_driver;

/* Usermode clients register for notifications over netlink, and we keep track of clients' state 
 *  using this structure */
struct netlink_list{
	struct list_head	list;
	int					pid;
	struct file*		file;
	void*				context;
	u8					compat_mode;
};

/* forward declaration */
struct snd_hdj_caps;

/* Context for card instance */
struct snd_hdj_chip {
	int index;
	struct usb_device *dev;
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
	struct snd_card *card;
#else
	snd_card_t *card;
#endif
	/* from usb_make_path */
	char usb_device_path[LOCATION_ID_LEN];
	u32 usb_id;
	u32 product_code;
	atomic_t shutdown;
	atomic_t no_urb_submission;
	atomic_t num_interfaces;
	atomic_t num_suspended_intf;
	
	/* our capabilities (e.g. can we save channel to device, number of ports, etc) */
	struct snd_hdj_caps caps;

	struct semaphore	vendor_request_mutex;	/* synchronize vendor requests */

	/* atomic variables for locking IO */
	atomic_t		locked_io;
	atomic_t		vendor_command_in_progress;

	/* for control requests */
	struct completion	ctrl_completion;	/* allows synchronous signal for control requests */
	struct urb*			ctrl_urb;		/* pointer to a URB reserved for control requests */
	void*				ctrl_req_buffer;	/* pointer to buffer for control requests 16 bits */
	u16			ctrl_req_buffer_len;	/* length of control request buffer */
	int			ctrl_in_pipe;		/* the control pipe in*/
	int			ctrl_out_pipe;		/* the control pipe out*/
	struct usb_ctrlrequest* ctl_req;		/* setup packet for our control requests */
	dma_addr_t 		ctl_req_dma; /* DMA setup for control requests */

	/* List of midi interfaces */
	struct list_head midi_list;	
	atomic_t next_midi_device;

	/* List of bulk interfaces */
	struct list_head bulk_list;	
	atomic_t next_bulk_device;
	
	/***********************************************************************************
	*	Netlink Notification State (notification to usermode)
	************************************************************************************/
	/* we maintain state of usermode clients for netlink notifications*/
	struct list_head	netlink_registered_processes;
	struct semaphore	netlink_list_mutex;

	/* chip reference count- accessed by inc_chip_ref_count() and dec_chip_ref_count() */
	int ref_count;
};

/* Get a minor range for your devices from the usb maintainer */
#define USB_HDJ_MINOR_BASE	192

/* For control requests */
#define REQT_READ       	0xc0
#define REQT_WRITE      	0x40

#define DJ_VERSION_REQUEST	2
#define DJ_CONFIG_REQUEST	0x3

/*
 * MIDI management for DJ Console only- used to manage transition from "virtual"
 *  (i.e. control topboard) and the physical ports.  All other MIDI devices
 *  thus far do not have physical MIDI ports, and thus they require no such
 *  commands.
 */
#define START_PHYSICAL_MIDI_IN_REQUEST			0x06
#define STOP_PHYSICAL_MIDI_IN_REQUEST			0x07
#define START_VIRTUAL_MIDI_IN_REQUEST			0x09
#define STOP_VIRTUAL_MIDI_IN_REQUEST			0x0a

/*
 * for setting/requesting the sample rate...they all hold for DJ2. except that reset request
 *  is ignored
 */
#define DJ_SET_SAMPLE_RATE						0xB
#define DJ_GET_SAMPLE_RATE						0xC
#define DJ_REQUEST_RESET						2
#define REQUEST_SET_SAMPLE_RATE_44100			1
#define REQUEST_SET_SAMPLE_RATE_48000			0
#define REQUEST_GET_SAMPLE_RATE_44100			44100UL
#define REQUEST_GET_SAMPLE_RATE_48000			48000UL

#define DJ_DRV_START_BULK	0	/* Start bulk transfer. No parameter. */
#define DJ_DRV_STOP_BULK	1	/* Stop bulk transfer. No parameter. */

/*
 * DJ2 specific commands.  SR and firmware update commands above and still valid
 *  (except for noted exceptions)
 */
#define DJ_SET_TALKOVER				3
#define DJ_GET_TALKOVER				4
#define DJ_SET_AUDIO_CONFIG			5
#define DJ_GET_AUDIO_CONFIG			6
#define DJ_DISABLE_MOUSE			9
#define DJ_ENABLE_MOUSE				10
#define DJ_UPDATE_UPPER_SECTION		13 
#define DJ_SET_CFADER_LOCK			14
#define DJ_GET_CFADER_LOCK			15
#define DJ_SET_CROSSFADER_STYLE		16
#define DJ_SET_MIDI_CHANNEL			19
#define DJ_GET_MIDI_CHANNEL			20

/*RMX specific commands*/
#define DJ_SET_JOG_WHEEL_SENSITIVITY		21
#define DJ_GET_JOG_WHEEL_SENSITIVITY		22
#define DJ_SET_JOG_WHEEL_LOCK_SETTING		23
#define DJ_GET_JOG_WHEEL_LOCK_SETTING		24
#define DJ_SET_SERIAL_NUMBER_WORD_1			26
#define DJ_SET_SERIAL_NUMBER_WORD_2			27
#define DJ_GET_SERIAL_NUMBER_WORD_1			28
#define DJ_GET_SERIAL_NUMBER_WORD_2			29

/*
 *	DJ Console DJ Config settings 
 */
/*default DJ Console 1 device config*/
#define DJ_DEFAULT_CONFIG	0x0000

/*
 * DJ2 and Rmx audio config bit masks 
 */
/* AUDIO CONFIG bit masks */
#define AUDIO_CONFIG_ASIO_EN			0x80

/* Audio firmware upgrade */
#define DJ_DRV_TRANSFER_TYPE_AUDIO		1

/* used for debugging only! 
void write_to_file(const char* fmt, ...); */
struct snd_hdj_chip* inc_chip_ref_count(int chip_index);
struct snd_hdj_chip* dec_chip_ref_count(int chip_index);

/* since we could be "alive" for a bit of time after disconnect if a usermode
 *  client maintains a handle, we reference interface and devices */
void reference_usb_intf_and_devices(struct usb_interface *intf);
void dereference_usb_intf_and_devices(struct usb_interface *intf);

/* MARK: PRODCHANGE */
/* just does a printk of the product name and IDs, and driver version */
void dump_product_name_to_console(struct snd_hdj_chip* chip,
									u8 output_bulk_info,
									u8 output_midi_info);

/* warning: assumes that inc_chip_ref_count() has been called on the chip */
struct usb_hdjbulk * bulk_from_chip(struct snd_hdj_chip* chip);
/* warning: assumes that inc_chip_ref_count() has been called on the chip */
struct snd_hdjmidi* midi_from_chip(struct snd_hdj_chip* chip);

int init_netlink_state(struct snd_hdj_chip* chip);
void uninit_netlink_state(struct snd_hdj_chip* chip);
int register_for_netlink(struct snd_hdj_chip* chip, 
							struct file* file, 
							void* context,
							u8 compat_mode);
/* Note: if file==NULL unregister all clients */
int unregister_for_netlink(struct snd_hdj_chip* chip, struct file* file);
/* This sends a control change to all listeners (over netlink in usermode), and formats
 *  the control change data into a control change message */
int send_control_change_over_netlink(struct snd_hdj_chip* chip, 
									unsigned long product_code,
									unsigned long control_id,
									unsigned long control_value);
#ifdef DEBUG
int netlink_test(struct snd_hdj_chip* chip);
#endif

int is_device_present(struct snd_hdj_chip* chip);
#endif
