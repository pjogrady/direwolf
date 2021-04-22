
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2013, 2014, 2019  John Langner, WB2OSZ
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "direwolf.h"

#include <stdio.h>

#include "hdlc_send.h"
#include "audio.h"
#include "gen_tone.h"
#include "textcolor.h"
#include "fcs_calc.h"
#include "fx25.h"

static void send_control (int, int);
static void send_data (int, int);
static void send_bit (int, int);



static int number_of_bits_sent[MAX_CHANS];		// Count number of bits sent by "hdlc_send_frame" or "hdlc_send_flags"





/*-------------------------------------------------------------
 *
 * Name:	hdlc_send
 *
 * Purpose:	Convert HDLC frames to a stream of bits.
 *
 * Inputs:	chan	- Audio channel number, 0 = first.
 *
 *		fbuf	- Frame buffer address.
 *
 *		flen	- Frame length, not including the FCS.
 *
 *		bad_fcs	- Append an invalid FCS for testing purposes.
 *			  Applies only to regular AX.25.
 *
 *		fx25_xmit_enable - Just like the name says.
 *
 * Outputs:	Bits are shipped out by calling tone_gen_put_bit().
 *
 * Returns:	Number of bits sent including "flags" and the
 *		stuffing bits.  
 *		The required time can be calculated by dividing this
 *		number by the transmit rate of bits/sec.
 *
 * Description:	Convert to stream of bits including:
 *			start flag
 *			bit stuffed data
 *			calculated FCS
 *			end flag
 *		NRZI encoding
 *
 * 
 * Assumptions:	It is assumed that the tone_gen module has been
 *		properly initialized so that bits sent with 
 *		tone_gen_put_bit() are processed correctly.
 *
 *--------------------------------------------------------------*/

static int ax25_only_hdlc_send_frame (int chan, unsigned char *fbuf, int flen, int bad_fcs);

// New in 1.6: Option to encapsulate in FX.25.

int hdlc_send_frame (int chan, unsigned char *fbuf, int flen, int bad_fcs, int fx25_xmit_enable)
{
	if (fx25_xmit_enable) {
	  int n = fx25_send_frame (chan, fbuf, flen, fx25_xmit_enable);
	  if (n > 0) {
	    return (n);
	  }
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Unable to send FX.25.  Falling back to regular AX.25.\n");
	}

	return (ax25_only_hdlc_send_frame (chan, fbuf, flen, bad_fcs));
}


static int ax25_only_hdlc_send_frame (int chan, unsigned char *fbuf, int flen, int bad_fcs)
{
	int j, fcs;
	

	number_of_bits_sent[chan] = 0;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("hdlc_send_frame ( chan = %d, fbuf = %p, flen = %d, bad_fcs = %d)\n", chan, fbuf, flen, bad_fcs);
	fflush (stdout);
#endif


	send_control (chan, 0x7e);	/* Start frame */
	
	for (j=0; j<flen; j++) {
	  send_data (chan, fbuf[j]);
	}

	fcs = fcs_calc (fbuf, flen);

	if (bad_fcs) {
	  /* For testing only - Simulate a frame getting corrupted along the way. */
	  send_data (chan, (~fcs) & 0xff);
	  send_data (chan, ((~fcs) >> 8) & 0xff);
	}
	else {
	  send_data (chan, fcs & 0xff);
	  send_data (chan, (fcs >> 8) & 0xff);
	}

	send_control (chan, 0x7e);	/* End frame */

	return (number_of_bits_sent[chan]);
}


/*-------------------------------------------------------------
 *
 * Name:	hdlc_send_flags
 *
 * Purpose:	Send HDLC flags before and after the frame.
 *
 * Inputs:	chan	- Audio channel number, 0 = first.
 *
 *		nflags	- Number of flag patterns to send.
 *
 *		finish	- True for end of transmission.
 *			  This causes the last audio buffer to be flushed.
 *
 * Outputs:	Bits are shipped out by calling tone_gen_put_bit().
 *
 * Returns:	Number of bits sent.  
 *		There is no bit-stuffing so we would expect this to
 *		be 8 * nflags.
 *		The required time can be calculated by dividing this
 *		number by the transmit rate of bits/sec.
 *
 * Assumptions:	It is assumed that the tone_gen module has been
 *		properly initialized so that bits sent with 
 *		tone_gen_put_bit() are processed correctly.
 *
 *--------------------------------------------------------------*/

int hdlc_send_flags (int chan, int nflags, int finish)
{
	int j;
	

	number_of_bits_sent[chan] = 0;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("hdlc_send_flags ( chan = %d, nflags = %d, finish = %d )\n", chan, nflags, finish);
	fflush (stdout);
#endif

	/* The AX.25 spec states that when the transmitter is on but not sending data */
	/* it should send a continuous stream of "flags." */

	for (j=0; j<nflags; j++) {
	  send_control (chan, 0x7e);
	}

/* Push out the final partial buffer! */

	if (finish) {
	  audio_flush(ACHAN2ADEV(chan));
	}

	return (number_of_bits_sent[chan]);
}



static int stuff[MAX_CHANS];		// Count number of "1" bits to keep track of when we
					// need to break up a long run by "bit stuffing."
					// Needs to be array because we could be transmitting
					// on multiple channels at the same time.

static void send_control (int chan, int x) 
{
	int i;

	for (i=0; i<8; i++) {
	  send_bit (chan, x & 1);
	  x >>= 1;
	}
	
	stuff[chan] = 0;
}

static void send_data (int chan, int x) 
{
	int i;

	for (i=0; i<8; i++) {
	  send_bit (chan, x & 1);
	  if (x & 1) {
	    stuff[chan]++;
	    if (stuff[chan] == 5) {
	      send_bit (chan, 0);
	      stuff[chan] = 0;
	    }
	  } else {
	    stuff[chan] = 0;
          }
	  x >>= 1;
	}
}

/*
 * NRZI encoding.
 * data 1 bit -> no change.
 * data 0 bit -> invert signal.
 */

static void send_bit (int chan, int b)
{
	static int output[MAX_CHANS];

	if (b == 0) {
	  output[chan] = ! output[chan];
	}

	tone_gen_put_bit (chan, output[chan]);

	number_of_bits_sent[chan]++;
}

/* end hdlc_send.c */