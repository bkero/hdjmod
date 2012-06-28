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

#ifndef _DJ_DEVIO_CTLS__H_
#define _DJ_DEVIO_CTLS__H_

/* 
 * The major device number. 
 */
#define MAJOR_NUM 80

/* invalid netlink unit value */
#define	NETLINK_UNIT_INVALID_VALUE		-1


/*
 * Bit code for DJ Console.
 */
#define DJC_AUDIOCFG_DIGITAL_IN			0x0001	/* else analog in */
#define DJC_AUDIOCFG_ANALOG_IN_LINE		0x0002	/* else mic in */
#define DJC_AUDIOCFG_DIGITAL_IN_COAXIAL		0x0004	/* else optical in */
#define DJC_AUDIOCFG_ADVANCED_MODE		0x0008	/* else normal mode */
#define DJC_AUDIOCFG_RESOLUTION_24_BIT		0x0010	/* else 16-bit 44.1 kHz */
#define DJC_AUDIOCFG_ASIO_MODE			0x0020	/* else in wave mode */
#define DJC_AUDIOCFG_MUTE_REAR_3_4		0x0040	/* else unnmute 3/4. */
#define DJC_AUDIOCFG_MIDI_IN_BUTTONS		0x0080	/* if set MIDI In: DJ Console buttons
                                                      	 * else MIDI In: DJ Console plug.*/
#define DJC_AUDIOCFG_TALKOVER_ATT_VAL		0x1F00	/* in 1/2 dB. */
#define DJC_AUDIOCFG_INPUT_MONITORED		0x2000	/* If set, Input is monitored to ch. 1/2 */
#define DJC_AUDIOCFG_TALKOVER_ATT_ENABLE	0x4000	/* If set, Talkover Attenuation is on. */
#define DJC_AUDIOCFG_HEADPHONE_INPUT_SEL	0x8000	/* 0 = output ch. 3/4, 1 = output ch. 1/2 */

/* we wish to disable the mouse on startup- this is done through a set report */
#define DJC_HID_OUT_DISABLE_MOUSE_BYTE_POS	3
#define DJC_HID_OUT_DISABLE_MOUSE_BIT_POS	2

/*
 * Bit code for DJ Console Mk2.
 */
#define DJMK2_TALKOVER_ATT_MASK_VALUE		0x3F	/* Talkover attenuation in 1/2 dB. */
#define DJMK2_TALKOVER_ATT_ENABLE		0x40	/* else talkover disable. */

#define DJMK2_AUDIOCFG_USE_EXTERNAL_MIXER	0x02	/* If set, external mixer in use. */
#define DJMK2_AUDIOCFG_DECK_B_OUTPUT_3_4	0x04	/* Audio source: 0 = input 3/4, 1 = usb 3/4 */
#define DJMK2_AUDIOCFG_DECK_A_OUTPUT_1_2	0x08	/* Audio source: 0 = input 1/2, 1 = usb 1/2 */
#define DJMK2_AUDIOCFG_HW_MUTE_CH_3_4		0x10	/* else unmute 3/4. */
#define DJMK2_AUDIOCFG_RECORD_INPUT_LINE_IN	0x20	/* else record input microphone. */
#define DJMK2_AUDIOCFG_ENABLE_24_BITS		0x40	/* else 16-bit (Windows only) */
#define DJMK2_AUDIOCFG_ENABLE_ASIO_MODE		0x80	/* else WDM (Windows only) */

#define DJMK2_FIRMWARE_TUSB_VERSION		0x00FF	/* Firmware TUSB version (Audio Part + usb) */
#define DJMK2_FIRMWARE_PSOC_VERSION		0x0F00	/* Firmware PSOC version (Controller Part) */
#define DJMK2_FIRMWARE_HARDWARE_VERSION		0xF000	/* Hardware version */

/*
 * Bit code for DJ Console Rmx.
 */
#define DJRMX_JOGWHEEL_LOCK_NONE		0x0000	/* Both Jog-Wheel are unlock */
#define DJRMX_JOGWHEEL_LOCK_DECK_A		0x0100	/* Only Jog-Wheel on Deck A is lock */
#define DJRMX_JOGWHEEL_LOCK_DECK_B		0x0001	/* Only Jog-Wheel on Deck B is lock */
#define DJRMX_JOGWHEEL_LOCK_DECK_BOTH		0x0101	/* Both Jog-Wheel are lock */

