#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Core/Timestep.h"
#include "Zamin/Events/Event.h"

#include <string>
#include <vector>

namespace Zamin {

    class Layer
    {
    public:
        explicit Layer(std::string name = "Layer") : m_DebugName(std::move(name)) {}
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(Timestep) {}
        virtual void OnImGuiRender() {}
        virtual void OnEvent(Event&) {}

        const std::string& GetName() const { return m_DebugName; }
    protected:
        std::string m_DebugName;
    };

    // Layers are updated first-to-last and receive events last-to-first,
    // so overlays (pushed at the end) get the first chance to handle input.
    class LayerStack
    {
    public:
        LayerStack() = default;
        ~LayerStack();

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* overlay);
        void PopLayer(Layer* layer);
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
