// Exercise the actual DEV request/lifecycle code with a fake engine. This is not
// an in-game mining test: native reads are replaced with fake engine functions.
#include "../../src/mining.cpp"
#include "../../src/dev.cpp"
#include <limits>
#include <initializer_list>

Section g_text, g_rdata;
const uint8_t* g_isOnlineFlag = nullptr;
TeleportApi g_tp;
static uint8_t editorFlag = 0, onlineFlag = 0, serverFlag = 1;
static bool ready = true, loaded = false, removable = true, lookupFault = false;
static int spawns = 0, removes = 0, lookups = 0, failures = 0;
static uintptr_t entityVtable[256] = {}, esVtable[256] = {};
static uintptr_t entityObject = reinterpret_cast<uintptr_t>(entityVtable);
static uintptr_t esObject = reinterpret_cast<uintptr_t>(esVtable);
static uintptr_t esPointer = reinterpret_cast<uintptr_t>(&esObject);
alignas(8) static uint8_t rockMemory[0x680], controllerMemory[0x2800], paramsMemory[0x80], globalMemory[0x270];
static uintptr_t controllerEntity = reinterpret_cast<uintptr_t>(entityVtable);
static bool discoverController = false, massFault = false;
static int massCalls = 0;
static uint64_t validTarget = 0;
static double spawnScale = 0;
static bool discoverMarkers = false, markerFault = false;
alignas(8) static uint8_t markerMemory[2][0xb0], bankMemory[0x1400];
static uintptr_t bankEntity = reinterpret_cast<uintptr_t>(entityVtable);
static int hookCalls = 0, hookFailure = -1;
template<class T> static void Put(uint8_t* buffer, size_t offset, T value) { memcpy(buffer + offset, &value, sizeof(value)); }

