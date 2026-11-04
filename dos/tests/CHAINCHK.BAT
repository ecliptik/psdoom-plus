@ECHO OFF
REM CHAINCHK.BAT - what modern mode protects, and why (no TSRs, no game, no reboot)
REM Writes CHAIN.TXT (PSDPS with every interrupt-chain link it follows) and
REM RAW.TXT (the raw MCB chain and the DOS "SD" sub-blocks).
SET PSD_CHAIN_DEBUG=1
PSDPS > CHAIN.TXT
SET PSD_CHAIN_DEBUG=
PSDPS -v > RAW.TXT
