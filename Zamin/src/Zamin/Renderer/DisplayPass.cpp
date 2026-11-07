#include "Zamin/Renderer/DisplayPass.h"

#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Shader.h"

#include <algorithm>

namespace Zamin {

    namespace {

        const char* s_VertexShader = R"(#version 410 core
out vec2 v_UV;
void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_UV = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

        // DisplayAdjustment::Apply, then the dimming.
        const char* s_FragmentShader = R"(#version 410 core
in vec2 v_UV;
out vec4 o_Color;
uniform sampler2D u_Source;
uniform float u_Gamma;
uniform float u_Brightness;
uniform float u_Dim;
void main()
{
    vec3 color = texture(u_Source, v_UV).rgb;
    color = pow(clamp(color * u_Brightness, 0.0, 1.0), vec3(1.0 / u_Gamma));
    o_Color = vec4(color * (1.0 - u_Dim), 1.0);
}
)";

        Ref<Shader>& PassShader()
        {
            static Ref<Shader> s_Shader;
            return s_Shader;
        }

    }

    void DisplayPass::Present(uint32_t texture, const DisplayAdjustment& adjustment, float dim)
    {
        Ref<Shader>& shader = PassShader();
        if (!shader)
            shader = Shader::Create("DisplayPass", s_VertexShader, s_FragmentShader);
        DisplayAdjustment a = adjustment;
        a.Sanitize();
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetBlendMode(BlendMode::None);
        shader->Bind();
        shader->SetInt("u_Source", 0);
        shader->SetFloat("u_Gamma", a.Gamma);
        shader->SetFloat("u_Brightness", a.Brightness);
        shader->SetFloat("u_Dim", std::clamp(dim, 0.0f, 1.0f));
        RenderCommand::BindTextureUnit(0, texture);
        RenderCommand::DrawArrays(3);
        // Back to the renderers' default state (the next frame's 3D pass relies on the depth test).
        RenderCommand::SetBlendMode(BlendMode::Alpha);
        RenderCommand::SetDepthTest(true);
    }

    void DisplayPass::Shutdown() { PassShader().reset(); }

}
