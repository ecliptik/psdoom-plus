@ECHO OFF
REM MCBTEST.BAT - psdoom-plus memory-chain test (DOSBox-X and real hardware)
REM   MCBTEST (iwad) [psdoom options] [command after each game]
REM e.g. MCBTEST FREEDOOM.WAD -640 C:\VGACAP\MODE12.COM
REM (results: PS?.TXT C?.TXT M?.TXT RAW.TXT PSD.LOG CLASSIC.PPM DONE.TXT)
REM Loads TSRs that stay resident: reboot afterwards. Check with check-mcb.sh.
IF "%1"=="" GOTO usage
DEL PS?.TXT > NUL
DEL C?.TXT > NUL
DEL M?.TXT > NUL
DEL RAW.TXT > NUL
DEL PSD.LOG > NUL
TSR
TSR
HOOKTSR
PSDPS > PS0.TXT
PSDOOM -iwad %1 -warp 1 1 %2 -psclassic -pstest TSR -pstestview -pstestshot CLASSIC.PPM > C1.TXT
%3
PSDPS > PS1.TXT
PSDOOM -iwad %1 -warp 1 1 %2 -psclassic -pstest HOOKTSR > C2.TXT
%3
PSDPS > PS2.TXT
TSR
TSR
HOOKTSR
PSDPS > PS3.TXT
PSDOOM -iwad %1 -warp 1 1 %2 -pstest TSR > M1.TXT
%3
PSDPS > PS4.TXT
PSDOOM -iwad %1 -warp 1 1 %2 -pstest HOOKTSR > M2.TXT
%3
PSDPS > PS5.TXT
PSDOOM -iwad %1 -warp 1 1 %2 -pstest COMMAND > M3.TXT
%3
PSDPS > PS6.TXT
REM HOOKTSR behind CHAINTSR on INT F1h: still protected in modern mode
CHAINTSR
PSDPS > PS7.TXT
PSDOOM -iwad %1 -warp 1 1 %2 -pstest HOOKTSR > M4.TXT
%3
PSDPS > PS8.TXT
PSDPS -v > RAW.TXT
ECHO MCBTEST done > DONE.TXT
GOTO end
:usage
ECHO usage: MCBTEST iwad [psdoom options] [command after each game]
:end
