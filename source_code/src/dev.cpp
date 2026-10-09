#include "dev.h"
#include "spawner.h"
#include "build.h"
#include "teleport.h"
#include "npc.h"
#include "patches.h"
#include "hooks.h"
#include <cmath>
#include <cstdarg>

// Validated against the executable and native callers documented in docs/dev-mining.md.
// These are deliberately build-specific. A different executable disables the native
// adapter and offset-based diagnostics; the existing generic spawner remains usable.
static constexpr const char* kRocks[] = { "MineableRock_SurfaceCommon_Iron", "MineableRock_FPS_Hadanite" };
struct EntryId { uint64_t value = 0; uint32_t type = 0, extra = 0; };
static_assert(sizeof(EntryId) == 16);
using QueueContactFn = bool(__fastcall*)(uintptr_t, const uint64_t*, const uint32_t*);
using RemoveContactFn = bool(__fastcall*)(uintptr_t, const EntryId*);
using EntryIdFn = EntryId*(__fastcall*)(void*, EntryId*, uint64_t);
using EntryQueryFn = bool(__fastcall*)(uintptr_t, const EntryId*);
using TargetableFn = bool(__fastcall*)(uintptr_t, const EntryId*, uint8_t);
using TokenFn = bool(__fastcall*)(uintptr_t);
using MassFn = float(__fastcall*)(uintptr_t);
using ResistanceFn = float(__fastcall*)(uintptr_t, bool);
using ValidHandleFn = bool(__fastcall*)(const uint64_t*);
using TargetEntryFn = EntryId*(__fastcall*)(uintptr_t, EntryId*);
using TargetEntityFn = uint64_t*(__fastcall*)(uintptr_t, uint64_t*);
struct EntryVisitor { void(__fastcall* invoke)(uint64_t); uintptr_t manager; void* storage; };
using ForEachEntryFn = void(__fastcall*)(uintptr_t, const EntryVisitor*);
static struct {
    uintptr_t base = 0;
    QueueContactFn add = nullptr;
    RemoveContactFn remove = nullptr;
    EntryIdFn entryId = nullptr;
    EntryQueryFn contains = nullptr, hasScan = nullptr;
    TargetableFn targetable = nullptr;
    TokenFn token = nullptr;
    MassFn mass = nullptr;
    ResistanceFn resistance = nullptr;
    ValidHandleFn validHandle = nullptr;
    ValidHandleFn validEntity = nullptr;
    TargetEntryFn scanEntry = nullptr;
    TargetEntityFn scanEntity = nullptr;
    ForEachEntryFn forEachEntry = nullptr;
} g_api;

static SRWLOCK g_lock = SRWLOCK_INIT;
static DevSnapshot g_state, g_published;
static struct { DevAction action; int fixture; float distance, scale; } g_request = {};
static DWORD g_spawnAt = 0, g_removeAt = 0, g_lastRefresh = 0;
static bool g_waitingSpawn = false, g_removing = false;
static bool g_diagnosticsSuspended = false;
static DWORD g_traceAt = 0;
static EntryId g_contactId;
static uint64_t g_registeredBanks[8] = {};
static int g_registeredCount = 0;
const char* DevRockClass(int fixture) { return fixture >= 0 && fixture < 2 ? kRocks[fixture] : ""; }

static void Publish() {
    AcquireSRWLockExclusive(&g_lock);
    g_published = g_state;
    ReleaseSRWLockExclusive(&g_lock);
}

void Menu_DevSnapshot(DevSnapshot& out) {
    AcquireSRWLockShared(&g_lock);
    out = g_published;
    out.busy = out.busy || g_request.action != DevAction::None;
    ReleaseSRWLockShared(&g_lock);
}

void Menu_RequestDev(DevAction action, int fixture, float distance, float scale) {
    if (action == DevAction::SpawnRock && (fixture < 0 || fixture >= 2 || !std::isfinite(distance)
        || !std::isfinite(scale) || scale < 0.25f || scale > 1.0f)) return;
    if (distance < 5) distance = 5;
    if (distance > 150) distance = 150;
    AcquireSRWLockExclusive(&g_lock);
    if (!g_published.busy && g_request.action == DevAction::None)
        g_request = { action, fixture, distance, scale };
    ReleaseSRWLockExclusive(&g_lock);
}

