@ECHO OFF
REM AUDTIMER.BAT - which audio IRQ timer did SDL3-DOS pick (SDL/0039 + SDL/0138).
REM Step 1 never executes RDTSC: killswitch on, expect the PIT banner with
REM   RDTSC disabled via SDL_HINT_DOS_AUDIO_TIMER_RDTSC=0 in LOGS\KS0SDL.LOG.
REM Step 2 is the production default: executes RDTSC from the DPMI client on a
REM   TSC-bearing CPU, expect the RDTSC banner in LOGS\DEFSDL.LOG.
REM Output: AUDTIMER.LOG (appends) + LOGS\KS0SDL.LOG + LOGS\DEFSDL.LOG.
REM Runtime: about 2 sec. Needs BLASTER set; SDL3-linked probe; CWSDPMI.EXE same dir.
ECHO Step 1: killswitch on (no RDTSC executed)...
SET SDL_HINT_DOS_AUDIO_TIMER_RDTSC=0
SET DOS_PORT_LOG_TAG=KS0
AUDTIMER.EXE
ECHO Step 2: default auto-detect (RDTSC if the CPU has a TSC)...
SET SDL_HINT_DOS_AUDIO_TIMER_RDTSC=
SET DOS_PORT_LOG_TAG=DEF
AUDTIMER.EXE
SET DOS_PORT_LOG_TAG=
ECHO ----
ECHO Probe finished. AUDTIMER.LOG contents:
TYPE AUDTIMER.LOG
