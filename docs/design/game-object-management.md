# 游戏对象管理系统设计

## 目标

基于 ECS 架构的游戏对象管理系统，使用原型（Prototype）模式定义和创建对象。

## 核心概念

### Entity（实体）

- 轻量级 ID，不包含任何逻辑和数据
- 仅作为 Component 的聚合标识
- 可通过 Prototype 实例化创建

### Component（组件）

- 纯数据容器，不包含逻辑，完全扁平化
- **C++ 框架不感知具体字段**：Component 类型由脚本在初始化时通过 API 动态注册，C++ 侧只维护 schema
- 支持的字段类型：`bool`、`int32`、`float`、`string`（字符串尽量避免，必要时用字符串池）
- **字段句柄化**：Schema 注册时为每个字段分配 FieldHandle（整数 ID），运行时通过 handle → offset → 直接内存访问，O(1)，不做字符串匹配
- 可序列化为 Value，供脚本系统读取（只读快照）
- C++ 根据已注册的 Component schema 计算内存布局（offset、size、alignment）

### System（系统）

- **由 Request 驱动**，而非由 Component 组合驱动
- System 定义输入契约（语义层面的字段名+类型），编译时确定
- 脚本通过 Request 将 FieldHandle 绑定到 System 的输入契约，实现数据来源映射
- System 执行时遍历**该 Request 的 FieldHandle 绑定所覆盖的 Entity**（拥有所有绑定 Component 的 Archetype）
- **同一 System 可被不同 Request 以不同 FieldHandle 绑定调用**
- 按 priority 排序执行
- System 自身也可产生新的 Request（如碰撞后 DestroyEntity）
- 接口预留依赖声明能力（dependsOn / before / after），当前仅按优先级排序，未来可切换为依赖图拓扑排序+并行

### Request（请求）

- System 与 Component 之间的解耦核心
- 脚本创建 Request 时指定：
  - 目标 System
  - FieldHandle → System 输入的绑定映射
- Request 的 FieldHandle 绑定承担两个职责：
  1. **数据寻址**：告诉 System 数据在 Storage 中的 offset
  2. **遍历范围定义**：拥有所有绑定 Component 的 Entity 集合即为 System 要遍历的范围

```js
// 脚本创建 Request，绑定 FieldHandle 到 System 输入
ecs.request("PhysicsStep", {
    position: [Transform.x, Transform.y],  // FieldHandle 绑定
    velocity: [Motion.vx, Motion.vy],
    mass:     PhysicsBody.mass
});
// → PhysicsSystem 遍历所有拥有 Transform+Motion+PhysicsBody 的 Entity
// → 通过 bindings 中的 FieldHandle 从 Storage 读写数据
```

### CommandBuffer（命令缓冲）

- 脚本不直接读写 Component 字段，而是通过推送 Request 录制变更意图
- **环形队列双缓冲**，Script 写 / Logic 读
- Request 分为两类：
  - **数据变更类**：`SetField`、`AddComponent`、`RemoveComponent`、`SpawnEntity`、`DestroyEntity` — 由 World 直接应用到 Storage
  - **System 驱动类**：携带 FieldHandle 绑定的 Request — 由对应 System 消费执行
- CommandBuffer 消费是框架级步骤：逻辑帧开始时 World 自动 drain，按 Request 类型分发

### 脚本交互模型

- 脚本通过事件驱动（订阅 EventBus 事件）
- 事件回调中，脚本通过 `ecs.request()` API 向 CommandBuffer 推送 Request
- 脚本**不直接访问 Component 数据**，所有变更走 CommandBuffer
- Component 对脚本只提供只读快照
- 脚本通过 Request 的 FieldHandle 绑定连接 System 与 Component，C++ 不感知具体字段

### Prototype（原型）

- 定义 Entity 的 Component 组合及默认值
- 类似 ECS 中的 Archetype 概念：相同 Prototype 的 Entity 共享相同的 Component 集合
- **继承 = 创建时复用**：子 Prototype 从父拷贝 Component 集合+默认值，创建后与父无关联
- 原型数据可序列化，支持从 JSON/脚本定义
- **Component Schema 在脚本初始化阶段注册，之后锁定（不可再增删字段类型）**
- Entity 上的 Component 可动态添加/移除（通过 CommandBuffer），触发 Archetype 迁移

### World（世界）

