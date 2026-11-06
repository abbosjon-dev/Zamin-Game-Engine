#pragma once

#include "Zamin/Renderer/Camera.h"
#include "Zamin/Renderer/EditorViewSettings.h"
#include "Zamin/Core/Timestep.h"
#include "Zamin/Events/Event.h"

#include <glm/glm.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

namespace Zamin {

    struct AABB;

    // Viewport camera for the editor.
    //  * Alt + LMB: orbit    * Alt + MMB / MMB: pan    * Alt + RMB / scroll: zoom
    //  * RMB + WASD/QE: fly (Shift = faster), mouse looks around
    class EditorCamera : public Camera
    {
    public:
        EditorCamera() { UpdateProjection(); UpdateView(); }
        EditorCamera(float fov, float aspectRatio, float nearClip, float farClip);

        // `active` = viewport is hovered/focused and may consume input
        void OnUpdate(Timestep ts, bool active);
        void OnEvent(Event& e);

        void SetViewportSize(float width, float height);
        void Focus(const glm::vec3& point, float distance = 8.0f);
        void SetOrientation(float pitch, float yaw) { m_Pitch = pitch; m_Yaw = yaw; UpdateView(); }

        // Focal point, distance, orientation and projection (camera bookmarks, framing).
        EditorCameraState GetViewState() const;
        void SetViewState(const EditorCameraState& state);
        // Moves smoothly to `state` over `duration` seconds (<= 0: at once). The projection
        // switches right away; any mouse navigation or zoom stops the transition where it is.
        void TransitionTo(const EditorCameraState& state, float duration = 0.3f);
        bool IsTransitioning() const { return m_Transition.Active; }
        void StopTransition() { m_Transition.Active = false; }
        // The view of `bounds` from the current direction: the focal point at the box center,
        // just far enough that every corner of the box is inside the viewport. `padding` > 1
        // leaves a margin; a box smaller than about a meter is framed as if it were that big.
        EditorCameraState ComputeFrameState(const AABB& bounds, float padding = 1.15f) const;

        float GetDistance() const { return m_Distance; }
        void SetDistance(float distance) { m_Distance = distance; UpdateView(); }

        const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
        glm::mat4 GetViewProjection() const { return m_Projection * m_ViewMatrix; }

        glm::vec3 GetUpDirection() const;
        glm::vec3 GetRightDirection() const;
        glm::vec3 GetForwardDirection() const;
        const glm::vec3& GetPosition() const { return m_Position; }
        const glm::vec3& GetFocalPoint() const { return m_FocalPoint; }
        glm::quat GetOrientation() const;

        // The perspective's vertical field of view (degrees; the 2D view's size follows it too) and
        // clip planes (far at least 10x near).
        void SetProjection(float fovDegrees, float nearClip, float farClip);

        float GetPitch() const { return m_Pitch; }
        float GetYaw() const { return m_Yaw; }
        float GetFOV() const { return m_FOV; }
        float GetNearClip() const { return m_NearClip; }
        float GetFarClip() const { return m_FarClip; }
        float GetAspectRatio() const { return m_AspectRatio; }
        bool IsFlying() const { return m_Flying; }

        // 2D editing: orthographic projection, rotation disabled (pan + zoom only).
        void SetOrthographic(bool ortho) { m_Orthographic = ortho; UpdateProjection(); }
        bool IsOrthographic() const { return m_Orthographic; }

        float FlySpeed = 6.0f;

    private:
        void UpdateProjection();
        void UpdateView();

        void MousePan(const glm::vec2& delta);
        void MouseRotate(const glm::vec2& delta);
        void MouseZoom(float delta);

        glm::vec3 CalculatePosition() const;
        std::pair<float, float> PanSpeed() const;
        float RotationSpeed() const { return 0.8f; }
        float ZoomSpeed() const;

    private:
        float m_FOV = 45.0f, m_AspectRatio = 16.0f / 9.0f, m_NearClip = 0.1f, m_FarClip = 1000.0f;

        glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
        glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
        glm::vec3 m_FocalPoint = { 0.0f, 0.0f, 0.0f };

        glm::vec2 m_InitialMousePosition = { 0.0f, 0.0f };
        bool m_Flying = false;
        bool m_Orthographic = false;
        bool m_HasMouse = false;

        float m_Distance = 12.0f;
        float m_Pitch = 0.35f, m_Yaw = -0.6f;

        float m_ViewportWidth = 1280, m_ViewportHeight = 720;

        struct Transition
        {
            bool Active = false;
            float Time = 0.0f, Duration = 0.0f;
            EditorCameraState From, To;
        } m_Transition;
    };

}
