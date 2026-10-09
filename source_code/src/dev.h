#pragma once
#include "common.h"
#include "mining.h"

// Temporary experiments. Requests cross from the menu thread to the game thread;
// the menu only reads a copied snapshot and never holds engine pointers.
enum class DevAction { None, SpawnRock, EnableContact, RemoveRock, Refresh, StartTrace, StopTrace, InspectMarkers };
struct DevRockMetrics {
    uint64_t id = 0;
    char name[96] = {};
    bool valid = false, globalParams = false, updateEnabled = false;
    int authority = -1, composition = -1, recentHitSources = -1, queuedHitSources = -1;
    float mass = 0, volume = 0, resistance = 0, baseResistance = 0;
    float charge = 0, pendingCharge = 0, capacity = 0, decayPerSecond = 0;
};
struct DevMiningController {
    uint64_t id = 0;
    uint64_t scanEntry = 0, scanEntity = 0;
    uint32_t scanEntryType = 0;
    char name[96] = {};
    int authority = -1;
    bool firing = false, focused = false, scanned = false, fractureMode = false;
    float throttle = 0, transfer = 0, lastHitRate = 0, hudMass = 0, hudResistance = 0;
    DevRockMetrics target;
};
constexpr int kDevMaxControllers = 8;
struct DevSnapshot {
    bool nativeReady = false, spawnerReady = false, busy = false, live = false;
    bool mineable = false, harvestable = false, signature = false;
    int authority = -1, compositionEntries = -1;
    int databanks = 0, contacts = 0, targetable = 0, scanData = 0, scanTokens = 0;
    uint64_t rockId = 0;
    int fixture = 0;
    float spawnScale = 1.0f;
    DevRockMetrics metrics;
    DevMiningController controllers[kDevMaxControllers];
    int controllerCount = 0, traceSeconds = 0;
    uint64_t inspectedShip = 0;
    bool tracing = false;
    int markerSamples = 0, markerLive = 0, markerMineable = 0;
    bool markersInspected = false, markerTruncated = false;
    MiningSnapshot mining;
    char status[256] = "Spawn a rock and try normal scanning. The contact button is an optional fallback.";
    char apiStatus[160] = "Waiting for the offline mod to initialize.";
};

void ResolveDevApi();
void ProcessDev(DWORD now);
void Menu_DevSnapshot(DevSnapshot& out);
void Menu_RequestDev(DevAction action, int fixture = 0, float distance = 25.0f, float scale = 1.0f);
const char* DevRockClass(int fixture);
