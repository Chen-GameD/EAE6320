// Includes
//=========

#include "Graphics.h"

#include "cMesh.h"
#include "cEffect.h"
#include "cView.h"

#include <Engine/Concurrency/cEvent.h>
#include <Engine/Logging/Logging.h>
#include <Engine/UserOutput/UserOutput.h>
#include "sContext.h"

// Static Data
//============

namespace
{
	// Static View
	eae6320::Graphics::cView s_view;

	// Constant buffer object
	eae6320::Graphics::cConstantBuffer s_constantBuffer_frame(eae6320::Graphics::ConstantBufferTypes::Frame);

	// Submission Data
	//----------------

	// This struct's data is populated at submission time;
	// it must cache whatever is necessary in order to render a frame
	/*struct sDataRequiredToRenderAFrame
	{
		eae6320::Graphics::ConstantBufferFormats::sFrame constantData_frame;
	};*/
	// In our class there will be two copies of the data required to render a frame:
	//	* One of them will be in the process of being populated by the data currently being submitted by the application loop thread
	//	* One of them will be fully populated and in the process of being rendered from in the render thread
	// (In other words, one is being produced while the other is being consumed)
	eae6320::sDataRequiredToRenderAFrame s_dataRequiredToRenderAFrame[2];
	auto* s_dataBeingSubmittedByApplicationThread = &s_dataRequiredToRenderAFrame[0];
	auto* s_dataBeingRenderedByRenderThread = &s_dataRequiredToRenderAFrame[1];
	// The following two events work together to make sure that
	// the main/render thread and the application loop thread can work in parallel but stay in sync:
	// This event is signaled by the application loop thread when it has finished submitting render data for a frame
	// (the main/render thread waits for the signal)
	eae6320::Concurrency::cEvent s_whenAllDataHasBeenSubmittedFromApplicationThread;
	// This event is signaled by the main/render thread when it has swapped render data pointers.
	// This means that the renderer is now working with all the submitted data it needs to render the next frame,
	// and the application loop thread can start submitting data for the following frame
	// (the application loop thread waits for the signal)
	eae6320::Concurrency::cEvent s_whenDataForANewFrameCanBeSubmittedFromApplicationThread;

	//static mesh
	eae6320::Graphics::cMesh* s_mesh_1;
	eae6320::Graphics::cMesh* s_mesh_2;
	//static effect
	eae6320::Graphics::cEffect* s_effect_1;
	eae6320::Graphics::cEffect* s_effect_2;
}

// Interface
//==========

// Submission
//-----------

void eae6320::Graphics::SubmitElapsedTime(const float i_elapsedSecondCount_systemTime, const float i_elapsedSecondCount_simulationTime)
{
	EAE6320_ASSERT(s_dataBeingSubmittedByApplicationThread);
	auto& constantData_frame = s_dataBeingSubmittedByApplicationThread->constantData_frame;
	constantData_frame.g_elapsedSecondCount_systemTime = i_elapsedSecondCount_systemTime;
	constantData_frame.g_elapsedSecondCount_simulationTime = i_elapsedSecondCount_simulationTime;
}

void eae6320::Graphics::SubmitBackBufferColor(const float r, const float g, const float b, const float a)
{
	EAE6320_ASSERT(s_dataBeingSubmittedByApplicationThread);
	auto& backBufferColor = s_dataBeingSubmittedByApplicationThread->backBufferColor;
	backBufferColor.R = r;
	backBufferColor.G = g;
	backBufferColor.B = b;
	backBufferColor.A = a;
}

eae6320::cResult eae6320::Graphics::WaitUntilDataForANewFrameCanBeSubmitted(const unsigned int i_timeToWait_inMilliseconds)
{
	return Concurrency::WaitForEvent(s_whenDataForANewFrameCanBeSubmittedFromApplicationThread, i_timeToWait_inMilliseconds);
}

eae6320::cResult eae6320::Graphics::SignalThatAllDataForAFrameHasBeenSubmitted()
{
	return s_whenAllDataHasBeenSubmittedFromApplicationThread.Signal();
}

// Render
//-------

