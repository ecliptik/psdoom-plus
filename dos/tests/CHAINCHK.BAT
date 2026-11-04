@ECHO OFF
REM CHAINCHK.BAT - what modern mode protects, and why (no TSRs, no game, no reboot)
REM   CHAINCHK [tag]    e.g. CHAINCHK P in PGSB, CHAINCHK N in the NET profile
REM Writes CHAIN<tag>.TXT (PSDPS with every interrupt-chain link it follows)
REM and RAW<tag>.TXT (the raw MCB chain and the DOS "SD" sub-blocks).
REM Read-only: PSDPS walks memory and the vector table, changes nothing.
SET PSD_CHAIN_DEBUG=1
PSDPS > CHAIN%1.TXT
SET PSD_CHAIN_DEBUG=
PSDPS -v > RAW%1.TXT
