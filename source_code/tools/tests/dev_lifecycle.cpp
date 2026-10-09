// Exercise the actual DEV request/lifecycle code with a fake engine. This is not
// an in-game mining test: native reads are replaced with fake engine functions.
#include "../../src/dev.cpp"
#include <limits>
#include <initializer_list>

Section g_text, g_rdata;
const uint8_t* g_isOnlineFlag = nullptr;
TeleportApi g_tp;
static uint8_t naturalGate[sizeof(kNaturalGate)], editorFlag = 0, onlineFlag = 0, serverFlag = 1;
static int codeWrites = 0;
static bool codeWriteFail = false;
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
bool WriteCode(uint8_t* at, const uint8_t* bytes, size_t n, DWORD& error) {
    if (codeWriteFail) { error = ERROR_ACCESS_DENIED; return false; }
    if (at != naturalGate + 7 || n != 1) { ++failures; return false; }
    memcpy(at, bytes, n); ++codeWrites; return true;
}
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
    g_natural = {}; codeWrites = 0; codeWriteFail = false; editorFlag = onlineFlag = 0; serverFlag = 1;
    g_contactId = {}; g_registeredCount = 0;
    g_waitingSpawn = g_removing = g_diagnosticsSuspended = false;
    g_spawnAt = g_removeAt = g_lastRefresh = g_traceAt = 0;
    discoverController = massFault = false; massCalls = 0;
    discoverMarkers = markerFault = false; spawnScale = 0; g_markers = {};
    ready = removable = true; loaded = lookupFault = false;
    spawns = removes = lookups = 0;
    esVtable[0x120 / 8] = reinterpret_cast<uintptr_t>(&Lookup);
    g_tp.ok = true; g_tp.entitySystem = &esPointer;
    using namespace NaturalProbe;
    active = false; installed = 0; loggedAt = 0; sampleCount = 0;
    promotionDepth = 0; currentRequest = nullptr;
    biomeDepth = 0; currentBiomeJob = nullptr; nearbyEnabled = nearbyFaulted = false;
    biomeJobs = biomeJobsWithSpawn = biomeJobsEligible = biomeJobsBlocked = biomePromoted = 0;
    biomeBuilds = biomeBuildWithSpawn = biomeCells = biomeCellWithSpawn = biomeNearCells = biomeRequests = biomeOtherModes = biomeDropped = 0;
    for (auto& n : biomeModes) n = 0;
    memset(biomeSampleCount, 0, sizeof(biomeSampleCount));
    jobs = objects = withGeometry = faults = 0;
    requests = promotionRequests = accepted = rejected = unchecked = afterCheck = dropped = 0;
    for (auto& n : kinds) n = 0;
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
static void SetUpNatural() {
    memcpy(naturalGate, kNaturalGate, sizeof(naturalGate));
    g_natural.gate = naturalGate; g_natural.editor = &editorFlag; g_natural.online = &onlineFlag; g_natural.server = &serverFlag;
    g_state.nativeReady = g_state.naturalReady = true;
    Publish();
}

