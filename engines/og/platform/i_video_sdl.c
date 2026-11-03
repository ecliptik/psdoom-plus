// i_video_sdl.c - SDL3 video and input for linuxdoom-1.10
//
// Replaces id's X11 i_video.c. 320x200 8-bit screens[0] -> ARGB texture,
// shown at 4:3 with nearest-neighbour scaling.
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "doomstat.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"
#include "doomdef.h"

#include "i_platform.h"

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;
static Uint32        argb[256];
static boolean       grabmouse;
static int           mousebuttons;

void I_InitGraphics(void)
{
	static boolean done;
	int scale = 2;
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;

	if (done)
		return;
	done = true;

	if (M_CheckParm("-1")) scale = 1;
	if (M_CheckParm("-2")) scale = 2;
	if (M_CheckParm("-3")) scale = 3;
	if (M_CheckParm("-4")) scale = 4;
	if (M_CheckParm("-fullscreen"))
		flags |= SDL_WINDOW_FULLSCREEN;
	grabmouse = M_CheckParm("-grabmouse") != 0;

	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		I_Error("I_InitGraphics: %s", SDL_GetError());

	window = SDL_CreateWindow("psdoom-plus", SCREENWIDTH * scale,
	                          SCREENHEIGHT * scale * 6 / 5, flags);
	if (window == NULL)
		I_Error("I_InitGraphics: %s", SDL_GetError());
	renderer = SDL_CreateRenderer(window, NULL);
	if (renderer == NULL)
		I_Error("I_InitGraphics: %s", SDL_GetError());
	SDL_SetRenderVSync(renderer, 1);
	// Doom's 320x200 was shown on 4:3 screens: present it as 320x240
	SDL_SetRenderLogicalPresentation(renderer, SCREENWIDTH, SCREENHEIGHT * 6 / 5,
	                                 SDL_LOGICAL_PRESENTATION_LETTERBOX);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
	                            SDL_TEXTUREACCESS_STREAMING, SCREENWIDTH, SCREENHEIGHT);
	if (texture == NULL)
		I_Error("I_InitGraphics: %s", SDL_GetError());
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

	if (grabmouse)
		SDL_SetWindowRelativeMouseMode(window, true);
}

void I_ShutdownGraphics(void)
{
	if (texture)  SDL_DestroyTexture(texture);
	if (renderer) SDL_DestroyRenderer(renderer);
	if (window)   SDL_DestroyWindow(window);
	texture = NULL;
	renderer = NULL;
	window = NULL;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void I_SetPalette(byte *palette)
{
	int i;

	for (i = 0; i < 256; i++) {
		Uint32 r = gammatable[usegamma][*palette++];
		Uint32 g = gammatable[usegamma][*palette++];
		Uint32 b = gammatable[usegamma][*palette++];
		argb[i] = 0xff000000u | (r << 16) | (g << 8) | b;
	}
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
	void *pixels;
	int pitch, x, y;
	const byte *src = screens[0];

	if (texture == NULL)
		return;
	if (!SDL_LockTexture(texture, NULL, &pixels, &pitch))
		return;
	for (y = 0; y < SCREENHEIGHT; y++) {
		Uint32 *dst = (Uint32 *)((Uint8 *)pixels + y * pitch);
		for (x = 0; x < SCREENWIDTH; x++)
			dst[x] = argb[*src++];
	}
	SDL_UnlockTexture(texture);
	SDL_RenderClear(renderer);
	SDL_RenderTexture(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

void I_ReadScreen(byte *scr)
{
	memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_StartFrame(void)
{
}

static int translate_key(SDL_Keycode k)
{
	switch (k) {
	case SDLK_LEFT:      return KEY_LEFTARROW;
	case SDLK_RIGHT:     return KEY_RIGHTARROW;
	case SDLK_UP:        return KEY_UPARROW;
	case SDLK_DOWN:      return KEY_DOWNARROW;
	case SDLK_ESCAPE:    return KEY_ESCAPE;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:  return KEY_ENTER;
	case SDLK_TAB:       return KEY_TAB;
	case SDLK_F1:        return KEY_F1;
	case SDLK_F2:        return KEY_F2;
	case SDLK_F3:        return KEY_F3;
	case SDLK_F4:        return KEY_F4;
	case SDLK_F5:        return KEY_F5;
	case SDLK_F6:        return KEY_F6;
	case SDLK_F7:        return KEY_F7;
	case SDLK_F8:        return KEY_F8;
	case SDLK_F9:        return KEY_F9;
	case SDLK_F10:       return KEY_F10;
	case SDLK_F11:       return KEY_F11;
	case SDLK_F12:       return KEY_F12;
	case SDLK_BACKSPACE:
	case SDLK_DELETE:    return KEY_BACKSPACE;
	case SDLK_PAUSE:     return KEY_PAUSE;
	case SDLK_EQUALS:
	case SDLK_KP_PLUS:   return KEY_EQUALS;
	case SDLK_MINUS:
	case SDLK_KP_MINUS:  return KEY_MINUS;
	case SDLK_LSHIFT:
	case SDLK_RSHIFT:    return KEY_RSHIFT;
	case SDLK_LCTRL:
	case SDLK_RCTRL:     return KEY_RCTRL;
	case SDLK_LALT:
	case SDLK_RALT:
	case SDLK_LGUI:
	case SDLK_RGUI:      return KEY_RALT;
	default:
		if (k >= 32 && k < 127)
			return (int)k;   // SDL keycodes for printable keys are lowercase ASCII
		return 0;
	}
}

static int buttons_of(SDL_MouseButtonFlags b)
{
	return ((b & SDL_BUTTON_LMASK) ? 1 : 0) |
	       ((b & SDL_BUTTON_RMASK) ? 2 : 0) |
	       ((b & SDL_BUTTON_MMASK) ? 4 : 0);
}

void I_StartTic(void)
{
	SDL_Event e;
	event_t ev;

	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_EVENT_QUIT:
			I_Quit();
			break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			if (e.key.repeat)
				break;
			ev.type = e.type == SDL_EVENT_KEY_DOWN ? ev_keydown : ev_keyup;
			ev.data1 = translate_key(e.key.key);
			if (ev.data1 != 0)
				D_PostEvent(&ev);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			mousebuttons = buttons_of(SDL_GetMouseState(NULL, NULL));
			ev.type = ev_mouse;
			ev.data1 = mousebuttons;
			ev.data2 = ev.data3 = 0;
			D_PostEvent(&ev);
			break;
		case SDL_EVENT_MOUSE_MOTION:
			if (!grabmouse)
				break;
			ev.type = ev_mouse;
			ev.data1 = mousebuttons;
			ev.data2 = (int)(e.motion.xrel * 4);
			ev.data3 = (int)(-e.motion.yrel * 4);
			if (ev.data2 || ev.data3)
				D_PostEvent(&ev);
			break;
		default:
			break;
		}
	}
}

// Binary PPM of the current frame: no image library needed.
int I_WriteScreenshotPPM(const char *path)
{
	FILE *f = fopen(path, "wb");
	const byte *src = screens[0];
	int i;

	if (f == NULL)
		return -1;
	fprintf(f, "P6\n%d %d\n255\n", SCREENWIDTH, SCREENHEIGHT);
	for (i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++) {
		Uint32 c = argb[src[i]];
		fputc((int)((c >> 16) & 0xff), f);
		fputc((int)((c >> 8) & 0xff), f);
		fputc((int)(c & 0xff), f);
	}
	return fclose(f) == 0 ? 0 : -1;
}
