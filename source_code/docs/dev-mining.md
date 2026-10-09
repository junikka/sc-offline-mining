# DEV: first mining-rock experiment

This is an opt-in runtime test in the mod's **DEV** tab. It spawns a full entity class through the existing Build/vehicle spawn-batch path, then offers a separate **Enable targeting / scanning (fallback)** button. Normal contacts may appear automatically. The optional adapter submits a native fixed-contact request to the local actor, current ship, and controlling player's available databanks. The engine derives the contact type and scanning properties from the fixture definition.

Implemented and extended on 8 October 2026. The user confirmed the half-size Iron fixture could be scanned, fractured, extracted and collected into Prospector cargo. Its trace showed mass 6,193.6, volume 1,056.37 and charge rising to 0.590479 during the captured interval. The full-size fixture was too massive for the tested laser. **The natural-promotion experiment failed on Bloom and Terminus.** The Terminus observers recorded no calls to the previously patched promotion function or the native spawn-request entry point. The next Terminus capture reached the older biome path but never selected spawning. The current revision adds a separate nearby-biome spawning experiment; it is not yet verified in game. Cargo persistence across reload and natural-rock depletion/streaming are not yet verified.

## Current experiment: nearby biome spawning, 9 October 2026

The latest Terminus run installed all seven earlier probes and enabled the experiment before teleport. Its 83 counter snapshots ended with **1,129,209 root builder calls, 9,952 cell calls, 438 close LOD transitions, 333 drawing instances and zero spawn instances or requests**. Every observed cell lacked bit `0x8`; sampled close cells had flags `0x6` and an initial harvestable LOD of 100. Two unique natural marker keys still had no live entity, and all six controller samples had no target. Original logs and a reproducible summary are in `analysis/mining-rnd/dev-nearby-biome/runtime/`.

The runtime server flag was **1**, and both harvestable spawning and planetside spawning settings were **1**. A disabled server flag or disabled spawning CVar does not explain this run. Root samples used `0xc` in a physics pre-spawn path; cell samples used `0x4` or `0x6`. Those aggregate observations alone do not establish which root call produced which cell. The native job can remove bit `0x8` when its captured eligibility byte is false, but that byte was not measured in the supplied run.

### Run the new experiment

1. Restart using the updated `compiled` build.
2. Open DEV and press **Enable nearby biome spawning** before travelling to the ore area. Leave **older V5 promotion experiment** off; it is a separate, previously ineffective experiment.
3. Approach natural ore on Terminus or Bloom. Press **Inspect natural markers**, then capture a mining trace and try normal scanning/mining.
4. Check `promotedCells`, `mode1_spawn`, requests/accepted/rejections, then marker `entity!=0 mineable=1` and a valid controller target. An accepted request alone is not completed spawning.
5. If the rock becomes mineable, test fracture/extraction and leave/return to check native depletion and duplicate behavior. Preserve mod.log and Game.log for either outcome.

The new toggle is off each restart. It adds only bit `0x8` to an already scheduled cell with **type 0, enabled builder, incoming flags exactly `0x6`, requested LOD 0/1 and previous harvestable LOD 2..100**. The original function runs once with its other arguments unchanged. Other builder types, physics-only work, unknown flags, distant cells and already completed near LODs pass unchanged. This is a constrained workaround to test the observed missing spawn selection, not a claim that the upstream scheduling cause is fixed.

The native cell builder still resolves the provider, checks its enabled state, builds the original location/transform/composition inputs, calls `CanSpawnInstance`, tracks pending/depleted locations, submits its own batch and writes its own LOD state. Type 0 excludes the separate planet-side entity branch that requires types 1/2/3. The experiment operates on nearby native harvestable cells; it is not limited to the marker under the crosshair. No extra manual fixture, guessed position, forced provider acceptance, global server flag or CVar write is involved.

Enable requires the exact executable and native code checks, all observers, and server=1/editor=0/online=0. An atomic flag controls workers independently from diagnostic tracing. Workers recheck the native context at each qualifying decision. Main-thread context/spawner/adapter loss disables it; a context read fault latches it off until restart. Disabling restores future argument pass-through but does not undo in-flight batches or remove existing natural entities. Native streaming/depletion behavior remains unverified.

### Added job correlation

Current startup is **`installed=0xff complete=1`** (eight observers, 25 native code guards). The new job observer is `SubmitEcosystemWork::<lambda_1>` at RVA `0x263e3e0`, with five copied whole non-relative entry bytes. Stage-3 samples read the native job's incoming flags, requested LOD, cell, builder and eligibility byte. A thread-local scope associates nested cell calls with that actual job, preserving nested contexts and propagating native exceptions.

