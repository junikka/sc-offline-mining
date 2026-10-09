#include "mining.h"
#include "hooks.h"
#include "patches.h"
#include "spawner.h"
#include "teleport.h"
#include <atomic>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace Mining {
// Native contract and user-confirmed manual savepoint: docs/natural-mining.md.
static constexpr char kGameHash[] = "3953f8b1a9894d5d9d1836e50939b726141f1829fe65a3a622d5acb2c6f89162";
static constexpr uintptr_t kCellRva = 0x26593c0;
static constexpr char kCellEntry[] = "44 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 48";
using CellFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uint32_t, uint32_t, uint64_t, uint8_t);
static CellFn originalCell = nullptr;
static const uint8_t* server = nullptr;
static const uint8_t* editor = nullptr;
static const uint8_t* online = nullptr;
static std::atomic<bool> verified{false}, ready{false}, faulted{false};
static std::atomic<uint64_t> cells{0}, promoted{0}, faults{0};
static DWORD lastTraceAt = 0;
static int lastState = -1;

static void Fault() { faults.fetch_add(1, std::memory_order_relaxed); faulted.store(true); }
static bool ContextAllowed() {
    if (!ready.load() || faulted.load()) return false;
    __try {
        return g_tp.ok && SpawnerReady() && server && *server == 1
            && editor && *editor == 0 && online && *online == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { Fault(); return false; }
}
static uint32_t CellFlags(uintptr_t builder, uintptr_t cell, uint32_t lod, uint32_t flags) {
    // Identical eligibility and flag change to the working manual experiment.
    // Type 0 excludes the separate planet-side entity branch (types 1/2/3).
    if (flags != 6 || lod > 1 || !ContextAllowed()) return flags;
    __try {
        if (!builder || !cell || Rd<int>(builder + 8) != 0 || Rd<uint8_t>(builder + 0x28) != 1) return flags;
        const int previousLod = Rd<int>(cell + 0x16a8);
        if (previousLod < 2 || previousLod > 100) return flags;
        return flags | 8;
    } __except (EXCEPTION_EXECUTE_HANDLER) { Fault(); return flags; }
}
static void __fastcall CellHook(uintptr_t builder, uintptr_t cell, uintptr_t planet, uint32_t lod,
    uint32_t flags, uint64_t page, uint8_t option) {
    cells.fetch_add(1, std::memory_order_relaxed);
    const uint32_t forwarded = CellFlags(builder, cell, lod, flags);
    if (forwarded != flags) promoted.fetch_add(1, std::memory_order_relaxed);
    // Preserve native provider checks, location/depletion state, batch submission
    // and completed LOD writes. Native exceptions propagate; never retry here.
    originalCell(builder, cell, planet, lod, forwarded, page, option);
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

static bool EntryMatches(uintptr_t base) {
    __try {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(base + kCellRva);
        return p >= g_text.base && p + 16 <= g_text.base + g_text.size && BytesMatch(p, kCellEntry)
            && g_isOnlineFlag == reinterpret_cast<const uint8_t*>(base + 0x9e2ec6e);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
} // namespace Mining

void ResolveMiningApi() {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    Mining::verified.store(Mining::ExecutableMatches());
    if (!Mining::verified.load() || !Mining::EntryMatches(base)) {
        Log("[mining] Automatic natural spawning unavailable: executable or native entry checks differ.");
        return;
    }
    Mining::server = reinterpret_cast<const uint8_t*>(base + 0x9e2e908);
    Mining::editor = reinterpret_cast<const uint8_t*>(base + 0x9e2ec66);
    Mining::online = g_isOnlineFlag;
    // Five bytes: MOV [RSP+0x20],R9D. Whole instruction, no relative operand.
    Mining::ready.store(HookFunction(reinterpret_cast<uint8_t*>(base + Mining::kCellRva), 5,
        reinterpret_cast<void*>(&Mining::CellHook), reinterpret_cast<void**>(&Mining::originalCell)));
    Log("[mining] Automatic natural spawning: native cell hook %s. Active in offline server context; no DEV action needed.",
        Mining::ready.load() ? "ready" : "unavailable");
}

bool MiningBuildVerified() { return Mining::verified.load(); }

void GetMiningSnapshot(MiningSnapshot& out) {
    out.buildVerified = Mining::verified.load();
    out.hookReady = Mining::ready.load();
    out.active = Mining::ContextAllowed();
    out.faulted = Mining::faulted.load();
    out.cells = Mining::cells.load(std::memory_order_relaxed);
    out.promoted = Mining::promoted.load(std::memory_order_relaxed);
    out.faults = Mining::faults.load(std::memory_order_relaxed);
}

void ProcessMining(DWORD now, bool trace) {
    MiningSnapshot state;
    GetMiningSnapshot(state);
    const int current = state.faulted ? 3 : !state.hookReady ? 0 : state.active ? 2 : 1;
    if (current != Mining::lastState) {
        static const char* labels[] = { "unavailable", "waiting for offline server context", "active automatically", "disabled after read fault; restart required" };
        Log("[mining] Natural spawning %s.", labels[current]);
        Mining::lastState = current;
    }
    if (trace && now - Mining::lastTraceAt >= 1000) {
        Log("[mining/trace] automatic=%d cells=%llu promotedCells=%llu readFaults=%llu", state.active, state.cells, state.promoted, state.faults);
        Mining::lastTraceAt = now;
    }
}
