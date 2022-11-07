/*************************************************************************/
/*                                                                       */
/*                  Language Technologies Institute                      */
/*                     Carnegie Mellon University                        */
/*                        Copyright (c) 2000                             */
/*                        All Rights Reserved.                           */
/*                                                                       */
/*  Permission is hereby granted, free of charge, to use and distribute  */
/*  this software and its documentation without restriction, including   */
/*  without limitation the rights to use, copy, modify, merge, publish,  */
/*  distribute, sublicense, and/or sell copies of this work, and to      */
/*  permit persons to whom this work is furnished to do so, subject to   */
/*  the following conditions:                                            */
/*   1. The code must retain the above copyright notice, this list of    */
/*      conditions and the following disclaimer.                         */
/*   2. Any modifications must be clearly marked as such.                */
/*   3. Original authors' names are not deleted.                         */
/*   4. The authors' names are not used to endorse or promote products   */
/*      derived from this software without specific prior written        */
/*      permission.                                                      */
/*                                                                       */
/*  CARNEGIE MELLON UNIVERSITY AND THE CONTRIBUTORS TO THIS WORK         */
/*  DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING      */
/*  ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT   */
/*  SHALL CARNEGIE MELLON UNIVERSITY NOR THE CONTRIBUTORS BE LIABLE      */
/*  FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES    */
/*  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN   */
/*  AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,          */
/*  ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF       */
/*  THIS SOFTWARE.                                                       */
/*                                                                       */
/*************************************************************************/
/*             Author:  Alan W Black (awb@cs.cmu.edu)                    */
/*               Date:  October 2000                                     */
/*************************************************************************/
/*                                                                       */
/*  Access to audio devices                                   ,          */
/*                                                                       */
/*************************************************************************/
#include "cst_string.h"
#include "cst_wave.h"
#include "cst_audio.h"
#include "native_audio.h"
#include "portaudio.h"

int audio_bps(cst_audiofmt fmt)
{
    switch (fmt)
    {
    case CST_AUDIO_LINEAR16:
	return 2;
    case CST_AUDIO_LINEAR8:
    case CST_AUDIO_MULAW:
	return 1;
    }
    return 0;
}

cst_audiodev *audio_open(int sps, int channels, cst_audiofmt fmt)
{
    cst_audiodev *ad;
    int up, down;

    ad = AUDIO_OPEN_NATIVE(sps, channels, fmt);
    if (ad == NULL)
	return NULL;

    down = sps / 1000;
    up = ad->real_sps / 1000;

    if (up != down)
	ad->rateconv = new_rateconv(up, down, channels);

    return ad;
}

int audio_close(cst_audiodev *ad)
{
    if (ad->rateconv)
	delete_rateconv(ad->rateconv);

    return AUDIO_CLOSE_NATIVE(ad);
}