`[dev/mining/biome-scheduling]` logs `jobs`, `withSpawnBit`, `eligible`, `spawnBitBlocked`, `nearbyEnabled`, `promotedCells` and `contextFault`. `spawnBitBlocked` counts job inputs with bit 8 and eligibility zero; it does not prove later execution reached the clearing branch. Cell sample `flags` is input; `forwardedFlags` is the actual value passed on. `fromJob`, `jobFlags` and `jobEligible` show correlated parent input when available. An uncorrelated call can still be native work. `forwardedFlags` applies only to stage 1. The older `cellsWithSpawnBit` remains the count of incoming native flags; mod additions are separate.

There are eight buffered samples per stage (four stages), drained on the main thread; native workers never wait on the sample lock. Large `droppedSamples` totals mean bounded diagnostic samples were omitted, not that native jobs were dropped. Cumulative call counters continue independently. Neither global counters nor those limited samples establish which instance corresponds to the user's Gold marker.

Release compilation and fake-engine tests pass. Tests cover unchanged behavior while disabled/trace-only, the exact added bit with one original call, all excluded flags/types/LODs, native request origin, simulated completed-LOD suppression, context failure/disable/fault latching, parent-job correlation and native-exception cleanup. **The new job hook and spawning experiment have not yet run in game.**

## Earlier Terminus result and diagnostic build

The Terminus session is preserved in `analysis/mining-rnd/dev-biome-trace/runtime/`, with a reproducible `analyze-terminus.py` summary. The four earlier observers installed successfully (`installed=0xf complete=1`) at 18:44:00.498. The experiment enabled at 18:46:00.973 before teleport to `pyro6` at 18:46:33.536. All 87 counter samples from 18:46:00.974 through 18:51:39.089 report `active=1`; jobs, objects, kind-5 candidates, spawn requests, rejections, read faults and dropped samples all remained zero.

Two marker inspections across both banks produced eight records for two unique provider/location keys: `8214576489518467774` and `8214576489518467775`. All had `targetable=1 scanData=1 entity=0 mineable=0`; there was no live entity association. The user identified the visible ore as Gold; the records themselves do not resolve its class name. All 24 controller samples had zero target/focus/scan selection/hit rate/HUD mass/transfer, including 14 samples with firing active. The native Game.log also had no matching spawn-request/rejection messages.

This establishes that the instrumented V5 callback and request entry point were not reached during the observed interval. It does not establish that all terrain processing stopped: alternate or inlined paths were not covered. The valid test sequence and lack of a saved state do not explain this failure. Repeated firing is unnecessary while the marker has no mineable entity.

### Older biome path found in native code

Static analysis found a separate path that distinguishes drawing harvestable meshes from spawning their entities:

1. `CWorldBuilder::UpdatePlanet` (`0x1427527b0`) starts ecosystem work flags at `0x6`, using `0xe` when the native server flag is set. Bit `0x8` selects spawning work. This is static evidence; the current runtime value has not yet been measured.
2. `CBiomeBuilder::BuildEcoSystems` (`0x142652ce0`) considers spawning work only with bit `0x8`, an upstream per-cell eligibility byte, a changed harvestable LOD, and the native server flag. Other drawing/physics reasons can still schedule a cell. The authority predicate participates upstream; the eligibility byte must not be treated as a complete authority result.
3. `SubmitEcosystemWork` (`0x142743150`) copies the work flags into the asynchronous job. Its callback (`0x14263e3e0`) can clear bit `0x8` when a separate captured eligibility byte is false. `BuildLargeScaleJob` (`0x1426dcf90`) then calls the cell builder.
4. `BuildLargeScaleEcoSystem` (`0x1426593c0`) resolves the provider, checks enabled state and chooses an instance mode. Mode 1 needs spawning bit `0x8` and a transition from previous harvestable LOD >= 2 to requested LOD <= 1, among other gates. Mode 0 records drawing data. The helper at `0x14273c180` sends a native provider spawn request only in mode 1, with the existing native batch and caller-authority byte 1.

This makes missing local spawning work a concrete hypothesis. It does not yet prove that this path handled the observed Gold or Iron contacts. Setting the global server flag or blindly adding bit `0x8` would affect additional engine work, including other planetside entities; this revision does neither.

### What the added measurements mean

Three additional pass-through observers extend the four earlier probes. That diagnostic revision reported **`installed=0x7f complete=1`**; the current eight-observer build uses `0xff`. The prior Terminus mask `0xf` was complete for that older build.

