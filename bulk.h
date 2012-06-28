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

#if !defined(_BULK_H_)
#define _BULK_H_

/* in BCD */
extern u32 driver_version;

/* if defined, each ioctl will send a KERN_INFO entry printk */
#define IOCTL_TRACE_PRINTK
#ifdef IOCTL_TRACE_PRINTK
	#define ioctl_trace_printk(message... ) \
		do {printk(message); } while(0)
#else
	#define ioctl_trace_printk(message... ) 
#endif

#define DJC_HID_INTERFACE_NUMBER			1
#define DJMK2_HID_INTERFACE_NUMBER			5
#define DJRMX_HID_INTERFACE_NUMBER			5
#define DJSTEEL_BULK_INTERFACE_NUMBER		0

/* some devices allow the ability to set HID output report */
#define USB_REQ_SET_REPORT 					0x09
#define USB_HID_INPUT_REPORT				1
#define USB_HID_OUTPUT_REPORT				2
#define USB_HID_FEATURE_REPORT				3

/* output set report buffer sizes, including report ID */
#define DJC_SET_REPORT_LEN					4
#define DJMK2_SET_REPORT_LEN				4
#define DJRMX_SET_REPORT_LEN				5

/* Report IDs for our HID devices */
#define DJC_SET_REPORT_ID					1
#define DJMK2_SET_REPORT_ID					1
#define DJRMX_SET_REPORT_ID					1

struct hdj_open_list {
	struct list_head	list;
	struct file			*file;
	u8					is_releasing;
	long				access_count;
	struct list_head	read_list;
};

struct hdj_read_list {
	struct list_head	list;
	struct completion	read_completion;
	long				thread_id;
	struct list_head	list_buffers;
	atomic_t			num_pending_waits;
};

#define HDJ_READ_BUFFERS_COUNT		15UL
#define HDJ_POLL_INPUT_BUFFER_SIZE	64UL
struct hdj_read_buffers {
	struct list_head	list;
	u8			buffer[HDJ_POLL_INPUT_BUFFER_SIZE];
	u8			is_ready;
};

struct hdj_common_context {
	/*Common Settings:*/

	/* firmware version of the device, to be interpreted in a device specific way*/
	atomic_t	firmware_version;
};

struct hdj_console_context {
	/* maintains various device attributes */
	atomic_t	device_config;
	
	/* if we disable talkover, and then reenable it, we shall restore this attenuation */
	atomic_t	cached_talkover_atten;

	/* since we manipulate bitmasks on device_config we require synchronization */
	struct semaphore device_config_mutex;
	
	/* current device sample*/
	atomic_t	sample_rate;
};

/* for crossfader curve (style) - Mk2 ONLY */
#define DECKA_ATTENUATION_START_POSITION		128
#define CROSSFADER_ATTENUATION_MULTIPLIER_DEFAULT	7
struct hdj_mk2_rmx_context {
	
	/*DJ Console MK2*/
	/* Stores the talkover attenuation state, as defined in the firmware documentation*/
	atomic_t	talkover_atten;
	/* if we disable talkover, and then reenable it, we shall restore this attenuation */
	atomic_t	cached_talkover_atten; 
	
	/* Lower byte: stores the audio config for the device, as defined
	 *		by the firmware documentation.
	 * Upper byte: holds information on the topboard physical type*/
	atomic_t	audio_config;

	/* Crossfader lock setting */
	atomic_t	crossfader_lock;

	/* denotes current state of the mouse, enabled or not */
	atomic_t	mouse_enabled;

	/* denotes the crossfader curve */
	atomic_t	crossfader_style;

	/* jog wheel sensitivity */
	atomic_t	jog_wheel_sensitivity;

	/* jog wheel lock state */
	atomic_t	jog_wheel_lock_status;

	/* custom serial number */
	atomic_t	serial_number;
	
	/* current device sample*/
	atomic_t	sample_rate;
};

struct hdj_steel_context {
	/* boot mode, normal mode, or unknown mode */
	atomic_t		device_mode;

	/* sequence number within our bulk input packets */
	atomic_t		sequence_number;

	/* Bulk data: it is protected by bulk_buffer_lock */
	u8		bulk_data[DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE];
	spinlock_t	bulk_buffer_lock;
	atomic_t	is_bulk_read_request_pending;
	struct completion bulk_request_completion;
	struct semaphore bulk_request_mutex;

