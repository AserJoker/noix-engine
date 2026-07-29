# SDL3 GPU API 参考文档

> 基于 SDL3 `include/SDL3/SDL_gpu.h` (4655 行) 整理，用于 noix-engine 图形层开发参考。

## 1. 设计哲学

SDL3 GPU API 是跨平台、低级图形 API，仿照 Vulkan/Metal/D3D12 设计：

- **显式状态管理**：管线、Pass、绑定全部显式声明，无隐式状态转换
- **薄抽象层**：对底层图形 API 的相对薄封装
- **不抽象着色器**：各后端需各自的着色器格式（Vulkan=SPIR-V, D3D12=DXBC/DXIL, Metal=MSL/metallib），应用声明支持的格式，SDL 选择对应后端
- **固定特性集**：面向广泛硬件支持，不包含光线追踪/Mesh Shader 等前沿特性，所有特性保证可用无需查询
- **环缓冲模型（Cycle）**：资源（Texture/Buffer/TransferBuffer）以环缓冲方式运作，写操作有 `cycle` 布尔参数，自动轮转到下一个未被挂起 CommandBuffer 占用的内部资源
- **左手坐标系**：遵循 D3D12/Metal 惯例，NDC 左下(-1,-1)到右上(1,1)，Z 从 0(近)到 1(远)

## 2. 核心不透明类型

| 类型 | 用途 |
|------|------|
| `SDL_GPUDevice` | GPU 上下文，拥有所有资源。通过 `SDL_CreateGPUDevice()` 创建 |
| `SDL_GPUBuffer` | GPU 缓冲区（顶点/索引/间接绘制/存储等） |
| `SDL_GPUTransferBuffer` | 暂存缓冲区，用于 CPU→GPU 上传或 GPU→CPU 下载，可 CPU 映射 |
| `SDL_GPUTexture` | GPU 纹理（2D/2D数组/3D/Cube/Cube数组） |
| `SDL_GPUSampler` | 纹理采样器（过滤/寻址/各向异性/LOD） |
| `SDL_GPUShader` | 编译后的着色器对象（顶点或片段阶段） |
| `SDL_GPUGraphicsPipeline` | 预编译图形管线状态对象 |
| `SDL_GPUComputePipeline` | 预编译计算管线 |
| `SDL_GPUCommandBuffer` | 命令录制器，获取→录制→提交，单次使用 |
| `SDL_GPURenderPass` | 活动渲染 Pass 的临时句柄 |
| `SDL_GPUComputePass` | 活动计算 Pass 的临时句柄 |
| `SDL_GPUCopyPass` | 活动拷贝 Pass 的临时句柄 |
| `SDL_GPUFence` | GPU 同步原语 |

## 3. 后端架构

`SDL_GPUDevice` 内部包含函数指针表（~70 个），各后端填充自己的实现：

| 后端 | 平台 | 要求 |
|------|------|------|
| Vulkan | Windows/Linux/Android/Switch | Vulkan 1.0 + VK_KHR_swapchain + VK_KHR_maintenance1 |
| D3D12 | Windows 10+/Xbox | Feature Level 11_0 + Resource Binding Tier 2 |
| Metal | macOS 10.14+/iOS/tvOS 13.0+ | Apple Silicon 或 Intel Mac2 |

着色器格式标志：
- `SDL_GPU_SHADERFORMAT_SPIRV` — Vulkan
- `SDL_GPU_SHADERFORMAT_DXBC` — D3D12 (SM5_1)
- `SDL_GPU_SHADERFORMAT_DXIL` — D3D12 (SM6_0)
- `SDL_GPU_SHADERFORMAT_MSL` — Metal 源码
- `SDL_GPU_SHADERFORMAT_METALLIB` — Metal 预编译