| Observer | RVA / copied entry bytes | Measurement |
| --- | --- | --- |
| `BuildEcoSystems` | `0x2652ce0` / 7 | Root calls, incoming work flags, builder type and enabled byte. |
| `BuildLargeScaleEcoSystem` | `0x26593c0` / 5 | Cell calls, incoming flags, previous harvestable LOD and requested LOD. |
| Harvestable instance helper | `0x273c180` / 5 | Mode histogram, copied provider/class/location in modes 0/1, and nested native requests. |

`[dev/mining/biome-probe]` contains cumulative `builds`, `buildsWithSpawnBit`, `cells`, `cellsWithSpawnBit`, `nearCells`, `mode0_draw`, `mode1_spawn`, `mode2`, `mode3`, `otherModes`, `requests` and `droppedSamples`. `nearCells` counts only the observed LOD transition; it does not mean every spawning gate passed. Mode 0 is drawing, mode 1 is a spawn attempt, and request acceptance still does not prove completed entity creation.

`[dev/mining/biome-sample]` stages are 0 = root builder, 1 = cell builder, 2 = instance helper. Only relevant fields apply to each stage; sentinel -1 fields are not measured. Each stage retains at most eight samples per main-thread drain. Sampling uses a nonblocking lock; excess or contended samples increment the biome drop counter while cumulative call counters continue. Copied fields are guarded reads, and original functions receive every argument unchanged, exactly once. The request record now includes `fromBiome`, tracked only inside the native instance helper with exception-safe thread-local cleanup.

On enable, inspection, trace start and refresh, `[dev/mining/environment]` reads the server/editor/online flags, and `[dev/mining/cvar]` reads harvestable spawning, planetside spawning and relevant scattering settings using the existing console accessor. `known=0` means unavailable, not disabled; an environment value of -1 means unreadable. No settings are written. Counts are process-wide during observation, not exclusively the aimed-at marker.

That diagnostic test has now been completed. Use the new nearby-biome procedure at the top of this document for the current build.

## Earlier Bloom result and core promotion observers

The 18:00–18:10 session is preserved in `analysis/mining-rnd/dev-promotion-trace/runtime/`. The experiment enabled at 18:00:44.983 with `editor=0 online=0`; teleport to Bloom followed at 18:01:05.691. Two marker snapshots, each containing both databanks, produced 104 records for 26 unique keys. Every record had `entity=0 mineable=0`; five unique keys had scan metadata. All 29 controller samples had `target=0 focused=0 scanned=0 scanEntry=0 scanEntity=0 lastHitRate=0 hudMass=0`. The laser fired in 21 samples and throttle reached 1. This agrees with no highlighting, no mining composition and no laser interaction. Mass/resistance are not the immediate blocker when there is no mining target.

The same-session Game.log contains no `CanSpawnInstance`, `UnexpectedPlanetResource`, `RequestHarvestableSpawn` or `PlanetScatteredEntityPromotion` messages. That absence does not prove that these functions were never called: the previous build did not observe their execution, and native logging may be filtered. Missing live entity associations remain established; the exact stopping point and any missing server response remain unproven.

“Fresh terrain” meant patches not already processed earlier in the current running session. Restarting clears the in-memory state in question; it does not require new procedural terrain or deleting a save. Enabling before travelling to Bloom was the intended test. That result must not be dismissed as an incorrect test sequence.

The earlier revision installed the following four core pass-through observers during mod startup after executable/code checks; the current build adds the three biome observers above. They are inactive by default and collect only during the enabled natural experiment or a mining trace. No extra button is needed. `[dev/mining/promotion-probe]` logs cumulative session counts every five seconds during the experiment, every second during a trace, and on inspection/refresh/enable/disable/trace transitions. Differences between consecutive totals show activity in that interval. DEV also shows the main totals. `installed=0xff complete=1` confirms all eight current observers installed; unavailable observers must not be interpreted as idle native code.

| Observer | RVA / copied entry bytes | What it measures |
| --- | --- | --- |
| `QueuePatchPromotion` worker lambda | `0x2639070` / 7 | `jobs`: actual calls while observation is active. |
| Scatter object descriptor reader | `0x26d2250` / 5 | `objects`, `kind1`, `kind4`, `kind5`, `kindOther`, `geometry5`, only inside an observed promotion job. Kind pointer is output `+0x30`; geometry references are `+0x48/+0x50`. |
| `RequestHarvestableSpawn` | `0x3d18b20` / 5 | All `requests`, `fromPromotion`, and return-value `accepted`; other native harvestable callers are counted separately by origin. |
| `CanSpawnInstance` | `0x3cd3990` / 6 | Within an observed request, distinguish `rejectedCheck`, `rejectedBeforeCheck`, and `rejectedAfterCheck`. This records the boolean result, not a fabricated rejection reason. |

