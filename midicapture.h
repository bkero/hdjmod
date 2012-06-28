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


#if !defined(_MIDICAPTURE_H_)
#define _MIDICAPTURE_H_

/*#define CAPTURE_DATA_PRINTK
#define CAPTURE_DUMP_PACKETS
#define CAPTURE_DUMP_URB_THROTTLE*/
#define CAPTURE_DUMP_URB_THROTTLE_LEVEL		20
void snd_hdjmidi_standard_input(struct snd_hdjmidi_in_endpoint* ep,
				 uint8_t* buffer, 
				int buffer_length);
void snd_hdjmp3_standard_input(struct snd_hdjmidi_in_endpoint* ep,
				uint8_t* buffer, 
				int buffer_length);
/* Here we have to buffer our HID report buffer up to 20 bytes, as we only receive up to 8
 *  bytes at a time, as the PSOC version is low speed
 */
void snd_hdjmp3_nonweltrend_input(struct snd_hdjmidi_in_endpoint* ep,
 	   			  uint8_t* buffer, 
		 		  int buffer_length);
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16) )
int snd_hdjmidi_input_open(struct snd_rawmidi_substream *substream);
int snd_hdjmidi_input_close(struct snd_rawmidi_substream *substream);
void snd_hdjmidi_input_trigger(struct snd_rawmidi_substream *substream, int up_param);
#else
int snd_hdjmidi_input_open(snd_rawmidi_substream_t *substream);
int snd_hdjmidi_input_close(snd_rawmidi_substream_t *substream);
void snd_hdjmidi_input_trigger(snd_rawmidi_substream_t *substream, int up_param);
#endif
/*
 * Processes the data read from the device.
 */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) )
void snd_hdjmidi_in_urb_complete(struct urb* urb);
#else
void snd_hdjmidi_in_urb_complete(struct urb* urb, struct pt_regs *junk);
#endif
int snd_hdjmidi_input_start_ep(struct snd_hdjmidi_in_endpoint* ep);
void snd_hdjmidi_input_kill_urbs(struct snd_hdjmidi_in_endpoint* ep);
#endif
