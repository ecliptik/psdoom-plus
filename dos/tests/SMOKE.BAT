@ECHO OFF
REM SMOKE.BAT - psdoom-plus smoke test: start, play MAP01 for ~15 s, exit
REM   SMOKE <iwad> [psdoom options]     e.g. SMOKE FREEDOOM.WAD -640
REM Writes SMOKE.TXT (the log), SMOKEERR.TXT (stderr), SMOKE.PPM (last frame).
REM No keyboard input is needed: -pstest with a name that matches nothing
REM waits 15 s for a target, then quits on its own.
IF "%1"=="" GOTO usage
SET PSDOOM_STDERR=SMOKEERR.TXT
PSDOOM -iwad %1 -warp 1 1 %2 -pstest NOSUCHPROC -pstestshot SMOKE.PPM > SMOKE.TXT
SET PSDOOM_STDERR=
GOTO end
:usage
ECHO usage: SMOKE iwad [psdoom options]
:end