Each hook calls the original once with unchanged arguments and returns its actual result. Copied entry instructions contain no relative operands and end on instruction boundaries. Workers use atomic counters and thread-local call context. Sample reads are guarded; native exceptions propagate, with thread context restored. No extra engine queries or logging run under native worker locks. A nonblocking buffer holds at most 32 request records between main-thread drains; overflow/contention increments `droppedSamples`. Totals are independent atomic observations and may include in-flight work, not a transactionally consistent snapshot.

`[dev/mining/promotion-request]` adds the call-site RVA, origin, copied provider handle, class-definition address, native location tuple, caller-authority byte, precondition result (`-1` if not observed), and accepted result. Pointers/handles are copied values and are never resolved later. `readFaults` / `readable=0` identify incomplete diagnostic reads. An accepted request does not prove that its asynchronous batch completed or that a visible marker gained an entity. Counts cover all active native callers, not only the rock under the crosshair.

The subsequent Terminus run tested these four observers and found no calls, as detailed above. The current next step is to observe the older biome path. The prior one-byte experiment is unchanged; no additional spawning gate is bypassed.

## Natural promotion experiment

The earlier fixture/surface log is preserved in `analysis/mining-rnd/dev-natural-promotion/runtime/`. Five surface marker inspections recorded the same four unique provider/location keys, ending in 6284 through 6287. Across both databanks there were 40 records, all with `entity=0 mineable=0 authority=-1`, while `targetable=1 scanData=1`. All 35 surface controller samples had `target=0 focused=0 scanEntry=0 scanEntity=0`. These are class-backed radar contacts without a validated live entity association; `authority=-1` means unmeasured, not non-authoritative.

Static analysis located `PlanetV5::ScatterGeneratorCPU::QueuePatchPromotion::<lambda_1>` at `0x142639070`. Its kind-5 (harvestable) branch loads the editor flag from `0x149e2ec66` and skips local spawning when it is false. The flag's editor meaning is independently corroborated by `CWorldBuilder::InvalidateAllRenderMeshes_EditorOnly` at `0x1426e7310` and tag-database editor guards. It is not the server flag (`0x149e2e908`). This is a concrete candidate for the missing local promotion step, not yet proof that this callback handled the four recorded locations.

The opt-in **Enable natural promotion experiment** button permits that one harvestable branch during offline play. It retains geometry, provider-handle, batch and `CanSpawnInstance` checks, then uses the native spawn request, provider/location/setup attributes, pending-spawn bookkeeping and batch submission. The native caller already passes `true` as its caller-authority argument. The mod does not change global editor/server/authority flags or manufacture replacement rocks at guessed marker positions.

1. Restart using the updated build. The experiment starts **off** every session.
2. In DEV, press **Enable natural promotion experiment** before approaching an unvisited planetary mining area.
3. Travel to the test area and find natural ore markers. Enabling before travel avoids relying on an already processed patch being revisited during that same session.
4. Press **Inspect natural markers**; optionally use **Capture mining trace (30 seconds)** for more frequent samples. For the current scheduling investigation, firing is unnecessary. A future functional result would require `entity!=0 mineable=1`, then a valid controller target and charge.
5. If it works, test fracture/extraction and leave/return to check that native depletion and streaming avoid duplicate rocks. If it does not, preserve both `compiled/data/mod.log` and `Game.log`; provider rejection logs may identify the next gate. Check the promotion and biome counters to determine which instrumented entry points ran.

**Disable natural promotion experiment** restores the original comparison for future promotions. It does not delete already spawned natural entities or cancel submitted batches; those retain native lifetime handling. The adapter attempts to restore the gate if native diagnostics become unavailable or the offline/editor context changes. A restore failure is reported and requires restarting. The switch is temporary and is not saved in configuration.

The edit is one byte in process memory, RVA `0x263984a`: the immediate in `cmp byte ptr [rsp+0x3a0],0` becomes `0xff`. The following `jz` therefore no longer skips for a cached editor boolean of 0 or 1. Enabling requires the exact executable hash, 25 native code guards, the expected online-flag address, online=0, editor=0, and an exact 14-byte gate match. Disabling restores the original byte only while the surrounding instruction matches. No disk executable is modified. This uses the existing protected code-write/cache-flush helper and a single-byte operand write, avoiding replacement of a multibyte instruction while worker jobs execute. The pass-through observer detours are installed only at startup and remain until process exit; disabling stops collection, not those pass-through calls.

