/*
 * audtimer.c -- witness for which audio IRQ timer the SDL3-DOS Sound
 * Blaster backend selected (SDL/0039 auto-detect + the SDL/0138 killswitch).
 *
 * Opens the default SDL3-DOS playback device -- DOSSOUNDBLASTER_OpenDevice
 * -> SDL_DOSAudioInitTimer, the same path every SDL-audio port takes --
 * holds it for 500 ms of audio IRQs, and reports the backend's own
 * g_dos_audio_timer_mode / g_dos_audio_timer_tsc_mhz. Every line is also
 * appended to AUDTIMER.LOG with fflush+fsync before the next step runs, so
 * if the machine wedges the last line in that file says how far it got.
 * The backend's own banner ("audio IRQ timer: ...") lands in
 * LOGS\<tag>SDL.LOG per DOS_PORT_LOG_TAG (LOGS\sdldbg.log if unset).
 *
 * Why it exists: RDTSC from a real-mode (V86) program hard-hangs the g2k
 * Pentium OverDrive 83 under EMM386 (2026-09-03), and the DPMI-client
 * context this backend runs in has only been observed not to hang (the
 * 2026-08-13 doskutsu r2-POD83 matrix). SDL/0138's
 * SDL_HINT_DOS_AUDIO_TIMER_RDTSC=0 forces the PIT path; this probe is the
 * real-hardware A/B for it. See docs/timing.md (RDTSC section) in the hub.
 *
 * Steps (AUDTIMER.BAT runs both, safe one first):
 *   SDL_HINT_DOS_AUDIO_TIMER_RDTSC=0 DOS_PORT_LOG_TAG=KS0  -> expect
 *     timer_mode=1 and "8253 PIT counter 0 (RDTSC disabled via ...)";
 *     this step never executes RDTSC.
 *   hint unset, DOS_PORT_LOG_TAG=DEF -> executes RDTSC from the DPMI
 *     client (10 ms calibration spin, then once per audio IRQ); expect
 *     timer_mode=2 and "RDTSC (Pentium-class detected, N cycles/us ...)"
 *     on a TSC-bearing CPU, timer_mode=1 "486-class fallback" otherwise.
 *
 * Build (SDL3-linked, like audbuf.c): link against the port's sysroot
 * libSDL3.a plus probe_sdl_stubs.c, -lm, then stubedit minstack=2048k on
 * the file you actually stage (see shared/agents/probe-engineer.md).
 * Needs BLASTER set and CWSDPMI.EXE alongside. No engine symbols beyond
 * the stubs TU. Exit codes: 0 ok, 1 SDL_Init failed, 2 device open failed.
 *
 * 8.3 DOS filename:
 *   Source:   tests/probes/audtimer.c
 *   Binary:   AUDTIMER.EXE
 *   Log:      AUDTIMER.LOG (appends; delete between repeat runs)
 *
 * DOSBox-X: passes with cputype=pentium (RDTSC branch, ~40 cycles/us);
 * the hub's parity config is cputype=pentium_slow, whose CPUID reports no
 * TSC bit, so the default step shows the PIT fallback there. Correctness
 * smoke only; real hardware is the data gate.
 *
 * License: MIT.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

extern volatile unsigned int g_dos_audio_timer_mode;
extern volatile unsigned int g_dos_audio_timer_tsc_mhz;

static FILE *g_log;

static void say(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); fsync(fileno(g_log)); }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_log = fopen("AUDTIMER.LOG", "a");
    const char *hint = SDL_GetHint("SDL_HINT_DOS_AUDIO_TIMER_RDTSC");
    const char *tag = SDL_getenv("DOS_PORT_LOG_TAG");
    say("AUDTIMER: start; SDL revision=%s; SDL_HINT_DOS_AUDIO_TIMER_RDTSC=%s; DOS_PORT_LOG_TAG=%s",
        SDL_GetRevision(), hint ? hint : "(unset)", tag ? tag : "(unset)");
    say("AUDTIMER: SDL_Init(AUDIO)...");
    if (!SDL_Init(SDL_INIT_AUDIO)) { say("AUDTIMER: init FAILED: %s", SDL_GetError()); return 1; }
    say("AUDTIMER: audio driver=%s; opening default playback device (runs SDL_DOSAudioInitTimer)...",
        SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)");
    SDL_AudioSpec spec; spec.format = SDL_AUDIO_S16; spec.channels = 1; spec.freq = 11025;
    SDL_AudioStream *st = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!st) { say("AUDTIMER: open FAILED: %s", SDL_GetError()); SDL_Quit(); return 2; }
    say("AUDTIMER: device open; timer_mode=%u (0=bios-tick,1=pit,2=rdtsc) tsc_mhz=%u",
        (unsigned)g_dos_audio_timer_mode, (unsigned)g_dos_audio_timer_tsc_mhz);
    SDL_ResumeAudioStreamDevice(st);
    SDL_Delay(500);
    say("AUDTIMER: 500 ms of audio IRQs survived; timer_mode=%u tsc_mhz=%u",
        (unsigned)g_dos_audio_timer_mode, (unsigned)g_dos_audio_timer_tsc_mhz);
    SDL_DestroyAudioStream(st);
    SDL_Quit();
    say("AUDTIMER: clean exit");
    if (g_log) fclose(g_log);
    return 0;
}