#define DJRMX_JOGWHEEL_SENSITIVITY_NORMAL	0x00	/* Normal Jog-Wheel sensitivity */
#define DJRMX_JOGWHEEL_SENSITIVITY_LESS		0x01	/* Jog-Wheel are less sensitive. */
#define DJRMX_JOGWHEEL_SENSITIVITY_LEAST	0x02	/* Jog-Wheel are least sensitive. */
#define DJRMX_JOGWHEEL_SENSITIVITY_MASK		0xFF

#define DJRMX_TALKOVER_ATT_MASK_VALUE		0x7F	/* Talk-over attenuation in 1/2 dB. */
#define DJRMX_TALKOVER_ATT_ENABLE		0x80	/* else talkover disable */

#define DJRMX_AUDIOCFG_MAIN_OUT_MIX		0x01	/* Main out is mix with record input (line-in or mic) */
#define DJRMX_AUDIOCFG_STREAM_12_ON_HEADPHONE	0x02	/* Stream 1-2 in headphone */
#define DJRMX_AUDIOCFG_HEADPHONE_MIX_34_12	0x04	/* Stream mix of 1-2 and 3-4 in headphone */
#define DJRMX_AUDIOCFG_MUTE_34			0x10	/* else unmute 3-4 */
#define DJRMX_AUDIOCFG_RECORD_INPUT_LINE_IN	0x20	/* else record input is set to microphone */

#define DJRMX_FIRMWARE_TUSB_VERSION		0x00FF	/* TUSB Firmware version (audio part + usb) */
#define DJRMX_FIRMWARE_PSOC_VERSION		0x0F00	/* PSOC Firmware version (controller part) */
#define DJRMX_FIRMWARE_HARDWARE_VERSION		0xF000	/* Hardware version */

/* 
 * Bit code for common jogwheel sensitivity and lock format
 */
#define COMMON_JOGWHEEL_LOCK_NONE		0x0000	/* Both Jog-Wheel are unlock */
#define COMMON_JOGWHEEL_LOCK_DECK_A		0x0001	/* Only Jog-Wheel on Deck A is lock */
#define COMMON_JOGWHEEL_LOCK_DECK_B		0x0100	/* Only Jog-Wheel on Deck B is lock */
#define COMMON_JOGWHEEL_LOCK_DECK_BOTH		0x0101	/* Both Jog-Wheel are lock */

#define COMMON_JOGWHEEL_SENSITIVITY_NORMAL	0x00	/* Normal Jog-Wheel sensitivity */
#define COMMON_JOGWHEEL_SENSITIVITY_LESS	0x01	/* Jog-Wheel are less sensitive. */
#define COMMON_JOGWHEEL_SENSITIVITY_LEAST	0x02	/* Jog-Wheel are least sensitive. */
#define COMMON_JOGWHEEL_SENSITIVITY_MASK	0xFF	/* sensitivity mask */
#define COMMON_TO_DJSTEEL_JOGWHEEL_SENSITIVITY_SF	0	/* shift for jogwheel sensivity */

/*
 * Bit code for DJ Control Steel.
 */
#define DJSTEEL_JOGWHEEL_LOCK_MASK		0x0300	/* Jog-Wheel Parameter (Mask for lock) */
#define DJSTEEL_JOGWHEEL_LOCK_NONE		0x0000	/* Both jog-wheel are enable. */
#define DJSTEEL_JOGWHEEL_LOCK_DECK_A		0x0100	/* Disable Jog-Wheel on Deck A. */
#define DJSTEEL_JOGWHEEL_LOCK_DECK_B		0x0200	/* Disable Jog-Wheel on Deck B. */
#define DJSTEEL_JOGWHEEL_LOCK_DECK_BOTH		0x0300	/* Disable Jog-Wheel on both deck. */
#define DJSTEEL_TO_COMMON_JOGWHEEL_LOCK_SF			8

#define DJSTEEL_TO_COMMON_JOGWHEEL_SENSITIVITY_SF		0	/* shift for jogwheel sensivity */
#define DJSTEEL_JOGWHEEL_SENSITIVITY_MASK	0x0003	/* Jog-Wheel Parameter (Mask for sensitive) */
#define DJSTEEL_JOGWHEEL_SENSITIVITY_NORMAL	0x0000	/* Normal Jog-Wheel sensitivity */
#define DJSTEEL_JOGWHEEL_SENSITIVITY_LESS	0x0001	/* Jog-Wheel are less sensitive. */
#define DJSTEEL_JOGWHEEL_SENSITIVITY_LEAST	0x0002	/* Jog-Wheel are least sensitive. */