	/* the continuous bulk reader's completion routine keeps these up to date */
	atomic_t	fx_state;
	atomic_t	mode_shift_state;
	atomic_t	jog_wheel_parameters;

	/* custom serial number  */
	atomic_t	serial_number;
};

struct hdjbulk_in_endpoint {
	unsigned int interface_number;
	struct usb_interface	*iface;	
	struct usb_hdjbulk* ubulk;
	struct urb* urb;
	atomic_t urb_sequence_number;
	int max_transfer;		/* size of urb buffer */
};

#define DJ_POLL_INPUT_URB_COUNT	2

/* continuous reader state */
#define CR_UNINIT		0
#define CR_STARTED		1
#define CR_STOPPED		2

/* Structure to hold all of our device specific stuff */
struct usb_hdjbulk {
	struct list_head	list;
	struct snd_hdj_chip	*chip;
	struct usb_interface	*iface;			/* the interface for this device */

	u32			bulk_out_size;		/* the size of the receive buffer */
	u8			bulk_out_endpoint_addr;	/* the address of the bulk out endpoint */
	u32			bulk_in_size;		/* the size of the receive buffer */
	u8			bulk_in_endpoint_addr;	/* the address of the bulk in endpoint */
	int			bulk_out_pipe;		/* the bulk out pipe */
	atomic_t	open_count;		/* count the number of openers */
	struct kref		kref;
	u8			registered_usb_dev;

	/*Bulk Requests*/
	struct completion	bulk_out_completion;	/* allows synchronous signals for bulk requests */
	struct urb*		bulk_out_urb;		/* pointer to a URB reserved for bulk requests */

	/* buffer for sending bulk out requests- serialized with mutex */
	struct semaphore	bulk_out_buffer_mutex;
	void *			bulk_out_buffer;

	/* lock for bulk out URBs, used if pre reset is called */
	atomic_t		bulk_out_command_in_progress;

	/* Common Settings */
	struct hdj_common_context hdj_common;

	void *			device_context;

	atomic_t		current_urb_sequence_number;

	/***********************************************************************************
	*	Continuous reader support- this could represent polling the input bulk 
	*       interface on the DJ Control Steel, or the HID interface on another product
	************************************************************************************/
	struct hdjbulk_in_endpoint *bulk_in_endpoint[DJ_POLL_INPUT_URB_COUNT];
	u8 *reader_cached_buffer;
	/* list for maintaining state of clients for read */
	spinlock_t	read_list_lock;
	struct list_head open_list;
	/* continuous reader state */
	atomic_t	continuous_reader_state;
	/* sequence number for URBs- no action taken yet if comepletion occurs out of order */
	atomic_t	expected_urb_sequence_number;
	u32		continuous_reader_packet_size;

	/* Output buffer size for setting control information to the device */
	u8*		output_control_buffer;
	u16		output_control_buffer_size;
	struct 		usb_interface *control_interface;
	struct          urb* output_control_urb;
	struct 		semaphore output_control_mutex;
	struct 		usb_ctrlrequest* output_control_ctl_req; /* setup packet for our control requests */
	dma_addr_t 	output_control_dma;
	struct 		completion output_control_completion;

	/* support for read poll/select */
	wait_queue_head_t       read_poll_wait;
};

#define to_hdjbulk_dev(d) container_of(d, struct usb_hdjbulk, kref)

#ifdef CONFIG_PM
void hdjbulk_resume(struct list_head* p);
void snd_hdjbulk_suspend(struct list_head* p);
void snd_hdjbulk_pre_reset(struct list_head* p);
void snd_hdjbulk_post_reset(struct list_head* p);
#endif
void kill_bulk_urbs(struct usb_hdjbulk *ubulk, u8 free_urbs);
void hdjbulk_disconnect(struct list_head* p);
void hdj_delete(struct kref *kref);
int hdjbulk_open(struct inode *inode, struct file *file);
int hdjbulk_release(struct inode *inode, struct file *file);
ssize_t hdjbulk_read (struct file *, char __user *, size_t, loff_t *);
unsigned int hdjbulk_poll (struct file *, struct poll_table_struct *);
long hdjbulk_ioctl(struct file *file,	
					 unsigned int ioctl_num,	
					 unsigned long ioctl_param,
					 u8 compat_mode);
