#include "dev.h"
#include "spawner.h"
#include "build.h"
#include "teleport.h"
#include "npc.h"
#include "patches.h"
#include "hooks.h"
#include "cvars.h"
#include <atomic>
#include <intrin.h>
#include <bcrypt.h>
#include <cmath>
#include <cstdarg>

#pragma comment(lib, "bcrypt.lib")

// Validated against the executable and native callers documented in docs/dev-mining.md.
// These are deliberately build-specific. A different executable disables the native
// adapter and offset-based diagnostics; the existing generic spawner remains usable.
static constexpr char kGameHash[] = "3953f8b1a9894d5d9d1836e50939b726141f1829fe65a3a622d5acb2c6f89162";
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
// Only the harvestable branch of ScatterGeneratorCPU::QueuePatchPromotion.
// Keep the instruction intact and replace one comparison immediate atomically.
static constexpr uint8_t kNaturalGate[] = { 0x80, 0xbc, 0x24, 0xa0, 0x03, 0, 0, 0, 0x0f, 0x84, 0xd0, 0x01, 0, 0 };
static struct { uint8_t* gate = nullptr; const uint8_t* editor = nullptr; const uint8_t* online = nullptr; const uint8_t* server = nullptr; bool restoreFailed = false; } g_natural;

// Observers installed during startup, before entering the universe. All hooks
// call the original exactly once. Only the separate nearby-spawning experiment
// may add bit 8 to eligible cell work; all other arguments/results are preserved.
// Workers only count/copy POD; the main-thread tick writes the bounded samples.
namespace NaturalProbe {
using PromotionFn = void(__fastcall*)(uintptr_t);
using ObjectFn = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using RequestFn = bool(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using CheckFn = bool(__fastcall*)(uintptr_t, uintptr_t, uintptr_t);
using BiomeBuildFn = void(__fastcall*)(uintptr_t, uintptr_t, uint32_t);
using BiomeCellFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uint32_t, uint32_t, uint64_t, uint8_t);
using BiomeInstanceFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, int, uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static PromotionFn promotion = nullptr;
static ObjectFn object = nullptr;
static RequestFn request = nullptr;
static CheckFn check = nullptr;
static BiomeBuildFn biomeBuild = nullptr;
static BiomeCellFn biomeCell = nullptr;
static BiomeInstanceFn biomeInstance = nullptr;
static PromotionFn biomeJob = nullptr;
static constexpr unsigned completeMask = 0xff;
static unsigned installed = 0;
static std::atomic<bool> active{false}, nearbyEnabled{false}, nearbyFaulted{false};
static std::atomic<uint64_t> jobs{0}, objects{0}, kinds[8]{}, withGeometry{0}, faults{0};
static std::atomic<uint64_t> requests{0}, promotionRequests{0}, accepted{0}, rejected{0}, unchecked{0}, afterCheck{0}, dropped{0};
static std::atomic<uint64_t> biomeBuilds{0}, biomeBuildWithSpawn{0}, biomeCells{0}, biomeCellWithSpawn{0}, biomeNearCells{0};
static std::atomic<uint64_t> biomeModes[4]{}, biomeOtherModes{0}, biomeRequests{0}, biomeDropped{0};
static std::atomic<uint64_t> biomeJobs{0}, biomeJobsWithSpawn{0}, biomeJobsEligible{0}, biomeJobsBlocked{0}, biomePromoted{0};
static thread_local unsigned promotionDepth = 0;
static thread_local unsigned biomeDepth = 0;
struct Sample {
    uint64_t providerHandle = 0, definition = 0, location[3] = {};
    uintptr_t callerRva = 0;
    uint32_t locationIndex = 0;
    int callerAuthority = -1, precondition = -1;
    bool fromPromotion = false, fromBiome = false, accepted = false, readable = false;
};
static thread_local Sample* currentRequest = nullptr;
static SRWLOCK sampleLock = SRWLOCK_INIT;
static Sample samples[32];
static unsigned sampleCount = 0;
struct BiomeSample {
    // 0 = root builder, 1 = cell builder, 2 = native instance, 3 = queued job.
    int stage = 0, builderType = -1, enabled = -1, mode = -1, previousLod = -1;
    uint32_t flags = 0, forwardedFlags = 0, lod = 0;
    int jobEligible = -1;
    uint32_t jobFlags = 0;
    bool fromJob = false;
    uint64_t providerHandle = 0, definition = 0, location[3] = {};
    uint32_t locationIndex = 0;
    bool readable = false;
};
static BiomeSample biomeSamples[4][8];
static unsigned biomeSampleCount[4] = {};
static thread_local const BiomeSample* currentBiomeJob = nullptr;
static DWORD loggedAt = 0;
static uint64_t N(const std::atomic<uint64_t>& n) { return n.load(std::memory_order_relaxed); }
static void Inc(std::atomic<uint64_t>& n) { n.fetch_add(1, std::memory_order_relaxed); }

static void ObserveObject(uintptr_t result) {
    __try {
        Inc(objects);
        const uintptr_t kindPtr = result ? Rd<uintptr_t>(result + 0x30) : 0;
        const unsigned kind = kindPtr ? Rd<uint8_t>(kindPtr) : 7;
        Inc(kinds[kind < 7 ? kind : 7]);
        if (kind == 5 && (Rd<uintptr_t>(result + 0x48) || Rd<uintptr_t>(result + 0x50))) Inc(withGeometry);
    } __except (EXCEPTION_EXECUTE_HANDLER) { Inc(faults); }
}
static void ReadSample(Sample& s, uintptr_t provider, uintptr_t instance, uintptr_t authority) {
    __try {
        s.providerHandle = Rd<uint64_t>(provider + 8);
        s.definition = Rd<uintptr_t>(instance + 0x18);
        memcpy(s.location, reinterpret_cast<const void*>(instance + 0x90), sizeof(s.location));
        s.locationIndex = Rd<uint32_t>(instance + 0xa8);
        s.callerAuthority = authority ? Rd<uint8_t>(authority) : -1;
        s.readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { Inc(faults); }
}
static void StoreSample(const Sample& s) {
    if (!TryAcquireSRWLockExclusive(&sampleLock)) { Inc(dropped); return; }
    if (sampleCount < _countof(samples)) samples[sampleCount++] = s;
    else Inc(dropped);
    ReleaseSRWLockExclusive(&sampleLock);
}
static void StoreBiome(const BiomeSample& s) {
    if (!TryAcquireSRWLockExclusive(&sampleLock)) { Inc(biomeDropped); return; }
    unsigned& count = biomeSampleCount[s.stage];
    if (count < _countof(biomeSamples[0])) biomeSamples[s.stage][count++] = s;
    else Inc(biomeDropped);
    ReleaseSRWLockExclusive(&sampleLock);
}
static void ReadBiomeContext(BiomeSample& s, uintptr_t builder, uintptr_t cell) {
    __try {
        s.builderType = Rd<int>(builder + 8);
        s.enabled = Rd<uint8_t>(builder + 0x28);
        if (cell) s.previousLod = Rd<int>(cell + 0x16a8);
        s.readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { Inc(faults); }
}
static void ReadBiomeInstance(BiomeSample& s, uintptr_t instance, uintptr_t provider) {
    __try {
        s.providerHandle = Rd<uint64_t>(provider);
        s.definition = Rd<uintptr_t>(instance + 0x18);
        memcpy(s.location, reinterpret_cast<const void*>(instance + 0x90), sizeof(s.location));
        s.locationIndex = Rd<uint32_t>(instance + 0xa8);
        s.readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { Inc(faults); }
}
// Read immutable-address context flags at the decision, not just the previous UI tick.
static bool NearbyContextAllowed() {
    __try {
        return g_natural.online && *g_natural.online == 0 && g_natural.editor && *g_natural.editor == 0
            && g_natural.server && *g_natural.server == 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { nearbyFaulted.store(true); return false; }
}
static uint32_t NearbyFlags(const BiomeSample& s) {
    // Only the observed type-0 client draw/physics work, at native close LODs.
    // Retain native provider validation, location/depletion checks, batch and LOD writes.
    if (!nearbyEnabled.load(std::memory_order_relaxed) || nearbyFaulted.load(std::memory_order_relaxed)
        || !s.readable || s.builderType != 0 || s.enabled != 1 || s.flags != 6
        || s.lod > 1 || s.previousLod < 2 || s.previousLod > 100 || !NearbyContextAllowed()) return s.flags;
    return s.flags | 8;
}
static void __fastcall BiomeJobHook(uintptr_t context) {
    if (!active.load(std::memory_order_relaxed)) { biomeJob(context); return; }
    Inc(biomeJobs);
    BiomeSample s; s.stage = 3;
    __try {
        s.flags = Rd<uint32_t>(context + 0x10);
        s.lod = Rd<uint32_t>(context + 0x20);
        s.jobEligible = Rd<uint8_t>(context + 0x38);
        ReadBiomeContext(s, Rd<uintptr_t>(context + 0x58), Rd<uintptr_t>(context + 0x28));
    } __except (EXCEPTION_EXECUTE_HANDLER) { Inc(faults); }
    if (s.readable) {
        if (s.flags & 8) { Inc(biomeJobsWithSpawn); if (!s.jobEligible) Inc(biomeJobsBlocked); }
        if (s.jobEligible) Inc(biomeJobsEligible);
    }
    StoreBiome(s);
    const BiomeSample* previous = currentBiomeJob;
    currentBiomeJob = &s;
    __try { biomeJob(context); }
    __finally { currentBiomeJob = previous; }
}
static void __fastcall BiomeBuildHook(uintptr_t builder, uintptr_t planet, uint32_t flags) {
    if (active.load(std::memory_order_relaxed)) {
        Inc(biomeBuilds);
        if (flags & 8) Inc(biomeBuildWithSpawn);
        BiomeSample s; s.flags = flags;
        ReadBiomeContext(s, builder, 0); StoreBiome(s);
    }
    biomeBuild(builder, planet, flags);
}
static void __fastcall BiomeCellHook(uintptr_t builder, uintptr_t cell, uintptr_t planet, uint32_t lod, uint32_t flags, uint64_t page, uint8_t option) {
    uint32_t forwarded = flags;
    if (active.load(std::memory_order_relaxed) || nearbyEnabled.load(std::memory_order_relaxed)) {
        Inc(biomeCells);
        if (flags & 8) Inc(biomeCellWithSpawn);
        BiomeSample s; s.stage = 1; s.flags = flags; s.lod = lod;
        ReadBiomeContext(s, builder, cell);
        if (s.readable && s.previousLod >= 2 && lod <= 1) Inc(biomeNearCells);
        if (currentBiomeJob && currentBiomeJob->readable) {
            s.fromJob = true; s.jobFlags = currentBiomeJob->flags; s.jobEligible = currentBiomeJob->jobEligible;
        }
        forwarded = NearbyFlags(s);
        s.forwardedFlags = forwarded;
        if (forwarded != flags) Inc(biomePromoted);
        StoreBiome(s);
    }
    biomeCell(builder, cell, planet, lod, forwarded, page, option);
}
static void __fastcall BiomeInstanceHook(uintptr_t context, uintptr_t instance, uintptr_t provider, int mode,
    uint32_t group, uintptr_t output, uintptr_t count, uintptr_t extra, uintptr_t batch) {
    if (!active.load(std::memory_order_relaxed)) { biomeInstance(context, instance, provider, mode, group, output, count, extra, batch); return; }
    if (mode >= 0 && mode < 4) Inc(biomeModes[mode]); else Inc(biomeOtherModes);
    BiomeSample s; s.stage = 2; s.mode = mode;
    if (mode == 0 || mode == 1) ReadBiomeInstance(s, instance, provider);
    StoreBiome(s);
    ++biomeDepth;
    __try { biomeInstance(context, instance, provider, mode, group, output, count, extra, batch); }
    __finally { --biomeDepth; }
}
static void __fastcall PromotionHook(uintptr_t context) {
    if (!active.load(std::memory_order_relaxed)) { promotion(context); return; }
    Inc(jobs);
    ++promotionDepth;
    __try { promotion(context); }
    __finally { --promotionDepth; }
}
static uintptr_t __fastcall ObjectHook(uintptr_t out, uintptr_t context, uintptr_t index, uintptr_t world) {
    const uintptr_t result = object(out, context, index, world);
    if (promotionDepth) ObserveObject(result);
    return result;
}
static bool __fastcall CheckHook(uintptr_t provider, uintptr_t instance, uintptr_t authority) {
    const bool result = check(provider, instance, authority);
    if (currentRequest) currentRequest->precondition = result ? 1 : 0;
    return result;
}
static bool __fastcall RequestHook(uintptr_t provider, uintptr_t instance, uintptr_t batch, uintptr_t authority) {
    if (!active.load(std::memory_order_relaxed)) return request(provider, instance, batch, authority);
    Sample s;
    s.callerRva = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_api.base;
    s.fromPromotion = promotionDepth != 0;
    s.fromBiome = biomeDepth != 0;
    ReadSample(s, provider, instance, authority);
    Inc(requests);
    if (s.fromPromotion) Inc(promotionRequests);
    if (s.fromBiome) Inc(biomeRequests);
    Sample* previous = currentRequest;
    currentRequest = &s;
    __try { s.accepted = request(provider, instance, batch, authority); }
    __finally { currentRequest = previous; }
    if (s.accepted) Inc(accepted);
    else if (s.precondition == 0) Inc(rejected);
    else if (s.precondition < 0) Inc(unchecked);
    else Inc(afterCheck);
    StoreSample(s);
    return s.accepted;
}
static void Install(uintptr_t base) {
    // Stolen spans contain only whole, non-relative instructions (see research).
    if (HookFunction(reinterpret_cast<uint8_t*>(base + 0x2639070), 7, reinterpret_cast<void*>(&PromotionHook), reinterpret_cast<void**>(&promotion))) installed |= 1;
    if (HookFunction(reinterpret_cast<uint8_t*>(base + 0x26d2250), 5, reinterpret_cast<void*>(&ObjectHook), reinterpret_cast<void**>(&object))) installed |= 2;
    if (HookFunction(reinterpret_cast<uint8_t*>(base + 0x3d18b20), 5, reinterpret_cast<void*>(&RequestHook), reinterpret_cast<void**>(&request))) installed |= 4;
    if (HookFunction(reinterpret_cast<uint8_t*>(base + 0x3cd3990), 6, reinterpret_cast<void*>(&CheckHook), reinterpret_cast<void**>(&check))) installed |= 8;
    if (HookFunction(reinterpret_cast<uint8_t*>(base + 0x2652ce0), 7, reinterpret_cast<void*>(&BiomeBuildHook), reinterpret_cast<void**>(&biomeBuild))) installed |= 16;
    if (HookFunction(reinterpret_cast<uint8_t*>(base + 0x26593c0), 5, reinterpret_cast<void*>(&BiomeCellHook), reinterpret_cast<void**>(&biomeCell))) installed |= 32;
    if (HookFunction(reinterpret_cast<uint8_t*>(base + 0x273c180), 5, reinterpret_cast<void*>(&BiomeInstanceHook), reinterpret_cast<void**>(&biomeInstance))) installed |= 64;
    if (HookFunction(reinterpret_cast<uint8_t*>(base + 0x263e3e0), 5, reinterpret_cast<void*>(&BiomeJobHook), reinterpret_cast<void**>(&biomeJob))) installed |= 128;
    Log("[dev/mining/promotion-probe] installed=0x%x complete=%d; counts cover enabled experiment / mining traces; accepted is not completed entity creation", installed, installed == completeMask);
}
static void LogCounts(DWORD now) {
    loggedAt = now;
    Log("[dev/mining/promotion-probe] installed=0x%x active=%d jobs=%llu objects=%llu kind1=%llu kind4=%llu kind5=%llu kindOther=%llu geometry5=%llu requests=%llu fromPromotion=%llu accepted=%llu rejectedCheck=%llu rejectedBeforeCheck=%llu rejectedAfterCheck=%llu readFaults=%llu droppedSamples=%llu",
        installed, active.load(), N(jobs), N(objects), N(kinds[1]), N(kinds[4]), N(kinds[5]),
        N(kinds[0]) + N(kinds[2]) + N(kinds[3]) + N(kinds[6]) + N(kinds[7]), N(withGeometry),
        N(requests), N(promotionRequests), N(accepted), N(rejected), N(unchecked), N(afterCheck), N(faults), N(dropped));
    Sample copied[32];
    AcquireSRWLockExclusive(&sampleLock);
    const unsigned count = sampleCount;
    memcpy(copied, samples, count * sizeof(Sample));
    sampleCount = 0;
    ReleaseSRWLockExclusive(&sampleLock);
    for (unsigned i = 0; i < count; ++i) {
        const auto& s = copied[i];
        Log("[dev/mining/promotion-request] callerRva=0x%llx fromPromotion=%d fromBiome=%d readable=%d providerHandle=0x%llx definition=0x%llx location=(%llu,%llu,%llu,%u) callerAuthority=%d precondition=%d accepted=%d",
            s.callerRva, s.fromPromotion, s.fromBiome, s.readable, s.providerHandle, s.definition,
            s.location[0], s.location[1], s.location[2], s.locationIndex, s.callerAuthority, s.precondition, s.accepted);
    }
    Log("[dev/mining/biome-probe] builds=%llu buildsWithSpawnBit=%llu cells=%llu cellsWithSpawnBit=%llu nearCells=%llu mode0_draw=%llu mode1_spawn=%llu mode2=%llu mode3=%llu otherModes=%llu requests=%llu droppedSamples=%llu",
        N(biomeBuilds), N(biomeBuildWithSpawn), N(biomeCells), N(biomeCellWithSpawn), N(biomeNearCells),
        N(biomeModes[0]), N(biomeModes[1]), N(biomeModes[2]), N(biomeModes[3]), N(biomeOtherModes), N(biomeRequests), N(biomeDropped));
    Log("[dev/mining/biome-scheduling] jobs=%llu withSpawnBit=%llu eligible=%llu spawnBitBlocked=%llu nearbyEnabled=%d promotedCells=%llu contextFault=%d",
        N(biomeJobs), N(biomeJobsWithSpawn), N(biomeJobsEligible), N(biomeJobsBlocked), nearbyEnabled.load(), N(biomePromoted), nearbyFaulted.load());
    BiomeSample copiedBiome[4][8]; unsigned counts[4];
    AcquireSRWLockExclusive(&sampleLock);
    memcpy(copiedBiome, biomeSamples, sizeof(copiedBiome));
    memcpy(counts, biomeSampleCount, sizeof(counts));
    memset(biomeSampleCount, 0, sizeof(biomeSampleCount));
    ReleaseSRWLockExclusive(&sampleLock);
    for (int stage = 0; stage < 4; ++stage) for (unsigned i = 0; i < counts[stage]; ++i) {
        const auto& s = copiedBiome[stage][i];
        Log("[dev/mining/biome-sample] stage=%d readable=%d builderType=%d enabled=%d flags=0x%x forwardedFlags=0x%x lod=%u previousHarvestableLod=%d mode=%d fromJob=%d jobFlags=0x%x jobEligible=%d providerHandle=0x%llx definition=0x%llx location=(%llu,%llu,%llu,%u)",
            s.stage, s.readable, s.builderType, s.enabled, s.flags, s.forwardedFlags, s.lod, s.previousLod, s.mode,
            s.fromJob, s.jobFlags, s.jobEligible, s.providerHandle, s.definition, s.location[0], s.location[1], s.location[2], s.locationIndex);
    }
}
} // namespace NaturalProbe

static void LogNaturalEnvironment() {
    int server = -1, editor = -1, online = -1;
    __try {
        if (g_api.base) {
            server = Rd<uint8_t>(g_api.base + 0x9e2e908);
            editor = Rd<uint8_t>(g_api.base + 0x9e2ec66);
            online = Rd<uint8_t>(g_api.base + 0x9e2ec6e);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { server = editor = online = -1; }
    Log("[dev/mining/environment] server=%d editor=%d online=%d", server, editor, online);
    const char* names[] = { "ec_harvestable.spawning_enabled", "ec_harvestable.log_spawns", "w_PlanetsideEntitySpawning",
        "w_PlanetV5_ForceLoadPlanetAsV5HybridMode", "w_Scattering_UseCPUVirtualCache", "w_Scattering_GPUScattering",
        "w_Scattering_PromotionSubdivisionLevel", "w_Scattering_ServerTestMode", "g_PlanetSpawnMineables" };
    for (const char* name : names) {
        float value = 0;
        if (GetCVarNow(name, value)) Log("[dev/mining/cvar] name=%s known=1 value=%g", name, value);
        else Log("[dev/mining/cvar] name=%s known=0", name);
    }
}

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

static bool SetNaturalPromotion(bool enable) {
    if (enable && (!g_state.nativeReady || !g_state.naturalReady || !g_natural.editor || !g_natural.online)) {
        Status("Natural promotion unavailable: executable/context checks have not passed."); return false;
    }
    if (enable && (*g_natural.online || *g_natural.editor)) {
        Status("Natural promotion is restricted to offline play outside the editor."); return false;
    }
    if (g_state.naturalEnabled == enable) return true;
    uint8_t expected[sizeof(kNaturalGate)];
    memcpy(expected, kNaturalGate, sizeof(expected));
    expected[7] = g_state.naturalEnabled ? 0xff : 0;
    if (!g_natural.gate || memcmp(g_natural.gate, expected, sizeof(expected)) != 0) {
        g_state.naturalReady = false;
        g_natural.restoreFailed = g_state.naturalEnabled;
        Status("Natural promotion code changed unexpectedly. No write performed; restart before retrying."); return false;
    }
    // Native cache contains IsEditor (0/1). Comparing against 0xff makes only
    // this gate pass; provider validity, geometry and creation-batch checks stay.
    const uint8_t value = enable ? 0xff : 0;
    DWORD error = 0;
    if (!WriteCode(g_natural.gate + 7, &value, 1, error)) {
        g_natural.restoreFailed = g_state.naturalEnabled;
        Status("Natural promotion %s failed (error %lu).%s", enable ? "enable" : "restore", error,
            g_state.naturalEnabled ? " Restart the game before continuing." : "");
        return false;
    }
    g_natural.restoreFailed = false;
    g_state.naturalEnabled = enable;
    Log("[dev/mining/natural] enabled=%d editor=%d online=%d gateRva=0x263984a immediate=0x%02x",
        enable, *g_natural.editor, *g_natural.online, value);
    Status(enable ? "Natural promotion experiment enabled. Enable before travelling to the test area; inspect markers there to save promotion counters."
        : "Natural promotion experiment disabled. Existing entities keep their native lifetime; future promotions use the original gate.");
    return true;
}

static bool SetNearbySpawning(bool enable) {
    if (enable && (!g_state.nativeReady || !g_state.naturalProbeReady || !g_tp.ok || !SpawnerReady()
        || NaturalProbe::nearbyFaulted.load() || !NaturalProbe::NearbyContextAllowed())) {
        Status("Nearby biome spawning unavailable: native probes and offline/server context must pass. Restart after a context fault.");
        return false;
    }
    g_state.nearbyEnabled = enable;
    NaturalProbe::nearbyEnabled.store(enable);
    Log("[dev/mining/nearby] enabled=%d; eligible type-0 cells: flags 0x6 -> 0xe, requested LOD 0/1, previous LOD 2..100", enable);
    Status(enable ? "Nearby biome spawning experiment enabled. Travel to a new ore area, inspect markers and try normal mining."
        : "Nearby biome spawning disabled. Submitted work and existing entities retain native lifetime handling.");
    return true;
}

static void CheckNaturalContext() {
    if (g_state.nearbyEnabled && (!g_state.nativeReady || !g_state.naturalProbeReady || !g_tp.ok || !SpawnerReady()
        || NaturalProbe::nearbyFaulted.load() || !NaturalProbe::NearbyContextAllowed())) SetNearbySpawning(false);
    __try {
        if (g_state.naturalEnabled && !g_natural.restoreFailed
            && (!g_state.nativeReady || !g_tp.ok || !SpawnerReady() || *g_natural.online || *g_natural.editor))
            SetNaturalPromotion(false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_natural.restoreFailed = true;
        g_state.naturalReady = false;
        Status("Natural promotion context/restore fault. Restart the game before continuing.");
    }
}

static bool ExecutableMatches() {
    wchar_t path[32768];
    const DWORD len = GetModuleFileNameW(nullptr, path, _countof(path));
    if (!len || len >= _countof(path)) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    BYTE* buffer = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, 1024 * 1024));
    bool ok = buffer && BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0
        && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, 1024 * 1024, &read, nullptr)) { ok = false; break; }
        if (!read) break;
        ok = BCryptHashData(hash, buffer, read, 0) == 0;
    }
    BYTE digest[32] = {};
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
    char hex[65] = {};
    if (ok) for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
    CloseHandle(file);
    return ok && strcmp(hex, kGameHash) == 0;
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
        { 0x2639070, "40 53 56 41 55 41 56 48 81 EC 88 0C 00 00 33 F6" },
        { 0x26396d2, "0F B6 05 8D 55 7F 07" },
        { 0x2639843, "80 BC 24 A0 03 00 00 00 0F 84 D0 01 00 00" },
        { 0x26d2250, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 55" },
        { 0x3d18b20, "4C 89 4C 24 20 4C 89 44 24 18 48 89 4C 24 08 55" },
        { 0x3cd3990, "40 55 53 56 41 55 48 8D AC 24 08 FF FF FF 48 81" },
        { 0x2652ce0, "48 8B C4 44 89 40 18 48 89 48 08 53 56 57 41 54" },
        { 0x26593c0, "44 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 48" },
        { 0x273c180, "4C 8B DC 55 56 41 56 48 81 EC E0 00 00 00 4D 8B" },
        { 0x263e3e0, "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C" },
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
    g_state.nativeReady = ExecutableMatches() && CheckNativeCode();
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
        NaturalProbe::Install(g_api.base);
        g_state.naturalProbeReady = NaturalProbe::installed == NaturalProbe::completeMask;
        if (g_isOnlineFlag == reinterpret_cast<const uint8_t*>(g_api.base + 0x9e2ec6e)) {
            g_natural.gate = reinterpret_cast<uint8_t*>(g_api.base + 0x2639843);
            g_natural.editor = reinterpret_cast<const uint8_t*>(g_api.base + 0x9e2ec66);
            g_natural.online = g_isOnlineFlag;
            g_natural.server = reinterpret_cast<const uint8_t*>(g_api.base + 0x9e2e908);
            g_state.naturalReady = true;
        }
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
    Log("[dev/mining/markers] begin banks=%d naturalPromotion=%d; sampling provider/location keys (type 2)", banks, g_state.naturalEnabled);
    if (g_natural.editor && g_natural.online)
        Log("[dev/mining/natural] enabled=%d editor=%d online=%d", g_state.naturalEnabled, *g_natural.editor, *g_natural.online);
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
        else if (action == DevAction::EnableNearby) SetNearbySpawning(true);
        else if (action == DevAction::DisableNearby) SetNearbySpawning(false);
        else if (action == DevAction::EnableNatural) SetNaturalPromotion(true);
        else if (action == DevAction::DisableNatural) SetNaturalPromotion(false);
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
    const bool wasObserving = NaturalProbe::active.load(std::memory_order_relaxed);
    CheckNaturalContext();
    if (!g_tp.ok || !SpawnerReady()) {
        NaturalProbe::active.store(false, std::memory_order_relaxed);
        if (wasObserving) NaturalProbe::LogCounts(now);
        Publish(); return;
    }
    AcquireSRWLockExclusive(&g_lock);
    const auto request = g_request;
    g_request.action = DevAction::None;
    if (request.action != DevAction::None) g_published.busy = true;
    ReleaseSRWLockExclusive(&g_lock);
    Tick(request.action, request.fixture, request.distance, request.scale, now);
    CheckNaturalContext();
    const bool observing = g_state.nativeReady && g_state.naturalProbeReady
        && (g_state.naturalEnabled || g_state.nearbyEnabled || g_state.tracing)
        && g_natural.online && !*g_natural.online && g_natural.editor && !*g_natural.editor;
    NaturalProbe::active.store(observing, std::memory_order_relaxed);
    if (g_state.nativeReady && (request.action == DevAction::InspectMarkers || request.action == DevAction::EnableNatural || request.action == DevAction::EnableNearby
        || request.action == DevAction::StartTrace || request.action == DevAction::Refresh)) LogNaturalEnvironment();
    if (request.action == DevAction::InspectMarkers || request.action == DevAction::Refresh
        || request.action == DevAction::EnableNatural || request.action == DevAction::DisableNatural
        || request.action == DevAction::EnableNearby || request.action == DevAction::DisableNearby
        || request.action == DevAction::StartTrace || request.action == DevAction::StopTrace
        || (wasObserving && !observing)
        || (observing && now - NaturalProbe::loggedAt >= (g_state.tracing ? 1000u : 5000u)))
        NaturalProbe::LogCounts(now);
    g_state.promotionJobs = NaturalProbe::N(NaturalProbe::jobs);
    g_state.promotionHarvestables = NaturalProbe::N(NaturalProbe::kinds[5]);
    g_state.promotionRequests = NaturalProbe::N(NaturalProbe::promotionRequests);
    g_state.harvestableRequests = NaturalProbe::N(NaturalProbe::requests);
    g_state.harvestableAccepted = NaturalProbe::N(NaturalProbe::accepted);
    g_state.biomeBuilds = NaturalProbe::N(NaturalProbe::biomeBuilds);
    g_state.biomeCells = NaturalProbe::N(NaturalProbe::biomeCells);
    g_state.biomeDraws = NaturalProbe::N(NaturalProbe::biomeModes[0]);
    g_state.biomeSpawns = NaturalProbe::N(NaturalProbe::biomeModes[1]);
    g_state.biomePromoted = NaturalProbe::N(NaturalProbe::biomePromoted);
    Publish();
}
