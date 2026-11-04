---
name: dos-rig-operations
description: >
  Operating a vcctrl-controlled real-hardware DOS rig for day-to-day work
  -- typing/input injection, screen capture, file transfer, log
  collection, and power management -- independent of running a full
  validation campaign. Use this whenever driving a real DOS machine
  directly -- sending a build over to poke at manually, taking a
  screenshot to check something, pulling a log file back, power-cycling
  the rig, or any other one-off rig interaction that isn't a full
  dos-hardware-validation campaign.
---

# DOS rig operations

Mechanics for the five things a real-hardware session does constantly:
type/click, capture the screen, move files, collect logs, control power.
For the deeper methodology of running a rigorous A/B campaign on top of
these primitives, see `dos-hardware-validation` -- that skill assumes this
one's discipline as a prerequisite rather than re-deriving it.

**vcctrl already has deep mechanics coverage of most of this** in its own
skills -- `vcctrl-rig-hazards`, `vcctrl-common-workflows`, and
`vcctrl-mcp-workflows`. Where a reference file below says "see vcctrl's
docs," that's deliberate: this hub's job is naming the hazard class and
why it matters for a *port* session, not re-deriving mechanics vcctrl
already owns and keeps current. Check vcctrl's docs for the literal
tool/command surface, which can move; treat this skill's prose as the
discipline, not a frozen API reference.

Distilled from the same real doskutsu/vcctrl campaign session as
`dos-hardware-validation` -- see `shared/skills/README.md` for provenance.

## The six areas

1. **Input injection** — typing/keystrokes landing correctly, or not.
   `references/input-injection.md`
2. **Screen capture** — what makes a capture actually useful evidence, not
   just a picture. `references/screen-capture.md`
3. **File transfer** — staging a build onto the rig and pulling results
   back, including the packaging step before a transfer. `references/file-transfer.md`
4. **Log collection** — telling "never written" from "transfer failed"
   from "written but incomplete." `references/log-collection.md`
5. **Power management** — safe power-cycle discipline for a rig where the
   physical wiring isn't software-verifiable. `references/power-management.md`
   Includes the one recurring hang worth knowing by sight: the target
   stops answering the keyboard right after an FTP session, at the
   return reboot chord -- signature, the four-step check, the one-power-
   cycle recovery, and the rule that any script which reboots the target
   must detect and recover from it by itself.
6. **Multi-agent coordination** — a spawned subagent does not inherit the
   rig connection, and more than one caller reaching the rig-driving
   session at once is a real double-dispatch hazard against physical
   hardware, not a hypothetical. Read this one *before* a team-lead spawns
   a specialist whose charter assumes direct rig access.
   `references/agent-coordination.md`

## The one rule that spans all five

**An ambiguous result is not evidence of a particular state -- it's a
prompt to take an independent read before deciding anything.** If a
fetch, populate, or cell operation comes back ambiguous (timed out,
partial, ambiguous error text), don't assume what state the machine is
actually in and don't assume the next step's precondition is safe. Take a
screenshot (or whatever independent read is cheapest) before choosing to
retry, re-verify, or escalate. This single rule is behind several of the
concrete hazards in the reference files below -- it's called out here
once because it generalizes past all of them, not just the one it was
first noticed in.

## Batch questions into fewer, longer rounds

A file-transfer round-trip typically costs a fixed reboot tax (order of
30-90s) independent of what's actually being tested -- unavoidable
overhead, not something to optimize away. Given that, batching several
questions into one longer real-hardware run beats splitting them across
multiple short ones every time; the fixed cost is paid once either way,
so paying it more often for less information per round is pure waste.
Plan a cell/round's scope with this in mind before staging anything.

## When something here turns out wrong

If a rule in this skill (or in vcctrl's own skills) doesn't match what
actually happens on the rig, that's a real bug or a real doc gap --
fix/update it and add a regression check where one applies, per
`dos-hardware-validation`'s "a harness bug is a bug, not a workaround"
rule. Coordinate with whichever session owns the harness (vcctrl, or
whichever session is driving `vcctrld` for a given port) before changing
shared mechanics out from under it.
