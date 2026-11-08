#pragma once

/// @file
/// @brief Layer and LayerStack: the parts of an application that update, draw and receive events.

#include "Zamin/Core/Base.h"
#include "Zamin/Core/Timestep.h"
#include "Zamin/Events/Event.h"

#include <string>
#include <vector>

namespace Zamin {

    /// One part of an application (the editor's EditorLayer, the player's layer, ImGuiLayer): the
    /// Application calls these from its main loop. Pushed with Application::PushLayer / PushOverlay;
    /// the layer stack owns it then.
    class Layer
    {
    public:
        explicit Layer(std::string name = "Layer") : m_DebugName(std::move(name)) {}
        virtual ~Layer() = default;

        /// Pushed onto the application's stack (resources, scenes, the first state).
        virtual void OnAttach() {}
        /// Taken off the stack, or the application is being destroyed.
        virtual void OnDetach() {}
        /// Every frame, first layer to last (the timestep is the frame's real time, capped at the
        /// project's longest frame).
        virtual void OnUpdate(Timestep) {}
        /// Every frame inside the ImGui frame (only when the application has ImGui).
        virtual void OnImGuiRender() {}
        /// A window event, top layer first; set Event::Handled to keep it from the layers below.
        virtual void OnEvent(Event&) {}
        /// The application stands still in the background (Application::IsPausedInBackground: the window lost
        /// the focus and the game does not run in the background): nothing is updated; a layer draws the
        /// paused picture here (the player: the last frame dimmed, "Pauza") when the pause starts and now and
        /// then after it. True when it drew - the application then shows it.
        virtual bool OnRenderPaused() { return false; }
        /// The main loop ended (the window was closed, Application::Close, ZAMIN_MAX_FRAMES) - before anything is
        /// shut down: the window, the renderer and the audio still work, and the exit code can still be set
        /// (OnDetach comes later, while the application is destroyed).
        virtual void OnRunEnded() {}

        const std::string& GetName() const { return m_DebugName; }  ///< the name given to the constructor
    protected:
        std::string m_DebugName;
    };

    /// Layers are updated first-to-last and receive events last-to-first,
    /// so overlays (pushed at the end) get the first chance to handle input.
    class LayerStack
    {
    public:
        LayerStack() = default;
        /// Detaches (OnDetach) and deletes every layer it still holds.
        ~LayerStack();

        /// Inserts below the overlays (after the layers pushed before it). Does not call OnAttach.
        void PushLayer(Layer* layer);
        /// Appends on top. Does not call OnAttach.
        void PushOverlay(Layer* overlay);
        /// Removes a layer (calls its OnDetach; the caller deletes it). Unknown layers are ignored.
        void PopLayer(Layer* layer);
        /// Removes an overlay (calls its OnDetach; the caller deletes it). Unknown ones are ignored.
        void PopOverlay(Layer* overlay);

        std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
        std::vector<Layer*>::iterator end() { return m_Layers.end(); }
        std::vector<Layer*>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
        std::vector<Layer*>::reverse_iterator rend() { return m_Layers.rend(); }

    private:
        std::vector<Layer*> m_Layers;
        unsigned int m_LayerInsertIndex = 0;
    };

}