#define DJ_CONTROL_STEEL_BULK_TRANSFER_MIN_SIZE		60
#define DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE			64
#define DJ_STEEL_MIN_BLOCK_NUMBER					0x4C
#define DJ_STEEL_MAX_BLOCK_NUMBER					0xFF
#define DJ_CONTROL_STEEL_MAX_FIRMWARE_SIZE	((DJ_STEEL_MAX_BLOCK_NUMBER - DJ_STEEL_MIN_BLOCK_NUMBER + 1) * DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE)

/* Max Firmware Size The Steel has the biggest firmware */
#define DJCONSOLES_FIRMWARE_SIZE		DJ_CONTROL_STEEL_MAX_FIRMWARE_SIZE

/* Location ID length in bytes */
#define LOCATION_ID_LEN					255

/* see msg_magic_number below */
#define MAGIC_NUM						0xdbdbface

/* every notification message from the driver has this header appended to it */
struct netlink_msg_header {
	unsigned long		msg_magic_number; /* to recognize valid packet boundary */
	unsigned long		msg_id;
	void*				context;
	unsigned long		bytes_to_follow;
	unsigned long		header_size; /* size of this header */
	unsigned long		reserved1;
	unsigned long		reserved2;
	unsigned long		reserved3;
	unsigned long		reserved4;
	unsigned long long	reserved5;
};

#ifdef CONFIG_COMPAT
struct netlink_msg_header32 {
	compat_ulong_t		msg_magic_number; /* to recognize valid packet boundary */
	compat_ulong_t		msg_id;
	compat_ulong_t		context;
	compat_ulong_t		bytes_to_follow;
	compat_ulong_t		header_size; /* size of this header */
	compat_ulong_t		reserved1;
	compat_ulong_t		reserved2;
	compat_ulong_t		reserved3;
	compat_ulong_t		reserved4;
	compat_u64			reserved5;
};
#endif

/* This message is concerned with changes on controls which can be set by IOCTLs.  Its associated message ID which
 *  would be placed in the netlink_msg_header is MSG_CONTROL_CHANGE
 */
struct control_change_data {
	unsigned long		product_code;
	__u8				location_id[LOCATION_ID_LEN];
	unsigned long		control_id; /* see callback.h for messages */
	unsigned long		control_value;
	unsigned long		reserved1;
	unsigned long		reserved2;
	unsigned long		reserved3;
	unsigned long		reserved4;
	unsigned long long	reserved5;
};

#ifdef CONFIG_COMPAT
struct control_change_data32 {
	compat_ulong_t		product_code;
	__u8				location_id[LOCATION_ID_LEN];
	compat_ulong_t		control_id; /* see callback.h for messages */
	compat_ulong_t		control_value;
	compat_ulong_t		reserved1;
	compat_ulong_t		reserved2;
	compat_ulong_t		reserved3;
	compat_ulong_t		reserved4;
	compat_u64			reserved5;
};
#endif

#define PSOC_26_CODE				1
#define PSOC_27_CODE				2
#define WELTREND_CODE				3
#define PSOC_RMX_UPGRADEABLE		1
#define PSOC_RMX_BOOTMODE			0xf
#define FIRST_PSOC1_VERSION			4
#define FIRST_PSOC2_VERSION			5

/* used in snd_hdj_caps structure below */
enum controller_type
{
	CONTROLLER_TYPE_UNKNOWN=0,
	CONTROLLER_TYPE_PSOC_26=1,
	CONTROLLER_TYPE_PSOC_27=2,
	CONTROLLER_TYPE_WELTREND=3,
	CONTROLLER_TYPE_PSCU=4,
	CONTROLLER_TYPE_ETOMS=5
};

/* generalizes capabilities for DJ device */
struct snd_hdj_caps {
	/* signifies that the MIDI channel is saved on the device */
	__u8 non_volatile_channel;

	/* signifies that the device has physical ports, and the
	*  port_mode toggles between physical and virtual ports 
	*/
	__u8 port_mode;

	/* signifies that the device has no bulk endpoints, but only HID
	*  endpoints.  Therefore convert HID to MIDI for input and vice-versa
	*  for output */
	__u8 hid_support_only;
	__u8 hid_interface_to_poll;
	
	/* specifies whether hid (set report) or bulk output controls the LEDs */
	__u8 leds_hid_controlled;
	__u8 leds_bulk_controlled;
	__u8 leds_report_len; /* could be bulk or HID set report */
	__u8 leds_report_id; /* could be bulk or HID set report */

	/* midi is supported */
	__u8 midi; // could be mapped via HID
	__u8 midi_bulk; // true MIDI endpoint
	__u8 midi_mapping_supported;