## Run the experiment

1. Start `sc-offline.exe` from the new `compiled` folder, with its `dinput8.dll`, `data` directory and `sc-offline.ini` beside it. This folder is independent of the existing release binaries.
2. Enter the universe and stand outdoors with open ground ahead. Press **M**, then **DEV**.
3. Choose **Iron (ship mining)** or **Hadanite (FPS mining)**. Iron defaults to 25 m ahead and **0.50x size**; Hadanite defaults to 8 m and 1.00x. Adjust distance and size if needed. Size is bounded to 0.25–1.00x. Press **Spawn test rock**. Remove any previous rock first: the size control applies to the next spawn.
4. Wait for **Test rock is live**. Placement reuses `PlaceNearPlayer` and `SpawnEntityInPlayerZone`; it uses the current player zone and attempts ground placement. It does not bypass collisions or guarantee terrain placement inside a ship or building.
5. For Iron, board and operate a mining ship. For Hadanite, equip the appropriate FPS mining tool. Try normal targeting / scanning first. Use **Enable targeting / scanning (fallback)** only if a contact is missing, including after changing ships.
6. Close the menu, aim at the rock and try the normal mining/scanning controls. Check the ordinary mining display for mass/material information and scan progress. Reopen DEV or press **Refresh diagnostics** to inspect state.
7. Use **Remove test rock** before spawning another fixture. A pending/timed-out spawn retains its entity id so repeated clicks cannot create untracked duplicates. Removal is confirmed by entity lookup; failure retains the id for retry.

Actions and changes are logged to the launcher's `mod.log` as `[dev/mining]`. Include those lines and the relevant `Game.log` section when reporting a test. Fixture choice, entity id, component presence, authority, composition count, contacts, targeting and scan data are recorded.

## Capture the no-charge problem

1. Operate the Prospector mining laser or a MOLE mining turret, aim at the spawned Iron rock, and complete its normal scan.
2. Open **DEV**, press **Capture mining trace (30 seconds)**, close the menu and fire at the rock. Include several seconds at full throttle and a few seconds with the laser off.
3. Repeat on a natural planetary rock. No DEV spawn is required for this second trace: the trace reads each discovered ship mining controller's current mineable target. Natural rocks are never adopted as the removable test rock.
4. Provide the `[dev/mining/trace]`, `[dev/mining/controller]`, `[dev/mining/power]`, `[dev/mining/markers]`, `[dev/mining/marker]`, `[dev/mining/marker-types]` and nearby `[dev/mining]` lines from both runs. **Refresh diagnostics** also takes one controller/target sample.

The trace runs once per second, stops automatically after 30 seconds, and can be stopped early. It reads the current ship's mining controllers using the existing bounded ship-parts discovery. Up to eight controllers are recorded; `controllers=0` means discovery did not find one, not that the ship has no mining turret. A `target=0` can mean no valid mineable target handle, including a rejected stale generation. This controller trace currently covers ship mining, not handheld tool discovery.

**Inspect natural markers** works even when `target=0`. Use it while close to the natural ore markers; it is also run once at the start of each trace. It enumerates the local databanks, including parent and permanent radar entries, copies type-2 provider/location contacts, then checks their associated entity and components. `definitionRef=1 entity=0` means a class definition is available but there is no validated live entity association. Earlier builds called this field `staticRef`; that label was misleading because ordinary live contacts also retain class definitions. It does not identify a static mesh. `entity!=0 mineable=0` distinguishes a live entity without the mining component. `targetable`, scan data, flags, authority and component presence are recorded independently.

Enumeration retains at most 64 unique matching keys per bank and reads at most 4,096 callbacks per bank. Native enumeration itself runs to completion so its read locks are released. `skipped` and `truncated` report these limits; `unsupported` reports entry layouts/generations that were not read. Banks can share entries, and samples are not sorted by distance or tied exclusively to the aimed-at marker. The callback only copies fields; entity queries run after the native lock is released. A callback fault is contained until enumeration returns, then disables native diagnostics. This tool does not alter contacts, instantiate harvestables or force authority.

Controller records now include `scanEntry`, `scanEntryType` and `scanEntity` from the game's own scan-selection getters. These distinguish a selected radar entry from a resolved entity and from the controller's mineable `target`. A `scanned=1` value with `target=0` must not be interpreted as a completed scan of the rock currently under the crosshair; the observed natural trace retained that flag with no target or mass.

### Prospector trace: first confirmed charging bottleneck

