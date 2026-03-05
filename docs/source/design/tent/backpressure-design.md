# TENT Backpressure when hitting limits — 分析与设计

> 对应 Issue [#1058: Mooncake Transfer Engine NEXT (TENT) Roadmap](https://github.com/kvcache-ai/Mooncake/issues/1058)  
> Phase 2 – Advanced Capabilities / **Connection & Resource Limits**

---

## 需求状态

| 子项 | 状态 |
|------|------|
| Max concurrent transfers or NIC connections | ❌ 未实现 |
| Connection pool reuse & fair queuing | 🔶 部分实现（有连接池，无公平调度） |
| **Backpressure when hitting limits** | ❌ **未实现** |

---

## 现有相关机制分析

### 1. 连接池（Connection Pool）

**位置**：`mooncake-transfer-engine/tent/include/tent/transport/rdma/endpoint_store.h`

存在两种 `EndpointStore` 实现：
- `FIFOEndpointStore`：按 FIFO 顺序驱逐最老的 endpoint
- `SIEVEEndpointStore`：基于 NSDI '24 SIEVE 算法

两者都有 `max_size_` 字段，但**满时只做驱逐（evict），不阻止新连接的建立，也不向上层传递背压信号**。

### 2. WR Depth Quota（RDMA QP 层限制）

**位置**：`mooncake-transfer-engine/tent/include/tent/transport/rdma/endpoint.h`

每个 `RdmaEndPoint` 维护 `wr_depth_list_`（per-QP 的 WR 深度计数器），通过 `reserveQuota()` 和 `cancelQuota()` 控制单个 QP 内的未完成 WR 数量。

这是 **RDMA 硬件层面**的微观限制，不是应用层的并发/连接数背压。

### 3. BoundedMPSCQueue（有容量但无背压语义）

**位置**：`mooncake-transfer-engine/tent/include/tent/common/concurrent/bounded_mpsc_queue.h`

`Workers` 使用 `BoundedMPSCQueue<RdmaSliceList, 8192>` 作为任务队列，但 `push()` 在队列已满时**无限自旋**，无法向调用方传递"队列满"的信号：

```cpp
void push(T &slice_list) {
    while (true) {      // 满时无限自旋，不返回错误
        // ...
        } else if (dif < 0) {
            std::this_thread::yield();  // 仅 yield，不通知上层
        }
    }
}
```

另外，`WorkerContext` 中已有 `inflight_slices` 字段（`std::atomic<int64_t>`）做动态追踪，但**未被用于限流**。

### 4. DeviceQuota（NIC 负载均衡，非背压）

**位置**：`mooncake-transfer-engine/tent/include/tent/transport/rdma/quota.h`

基于自适应反馈（exponential smoothing）的 NIC 选择策略，最小化预测完成时间。此为**负载均衡**机制，不对总并发量做硬性限制。

---

## 背压的含义 vs 当前行为

| 特性 | 背压的期望行为 | 当前实际行为 |
|------|--------------|------------|
| 并发 transfer 上限 | 达到上限后拒绝/阻塞/限速新请求 | 无此配置，无限接入 |
| NIC 连接上限 | 超过 `max_connections` 时阻塞或返回错误 | 满时只驱逐旧连接 |
| 队列满时行为 | 返回 `ResourceExhausted` / 阻塞 / 限速 | 生产者无限自旋 |
| Fair queuing | 多客户端公平占用带宽 | 不存在 |

---

## 设计方案

### 层次 1：配置层

在 `Config`（`config.h`）中新增配置项：

```json
{
  "rdma": {
    "max_concurrent_transfers": 1024,
    "max_connections_per_device": 32,
    "backpressure_policy": "block"
  }
}
```

| 配置项 | 说明 |
|--------|------|
| `max_concurrent_transfers` | 全局最大并行 transfer slice 数，0 表示不限 |
| `max_connections_per_device` | 每个 RDMA 设备最大 endpoint 数 |
| `backpressure_policy` | `block`（等待）/ `error`（返回错误）/ `drop`（丢弃） |

### 层次 2：Worker 任务队列层

**目标文件**：`workers.h` / `workers.cpp`

1. **`BoundedMPSCQueue` 新增 `try_push()`**

```cpp
// bounded_mpsc_queue.h
bool try_push(T &slice_list) {
    // 尝试一次 CAS，失败则立即返回 false
    // ...
}
```

2. **`Workers::submit()` 根据策略实施背压**

```cpp
Status Workers::submit(RdmaSliceList &slice_list, int worker_id) {
    // 检查 inflight_slices 上限
    if (max_concurrent_ > 0 &&
        worker_context_[wid].inflight_slices.load() >= max_concurrent_) {
        if (policy_ == BackpressurePolicy::kError)
            return Status::ResourceExhausted("transfer queue full");
        if (policy_ == BackpressurePolicy::kBlock)
            // 等待 inflight_slices 下降（条件变量）
    }
    // ...
}
```

### 层次 3：连接池层

**目标文件**：`endpoint_store.h` / `endpoint_store.cpp`

修改 `getOrInsert()` 逻辑：

- 当 `size() >= max_size_` 时，**不再新建连接**，改为：
  - `block`：等待已有 endpoint 空闲（基于 `condition_variable`）
  - `error`：返回 `Status::ResourceExhausted`
  - 复用方案：从现有连接中选 `inflight_slices` 最少的（公平调度）

### 层次 4：上层状态传播

**目标文件**：`rdma_transport.cpp` → `transfer_engine_impl.cpp`

将 `Status::ResourceExhausted` 从 `submitTransferTasks()` 传播到 `submitTransfer()`，让调用方（如 vLLM 集成层）能感知背压并做重试/限速。

---

## 实现优先级建议

```
P1: 配置项支持（config.h）
P2: Workers::submit() 检查 inflight_slices 上限 + error 策略
P3: BoundedMPSCQueue::try_push() 非阻塞接口
P4: EndpointStore max_connections 硬限 + block/error 策略
P5: Fair queuing（连接池中选最小 inflight 的 endpoint）
```

P1–P3 改动量小、风险低，适合作为社区 first PR 的范围。

---

## 涉及文件汇总

| 文件 | 修改内容 |
|------|---------|
| `include/tent/common/config.h` | 新增配置项声明 |
| `include/tent/common/concurrent/bounded_mpsc_queue.h` | 新增 `try_push()` |
| `include/tent/transport/rdma/workers.h` | 新增限流相关成员变量 |
| `src/transport/rdma/workers.cpp` | `submit()` 实现限流逻辑 |
| `include/tent/transport/rdma/endpoint_store.h` | 新增 `max_connections` 参数 |
| `src/transport/rdma/endpoint_store.cpp` | `getOrInsert()` 实现连接数限制 |
| `src/transport/rdma/rdma_transport.cpp` | 传播 `ResourceExhausted` 状态 |
| `src/transfer_engine.cpp` | 上层 API 补充背压说明 |

---

*最后更新：2026-03-05*