void Log(const char*, ...) {}
bool HookFunction(uint8_t*, size_t, void*, void**) { return hookCalls++ != hookFailure; }
bool SpawnerReady() { return ready; }
bool GetCVarNow(const char*, float&) { return false; }
bool BytesMatch(const uint8_t*, const char*) { return true; }
bool PlaceNearPlayer(double, double, double, double[3], double[4]) { return true; }
const char* SpawnEntityInPlayerZone(const char*, const double[3], const double[4], uint64_t& id, double scale) { ++spawns; id = 42; spawnScale = scale; return nullptr; }
uintptr_t EntityComponent(uintptr_t entity, const char* name) {
    if (!discoverController) return 0;
    if (discoverMarkers && entity == reinterpret_cast<uintptr_t>(&bankEntity) && !strcmp(name, "ObjectDataBank")) return reinterpret_cast<uintptr_t>(bankMemory);
    if (entity == reinterpret_cast<uintptr_t>(&entityObject) && !strcmp(name, "EntityComponentMineable")) return reinterpret_cast<uintptr_t>(rockMemory);
    if (entity == reinterpret_cast<uintptr_t>(&controllerEntity) && !strcmp(name, "SCItemMiningController")) return reinterpret_cast<uintptr_t>(controllerMemory);
    return 0;
}
uint64_t LocalPlayerEntityId() { return 0; }
uint64_t PlayerShipId() { return discoverController ? 100 : 0; }
uint64_t EntityIdOfComponent(uintptr_t c) {
    return c == reinterpret_cast<uintptr_t>(rockMemory) ? 77 : c == reinterpret_cast<uintptr_t>(controllerMemory) ? 80 : 0;
}
uint64_t EntityIdOfHandle(const void* h) {
    return (*static_cast<const uint64_t*>(h) & kPtrMask) == reinterpret_cast<uintptr_t>(&entityObject) ? 77 : 0;
}
int ShipPartComponents(uint64_t, const char*, uintptr_t* parts, char (*names)[96], int) {
    if (!discoverController) return 0;
    parts[0] = reinterpret_cast<uintptr_t>(controllerMemory);
    strcpy_s(names[0], "Test turret");
    return 1;
}
bool GetLocalPlayer(uintptr_t&, uintptr_t&) { return false; }
bool CanRemoveEntities() { return removable; }
void RemoveEntityById(uint64_t) { ++removes; }
static uintptr_t __fastcall Lookup(uintptr_t, uint64_t id) {
    ++lookups;
    if (lookupFault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    if (discoverController && id == 77) return reinterpret_cast<uintptr_t>(&entityObject);
    if (discoverController && id == 80) return reinterpret_cast<uintptr_t>(&controllerEntity);
    if (discoverMarkers && id == 100) return reinterpret_cast<uintptr_t>(&bankEntity);
    return id == 42 && loaded ? reinterpret_cast<uintptr_t>(&entityObject) : 0;
}
static const char* __fastcall Name(uintptr_t) { return "Natural test rock"; }
static bool __fastcall Authority(uintptr_t) { return true; }
static float __fastcall Mass(uintptr_t) {
    ++massCalls;
    if (massFault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    return 10000.0f;
}
static float __fastcall Resistance(uintptr_t, bool) { return 0.0f; }
static bool __fastcall ValidHandle(const uint64_t* handle) { return *handle == validTarget; }
static bool __fastcall ValidEntity(const uint64_t* h) { return *h == (reinterpret_cast<uintptr_t>(&entityObject) | (uint64_t(7) << 48)); }
static EntryId* __fastcall ScanEntry(uintptr_t, EntryId* id) { *id = {}; return id; }
static uint64_t* __fastcall ScanEntity(uintptr_t, uint64_t* h) { *h = 0; return h; }
static bool __fastcall EntryScan(uintptr_t, const EntryId*) { return true; }
static bool __fastcall EntryTargetable(uintptr_t, const EntryId*, uint8_t) { return false; }
static void __fastcall ForEachEntry(uintptr_t, const EntryVisitor* visitor) {
    if (markerFault) { visitor->invoke(1); return; }
    for (auto& buffer : markerMemory) visitor->invoke(reinterpret_cast<uintptr_t>(buffer + 8) | (uint64_t(7) << 48));
}
static void Check(bool condition, const char* name) {
    if (!condition) { printf("FAIL: %s\n", name); ++failures; }
}
static void Reset() {
    g_state = {}; g_published = {}; g_request = {}; g_api = {};
    editorFlag = onlineFlag = 0; serverFlag = 1;
    g_contactId = {}; g_registeredCount = 0;
    g_waitingSpawn = g_removing = g_diagnosticsSuspended = false;
    g_spawnAt = g_removeAt = g_lastRefresh = g_traceAt = 0;
    discoverController = massFault = false; massCalls = 0;
    discoverMarkers = markerFault = false; spawnScale = 0; g_markers = {};
    ready = removable = true; loaded = lookupFault = false;
    spawns = removes = lookups = 0;
    esVtable[0x120 / 8] = reinterpret_cast<uintptr_t>(&Lookup);
    g_tp.ok = true; g_tp.entitySystem = &esPointer;
    Mining::verified = Mining::ready = Mining::faulted = false;
    Mining::cells = Mining::promoted = Mining::faults = 0;
    Mining::server = &serverFlag; Mining::editor = &editorFlag; Mining::online = &onlineFlag;
    Mining::originalCell = nullptr; Mining::lastState = -1; Mining::lastTraceAt = 0;
    hookCalls = 0; hookFailure = -1;
}
static void SetUpController() {
    discoverController = true;
    memset(rockMemory, 0, sizeof(rockMemory)); memset(controllerMemory, 0, sizeof(controllerMemory));
    memset(paramsMemory, 0, sizeof(paramsMemory)); memset(globalMemory, 0, sizeof(globalMemory));
    entityVtable[0x78 / 8] = reinterpret_cast<uintptr_t>(&Name);
    entityVtable[0x7a8 / 8] = reinterpret_cast<uintptr_t>(&Authority);
    Put(rockMemory, 0x18, reinterpret_cast<uintptr_t>(paramsMemory));
    Put(paramsMemory, 0x28, reinterpret_cast<uintptr_t>(globalMemory));
    Put(globalMemory, 8, 10.0f); Put(globalMemory, 0xc, 0.2f);
    Put(rockMemory, 0x570, 0.25f); Put(rockMemory, 0x1cc, 100.0f);
    Put(rockMemory, 0x278, uint64_t(1));
    validTarget = reinterpret_cast<uintptr_t>(rockMemory) | (uint64_t(3) << 48);
    Put(controllerMemory, 0x248, validTarget);
    Put(controllerMemory, 0x911, uint8_t(1));
    Put(controllerMemory, 0xbe4, 1.0f);
    Put(controllerMemory, 0x2c0, 0.01f);
    g_api.mass = &Mass; g_api.resistance = &Resistance; g_api.validHandle = &ValidHandle;
    g_api.scanEntry = &ScanEntry; g_api.scanEntity = &ScanEntity; g_api.validEntity = &ValidEntity;
    g_api.forEachEntry = &ForEachEntry; g_api.targetable = &EntryTargetable; g_api.hasScan = &EntryScan;
    g_state.nativeReady = true;
    Publish();
}
static void SetUpMarkers() {
    discoverMarkers = true;
    memset(markerMemory, 0, sizeof(markerMemory)); memset(bankMemory, 0, sizeof(bankMemory));
    Put(bankMemory, 0, g_api.base + 0x85c7630);
    for (int i = 0; i < 2; ++i) {
        Put(markerMemory[i], 4, uint16_t(7));
        Put(markerMemory[i], 8, g_api.base + 0x85c2e90);
        Put(markerMemory[i], 8 + 0x18, EntryId{uint64_t(101+i), 2, 0});
        Put(markerMemory[i], 8 + 0x30, uintptr_t(1));
    }
    Put(markerMemory[1], 8 + 0x28, reinterpret_cast<uintptr_t>(&entityObject) | (uint64_t(7) << 48));
}
alignas(8) static uint8_t miningBuilder[0x40], miningCell[0x16b0];
static int nativeCellCalls = 0;
static uint32_t nativeCellFlags = 0;
static bool nativeCellThrow = false;
static uintptr_t MB() { return reinterpret_cast<uintptr_t>(miningBuilder); }
static uintptr_t MC() { return reinterpret_cast<uintptr_t>(miningCell); }
static void __fastcall NativeMiningCell(uintptr_t builder, uintptr_t cell, uintptr_t planet, uint32_t lod,
    uint32_t flags, uint64_t page, uint8_t option) {
    ++nativeCellCalls; nativeCellFlags = flags;
    Check(builder == MB() && cell == MC() && planet == 101 && lod == 1
        && page == 0x1122334455667788ull && option == 1, "automatic mining preserves all non-flag arguments");
    if (nativeCellThrow) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    if (flags & 8) Put(miningCell, 0x16a8, int(1)); // Native completion simulation.
}
static void CallMiningCell() { Mining::CellHook(MB(), MC(), 101, 1, 6, 0x1122334455667788ull, 1); }
static bool NativeExceptionPropagates() {
    __try { CallMiningCell(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
static void TestAutomaticMining() {
    Reset();
    Put(miningBuilder,8,int(0)); Put(miningBuilder,0x28,uint8_t(1)); Put(miningCell,0x16a8,int(100));
    ResolveMiningApi();
    Check(!MiningBuildVerified() && !Mining::ready && hookCalls == 0, "wrong executable cannot install automatic mining");
    Mining::verified = Mining::ready = true;
    Mining::originalCell = NativeMiningCell;
    nativeCellCalls = 0; nativeCellThrow = false;
    serverFlag = 0;
    CallMiningCell();
    Check(nativeCellCalls == 1 && nativeCellFlags == 6 && Mining::promoted == 0, "startup outside server context is pass-through");
    serverFlag = 1;
    // No ProcessDev, trace or menu action between context readiness and this call.
    CallMiningCell();
    Check(nativeCellCalls == 2 && nativeCellFlags == 14 && Mining::promoted == 1 && spawns == 0,
        "natural spawning starts automatically at the first eligible native call");
    CallMiningCell();
    Check(nativeCellCalls == 3 && nativeCellFlags == 6 && Mining::promoted == 1,
        "native completed LOD suppresses another promotion");
    Put(miningCell,0x16a8,int(100));
    for (uint32_t flags : {0u,2u,4u,7u,14u,0x106u})
        Check(Mining::CellFlags(MB(),MC(),1,flags) == flags, "only exact draw/physics flags are augmented");
    for (int type : {-1,1,2,3}) {
        Put(miningBuilder,8,type); Check(Mining::CellFlags(MB(),MC(),1,6) == 6, "other builder types unchanged");
    }
    Put(miningBuilder,8,int(0));
    for (int oldLod : {-1,0,1,101}) {
        Put(miningCell,0x16a8,oldLod); Check(Mining::CellFlags(MB(),MC(),1,6) == 6, "completed or unknown LOD unchanged");
    }
    Put(miningCell,0x16a8,int(100));
    Check(Mining::CellFlags(MB(),MC(),0,6) == 14 && Mining::CellFlags(MB(),MC(),1,6) == 14
        && Mining::CellFlags(MB(),MC(),2,6) == 6, "only native close LODs are eligible");
    Put(miningBuilder,0x28,uint8_t(0)); Check(Mining::CellFlags(MB(),MC(),1,6) == 6, "disabled builder ignored"); Put(miningBuilder,0x28,uint8_t(1));
    onlineFlag = 1; Check(Mining::CellFlags(MB(),MC(),1,6) == 6, "online context rejected before next UI tick"); onlineFlag = 0;
    editorFlag = 1; Check(Mining::CellFlags(MB(),MC(),1,6) == 6, "editor context rejected"); editorFlag = 0;
    serverFlag = 0; Check(Mining::CellFlags(MB(),MC(),1,6) == 6, "non-server context rejected"); serverFlag = 1;
    ready = false; Check(Mining::CellFlags(MB(),MC(),1,6) == 6, "unavailable spawner rejects promotion"); ready = true;
    g_tp.ok = false; Check(Mining::CellFlags(MB(),MC(),1,6) == 6, "unavailable offline API rejects promotion"); g_tp.ok = true;
    Mining::ready = false; Check(Mining::CellFlags(MB(),MC(),1,6) == 6, "missing hook never reports eligible"); Mining::ready = true;
    Check(Mining::CellFlags(0,MC(),1,6) == 6 && Mining::CellFlags(MB(),0,1,6) == 6, "null inputs unchanged");
    ProcessDev(1000);
    Check(g_state.mining.active && !g_state.tracing && Mining::promoted == 1, "automatic status appears without a DEV request");
    SetUpController(); Menu_RequestDev(DevAction::StartTrace); ProcessDev(2000);
    Menu_RequestDev(DevAction::StopTrace); ProcessDev(3000);
    Check(g_state.mining.active && !g_state.tracing && Mining::CellFlags(MB(),MC(),1,6) == 14,
        "ending a diagnostic trace does not disable natural mining");
    g_state.nativeReady = false; ProcessDev(4000);
    Check(g_state.mining.active, "DEV adapter availability is independent of verified natural mining");
    nativeCellThrow = true;
    Check(NativeExceptionPropagates() && nativeCellCalls == 4 && !Mining::faulted,
        "original native exception propagates after exactly one call");
    nativeCellThrow = false;
    Check(Mining::CellFlags(1,MC(),1,6) == 6 && Mining::faulted && Mining::faults == 1,
        "unreadable eligibility fails closed and latches for the session");
    Check(Mining::CellFlags(MB(),MC(),1,6) == 6, "read fault cannot silently re-enable automatic spawning");
    Reset(); Mining::verified = Mining::ready = true;
    Mining::server = reinterpret_cast<const uint8_t*>(1);
    Check(Mining::CellFlags(MB(),MC(),1,6) == 6 && Mining::faulted, "context read faults also fail closed");
    Reset();
}
int main() {
    Reset();
    ResolveDevApi();
    Check(!g_state.nativeReady && g_state.spawnerReady, "different executable fails closed, generic spawn stays available");
    Menu_RequestDev(DevAction::SpawnRock, -1);
    Menu_RequestDev(DevAction::SpawnRock, 2);
    Menu_RequestDev(DevAction::SpawnRock, 0, std::numeric_limits<float>::quiet_NaN());
    Menu_RequestDev(DevAction::SpawnRock, 0, 25, std::numeric_limits<float>::quiet_NaN());
    Menu_RequestDev(DevAction::SpawnRock, 0, 25, 0.0f);
    Menu_RequestDev(DevAction::SpawnRock, 0, 25, 1.1f);
    Check(g_request.action == DevAction::None, "invalid fixtures / NaN rejected");
    Menu_RequestDev(DevAction::SpawnRock, 0, 999, 0.5f);
    Menu_RequestDev(DevAction::SpawnRock, 1, 10);
    DevSnapshot snapshot;
    Menu_DevSnapshot(snapshot);
    Check(snapshot.busy && g_request.fixture == 0 && g_request.distance == 150, "request is clamped, single-flight and visible as busy");
    ProcessDev(1000);
    Check(spawns == 1 && g_state.rockId == 42 && g_state.busy && spawnScale == 0.5 && g_state.spawnScale == 0.5f,
        "scaled spawn forwards size to the native spawn path and retains id while loading");
    Menu_RequestDev(DevAction::SpawnRock, 1);
    ProcessDev(2000);
    Check(spawns == 1, "double-click while loading cannot create another rock");
    ProcessDev(31000);
    Check(!g_state.busy && g_state.rockId == 42 && !g_state.live, "timeout retains id for cleanup");
    Menu_RequestDev(DevAction::SpawnRock, 1);
    ProcessDev(32000);
    Check(spawns == 1, "timeout does not allow duplicate spawning");
    loaded = true;
    Menu_RequestDev(DevAction::Refresh);
    ProcessDev(33000);
    Check(g_state.live, "late spawn can still be inspected");
    removable = false;
    Menu_RequestDev(DevAction::RemoveRock);
    ProcessDev(34000);
    Check(removes == 0 && g_state.rockId == 42, "unavailable removal retains tracked rock");
    removable = true;
    Menu_RequestDev(DevAction::RemoveRock);
    ProcessDev(35000);
    Check(removes == 1 && g_state.busy, "removal waits for disappearance");
    ProcessDev(45000);
    Check(!g_state.busy && g_state.rockId == 42, "failed removal does not forget rock");
    Menu_RequestDev(DevAction::RemoveRock);
    ProcessDev(46000);
    loaded = false;
    ProcessDev(47000);
    Check(!g_state.busy && g_state.rockId == 0, "confirmed removal releases tracked slot");
    Menu_RequestDev(DevAction::SpawnRock, 1, 8);
    loaded = true;
    ProcessDev(48000);
    Check(spawns == 2 && g_state.live && !g_state.busy, "second fixture allowed after cleanup");
    lookupFault = true;
    Menu_RequestDev(DevAction::Refresh);
    ProcessDev(49000);
    const int atFault = lookups;
    ProcessDev(50000);
    Check(!g_state.nativeReady && g_diagnosticsSuspended && lookups == atFault && g_state.rockId == 42,
        "engine fault disables adapter, stops automatic probes and retains id");
    Reset();
    Menu_RequestDev(DevAction::StartTrace);
    ProcessDev(1000);
    Check(!g_state.tracing, "trace fails closed when native checks are unavailable");
    SetUpController();
    Menu_RequestDev(DevAction::StartTrace);
    ProcessDev(2000);
    const auto& natural = g_state.controllers[0].target;
    Check(g_state.tracing && g_state.traceSeconds == 30 && g_state.controllerCount == 1 && natural.id == 77
        && natural.valid && natural.mass == 10000 && natural.capacity == 100000
        && fabsf(natural.decayPerSecond - 0.02f) < 0.00001f && natural.charge == 0.25f
        && g_state.controllers[0].firing && spawns == 0 && removes == 0 && g_state.rockId == 0,
        "trace inspects a natural controller target without adopting it as the removable test rock");
    Put(controllerMemory, 0x248, validTarget ^ (uint64_t(1) << 48));
    ProcessDev(3000);
    Check(g_state.controllers[0].target.id == 0, "stale target generation is rejected and previous metrics cleared");
    Put(controllerMemory, 0x248, validTarget);
    Put(paramsMemory, 0x28, uintptr_t(0));
    const int beforeMissingParams = massCalls;
    ProcessDev(4000);
    Check(!g_state.controllers[0].target.valid && !g_state.controllers[0].target.globalParams && massCalls == beforeMissingParams,
        "missing global parameters are reported without calling the native mass getter");
    Put(paramsMemory, 0x28, reinterpret_cast<uintptr_t>(globalMemory));
    Put(rockMemory, 0x540, uintptr_t(0x1000)); Put(rockMemory, 0x548, uintptr_t(0x1001));
    ProcessDev(5000);
    Check(g_state.controllers[0].target.composition == -1 && !g_state.controllers[0].target.valid && massCalls == beforeMissingParams,
        "malformed composition vector is rejected before native traversal");
    Put(rockMemory, 0x540, uintptr_t(0)); Put(rockMemory, 0x548, uintptr_t(0));
    ProcessDev(32000);
    const int atExpiry = lookups;
    ProcessDev(33000);
    Check(!g_state.tracing && g_state.traceSeconds == 0 && lookups == atExpiry, "trace expires after 30 seconds with no unbounded polling");
    Menu_RequestDev(DevAction::StartTrace);
    ProcessDev(34000);
    Menu_RequestDev(DevAction::StopTrace);
    ProcessDev(35000);
    Check(!g_state.tracing && g_state.traceSeconds == 0, "trace can be stopped early");
    Menu_RequestDev(DevAction::StartTrace);
    massFault = true;
    ProcessDev(36000);
    const int atTraceFault = lookups;
    ProcessDev(37000);
    Check(!g_state.tracing && !g_state.nativeReady && g_diagnosticsSuspended && lookups == atTraceFault,
        "trace engine fault disables native reads and stops further polling");
    Reset(); SetUpController(); SetUpMarkers();
    Menu_RequestDev(DevAction::InspectMarkers);
    ProcessDev(1000);
    Check(g_state.nativeReady && g_state.markersInspected && g_state.markerSamples == 2 && g_state.markerLive == 1
        && g_state.markerMineable == 1 && g_state.rockId == 0 && spawns == 0 && removes == 0,
        "marker census distinguishes static-only contact from live mineable without adopting either");
    Put(markerMemory[1], 8 + 0x28, reinterpret_cast<uintptr_t>(&entityObject) | (uint64_t(8) << 48));
    Menu_RequestDev(DevAction::InspectMarkers); ProcessDev(2000);
    Check(g_state.markerSamples == 2 && g_state.markerLive == 0, "stale entity generation is rejected in marker samples");
    g_markers = {};
    const uint64_t marker = reinterpret_cast<uintptr_t>(markerMemory[0] + 8) | (uint64_t(7) << 48);
    CollectMarker(marker); CollectMarker(marker);
    Check(g_markers.count == 1, "duplicate provider marker keys are deduplicated per bank");
    g_markers = {};
    for (int i = 0; i < 65; ++i) { Put(markerMemory[0], 8 + 0x18, EntryId{uint64_t(101+i), 2, 0}); CollectMarker(marker); }
    Check(g_markers.count == 64 && g_markers.skipped == 1, "marker sample storage has a hard limit");
    g_markers.visited = 4096;
    CollectMarker(1);
    Check(!g_markers.fault && g_markers.skipped == 2, "entry read budget stops dereferencing after 4096 callbacks");
    markerFault = true;
    Menu_RequestDev(DevAction::StartTrace); ProcessDev(3000);
    Check(!g_state.nativeReady && !g_state.tracing && g_diagnosticsSuspended && g_markers.fault,
        "callback fault is contained until native enumeration returns, then disables diagnostics");
    Reset(); SetUpController(); SetUpMarkers();
    Put(markerMemory[0], 8 + 0x18, EntryId{101, 1, 0});
    Menu_RequestDev(DevAction::InspectMarkers); ProcessDev(1000);
    Check(g_state.markerSamples == 1, "class definitions on ordinary contacts do not crowd out provider markers");
    TestAutomaticMining();
    printf("DEV lifecycle and automatic mining: %s (%d failures)\n", failures ? "FAILED" : "PASS", failures);
    return failures ? 1 : 0;
}