alignas(8) static uint8_t probeObject[0xa0], probeProvider[0x20], probeInstance[0xb0];
static uint8_t probeKind = 5, probeAuthority = 1;
static std::atomic<int> probePromotionCalls{0}, probeObjectCalls{0}, probeRequestCalls{0}, probeCheckCalls{0};
static bool probeCheckResult = true, probeRequestResult = true, probeSkipCheck = false, probeThrow = false;
static bool probeParallel = false;
static thread_local bool probeThreadResult = false;
static uintptr_t ProbeP() { return reinterpret_cast<uintptr_t>(probeProvider); }
static uintptr_t ProbeI() { return reinterpret_cast<uintptr_t>(probeInstance); }
static uintptr_t ProbeA() { return reinterpret_cast<uintptr_t>(&probeAuthority); }
static bool __fastcall ProbeCheck(uintptr_t p, uintptr_t i, uintptr_t a) {
    ++probeCheckCalls;
    Check(p == ProbeP() && i == ProbeI() && a == ProbeA(), "precondition arguments preserved");
    return probeParallel ? probeThreadResult : probeCheckResult;
}
static bool __fastcall ProbeRequest(uintptr_t p, uintptr_t i, uintptr_t b, uintptr_t a) {
    ++probeRequestCalls;
    Check(p == ProbeP() && i == ProbeI() && b == 123 && a == ProbeA(), "request arguments preserved");
    if (probeThrow) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    if (probeSkipCheck) return false;
    return NaturalProbe::CheckHook(p, i, a) && probeRequestResult;
}
static uintptr_t __fastcall ProbeObject(uintptr_t o, uintptr_t c, uintptr_t i, uintptr_t w) {
    ++probeObjectCalls;
    Check(o == reinterpret_cast<uintptr_t>(probeObject) && c == 10 && i == 20 && w == 30, "object arguments preserved");
    return o;
}
static void __fastcall ProbePromotion(uintptr_t context) {
    ++probePromotionCalls;
    Check(context == 99, "promotion context preserved");
    Check(NaturalProbe::ObjectHook(reinterpret_cast<uintptr_t>(probeObject), 10, 20, 30) == reinterpret_cast<uintptr_t>(probeObject), "object return preserved");
    NaturalProbe::RequestHook(ProbeP(), ProbeI(), 123, ProbeA());
}
static bool ProbePropagatesException() {
    __try { NaturalProbe::PromotionHook(99); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
static DWORD WINAPI ProbeWorker(void* arg) {
    probeThreadResult = reinterpret_cast<uintptr_t>(arg) % 2 != 0;
    for (int i = 0; i < 128; ++i)
        if (NaturalProbe::RequestHook(ProbeP(), ProbeI(), 123, ProbeA()) != probeThreadResult) return 1;
    return 0;
}
static void TestPromotionObservers() {
    using namespace NaturalProbe;
    Reset();
    promotion = ProbePromotion; object = ProbeObject; request = ProbeRequest; check = ProbeCheck;
    Put(probeObject, 0x30, reinterpret_cast<uintptr_t>(&probeKind));
    Put(probeObject, 0x48, uintptr_t(1));
    Put(probeProvider, 8, uint64_t(0x7000012345678));
    Put(probeInstance, 0x18, uint64_t(456));
    Put(probeInstance, 0x90, uint64_t(789)); Put(probeInstance, 0x98, uint64_t(987));
    Put(probeInstance, 0xa0, uint64_t(654)); Put(probeInstance, 0xa8, uint32_t(321));
    PromotionHook(99);
    Check(probePromotionCalls == 1 && probeObjectCalls == 1 && probeRequestCalls == 1 && probeCheckCalls == 1
        && N(jobs) == 0 && N(requests) == 0, "disabled observers preserve native behavior and collect nothing");
    active = true;
    PromotionHook(99);
    Check(probePromotionCalls == 2 && probeObjectCalls == 2 && probeRequestCalls == 2 && probeCheckCalls == 2
        && N(jobs) == 1 && N(objects) == 1 && N(kinds[5]) == 1 && N(withGeometry) == 1
        && N(requests) == 1 && N(promotionRequests) == 1 && N(accepted) == 1, "enabled observers count actual boundary calls without duplicating them");
    Check(sampleCount == 1 && samples[0].readable && samples[0].providerHandle == 0x7000012345678
        && samples[0].definition == 456 && samples[0].location[0] == 789 && samples[0].location[1] == 987
        && samples[0].location[2] == 654 && samples[0].locationIndex == 321 && samples[0].callerAuthority == 1
        && samples[0].precondition == 1 && samples[0].accepted && samples[0].fromPromotion, "request diagnostics copy verified fields");
    probeCheckResult = false;
    Check(!RequestHook(ProbeP(), ProbeI(), 123, ProbeA()) && N(rejected) == 1, "native precondition rejection preserved");
    probeCheckResult = true; probeRequestResult = false;
    Check(!RequestHook(ProbeP(), ProbeI(), 123, ProbeA()) && N(afterCheck) == 1, "rejection after successful check distinguished");
    probeSkipCheck = true;
    Check(!RequestHook(ProbeP(), ProbeI(), 123, ProbeA()) && N(unchecked) == 1, "request returning before check distinguished");
    Check(N(promotionRequests) == 1 && !samples[1].fromPromotion, "other harvestable callers distinguished from planet promotion");
    probeSkipCheck = false; probeRequestResult = true;
    probeThrow = true;
    Check(ProbePropagatesException() && promotionDepth == 0 && currentRequest == nullptr, "native exceptions propagate with thread context restored");
    probeThrow = false;
    Sample bad;
    ReadSample(bad, 0, 0, 0);
    ObserveObject(1);
    Check(!bad.readable && N(faults) == 2, "observer read faults are contained and reported");
    LogCounts(1000);
    Check(sampleCount == 0, "main-thread logging drains bounded samples");
    for (int i = 0; i < 40; ++i) RequestHook(ProbeP(), ProbeI(), 123, ProbeA());
    Check(sampleCount == 32 && N(dropped) == 8, "sampling cap never blocks native requests");
    AcquireSRWLockExclusive(&sampleLock);
    RequestHook(ProbeP(), ProbeI(), 123, ProbeA());
    ReleaseSRWLockExclusive(&sampleLock);
    Check(N(dropped) == 9, "contended sample buffer does not wait under native locks");
    Reset(); active = true; probeParallel = true;
    HANDLE workers[4];
    for (uintptr_t i = 0; i < 4; ++i) workers[i] = CreateThread(nullptr, 0, ProbeWorker, reinterpret_cast<void*>(i), 0, nullptr);
    for (HANDLE worker : workers) {
        Check(worker != nullptr, "probe worker created");
        if (!worker) continue;
        WaitForSingleObject(worker, INFINITE);
        DWORD result = 1; GetExitCodeThread(worker, &result);
        Check(result == 0, "parallel native results preserved");
        CloseHandle(worker);
    }
    Check(N(requests) == 512 && N(accepted) == 256 && N(rejected) == 256
        && N(afterCheck) == 0 && N(unchecked) == 0 && N(promotionRequests) == 0,
        "thread-local request context separates concurrent accepted and rejected requests");
    Check(sampleCount <= 32 && N(dropped) + sampleCount == 512, "concurrent sample loss is bounded and accounted for");
    probeParallel = false;
    Reset(); SetUpNatural();
    Install(0);
    Check(installed == completeMask && hookCalls == 8, "all seven observer installation results tracked");
    g_state.naturalProbeReady = true;
    Menu_RequestDev(DevAction::EnableNatural); ProcessDev(1000);
    Check(active.load(), "enabled experiment collects promotion activity before a mining trace");
    Menu_RequestDev(DevAction::DisableNatural); ProcessDev(2000);
    Check(!active.load(), "disable stops observation unless a trace remains active");
    Menu_RequestDev(DevAction::StartTrace); ProcessDev(3000);
    Check(active.load() && !g_state.naturalEnabled, "trace can observe unmodified natural promotion");
    ProcessDev(34000);
    Check(!active.load(), "trace expiry stops observers");
    Reset(); hookFailure = 2; Install(0);
    Check(installed == (completeMask & ~4u), "partial installation is distinguishable from zero native activity");
}
alignas(8) static uint8_t biomeBuilderMemory[0x40], biomeCellMemory[0x16b0];
static uint64_t biomeProvider = 0x7000012345678;
static int biomeExpectedMode = 0, biomeBuildCalls = 0, biomeCellCalls = 0, biomeInstanceCalls = 0;
static uint32_t biomeExpectedFlags = 6;
static uintptr_t BB() { return reinterpret_cast<uintptr_t>(biomeBuilderMemory); }
static uintptr_t BC() { return reinterpret_cast<uintptr_t>(biomeCellMemory); }
static uintptr_t BP() { return reinterpret_cast<uintptr_t>(&biomeProvider); }
static void __fastcall FakeBiomeInstance(uintptr_t context, uintptr_t instance, uintptr_t provider, int mode,
    uint32_t group, uintptr_t out, uintptr_t count, uintptr_t extra, uintptr_t batch) {
    ++biomeInstanceCalls;
    Check(context == 10 && instance == ProbeI() && provider == BP() && mode == biomeExpectedMode && group == 20
        && out == 30 && count == 40 && extra == 50 && batch == 123, "all nine biome instance arguments preserved");
    if (mode == 1) NaturalProbe::RequestHook(ProbeP(), instance, batch, ProbeA());
}
static void __fastcall FakeBiomeCell(uintptr_t builder, uintptr_t cell, uintptr_t planet, uint32_t lod, uint32_t flags, uint64_t page, uint8_t option) {
    ++biomeCellCalls;
    Check(builder == BB() && cell == BC() && planet == 101 && lod == 1 && flags == biomeExpectedFlags
        && page == 0x1122334455667788ull && option == 1, "all seven biome cell arguments preserved");
    NaturalProbe::BiomeInstanceHook(10, ProbeI(), BP(), biomeExpectedMode, 20, 30, 40, 50, 123);
}
static void __fastcall FakeBiomeBuild(uintptr_t builder, uintptr_t planet, uint32_t flags) {
    ++biomeBuildCalls;
    Check(builder == BB() && planet == 101 && flags == biomeExpectedFlags, "biome build flags forwarded unchanged");
    NaturalProbe::BiomeCellHook(builder, BC(), planet, 1, flags, 0x1122334455667788ull, 1);
}
static bool BiomePropagatesException() {
    __try { NaturalProbe::BiomeBuildHook(BB(), 101, biomeExpectedFlags); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
static void TestBiomeObservers() {
    using namespace NaturalProbe;
    Reset();
    biomeBuild = FakeBiomeBuild; biomeCell = FakeBiomeCell; biomeInstance = FakeBiomeInstance;
    request = ProbeRequest; check = ProbeCheck;
    Put(biomeBuilderMemory, 8, int(0)); Put(biomeBuilderMemory, 0x28, uint8_t(1));
    Put(biomeCellMemory, 0x16a8, int(10));
    Put(probeProvider, 8, biomeProvider);
    Put(probeInstance, 0x18, uint64_t(456));
    Put(probeInstance, 0x90, uint64_t(789)); Put(probeInstance, 0x98, uint64_t(987));
    Put(probeInstance, 0xa0, uint64_t(654)); Put(probeInstance, 0xa8, uint32_t(321));
    BiomeBuildHook(BB(), 101, 6);
    Check(biomeBuildCalls == 1 && biomeCellCalls == 1 && biomeInstanceCalls == 1 && N(biomeBuilds) == 0,
        "inactive biome hooks call originals once and collect nothing");
    active = true;
    BiomeBuildHook(BB(), 101, 6);
    Check(biomeBuildCalls == 2 && biomeCellCalls == 2 && biomeInstanceCalls == 2 && N(biomeBuilds) == 1
        && N(biomeCells) == 1 && N(biomeNearCells) == 1 && N(biomeModes[0]) == 1
        && N(biomeBuildWithSpawn) == 0 && N(biomeCellWithSpawn) == 0 && N(requests) == 0,
        "client draw work remains draw work; no spawn bit or request is manufactured");
    Check(biomeSamples[0][0].readable && biomeSamples[0][0].builderType == 0 && biomeSamples[0][0].enabled == 1
        && biomeSamples[0][0].flags == 6 && biomeSamples[1][0].previousLod == 10 && biomeSamples[1][0].lod == 1
        && biomeSamples[2][0].providerHandle == biomeProvider && biomeSamples[2][0].definition == 456
        && biomeSamples[2][0].location[2] == 654 && biomeSamples[2][0].locationIndex == 321,
        "native biome context and instance fields captured");
    biomeExpectedMode = 1; biomeExpectedFlags = 14;
    BiomeBuildHook(BB(), 101, 14);
    Check(N(biomeBuildWithSpawn) == 1 && N(biomeCellWithSpawn) == 1 && N(biomeModes[1]) == 1
        && N(biomeRequests) == 1 && N(accepted) == 1 && N(promotionRequests) == 0
        && sampleCount == 1 && samples[0].fromBiome && !samples[0].fromPromotion,
        "native biome spawn request distinguished from V5 promotion");
    probeThrow = true;
    Check(BiomePropagatesException() && biomeDepth == 0 && currentRequest == nullptr,
        "biome native exceptions propagate and clear nested request context");
    probeThrow = false;
    BiomeSample bad; ReadBiomeContext(bad, 0, 0); ReadBiomeInstance(bad, 0, 0);
    Check(!bad.readable && N(faults) == 2, "biome diagnostic reads fail without changing native behavior");
    LogCounts(1000);
    Check(biomeSampleCount[0] == 0 && biomeSampleCount[1] == 0 && biomeSampleCount[2] == 0,
        "biome logging drains all three sample stages");
    for (int i = 0; i < 10; ++i) BiomeBuildHook(BB(), 101, 14);
    Check(biomeSampleCount[0] == 8 && biomeSampleCount[1] == 8 && biomeSampleCount[2] == 8 && N(biomeDropped) == 6,
        "per-stage limits preserve room for cell and instance evidence during frequent builder calls");
    AcquireSRWLockExclusive(&sampleLock);
    BiomeBuildHook(BB(), 101, 14);
    ReleaseSRWLockExclusive(&sampleLock);
    Check(N(biomeDropped) == 9, "biome observers never wait on a contended diagnostic buffer");
    Reset();
}
alignas(8) static uint8_t biomeJobMemory[0x60];
static int nearbyNativeCalls = 0, nearbyJobCalls = 0;
static uint32_t nearbySeenFlags = 0;
static bool nearbyJobThrows = false;
static void __fastcall NearbyCell(uintptr_t builder, uintptr_t cell, uintptr_t planet, uint32_t lod,
    uint32_t flags, uint64_t page, uint8_t option) {
    ++nearbyNativeCalls; nearbySeenFlags = flags;
    Check(builder == BB() && cell == BC() && planet == 101 && lod == 1
        && page == 0x1122334455667788ull && option == 1, "nearby override preserves every non-flag cell argument");
    if (flags & 8) {
        NaturalProbe::BiomeInstanceHook(10, ProbeI(), BP(), 1, 20, 30, 40, 50, 123);
        Put(biomeCellMemory, 0x16a8, int(1)); // Simulate the native completed-LOD write.
    }
}
static void __fastcall NearbyJob(uintptr_t context) {
    ++nearbyJobCalls;
    Check(context == reinterpret_cast<uintptr_t>(biomeJobMemory), "job capture pointer passed unchanged");
    if (nearbyJobThrows) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    NaturalProbe::BiomeCellHook(BB(), BC(), 101, 1, 6, 0x1122334455667788ull, 1);
}
static bool JobExceptionPropagates() {
    __try { NaturalProbe::BiomeJobHook(reinterpret_cast<uintptr_t>(biomeJobMemory)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
static void TestNearbySpawning() {
    using namespace NaturalProbe;
    Reset(); SetUpNatural();
    g_state.naturalProbeReady = true;
    biomeCell = NearbyCell; biomeInstance = FakeBiomeInstance; biomeJob = NearbyJob;
    request = ProbeRequest; check = ProbeCheck;
    probeThrow = probeParallel = probeSkipCheck = false; probeCheckResult = probeRequestResult = true;
    biomeExpectedMode = 1;
    nearbyNativeCalls = nearbyJobCalls = 0; nearbyJobThrows = false;
    Put(biomeBuilderMemory, 8, int(0)); Put(biomeBuilderMemory, 0x28, uint8_t(1));
    Put(biomeCellMemory, 0x16a8, int(100));
    Put(biomeJobMemory, 0x10, uint32_t(14)); Put(biomeJobMemory, 0x20, uint32_t(1));
    Put(biomeJobMemory, 0x28, BC()); Put(biomeJobMemory, 0x38, uint8_t(0)); Put(biomeJobMemory, 0x58, BB());
    active = true;
    BiomeJobHook(reinterpret_cast<uintptr_t>(biomeJobMemory));
    Check(nearbyNativeCalls == 1 && nearbyJobCalls == 1 && nearbySeenFlags == 6 && N(biomePromoted) == 0
        && N(requests) == 0 && N(biomeJobsBlocked) == 1 && currentBiomeJob == nullptr,
        "observation alone never promotes cell work, even when a job lacks eligibility");
    Check(biomeSamples[1][0].fromJob && biomeSamples[1][0].jobFlags == 14 && biomeSamples[1][0].jobEligible == 0
        && biomeSamples[3][0].readable, "cell records identify their actual parent job flags and eligibility");
    Check(SetNearbySpawning(true) && nearbyEnabled && !g_state.naturalEnabled && codeWrites == 0,
        "nearby experiment enables independently without changing the old V5 gate");
    BiomeJobHook(reinterpret_cast<uintptr_t>(biomeJobMemory));
    Check(nearbyNativeCalls == 2 && nearbySeenFlags == 14 && N(biomePromoted) == 1 && N(requests) == 1
        && N(accepted) == 1 && samples[0].fromBiome && N(biomeCellWithSpawn) == 0,
        "eligible cell gets only bit 8; input flags and resulting native request remain separately counted");
    Check(biomeSamples[1][1].flags == 6 && biomeSamples[1][1].forwardedFlags == 14,
        "diagnostics show exact input and modified flags");
    BiomeJobHook(reinterpret_cast<uintptr_t>(biomeJobMemory));
    Check(nearbyNativeCalls == 3 && nearbySeenFlags == 6 && N(biomePromoted) == 1 && N(requests) == 1,
        "native completed LOD prevents repeated promotion of the same cell");
    BiomeSample candidate; candidate.readable = true; candidate.builderType = 0; candidate.enabled = 1;
    candidate.flags = 6; candidate.lod = 1; candidate.previousLod = 100;
    Check(NearbyFlags(candidate) == 14, "close draw cell eligible");
    for (uint32_t flags : {0u,2u,4u,7u,14u,0x106u}) {
        auto other = candidate; other.flags = flags;
        Check(NearbyFlags(other) == flags, "physics-only, existing spawn and unknown flags untouched");
    }
    for (int type : {-1,1,2,3}) { auto other = candidate; other.builderType = type; Check(NearbyFlags(other) == 6, "other builder types untouched"); }
    for (int lod : {-1,0,1,101}) { auto other = candidate; other.previousLod = lod; Check(NearbyFlags(other) == 6, "completed or unknown old LOD untouched"); }
    auto other = candidate; other.lod = 2; Check(NearbyFlags(other) == 6, "distant cells untouched");
    other = candidate; other.enabled = 0; Check(NearbyFlags(other) == 6, "disabled builder untouched");
    other = candidate; other.readable = false; Check(NearbyFlags(other) == 6, "unreadable cells untouched");
    serverFlag = 0; Check(NearbyFlags(candidate) == 6, "non-server context rejected at worker decision"); serverFlag = 1;
    editorFlag = 1; Check(NearbyFlags(candidate) == 6, "editor context rejected at worker decision"); editorFlag = 0;
    onlineFlag = 1; Check(NearbyFlags(candidate) == 6, "online context rejected at worker decision");
    ProcessDev(1000); Check(!g_state.nearbyEnabled && !nearbyEnabled, "context loss disables nearby experiment on main tick");
    Check(!SetNearbySpawning(true), "cannot enable in online context"); onlineFlag = 0;
    g_state.naturalProbeReady = false; Check(!SetNearbySpawning(true), "partial probes cannot enable mutations"); g_state.naturalProbeReady = true;
    Check(SetNearbySpawning(true), "can enable after normal context recovery");
    ready = false; ProcessDev(1200); Check(!nearbyEnabled, "spawner loss disables experiment before early tick return"); ready = true;
    Check(SetNearbySpawning(true), "enable before native-adapter loss");
    g_state.nativeReady = false; ProcessDev(1300); Check(!nearbyEnabled, "adapter failure disables experiment"); g_state.nativeReady = true;
    Check(SetNearbySpawning(true), "enable before context read fault");
    g_natural.server = reinterpret_cast<const uint8_t*>(1);
    Check(NearbyFlags(candidate) == 6 && nearbyFaulted, "worker context fault fails closed and latches");
    g_natural.server = &serverFlag; ProcessDev(1400);
    Check(!nearbyEnabled && !SetNearbySpawning(true), "context fault disables experiment for the session");
    nearbyFaulted = false; SetNearbySpawning(true); SetNearbySpawning(false);
    Check(NearbyFlags(candidate) == 6, "explicit disable restores argument pass-through");
    active = true; nearbyJobThrows = true;
    Check(JobExceptionPropagates() && currentBiomeJob == nullptr, "native job exception propagates and clears TLS");
    nearbyJobThrows = false;
    LogCounts(1500); Check(biomeSampleCount[3] == 0, "job sample buffer drained");
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
    Reset(); SetUpNatural();
    Menu_RequestDev(DevAction::EnableNatural); ProcessDev(1000);
    Check(g_state.naturalEnabled && codeWrites == 1 && naturalGate[7] == 0xff && editorFlag == 0 && onlineFlag == 0,
        "opt-in promotion changes one comparison byte without changing global roles");
    Menu_RequestDev(DevAction::EnableNatural); ProcessDev(1100);
    Check(codeWrites == 1 && spawns == 0 && removes == 0, "enable is idempotent and does not manually spawn entities");
    Menu_RequestDev(DevAction::DisableNatural); ProcessDev(1200);
    Check(!g_state.naturalEnabled && codeWrites == 2 && !memcmp(naturalGate, kNaturalGate, sizeof(naturalGate)),
        "disable exactly restores the native gate");
    onlineFlag = 1; SetNaturalPromotion(true);
    Check(!g_state.naturalEnabled && codeWrites == 2, "online context rejects promotion experiment");
    onlineFlag = 0; editorFlag = 1; SetNaturalPromotion(true);
    Check(!g_state.naturalEnabled && codeWrites == 2, "editor context rejects promotion experiment");
    editorFlag = 0; g_state.nativeReady = false; SetNaturalPromotion(true);
    Check(!g_state.naturalEnabled && codeWrites == 2, "unverified executable cannot enable promotion");
    Reset(); SetUpNatural(); naturalGate[0] ^= 1; SetNaturalPromotion(true);
    Check(!g_state.naturalEnabled && !g_state.naturalReady && codeWrites == 0, "changed instruction fails closed");
    Reset(); SetUpNatural(); codeWriteFail = true; SetNaturalPromotion(true);
    Check(!g_state.naturalEnabled && codeWrites == 0 && !memcmp(naturalGate, kNaturalGate, sizeof(naturalGate)),
        "failed code write does not claim enabled");
    codeWriteFail = false; SetNaturalPromotion(true); onlineFlag = 1; ProcessDev(1300);
    Check(!g_state.naturalEnabled && codeWrites == 2, "context change restores the gate automatically");
    Reset(); SetUpNatural(); SetNaturalPromotion(true); ready = false; ProcessDev(1400);
    Menu_DevSnapshot(snapshot);
    Check(!g_state.naturalEnabled && !snapshot.naturalEnabled && codeWrites == 2,
        "restoration still runs and publishes when spawner is unavailable");
    Reset(); SetUpNatural(); SetNaturalPromotion(true); g_state.nativeReady = false; ProcessDev(1500);
    Check(!g_state.naturalEnabled && codeWrites == 2, "diagnostic fault suspension restores promotion gate");
    Reset(); SetUpNatural(); SetNaturalPromotion(true); codeWriteFail = true; onlineFlag = 1;
    ProcessDev(1600); ProcessDev(1700);
    Check(g_state.naturalEnabled && g_natural.restoreFailed && codeWrites == 1,
        "failed restoration remains visible and suppresses automatic retries");
    codeWriteFail = false; SetNaturalPromotion(false);
    Check(!g_state.naturalEnabled && !g_natural.restoreFailed, "explicit disable can retry failed restoration");
    TestPromotionObservers();
    TestBiomeObservers();
    TestNearbySpawning();
    printf("DEV lifecycle: %s (%d failures)\n", failures ? "FAILED" : "PASS", failures);
    return failures ? 1 : 0;
}