void eae6320::Graphics::RenderFrame()
{
	// Wait for the application loop to submit data to be rendered
	{
		if (Concurrency::WaitForEvent(s_whenAllDataHasBeenSubmittedFromApplicationThread))
		{
			// Switch the render data pointers so that
			// the data that the application just submitted becomes the data that will now be rendered
			std::swap(s_dataBeingSubmittedByApplicationThread, s_dataBeingRenderedByRenderThread);
			// Once the pointers have been swapped the application loop can submit new data
			if (!s_whenDataForANewFrameCanBeSubmittedFromApplicationThread.Signal())
			{
				EAE6320_ASSERTF(false, "Couldn't signal that new graphics data can be submitted");
				Logging::OutputError("Failed to signal that new render data can be submitted");
				UserOutput::Print("The renderer failed to signal to the application that new graphics data can be submitted."
					" The application is probably in a bad state and should be exited");
				return;
			}
		}
		else
		{
			EAE6320_ASSERTF(false, "Waiting for the graphics data to be submitted failed");
			Logging::OutputError("Waiting for the application loop to submit data to be rendered failed");
			UserOutput::Print("The renderer failed to wait for the application to submit data to be rendered."
				" The application is probably in a bad state and should be exited");
			return;
		}
	}

	// Every frame an entirely new image will be created.
	// Before drawing anything, then, the previous image will be erased
	// by "clearing" the image buffer (filling it with a solid color)
	s_view.ClearImageBuffer(s_dataBeingRenderedByRenderThread);

	// In addition to the color buffer there is also a hidden image called the "depth buffer"
	// which is used to make it less important which order draw calls are made.
	// It must also be "cleared" every frame just like the visible color buffer.
	s_view.ClearDepthBuffer();

	//// Update the frame constant buffer
	s_view.UpdateFrameConstantBuffer(s_constantBuffer_frame, s_dataBeingRenderedByRenderThread);

	// Bind the shading data
	s_effect_1->BindShadingData();

	// Draw the geometry
	s_mesh_1->DrawGeometry();

	// Bind the shading data
	s_effect_2->BindShadingData();

	// Draw the geometry
	s_mesh_2->DrawGeometry();

	// Everything has been drawn to the "back buffer", which is just an image in memory.
	// In order to display it the contents of the back buffer must be "presented"
	// (or "swapped" with the "front buffer", which is the image that is actually being displayed)
	s_view.SwapFrontBuffer();

	// After all of the data that was submitted for this frame has been used
	// you must make sure that it is all cleaned up and cleared out
	// so that the struct can be re-used (i.e. so that data for a new frame can be submitted to it)
	{
		// (At this point in the class there isn't anything that needs to be cleaned up)
		//dataRequiredToRenderFrame	// TODO
	}
}

// Initialize / Clean Up
//----------------------

