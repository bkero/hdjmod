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


#if !defined(_MIDIRENDER_H_)
#define _MIDIRENDER_H_

//#define RENDER_DATA_PRINTK
//#define RENDER_DUMP_PACKETS
//#define RENDER_DUMP_URB_THROTTLE
//#define RENDER_DUMP_URB_THROTTLE_LEVEL		20
//#define RENDER_POLL_STATE_PRINTK
/* This is recommended, because the mp3 is actually HID, with a 8ms period- so
 *  therefore it is highly recommended not to exceed this.  Our other devices are
 *  MIDI, and NAK us if we exceed the MIDI rate */
#define THROTTLE_MP3_RENDER

// TODO tune polling period
#define POLL_PERIOD_MS		8
#define POLL_VERSION		0x240

void snd_hdjmidi_standard_output(struct snd_hdjmidi_out_endpoint* ep);
void snd_hdjmidi_output_standard_packet(struct urb* urb, 
					uint8_t b0, 
					uint8_t b1, 
					uint8_t b2,
					uint8_t len);
void snd_hdjmp3_output_standard_packet(struct urb* urb, 
					uint8_t b0, 
					uint8_t b1, 
					uint8_t b2,
					uint8_t len);

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void hid_ctrl_complete(struct urb* urb);
#else
void hid_ctrl_complete(struct urb* urb, struct pt_regs *junk);
#endif

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
int snd_hdjmidi_output_open(struct snd_rawmidi_substream *substream);
int snd_hdjmidi_output_close(struct snd_rawmidi_substream *substream);
void snd_hdjmidi_output_trigger(struct snd_rawmidi_substream *substream, int up_param);
#else
int snd_hdjmidi_output_open(snd_rawmidi_substream_t *substream);
int snd_hdjmidi_output_close(snd_rawmidi_substream_t *substream);
void snd_hdjmidi_output_trigger(snd_rawmidi_substream_t *substream, int up_param);
#endif

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void snd_hdjmidi_out_urb_complete(struct urb* urb);
#else
void snd_hdjmidi_out_urb_complete(struct urb* urb, struct pt_regs *junk);
#endif

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void hid_ctrl_complete_kt(struct urb* urb);
#else
void hid_ctrl_complete_kt(struct urb* urb, struct pt_regs *junk);
#endif

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void midi_led_clear_complete(struct urb* urb);
#else
void midi_led_clear_complete(struct urb* urb, struct pt_regs *junk);
#endif

u8 mp3w_check_led_state(struct snd_hdjmidi_out_endpoint* ep, u8 called_from_kthread);
#ifdef THROTTLE_MP3_RENDER
void midi_render_throttle_timer(unsigned long data);
#endif
void snd_hdjmidi_do_output(struct snd_hdjmidi_out_endpoint* ep);
void snd_hdjmidi_out_tasklet(unsigned long data);
void snd_hdjmidi_output_kill_tasklet(struct snd_hdjmidi_out_endpoint* ep);
void snd_hdjmidi_output_kill_urbs(struct snd_hdjmidi_out_endpoint* ep);
void snd_hdjmidi_output_initialize_tasklet(struct snd_hdjmidi_out_endpoint* ep);

int mp3w_kthread_entry(void *arg);
#endif