int send_vendor_request(int chip_index, 
			     u32 bmRequestIn, 
			     u32 bmRequest, 
			     u16 value, 
			     u16 index, 
			     u16 *TransferBuffer,
			     u8 force_send);
int hdjbulk_init_dj_console(struct usb_hdjbulk *ubulk);
int hdjbulk_init_dj_mk2(struct usb_hdjbulk *ubulk);
int hdjbulk_init_dj_rmx(struct usb_hdjbulk *ubulk);
int hdjbulk_init_dj_steel(struct usb_hdjbulk *ubulk);
int parse_bulk_file(struct usb_hdjbulk *ubulk, 
		  u8* file_data, 
		  struct FIRMWARE_HEADER** firmware_header, 
		  u8 * wrapped);
int check_crc(u8 * file_data, u32 size);
int firmware_start_bulk(struct usb_hdjbulk *ubulk, u16 index, u8 full_update);
int firmware_stop_bulk(struct usb_hdjbulk *ubulk, u16 index);

int firmware_send_bulk(struct usb_hdjbulk *ubulk,
		     u8* buffer,
		     u32 buffer_size,
		     u8 force_send);
int firmware_send_steel_upgrade_bulk(struct usb_hdjbulk *ubulk,
				     u8* buffer,
				     u32 buffer_size,
				     u32 block_number);
void lock_vendor_io(struct usb_hdjbulk *ubulk);
void unlock_vendor_io(struct usb_hdjbulk *ubulk);
void lock_bulk_output_io(struct usb_hdjbulk *ubulk);
void unlock_bulk_output_io(struct usb_hdjbulk *ubulk);

/* Sends output report already prepared in output_control_buffer, caller must synchronize with
 *  output_control_mutex */
int usb_set_report(struct usb_hdjbulk *ubulk, u8 type, u8 id);

/*
 * Creates and registers everything needed for a MIDI streaming interface.
 */
int hdj_create_bulk_interface(struct snd_hdj_chip* chip,
 			      struct usb_interface* iface);

/*
 * Submits the URB, with error handling.
 */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14) )
int hdjbulk_submit_urb(struct snd_hdj_chip* chip, struct urb* urb, gfp_t flags);
#else
int hdjbulk_submit_urb(struct snd_hdj_chip* chip, struct urb* urb, int flags);
#endif

/*
 * Processes the data read from the device.
 */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void hdjbulk_in_urb_complete(struct urb* urb);
#else
void hdjbulk_in_urb_complete(struct urb* urb, struct pt_regs *junk);
#endif

/*
 * Frees an output endpoint.
 * May be called when ep hasn't been initialized completely.
 */
void hdjbulk_in_endpoint_delete(struct hdjbulk_in_endpoint* ep);

int hdjbulk_input_start_ep(struct hdjbulk_in_endpoint* ep);
void hdjbulk_input_kill_urbs(struct hdjbulk_in_endpoint* ep);
u8 is_continuous_reader_supported(struct snd_hdj_chip* chip);
int start_continuous_reader(struct usb_hdjbulk *ubulk);
int stop_continuous_reader(struct usb_hdjbulk *ubulk);

/*
 * get the data from the bulk endpoint
 */
int get_bulk_data(struct usb_hdjbulk *ubulk,
			u8* bulk_data,
			u32 size,
			u8 force_send);

int send_bulk_write(struct usb_hdjbulk *ubulk,
			u8* bulk_data,
			u32 size,
			u8 force_send);

int wait_for_bulk_answer(struct usb_hdjbulk *ubulk,
				u8* bulk_data,
				u32 size, 
				u32 timeout);

int send_boot_loader_command(struct usb_hdjbulk *ubulk, 
			     u8 boot_loader_command);
			     
/* ALERT: read_list_lock needs to be acquired before calling */
void signal_all_waiting_readers(struct list_head *open_list);

int has_bulk_interface(struct snd_hdj_chip* chip);

static inline void * zero_alloc(size_t size, int flags) {
	void * allocated_memory = kmalloc(size, flags);
	if (allocated_memory) {
		/*zero the memory*/
		memset(allocated_memory, 0, size);
	}
	return allocated_memory;
}
#endif