eae6320::cResult eae6320::Graphics::Initialize(const sInitializationParameters& i_initializationParameters)
{
	auto result = Results::Success;

	// Initialize the platform-specific context
	if (!(result = sContext::g_context.Initialize(i_initializationParameters)))
	{
		EAE6320_ASSERTF(false, "Can't initialize Graphics without context");
		return result;
	}
	// Initialize the platform-independent graphics objects
	{
		if (result = s_constantBuffer_frame.Initialize())
		{
			// There is only a single frame constant buffer that is reused
			// and so it can be bound at initialization time and never unbound
			s_constantBuffer_frame.Bind(
				// In our class both vertex and fragment shaders use per-frame constant data
				static_cast<uint_fast8_t>(eShaderType::Vertex) | static_cast<uint_fast8_t>(eShaderType::Fragment));
		}
		else
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without frame constant buffer");
			return result;
		}
	}
	// Initialize the events
	{
		if (!(result = s_whenAllDataHasBeenSubmittedFromApplicationThread.Initialize(Concurrency::EventType::ResetAutomaticallyAfterBeingSignaled)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without event for when data has been submitted from the application thread");
			return result;
		}
		if (!(result = s_whenDataForANewFrameCanBeSubmittedFromApplicationThread.Initialize(Concurrency::EventType::ResetAutomaticallyAfterBeingSignaled,
			Concurrency::EventState::Signaled)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without event for when data can be submitted from the application thread");
			return result;
		}
	}
	// Initialize the views
	{
		if (!(result = s_view.InitializeViews(i_initializationParameters)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without the views");
			return result;
		}
	}
	// Initialize the shading data
	{
		const char* vertexShaderAddress_1 = "data/Shaders/Vertex/standard.shader";
		const char* fragmentShaderAddress_1 = "data/Shaders/Fragment/myShader_1.shader";
		/*if (!(result = s_effect_1.InitializeShadingData(vertexShaderAddress_1, fragmentShaderAddress_1)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without the shading data");
			return result;
		}*/
		if (!(result = cEffect::CreateEffect(s_effect_1, vertexShaderAddress_1, fragmentShaderAddress_1)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without the shading data");
			return result;
		}

		const char* vertexShaderAddress_2 = "data/Shaders/Vertex/standard.shader";
		const char* fragmentShaderAddress_2 = "data/Shaders/Fragment/myShader_2.shader";
		/*if (!(result = s_effect_2.InitializeShadingData(vertexShaderAddress_2, fragmentShaderAddress_2)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without the shading data");
			return result;
		}*/
		if (!(result = cEffect::CreateEffect(s_effect_2, vertexShaderAddress_2, fragmentShaderAddress_2)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without the shading data");
			return result;
		}


		Logging::OutputMessage("A single effect takes : %d:", sizeof(s_effect_1));
	}
	// Initialize the geometry
	{
		//Data input is temporarily hardcoded...
		eae6320::Graphics::VertexFormats::sVertex_mesh vertexData_1[4];
		{
			// OpenGL is right-handed

			vertexData_1[0].x = 0.0f;
			vertexData_1[0].y = 0.0f;
			vertexData_1[0].z = 0.0f;

			vertexData_1[1].x = 1.0f;
			vertexData_1[1].y = 0.0f;
			vertexData_1[1].z = 0.0f;

			vertexData_1[2].x = 1.0f;
			vertexData_1[2].y = 1.0f;
			vertexData_1[2].z = 0.0f;

			vertexData_1[3].x = 0.0f;
			vertexData_1[3].y = 1.0f;
			vertexData_1[3].z = 0.0f;
		}

		uint16_t indexArray_1[6] = {0,1,2,0,2,3};

		if (!(result = cMesh::CreateMesh(s_mesh_1, vertexData_1, indexArray_1, 4, 6)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without the geometry data");
			return result;
		}

		/*if (!(result = s_mesh_1.InitializeGeometry(vertexData_1, indexArray_1, 4, 6)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without the geometry data");
			return result;
		}*/

		//Data input is temporarily hardcoded...
		eae6320::Graphics::VertexFormats::sVertex_mesh vertexData_2[7];
		{
			// OpenGL is right-handed

			vertexData_2[0].x = 0.0f;
			vertexData_2[0].y = 0.0f;
			vertexData_2[0].z = 0.0f;

			vertexData_2[1].x = 0.0f;
			vertexData_2[1].y = 1.0f;
			vertexData_2[1].z = 0.0f;

			vertexData_2[2].x = -1.0f;
			vertexData_2[2].y = 1.0f;
			vertexData_2[2].z = 0.0f;

			vertexData_2[3].x = -1.0f;
			vertexData_2[3].y = 0.0f;
			vertexData_2[3].z = 0.0f;

			vertexData_2[4].x = 0.0f;
			vertexData_2[4].y = -1.0f;
			vertexData_2[4].z = 0.0f;

			vertexData_2[5].x = 1.0f;
			vertexData_2[5].y = -1.0f;
			vertexData_2[5].z = 0.0f;

			vertexData_2[6].x = 1.0f;
			vertexData_2[6].y = 0.0f;
			vertexData_2[6].z = 0.0f;
		}

		uint16_t indexArray_2[15] = { 0,1,2,0,2,3,0,3,4,0,4,5,0,5,6 };

		if (!(result = cMesh::CreateMesh(s_mesh_2, vertexData_2, indexArray_2, 7, 15)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without the geometry data");
			return result;
		}

		/*if (!(result = s_mesh_2.InitializeGeometry(vertexData_2, indexArray_2, 7, 15)))
		{
			EAE6320_ASSERTF(false, "Can't initialize Graphics without the geometry data");
			return result;
		}*/

		Logging::OutputMessage("A single mesh takes : %d:", sizeof(s_mesh_1));
	}

	return result;
}

eae6320::cResult eae6320::Graphics::CleanUp()
{
	auto result = Results::Success;

	s_view.CleanUp();

	if (s_mesh_1)
	{
		s_mesh_1->DecrementReferenceCount();
		s_mesh_1 = nullptr;
	}
	if (s_mesh_2)
	{
		s_mesh_2->DecrementReferenceCount();
		s_mesh_2 = nullptr;
	}
	if (s_effect_1)
	{
		s_effect_1->DecrementReferenceCount();
		s_effect_1 = nullptr;
	}
	if (s_effect_2)
	{
		s_effect_2->DecrementReferenceCount();
		s_effect_2 = nullptr;
	}

	//result = s_mesh_1->CleanUp();

	//result = s_effect_1.CleanUp();

	//result = s_mesh_2->CleanUp();

	//result = s_effect_2.CleanUp();

	{
		const auto result_constantBuffer_frame = s_constantBuffer_frame.CleanUp();
		if (!result_constantBuffer_frame)
		{
			EAE6320_ASSERT(false);
			if (result)
			{
				result = result_constantBuffer_frame;
			}
		}
	}

	{
		const auto result_context = sContext::g_context.CleanUp();
		if (!result_context)
		{
			EAE6320_ASSERT(false);
			if (result)
			{
				result = result_context;
			}
		}
	}

	return result;
}