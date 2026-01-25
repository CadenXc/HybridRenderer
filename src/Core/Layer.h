#pragma once

namespace Chimera {

	class Layer
	{
	public:
		virtual ~Layer() = default;

		virtual void OnAttach() {}
		
		virtual void OnDetach() {}

				virtual void OnUpdate(float ts) {}

				

				virtual void OnUIRender() {}

		

				virtual void OnResize(uint32_t width, uint32_t height) {}

			};

		

		}

		