	/* number of MIDI ports */
	__u8 num_out_ports;
	__u8 num_in_ports;

	/* jog wheels can be locked */
	__u8 jog_locking;

	/* sensitivity can be set on jogs */
	__u8 jog_sensitivity;

	/* has talkover attenuation */
	__u8 talkover_atten;

	/* supports old djcondig word interface */
	__u8 djconfig_word;

	/* supports newer audio config interface */
	__u8 audio_config;

	/* has a mouse */
	__u8 mouse;

	/* can sample rate can be queried, or written to*/
	__u8 sample_rate_readable;
	__u8 sample_rate_writable;

	/* supports locking the xfader */
	__u8 xfader_lock;

	/* supports xfader curve setting */
	__u8 xfader_curve;

	/* supports shift mode */
	__u8 shift_mode;

	/* supports fx state */
	__u8 fx_state;

	/* Information on audio board */
	__u8 audio_board_present;
	__u8 audio_board_upgradeable;
	__u8 audio_board_upgradeable_full;
	__u8 audio_board_upgradeable_partial;
	__u8 audio_board_version;
	__u8 audio_board_in_boot_mode;

	/* specifies whether the topboard is upgradable */
	__u8 controller_board_present;
	__u8 controller_board_upgradeable;
	__u8 controller_board_version;
	__u8 controller_type;
	__u8 controller_upgrade_requires_USB_reenumeration;
	__u8 controller_board_in_boot_mode;
};

/*
 Firmware Header:
 4 bytes: ID File (determine which binary it's)
 4 bytes: Product ID (determine for which device)
 4 bytes: Version (determine which version)
 4 bytes: Human Readable Date: YYYY/MM/DD. (YYYYMMDD)
 4 bytes: Human Readable Time: 0x00 + HH:MM:SS (00HHMMSS)
 4 bytes: Start Address.
 4 bytes: Reserved (zero)
 4 bytes: Firmware CheckSum
*/
struct FIRMWARE_HEADER
{
	__u8  id[4];
	__u32 product_id;
	__u32 version;
	__u32 date;
	__u32 time;
	__u32 start_address;
	__u32 reserved;
	__u32 checksum;
};

/*
	Structure which contains the firmware
	This is used for the firmware update ioctl.
*/
struct FIRMWARE_FILE
{
	__u32 size;
	__u8 file[DJCONSOLES_FIRMWARE_SIZE + sizeof(struct FIRMWARE_HEADER)];
};

#define DJ_STEEL_IN_UNKNOWN_MODE				0x0
#define DJ_STEEL_IN_BOOT_MODE					0x1
#define DJ_STEEL_IN_NORMAL_MODE					0x2

#define BULK_READ_TIMEOUT						50
#define BULK_UPGRADE_TIMEOUT					100
#define BULK_BOOTLOADER_TIMEOUT					1000

#define DJ_STEEL_EP_81_FX_STATE_0					6
#define DJ_STEEL_EP_81_FX_STATE_1					7
#define DJ_STEEL_EP_81_MODE_SHIFT_STATE				8
#define DJ_STEEL_EP_81_FIRMWARE_VERSION				50
#define DJ_STEEL_EP_81_SERIAL_NUM_BITS_31_TO_24		51
#define DJ_STEEL_EP_81_SERIAL_NUM_BITS_23_TO_16		52
#define DJ_STEEL_EP_81_SERIAL_NUM_BITS_15_TO_08		53
#define DJ_STEEL_EP_81_SERIAL_NUM_BITS_07_TO_00		54
#define DJ_STEEL_EP_81_MIDI_CHANNEL					55
#define DJ_STEEL_EP_81_SEQ_NUM						56
#define DJ_STEEL_EP_81_JOG_WHEEL_SETTINGS_0			58
#define DJ_STEEL_EP_81_JOG_WHEEL_SETTINGS_1			59

#define DJ_STEEL_STANDARD_SET_LED_REPORT		0x01
#define DJ_STEEL_SET_MODE_SHIFT					0x02
#define DJ_STEEL_SET_FX_STATE					0x03
#define DJ_STEEL_FORCE_REPORT_IN				0x04
#define DJ_STEEL_SET_POLLING_RATE				0x05
#define DJ_STEEL_SET_NON_VOLATILE_DATA			0x06
#define DJ_STEEL_REBOOT_TO_BOOT_MODE			0x07
#define DJ_STEEL_SET_JOG_WHEEL_PARAMETER		0x08