- 顶层容器，持有所有 Entity、Component 存储、System 注册表
- 提供 Entity 的创建/销毁/查询 API
- World::tick() 负责帧循环：drain CommandBuffer → 分发 Request → 执行 System → 生成 RenderProxy

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                           World                              │
│  ┌─────────┐  ┌──────────┐  ┌───────────┐                  │
│  │ Entity   │  │Component │  │  System    │                  │
│  │ Registry │  │ Storage  │  │ Registry   │                  │
│  └─────────┘  └──────────┘  └───────────┘                  │
│  ┌──────────────────┐  ┌──────────────────────────────┐     │
│  │ Prototype        │  │ CommandBuffer (ring, dbl-buf) │     │
│  │ Registry         │  │ ┌──────────────────────────┐ │     │
│  │                  │  │ │ 数据变更类 Request       │ │     │
│  │                  │  │ │ - SetField / Spawn / ... │ │     │
│  │                  │  │ ├──────────────────────────┤ │     │
│  │                  │  │ │ System驱动类 Request     │ │     │
│  │                  │  │ │ - target + bindings      │ │     │
│  │                  │  │ │   {FieldHandle → input}  │ │     │
│  └──────────────────┘  │ └──────────────────────────┘ │     │
│                        └──────────────────────────────┘     │
└─────────────────────────────────────────────────────────────┘

System 执行流程:
  Request("PhysicsStep", bindings) → 解析 bindings 涉及的 Component type_ids
                                   → 筛选包含所有这些 Component 的 Archetype
                                   → System 遍历 Entity，通过 FieldHandle → offset 读写数据
```

## 线程模型

### 三线程架构

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  Script      │     │  Logic       │     │  Render      │
│  Thread      │     │  Thread      │     │  Thread      │
│              │     │  World::tick │     │              │
│ EventBus     │     │ drain+apply  │     │ Render Queue │
│ callbacks    │     │ System exec  │     │ traverse+draw│
│ ecs.request()│     │              │     │              │
└──────┬───────┘     └──────┬───────┘     └──────┬───────┘
       │                    │                     │
       │  CommandBuffer     │  RenderProxyBuffer  │
       │  (ring, spinlock)  │  (ring, latest-wins)│
       └────────────────────┘                     │
                            └────────────────────┘
```

### Script Thread ↔ Logic Thread 协调

- 双缓冲环形队列 CommandBuffer，Script 写 / Logic 读
- **帧边界 atomic swap**：Logic 帧开始时 swap 读写端
- **不可丢弃事件**：缓冲满时脚本端原子变量自旋锁（spinlock）短暂等待，直到 Logic 消费腾出空间
- 无条件变量 / 无内核态切换

### Logic Thread ↔ Render Thread 协调

- Logic Thread 执行完 System 后，从有视觉表现的 Entity 生成 **RenderProxy（渲染代理/视图对象）**
- RenderProxy 只包含渲染所需数据（Transform 位置、Sprite 纹理引用等），不含逻辑字段
- RenderProxy 压入 **RenderProxyBuffer**（三缓冲环形队列）
- Render Thread 每帧从缓冲中读取最新一帧的 RenderProxy，更新自己维护的**渲染对象队列**
- 渲染对象队列由 Render Thread 独占，无需加锁
- **latest wins 降级**：渲染消费慢时，Logic 丢弃旧帧写入最新状态，Render 始终读到最近完整快照

### RenderProxy（渲染代理）

- 由 Logic Thread 在 System 执行完毕后生成
- 每个 RenderProxy 对应一个可见 Entity 的渲染快照
- 包含：entity_id、position/rotation/scale、texture_ref、z_order、blend_mode 等
- 是 Component 数据的**只读投影**，不包含非视觉字段（如 AI、PhysicsBody 内部状态）
- **全局增量，局部全量**：
  - World 级增量：Logic 只推送本帧变更的 RenderProxy
  - 对象级全量：每个被推送的 RenderProxy 是完整快照，不做字段级 diff
  - Render 侧按 entity_id 全量替换：`renderQueue[id] = proxy`

### 帧循环

```
World::tick():
  1. swap CommandBuffer (atomic)
  2. drain CommandBuffer:
     - 数据变更类 Request → 直接 apply 到 Storage
     - System驱动类 Request → 按类型分发给对应 System
  3. for system in systems (by priority):
       for each Request targeting this system:
         resolve bindings → 筛选 Archetype → 遍历 Entity
  4. drain System 产生的 Request → apply
  5. 生成变更 Entity 的 RenderProxy → write RenderProxyBuffer (增量)
  6. swap RenderProxyBuffer (atomic)
```

