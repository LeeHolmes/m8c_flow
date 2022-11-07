#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>

#include "cst_string.h"
#include "cst_wave.h"
#include "cst_audio.h"

#include <portaudio.h>

int initialized = 0;
PaStreamParameters outputParameters;
PaStream* stream = NULL;

cst_audiodev *audio_open_portaudio(unsigned int sps, int channels, cst_audiofmt fmt)
{
    // Initialize PortAudio if not yet initialized
    if(! initialized)
    {
        Pa_Initialize();

        outputParameters.device = Pa_GetDefaultOutputDevice();
        if (outputParameters.device == paNoDevice){
            fprintf(stderr,"Error: Could not find output device.\n");
            return NULL;
        }

        outputParameters.channelCount = channels;
        outputParameters.sampleFormat = paInt16;
        outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
        outputParameters.hostApiSpecificStreamInfo = NULL;
        initialized = 1;
    }

    // Stop any ongoing streams
	if(stream != NULL)
	{
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
		stream = NULL;
	}

    /* Write hardware parameters to flite audio device data structure */
    cst_audiodev *ad = cst_alloc(cst_audiodev, 1);
    assert(ad != NULL);

    ad->real_sps = ad->sps = sps;
    ad->real_channels = ad->channels = channels;
    ad->real_fmt = ad->fmt = fmt;
    ad->platform_data = NULL;

    return ad;
}

int audio_close_portaudio(cst_audiodev *ad)
{
    cst_free(ad);

	if(stream != NULL)
	{
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
		stream = NULL;
	}

    return 1;
}

int audio_write_portaudio(cst_audiodev *ad, void *samples, int num_bytes)
{
    size_t frame_size  = audio_bps(ad->real_fmt) * ad->real_channels;
    ssize_t num_frames = num_bytes / frame_size;
    printf("Num frames: %ld\n", num_frames);
    
    PaError result = Pa_OpenStream(&stream, NULL, &outputParameters, ad->sps, num_frames, paClipOff, NULL, NULL);
    if(result != paNoError)
    {
        fprintf(stderr,"Here2.\n");
        printf("Could not initialize stream: %s\n", Pa_GetErrorText(result));
    }

    result = Pa_StartStream(stream);
    if(result != paNoError)
    {
        fprintf(stderr,"Here5.\n");
        printf("Could not start stream: %s\n", Pa_GetErrorText(result));
    }

    result = Pa_WriteStream(stream, samples, num_frames);
    if(result != paNoError)
    {
        fprintf(stderr,"Here7.\n");
        printf("Could not output audio: %s\n", Pa_GetErrorText(result));
    }

    Pa_StopStream(stream);
    Pa_CloseStream(stream);

    return num_bytes;
}

int audio_flush_portaudio(cst_audiodev *ad)
{
    return 1;
}

int audio_drain_portaudio(cst_audiodev *ad)
{
    return 1;
}
