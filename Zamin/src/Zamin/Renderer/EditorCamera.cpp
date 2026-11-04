#include "Zamin/Renderer/EditorCamera.h"
#include "Zamin/Core/Input.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Zamin {

    EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
        : Camera(glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip)),
          m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip)
    {
        UpdateView();
    }

    void EditorCamera::SetViewportSize(float width, float height)
    {
        if (width <= 0 || height <= 0) return;
        m_ViewportWidth = width;
        m_ViewportHeight = height;
        UpdateProjection();
    }

    void EditorCamera::Focus(const glm::vec3& point, float distance)
    {
        m_FocalPoint = point;
        m_Distance = distance;
        UpdateView();
    }

    void EditorCamera::UpdateProjection()
    {
        m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
        if (m_Orthographic)
        {
            float h = m_Distance * std::tan(glm::radians(m_FOV) * 0.5f);
            float w = h * m_AspectRatio;
            m_Projection = glm::ortho(-w, w, -h, h, -m_FarClip, m_FarClip);
        }
        else
            m_Projection = glm::perspective(glm::radians(m_FOV), m_AspectRatio, m_NearClip, m_FarClip);
    }

    void EditorCamera::UpdateView()
    {
        if (m_Orthographic)
            UpdateProjection(); // zoom changes the ortho size
        m_Position = CalculatePosition();
        glm::quat orientation = GetOrientation();
        m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
        m_ViewMatrix = glm::inverse(m_ViewMatrix);
    }

    std::pair<float, float> EditorCamera::PanSpeed() const
    {
        float x = std::min(m_ViewportWidth / 1000.0f, 2.4f);
        float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;
        float y = std::min(m_ViewportHeight / 1000.0f, 2.4f);
        float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;
        return { xFactor, yFactor };
    }

    float EditorCamera::ZoomSpeed() const
    {
        float distance = std::max(m_Distance * 0.2f, 0.0f);
        float speed = distance * distance;
        return std::min(speed, 100.0f);
    }

    void EditorCamera::OnUpdate(Timestep ts, bool active)
    {
        const glm::vec2 mouse = Input::GetMousePosition();
        glm::vec2 delta = (mouse - m_InitialMousePosition) * 0.003f;
        m_InitialMousePosition = mouse;

        bool rmb = Input::IsMouseButtonPressed(MouseButton::Right);
        bool alt = Input::IsKeyPressed(Key::LeftAlt) || Input::IsKeyPressed(Key::RightAlt);

        // Interaction only starts while the viewport is active, but continues
        // while the button is held even if the cursor leaves the viewport.
        if (!m_HasMouse)
        {
            bool anyButton = rmb || Input::IsMouseButtonPressed(MouseButton::Middle) ||
                             (alt && Input::IsMouseButtonPressed(MouseButton::Left));
            if (!active || !anyButton)
            {
                m_Flying = false;
                return;
            }
            m_HasMouse = true;
            return; // skip the first delta to avoid jumps
        }

        bool anyButton = rmb || Input::IsMouseButtonPressed(MouseButton::Middle) ||
                         (alt && Input::IsMouseButtonPressed(MouseButton::Left));
        if (!anyButton)
        {
            m_HasMouse = false;
            m_Flying = false;
            return;
        }

        if (m_Orthographic)
        {
            // 2D: any drag pans, Alt+RMB zooms
            if (alt && rmb) MouseZoom(delta.y);
            else MousePan(delta);
            m_Flying = false;
        }
        else if (alt)
        {
            if (Input::IsMouseButtonPressed(MouseButton::Left))        MouseRotate(delta);
            else if (Input::IsMouseButtonPressed(MouseButton::Middle)) MousePan(delta);
            else if (rmb)                                              MouseZoom(delta.y);
            m_Flying = false;
        }
        else if (Input::IsMouseButtonPressed(MouseButton::Middle))
        {
            MousePan(delta);
        }
        else if (rmb)
        {
            // Fly mode: rotate around the camera position, move with WASD/QE.
            m_Flying = true;
            glm::vec3 position = m_Position;
            float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;
            m_Yaw += yawSign * delta.x * RotationSpeed();
            m_Pitch += delta.y * RotationSpeed();
            m_Pitch = std::clamp(m_Pitch, -1.55f, 1.55f);

            float speed = FlySpeed * (Input::IsKeyPressed(Key::LeftShift) ? 3.0f : 1.0f) * ts;
            glm::vec3 move(0.0f);
            if (Input::IsKeyPressed(Key::W)) move += GetForwardDirection();
            if (Input::IsKeyPressed(Key::S)) move -= GetForwardDirection();
            if (Input::IsKeyPressed(Key::D)) move += GetRightDirection();
            if (Input::IsKeyPressed(Key::A)) move -= GetRightDirection();
            if (Input::IsKeyPressed(Key::E)) move += glm::vec3(0, 1, 0);
            if (Input::IsKeyPressed(Key::Q)) move -= glm::vec3(0, 1, 0);
            position += move * speed;

            m_FocalPoint = position + GetForwardDirection() * m_Distance;
        }
        UpdateView();
    }

    void EditorCamera::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& ev) {
            if (m_Flying && !m_Orthographic)
            {
                FlySpeed = std::clamp(FlySpeed * (ev.GetYOffset() > 0 ? 1.2f : 1.0f / 1.2f), 0.5f, 200.0f);
            }
            else
            {
                MouseZoom(ev.GetYOffset() * 0.1f);
                UpdateView();
            }
            return false;
        });
    }

    void EditorCamera::MousePan(const glm::vec2& delta)
    {
        auto [xSpeed, ySpeed] = PanSpeed();
        m_FocalPoint += -GetRightDirection() * delta.x * xSpeed * m_Distance;
        m_FocalPoint += GetUpDirection() * delta.y * ySpeed * m_Distance;
    }

    void EditorCamera::MouseRotate(const glm::vec2& delta)
    {
        float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;
        m_Yaw += yawSign * delta.x * RotationSpeed();
        m_Pitch += delta.y * RotationSpeed();
    }

    void EditorCamera::MouseZoom(float delta)
    {
        m_Distance -= delta * ZoomSpeed();
        if (m_Distance < 0.5f)
        {
            m_FocalPoint += GetForwardDirection() * (0.5f - m_Distance);
            m_Distance = 0.5f;
        }
    }

    glm::vec3 EditorCamera::GetUpDirection() const { return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f)); }
    glm::vec3 EditorCamera::GetRightDirection() const { return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f)); }
    glm::vec3 EditorCamera::GetForwardDirection() const { return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f)); }
    glm::vec3 EditorCamera::CalculatePosition() const { return m_FocalPoint - GetForwardDirection() * m_Distance; }
    glm::quat EditorCamera::GetOrientation() const { return glm::quat(glm::vec3(-m_Pitch, -m_Yaw, 0.0f)); }

}
