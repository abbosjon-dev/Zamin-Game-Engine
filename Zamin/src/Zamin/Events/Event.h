#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Core/KeyCodes.h"

#include <string>
#include <vector>

namespace Zamin {

    enum class EventType
    {
        None = 0,
        WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowDrop,
        KeyPressed, KeyReleased, KeyTyped,
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
    };

    enum EventCategory
    {
        EventCategoryNone        = 0,
        EventCategoryApplication = ZM_BIT(0),
        EventCategoryInput       = ZM_BIT(1),
        EventCategoryKeyboard    = ZM_BIT(2),
        EventCategoryMouse       = ZM_BIT(3),
        EventCategoryMouseButton = ZM_BIT(4)
    };

#define ZM_EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::type; } \
    EventType GetEventType() const override { return GetStaticType(); } \
    const char* GetName() const override { return #type; }

#define ZM_EVENT_CLASS_CATEGORY(category) int GetCategoryFlags() const override { return category; }

    class Event
    {
    public:
        virtual ~Event() = default;
        bool Handled = false;

        virtual EventType GetEventType() const = 0;
        virtual const char* GetName() const = 0;
        virtual int GetCategoryFlags() const = 0;
        virtual std::string ToString() const { return GetName(); }

        bool IsInCategory(EventCategory category) const { return GetCategoryFlags() & category; }
    };

    class EventDispatcher
    {
    public:
        explicit EventDispatcher(Event& event) : m_Event(event) {}

        template<typename T, typename F>
        bool Dispatch(const F& func)
        {
            if (m_Event.GetEventType() == T::GetStaticType())
            {
                m_Event.Handled |= func(static_cast<T&>(m_Event));
                return true;
            }
            return false;
        }

    private:
        Event& m_Event;
    };

    // ---------------- Application events ----------------
    class WindowResizeEvent : public Event
    {
    public:
        WindowResizeEvent(uint32_t width, uint32_t height) : m_Width(width), m_Height(height) {}
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        std::string ToString() const override { return std::format("WindowResizeEvent: {}, {}", m_Width, m_Height); }
        ZM_EVENT_CLASS_TYPE(WindowResize)
        ZM_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    private:
        uint32_t m_Width, m_Height;
    };

    class WindowCloseEvent : public Event
    {
    public:
        ZM_EVENT_CLASS_TYPE(WindowClose)
        ZM_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class WindowDropEvent : public Event
    {
    public:
        explicit WindowDropEvent(std::vector<std::string> paths) : m_Paths(std::move(paths)) {}
        const std::vector<std::string>& GetPaths() const { return m_Paths; }
        ZM_EVENT_CLASS_TYPE(WindowDrop)
        ZM_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    private:
        std::vector<std::string> m_Paths;
    };

    // ---------------- Key events ----------------
    class KeyEvent : public Event
    {
    public:
        Key GetKeyCode() const { return m_KeyCode; }
        ZM_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)
    protected:
        explicit KeyEvent(Key keycode) : m_KeyCode(keycode) {}
        Key m_KeyCode;
    };

    class KeyPressedEvent : public KeyEvent
    {
    public:
        KeyPressedEvent(Key keycode, bool isRepeat = false) : KeyEvent(keycode), m_IsRepeat(isRepeat) {}
        bool IsRepeat() const { return m_IsRepeat; }
        std::string ToString() const override { return std::format("KeyPressedEvent: {} (repeat={})", (int)m_KeyCode, m_IsRepeat); }
        ZM_EVENT_CLASS_TYPE(KeyPressed)
    private:
        bool m_IsRepeat;
    };

    class KeyReleasedEvent : public KeyEvent
    {
    public:
        explicit KeyReleasedEvent(Key keycode) : KeyEvent(keycode) {}
        ZM_EVENT_CLASS_TYPE(KeyReleased)
    };

    class KeyTypedEvent : public KeyEvent
    {
    public:
        explicit KeyTypedEvent(Key keycode) : KeyEvent(keycode) {}
        ZM_EVENT_CLASS_TYPE(KeyTyped)
    };

    // ---------------- Mouse events ----------------
    class MouseMovedEvent : public Event
    {
    public:
        MouseMovedEvent(float x, float y) : m_MouseX(x), m_MouseY(y) {}
        float GetX() const { return m_MouseX; }
        float GetY() const { return m_MouseY; }
        ZM_EVENT_CLASS_TYPE(MouseMoved)
        ZM_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
    private:
        float m_MouseX, m_MouseY;
    };

    class MouseScrolledEvent : public Event
    {
    public:
        MouseScrolledEvent(float xOffset, float yOffset) : m_XOffset(xOffset), m_YOffset(yOffset) {}
        float GetXOffset() const { return m_XOffset; }
        float GetYOffset() const { return m_YOffset; }
        ZM_EVENT_CLASS_TYPE(MouseScrolled)
        ZM_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
    private:
        float m_XOffset, m_YOffset;
    };

    class MouseButtonEvent : public Event
    {
    public:
        MouseButton GetMouseButton() const { return m_Button; }
        ZM_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput | EventCategoryMouseButton)
    protected:
        explicit MouseButtonEvent(MouseButton button) : m_Button(button) {}
        MouseButton m_Button;
    };

    class MouseButtonPressedEvent : public MouseButtonEvent
    {
    public:
        explicit MouseButtonPressedEvent(MouseButton button) : MouseButtonEvent(button) {}
        ZM_EVENT_CLASS_TYPE(MouseButtonPressed)
    };

    class MouseButtonReleasedEvent : public MouseButtonEvent
    {
    public:
        explicit MouseButtonReleasedEvent(MouseButton button) : MouseButtonEvent(button) {}
        ZM_EVENT_CLASS_TYPE(MouseButtonReleased)
    };

}
