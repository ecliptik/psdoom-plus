@ECHO OFF
REM OPL3MIDI.BAT - shared/audio/opl3_sink probe: plays a small self-authored
REM test SMF through midi_sched + the OPL3 sink; checkable via OPL3MIDI.LOG's
REM own event-count/tempo/dispatch witnesses and VERDICT=PASS/FAIL line.
REM Stage OPL3BANK.DAT alongside (shared/audio/opl3bank.dat) to also exercise
REM the full 128-program GM bank loader instead of the 8-patch fallback.
REM See opl3midi.c's own header for the build recipe and the oplmode=opl3
REM DOSBox-X conf requirement (this hub's shared confs default oplmode=none).
ECHO Running OPL3MIDI probe...
OPL3MIDI.EXE
ECHO ----
TYPE OPL3MIDI.LOG
ECHO DONE
