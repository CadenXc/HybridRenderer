# Chimera 渲染引擎设计图体系 (PlantUML)

本文件存储了 Chimera 渲染引擎在需求分析与概要设计阶段对应的所有用例图、类图及架构图代码。

---
# 第一部分：用例图 (需求分析)

## 1. 场景与资产管理用例图
**建议导出文件名：** `scene_management_usecase.png` (对应 LaTeX Label: `fig:scene_usecase`)

```plantuml
@startuml
left to right direction
skinparam packageStyle rectangle

actor "图形开发者 / 艺术家" as User

rectangle "Chimera 渲染引擎 - 场景与资产管理" {
    usecase "导入 GLTF 模型资产" as UC1
    usecase "浏览场景层级 (Hierarchy)" as UC2
    usecase "选中特定场景实体" as UC3
    usecase "编辑实体变换 (TRS)" as UC4
    usecase "实时调优 PBR 材质参数" as UC5
    usecase "移除选中的场景实体" as UC6
}

User --> UC1
User --> UC2
User --> UC3
UC3 ..> UC4 : <<include>>
UC3 ..> UC5 : <<include>>
UC3 ..> UC6 : <<include>>

@enduml
```

## 2. 渲染控制与参数配置用例图
**建议导出文件名：** `rendering_control_usecase.png` (对应 LaTeX Label: `fig:pipeline_usecase`)

```plantuml
@startuml
left to right direction
skinparam packageStyle rectangle

actor "图形开发者 / 艺术家" as User

rectangle "Chimera 渲染引擎 - 渲染控制与参数配置" {
    usecase "切换全局渲染路径 (Forward/Hybrid/RT)" as UC7
    usecase "开启/关闭光线追踪增强特性" as UC8
    usecase "配置 SVGF 降噪策略" as UC9
    usecase "调节后期显示参数 (ACES/TAA)" as UC10
    
    usecase "控制 RT 阴影/AO" as UC8_1
    usecase "控制 RT 反射/GI" as UC8_2
}

User --> UC7
User --> UC8
User --> UC9
User --> UC10

UC8 <|-- UC8_1
UC8 <|-- UC8_2

@enduml
```

## 3. 调试与可视化监控用例图
**建议导出文件名：** `visualization_debug_usecase.png` (对应 LaTeX Label: `fig:debug_usecase`)

```plantuml
@startuml
left to right direction
skinparam packageStyle rectangle

actor "图形开发者 / 艺术家" as User

rectangle "Chimera 渲染引擎 - 调试与可视化监控" {
    usecase "切换 G-Buffer 分量预览 (Display Mode)" as UC11
    usecase "预览 RT 原始信号与降噪方差图" as UC12
    usecase "实时监控性能曲线 (FPS/FrameTime)" as UC13
    usecase "分析 GPU 各 Pass 耗时明细" as UC14
}

User --> UC11
User --> UC12
User --> UC13
User --> UC14

@enduml
```

---
# 第二部分：类图 (概要设计)

## 4. 全局管理与分层框架类图
**建议导出文件名：** `framework_class_diagram.png` (对应 LaTeX Label: `fig:framework_class_diagram`)

```plantuml
@startuml
skinparam classAttributeIconSize 0
skinparam linetype ortho

class Application {
    - m_LayerStack: LayerStack
    - m_Context: VulkanContext
    - m_RenderPath: RenderPath
    - m_RenderState: RenderState
    + {static} Get(): Application&
    + Run(): void
    + OnEvent(e: Event&): void
    + PushLayer(layer: Layer*): void
    + SwitchRenderPath(path: unique_ptr<RenderPath>): void
    + GetActiveScene(): Scene*
    + GetFrameContext(): AppFrameContext&
}

class LayerStack {
    - m_Layers: std::vector<Layer*>
    + PushLayer(layer: Layer*): void
    + PopLayer(layer: Layer*): void
    + begin(): iterator
    + end(): iterator
}

abstract class Layer {
    # m_DebugName: string
    + {abstract} OnAttach(): void
    + {abstract} OnDetach(): void
    + {abstract} OnUpdate(ts: Timestep): void
    + {abstract} OnImGuiRender(): void
    + {abstract} OnEvent(e: Event&): void
}

class EditorLayer {
    - m_EditorCamera: EditorCamera
    - m_SelectedInstance: int
    + OnUpdate(ts: Timestep): void
    + OnImGuiRender(): void
}

class VulkanContext {
    - m_Instance: VkInstance
    - m_Device: VkDevice
    - m_PhysicalDevice: VkPhysicalDevice
    + Init(): void
    + GetDevice(): VkDevice
    + IsRayTracingSupported(): bool
}

' Relationships
Application *-- LayerStack
Application *-- VulkanContext
Application *-- RenderState
Application o-- RenderPath
LayerStack o-- Layer
Layer <|-- EditorLayer
@enduml
```

