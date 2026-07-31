# 渲染系统设计

## 目标

基于 SDL3 GPU API 的渲染系统，消费 Logic Thread 生成的 RenderProxy，驱动 Render Thread 绘制。

## 已有基础

- `noix::video::Renderer`：当前仅清屏
- SDL3 GPU API（pipeline/cmdBuf/pass/fence 模型，SPIR-V only）
- RenderProxy + 三缓冲 RenderProxyBuffer（游戏对象管理设计中定义）

## 核心概念

### 渲染线程模型

- **单线程渲染**：渲染命令录制和提交在主线程完成
- 暂不做多线程命令录制（未来可扩展为多线程录制+单线程提交）
- **当前架构**：主线程 Event Poll + Render Tick（暂无 Logic Thread）
- **未来架构**：三线程 — Script Thread / Logic Thread / Main Thread（Render）
- Logic Thread 引入后，通过 RenderProxy + 三缓冲衔接 Render Thread

```
当前:
  Main Thread:  Event Poll → Render Tick
  Script Thread: EventBus callbacks

未来（引入 Logic Thread 后）:
  Main Thread:  Event Poll → Render Tick (consume RenderProxyBuffer)
  Logic Thread: World::tick() → System → generate RenderProxy → push buffer
  Script Thread: EventBus callbacks → push Request (SPSC ring queue)
```

### 渲染对象

- 主线程维护一组**渲染对象**（RenderObject），每个对应一个可绘制的视觉实体
- 每帧遍历所有 RenderObject，生成 GPU 渲染指令（绑定管线、设置参数、绘制），最后提交
- RenderObject 持有 GPU 资源引用（pipeline、texture、vertex buffer 等），可直接用于命令录制

### 渲染分组（RenderBatch）

- RenderObject 按渲染特征**分组存储**，减少管线切换和状态变更
- 分组维度：
  - **Material**：相同材质的对象归为一组，管线/纹理/uniform 一次绑定
  - **透明/不透明**：不透明组先绘制（可乱序），透明组后绘制（需按深度排序）
- 同组内连续录制命令，GPU 状态切换最少化
- 分组在 RenderProxy 消费时动态维护：RenderObject 创建/更新时自动归入对应分组

### 纹理图集（TextureAtlas）

- 小纹理（UI 图标、Tile、Sprite 动画帧等）合并为**纹理图集**，减少纹理切换
- 图集在资源加载阶段由外部工具或运行时打包生成
- 使用图集的 RenderObject 通过 UV 偏移和缩放定位子纹理
- 同一图集内的对象可共享同一个纹理绑定，减少 DrawCall

### 合批绘制（Instancing / Batched Draw）

- 同材质 + 同图集的小对象**合批为一次 DrawCall**
- 合批策略：
  - **实例化绘制（Instancing）**：相同 mesh + 相同材质，仅 transform 不同的对象，用一次 DrawCall 绘制多个实例
  - **动态合批**：不同小 mesh 但同材质的对象，合并顶点数据到一个 buffer，一次 DrawCall 绘制
- 合批由 Scene 在帧渲染前自动处理，对上层透明

```
不合批（6 DrawCall）:          合批后（2 DrawCall）:
  obj1(tile_A) → DrawCall        atlas_0(instanced) → DrawCall
  obj2(tile_A) → DrawCall          obj1, obj2, obj3 合批
  obj3(tile_A) → DrawCall        atlas_1(instanced) → DrawCall
  obj4(tile_B) → DrawCall          obj4, obj5, obj6 合批
  obj5(tile_B) → DrawCall
  obj6(tile_B) → DrawCall

帧渲染顺序:
  1. Opaque 组：按材质分组 → 自动合批 → 录制命令
  2. Transparent 组：按材质分组 → 按深度排序 → 录制命令
  3. Submit
```

### 渲染流水线（RenderPipeline）

- Renderer 只提供**定制流水线、组织 Pipeline** 的能力，不内置具体渲染行为
- 后处理、bloom、tone mapping 等具体效果由**外部 Shader 资源 + 脚本**定义和驱动
- Renderer 负责执行流水线中定义的 Pass 序列，每个 Pass 的行为由其绑定的 Shader 决定
- 内置 Forward Pipeline 仅作为默认起步，可被外部覆盖

```
Forward Pipeline:
  ┌──────────────────────────────────┐
  │ Pass 1: Opaque (color + depth)   │ → swapchain
  │ Pass 2: Transparent (depth-sort) │ → swapchain
  └──────────────────────────────────┘

Deferred Pipeline:
  ┌──────────────────────────────────┐
  │ Pass 1: G-Buffer (albedo+normal+ │ → offscreen textures
  │          depth+material)          │
  │ Pass 2: Lighting (fullscreen quad)│ → offscreen color
  │ Pass 3: Transparent (forward)    │ → offscreen color
  │ Pass 4: Composite → swapchain    │
  └──────────────────────────────────┘

Offscreen Pipeline:
  ┌──────────────────────────────────┐
  │ Pass 1: Render to target texture │ → user texture
  └──────────────────────────────────┘
```

#### RenderPass

