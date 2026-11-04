@ECHO OFF
REM NETTEST.BAT - psdoom-plus shoots a remote `sleep` through psdoomd
REM   NETTEST <iwad> <psdoomd-ip:port> [psdoom options]
REM Needs the packet driver loaded, our address (PSDOOM_IP, or mTCP's MTCPCFG),
REM and the token in PSD.CFG ("net_token = ...") or PSDOOM_NET_TOKEN.
REM Writes NET.TXT (PSTEST line) and NETERR.TXT.
IF "%2"=="" GOTO usage
SET PSDOOM_STDERR=NETERR.TXT
PSDOOM -iwad %1 -warp 1 1 %3 -psnet %2 -pstest sleep > NET.TXT
SET PSDOOM_STDERR=
GOTO end
:usage
ECHO usage: NETTEST iwad psdoomd-ip:port [psdoom options]
:end
