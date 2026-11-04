@ECHO OFF
REM WBSINKMIDI.BAT - shared/audio/waveblaster_sink probe: plays a small
REM self-authored test SMF through midi_sched + the WaveBlaster/MPU-401
REM sink; checkable via WBSNKMD.LOG's own event-count/tempo/dispatch/
REM MPU-byte witnesses and VERDICT=PASS/FAIL + MPU401_EMULATION lines.
REM WBSINKMD.EXE sets SDL_HINT_DOS_AUDIO_PROBE_MPU401=1 itself, in-process,
REM before touching the sink (see wbsinkmd.c's own header for why this
REM env var must never be set anywhere else) -- this .bat needs no SET line.
ECHO Running WBSINKMIDI probe...
WBSINKMD.EXE
ECHO ----
TYPE WBSNKMD.LOG
ECHO DONE