## 5. 资源管理与场景组织类图
**建议导出文件名：** `resource_scene_class_diagram.png` (对应 LaTeX Label: `fig:resource_scene_class_diagram`)

```plantuml
@startuml
skinparam classAttributeIconSize 0
skinparam linetype ortho

' --- 资源层 ---
class ResourceManager {
    - m_Textures: vector<unique_ptr<Image>>
    - m_Materials: vector<unique_ptr<Material>>
    - m_ResourceFreeQueue: vector<vector<function>>
    + {static} Get(): ResourceManager&
    + LoadModelAsync(path: string): void
    + SubmitResourceFree(func: function): void
    + SyncMaterialsToGPU(): void
}

class Buffer {
    - m_Buffer: VkBuffer
    - m_Allocation: VmaAllocation
    + UploadData(data: void*, size: size_t): void
}

class Image {
    - m_Image: VkImage
    - m_View: VkImageView
    + TransitionLayout(): void
}

' --- 场景层 ---
class Scene {
    - m_Entities: vector<Entity>
    - m_TopLevelAS: VkAccelerationStructureKHR
    + AddEntity(e: Entity): void
    + RemoveEntity(index: uint32): void
    + UpdateTLAS(): void
}

struct Entity {
    + name: string
    + transform: Transform
    + model: shared_ptr<Model>
    + primitiveOffset: uint32
}

class Model {
    - m_Meshes: vector<Mesh>
    - m_VertexBuffer: unique_ptr<Buffer>
    - m_IndexBuffer: unique_ptr<Buffer>
    - m_BLAS: vector<VkAccelerationStructureKHR>
    + GetMeshes(): vector<Mesh>&
}

struct Mesh {
    + indexCount: uint32
    + materialIndex: uint32
    + localBounds: AABB
}

' --- 关系 ---
ResourceManager ..> Buffer : "creates"
ResourceManager ..> Image : "manages"

Scene *-- Entity : "contains"
Entity o-- Model : "references (shared_ptr)"
Model *-- Mesh : "contains"
Model *-- Buffer : "owns geometry data"

@enduml
```

## 6. RenderGraph 调度引擎类图
**建议导出文件名：** `rendergraph_engine_class_diagram.png` (对应 LaTeX Label: `fig:rendergraph_engine_class_diagram`)

