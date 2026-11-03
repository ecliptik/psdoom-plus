// i_audio_sdl.c - SDL3 audio output and small system helpers for og
//
// id's i_sound.c keeps its software mixer; it hands finished 16-bit stereo
// buffers to I_AudioWrite instead of write()ing them to /dev/dsp.
//
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>

#include <SDL3/SDL.h>

#include "i_platform.h"

static SDL_AudioStream *stream;

int I_AudioOpen(int rate)
{
	SDL_AudioSpec spec;

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		fprintf(stderr, "I_AudioOpen: %s (no sound)\n", SDL_GetError());
		return -1;
	}
	spec.format = SDL_AUDIO_S16LE;
	spec.channels = 2;
	spec.freq = rate;
	stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if (stream == NULL) {
		fprintf(stderr, "I_AudioOpen: %s (no sound)\n", SDL_GetError());
		return -1;
	}
	SDL_ResumeAudioStreamDevice(stream);
	return 0;
}

int I_AudioQueued(void)
{
	return stream ? SDL_GetAudioStreamQueued(stream) : 1 << 30;
}

void I_AudioWrite(const void *buf, int bytes)
{
	if (stream)
		SDL_PutAudioStreamData(stream, buf, bytes);
}

void I_AudioClose(void)
{
	if (stream)
		SDL_DestroyAudioStream(stream);
	stream = NULL;
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// TryRunTics used to spin at 100% CPU waiting for the next tic.
void I_Yield(void)
{
	SDL_Delay(1);
}
