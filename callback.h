/*
*
*  Copyright (c) 2008  Guillemot Corporation S.A.
*
*  Philip Lukidis plukidis@guillemot.com
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
#if !defined(_HDJCALLBACK_H_)
#define _HDJCALLBACK_H_

/****************************************************************************************/
/*	Messages sent over netlink to usermode clients					*/
/****************************************************************************************/

/* This message is associated with a control change, and it set over netlink with 
 *  struct struct netlink_msg_header as a header, and struct control_change_data
 *  as data.
 */
#define MSG_CONTROL_CHANGE				1

/* for tests only */
#define MSG_TEST_STR					0xdbdb

/****************************************************************************************/
/*	CONTROL IDs sent in control change messages to usermode clients (over netlink)	*/
/****************************************************************************************/

/* control change associated with jog wheel locking/unlocking */
#define CTRL_CHG_JOG_WHEEL_LOCK				1

/* control change associated with jog wheel sensitivity */
#define CTRL_CHG_JOG_WHEEL_SENS				2

/* control change associated with MIDI channel */
#define CTRL_CHG_MIDI_CHANNEL				3

/* control change associated with talkover attenuation */
#define CTRL_CHG_TALKOVER_ATTEN				4

/* control change associated with talkover enable */
#define CTRL_CHG_TALKOVER_ENABLE			5

/* control change associated with DJ Console 1 Device Config Word */
#define CTRL_CHG_DJ1_DEVICE_CONFIG			6

/* control change associated with DJ Console Mk2/Rmx audio config */
#define CTRL_CHG_AUDIO_CONFIG				7

/* control change associated with DJ Console and DJ Console Mk2 DJ Mouse enable status */
#define CTRL_CHG_DJ_MOUSE_ENABLE			8

/* control change associated sample rate change */
#define CTRL_CHG_SAMPLE_RATE				9

/* control change associated crossfader lock */
#define CTRL_CHG_XFADER_LOCK				10

/* control change associated crossfader style */
#define CTRL_CHG_XFADER_STYLE				11

/* control change associated shift mode state */
#define CTRL_CHG_SHIFT_MODE_STATE			12

/* control change associated FX STATE */
#define CTRL_CHG_FX_STATE					13

/* control change associated with an output_control apply */
#define CTRL_CHG_OUTPUT_CONTROL				14

#endif