```plantuml
@startuml
skinparam classAttributeIconSize 0
skinparam linetype ortho

' --- 1. Orchestration ---
abstract class RenderPath {
    # m_Graph: RenderGraph
    + {abstract} BuildGraph(graph: RenderGraph&, scene: shared_ptr<Scene>): void
    + Render(frameInfo: RenderFrameInfo): void
}

class HybridRenderPath {
    + BuildGraph(): void
}

class RenderGraph {
    - m_Nodes: vector<PassNode>
    - m_Registry: RenderGraphRegistry
    + AddPass<T>(args...): void
    + Compile(): void
    + Execute(cmd: VkCommandBuffer): void
}

' --- 2. Pass Hierarchy ---
interface IRenderGraphPass {
    + {abstract} Setup(builder: PassBuilder&): void
    + {abstract} Execute(reg: Registry&, cmd: VkCommandBuffer): void
}

abstract class "RenderPass<T>" as RenderPass_T {
    # m_Data: T
}

abstract class GraphicsPass {
    + Execute(reg: Registry&, ctx: GraphicsExecutionContext&): void
}

abstract class ComputePass {
    + Execute(reg: Registry&, ctx: ComputeExecutionContext&): void
}

abstract class RaytracingPass {
    + Execute(reg: Registry&, ctx: RaytracingExecutionContext&): void
}

' --- 3. Concrete Implementations ---
class GBufferPass {
    + Setup(): void
    + Execute(): void
}

class RTShadowPass {
    + Setup(): void
    + Execute(): void
}

class SVGFPass {
    + Setup(): void
    + Execute(): void
}

' --- 4. Contexts & Builder ---
class PassBuilder {
    + Read(resource): RGResourceHandle
    + Write(resource): RGResourceHandle
    + WriteStorage(resource): RGResourceHandle
}

class GraphicsExecutionContext {
    - m_Cmd: VkCommandBuffer
    + BindPipeline(): void
    + DrawMeshes(): void
}

class RaytracingExecutionContext {
    + BindPipeline(): void
    + TraceRays(): void
}

' --- Relationships ---
RenderPath <|-- HybridRenderPath
RenderPath *-- RenderGraph

IRenderGraphPass <|.. RenderPass_T
RenderPass_T <|-- GraphicsPass
RenderPass_T <|-- ComputePass
RenderPass_T <|-- RaytracingPass

GraphicsPass <|-- GBufferPass
RaytracingPass <|-- RTShadowPass
ComputePass <|-- SVGFPass

RenderGraph *-- IRenderGraphPass
IRenderGraphPass ..> PassBuilder : "Setup"
GraphicsPass ..> GraphicsExecutionContext : "Execute"
RaytracingPass ..> RaytracingExecutionContext : "Execute"

@enduml
```

---
# 第三部分：系统架构图

## 7. Chimera 引擎五层分层拓扑架构图
**建议导出文件名：** `renderer_architecture.png` (对应 LaTeX Label: `fig:renderer_architecture`)

```plantuml
@startuml
skinparam packageStyle rectangle
skinparam linetype ortho
skinparam nodesep 10
skinparam ranksep 20

package "应用层 (Sandbox Layer)" #E3F2FD {
    [SandboxApp]
    [EditorLayer]
}

package "核心框架层 (Core Layer)" #FFF3E0 {
    [Application]
    [LayerStack]
    [TaskSystem (Worker Threads)]
}

package "场景与资源层 (Scene & Assets Layer)" #E8F5E9 {
    [Scene Graph]
    [ResourceManager (VMA)]
    [Acceleration Structures (AS)]
}

package "渲染调度层 (Renderer Layer)" #F3E5F5 {
    [RenderPath (Hybrid/RT/Forward)]
    [RenderGraph (DAG / Aliasing)]
    [Pass Execution Contexts]
}

package "平台硬件层 (Platform Layer)" #FAFAFA {
    [Vulkan Context / Device]
    [Window (GLFW)]
    [volk (API Loader)]
}

' Control & Data Flow
[SandboxApp] --> [Application] : "Constructs"
[Application] --> [LayerStack] : "Ticks / Events"
[EditorLayer] --> [Scene Graph] : "Edits / Updates"
[EditorLayer] --> [RenderPath (Hybrid/RT/Forward)] : "Configures"
[RenderPath (Hybrid/RT/Forward)] --> [RenderGraph (DAG / Aliasing)] : "Builds"
[RenderGraph (DAG / Aliasing)] --> [ResourceManager (VMA)] : "Resource Lifetimes"
[ResourceManager (VMA)] --> [Vulkan Context / Device] : "VMA Allocations"

@enduml
```

---
## 8. 系统渲染数据流图 (DFD)
**建议导出文件名：** `render_dfd.png` (对应 LaTeX Label: `fig:render_dfd`)

```plantuml
@startuml
skinparam backgroundColor white
skinparam shadowing false

start

partition "资产预处理阶段 (CPU)" {
    :GLTF 模型与纹理资产;
    :TaskSystem 异步解析;
    :构建 BLAS 与 GPU 缓冲区上传;
}

partition "逻辑调度阶段 (Renderer)" {
    :RenderGraph 节点注册;
    :DAG 拓扑依赖分析;
    :显存别名 (Aliasing) 分配;
    :自动插入同步屏障 (Barriers);
}

partition "混合计算阶段 (GPU)" {
    :G-Buffer 几何采集 (Rasterization);
    :Ray Query 硬件光追探测;
    :PBR 物理光影合成;
}

partition "画质重建阶段 (GPU Post-process)" {
    :SVGF 时空方差导向滤波;
    :TAA 时域抗锯齿;
    :ACES 色调映射与 Gamma 校正;
}

:交换链呈现 (Swapchain);

stop
@enduml
```

