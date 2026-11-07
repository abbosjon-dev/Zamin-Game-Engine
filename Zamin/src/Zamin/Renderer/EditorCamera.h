#pragma once

#include "Zamin/Renderer/Camera.h"
#include "Zamin/Renderer/EditorViewSettings.h"
#include "Zamin/Editor/SceneViews.h"
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
    //  * RMB + WASD/QE: fly (Shift = faster), mouse looks around; the wheel changes the fly speed
    // Orthographic views: RMB / MMB pan; in an axis view (Top, Front ...) orbiting turns back to
    // perspective. The 2D mode locks the rotation (pan + zoom only).
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
        // Moves smoothly to `state` over `duration` seconds (<= 0: at once). Perspective to
        // orthographic switches at the end (the turn is seen in perspective), the other way at
        // once; any mouse navigation or zoom stops the transition where it is.
        void TransitionTo(const EditorCameraState& state, float duration = 0.3f);
        bool IsTransitioning() const { return m_Transition.Active; }
        // The user takes over: a transition into an orthographic view arrives at once (the projection
        // and a 2D view's rotation lock are the ones it was going to); any other stops where it is.
        void StopTransition();
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
        void SetRotationLocked(bool locked) { m_RotationLocked = locked; }
        bool IsRotationLocked() const { return m_RotationLocked; }

        // ---- axis views (Top, Front, Right ... orthographic; Free: perspective)
        // The axis view shown now: orthographic, not the 2D mode, looking along an axis.
        ViewAxis GetViewAxis() const;
        // Where the camera goes for `axis`: the same focal point and distance, the axis' direction,
        // orthographic (Free: perspective, the direction kept).
        EditorCameraState GetAxisViewState(ViewAxis axis) const;
        void SetViewAxis(ViewAxis axis, float duration = 0.3f) { TransitionTo(GetAxisViewState(axis), duration); }
        // Perspective <-> orthographic, the direction kept (not in the 2D mode).
        void ToggleOrthographic();

        float FlySpeed = 6.0f;
        float FlyAcceleration = 0.0f;  // the fly speed grows while moving (GetFlyBoost)
        float LookSensitivity = 1.0f;  // x the mouse's turning speed
        bool InvertY = false;          // mouse up looks down (fly look, orbit)
        // The fly speed's boost now (1 when not accelerating).
        float GetFlyBoostNow() const { return m_Flying ? GetFlyBoost(m_FlyTime, FlyAcceleration) : 1.0f; }

    private:
        void UpdateProjection();
        void UpdateView();

        void MousePan(const glm::vec2& delta);
        void MouseRotate(const glm::vec2& delta);
        void MouseZoom(float delta);

        glm::vec3 CalculatePosition() const;
        std::pair<float, float> PanSpeed() const;
        float RotationSpeed() const { return 0.8f * LookSensitivity; }
        float ZoomSpeed() const;

    private:
        float m_FOV = 45.0f, m_AspectRatio = 16.0f / 9.0f, m_NearClip = 0.1f, m_FarClip = 1000.0f;

        glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
        glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
        glm::vec3 m_FocalPoint = { 0.0f, 0.0f, 0.0f };

        glm::vec2 m_InitialMousePosition = { 0.0f, 0.0f };
        bool m_Flying = false;
        bool m_Orthographic = false;
        bool m_RotationLocked = false;
        bool m_HasMouse = false;
        float m_FlyTime = 0.0f;        // seconds the fly keys have been held (acceleration)

        float m_Distance = 12.0f;
        float m_Pitch = 0.35f, m_Yaw = -0.6f;

        float m_ViewportWidth = 1280, m_ViewportHeight = 720;

        struct Transition
        {
            bool Active = false;
            bool OrthographicAtEnd = false; // perspective now, orthographic once it arrives
            float Time = 0.0f, Duration = 0.0f;
            EditorCameraState From, To;
        } m_Transition;
    };

}
