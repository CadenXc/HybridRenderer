#include "pch.h"
#include "core/EntryPoint.h"

// ==============================================================================
// 自定义层：RayTracingLayer
// 所有的游戏逻辑、输入控制、UI代码以后都写在这里
// ==============================================================================
class RayTracingLayer : public Chimera::Layer
{
public:
    virtual void OnAttach() override
    {
        CH_INFO("RayTracingLayer Attached!");
        // 这里可以做：场景加载、资源初始化
    }

    virtual void OnDetach() override
    {
        CH_INFO("RayTracingLayer Detached!");
    }

    virtual void OnUpdate(float ts) override
    {
        // ts (TimeStep) 是上一帧到这一帧的时间（秒）
        // 例如：打印帧率 (每秒打印一次)
        static float timeAccumulator = 0.0f;
        timeAccumulator += ts;
        if (timeAccumulator > 1.0f)
        {
            CH_INFO("FPS: {}", 1.0f / ts);
            timeAccumulator = 0.0f;
        }

        // 未来在这里写：
        // m_Camera.OnUpdate(ts);  // 处理 WASD 移动
        // m_Scene.OnUpdate(ts);   // 处理物体动画
    }

    virtual void OnUIRender() override
    {
        // 未来在这里写 ImGui 代码：
        // ImGui::Begin("Settings");
        // ImGui::SliderFloat("Light Pos X", &x, -10.0f, 10.0f);
        // ImGui::End();
    }
};

// ==============================================================================
// 客户端应用
// ==============================================================================
class ChimeraApp : public Chimera::Application
{
public:
    ChimeraApp()
    {
        // 将我们的光追层推入引擎
        PushLayer(std::make_shared<RayTracingLayer>());
        
        CH_INFO("---------------------------------------------");
        CH_INFO("Welcome to Chimera Hybrid Renderer!");
        CH_INFO("App constructed successfully.");
        CH_INFO("---------------------------------------------");
    }

    ~ChimeraApp()
    {
        CH_INFO("Chimera App shutting down...");
    }
};

Chimera::Application* Chimera::CreateApplication(int argc, char** argv)
{
    return new ChimeraApp();
}