The user supplied two logs, preserved under `analysis/mining-rnd/dev-size-markers/runtime/`. The spawned Iron rock had mass **38,958.9**, volume **8,450.97**, resistance **0**, capacity **389,588**, authority **1**, and decay rate **0.02**. At full throttle, the positive transfer samples reported a normalized input rate of **0.00600544**. Recent hit sources became 1, queued sources appeared, and `updateEnabled` became 1 during firing; they cleared after firing stopped. This confirms native hit delivery and activation rather than a universally missing mining callback.

The measured input is about **0.6005 percentage points/second**, versus **2 percentage points/second** of decay. It supplies only **30.03%** of the amount required to break even. Multiplying by capacity gives approximately **2,340** effective input power versus **7,792** decay power in the game's native units. Later HUD transfer samples became zero while `lastHitRate` remained at its previous value; that retained value is not independent proof of continuing throughput. The earlier simultaneous positive transfer and active hit samples already establish the insufficient-input condition.

The new half-size default passes scale through the existing spawn transform. Native initialization at `0x1444f0700` computes volume as geometry volume multiplied by scale cubed; mass then derives from volume and composition. At 0.50x and identical composition, the recorded rock would be about **4,870 mass**. Actual mass varies with the generated composition. There are no direct mass, resistance, laser power, update or authority overrides. Existing Build and ship callers retain the default 1.00x scale.

In the natural trace, every controller sample had `target=0 focused=0 hudMass=0 lastHitRate=0`, including during laser fire. This is a target-acquisition/entity-resolution failure before charge comparison can be made. Visible ore markers alone do not establish an initialized live mineable entity. The next evidence is the marker inspection; natural entity/provider authority is still unknown.

Each controller records authority, target id, firing/focus/scan/fracture state, throttle, HUD mass and resistance, HUD transfer, and the last reported normalized laser-hit rate. Both the spawned rock and controller targets record native mass, volume, base/modified resistance, charge capacity, normalized decay rate, charge, pending charge, update state, recent hit-source count and queued source count. `valid=0` means power metrics are incomplete; zeros in that sample are not measured physical zero values. Instantaneous queues may be empty between samples even while mining works.

Interpret these as evidence to localize the failure:

- A natural target with `authority=0` cannot pass the authority gates in native `OnHitByMiningLaser` / `Update`. This has not yet been observed; the spawned rocks already report `authority=1`.
- Firing with no recent hit sources and no reported hit rate points toward hit delivery / target acquisition. It does not establish a particular missing callback.
- Positive hit sources and a positive normalized hit rate, but insufficient rate relative to decay, support a power-versus-mass explanation. The recorded last rate can be stale or represent one controller; account for firing state and other active lasers.
- Positive hit activity without charge growth despite sufficient input warrants checking the update path and parameters. These diagnostics do not synthesize hits or force authority.

### Why Iron can stay at 0% resistance

`composition-iron.json` contains two `Iron_Ore` entries with different percentage ranges and qualities. `element-iron.json` defines `elementResistance = -0.4`. Native `0x1444f9250` starts the resistance aggregate at zero, combines these contributions and clamps the result to `[0, 1]`. In this all-Iron preset, changing composition percentages does not introduce a positive resistance contribution. The native getter at `0x144420070` applies modifiers and clamps the result again. This is consistent with the observed fixed 0%, without assuming a scan failure.

The native controller explicitly binds this getter to `depositResistance`; `0x1444259c0` is the **instability** getter. Ghidra's initial speculative filenames in the working investigation should not be used as semantic evidence.

Native charge processing at `0x144484ee0` scales delivered laser energy by `(1 - resistance)` and divides by rock capacity. `0x14452f990` subtracts decay before committing charge. The ship global parameters are `powerCapacityPerMass = 10` and `decayPerMass = 0.2`, corresponding to a base normalized decay of `0.02` (2 percentage points per second) for a valid capacity. **Zero resistance does not make a rock immune to the laser.** The subsequent Prospector trace establishes insufficient input relative to decay. The UI's exact difficulty-label formula remains unverified.

## Interpret the diagnostics

