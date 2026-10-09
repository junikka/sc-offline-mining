Forked from sc-offline rc4 at first

Dev tab added to test mining and stuff

# Automatic natural mining

Nearby natural harvestables spawn automatically during offline startup. Start the game, approach ore and use the normal mining controls; no DEV switch is required. The DEV tab reports availability and retains optional fixture, marker and trace tools.

The manual version was confirmed in game on 9 October 2026: natural Titanium(Ore) highlighted, displayed composition, responded to the mining laser and supported extraction. The corresponding trace recorded 10 native spawn requests, all accepted. That exact source is preserved by commit `1c0f97456ae34860ea079a4246e351455ba1d800` and tag `mining-working-manual` in the `junikka/sc-offline-mining` repository. The automatic-startup refactor passes compilation and automated tests; its startup behavior still needs a new in-game run.

## Implementation

`src/mining.cpp` installs one hook on `CBiomeBuilder::BuildLargeScaleEcoSystem` during `StartOffline`, before DEV initializes. The hook checks the current offline/server context on each eligible call, so it also works before the menu's main-thread service starts. No manual activation flag, trace session or UI tick is needed.

The successful manual rule is retained: for enabled builder type 0, incoming flags exactly `0x6`, requested LOD 0/1 and previous harvestable LOD 2..100, pass flags `0xe` to the native function. Only spawning bit `0x8` is added. All other arguments are unchanged and the native function is called once. Other builder types, unknown flags, physics-only work, distant cells and completed near LODs pass unchanged.

Native provider validation, location/depletion checks, original transforms/materials, batch submission and completed LOD writes remain in the game. Type 0 excludes the separate planet-side entity spawning branch for types 1/2/3. There are no synthetic replacement rocks or changes to mass, resistance, laser power, global server/editor flags or CVars. Native rejection and asynchronous spawn failures remain possible.

The module requires server=1, editor=0 and online=0, plus the existing offline APIs. Context changes suspend changes immediately and normal context restoration resumes them. An eligibility/context read fault disables the hook's changes until restart. Exceptions from the original native function propagate without retries. DEV inspection faults are independent of this verified hook.

## Native contract

- Supported executable SHA-256: `3953f8b1a9894d5d9d1836e50939b726141f1829fe65a3a622d5acb2c6f89162`.
- Preferred image base: `0x140000000`; all addresses relocate against the running module.
- Cell function RVA: `0x26593c0`; first instruction `44 89 4c 24 20` (`MOV [RSP+0x20],R9D`), a complete five-byte instruction without a relative operand. A longer entry signature is verified before installation.
- Seven integer/pointer arguments: builder, cell, procedural component, requested LOD, flags, 64-bit page and one-byte option. Return type: void.
- Builder type `+8`, enabled byte `+0x28`; cell harvestable LOD `+0x16a8`.
- Context RVAs: server `0x9e2e908`, editor `0x9e2ec66`, online `0x9e2ec6e`. The online flag address must also match the existing offline patch resolver.

The executable hash is computed once and shared with DEV diagnostics. DEV separately verifies its 15 contact/controller entry signatures. Natural spawning does not depend on those diagnostic hooks; it needs only the cell hook. The ineffective V5 patch and seven research-only observers were removed.

## Diagnostics and validation

Normal logging contains installation and state transitions. During a requested DEV trace, `[mining/trace]` reports cumulative native cell calls, promoted cells and read faults. A promoted cell means the spawning flag was forwarded, not that every candidate produced an entity. Marker/controller traces establish live mineables and mining response. Counters are process-wide.

Automated tests exercise the actual hook: first eligible call without any UI tick/action, startup outside server context, unchanged arguments, completed-LOD suppression, excluded flags/types/LODs, context loss/recovery, invalid executable, native exception propagation, read-fault latching and independence from DEV tracing. Existing fixture/marker/controller lifecycle tests remain.

The next acceptance test is to restart, approach natural ore without opening DEV, scan, fracture and extract. Leaving/returning to the area, duplicate suppression across streaming, and persistence across restarts still need runtime verification.

## Restore the working manual version

Create a separate checkout rather than discarding current work:

```powershell
git worktree add ../sc-offline-mining-manual mining-working-manual
```

Build its `source_code` projects according to `build.md`. The original tested DLL and logs are also retained locally under `analysis/mining-rnd/dev-confirmed-savepoint/`. Research, logs, generated builds and personal runtime state are ignored by Git; they remain on disk.