---
## 11. SVGF 降噪管线数据流转与逻辑拓扑图
**建议导出文件名：** `svgf_pipeline_data_flow.png` (对应 LaTeX Label: `fig:svgf_pipeline`)

```plantuml
@startuml
skinparam backgroundColor white
skinparam shadowing false

start

partition "输入阶段 (G-Buffer & RT)" {
    fork
        :读取当前帧光追信号 (1 SPP);
        :反照率解耦 (Albedo Demodulation);
    fork again
        :获取几何辅助数据 (Normal, Depth, Motion);
    end fork
}

partition "阶段一：时域重投影与累积" {
    :利用 Motion Vector 回溯历史坐标;
    :进行双线性插值采样历史缓冲区;
    :执行指数移动平均 (EMA) 累积亮度与矩;
    :更新有效样本计数 (Sample Depth);
}

partition "阶段二：方差估算" {
    :基于累积的一阶矩与二阶矩计算亮度方差;
    :进行初步空间域 3x3 滤波平滑方差图;
}

partition "阶段三：À-Trous 分级空间滤波" {
    while (执行 5 轮迭代 (Filter Level 1 to 5)?) is (下一级)
        :计算联合双边权重 (Edge-Stopping Weights);
        note right
            权重由位置、法线、深度
            及亮度方差共同决定
        end note
        :执行 5x5 跨步卷积内核采样;
        :输出至 Ping-Pong 临时缓冲区;
    endwhile (结束)
}

partition "输出阶段" {
    :反照率重耦合 (Albedo Modulation);
    :写入最终降噪信号 (Filtered_Final);
}

stop
@enduml
```


---
## 10. 信号重建时域历史持久化机制图
**建议导出文件名：** `history_persistent_mechanism.png` (对应 LaTeX Label: `fig:history_mechanism`)

```plantuml
@startuml
skinparam backgroundColor white
skinparam shadowing false
skinparam linetype polyline

package "第 N 帧执行环境" {
    node "当前帧输入" as CurrentFrame {
        [Raw RT Signal (1 SPP)]
        [Motion Vector]
        [Depth / Normal]
    }

    node "重建算法节点 (SVGF / TAA)" as Pass {
        component "时域重投影逻辑" as Reprojection
        component "指数移动平均 (EMA)" as Accumulation
    }

    database "历史缓冲区 (History Pool)" {
        [上一帧持久化数据] as History_Read #E1F5FE
        [新生成的累积数据] as History_Write #FFF9C4
    }
}

package "帧间状态转移 (RenderGraph Persistent Logic)" {
    [句柄交换 / 指针重定向] as Swap #FFCCBC
}

' Data Flow
CurrentFrame --> Reprojection : "提供采样输入"
History_Read --> Reprojection : "提供历史颜色/矩"
Reprojection --> Accumulation : "计算时域权重"
Accumulation --> History_Write : "存储更新状态"

' Lifecycle
History_Write ..> Swap : "帧执行结束"
Swap ..> History_Read : "下一帧重用"

@enduml
```


---
## 9. RenderGraph 运行生命周期流程图
**建议导出文件名：** `render_graph_lifecycle_flow.png` (对应 LaTeX Label: `fig:render_graph_lifecycle`)

```plantuml
@startuml
skinparam backgroundColor white
skinparam shadowing false

start

partition "Setup 阶段 (每帧声明)" {
    :调用 AddPass<T>() / AddPassRaw();
    :执行具体的 Pass::Setup();
    :PassBuilder 记录 Read/Write 依赖;
    :构建虚拟 Pass 栈与 Handle 映射;
}

partition "Compile 阶段 (依赖分析)" {
    :Reset 物理资源状态追踪器;
    :BuildDependencyGraph (拓扑排序);
    :Parallel Leveling (压缩 Pass 为并行层级);
    :计算资源生存区间 (firstPass, lastPass);
    :物理资源实例化 (池化创建或复用);
}

partition "Execute 阶段 (指令录制)" {
    while (遍历 Parallel Layers?) is (下一个 Layer)
        :BuildBarriers (自动推导并插入同步屏障);
        fork
            :图形任务 (Graphics Pass);
            :BeginDynamicRendering;
            :设置 Viewport 与 Scissor;
        fork again
            :非图形任务 (Compute / RT Pass);
        end fork
        :回调执行 Pass::Execute();
        if (是图形任务?) then (yes)
            :EndDynamicRendering;
        endif
    endwhile (结束)
    :UpdatePersistentResources (更新历史帧持久化数据);
}

stop
@enduml
```