- 每个 Pass 声明：
  - **输入**：依赖的纹理/buffer（上一 Pass 的输出）
  - **输出**：颜色目标 + 可选深度模板目标
  - **执行体**：遍历哪些 RenderBatch 组，如何录制命令
- Pass 之间通过 offscreen texture 传递数据
- Pass 的执行顺序由流水线定义，Scene 依次执行

#### 自定义流水线

- 外部可通过 API 组装自定义流水线：
  ```
  scene->setPipeline(CustomPipeline()
      .addPass("shadow",  output: shadow_map)
      .addPass("gbuffer", output: [albedo, normal, depth])
      .addPass("lighting", input: [albedo, normal, depth, shadow_map], output: lit_color)
      .addPass("forward_transparent", input: [depth], output: lit_color)
      .addPass("composite", input: [lit_color], output: swapchain)
  );
  ```
- 默认使用内置 Forward Pipeline

- RenderProxy 设计延后：先实现正确的 RenderObject，根据实现反馈调整设计后再定义 RenderProxy schema

### 资源管理

- 渲染器管理 GPU 资源（Pipeline、Texture、Mesh），场景是脚本层上层业务概念，渲染器不感知
- 资源通过批量接口更新，采用**差量更新**策略：对比当前集合与目标集合，仅销毁不再需要的、创建新增的、保留未变的
- 例：当前 `[builtin, A, B, C]`，设置 `[B, C, D]` → 结果 `[builtin, B, C, D]`（销毁 A，保留 B/C，创建 D）

```
批量更新流程:
  renderer->setPipelines({B, C, D});
  内部 diff:
    keep:   [B, C]        ← 仍在目标集合中
    remove: [A]           ← 不在目标集合中，销毁 GPU 对象
    add:    [D]           ← 新增，创建 GPU 对象
    result: [builtin, B, C, D]
```

- 差量更新适用于所有资源类型：Pipeline、Texture、Mesh
- 内置资源（`noix:builtin-*`）始终保留，不被销毁

### 窗口与 Swapchain

- SDL3 自动管理 swapchain 的创建和重建（窗口 resize、全屏切换时自动适配）
- `SDL_WaitAndAcquireGPUSwapchainTexture()` 每帧返回当前尺寸的 texture，最小化时返回 NULL（跳过本帧）
- **Renderer 需检测窗口尺寸变化**，自动重建依赖窗口尺寸的 offscreen texture（G-Buffer、后处理 buffer 等）
- 上层无需感知 swapchain 生命周期

### 帧控制

- Renderer 运行在主线程，帧率由 Renderer 自身的渲染函数控制
- 帧节奏：计算距上一帧的时间差，未达目标帧间隔则直接返回（不渲染、不提交）
- VSync 和目标帧率通过**线程安全 API** 设置，脚本等外部线程可随时调用
- 设置持久化到 ConfigManager（如 `noix:renderer.vsync`、`noix:renderer.target_fps`）
- Logic Thread 有独立的 tick 节奏，与 Renderer 帧率解耦

```
Renderer::render():
  elapsed = now - last_frame_time
  if elapsed < target_interval:
    return          // 未到帧间隔，跳过
  last_frame_time = now
  // ... 录制命令、提交 ...

线程安全 API:
  renderer.setVSync(bool)        // 原子操作
  renderer.setTargetFPS(int)     // 原子操作
  renderer.vsync() → bool
  renderer.targetFPS() → int
```

#### 管线缓存（PipelineCache）

- 存储已创建的 `SDL_GPUGraphicsPipeline`
- 管线创建开销大，缓存后同配置的管线只创建一次
- 外部在合适时机**批量创建/重置**管线缓存（如场景切换、资源配置变更时）
- RenderObject 引用管线缓存中的 pipeline 句柄，不自行创建管线
- **Shader 管线通过 NamespacedId 加载**：如 `noix:opaque-sprite`，从 AssetManager 解析对应的 SPIR-V 资源

#### 纹理缓存（TextureCache）

- 存储已上传的 `SDL_GPUTexture`，按 NamespacedId 索引
- 纹理上传开销大，缓存避免重复上传
- 外部通过 NamespacedId 请求纹理，首次使用时从 AssetManager 加载并上传 GPU，后续直接引用
- 支持批量预加载和卸载（场景切换时）

#### 网格缓存（MeshCache）

- 网格（模型）也是资源，按 NamespacedId 索引和缓存
- 存储 `SDL_GPUBuffer`（Vertex + Index Buffer），首次使用时从 AssetManager 加载并上传 GPU
- 与纹理/管线一致的生命周期管理：预加载、卸载、场景切换时清理
- 合批时：在 RenderObject 生命周期变更（创建/销毁）时进行顶点合并，合并后的结果存在渲染队列中
- 暂时只内置 `noix:builtin-quad`

#### 材质（Material）

- 材质 = 管线 + 纹理绑定 + uniform 参数的组合描述
- 材质定义了"用什么管线、贴什么纹理、传什么参数"
- **材质通过 NamespacedId 加载**：如 `noix:mat-player`，从 AssetManager 解析材质定义文件（JSON）
- RenderObject 引用材质，而非直接引用管线和纹理
- 材质是渲染分组的关键维度：相同材质的对象天然在同一分组