- **Live** means an entity lookup resolves the returned spawn id, not that mining is functional.
- **Mineable / harvestable / signature** report the corresponding native components. Missing mineable or signature components prevent contact registration.
- **Local authority** reads the native authority predicate. The adapter never changes authority.
- **Composition entries** is a bounded, read-only count of the native mineable vector. Zero means the native initializer has not populated it; unknown means the adapter is unavailable or the layout check failed.
- **Contacts / targetable** query the current local databanks after the asynchronous request. Counts may include banks sharing a parent; they are diagnostic observations, not unique independent entries.
- **Banks with scan data** reports the game's `HasScanData` result. Some data can exist before the scan completes; this is not a completion flag.
- **Linked scan tokens** is specifically `IsTokenGrantedForLinkedDataBank`, which can be false for an unlinked/root bank. Zero alone does not establish a scanning failure.
- **Request queued** means the native queue accepted it. A nonzero targetable count, populated composition, visible mining HUD and completed normal scan are separate runtime acceptance checks.

The adapter does not fill in material data, call the inferred composition initializer, force scan completion or globally patch authority. Empty composition, missing contact state or stalled scanning remain observable follow-up work.

## Implementation and binary contract

The predecessor's `analysis/mining-rnd/RECOVERY.md`, `FINDINGS.md`, fixture JSON and decompilations supplied the starting point. Additional analysis is saved in `analysis/mining-rnd/dev-contact/` at the release root. The inspected executable is:

```text
StarCitizen.exe SHA-256
3953f8b1a9894d5d9d1836e50939b726141f1829fe65a3a622d5acb2c6f89162
Preferred image base: 0x140000000
```

`dev.cpp` hashes the running executable's file during offline startup, verifies the in-memory entry bytes and entity-system global, then relocates these RVAs against the actual module base. The adapter and offset diagnostics fail closed on a mismatch; generic spawning still uses the existing signature-resolved mod APIs. The concrete `ObjectDataBank` vtable is also checked before native calls. An engine fault disables the adapter for the session and suspends automatic diagnostics.

| RVA | Native contract / evidence |
| --- | --- |
| `0x33591e0` | `SObjDBAction_OnFixedEntryAdd::QueueAction(CObjectDataBank*, IEntityPtr&, EFlags&)`; three arguments, queue copies the generation-bearing handle and 32-bit flags. |
| `0x3356460` | `SObjDBAction_OnFixedEntryRemove::QueueAction(CObjectDataBank*, SEntryId&)`; used for cleanup of the experiment's fixed entries. |
| `0x338f8b0` | Native entity-to-entry-id helper. Caller-provided 16-byte output in RDX, generation-bearing entity handle in R8; RCX is unused in this build. Avoids assuming harvestables always use raw entity ids as databank keys. |
| `0x33dd470` | `CObjectDataBank::IsEntry(SEntryId)`; 16-byte entry id passed indirectly. |
| `0x33de650` | `CObjectDataBank::IsTargetable(SEntryId, ETargetType)`; normal target type 0. |
| `0x33cea90` | `CObjectDataBank::HasScanData(SEntryId)`. |
| `0x33de9a0` | `CObjectDataBank::IsTokenGrantedForLinkedDataBank()`. |
| `0x441ce00` | Composition accessor corroborates mineable `+0x540`; end pointer `+0x548`, stride `0x58` corroborated by native traversal. |
| `0x4428da0` | Native mass getter, used by the mining controller at `0x61005d0`; float return in XMM0. Called only with present global parameters and a bounded composition vector. |
| `0x4420070` | Native resistance getter: component in RCX, apply-modifiers boolean in DL, float return in XMM0. |
| `0x326ed0` | Generation-aware component handle validation before following controller `+0x248` to a mineable. Owner entity and component identity are then rechecked. |
| `0x326f70` | Generation-aware entity handle validation for marker/scan entity associations. |
| `0x33bc910` | `CObjectDataBank::ForEachEntry(const CigFunction<void(CigPODWeakPtr<IObjectDatabankEntry>)>&)`. Stateless visitor: invoke pointer, manager sentinel 1, storage pointer; weak entry is passed by value in RCX. Includes parent and available permanent entries. |
| `0x609e590` | Controller's selected scan entry id getter; output is a 16-byte entry id in RDX. Uses databank slot `0x860`. |
| `0x609e4c0` | Controller's selected scan entity getter; output is an 8-byte generation-bearing handle in RDX. Uses databank slot `0x858`. |

Marker evidence is saved in `analysis/mining-rnd/dev-size-markers/` and corrected/extended in `dev-natural-promotion/`. Concrete entry vtable RVA `0x85c2e90` is verified before reading entry id `+0x18`, entity handle `+0x28`, class definition `+0x30` and flags `+0x90`, corroborated by entry creation `0x3395030`, permanent registration `0x3416330`, state `0x33c8ad0` and entity-to-entry-id `0x338f8b0`. Registration fills `+0x30` from entity slot `0x20` (class); `0x33c1e70` can derive mining/contact data from that definition without a live mineable. Only type-2 provider/location keys are now sampled, avoiding the previous flood of permanent navigation contacts. A type histogram still counts all supported entries within the read budget.

