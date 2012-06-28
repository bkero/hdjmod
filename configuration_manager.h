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
#if !defined(_CONFIGURATION_MANAGER_H_)
#define _CONFIGURATION_MANAGER_H_

/* attempts to clear all LEDs based on product type */
int clear_leds(struct snd_hdj_chip* chip);

int get_output_control_data_len(struct snd_hdj_chip* chip, u32* len);
int get_input_control_data_len(struct snd_hdj_chip* chip, u32* len);
int send_control_output_report(struct snd_hdj_chip* chip, 
								u8 *masked_buffer,
								u32 buffer_len);
int get_control_output_report(struct snd_hdj_chip* chip, 
								u8 __user *buffer,
								u32 buffer_len);

/* supports Rmx and Steel */
int get_jogwheel_lock_status(struct usb_hdjbulk *ubulk, u16 * lock_state, u8 query_hardware, u8 native);
/* supports Rmx and Steel */
int set_jogwheel_lock_status(struct usb_hdjbulk *ubulk, u16 lock_state);

/* supports Rmx and Steel */
int get_jogwheel_sensitivity(struct usb_hdjbulk *ubulk, u16 * jogwheel_sensitivity, u8 query_hardware, u8 native);
/* supports Rmx and Steel */
int set_jogwheel_sensitivity(struct usb_hdjbulk *ubulk, u16 jogwheel_sensitivity);

int set_midi_channel(struct snd_hdj_chip* chip, u16* channel);
int get_midi_channel(struct snd_hdj_chip* chip, u16* channel);

int __set_midi_channel(int chip_index, u16 channel);
int __get_midi_channel(int chip_index, u16* channel);

int get_talkover_state(struct usb_hdjbulk *ubulk,u16 * talkover_att, u8 query_hardware);
int get_talkover_att(struct usb_hdjbulk *ubulk,u16 * talkover_att, u8 query_hardware);
int set_talkover_att(struct usb_hdjbulk *ubulk, u16 talkover_att);
int activate_talkover(struct usb_hdjbulk *ubulk);
int set_talkover_enable(struct usb_hdjbulk *ubulk, u8 enable);
int get_talkover_enable(struct usb_hdjbulk *ubulk, u8 *enable);

int get_firmware_version(struct snd_hdj_chip* chip, 
							u16 * firmware_version, 
							u8 query_hardware);

int get_djconsole_device_config(int chip_index, u16 * device_config, u8 include_to);
int set_djconsole_device_config(int chip_index, u32 device_config, u8 include_to);

int get_audio_config(struct usb_hdjbulk *ubulk, u16 * audio_config, u8 query_hardware);
int set_audio_config(struct usb_hdjbulk *ubulk, u16 audio_config);

int set_mouse_state(struct snd_hdj_chip* chip, u8 enable_mouse);
int get_mouse_state(struct snd_hdj_chip* chip, u16 * mouse_enabled);

int get_sample_rate(struct usb_hdjbulk *ubulk, u16 * sample_rate, u8 query_hardware);
int set_sample_rate(struct usb_hdjbulk *ubulk, u16 sample_rate);

int get_crossfader_lock(struct usb_hdjbulk *ubulk, u16 * cross_fader_lock, u8 query_hardware);
int set_crossfader_lock(struct usb_hdjbulk *ubulk, u16 cross_fader_lock);

int get_crossfader_style(struct usb_hdjbulk *ubulk, u16 * crossfader_style);
int set_crossfader_style(struct usb_hdjbulk *ubulk, u16 crossfader_style);

int set_alternate_setting(struct usb_hdjbulk *ubulk, int alt_setting);

int get_serial_number(struct usb_hdjbulk *ubulk, u32* serial_number);
int set_serial_number(struct usb_hdjbulk *ubulk, u32 serial_number);

/*
 *set the mode shift state
 *mode_shift_state:	Bit7: Mstate_DA
 *			Bit6: Mstate_DB
 *			Bit 5 to 0: Don't care
 */
int set_mode_shift_state(struct usb_hdjbulk * ubulk, u8 mode_shift_state);

/*
 *get the mode shift state
 *mode_shift_state:	Bit7: Mstate_DA
 *			Bit6: Mstate_DB
 *			Bit 5 to 0: Don't care
 */
int get_mode_shift_state(struct usb_hdjbulk * ubulk, u8* mode_shift_state);

/*
 *set the FX state
 *fx_state:	Byte0:	Bit7: STEEL_Deck_B
 *			Bit6: STEEL_Deck_A
 *			Bit5 to 0: Don't care
 *		Byte1:	Bit7: STEEL_Lock
 *			Bit6: STEEL_Master
 *			Bit5 to 0: Don't care
 */
int set_fx_state(struct usb_hdjbulk * ubulk, u16 fx_state);

/*
 *get the FX state
 *fx_state:	Byte0:	Bit7: STEEL_Deck_B
 *			Bit6: STEEL_Deck_A
 *			Bit5 to 0: Don't care
 *		Byte1:	Bit7: STEEL_Lock
 *			Bit6: STEEL_Master
 *			Bit5 to 0: Don't care
 */
int get_fx_state(struct usb_hdjbulk * ubulk, u16* fx_state);

int reboot_djcontrolsteel_to_boot_mode(struct usb_hdjbulk *ubulk);
int reboot_djcontrolsteel_to_normal_mode(struct usb_hdjbulk *ubulk);
#endif
