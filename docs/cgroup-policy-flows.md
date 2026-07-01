# Cgroup Policy Mechanisms and Flows

Cgroup policy control implements cgroup-scoped GPU control without adding a custom
Linux cgroup controller. The design uses existing cgroup v2 identity as the
ownership key, `/dev/vgpuctl` as the policy injection boundary, NVIDIA ioctl
hooks as the enforcement point, and debugfs as the verification surface.

## 1. Overall Architecture

```mermaid
flowchart TD
    Kubelet["kubelet"] --> DP["vCUDA device-plugin"]
    DP --> Select["select GPU slice policy"]
    Select --> Resolve["resolve container cgroup v2 id"]
    Resolve --> CTL["/dev/vgpuctl ioctl"]
    CTL --> CPolicy["cgroup policy table\nkey: cgroup_id + gpu_minor"]

    App["CUDA process"] --> Dev["/dev/nvidia* / /dev/nvidiactl"]
    Dev --> Hook["vCUDA-kernel kprobe hooks"]
    Hook --> Cgid["read current cgroup_id"]
    Hook --> Tgid["read current tgid"]
    Cgid --> Resolver["policy resolver"]
    Tgid --> Resolver
    CPolicy --> Resolver
    TPolicy["tgid policy table\ntgid compatibility"] --> Resolver
    Resolver --> Compute["compute timeslice path"]
    Resolver --> Memory["memory accounting path"]
    Compute --> Trace["debugfs: timeslices / stats / events"]
    Memory --> CStats["debugfs: cgroups / stats / events"]
```

Principle:

- Device-plugin owns policy creation because Kubernetes knows which pod/container should receive a GPU slice.
- Kernel owns enforcement because CUDA user-space can bypass LD_PRELOAD hooks.
- cgroup v2 id is the stable runtime identity used to bind a container to a GPU policy.
- tgid policy remains supported and has higher priority, preserving tgid-level behavior and debugging tools.

## 2. Cgroup Identity Flow

```mermaid
flowchart TD
    Proc["current task"] --> Hook["NVIDIA open/ioctl/release hook"]
    Hook --> Helper["vgpu_cgroup_current_id()"]
    Helper --> KernelAPI["task_dfl_cgroup(current)"]
    KernelAPI --> CgroupID["cgroup_id(cgrp)"]
    CgroupID --> TaskCtx["task context snapshot"]
    TaskCtx --> TasksDebug["/sys/kernel/debug/vgpu/tasks"]
```

Principle:

- The kernel reads cgroup identity at the same point it observes NVIDIA device activity.
- `tasks` debugfs shows the observed `cgroup_id`, allowing userspace/device-plugin to confirm id matching.
- No custom cgroup controller is registered, so the module stays out-of-tree friendly.

Key files:

- `core/vgpu_cgroup.c`
- `core/vgpu_task.c`
- `ctl/vgpu_debugfs.c`

## 3. Policy Injection Flow

```mermaid
sequenceDiagram
    participant DP as device-plugin / helper
    participant CTL as /dev/vgpuctl
    participant UAPI as vgpu_ctl_ioctl
    participant Table as cgroup policy rhashtable
    participant DBG as debugfs

    DP->>CTL: open("/dev/vgpuctl")
    DP->>UAPI: VGPU_IOCTL_SET_CGROUP_POLICY(policy)
    UAPI->>UAPI: validate cgroup_id, gpu_minor, flags, weight, memory limit
    UAPI->>Table: insert or replace by (cgroup_id, gpu_minor)
    UAPI-->>DP: 0 or errno
    DP->>UAPI: VGPU_IOCTL_GET_CGROUP_POLICY(query)
    UAPI->>Table: lookup by (cgroup_id, gpu_minor)
    UAPI-->>DP: found + policy
    DBG->>Table: /sys/kernel/debug/vgpu/cgroup_policies
```

Principle:

- `/dev/vgpuctl` is the control-plane boundary.
- `VGPU_IOCTL_SET_CGROUP_POLICY` is idempotent for the same `(cgroup_id, gpu_minor)` key.
- `VGPU_IOCTL_GET_CGROUP_POLICY` gives the device-plugin a read-back check before trusting enforcement.
- debugfs is read-only diagnostic state, not the control API.

Policy shape:

```c
struct vgpu_cgroup_policy {
    __u64 cgroup_id;
    __s32 gpu_minor;
    __u32 reserved0;
    __u64 memory_limit_bytes;
    __u32 compute_weight;
    __u32 flags;
};
```

Flags:

- `VGPU_POLICY_F_MEMORY`: apply memory limit accounting.
- `VGPU_POLICY_F_COMPUTE`: apply compute timeslice scaling.
- `VGPU_POLICY_F_DRY_RUN`: record would-deny/would-rewrite without write-capable enforcement.

Key files:

- `include/vgpu_types.h`
- `include/vgpu_ioctl.h`
- `ctl/vgpu_ctl.c`
- `core/vgpu_policy.c`

## 4. Policy Resolver Flow

```mermaid
flowchart TD
    Ioctl["NVIDIA ioctl observed"] --> Keys["collect tgid, gpu_minor, cgroup_id"]
    Keys --> TLookup["lookup tgid policy"]
    TLookup -->|hit| TScope["policy_scope=1 tgid"]
    TLookup -->|miss| CLookup["lookup cgroup policy"]
    CLookup -->|hit| CScope["policy_scope=2 cgroup"]
    CLookup -->|miss| NScope["policy_scope=0 none"]
    TScope --> ComputePath["compute/memory path"]
    CScope --> ComputePath
    NScope --> TraceOnly["trace no_policy reason"]
```

Principle:

- Priority is fixed: `tgid > cgroup > none`.
- tgid policy is useful for targeted debugging or compatibility with tgid-level tools.
- cgroup policy is the Kubernetes path because all container processes share cgroup ownership.
- `policy_scope` is recorded in timeslice traces so verification can prove which policy source won.

Debug evidence:

```text
/sys/kernel/debug/vgpu/timeslices
policy_scope=0  # no policy
policy_scope=1  # tgid policy
policy_scope=2  # cgroup policy
```

Key files:

- `nvidia/vgpu_nv_ioctl.c`
- `ctl/vgpu_events.h`
- `ctl/vgpu_events.c`
- `ctl/vgpu_debugfs.c`

## 5. Compute Timeslice Enforcement Flow

```mermaid
flowchart TD
    RM["RM_CONTROL ioctl"] --> Ctrl["read ctrl cmd"]
    Ctrl --> IsTSG{"ctrl cmd == SET_TIMESLICE?"}
    IsTSG -->|no| Return["return, trace only"]
    IsTSG -->|yes| ReadTS["read old timesliceUs"]
    ReadTS --> Resolve["resolve policy: tgid > cgroup > none"]
    Resolve --> HasCompute{"policy has COMPUTE flag?"}
    HasCompute -->|no| NoCompute["record no_compute_flag"]
    HasCompute -->|yes| Scale["new = old * compute_weight / 10000"]
    Scale --> Clamp["apply min/max module clamps"]
    Clamp --> Same{"new == old?"}
    Same -->|yes| Unchanged["record unchanged"]
    Same -->|no| Would["record TIMESLICE_WOULD_REWRITE"]
    Would --> CanWrite{"enforcing mode and not dry-run?"}
    CanWrite -->|no| Dry["record policy_dry_run/not_enforcing"]
    CanWrite -->|yes| Write["write new timesliceUs into ioctl params"]
    Write --> Done["record TIMESLICE_REWRITTEN or write_failed"]
```

Principle:

- NVIDIA TSG scheduling uses a timeslice field inside an RM control parameter block.
- The hook reads the requested `timesliceUs`, scales it by `compute_weight`, then optionally writes it back before the real NVIDIA ioctl handler consumes it.
- `compute_weight=10000` means 100%; `5000` means 50% relative timeslice.
- Dry-run mode records what would happen without mutating user/kernel ioctl memory.

Verification:

```bash
make verify-cgroup-compute
cat /sys/kernel/debug/vgpu/timeslices | tail -n 20
```

Expected cgroup path evidence:

```text
policy_scope=2 cgroup_id=<non-zero> name=TIMESLICE_WOULD_REWRITE
```

Key files:

- `nvidia/vgpu_nv_ioctl.c`
- `scripts/verify_cgroup_compute.sh`

## 6. Memory Accounting Flow

```mermaid
flowchart TD
    Alloc["RM allocation ioctl"] --> Parse["parse RM alloc detail"]
    Parse --> Match{"matches memory class/detail gates?"}
    Match -->|no| Ignore["ignore for accounting"]
    Match -->|yes| Size["extract allocation size"]
    Size --> Obj{"has RM object handle?"}
    Obj -->|yes| ObjCharge["charge object by handle"]
    Obj -->|no| RawCharge["charge raw size"]
    ObjCharge --> TaskStats["task memory_used_bytes"]
    RawCharge --> TaskStats
    ObjCharge --> CgroupStats["cgroup aggregate memory"]
    RawCharge --> CgroupStats
    CgroupStats --> Limit{"over cgroup memory_limit?"}
    Limit -->|yes dry-run| WouldDeny["increment would_deny and keep accounting"]
    Limit -->|yes enforce future| Denied["deny path reserved"]
    Limit -->|no| Accounted["record alloc_seen"]
```