Power-trace evidence is saved in `analysis/mining-rnd/dev-power/`. Mineable offsets: params `+0x18`, global mining params at params `+0x28`, resistance `+0x1a8`, volume `+0x1cc`, pending normalized charge `+0x238`, update enabled `+0x268`, recent hit map size `+0x278`, queued per-laser sources `+0x2d8/+0x2e0` with stride `0x30`, and charge `+0x570`. Native hit/update functions corroborate these reads. Capacity and base decay are calculated from native mass and global parameters `+8/+0xc`; no fields are written.

Controller offsets are corroborated by `CSCItemMiningController::PostInitialize` (`0x60d1a80`) and the hit-rate binding at `0x60e7d00`: target mineable handle `+0x248`, last hit rate `+0x2c0`, focus `+0x761`, fracture mode `+0x8c9`, firing `+0x911`, scanned `+0x959`, throttle `+0xbe4`, displayed transfer `+0xd04`, HUD mass `+0xd4c`, HUD resistance `+0xe24`. The source entity/component is looked up again before reading a discovered controller. UI snapshots contain only copied values, not engine pointers.

The flags overload is corroborated by its embedded native C++ signature and assembly. `CPlayerMissionDataBankManagementComponent::AddObjectsToDataBank_Impl` calls the same fixed-entry family via databank virtual slot `0x780`. The plain overload at `0x3408b00` calls queue helper `0x3358b20`. The DEV adapter uses the flags overload, setting only `0x800`, which the entry targeting predicate at `0x33de940` explicitly checks. Fixed-entry handling at `0x34027b0` / `0x3388310` applies supplied flags, registers the fixed contact and resolves its native radar contact type. The user has since confirmed normal scanning and the full mining loop on the half-size Iron fixture.

Entity-system slot `0x128` supplies a real generation-bearing `IEntityPtr`; no handle is fabricated from a raw pointer. The existing mining target-changed callback at `0x60be8d0` corroborates entity slot `0x708` for the controlling player component. Databanks are looked up fresh on each action; the adapter retains owner entity ids, not component pointers. Active DEV engine operations run on the existing main-thread tick; the new pass-through observers execute on the original caller thread and only collect diagnostic values. An SRW lock protects the request and copied UI snapshot only, and is never held across engine calls.

The spawn path keeps the established `0x1000` flags. Natural harvestable flags `0x201000` and provider attributes remain research leads, not speculative changes in this test.

## Build and automated checks

Follow `build.md` for VS 2026. This machine has VS 2022 17.12, which supports `v143` but cannot load `.slnx`; both projects were built directly without changing their default toolset:

```powershell
# Run from source_code in a Developer PowerShell for Visual Studio.
$devOut = Join-Path (Get-Location) 'x64\Release\'
msbuild src/ChrisWareOffline.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 "/p:OutDir=$devOut" /m
msbuild launcher/sc-offline.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 "/p:OutDir=$devOut" /m
msbuild tools/tests/dev_lifecycle.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143
./tools/tests/x64/Release/dev_lifecycle.exe
```

The standalone fake-engine harness exercises the actual DEV request and lifecycle code: executable mismatch, invalid input, single-flight requests, distance clamping, duplicate suppression, late spawn, timeout retention, unavailable/failed/successful removal, and one-shot handling of an engine fault. Trace tests cover inspecting a natural target without adopting it, stale handle rejection, missing parameters, malformed composition vectors, automatic expiry, early stop and fault suspension. Marker tests cover scale forwarding, definition-only versus live mineable associations, stale entity generations, key deduplication, sample/read limits, contained callback faults and rejection of ordinary class-backed navigation contacts. Promotion tests cover offline/editor/fingerprint rejection, changed instruction rejection, exact single-byte enable/restore, idempotence, write failure, context/fault restoration even without a spawner, and visible restore failure with explicit retry. Observer tests cover inactive pass-through, unchanged arguments/results, actual candidate/request counting, rejection stages, read-fault containment, native-exception propagation and context cleanup, bounded/nonblocking sampling, concurrent accepted/rejected calls, activation/expiry and partial installation. Biome tests additionally verify all seven/nine cell/instance arguments, unchanged draw-only flags, mode-specific sampling, native request origin, exception cleanup, bounded per-stage samples and contention drops. There are 25 fingerprint checks and eight instruction-boundary checks against the exact native executable. These tests do not validate game ABI behavior or replace the in-game acceptance steps above.
