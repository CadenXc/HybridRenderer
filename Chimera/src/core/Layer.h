#pragma once

#include "Core/Events/Event.h"
#include "Core/Timestep.h"

namespace Chimera {

	class Layer
	{
	public:
		virtual ~Layer() = default;

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep ts) {}
		virtual void OnUIRender() {}
		virtual void OnRender() {}
		virtual void OnEvent(Event& event) {}
	};

}