Principle:

- RM allocation object handles are the safest key for avoiding duplicate counting.
- Duplicate object charge replaces the previous size, so aggregate memory tracks current live object size.
- cgroup aggregate stats are separate from task-local stats. Task stats remain useful for process-level debugging; cgroup stats are the Kubernetes accounting surface.
- Cgroup policy control applies cgroup memory limit in dry-run first. It records `would_deny`, but does not block allocations yet.

Debug evidence:

```text
/sys/kernel/debug/vgpu/cgroups
cgroup_id=<id> gpu_minor=255 memory_used_bytes=... alloc_seen=... free_seen=... would_deny=...
```

Key files:

- `core/vgpu_cgroup_mem.c`
- `nvidia/vgpu_nv_ioctl.c`
- `ctl/vgpu_debugfs.c`
- `scripts/verify_cgroup_memory.sh`

## 7. Memory Free Flow

```mermaid
flowchart TD
    Free["RM free ioctl"] --> ParseFree["parse free object or size"]
    ParseFree --> HasObj{"free object handle found?"}
    HasObj -->|yes| TaskObj["task uncharge object"]
    HasObj -->|yes| CgroupObj["cgroup uncharge object"]
    HasObj -->|no| HasSize{"free size found?"}
    HasSize -->|yes| TaskSize["task uncharge size"]
    HasSize -->|yes| CgroupSize["cgroup uncharge size"]
    HasSize -->|no| Unmatched["increment unmatched_free"]
    CgroupObj --> Debug["/sys/kernel/debug/vgpu/cgroups free_seen++"]
    CgroupSize --> Debug
```

Principle:

- Free by object is preferred because it mirrors allocation object tracking.
- If only size exists, the cgroup aggregate subtracts size conservatively.
- Underflow clamps to zero rather than wrapping, preventing bogus large memory usage.

## 8. Device-Plugin Runtime Flow

```mermaid
sequenceDiagram
    participant K as kubelet
    participant DP as vCUDA device-plugin
    participant RT as container runtime
    participant KERN as vCUDA-kernel
    participant APP as CUDA app

    K->>DP: Allocate(vcuda slice)
    DP->>DP: select gpu_minor, memory_limit, compute_weight
    DP->>RT: resolve container cgroup path/id
    DP->>KERN: VGPU_IOCTL_SET_CGROUP_POLICY
    DP->>KERN: VGPU_IOCTL_GET_CGROUP_POLICY
    DP-->>K: return device specs/env
    K->>RT: start container
    APP->>KERN: NVIDIA ioctl activity
    KERN->>KERN: match current cgroup_id to policy
    KERN->>KERN: account memory / rewrite timeslice
    DP->>KERN: read debugfs health diagnostics
```

Principle:

- Device-plugin does scheduling and policy ownership.
- Kernel module does low-level enforcement and accounting.
- Debugfs gives health and verification state, but should not become the control plane.

Device-plugin docs:

- `../device-plugin/README.md`
- `../device-plugin/docs/kernel-uapi.md`
- `../device-plugin/docs/call-sequence.md`

## 9. Verification Matrix

```mermaid
flowchart LR
    VC["make verify-cgroup"] --> CID["tasks shows cgroup_id"]
    VCP["make verify-cgroup-policy"] --> POL["cgroup_policies has injected policy"]
    VCC["make verify-cgroup-compute"] --> TS["timeslices policy_scope=2"]
    VCM["make verify-cgroup-memory"] --> MEM["cgroups memory + would_deny"]
    VK["make test-kunit"] --> UNIT["policy/cgroup memory/event unit coverage"]
```

Expected PASS output:

```text
PASS: verify-cgroup
PASS: verify-cgroup-policy cgroup_id=...
PASS: verify-cgroup-compute cgroup_id=...
PASS: verify-cgroup-memory cgroup_id=...
```

Failure output starts with `FAILED:` and includes the failing condition.

## 10. Current Limits

- Cgroup memory limit is dry-run first; hard allocation denial is intentionally not enabled yet.
- Policy deletion is not implemented. Device-plugin cleanup should overwrite stale policy with a harmless dry-run policy until delete UAPI exists.
- Userspace cgroup path to kernel `cgroup_id` resolution must be validated on the target host.
- `gpu_minor=255` remains the observed global/control-device path in current validation; per-minor matching can be tightened when GPU minor attribution is stable for every RM call.