#### RenderObject 自定义 Uniform

- RenderObject 可携带**自定义 uniform 数据**，在材质 uniform 之上覆盖/扩展
- 分为两级：
  - **材质级 uniform**：材质定义中的默认值，同材质所有对象共享
  - **对象级 uniform**：RenderObject 自身携带，按对象覆盖（如 tint 颜色、UV 偏移、动画参数等）
- 渲染时：先绑定材质级 uniform，再覆盖对象级 uniform
- 对象级 uniform 通过 RenderProxy 从 Logic Thread 传递

```
材质级 uniform (共享):
  mat-player: { tint: [1,1,1,1], uv_scale: [1,1] }

对象级 uniform (覆盖):
  obj1: { tint: [1,0,0,1] }       ← 红色变体
  obj2: { uv_offset: [0.5,0] }    ← 动画帧偏移
  obj3: {}                         ← 使用材质默认值

渲染时绑定顺序:
  1. Bind Pipeline
  2. Bind Material Uniforms (基线)
  3. Bind Object Uniforms (覆盖)
  4. Draw
```

```
材质定义文件 (assets/noix/materials/mat-player.json):
{
  "pipeline": "noix:opaque-sprite",
  "textures": {
    "albedo": "noix:player-sprite",
    "normal": null
  },
  "uniforms": {
    "tint": [1.0, 1.0, 1.0, 1.0],
    "uv_offset": [0.0, 0.0]
  }
}

Shader管线定义文件 (assets/noix/pipelines/opaque-sprite.json):
{
  "vertex_shader": "noix:sprite.vert",
  "fragment_shader": "noix:sprite.frag",
  "blend_mode": "none",
  "topology": "triangle_list"
}

渲染流水线定义文件 (assets/noix/render_pipelines/forward.json):
{
  "passes": [
    { "name": "opaque", "target": "swapchain", "sort": "none" },
    { "name": "transparent", "target": "swapchain", "sort": "back_to_front" }
  ]
}
```

#### 内置资源（Builtin Resources）

- 渲染器启动时自动加载一组内置基础资源，无需外部定义
- 内置资源用 `noix:builtin-*` 命名空间标识
- 内置资源包括：

| NamespacedId | 类型 | 用途 |
|---|---|---|
| `noix:builtin-forward` | RenderPipeline | 默认前向渲染流水线 |
| `noix:builtin-sprite` | Pipeline | 2D 精灵渲染管线 |
| `noix:builtin-sprite.vert` | Shader | 2D 精灵顶点着色器 |
| `noix:builtin-sprite.frag` | Shader | 2D 精灵片段着色器 |
| `noix:builtin-sdf-text.vert` | Shader | SDF 文字顶点着色器（内置默认） |
| `noix:builtin-sdf-text.frag` | Shader | SDF 文字片段着色器（内置默认） |
| `noix:builtin-default` | Texture | 2x2 白灰棋盘纹理（默认/占位/缺失纹理提示） |
| `noix:builtin-quad` | Mesh | 单位四边形顶点缓冲 |

- 内置 SPIR-V 字节码直接嵌入 C++ 源码（`incbin` 或字节数组），不依赖外部文件
- 启动流程：`Renderer::init()` → 加载内置 shader → 创建内置 pipeline → 注册内置材质
- 外部资源可覆盖内置资源（同 NamespacedId 优先使用外部定义）

#### SDF 文字渲染

- 使用 SDL_TTF 生成 **SDF（Signed Distance Field）纹理**，利用 SDL3 提供的 SDF 纹理功能
- 框架职责边界：
  - **框架提供**：文字 → SDF 纹理的生成管线（字体加载、字形光栅化、SDF 纹理上传 GPU）
  - **外部自定义**：SDF 纹理的渲染 Shader（轮廓、发光、阴影等效果由 Shader 实现）
- 内置默认 SDF 渲染 Shader（`noix:builtin-sdf-text.*`），可被外部同名资源覆盖
- 字体资源通过 NamespacedId 加载（如 `noix:font-main`），框架自动管理字形缓存和纹理图集

```
文字渲染流程:
  Font(NamespacedId) + Text → SDL_TTF → SDF Glyph Texture → TextureAtlas(字形图集)
                                                          ↓
  Material(pipeline: custom-sdf-shader, texture: glyph_atlas) → RenderObject

自定义文字效果:
  外部只需编写新的 Fragment Shader，在 Material 中引用即可:
  - 描边：SDF 阈值多采样
  - 发光：SDF 距离衰减
  - 阴影：SDF 偏移采样
```

#### 资源加载链路

```
NamespacedId → AssetManager → 资源文件 → 解析 → GPU 对象
  noix:forward-pipeline   → forward.json   → RenderPipeline
  noix:opaque-sprite      → opaque-sprite.json → SDL_GPUGraphicsPipeline
  noix:mat-player         → mat-player.json → Material
  noix:player-sprite      → player.png     → SDL_GPUTexture
  noix:sprite.vert        → sprite.vert.spv → SDL_GPUShader
```
