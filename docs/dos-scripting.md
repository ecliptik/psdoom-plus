# DOS scripting: COMMAND.COM is not cmd.exe

Any port shipping BAT-based installers, launchers, or test harnesses for
real DOS (not just DOSBox-X, where `COMMAND.COM` is more forgiving) will
hit these. This is this hub's DOS/DJGPP instance of the "platform
profile" concept in `shared/skills/dos-hardware-validation/references/harness-invariants.md`
-- a project's own list of what its target platform does silently,
worth enforcing as a lint gate before packaging rather than discovering
after deployment. Remember that section's own warning while building any
such gate: a noisy gate stops getting run, and a gate nobody runs is
worse than no gate, because its mere existence keeps creating false
confidence the class of bug is covered.

## No escape character, at all

`^` is a `cmd.exe`/Windows-NT-only convention and does nothing on real
MS-DOS -- it's a literal character in the line, not an escape. A BAT
using `-^>` to mean "a literal arrow, not a redirect" doesn't work; the
`^` just becomes part of the text and the `>` still redirects. This kind
of bug is invisible on inspection because the line *looks* correctly
escaped.

## `<`/`>` are parsed as redirection even inside `REM` comments

`COMMAND.COM` does not special-case comment lines when scanning for
redirect characters. `REM (env > CFG by design)` silently creates a
0-byte file literally named `CFG` on every run -- and note that citing
the broken syntax as an example of the bug, even inside a comment meant
to explain or warn about it, reproduces the exact file the fix was
supposed to stop creating. **Audit for the effect a line would have (what
file would this create), not for what the syntax looks like it's trying
to do** -- this is the DOS-specific instance of `harness-invariants.md`'s
I3 ("audit for the effect, not the syntax").

## `DIR` takes exactly one path argument

Real DOS's `DIR` has no multi-argument form (`cmd.exe`'s `DIR a.bat
b.bat` doesn't exist here). `DIR a.bat b.bat` fails with an error naming
only the *second* file -- so a failed two-file `DIR` call tells you
nothing reliable about either file's actual presence; it's a report on
the command shape, not on what's on disk.

## `%VAR%` only expands inside a `.BAT` file, never at the interactive prompt

Typed directly at `C:\>`, `%VAR%` is literal text, not a variable
reference. This breaks the obvious way to check "is this variable unset"
(`IF "%VAR%"=="" ...`) when done interactively rather than from a
script -- it fails *closed* (always reports "not empty"), which wastes a
check rather than giving a wrong answer, but it's still a trap for anyone
debugging at the prompt. A check that works in both contexts:
`SET | FIND "NAME" | FIND /C "="` -- pure text processing, no expansion
involved at any layer.

## `FIND` is case-sensitive by default

Use `FIND /I` for case-insensitive matching. This matters beyond typos:
if an input-injection harness types via a mechanism sensitive to
shift/caps-lock state, the case of what actually gets typed can depend on
caps-lock state at the moment of typing -- silently flipping a search
from matching to not-matching between otherwise-identical runs. Always
use `/I` on any `FIND` whose input came from a typed/injected source
rather than a fixed script literal.

## Command lines truncate silently past 127 characters

DOS drops the tail with no error. A long `ECHO` building a manifest or
log line can lose data with nothing anywhere reporting it.

## The default environment block is 256 bytes, and overflow fails the `SET`, not the launch

A BAT that `SET`s several long-named config vars (a dozen
`SDL_HINT_*`/backend-select vars for one test cell adds up fast) can
overflow the block. `COMMAND.COM` prints "Out of environment space" and
**the offending `SET` silently fails** -- the binary then launches with
whatever default/stale config it had, not with a launch failure. This is
worse than a crash because it looks like a successful run that just
happened to use different settings. Fix: bake a self-relaunch into any
config-heavy launcher BAT -- a guard-flag `SET` that fits in the default
256B, followed by `COMMAND /E:2048 /C <inner>.BAT` where the real config
`SET`s happen inside the relaunched inner BAT with 2KB of environment
space. Don't rely on the operator's own `CONFIG.SYS` already having a
larger `SHELL=` env size configured -- ship the workaround inside the
launcher itself so it's unconditional.

## A non-idempotent CRLF pass can double a line ending that was already CRLF

If a packaging pipeline does a blanket LF-\>CRLF conversion that doesn't
first strip a pre-existing trailing CR, a file that was *already* CRLF
going in gets a doubled `\r\r\n`. DOSBox-X's `COMMAND.COM` tolerates a
stray CR; real hardware's does not -- a double-CR'd `CALL SOMETHING.BAT\r`
can mis-parse or fail the `CALL` on real DOS while passing every emulator
smoke test clean. Normalize as strip-then-add (`s/\r$//` then append
exactly one `\r\n`), and gate packaging on a hard check
(`grep -c $'\r\r' *.BAT` must be zero) before anything ships.

## The general rule underneath all of the above

Never trust an "X is absent" check unless the same pipeline has just
proven, on the same run, that it can detect "X is present" through the
identical code path. A broken pipeline (wrong case, bad quoting, the
wrong redirect landing somewhere unexpected) reports absence for
everything -- which is indistinguishable from genuine absence unless it's
been bracketed by a known-positive control run through the exact same
path.
