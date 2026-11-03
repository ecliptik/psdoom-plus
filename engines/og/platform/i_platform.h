// i_platform.h - what og's platform layer adds to linuxdoom's i_* interfaces
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef I_PLATFORM_H
#define I_PLATFORM_H

int  I_AudioOpen(int rate);          // 16-bit stereo; -1 = no sound
int  I_AudioQueued(void);            // bytes waiting to play
void I_AudioWrite(const void *buf, int bytes);
void I_AudioClose(void);

void I_Yield(void);                  // give up the CPU briefly
int  I_WriteScreenshotPPM(const char *path);

#endif