int audio_write(cst_audiodev *ad,void *buff,int num_bytes)
{
    void *abuf = buff, *nbuf = NULL;
    int rv, i, real_num_bytes = num_bytes;

    if (ad->rateconv)
    {
	short *in, *out;
	int insize, outsize, n;

	insize = real_num_bytes / 2;
	in = (short *)buff;

	outsize = ad->rateconv->outsize;
	nbuf = out = cst_alloc(short, outsize);
	real_num_bytes = outsize * 2;

	while ((n = cst_rateconv_in(ad->rateconv, in, insize)) > 0)
	{
	    in += n; 
	    insize -= n;
	    while ((n = cst_rateconv_out(ad->rateconv, out, outsize)) > 0)
	    {
		out += n; 
		outsize -= n;
	    }
	}
	real_num_bytes -= outsize * 2;
	if (abuf != buff)
	    cst_free(abuf);
	abuf = nbuf;
    }
    if (ad->real_channels != ad->channels)
    {
	/* Yeah, we only do mono->stereo for now */
	if (ad->real_channels != 2 || ad->channels != 1)
	{
	    cst_errmsg("audio_write: unsupported channel mapping requested (%d => %d).\n",
		       ad->channels, ad->real_channels);
	}
	nbuf = cst_alloc(char, real_num_bytes * ad->real_channels / ad->channels);

	if (audio_bps(ad->fmt) == 2)
	{
	    for (i = 0; i < real_num_bytes / 2; ++i)
	    {
		((short *)nbuf)[i*2] = ((short *)abuf)[i];
		((short *)nbuf)[i*2+1] = ((short *)abuf)[i];
	    }
	}
	else if (audio_bps(ad->fmt) == 1)
	{
	    for (i = 0; i < real_num_bytes / 2; ++i)
	    {
		((unsigned char *)nbuf)[i*2] = ((unsigned char *)abuf)[i];
		((unsigned char *)nbuf)[i*2+1] = ((unsigned char *)abuf)[i];
	    }
	}
	else
	{
	    cst_errmsg("audio_write: unknown format %d\n", ad->fmt);
	    cst_free(nbuf);
	    if (abuf != buff)
		cst_free(abuf);
	    cst_error();
	}

	if (abuf != buff)
	    cst_free(abuf);
	abuf = nbuf;
	real_num_bytes = real_num_bytes * ad->real_channels / ad->channels;
    }
    if (ad->real_fmt != ad->fmt)
    {
	if (ad->real_fmt == CST_AUDIO_LINEAR16
	    && ad->fmt == CST_AUDIO_MULAW)
	{
	    nbuf = cst_alloc(char, real_num_bytes * 2);
	    for (i = 0; i < real_num_bytes; ++i)
		((short *)nbuf)[i] = cst_ulaw_to_short(((unsigned char *)abuf)[i]);
	    real_num_bytes *= 2;
	}
	else if (ad->real_fmt == CST_AUDIO_MULAW
		 && ad->fmt == CST_AUDIO_LINEAR16)
	{
	    nbuf = cst_alloc(char, real_num_bytes / 2);
	    for (i = 0; i < real_num_bytes / 2; ++i)
		((unsigned char *)nbuf)[i] = cst_short_to_ulaw(((short *)abuf)[i]);
	    real_num_bytes /= 2;
	}
	else if (ad->real_fmt == CST_AUDIO_LINEAR8
		 && ad->fmt == CST_AUDIO_LINEAR16)
	{
	    nbuf = cst_alloc(char, real_num_bytes / 2);
	    for (i = 0; i < real_num_bytes / 2; ++i)
		((unsigned char *)nbuf)[i] = (((short *)abuf)[i] >> 8) + 128;
	    real_num_bytes /= 2;
	}
	else
	{
	    cst_errmsg("audio_write: unknown format conversion (%d => %d) requested.\n",
		       ad->fmt, ad->real_fmt);
	    cst_free(nbuf);
	    if (abuf != buff)
		cst_free(abuf);
	    cst_error();
	}
	if (abuf != buff)
	    cst_free(abuf);
	abuf = nbuf;
    }
    if (ad->byteswap && audio_bps(ad->real_fmt) == 2)
	swap_bytes_short((short *)abuf, real_num_bytes/2);

    if (real_num_bytes)
	rv = AUDIO_WRITE_NATIVE(ad,abuf,real_num_bytes);
    else
	rv = 0;

    if (abuf != buff)
	cst_free(abuf);

    /* Callers expect to get the same num_bytes back as they passed
       in.  Funny, that ... */
    return (rv == real_num_bytes) ? num_bytes : 0;
}

int audio_drain(cst_audiodev *ad)
{
    return AUDIO_DRAIN_NATIVE(ad);
}

int audio_flush(cst_audiodev *ad)
{
    return AUDIO_FLUSH_NATIVE(ad);
}

/**
 * Audio play callback.
 * 
 * Follows the PaStreamCallback signature, wherein:
 * 
 * @param input   and
 * @param output  are either arrays of interleaved samples or; if
 *                non-interleaved samples were requested using the
 *                paNonInterleaved sample format flag, an array of buffer
 *                pointers, one non-interleaved buffer for each channel.
 * @param frameCount    The number of sample frames to be processed by the
 *                      stream callback.
 * @param timeInfo      Timestamps indicating the ADC capture time of the first
 *                      sample in the input buffer, the DAC output time of the
 *                      first sample in the output buffer and the time the
 *                      callback was invoked. See PaStreamCallbackTimeInfo and
 *                      Pa_GetStreamTime()
 * @param statusFlags   Flags indicating whether input and/or output buffers
 *                      have been inserted or will be dropped to overcome
 *                      underflow or overflow conditions.
 * @param userData      The value of a user supplied pointer passed to
 *                      Pa_OpenStream() intended for storing synthesis data
 *                      etc.
 */