static void Status(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_state.status, sizeof(g_state.status), fmt, args);
    va_end(args);
    Log("[dev/mining] %s", g_state.status);
}

static bool CheckNativeCode() {
    const struct { uintptr_t rva; const char* bytes; } checks[] = {
        { 0x33591e0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48" },
        { 0x3356460, "40 53 55 48 83 EC 48 48 8D 05 B2 05 28 05 48 8B" },
        { 0x338f8b0, "4C 89 44 24 18 55 56 57 48 8D 6C 24 B9 48 81 EC" },
        { 0x33dd470, "40 53 56 57 41 56 48 83 EC 48 48 8D 05 3F 69 1F" },
        { 0x33de650, "48 89 5C 24 20 55 57 41 54 41 56 41 57 48 8B EC" },
        { 0x33cea90, "40 53 57 41 56 48 83 EC 40 48 8D 05 C0 47 20 05" },
        { 0x33de9a0, "40 53 48 83 EC 30 48 8D 05 F3 55 1F 05 48 8B D9" },
        { 0x441ce00, "48 8D 81 40 05 00 00 C3" },
        { 0x4428da0, "40 57 48 81 EC 90 00 00 00 48 8B F9 E8 6F 99 FA" },
        { 0x4420070, "84 D2 74 2B C5 FA 10 81 D8 00 00 00 C5 FA 59 89" },
        { 0x326ed0, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57" },
        { 0x326f70, "48 89 5C 24 08 57 48 83 EC 20 4C 8B 09 48 8B F9" },
        { 0x33bc910, "40 55 57 48 83 EC 38 48 8D 05 52 73 21 05 48 89" },
        { 0x609e590, "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57" },
        { 0x609e4c0, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83" },
    };
    __try {
        for (const auto& check : checks) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(g_api.base + check.rva);
            if (p < g_text.base || p + 16 > g_text.base + g_text.size || !BytesMatch(p, check.bytes)) return false;
        }
        return g_tp.entitySystem == reinterpret_cast<uintptr_t*>(g_api.base + 0x9e2e708);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void ResolveDevApi() {
    g_api.base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_state.spawnerReady = SpawnerReady();
    g_state.nativeReady = MiningBuildVerified() && CheckNativeCode();
    if (g_state.nativeReady) {
        g_api.add = reinterpret_cast<QueueContactFn>(g_api.base + 0x33591e0);
        g_api.remove = reinterpret_cast<RemoveContactFn>(g_api.base + 0x3356460);
        g_api.entryId = reinterpret_cast<EntryIdFn>(g_api.base + 0x338f8b0);
        g_api.contains = reinterpret_cast<EntryQueryFn>(g_api.base + 0x33dd470);
        g_api.targetable = reinterpret_cast<TargetableFn>(g_api.base + 0x33de650);
        g_api.hasScan = reinterpret_cast<EntryQueryFn>(g_api.base + 0x33cea90);
        g_api.token = reinterpret_cast<TokenFn>(g_api.base + 0x33de9a0);
        g_api.mass = reinterpret_cast<MassFn>(g_api.base + 0x4428da0);
        g_api.resistance = reinterpret_cast<ResistanceFn>(g_api.base + 0x4420070);
        g_api.validHandle = reinterpret_cast<ValidHandleFn>(g_api.base + 0x326ed0);
        g_api.validEntity = reinterpret_cast<ValidHandleFn>(g_api.base + 0x326f70);
        g_api.forEachEntry = reinterpret_cast<ForEachEntryFn>(g_api.base + 0x33bc910);
        g_api.scanEntry = reinterpret_cast<TargetEntryFn>(g_api.base + 0x609e590);
        g_api.scanEntity = reinterpret_cast<TargetEntityFn>(g_api.base + 0x609e4c0);
        strcpy_s(g_state.apiStatus, "Native contact adapter: executable fingerprint and code checks passed.");
    } else {
        strcpy_s(g_state.apiStatus, "Native contact adapter unavailable: executable fingerprint or code checks differ. Generic spawning is still available.");
    }
    Log("[dev/mining] %s", g_state.apiStatus);
    Publish();
}

static uintptr_t Entity(uint64_t id) {
    return id && g_tp.entitySystem && *g_tp.entitySystem ? VCall<uintptr_t>(*g_tp.entitySystem, 0x120, id) : 0;
}

static uintptr_t Databank(uint64_t owner) {
    const uintptr_t entity = Entity(owner);
    const uintptr_t bank = entity ? EntityComponent(entity, "ObjectDataBank") : 0;
    // Reject interface subobjects and changed/derived layouts before using native methods.
    return bank && Rd<uintptr_t>(bank) == g_api.base + 0x85c7630 ? bank : 0;
}

static int CurrentBanks(uint64_t owners[3]) {
    uint64_t candidates[3] = { LocalPlayerEntityId(), PlayerShipId(), 0 };
    uintptr_t actor = 0, entity = 0;
    if (GetLocalPlayer(actor, entity)) {
        // The mining controller's OnDataBankScanningTargetChanged uses this same
        // IEntity slot to reach the controlling player component, then its owner.
        const uintptr_t player = VCall<uintptr_t>(entity, 0x708);
        if (player) candidates[2] = EntityIdOfComponent(player);
    }
    int count = 0;
    for (uint64_t id : candidates) {
        bool duplicate = false;
        for (int i = 0; i < count; ++i) if (owners[i] == id) duplicate = true;
        if (id && !duplicate && Databank(id)) owners[count++] = id;
    }
    return count;
}

static int VectorCount(uintptr_t object, size_t offset, size_t stride, size_t limit) {
    const uintptr_t begin = Rd<uintptr_t>(object + offset), end = Rd<uintptr_t>(object + offset + 8);
    if ((!begin && !end) || (begin && end >= begin && (end - begin) % stride == 0 && (end - begin) / stride <= limit))
        return static_cast<int>((end - begin) / stride);
    return -1;
}

// Read the same mass / resistance used by the native mining controller. Never
// modify rock parameters or call its composition / hit / update entry points.
static void ReadRock(uint64_t id, DevRockMetrics& out) {
    out = {};
    if (!g_state.nativeReady || !id) return;
    const uintptr_t entity = Entity(id);
    const uintptr_t rock = entity ? EntityComponent(entity, "EntityComponentMineable") : 0;
    if (!rock) return;
    out.id = id;
    if (const char* name = VCall<const char*>(entity, 0x78)) strncpy_s(out.name, name, _TRUNCATE);
    out.authority = VCall<bool>(entity, 0x7A8) ? 1 : 0;
    out.composition = VectorCount(rock, 0x540, 0x58, 128);
    out.queuedHitSources = VectorCount(rock, 0x2d8, 0x30, 128);
    const uint64_t recent = Rd<uint64_t>(rock + 0x278);
    out.recentHitSources = recent <= 128 ? static_cast<int>(recent) : -1;
    out.updateEnabled = Rd<uint8_t>(rock + 0x268) != 0;
    out.charge = Rd<float>(rock + 0x570);
    out.pendingCharge = Rd<float>(rock + 0x238);
    out.volume = Rd<float>(rock + 0x1cc);
    out.baseResistance = Rd<float>(rock + 0x1a8);
    const uintptr_t params = Rd<uintptr_t>(rock + 0x18);
    const uintptr_t global = params ? Rd<uintptr_t>(params + 0x28) : 0;
    out.globalParams = global != 0;
    if (!global || out.composition < 0) return;
    out.mass = g_api.mass(rock);
    out.resistance = g_api.resistance(rock, true);
    out.capacity = out.mass * Rd<float>(global + 8);
    const float decayPower = out.mass * Rd<float>(global + 0xc);
    if (std::isfinite(out.capacity) && out.capacity > 0) out.decayPerSecond = decayPower / out.capacity;
    out.valid = std::isfinite(out.mass) && out.mass > 0 && std::isfinite(out.resistance)
        && std::isfinite(out.capacity) && out.capacity > 0 && std::isfinite(decayPower)
        && std::isfinite(out.charge) && std::isfinite(out.volume);
}

static void InspectControllers() {
    g_state.controllerCount = 0;
    g_state.inspectedShip = 0;
    for (auto& controller : g_state.controllers) controller = {};
    if (!g_state.nativeReady) return;
    const uint64_t ship = PlayerShipId();
    g_state.inspectedShip = ship;
    if (!ship) return;
    uintptr_t parts[kDevMaxControllers] = {};
    char names[kDevMaxControllers][96] = {};
    // Reuse the existing bounded ship-parts discovery used by the loadout UI.
    const int count = ShipPartComponents(ship, "SCItemMiningController", parts, names, kDevMaxControllers);
    for (int i = 0; i < count && i < kDevMaxControllers; ++i) {
        const uintptr_t c = parts[i];
        const uint64_t id = EntityIdOfComponent(c);
        const uintptr_t entity = Entity(id);
        if (!entity || EntityComponent(entity, "SCItemMiningController") != c) continue;
        DevMiningController& out = g_state.controllers[g_state.controllerCount++];
        out.id = id;
        strcpy_s(out.name, names[i]);
        out.authority = VCall<bool>(entity, 0x7A8) ? 1 : 0;
        // Binding names are corroborated by CSCItemMiningController::PostInitialize.
        out.firing = Rd<uint8_t>(c + 0x911) != 0;
        out.focused = Rd<uint8_t>(c + 0x761) != 0;
        out.scanned = Rd<uint8_t>(c + 0x959) != 0;
        out.fractureMode = Rd<uint8_t>(c + 0x8c9) != 0;
        out.throttle = Rd<float>(c + 0xbe4);
        out.transfer = Rd<float>(c + 0xd04);
        out.lastHitRate = Rd<float>(c + 0x2c0);
        out.hudMass = Rd<float>(c + 0xd4c);
        out.hudResistance = Rd<float>(c + 0xe24);
        EntryId entry;
        g_api.scanEntry(c, &entry);
        out.scanEntry = entry.value;
        out.scanEntryType = entry.type;
        uint64_t scannedEntity = 0;
        g_api.scanEntity(c, &scannedEntity);
        if (scannedEntity && g_api.validEntity(&scannedEntity)) out.scanEntity = EntityIdOfHandle(&scannedEntity);
        uint64_t target = Rd<uint64_t>(c + 0x248);
        if (target && g_api.validHandle(&target)) {
            const uintptr_t mineable = target & kPtrMask;
            const uint64_t targetId = EntityIdOfComponent(mineable);
            const uintptr_t targetEntity = Entity(targetId);
            if (targetEntity && EntityComponent(targetEntity, "EntityComponentMineable") == mineable)
                ReadRock(targetId, out.target);
        }
    }
}

static void Inspect() {
    g_state.metrics = {};
    g_state.live = g_state.mineable = g_state.harvestable = g_state.signature = false;
    g_state.authority = g_state.compositionEntries = -1;
    g_state.databanks = g_state.contacts = g_state.targetable = g_state.scanData = g_state.scanTokens = 0;
    const uintptr_t entity = Entity(g_state.rockId);
    if (!entity) return;
    g_state.live = true;
    const uintptr_t mineable = EntityComponent(entity, "EntityComponentMineable");
    g_state.mineable = mineable != 0;
    g_state.harvestable = EntityComponent(entity, "HarvestableComponent") != 0;
    g_state.signature = EntityComponent(entity, "SCSignatureSystem") != 0;
    if (!g_state.nativeReady) return;
    g_state.authority = VCall<bool>(entity, 0x7A8) ? 1 : 0;
    if (mineable) {
        g_state.compositionEntries = VectorCount(mineable, 0x540, 0x58, 128);
        ReadRock(g_state.rockId, g_state.metrics);
    }
    uint64_t handle = 0;
    VCall<void>(*g_tp.entitySystem, 0x128, &handle, g_state.rockId);
    if ((handle & kPtrMask) != entity) return;
    uint8_t scratch[16] = {};
    g_api.entryId(scratch, &g_contactId, handle);
    uint64_t owners[3] = {};
    g_state.databanks = CurrentBanks(owners);
    for (int i = 0; i < g_state.databanks; ++i) {
        const uintptr_t bank = Databank(owners[i]);
        g_state.scanTokens += g_api.token(bank) ? 1 : 0;
        if (!g_contactId.value) continue;
        g_state.contacts += g_api.contains(bank, &g_contactId) ? 1 : 0;
        g_state.targetable += g_api.targetable(bank, &g_contactId, 0) ? 1 : 0;
        g_state.scanData += g_api.hasScan(bank, &g_contactId) ? 1 : 0;
    }
}

static void LogRock(const char* source, const DevRockMetrics& m) {
    if (!m.id) return;
    Log("[dev/mining/power] source=%s rock=%llu name=%s valid=%d params=%d authority=%d composition=%d mass=%.6g volume=%.6g resistance=%.6g baseResistance=%.6g capacity=%.6g decayRate=%.6g charge=%.6g pendingCharge=%.6g updateEnabled=%d recentHitSources=%d queuedHitSources=%d",
        source, m.id, m.name, m.valid, m.globalParams, m.authority, m.composition, m.mass, m.volume,
        m.resistance, m.baseResistance, m.capacity, m.decayPerSecond, m.charge, m.pendingCharge,
        m.updateEnabled, m.recentHitSources, m.queuedHitSources);
}

static void LogControllers() {
    Log("[dev/mining/trace] ship=%llu controllers=%d secondsRemaining=%d", g_state.inspectedShip, g_state.controllerCount, g_state.traceSeconds);
    for (int i = 0; i < g_state.controllerCount; ++i) {
        const auto& c = g_state.controllers[i];
        Log("[dev/mining/controller] id=%llu name=%s authority=%d target=%llu firing=%d focused=%d scanned=%d fractureMode=%d throttle=%.6g transfer=%.6g lastHitRate=%.6g hudMass=%.6g hudResistance=%.6g scanEntry=%llu scanEntryType=%u scanEntity=%llu",
            c.id, c.name, c.authority, c.target.id, c.firing, c.focused, c.scanned, c.fractureMode,
            c.throttle, c.transfer, c.lastHitRate, c.hudMass, c.hudResistance, c.scanEntry, c.scanEntryType, c.scanEntity);
        LogRock("controller-target", c.target);
    }
}

static void LogSnapshot() {
    Log("[dev/mining] rock=%llu class=%s live=%d mineable=%d harvestable=%d signature=%d authority=%d composition=%d banks=%d contacts=%d targetable=%d scanData=%d linkedScanTokens=%d",
        g_state.rockId, DevRockClass(g_state.fixture), g_state.live, g_state.mineable, g_state.harvestable, g_state.signature,
        g_state.authority, g_state.compositionEntries, g_state.databanks, g_state.contacts, g_state.targetable, g_state.scanData, g_state.scanTokens);
    LogRock("spawned", g_state.metrics);
}

struct MarkerSample { EntryId id; uint64_t entityHandle = 0; uint32_t flags = 0; bool definitionRef = false; };
static struct {
    MarkerSample samples[64];
    int count = 0, visited = 0, unsupported = 0, skipped = 0, types[16] = {};
    bool fault = false;
} g_markers;

// ForEachEntry holds native read locks. This callback only copies fields; never
// call back into the engine or let an exception escape and strand those locks.
static void __fastcall CollectMarker(uint64_t weak) {
    ++g_markers.visited;
    if (g_markers.fault || g_markers.visited > 4096) { ++g_markers.skipped; return; }
    __try {
        const uintptr_t entry = weak & kPtrMask;
        if (!entry || Rd<uint16_t>(entry - 4) != static_cast<uint16_t>(weak >> 48)
            || Rd<uintptr_t>(entry) != g_api.base + 0x85c2e90) { ++g_markers.unsupported; return; }
        const EntryId id = Rd<EntryId>(entry + 0x18);
        if (id.type < _countof(g_markers.types)) ++g_markers.types[id.type];
        // Type 2 is the provider/location harvestable key. +0x30 is the class
        // definition, also present on ordinary contacts; it is not a static mesh.
        if (id.type != 2) return;
        for (int i = 0; i < g_markers.count; ++i)
            if (g_markers.samples[i].id.value == id.value && g_markers.samples[i].id.type == id.type) return;
        if (g_markers.count == _countof(g_markers.samples)) { ++g_markers.skipped; return; }
        MarkerSample& out = g_markers.samples[g_markers.count++];
        out.id = id;
        out.entityHandle = Rd<uint64_t>(entry + 0x28);
        out.flags = Rd<uint32_t>(entry + 0x90);
        out.definitionRef = Rd<uintptr_t>(entry + 0x30) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_markers.fault = true; }
}

static void InspectMarkers() {
    g_state.markersInspected = false;
    g_state.markerSamples = g_state.markerLive = g_state.markerMineable = 0;
    g_state.markerTruncated = false;
    if (!g_state.nativeReady) { Status("Marker inspection unavailable: native checks have not passed."); return; }
    uint64_t owners[3] = {};
    const int banks = CurrentBanks(owners);
    const EntryVisitor visitor = { &CollectMarker, 1, nullptr };
    MiningSnapshot mining;
    GetMiningSnapshot(mining);
    Log("[dev/mining/markers] begin banks=%d naturalMining=%d; sampling provider/location keys (type 2)", banks, mining.active);
    for (int i = 0; i < banks; ++i) {
        const uintptr_t bank = Databank(owners[i]);
        if (!bank) continue;
        g_markers = {};
        g_api.forEachEntry(bank, &visitor);
        Log("[dev/mining/markers] bankOwner=%llu visited=%d samples=%d unsupported=%d skipped=%d callbackFault=%d",
            owners[i], g_markers.visited, g_markers.count, g_markers.unsupported, g_markers.skipped, g_markers.fault);
        for (int t = 0; t < _countof(g_markers.types); ++t)
            if (g_markers.types[t]) Log("[dev/mining/marker-types] bankOwner=%llu type=%d count=%d", owners[i], t, g_markers.types[t]);
        if (g_markers.fault) {
            g_state.nativeReady = g_state.tracing = false;
            g_state.traceSeconds = 0;
            g_diagnosticsSuspended = true;
            strcpy_s(g_state.apiStatus, "Native adapter disabled after a marker read fault. Restart before retrying.");
            Status("Marker read fault; native enumeration returned and released its locks. Diagnostics suspended.");
            return;
        }
        g_state.markerTruncated = g_state.markerTruncated || g_markers.skipped != 0;
        for (int j = 0; j < g_markers.count; ++j) {
            const auto& m = g_markers.samples[j];
            uint64_t id = 0;
            uintptr_t entity = 0;
            if (m.entityHandle && g_api.validEntity(&m.entityHandle)) {
                id = EntityIdOfHandle(&m.entityHandle);
                entity = Entity(id);
                if (entity != (m.entityHandle & kPtrMask)) { entity = 0; id = 0; }
            }
            const bool mineable = entity && EntityComponent(entity, "EntityComponentMineable") != 0;
            const bool harvestable = entity && EntityComponent(entity, "HarvestableComponent") != 0;
            const bool signature = entity && EntityComponent(entity, "SCSignatureSystem") != 0;
            const int authority = entity ? (VCall<bool>(entity, 0x7A8) ? 1 : 0) : -1;
            const char* name = entity ? VCall<const char*>(entity, 0x78) : "<no live entity>";
            Log("[dev/mining/marker] bankOwner=%llu entry=%llu type=%u definitionRef=%d flags=0x%08x targetable=%d scanData=%d entity=%llu name=%s mineable=%d harvestable=%d signature=%d authority=%d",
                owners[i], m.id.value, m.id.type, m.definitionRef, m.flags,
                g_api.targetable(bank, &m.id, 0), g_api.hasScan(bank, &m.id), id, name ? name : "?", mineable, harvestable, signature, authority);
            ++g_state.markerSamples;
            g_state.markerLive += entity ? 1 : 0;
            g_state.markerMineable += mineable ? 1 : 0;
            if (mineable) { DevRockMetrics rock; ReadRock(id, rock); LogRock("marker", rock); }
        }
    }
    g_state.markersInspected = true;
    Log("[dev/mining/markers] done samples=%d live=%d mineable=%d truncated=%d; banks may share entries",
        g_state.markerSamples, g_state.markerLive, g_state.markerMineable, g_state.markerTruncated);
}

static void Spawn(int fixture, float distance, float scale, DWORD now) {
    if (g_state.rockId) { Status("Remove the tracked test rock before spawning another."); return; }
    if (!SpawnerReady() || !g_tp.ok) { Status("The entity spawner is unavailable."); return; }
    double pos[3] = {}, rot[4] = {};
    if (!PlaceNearPlayer(distance, 0, 0.2, pos, rot)) { Status("Cannot place a rock yet. Enter the universe and stand in an open area."); return; }
    uint64_t id = 0;
    const char* error = SpawnEntityInPlayerZone(DevRockClass(fixture), pos, rot, id, scale);
    if (error) { Status("Spawn failed: %s.", error); return; }
    g_state.rockId = id;
    g_state.fixture = fixture;
    g_state.spawnScale = scale;
    g_contactId = {};
    g_registeredCount = 0;
    g_waitingSpawn = g_state.busy = true;
    g_spawnAt = now;
    Status("Spawn requested for %s (id %llu, scale %.2fx); waiting for its entity.", DevRockClass(fixture), id, scale);
}

static void EnableContact() {
    if (!g_state.nativeReady) { Status("Native adapter unavailable. See the executable check above."); return; }
    Inspect();
    if (!g_state.live || !g_state.mineable || !g_state.signature) { Status("The tracked rock needs a live mineable and signature component first."); return; }
    uint64_t handle = 0;
    VCall<void>(*g_tp.entitySystem, 0x128, &handle, g_state.rockId);
    if (!handle || !g_contactId.value) { Status("Could not resolve the rock's native entity handle / databank entry id."); return; }
    uint64_t owners[3] = {};
    const int count = CurrentBanks(owners);
    int accepted = 0;
    const uint32_t flags = 0x800; // Native IObjectDatabankEntry::IsTargetable tests this bit.
    for (int i = 0; i < count; ++i) {
        bool known = false;
        for (int j = 0; j < g_registeredCount; ++j) if (g_registeredBanks[j] == owners[i]) known = true;
        if (!known && g_registeredCount == _countof(g_registeredBanks)) continue;
        // The engine queues/copies the handle and flags and derives scan/contact
        // properties from the entity class. No manual entry allocation or scan data.
        if (g_api.add(Databank(owners[i]), &handle, &flags)) {
            if (!known) g_registeredBanks[g_registeredCount++] = owners[i];
            ++accepted;
        }
    }
    if (accepted) Status("Contact request queued for %d databank(s). Close the menu, aim at the rock and scan; diagnostics update once a second.", accepted);
    else Status("No contact requests accepted (%d local databanks found). Enter your mining ship or equip the FPS mining tool and retry.", count);
    LogSnapshot();
}

static void Remove(DWORD now) {
    if (!g_state.rockId) return;
    if (!CanRemoveEntities()) { Status("Entity removal is unavailable; the rock remains tracked."); return; }
    if (g_state.nativeReady && g_contactId.value) {
        for (int i = 0; i < g_registeredCount; ++i) {
            const uintptr_t bank = Databank(g_registeredBanks[i]);
            if (bank && !g_api.remove(bank, &g_contactId)) {
                Status("Databank cleanup was rejected. Rock retained; retry Remove.");
                return;
            }
        }
    }
    RemoveEntityById(g_state.rockId);
    g_removing = g_state.busy = true;
    g_waitingSpawn = false;
    g_removeAt = now;
    Status("Removal requested for test rock %llu.", g_state.rockId);
}

// Keep SEH on this POD-only boundary: engine work always runs from the existing
// main-thread tick. Never recover faults by repeating a native mutation every tick.
static void Tick(DevAction action, int fixture, float distance, float scale, DWORD now) {
    __try {
        if (action == DevAction::SpawnRock) Spawn(fixture, distance, scale, now);
        else if (action == DevAction::EnableContact) EnableContact();
        else if (action == DevAction::RemoveRock) Remove(now);
        else if (action == DevAction::InspectMarkers) {
            InspectMarkers();
            if (g_state.markersInspected) Status("Marker inspection saved to mod.log: %d samples, %d live, %d mineable%s. Banks may share entries.",
                g_state.markerSamples, g_state.markerLive, g_state.markerMineable, g_state.markerTruncated ? " (limited sample)" : "");
        }
        else if (action == DevAction::StartTrace) {
            if (!g_state.nativeReady) Status("Mining trace unavailable: native executable checks have not passed.");
            else {
                g_traceAt = now;
                g_state.tracing = true;
                Status("Mining trace started for 30 seconds. Close the menu, aim and fire from the mining turret. Then repeat on a natural rock.");
                InspectMarkers();
            }
        } else if (action == DevAction::StopTrace) {
            g_state.tracing = false;
            g_state.traceSeconds = 0;
            Status("Mining trace stopped. Samples are in mod.log.");
        }
        if (action != DevAction::None || (!g_diagnosticsSuspended && (g_state.rockId || g_state.tracing) && now - g_lastRefresh >= 1000)) {
            g_lastRefresh = now;
            const DevSnapshot before = g_state;
            Inspect();
            const bool traceSample = g_state.tracing;
            if (traceSample) {
                const DWORD elapsed = now - g_traceAt;
                g_state.traceSeconds = elapsed >= 30000 ? 0 : static_cast<int>((30000 - elapsed + 999) / 1000);
                if (elapsed >= 30000) {
                    g_state.tracing = false;
                    Status("Mining trace finished. Samples are in mod.log.");
                }
            }
            if (traceSample || action == DevAction::Refresh) {
                InspectControllers();
                LogControllers();
            }
            if (g_waitingSpawn && g_state.live) {
                g_waitingSpawn = g_state.busy = false;
                Status(g_state.targetable ? "Test rock is live and already targetable. Try normal scanning / mining; no contact request is needed."
                    : "Test rock is live. Try normal scanning / mining; Enable targeting / scanning is an optional fallback.");
            } else if (g_waitingSpawn && now - g_spawnAt >= 30000) {
                g_waitingSpawn = g_state.busy = false;
                Status("Spawn timed out after 30 seconds. The id is retained for Refresh / Remove; no second rock will be spawned.");
                LogSnapshot();
            }
            if (g_removing && !g_state.live) {
                g_removing = g_state.busy = false;
                g_state.rockId = 0;
                g_registeredCount = 0;
                g_contactId = {};
                Status("Test rock removed.");
            } else if (g_removing && now - g_removeAt >= 10000) {
                g_removing = g_state.busy = false;
                Status("Rock still exists after the removal request. Retry Remove; no second rock will be spawned.");
            }
            if (traceSample || action == DevAction::Refresh || before.live != g_state.live || before.authority != g_state.authority
                || before.compositionEntries != g_state.compositionEntries || before.targetable != g_state.targetable
                || before.contacts != g_state.contacts || before.scanData != g_state.scanData) LogSnapshot();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_waitingSpawn = g_removing = g_state.busy = false;
        g_state.tracing = false;
        g_state.traceSeconds = 0;
        g_diagnosticsSuspended = true;
        // Disable this session's adapter after a fault. Spawned id stays visible for cleanup.
        g_state.nativeReady = false;
        strcpy_s(g_state.apiStatus, "Native adapter disabled after an engine fault. Restart the game before retrying the adapter.");
        Status("Engine fault during DEV action / diagnostics (0x%08lx). See mod.log.", GetExceptionCode());
    }
}

void ProcessDev(DWORD now) {
    if (!g_tp.ok || !SpawnerReady()) {
        ProcessMining(now, false);
        GetMiningSnapshot(g_state.mining);
        Publish(); return;
    }
    AcquireSRWLockExclusive(&g_lock);
    const auto request = g_request;
    g_request.action = DevAction::None;
    if (request.action != DevAction::None) g_published.busy = true;
    ReleaseSRWLockExclusive(&g_lock);
    Tick(request.action, request.fixture, request.distance, request.scale, now);
    ProcessMining(now, g_state.tracing);
    GetMiningSnapshot(g_state.mining);
    Publish();
}