#define DJ_MAX_RETRY							5
#define DJ_STEEL_MAX_RETRY_UPGRADE				20
#define DJ_STEEL_BOOT_LOADER_RESPONSE			0x00
#define DJ_STEEL_BOOT_LOADER_SUCCESS			0x20
#define DJ_STEEL_BOOT_LOADER_SUCCESS_2			0x21
#define DJ_STEEL_BOOT_LOADER_COMPLETE			0x01

#define DJ_STEEL_BOOT_LOADER_MODE				0xFF
#define DJ_STEEL_ENTER_BOOT_LOADER				0x38
#define DJ_STEEL_WRITE_BLOCK					0x39
#define DJ_STEEL_EXIT_BOOT_LOADER				0x3B

/* Default serial numbers for products which support them */
#define STEEL_DEFAULT_SERIAL_NUMBER				0x30303030
#define RMX_DEFAULT_SERIAL_NUMBER				0

/* product codes */
#define DJCONSOLE_PRODUCT_UNKNOWN				0
#define DJCONSOLE_PRODUCT_CODE					1
#define DJCONSOLE2_PRODUCT_CODE					2
#define DJCONTROLLER_PRODUCT_CODE				3
#define DJCONSOLERMX_PRODUCT_CODE				4
#define DJCONTROLSTEEL_PRODUCT_CODE				5

/* DJ_IOCTL_GET_VERSION
 * Returns driver version in BCD in ioctl_param
 * IOCTL required buffer size: u32
 */
#define DJ_IOCTL_GET_VERSION			_IOR (MAJOR_NUM,  0, __u32)

/* DJ_IOCTL_GET_JOG_WHEEL_LOCK_SETTING
 * Returns the jog wheel lock state from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_JOG_WHEEL_LOCK_SETTING	_IOR (MAJOR_NUM,  1, __u16)

/* DJ_IOCTL_SET_JOG_WHEEL_LOCK_SETTING
 * Applies the the jog wheel lock state in ioctl_param to the hardware
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_SET_JOG_WHEEL_LOCK_SETTING	_IOW (MAJOR_NUM,  2, __u16)

/* DJ_IOCTL_GET_JOG_WHEEL_SENSITIVITY
 * Returns the jog wheel sensitivity from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_JOG_WHEEL_SENSITIVITY	_IOR (MAJOR_NUM,  3, __u16)

/* DJ_IOCTL_SET_JOG_WHEEL_SENSITIVITY
 * Applies the the jog wheel sensitivity in ioctl_param to the hardware
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_SET_JOG_WHEEL_SENSITIVITY	_IOW (MAJOR_NUM,  4, __u16)

/* DJ_IOCTL_SET_MIDI_CHANNEL
 * Applies the the MIDI channel in ioctl_param to the hardware.  Note that the
 *  actual channel applied is returned, due to possible conflict with other
 *  currently active DJ devices
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_SET_MIDI_CHANNEL		_IOWR(MAJOR_NUM,  5, __u16)

/* DJ_IOCTL_GET_MIDI_CHANNEL
 * Returns the MIDI channel from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_MIDI_CHANNEL		_IOR (MAJOR_NUM,  6, __u16)

/* DJ_IOCTL_GET_PRODUCT_CODE
 * Returns the product code of the current hardware in ioctl_param
 * IOCTL Required buffer size: u32
 */
#define DJ_IOCTL_GET_PRODUCT_CODE		_IOR (MAJOR_NUM,  8, __u32)

/* For use only by firmware updater application */
/* DJ_IOCTL_LOCK_IO
 * Ensures that vendor specific I/O is forbidden, and waits for any current 
 *  vendor specific I/O t drain.
 * IOCTL Required buffer size: none
 */
#define DJ_IOCTL_LOCK_IO			_IO  (MAJOR_NUM,  9)

/* For use only by firmware updater application */
/* DJ_IOCTL_UNLOCK_IO 
 * Reverses effect of DJ_IOCTL_LOCK_IO
 * IOCTL Required buffer size: none
 */
#define DJ_IOCTL_UNLOCK_IO			_IO  (MAJOR_NUM, 10)

/* DJ_IOCTL_GET_TALKOVER_ATT
 * Returns the talkover attenuation from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_TALKOVER_ATT		_IOR (MAJOR_NUM, 11, __u16)

/* DJ_IOCTL_SET_TALKOVER_ATT
 * Applies the talkover attenuation in ioctl_param to the hardware
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_SET_TALKOVER_ATT		_IOW (MAJOR_NUM, 12, __u16)

/* DJ_IOCTL_SET_TALKOVER_ENABLE
 * Enabled or disables talkover attenuation
 * IOCTL Required buffer size: u8, 0 disable, non-zero enable
 */
