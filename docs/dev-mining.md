# DEV mining tools

Natural rock spawning is automatic; see [Natural mining](natural-mining.md) for its implementation, confirmed savepoint and rollback steps. DEV shows its current status without an enable switch. The earlier V5 experiment and verbose scheduling probes have been removed.

## Test fixture

Open **M > DEV**, choose Iron (ship mining) or Hadanite (FPS mining), then spawn a test rock in an open area. Iron defaults to 0.50x size, suitable for testing a single mining laser; Hadanite defaults to 1.00x. Size applies to the next spawn and uses the native transform/volume calculation. Actual mass varies with composition. Remove the current fixture before spawning another.

The fixture uses the existing Build/vehicle spawn-batch path in the player's current zone. Ground placement is best effort. Try normal targeting and scanning first; **Enable targeting / scanning (fallback)** is available if contact registration is missing. Natural rocks are never adopted as the removable test fixture.

The user confirmed scanning, fracture, extraction and Prospector cargo collection for half-size Iron. Full-size Iron was too massive for the tested laser: normalized input was about 0.006005/s against decay 0.02/s. Its zero resistance is consistent with the all-Iron preset's native clamping and does not imply laser immunity.

## Inspect and trace

**Inspect natural markers** records nearby available provider/location contacts even when the mining controller has no target. `entity=0 mineable=0` means no validated live entity association; a radar marker alone is insufficient. `entity!=0 mineable=1`, controller focus/scan and a responsive charge bar provide stronger evidence. Databanks may share entries, so record counts are not unique rock counts.

**Capture mining trace (30 seconds)** samples the current ship's mining controllers and their native targets once per second. Close the menu, aim and fire; the trace expires automatically or can be stopped early. It records mass, composition, resistance, charge, recent hit sources, input/decay and scan selection. It also enables compact `[mining/trace]` counts for automatic natural spawning. Starting or stopping a trace does not enable or disable spawning.

**Refresh diagnostics** takes a single current sample. DEV actions run on the existing main-thread tick; the menu reads a copied snapshot. Generation-bearing handles and relevant vtables are checked before native reads. A diagnostic fault suspends DEV reads for the session while retaining the test fixture id for cleanup. The independent natural-mining hook has its own context/read-fault checks.

## Native diagnostic contract

`mining.cpp` verifies the executable fingerprint once; `dev.cpp` independently verifies the following 15 entry signatures and entity-system address. Source offsets and ABI details are specific to that executable. Original decompilations and investigation history remain in local `analysis/mining-rnd/`, outside Git.

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

Entity-system slot `0x128` supplies a real generation-bearing `IEntityPtr`; no handle is fabricated from a raw pointer. The existing mining target-changed callback at `0x60be8d0` corroborates entity slot `0x708` for the controlling player component. Databanks are looked up fresh on each action; the adapter retains owner entity ids, not component pointers. Active DEV engine operations run on the existing main-thread tick; the automatic mining hook is separate and described in natural-mining.md. An SRW lock protects the request and copied UI snapshot only, and is never held across engine calls.

The spawn path keeps the established `0x1000` flags. Natural harvestable flags `0x201000` and provider attributes remain research leads, not speculative changes in this test.

## Build and tests

Follow [build.md](build.md). With VS 2022, build the projects directly using v143:

```powershell
# From source_code, in a Visual Studio Developer PowerShell.
$miningOut = Join-Path (Get-Location) 'x64\Release\'
msbuild src/ChrisWareOffline.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 "/p:OutDir=$miningOut" /m
msbuild launcher/sc-offline.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 "/p:OutDir=$miningOut" /m
$miningTests = Join-Path (Get-Location) 'x64\tests\'
msbuild tools/tests/dev_lifecycle.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 "/p:OutDir=$miningTests"
./x64/tests/dev_lifecycle.exe
```

The harness includes the actual DEV and automatic-mining implementations with fake engine boundaries. It covers fixture lifecycle/timeout/removal, scale forwarding, stale handles, bounded marker enumeration, malformed mining parameters, trace expiry/fault containment, automatic spawning without UI input, eligibility exclusions, exact native argument forwarding and native/read exception behavior. It does not replace in-game validation of startup, streaming or extraction.
