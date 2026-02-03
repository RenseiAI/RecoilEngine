# Agent: platform-mac

## Purpose
Create macOS-specific platform code for Apple Silicon, particularly CPU topology
detection which is needed for optimal thread scheduling on big.LITTLE architectures.

## Owned Files
- `rts/System/Platform/Mac/CpuTopology.cpp` (NEW - create this)
- `rts/System/Platform/Mac/CpuTopology.h` (NEW - if needed)

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`
Branch: `arm64-metal-port`

PR #2540 added ARM64 CPU topology detection for Linux (via `/sys/devices/system/cpu/`
sysfs interface). macOS doesn't have sysfs. Instead, Apple provides `sysctl` APIs.

### Existing patterns

**Linux ARM64 topology** (from PR #2540) is in:
`rts/System/Platform/Linux/CpuTopology.cpp`

It implements these functions (defined in `rts/System/Platform/CpuTopology.h`):
```cpp
namespace cpu_topology {
    struct ProcessorMasks {
        uint32_t efficiencyCoreMask;
        uint32_t performanceCoreMask;
        uint32_t hyperThreadLowMask;
        uint32_t hyperThreadHighMask;
    };
    struct ProcessorGroupCaches { ... };
    struct ProcessorCaches { ... };
    enum ThreadPinPolicy { ... };

    ProcessorMasks GetProcessorMasks();
    ProcessorCaches GetProcessorCache();
    ThreadPinPolicy GetThreadPinPolicy();
}
```

**Windows topology** is in `rts/System/Platform/Win/CpuTopology.cpp`.

The macOS version must implement the same interface.

### Apple Silicon specifics

Apple Silicon has P-cores (Performance) and E-cores (Efficiency):
- M1: 4P + 4E
- M1 Pro/Max: 8P + 2E or 6P + 2E
- M2/M3/M4: various configurations

**sysctl APIs to use:**
```cpp
#include <sys/sysctl.h>

// Total CPU count
int cpu_count;
size_t size = sizeof(cpu_count);
sysctlbyname("hw.ncpu", &cpu_count, &size, NULL, 0);

// Performance levels (macOS 12+)
// hw.nperflevels = number of performance levels (usually 2)
// hw.perflevel0.physicalcpu = P-core count
// hw.perflevel1.physicalcpu = E-core count
// hw.perflevel0.l2cachesize = P-core L2 cache
// hw.perflevel1.l2cachesize = E-core L2 cache

int nperflevels;
sysctlbyname("hw.nperflevels", &nperflevels, &size, NULL, 0);

int pcores;
sysctlbyname("hw.perflevel0.physicalcpu", &pcores, &size, NULL, 0);

int ecores;
sysctlbyname("hw.perflevel1.physicalcpu", &ecores, &size, NULL, 0);
```

**Thread affinity on macOS:**
macOS does NOT support `pthread_setaffinity_np`. Instead, use QoS classes:
```cpp
#include <pthread/qos.h>
pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); // P-cores
pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);       // E-cores
```

For the CPU topology masks, since macOS doesn't expose per-CPU affinity:
- P-cores: CPUs 0 to (pcores-1)
- E-cores: CPUs pcores to (pcores + ecores - 1)
- Apple Silicon has no SMT (no hyperthreading), so SMT masks are 0

### Cache topology

```cpp
// L2 cache sizes per performance level
int64_t p_l2_cache;
size = sizeof(p_l2_cache);
sysctlbyname("hw.perflevel0.l2cachesize", &p_l2_cache, &size, NULL, 0);

int64_t e_l2_cache;
sysctlbyname("hw.perflevel1.l2cachesize", &e_l2_cache, &size, NULL, 0);

// System-level L2 cache (if per-level not available)
int64_t l2_cache;
sysctlbyname("hw.l2cachesize", &l2_cache, &size, NULL, 0);
```

## Tasks

1. Create `rts/System/Platform/Mac/CpuTopology.cpp`
2. Implement `GetProcessorMasks()`:
   - Use `sysctlbyname("hw.nperflevels")` to detect big.LITTLE
   - Build P-core and E-core masks based on `hw.perflevel*.physicalcpu`
   - SMT masks should be 0 (Apple Silicon has no hyperthreading)
3. Implement `GetProcessorCache()`:
   - Query cache sizes via `hw.perflevel*.l2cachesize`
   - Build ProcessorGroupCaches with P-core and E-core groups
4. Implement `GetThreadPinPolicy()`:
   - Return `THREAD_PIN_POLICY_ANY_PERF_CORE`
5. Guard with `#if defined(__APPLE__)` and appropriate includes
6. Add the file to the build system (document the CMakeLists.txt change needed since
   you don't own CMakeLists.txt files)

## Build system note (cross-cutting - document only)
The build system needs to know to compile Mac/CpuTopology.cpp on macOS instead of
Linux/CpuTopology.cpp. Look at how the existing platform files are selected in:
- `rts/builds/legacy/CMakeLists.txt`
- `rts/System/Platform/` CMakeLists if one exists

## Output
- Create branch `agent/platform-mac` from current HEAD
- Commit the new CpuTopology.cpp
- Document any CMakeLists.txt changes needed