```
                    Script Thread                    Logic Thread              Render Thread
                    ─────────────                    ────────────              ─────────────

  ┌─────────┐    ┌──────────────┐                   ┌──────────┐
  │  Event   │───▶│    Script    │──▶ cmdBuf(ring) ──▶│  World   │──▶ Storage
  │  (SDL /  │    │  callbacks   │    atomic swap     │  tick()  │
  │  EventBus)│    │  ecs.request │                   └─────┬────┘
  └─────────┘    └──────────────┘                          │
                                                     ┌──────┴──────┐
                                                     │  Systems    │
                                                     │  Request    │
                                                     │  driven     │
                                                     └──────┬──────┘
                                                            │
                                                     ┌──────┴──────┐
                                                     │ RenderProxy │
                                                     │ 增量生成     │
                                                     └──────┬──────┘
                                                            │ atomic swap
                                                     ┌──────▼──────┐
                                                     │ RenderProxy │
                                                     │ Buffer(ring│
                                                     │ triple-buf)│
                                                     └──────┬──────┘
                                                            │ read
                                                     ┌──────▼──────┐
                                                     │  Render     │
                                                     │  Thread     │
                                                     └─────────────┘
```

### Component Schema Registry（组件模式注册表）

- 脚本初始化时通过 API 注册 Component 类型，返回字段 handle 映射：
  ```js
  const Transform = ecs.defineComponent("Transform", {
      x: "float", y: "float", rotation: "float"
  });
  // Transform.x → FieldHandle, Transform.y → FieldHandle, Transform.rotation → FieldHandle

  const Sprite = ecs.defineComponent("Sprite", {
      texture: "string", width: "float", height: "float"
  });
  ```
- C++ 收到注册请求后：
  1. 记录字段名→FieldHandle 映射（字符串仅注册时使用）
  2. 计算该 Component 的 size、alignment、每个字段的 offset
  3. 为该 Component 分配固定 type_id
- **注册阶段结束后锁定**，运行时不再接受新 Component 类型定义
- 运行时通过 FieldHandle 查找 offset，O(1) 直接访问

### Component Storage（基于原型的存储）

- 同一 Archetype 的 Entity 在内存中连续排列
- 每个 Archetype 维护一组 dense array，每个 Component 类型一列
- 列内部按 Schema 描述的 offset 访问字段（通过 FieldHandle），cache 友好
- 字符串字段用 ref（offset+length）指向独立字符串池（避免变长破坏对齐）
- 字符串尽量少用，必须时用字符串池保证 O(1) 访问

```
Archetype [Transform, Sprite]:
  Transform 列 (size=12, align=4):
    FieldHandle#0 → offset 0: x  (float)
    FieldHandle#1 → offset 4: y  (float)
    FieldHandle#2 → offset 8: rotation (float)
  Sprite 列 (size=16, align=4):
    FieldHandle#3 → offset 0: texture (string ref: offset+length)
    FieldHandle#4 → offset 8: width  (float)
    FieldHandle#5 → offset 12: height (float)

  行存储: [Transform_row0][Sprite_row0] [Transform_row1][Sprite_row1] ...
```

## 接口定义

> 待补充

## 与现有系统集成

> 待补充

## 已决问题

| # | 问题 | 决定 | 理由 |
|---|------|------|------|
| 1 | Prototype 继承 | 创建时复用，不维护运行时继承关系 | 创建后与父原型无关联 |
| 2 | Component 动态添加/移除 | 支持，通过 CommandBuffer 请求，World::tick 中执行 Archetype 迁移 | Schema 字段不可变，但 Entity 的 Component 集合可变 |
| 3 | System 执行模型 | 固定优先级顺序，预留依赖声明接口 | 当前单线程无收益，未来可切换拓扑排序+并行 |
| 4 | 嵌套 Component / entity_ref | 不支持，完全扁平化 | Component 只有原始类型字段，保持简单 |
| 5 | 字符串存储 | 尽量少用，必要时字符串池 + ref | O(1) 访问，行存储定长 |
| 5b | 字段名句柄化 | Schema 注册时分配 FieldHandle，运行时 handle→offset→内存访问 | 避免字符串匹配，O(1) |
| 6 | Script ↔ Logic 同步 | 双缓冲环形队列 + atomic swap + spinlock 降级 | 无条件变量，无内核切换；事件不可丢弃，满时自旋等待 |
| 7 | RenderProxy 策略 | 全局增量，局部全量 | Logic 只推变更对象；每个对象完整快照；Render 按 id 替换 |
| 9 | System 驱动模型 | Request 驱动，脚本绑定 FieldHandle | System 不感知 Component，脚本通过 Request 映射数据来源+定义遍历范围 |

| 8 | 缓冲策略 | 三缓冲 | 逻辑/渲染帧率解耦更彻底，避免 Render 帧率低于 Logic 时丢帧 |

## 待决问题

（无）