#define DJ_IOCTL_SET_TALKOVER_ENABLE	_IOW (MAJOR_NUM, 13, __u8)

/* DJ_IOCTL_GET_TALKOVER_ENABLE
 * Returns whether talkover is enabled or not
 * IOCTL Required buffer size: u8, 0 disabled, non-zero enabled
 */
#define DJ_IOCTL_GET_TALKOVER_ENABLE	_IOR (MAJOR_NUM, 14, __u8)

/* DJ_IOCTL_GET_FIRMWARE_VERSION
 * Returns the firmware version from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_FIRMWARE_VERSION	_IOR (MAJOR_NUM, 15, __u16)

/* DJ_IOCTL_GET_DJCONSOLE_CONFIG
 * Returns the device config in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_DJCONSOLE_CONFIG	_IOR (MAJOR_NUM, 16, __u16)

/* DJ_IOCTL_SET_DJCONSOLE_CONFIG
 * Applies the DJ Console device config word in ioctl_param to the hardware.
 * IOCTL Required buffer size: u32 (upper word is a bitmask for desired
 *  operation, lower word holds the data).
 */
#define DJ_IOCTL_SET_DJCONSOLE_CONFIG	_IOW (MAJOR_NUM, 17, __u32)

/* DJ_IOCTL_GET_AUDIO_CONFIG
 * Returns the audio config from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_AUDIO_CONFIG		_IOR (MAJOR_NUM, 18, __u16)

/* DJ_IOCTL_SET_AUDIO_CONFIG
 * Applies the audio config in ioctl_param to the hardware
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_SET_AUDIO_CONFIG		_IOW (MAJOR_NUM, 19, __u16)

/* DJ_IOCTL_DISABLE_MOUSE
 * Disable the DJ Mouse
 * IOCTL Required buffer size: none
 */
#define DJ_IOCTL_DISABLE_MOUSE			_IO  (MAJOR_NUM, 20)

/* DJ_IOCTL_ENABLE_MOUSE
 * Enable the DJ Mouse
 * IOCTL Required buffer size: none
 */
#define DJ_IOCTL_ENABLE_MOUSE			_IO  (MAJOR_NUM, 21)

/* DJ_IOCTL_GET_MOUSE_STATE
 * Returns the DJ Mouse state from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_MOUSE_STATE		_IOR (MAJOR_NUM, 22, __u16)

/* DJ_IOCTL_GET_SAMPLE_RATE
 * Returns the sample rate from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_SAMPLE_RATE		_IOR (MAJOR_NUM, 23, __u16)

/* DJ_IOCTL_SET_SAMPLE_RATE
 * Applies the sample rate in ioctl_param to the hardware
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_SET_SAMPLE_RATE		_IOW (MAJOR_NUM, 24, __u16)

/* DJ_IOCTL_SET_CFADER_LOCK
 * Applies the crossfader lock in ioctl_param to the hardware
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_SET_CFADER_LOCK		_IOW (MAJOR_NUM, 25, __u16)

/* DJ_IOCTL_GET_CFADER_LOCK
 * Returns the cross fader lock from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_CFADER_LOCK		_IOR (MAJOR_NUM, 26, __u16)

/* DJ_IOCTL_SET_CROSSFADER_STYLE
 * Applies the crossfader style in ioctl_param to the hardware
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_SET_CROSSFADER_STYLE	_IOW (MAJOR_NUM, 27, __u16)

/* DJ_IOCTL_GET_CROSSFADER_STYLE
 * Returns the cross fader style from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_GET_CROSSFADER_STYLE	_IOR (MAJOR_NUM, 28, __u16)

/* For use only by firmware updater application */
/* IOCTL_DJBULK_UPGRADE_FIRMWARE
 * Performs a firmware upgrade- reserved for use by the firmware updater program
 * IOCTL Required buffer size: properly initialized struct FIRMWARE_FILE, which
 *                             details the firmware file size (this includes the
 *				the firmware header), the firmware header,
 *				and the actual firmware data.
 */
#define DJ_IOCTL_DJBULK_UPGRADE_FIRMWARE	_IOW (MAJOR_NUM, 29, struct FIRMWARE_FILE*)

/* DJ_IOCTL_SET_MODE_SHIFT_STATE
 * Applies the shift mode state in ioctl_param to the hardware
 * IOCTL Required buffer size: u8
 *  BufferFormat:
 *		mode_shift_state:Bit7: Mstate_DA
 *				 Bit6: Mstate_DB
 *	  			 Bit 5 to 0: Don't care
 */
