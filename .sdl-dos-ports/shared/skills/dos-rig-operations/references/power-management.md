# Power management

Lower bug-density than input/capture/transfer, but the stakes when it's
wrong are physical rather than just "the run is invalid" -- so the rules
here are short and non-negotiable rather than judgment calls.

For the literal smart-plug scoping and command surface, see vcctrl's own
`vcctrl-rig-hazards`.

## The plug's scoping is visibility to a human, not a hardware interlock

A power-control system scoped so a given control can't hit the wrong
*plug* still can't verify which physical *machine* is actually seated on
the other end of that plug's cabling. Scoping prevents you from
accidentally power-cycling a different rig by picking the wrong control;
it does not prove the cabling matches what the config claims. Treat plug
scoping as a safety rail against picking the wrong control, not as proof
of which machine you're actually about to affect -- if there's ever real
doubt about what's physically wired to a given control, resolve that
before acting, don't infer it from the config alone.

## A power cycle should be unconditional regardless of reported state

A machine that's wedged (hung, frozen, unresponsive) can still report
"on" through whatever status channel exists -- a wedged machine isn't
necessarily a powered-off one. Don't skip a power cycle because the
reported state already looks like what you want; if the goal is a known-
clean boot, cycle power unconditionally rather than trusting a status
read that a hang can trivially falsify.

## Confirm-string discipline

Any power action that isn't trivially reversible (a full power-off, not
just a reboot) should require an explicit confirm step, not fire on the
first request -- the cost of an accidental power-off on a real machine
(interrupted writes, a card that needs a clean reboot to re-enumerate
correctly) is higher than the cost of one extra confirmation prompt.

## A working reset chord can take far longer to register than expected

A soft-reboot chord (Ctrl-Alt-Del or equivalent) that's genuinely going
to work can still take a long time to actually register on real hardware
-- measured as long as ~85 seconds on one real rig. A short timeout that
gives up before that window has elapsed and reports "the machine never
reset" is not evidence the chord was swallowed or the hardware is at
fault -- it's evidence the timeout was too impatient. Before concluding a
reset chord failed, confirm the wait window was actually long enough for
a real (not emulated) reboot cycle; don't let a short-timeout false
negative get diagnosed as a hardware or input-injection problem (see
`input-injection.md`'s landed-vs-dropped material for the general
version of this mistake).

## A PS/2 hang looks like a lock problem until you check

If a keyboard/mouse action against the rig appears to hang, the actual
cause can be either a genuine PS/2-level hardware hang *or* the session's
own input lock being held by something else entirely (see
`agent-coordination.md`) -- and from the caller's side, both look
identical: the action just doesn't complete. **Check whether the lock is
actually free before concluding it's a hardware fault.** Diagnosing a
lock conflict as a hardware hang leads to power-cycling a machine that
was never actually broken; diagnosing a real hardware hang as "just a
lock issue" leads to waiting indefinitely on something that will never
resolve itself.

**Recovery policy once the cause is genuinely ambiguous**: one power
cycle, then stop. Attempt a single power-cycle recovery; if that doesn't
resolve it, stop and escalate to a human rather than repeatedly retrying
power actions against a real machine on the assumption that the next
attempt will be the one that works. Repeated blind power-cycling against
unresolved ambiguity risks compounding whatever's actually wrong (a card
that needs a clean shutdown to re-enumerate, interrupted writes) rather
than fixing it.

## The return reboot that never takes: signature, check, recovery

A recurring fault on the g2k 486 machine: twice in two days (2026-09-16,
2026-09-17) the target stopped answering the keyboard immediately after
an FTP session in the NET boot profile, at the reboot chord that
`send-file --return` / `get-file --return` issue to get back to the
normal profile. A related dead-link incident a day earlier (dosags tag
S32B5, 2026-09-15) had the same `verify-input` signature but struck
mid-run, after the launch batch file was typed -- so the FTP session may
be a trigger rather than the cause. Root cause unknown; it is tracked on
vcctrl's side as an open PS/2-hang fault. What matters for a port session
is recognising it in a minute and recovering without a human.

**Signature.** The screen sits on the FINISHED FTP-client output (mTCP's
"226 Transfer complete", "221 Goodbye", "Server closed control
connection", its bug-report footer) with no prompt after it. Nothing is
in flight on vcctrl, the input lock is free, mains power reads on.
Ctrl-Alt-Del from anywhere does nothing.

**Check, in this order:**

1. Lock and activity first (previous section) -- a held lock is not a
   hang.
2. Give the chord its real window (the section before that: a working
   chord has taken ~85 s). Judge from a time at least that long after
   the chord, not from the first static screenshot.
3. Both capture channels (`vcctrl_shot` and the camera) still show the
   same FTP output.
4. `verify-input`. It is valid and safe HERE because no SDL3-DOS program
   is running (never send it while one might be -- see
   `docs/hardware-testing.md`). The fault reads `"verified": false`, "no
   LED change", typically with the lock LEDs stuck in whatever state they
   had. A hung machine and a dead PS/2 link are indistinguishable from
   the wire, and it does not matter: the recovery is the same.

**Recovery.** One full power cycle -- a warm reboot cannot work, the
keyboard path is the thing that is dead. Verify the FULL boot sequence by
screenshot (not merely "picture present"), and require `verify-input` to
come back true before doing anything else. Then account for the step
that was interrupted: trust only collected files whose size/hash verify,
and re-collect or re-run under a FRESH tag if there is any doubt. If the
single power cycle does not restore the link, stop and escalate, per the
policy above.

**Automate it -- do not leave this to whoever happens to be watching.**
Any script that reboots the target must (a) require a positive "the
reboot took" witness within a bounded time -- the screen leaves the FTP
output and reaches the boot banner / ready prompt; (b) on failure run
the check above and, on `verified: false`, perform the one power cycle
itself, re-verify boot and link, and resume from the step that failed;
(c) stop and report on a second failure; (d) log every recovery in the
run record (tag, step, time lost) so the frequency is visible. Without
(a), a harness that correctly refuses to proceed simply sits there, and
the fault costs as long as it takes a human to notice.
