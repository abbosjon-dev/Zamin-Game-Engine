#include "Zamin/Renderer/EditorCamera.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Core/Input.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

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

    EditorCameraState EditorCamera::GetViewState() const
    {
        EditorCameraState state;
        state.FocalPoint = m_FocalPoint;
        state.Distance = m_Distance;
        state.Pitch = m_Pitch;
        state.Yaw = m_Yaw;
        state.Orthographic = m_Orthographic;
        return state;
    }

    void EditorCamera::SetViewState(const EditorCameraState& state)
    {
        m_Transition.Active = false;
        m_FocalPoint = state.FocalPoint;
        m_Distance = std::max(state.Distance, 0.01f);
        m_Pitch = state.Pitch;
        m_Yaw = state.Yaw;
        m_Orthographic = state.Orthographic;
        UpdateProjection();
        UpdateView();
    }

    // The same angle as close to `from` as possible (shortest way around).
    static float NearestAngle(float from, float to)
    {
        float diff = std::remainder(to - from, glm::two_pi<float>());
        return from + diff;
    }

    void EditorCamera::TransitionTo(const EditorCameraState& state, float duration)
    {
        if (duration <= 0.0f)
        {
            SetViewState(state);
            return;
        }
        if (m_Orthographic != state.Orthographic)
        {
            m_Orthographic = state.Orthographic;
            UpdateProjection();
        }
        m_Transition.Active = true;
        m_Transition.Time = 0.0f;
        m_Transition.Duration = duration;
        m_Transition.From = GetViewState();
        m_Transition.To = state;
        m_Transition.To.Distance = std::max(state.Distance, 0.01f);
    }

    EditorCameraState EditorCamera::ComputeFrameState(const AABB& bounds, float padding) const
    {
        constexpr float MinFrameRadius = 1.0f; // a point (empty entity, light) is not framed from 0 m
        EditorCameraState state = GetViewState();
        glm::vec3 center = (bounds.Min + bounds.Max) * 0.5f;
        glm::vec3 half = glm::max((bounds.Max - bounds.Min) * 0.5f, glm::vec3(0.0f));
        state.FocalPoint = center;

        float tanHalfY = std::tan(glm::radians(m_FOV) * 0.5f);
        float aspect = m_AspectRatio > 0.0f ? m_AspectRatio : 1.0f;
        padding = std::max(padding, 1.0f);
        if (m_Orthographic)
        {
            // The box's half size across the view: its corners projected on the right / up axes.
            glm::vec3 right = glm::abs(GetRightDirection()), up = glm::abs(GetUpDirection());
            float halfWidth = glm::dot(half, right), halfHeight = glm::dot(half, up);
            float needed = std::max({ halfHeight, halfWidth / aspect, MinFrameRadius }) * padding;
            state.Distance = needed / tanHalfY; // UpdateProjection: half height = distance * tan(fov / 2)
        }
        else if (glm::length(half) < MinFrameRadius)
        {
            // Small things: a sphere of MinFrameRadius inside the narrower field-of-view angle.
            float halfFov = std::atan(std::min(tanHalfY, tanHalfY * aspect));
            state.Distance = MinFrameRadius * padding / std::sin(halfFov);
        }
        else
        {
            // Every corner of the box inside the view (shrunk by `padding`): a corner at
            // (x, y) across the view and z along the view direction from the center needs
            // distance + z >= |x| / tan(fovX / 2) and >= |y| / tan(fovY / 2).
            glm::vec3 right = GetRightDirection(), up = GetUpDirection(), forward = GetForwardDirection();
            float tanY = tanHalfY / padding, tanX = tanHalfY * aspect / padding;
            float distance = 0.0f;
            for (int i = 0; i < 8; i++)
            {
                glm::vec3 o((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
                float x = glm::dot(o, right), y = glm::dot(o, up), z = glm::dot(o, forward);
                distance = std::max({ distance, std::abs(x) / tanX - z, std::abs(y) / tanY - z, m_NearClip * 2.0f - z });
            }
            state.Distance = distance;
        }
        state.Distance = std::clamp(state.Distance, 0.5f, m_FarClip * 0.5f);
        return state;
    }

    void EditorCamera::SetProjection(float fovDegrees, float nearClip, float farClip)
    {
        m_FOV = std::clamp(std::isfinite(fovDegrees) ? fovDegrees : 45.0f, 1.0f, 179.0f);
        m_NearClip = std::max(std::isfinite(nearClip) ? nearClip : 0.1f, 1e-4f);
        m_FarClip = std::max(std::isfinite(farClip) ? farClip : 1000.0f, m_NearClip * 10.0f);
        UpdateProjection();
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

        if (m_Transition.Active)
        {
            m_Transition.Time += (float)ts;
            float t = std::clamp(m_Transition.Time / m_Transition.Duration, 0.0f, 1.0f);
            const EditorCameraState& a = m_Transition.From;
            const EditorCameraState& b = m_Transition.To;
            if (t >= 1.0f)
            {
                m_FocalPoint = b.FocalPoint;
                m_Distance = b.Distance;
                m_Pitch = b.Pitch;
                m_Yaw = b.Yaw;
                m_Transition.Active = false;
            }
            else
            {
                float k = t * t * (3.0f - 2.0f * t); // smoothstep
                m_FocalPoint = glm::mix(a.FocalPoint, b.FocalPoint, k);
                // zoom evenly (in log space): far-away targets do not rush the first frames
                m_Distance = a.Distance * std::pow(b.Distance / std::max(a.Distance, 0.01f), k);
                m_Pitch = glm::mix(a.Pitch, NearestAngle(a.Pitch, b.Pitch), k);
                m_Yaw = glm::mix(a.Yaw, NearestAngle(a.Yaw, b.Yaw), k);
            }
            UpdateView();
        }

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
            m_Transition.Active = false; // the user takes over
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
            m_Transition.Active = false;
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