#define DJ_IOCTL_SET_MODE_SHIFT_STATE		_IOW (MAJOR_NUM, 30, __u8)

/* DJ_IOCTL_GET_MODE_SHIFT_STATE
 * Returns the shift mode state from the hardware in ioctl_param
 * IOCTL Required buffer size: u8
 *  BufferFormat:
 *		mode_shift_state:Bit7: Mstate_DA
 *				 Bit6: Mstate_DB
 *	  			 Bit 5 to 0: Don't care
 */
#define DJ_IOCTL_GET_MODE_SHIFT_STATE		_IOR (MAJOR_NUM, 31, __u8)

/* DJ_IOCTL_SET_FX_STATE
 * Applies the FX state in ioctl_param to the hardware
 * IOCTL Required buffer size: u16
 * BufferFormat:
 * 	fx_state:	Byte0:	Bit7: STEEL_Deck_B
 *				Bit6: STEEL_Deck_A
 *				Bit5 to 0: Don't care
 *			Byte1:	Bit7: STEEL_Lock
 *				Bit6: STEEL_Master
 *				Bit5 to 0: Don't care
 */
#define DJ_IOCTL_SET_FX_STATE			_IOW (MAJOR_NUM, 32, __u16)

/* DJ_IOCTL_GET_FX_STATE	
 * Returns the FX state from the hardware in ioctl_param
 * IOCTL Required buffer size: u16
 * BufferFormat:
 * 	fx_state:	Byte0:	Bit7: STEEL_Deck_B
 *				Bit6: STEEL_Deck_A
 *				Bit5 to 0: Don't care
 *			Byte1:	Bit7: STEEL_Lock
 *				Bit6: STEEL_Master
 *				Bit5 to 0: Don't care
 */
#define DJ_IOCTL_GET_FX_STATE			_IOR (MAJOR_NUM, 33, __u16)

/* For use only by firmware updater application */
/* DJ_IOCTL_DJBULK_GOTO_BOOT_MODE
 * Asks the device to depart, and then arrive in boot mode (must be in normal mode first).
 * Returns the device state for this session, so far as it is known,in ioctl_param.  
 *  If the opration is successful, then the device arrives in boot mode.
 *  Possible return values: (DJ_STEEL_IN_BOOT_MODE, DJ_STEEL_IN_NORMAL_MODE or DJ_STEEL_IN_UNKNOWN_MODE)
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_DJBULK_GOTO_BOOT_MODE	_IOR (MAJOR_NUM, 34, __u16)

/* For use only by firmware updater application */
/* DJ_IOCTL_DJBULK_EXIT_BOOT_MODE
 * Asks the device to depart, and then arrive in normal mode (must be in normal mode first).
 * Returns the device state for this session, so far as it is known,in ioctl_param.  
 *  If the opration is successful, then the device arrives in normal mode.
 *  Possible return values: (DJ_STEEL_IN_BOOT_MODE, DJ_STEEL_IN_NORMAL_MODE or DJ_STEEL_IN_UNKNOWN_MODE)
 * IOCTL Required buffer size: u16
 */
#define DJ_IOCTL_DJBULK_EXIT_BOOT_MODE	_IOR (MAJOR_NUM, 35, __u16)

/* DJ_IOCTL_GET_LOCATION_ID
 * Returns the USB location ID of the device in ioctl_param
 * IOCTL Required buffer size: buffer of size LOCATION_ID_LEN 
 */
#define DJ_IOCTL_GET_LOCATION_ID		_IOR (MAJOR_NUM, 36, char *)
#ifdef CONFIG_COMPAT
#define DJ_IOCTL_GET_LOCATION_ID32		_IOR (MAJOR_NUM, 36, compat_long_t)
#endif

/* DJ_IOCTL_DJBULK_SEND_BULK_WRITE
 * Applies a LED or force report update bulk write to a DJ Control Steel device.
 * IOCTL Required buffer size: bulk output buffer size (DJ_CONTROL_STEEL_BULK_TRANSFER_SIZE bytes).
 */
#define DJ_IOCTL_DJBULK_SEND_BULK_WRITE		_IOW (MAJOR_NUM, 37, char *)
#ifdef CONFIG_COMPAT
#define DJ_IOCTL_DJBULK_SEND_BULK_WRITE32		_IOW (MAJOR_NUM, 37, compat_long_t)
#endif

/* DJ_IOCTL_IS_DEVICE_ALIVE
 * Tests to see if the device is still present, and returns success or failure accordingly.
 * IOCTL Required buffer size: None.
 */