运行时交叉编译可使用 **SDL_shadercross** (https://github.com/libsdl-org/SDL_shadercross)。

## 4. Device 创建

```c
SDL_GPUDevice* SDL_CreateGPUDevice(
    SDL_GPUShaderFormat format_flags,  // 位掩码
    bool debug_mode,
    const char* name                   // "vulkan"/"direct3d12"/"metal"/NULL(自动)
);
```

SDL 根据应用提供的着色器格式和平台可用性选择后端。

## 5. 渲染管线工作流

### 初始化阶段（启动时一次）

1. `SDL_CreateGPUDevice()` — 创建 GPU 上下文
2. `SDL_ClaimWindowForGPUDevice()` — 关联窗口，创建交换链
3. `SDL_SetGPUSwapchainParameters()` — 配置 HDR/SDR、呈现模式
4. 创建着色器：`SDL_CreateGPUShader()`
5. 创建图形管线：`SDL_CreateGPUGraphicsPipeline()`
6. 创建缓冲区：`SDL_CreateGPUBuffer()`
7. 创建纹理：`SDL_CreateGPUTexture()`
8. 创建采样器：`SDL_CreateGPUSampler()`
9. 创建传输缓冲区：`SDL_CreateGPUTransferBuffer()`
10. 上传数据：`MapTransferBuffer` → 写入 → `UnmapTransferBuffer` → `UploadToGPUTexture/Buffer`

### 每帧渲染循环

```
SDL_AcquireGPUCommandBuffer()             — 获取命令缓冲区
SDL_WaitAndAcquireGPUSwapchainTexture()   — 获取交换链纹理（挂起帧过多时阻塞）
SDL_BeginGPURenderPass()                  — 开始渲染 Pass（颜色目标 + 可选深度模板）
  SDL_BindGPUGraphicsPipeline()           — 绑定管线
  SDL_SetGPUViewport()                    — 设置视口
  SDL_BindGPUVertexBuffers()              — 绑定顶点缓冲区
  SDL_BindGPUIndexBuffer()                — 绑定索引缓冲区
  SDL_BindGPUFragmentSamplers()           — 绑定纹理-采样器对
  SDL_PushGPUVertexUniformData()          — 推送顶点 Uniform
  SDL_PushGPUFragmentUniformData()        — 推送片段 Uniform
  SDL_DrawGPUIndexedPrimitives()          — 执行绘制调用
SDL_EndGPURenderPass()                    — 结束渲染 Pass，清除所有渲染状态
SDL_SubmitGPUCommandBuffer()              — 提交到 GPU，自动呈现交换链纹理
```

### 带同步（Fence）

```c
SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);
while (!SDL_QueryGPUFence(device, fence)) { /* 等待 */ }
SDL_ReleaseGPUFence(device, fence);
```

## 6. Pass 类型

三种互斥的 Pass 类型，可在同一 CommandBuffer 中交错：

### Render Pass
`SDL_BeginGPURenderPass()` / `SDL_EndGPURenderPass()`
- 最多 8 个颜色目标 + 1 个深度模板目标
- 支持 MSAA 解析纹理
- 每个目标独立 Load/Store 操作

### Compute Pass
`SDL_BeginGPUComputePass()` / `SDL_EndGPUComputePass()`
- 可写存储纹理/缓冲区需提前声明
- 同一 Pass 内的 Dispatch 无隐式同步

### Copy Pass
`SDL_BeginGPUCopyPass()` / `SDL_EndGPUCopyPass()`
- 数据传输（上传/下载/拷贝）
- GPU→GPU 拷贝在 GPU 时间线上有隐式同步

## 7. 图形管线状态

`SDL_GPUGraphicsPipelineCreateInfo` 封装所有固定功能状态：

- **着色器**：顶点 + 片段（独立的 `SDL_GPUShader` 对象）
- **顶点输入状态**：缓冲区描述（slot/pitch/inputRate）+ 属性（location/bufferSlot/format/offset）
- **图元类型**：TRIANGLELIST, TRIANGLESTRIP, LINELIST, LINESTRIP, POINTLIST
- **光栅化状态**：填充模式(FILL/LINE)、剔除模式、正面方向、深度偏移
- **多重采样状态**：采样数、Alpha-to-Coverage
- **深度模板状态**：比较操作、模板操作(前/后)、深度测试/写入/模板测试
- **目标信息**：颜色目标描述（格式 + 混合状态）、深度模板格式

管线创建代价高，应一次性创建，跨帧复用。

## 8. 着色器资源绑定布局

### SPIR-V (Vulkan) — Descriptor Sets
- 顶点着色器：Set 0 = 采样器/存储纹理/存储缓冲区; Set 1 = Uniform 缓冲区
- 片段着色器：Set 2 = 采样器/存储纹理/存储缓冲区; Set 3 = Uniform 缓冲区
- 计算着色器：Set 0 = 采样器/只读存储; Set 1 = 读写存储; Set 2 = Uniform 缓冲区

### DXBC/DXIL (D3D12) — Register Spaces
- 顶点：(t[n],space0)=纹理/存储; (s[n],space0)=采样器; (b[n],space1)=Uniform
- 片段：(t[n],space2)=纹理/存储; (s[n],space2)=采样器; (b[n],space3)=Uniform
- 计算：(t[n],space0)=采样器+只读; (u[n],space1)=读写; (b[n],space2)=Uniform

### MSL (Metal) — 装饰器
- `[[buffer(N)]]` = Uniform/只读存储/读写存储
- `[[texture(N)]]` = 采样/只读存储/读写存储
- `[[sampler(N)]]` = 采样器

## 9. Uniform 数据系统

- 每着色器阶段 4 个 slot（顶点/片段/计算各 4 个）
- 每个 slot 容量 32KB (`UNIFORM_BUFFER_SIZE = 32768`)
- 通过 `SDL_PushGPUVertexUniformData(cmdBuf, slot, data, length)` 等推送
- 数据在整个 CommandBuffer 生命周期内持续有效，直到被覆盖
- 必须遵循 **std140 布局**（vec3/vec4 按 16 字节对齐）
- 更大的数据应使用存储缓冲区

## 10. 同步模型

- **CommandBuffer 按提交顺序执行**：提交 A 的所有命令在提交 B 的任何命令之前开始
- **Fence**：`SubmitGPUCommandBufferAndAcquireFence()` 返回 fence，`QueryGPUFence()` 轮询，`WaitForGPUFences()` 阻塞
- **在飞帧数**：`SDL_SetGPUAllowedFramesInFlight()` 配置（1-3，默认 2），控制延迟/吞吐量权衡
- **Cycle**：写操作的 `cycle=true` 时，若资源被挂起的 CommandBuffer 绑定，自动轮转到下一个空闲内部资源
- **下载**：`DownloadFromGPUTexture/Buffer()` 的数据在 fence 信号前不保证可用
- **Pass 内隐式同步**：上传和 GPU→GPU 拷贝在后续命令中隐式同步；同一 Compute Pass 内的 Dispatch **不是**隐式同步的

## 11. 资源限制

```
MAX_TEXTURE_SAMPLERS_PER_STAGE   16
MAX_STORAGE_TEXTURES_PER_STAGE    8
MAX_STORAGE_BUFFERS_PER_STAGE     8
MAX_UNIFORM_BUFFERS_PER_STAGE     4
MAX_COMPUTE_WRITE_TEXTURES        8
MAX_COMPUTE_WRITE_BUFFERS         8
UNIFORM_BUFFER_SIZE           32768
MAX_VERTEX_BUFFERS               16
MAX_VERTEX_ATTRIBUTES            16
MAX_COLOR_TARGET_BINDINGS         8
MAX_PRESENT_COUNT                16
MAX_FRAMES_IN_FLIGHT              3
```

## 12. 纹理格式

80+ 种格式：
- 标准颜色：R8G8B8A8_UNORM, B8G8R8A8_UNORM 等
- 压缩：BC1-BC7 (S3TC/Basis), ASTC (4x4 到 12x12)
- 深度：D16, D24, D32_FLOAT, D24+S8, D32+S8
- 有符号/无符号浮点、整数、sRGB 变体

纹理用途标志：SAMPLER, COLOR_TARGET, DEPTH_STENCIL_TARGET, GRAPHICS_STORAGE_READ, COMPUTE_STORAGE_READ/WRITE/SIMULTANEOUS_READ_WRITE

纹理类型：2D, 2D_ARRAY, 3D, CUBE, CUBE_ARRAY

## 13. 交换链与呈现

- `SDL_ClaimWindowForGPUDevice()` — 创建交换链
- 默认：SDR + VSYNC
- 查询支持：`WindowSupportsGPUSwapchainComposition()` / `WindowSupportsGPUPresentMode()`
- 设置参数：`SDL_SetGPUSwapchainParameters()`
- HDR 支持：EXTENDED_LINEAR (R16G16B16A16_FLOAT) 和 HDR10_ST2084 (A2R10G10B10)
- 呈现模式：VSYNC / IMMEDIATE / MAILBOX

## 14. Blit 和 Mipmap

- `SDL_BlitGPUTexture()` — 纹理区域间的过滤/拷贝 Blit，**必须在 Pass 外调用**
- `SDL_GenerateMipmapsForGPUTexture()` — 自动生成 Mipmap 链，**必须在 Pass 外调用**

## 15. 调试支持

- `SDL_SetGPUBufferName()` / `SDL_SetGPUTextureName()` — 标记资源
- `SDL_InsertGPUDebugLabel()` — 在命令流中插入标签
- `SDL_PushGPUDebugGroup()` / `SDL_PopGPUDebugGroup()` — 分组调用
- D3D12：需要 WinPixEventRuntime.dll
- Metal：Xcode GPU Frame Capture
- Vulkan：Vulkan SDK 验证层

## 16. 2D Render API over GPU

SDL3 的 2D Render API (`SDL_Render*`) 有 GPU 后端 (`src/render/gpu/SDL_render_gpu.c`)，在 GPU API 之上实现简单的 2D 渲染图元（线条、矩形、纹理四边形），内部处理着色器、管线和 Uniform 管理。推荐用于简单 2D 图形 — 使用 `SDL_CreateRenderer()` 而非直接使用 GPU API。

## 17. 源码位置

| 文件 | 内容 |
|------|------|
| `third_party/SDL/include/SDL3/SDL_gpu.h` | 公共 API 头文件 |
| `third_party/SDL/src/gpu/SDL_sysgpu.h` | 内部驱动头文件 |
| `third_party/SDL/src/gpu/SDL_gpu.c` | API 实现 |
| `third_party/SDL/src/gpu/vulkan/` | Vulkan 后端 |
| `third_party/SDL/src/gpu/d3d12/` | D3D12 后端 |
| `third_party/SDL/src/gpu/metal/` | Metal 后端 |
| `third_party/SDL/src/render/gpu/` | 2D Render over GPU |
