# CLAUDE.md

Project guidance lives in [AGENTS.md](AGENTS.md); read it first.

## Building

The Oscar64 compiler is not on PATH. Before running `make`:

```sh
export OSCAR64=~/Programs/oscar64/build/oscar64
```

Then `make` (builds `build/game.crt`), `make debug` (builds
`build/game-debug.crt` with `-dGAME_DEBUG`, enabling developer keys such
as `+`/`-` for the global light level), `make test` (VICE + Xvfb regression,
including an F1/F3 save/load round trip on a disposable D64).

`make` also runs `tools/check_bank_switches.py` on `build/game.asm` and fails
if any `$DE00` bank switch is emitted inside banked code (see AGENTS.md).