---
# 第四部分：软工详细设计动态图

## 12. 应用单帧生命周期活动图
**建议导出文件名：** `app_frame_activity.png` (对应 LaTeX Label: `fig:app_activity`)

```plantuml
@startuml
skinparam backgroundColor white
skinparam shadowing false

start
:计算当前帧 Timestep;
:Input Polling (查询键盘鼠标状态);
partition "LayerStack 遍历" {
    :从底向上调用 Layer::OnUpdate();
    note right: 处理物理逻辑与渲染指令
    :从底向上调用 Layer::OnImGuiRender();
    note right: 提交 UI 绘图指令
}
:Vulkan 交换链获取可用图像;
:RenderGraph 执行录制好的指令流;
:提交 Queue 至 GPU 并呈现 (Present);
stop
@enduml
```

## 13. 异步资源加载处理顺序图
**建议导出文件名：** `async_loading_sequence.png` (对应 LaTeX Label: `fig:loading_sequence`)

```plantuml
@startuml
skinparam backgroundColor white
participant "EditorLayer" as UI
participant "ResourceManager" as RM
participant "TaskSystem" as TS
participant "VulkanDevice" as GPU

UI -> RM : LoadModelAsync(path)
activate RM
RM -> TS : Submit(IO & Parsing Task)
activate TS
TS -> TS : cgltf 解析模型
TS --> RM : 解析完成 (Geometry Data)
deactivate TS

RM -> TS : Submit(Texture Decoding Task)
activate TS
TS -> TS : STB 并行解码纹理
TS --> RM : 解码完成 (Bitmaps)
deactivate TS

RM -> GPU : 录制 Copy 指令并提交 Transfer 队列
activate GPU
GPU -> GPU : 物理显存分配与数据上传
GPU --> RM : 上传完成 (Fence Signal)
deactivate GPU

RM -> UI : 通知加载就绪 (OnSceneUpdated)
UI -> RM : 获取 Ready 模型句柄
deactivate RM
@enduml
```

## 14. 渲染图自动同步执行顺序图
**建议导出文件名：** `rg_sync_sequence.png` (对应 LaTeX Label: `fig:sync_sequence`)

```plantuml
@startuml
skinparam backgroundColor white
participant "RenderGraph" as RG
participant "ResourceTracker" as TR
participant "PassNode" as PN
participant "CommandBuffer" as CMD

RG -> RG : 拓扑排序完成
loop 遍历所有 Pass 节点
    RG -> TR : 查询资源当前 Layout (e.g. GENERAL)
    RG -> PN : 获取 Pass 需求 Layout (e.g. SHADER_READ)
    
    alt 状态不匹配?
        RG -> CMD : 插入 vkCmdPipelineBarrier2
        note right: 自动转换布局并处理 RAW 冒险
        TR -> TR : 更新资源全局状态
    end
    
    RG -> PN : 调用 Pass::Execute()
    PN -> CMD : 录制具体的绘制/计算指令
end
@enduml
```

## 15. 渲染参数动态调节顺序图
**建议导出文件名：** `param_adjustment_sequence.png` (对应 LaTeX Label: `fig:param_sequence`)

```plantuml
@startuml
skinparam backgroundColor white
actor "用户" as User
participant "ImGui 控制面板" as UI
participant "AppFrameContext" as Context
participant "GPU 常量缓冲区" as UBO
participant "Shader" as Shader

User -> UI : 拖动 SVGF 时间步长滑块
UI -> Context : 更新 DenoiseParam 字段
UI -> Context : 设置 RenderDirty = true

loop 每一帧渲染
    Context -> UBO : 数据映射 (Map/Unmap)
    UBO -> Shader : 绑定全局 Descriptor Set
    Shader -> Shader : 执行新的降噪逻辑
end
@enduml
```