#define DJ_IOCTL_IS_DEVICE_ALIVE		_IO  (MAJOR_NUM, 38)

/* DJ_IOCTL_REGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS
 * Registers for netlink based device notifications.
 * IOCTL Required buffer size: unsigned long, holding a context to be returned in notifications.
 */
#define DJ_IOCTL_REGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS	_IOW (MAJOR_NUM, 39, unsigned long)
#ifdef CONFIG_COMPAT
#define DJ_IOCTL_REGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS32 _IOW (MAJOR_NUM, 39, compat_long_t)
#endif

/* DJ_IOCTL_UNREGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS
 * Unregisters for netlink based device notifications.
 * IOCTL Required buffer size: None.
 */
#define DJ_IOCTL_UNREGISTER_FOR_NETLINK_DEVICE_NOTIFICATIONS	_IO  (MAJOR_NUM, 40)

/* DJ_IOCTL_GET_CONTROL_DATA_INPUT_PACKET_SIZE
 * The driver returns the size of control data that read returns, which may allow clients to 
 *  allocate their read buffers ahead of time in a more efficient manner.
 * IOCTL Required buffer size: __u32.
 */
#define DJ_IOCTL_GET_CONTROL_DATA_INPUT_PACKET_SIZE		_IOR (MAJOR_NUM, 41, __u32)

/* DJ_IOCTL_GET_CONTROL_DATA_OUTPUT_PACKET_SIZE
 * The driver returns the size of control data that may be send to the device, which may allow clients to 
 *  allocate their buffers ahead of time in a more efficient manner.
 * IOCTL Required buffer size: __u32.
 */
#define DJ_IOCTL_GET_CONTROL_DATA_OUTPUT_PACKET_SIZE		_IOR (MAJOR_NUM, 42, __u32)

/* DJ_IOCTL_SET_OUTPUT_CONTROL_DATA
 * This IOCTL sets output control data to the device.  What this actually does is quite device 
 *  dependent.  For many devices, it will resolve to a HID set report operationl; for others, a 
 *  bulk out operation.  LEDs may be set, the DJ mouse may be enabled/disabled, etc.
 * IOCTL Required buffer size: A buffer twice the size returned from DJ_IOCTL_GET_CONTROL_DATA_OUTPUT_PACKET_SIZE.  
 *                             	is required; The first half of the buffer is the data, while the second half is 
 *				a bitmask, indicating which bits will be set in the driver's own cached buffer 
 *				before sending.  The driver's cached control data buffer may be queried via 
 *				DJ_IOCTL_GET_OUTPUT_CONTROL_DATA.
 */
#define DJ_IOCTL_SET_OUTPUT_CONTROL_DATA			_IOW (MAJOR_NUM, 43, char*)
#ifdef CONFIG_COMPAT
#define DJ_IOCTL_SET_OUTPUT_CONTROL_DATA32			_IOW (MAJOR_NUM, 43, compat_long_t)
#endif

/* DJ_IOCTL_GET_OUTPUT_CONTROL_DATA
 * This IOCTL returns the current cached control data buffer
 * IOCTL required buffer size: A buffer of size returned from DJ_IOCTL_GET_CONTROL_DATA_OUTPUT_PACKET_SIZE.
 */
#define DJ_IOCTL_GET_OUTPUT_CONTROL_DATA			_IOR (MAJOR_NUM, 44, char*)
#ifdef CONFIG_COMPAT
#define DJ_IOCTL_GET_OUTPUT_CONTROL_DATA32			_IOR (MAJOR_NUM, 44, compat_long_t)
#endif

/* DJ_IOCTL_ACQUIRE_NETLINK_UNIT
 * This IOCTL lets the client know which netlink unit is used for unicasting
 *  device property changes to listening clients
 * IOCTL required buffer size: int.  This is either: -1, which is
 *                             invalid, and therefore it was impossible to acquire a 
 *                             netlink unit, otherwise it is a valid unit number which
 *                             the client can use as the protocol parameter in a call 
 *                             to the socket system call) */
#define DJ_IOCTL_ACQUIRE_NETLINK_UNIT				_IOR (MAJOR_NUM, 45, int)

/* DJ_IOCTL_GET_DEVICE_CAPS
 * This IOCTL allows the client to get the device caps
 * IOCTL required buffer size: struct snd_hdj_caps (should be same 32 or 64 bit)
 */
#define DJ_IOCTL_GET_DEVICE_CAPS					_IOR (MAJOR_NUM, 46, struct snd_hdj_caps*)

#endif