static int  playCallback(const void*                     inputBuffer,
                         void*                           outputBuffer,
                         unsigned long                   framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags           statusFlags,
                         void*                           userData){
    (void) inputBuffer; /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;


    /**
     * Compute current processing state.
     */

    cst_wave*    data;
    short*       rptr;
    short*       wptr;
    unsigned int framesLeft, /* Number of frames of data remaining within the stream ***as a whole*** */
                 frames,     /* Number of frames of data to be written for this buffer. */
                 framesPad,  /* Number of frames of padding required within the final buffer. */
                 samples,    /* Number of samples of data to be written for this buffer. */
                 samplesPad, /* Number of samples of padding required within the final buffer. */
                 numBytes,   /* Number of bytes of data to be written for this buffer. */
                 numBytesPad;/* Number of bytes of padding required within the final buffer. */
    int          finalBuffer;/* Stores whether or not this is the final buffer. */


    data         = (cst_wave*)userData;
    rptr         = &data->samples[cst_wave_frameIndex  (data) *
                                  cst_wave_num_channels(data)];
    wptr         = (short*)outputBuffer;
    framesLeft   = cst_wave_maxFrameIndex(data) - cst_wave_frameIndex(data);
    finalBuffer  = framesLeft      <= framesPerBuffer;
    frames       = finalBuffer     ?  framesLeft     : framesPerBuffer;
    framesPad    = framesPerBuffer -  frames;
    samples      = frames     * cst_wave_num_channels(data);
    samplesPad   = framesPad  * cst_wave_num_channels(data);
    numBytes     = samples    * sizeof(short);
    numBytesPad  = samplesPad * sizeof(short);

    /**
     * Output data. We handle the final buffer specially, padding it with zeros.
     */

    memcpy(wptr, rptr, numBytes);
    wptr += samples;
    rptr += samples;
    cst_wave_set_frameIndex(data, cst_wave_frameIndex(data) + frames);
    memset(wptr, 0, numBytesPad);
    wptr += samplesPad;
    rptr += samplesPad;


    /**
     * Return a completion or continue code depending on whether this was the
     * final buffer or not respectively.
     */

    return finalBuffer ? paComplete : paContinue;
}

/**
 * Play wave function.
 * 
 * Plays the given cst_wave data as audio, blocking until this is done.
 */

int play_wave(cst_wave *w){
    PaStream*          stream;
    PaStreamParameters outputParameters;
    int                err;

    /**
     * Initialize custom fields in cst_wave struct.
     */

    cst_wave_set_frameIndex(w, 0);
    cst_wave_set_maxFrameIndex(w, (cst_wave_num_samples(w)));
    // / cst_wave_sample_rate(w)  * cst_wave_num_channels(w) * sizeof(short)


    /**
     * Initialize Port Audio device and stream parameters.
     */

    err = Pa_Initialize();
    outputParameters.device = Pa_GetDefaultOutputDevice();
    if (outputParameters.device == paNoDevice){
        fprintf(stderr,"Error: No default output device.\n");
        return -5;
    }

    outputParameters.channelCount = cst_wave_num_channels(w);
    outputParameters.sampleFormat = paInt16;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;


    /**
     * Open the stream for playback.
     */

    err = Pa_OpenStream(&stream,
                        NULL, /* no input */
                        &outputParameters,
                        cst_wave_sample_rate(w),
                        512,
                        paClipOff,
                        playCallback,
                        w);

    if(stream){
        /**
         * Start the stream.
         */

        err = Pa_StartStream(stream);
        if(err != paNoError){
            goto done;
        }

        /**
         * Block while it plays.
         */

        while((err = Pa_IsStreamActive(stream)) == 1){
            Pa_Sleep(100);
        }
        if(err < 0){
            goto done;
        }


        /**
         * Stop and close the stream. Both are necessary.
         */

        Pa_StopStream(stream);
        err = Pa_CloseStream(stream);
        if(err != paNoError){
            goto done;
        }
    }

    /**
     * Terminate and leave.
     */
done:
    Pa_Terminate();
    return 0;
}