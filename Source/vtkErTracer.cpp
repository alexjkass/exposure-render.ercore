/*
	Copyright (c) 2011, T. Kroes <t.kroes@tudelft.nl>
	All rights reserved.

	Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

	- Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
	- Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
	- Neither the name of the TU Delft nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include "vtkErStable.h"
#include "vtkErTracer.h"
#include "vtkErCamera.h"
#include "vtkErVolume.h"
#include "vtkErLight.h"
#include "vtkErObject.h"

#include "vtkgl.h"

vtkStandardNewMacro(vtkErTracerData);
vtkCxxRevisionMacro(vtkErTracerData, "$Revision: 1.0 $");

vtkStandardNewMacro(vtkErTracer);
vtkCxxRevisionMacro(vtkErTracer, "$Revision: 1.0 $");

vtkErTracer::vtkErTracer(void)
{
	this->ImageBuffer = NULL;

	this->RenderSize[0]	= 0;
	this->RenderSize[1]	= 0;

	this->SetNumberOfInputPorts(4);
	this->SetNumberOfOutputPorts(0);

	for (int i = 0; i < MAX_NO_VOLUMES; i++)
	{
		this->Opacity[i]		= vtkPiecewiseFunction::New();
		this->Diffuse[i]		= vtkColorTransferFunction::New();
		this->Specular[i]		= vtkColorTransferFunction::New();
		this->Glossiness[i]		= vtkPiecewiseFunction::New();
		this->Emission[i]		= vtkColorTransferFunction::New();

		double Min = 0, Max = 255;

		Opacity[i]->AddPoint(Min, 0);
		Opacity[i]->AddPoint(Max, 1);
		Diffuse[i]->AddRGBPoint(Min, 1, 1, 1);
		Diffuse[i]->AddRGBPoint(Max, 1, 1, 1);
		Specular[i]->AddRGBPoint(Min, 0, 0, 0);
		Specular[i]->AddRGBPoint(Max, 0, 0, 0);
		Glossiness[i]->AddPoint(Min, 1);
		Glossiness[i]->AddPoint(Max, 1);
		Emission[i]->AddRGBPoint(Min, 0, 0, 0);
		Emission[i]->AddRGBPoint(Max, 0, 0, 0);
	}

	this->SetStepFactorPrimary(3.0f);
	this->SetStepFactorShadow(3.0f);
	this->SetShadows(true);
	this->SetShadingMode(Enums::PhaseFunctionOnly);
	this->SetDensityScale(10.0f);
	this->SetOpacityModulated(true);
	this->SetGradientMode(Enums::CentralDifferences);
	this->SetGradientThreshold(0.5f);
	this->SetGradientFactor(1.0f);

	glGenTextures(1, &TextureID);

	this->Tracer.SetDirty();
}

vtkErTracer::~vtkErTracer(void)
{
	delete[] this->ImageBuffer;
}

int vtkErTracer::FillInputPortInformation(int Port, vtkInformation* Info)
{
	switch (Port)
	{
		case VolumesPort:
		{
			Info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkErVolumeData");
			Info->Set(vtkAlgorithm::INPUT_IS_REPEATABLE(), 1);
			Info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 0);
			return 1;
		}

		case LightsPort:
		{
			Info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkErLightData");
			Info->Set(vtkAlgorithm::INPUT_IS_REPEATABLE(), 1);
			Info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 0);
			return 1;
		}

		case ObjectsPort:
		{
			Info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkErObjectData");
			Info->Set(vtkAlgorithm::INPUT_IS_REPEATABLE(), 1);
			Info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
			return 1;
		}

		case ClippingObjectsPort:
		{
			Info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkErClippingObjectData");
			Info->Set(vtkAlgorithm::INPUT_IS_REPEATABLE(), 1);
			Info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
			return 1;
		}
	}

	return 0;
}

int vtkErTracer::FillOutputPortInformation(int Port, vtkInformation* Info)
{
	return 1;
}

void vtkErTracer::BeforeRender(vtkRenderer* Renderer, vtkVolume* Volume)
{
	for (int i = 0; i < MAX_NO_VOLUMES; i++)
	{
		this->Tracer.Opacity1D[i].Reset();
		
		for (int j = 0; j < this->Opacity[i]->GetSize(); j++)
		{
			double NodeValue[4];
			this->Opacity[i]->GetNodeValue(j, NodeValue);
			this->Tracer.Opacity1D[i].AddNode(ExposureRender::ScalarNode(NodeValue[0], NodeValue[1]));
		}
		
		this->Tracer.Diffuse1D[i].Reset();

		for (int j = 0; j < this->Diffuse[i]->GetSize(); j++)
		{
			double NodeValue[6];
			this->Diffuse[i]->GetNodeValue(j, NodeValue);
			this->Tracer.Diffuse1D[i].AddNode(ExposureRender::ColorNode::FromRGB(NodeValue[0], ExposureRender::ColorRGBf(NodeValue[1], NodeValue[2], NodeValue[3])));
		}
		
		this->Tracer.Specular1D[i].Reset();

		for (int j = 0; j < this->Specular[i]->GetSize(); j++)
		{
			double NodeValue[6];
			this->Specular[i]->GetNodeValue(j, NodeValue);
			this->Tracer.Specular1D[i].AddNode(ExposureRender::ColorNode::FromRGB(NodeValue[0], ExposureRender::ColorRGBf(NodeValue[1], NodeValue[2], NodeValue[3])));
		}

		this->Tracer.Glossiness1D[i].Reset();
		
		for (int j = 0; j < this->Glossiness[i]->GetSize(); j++)
		{
			double NodeValue[4];
			this->Glossiness[i]->GetNodeValue(j, NodeValue);
			this->Tracer.Glossiness1D[i].AddNode(ExposureRender::ScalarNode(NodeValue[0], NodeValue[1]));
		}

		this->Tracer.Emission1D[i].Reset();

		for (int j = 0; j < this->Emission[i]->GetSize(); j++)
		{
			double NodeValue[6];
			this->Emission[i]->GetNodeValue(j, NodeValue);
			this->Tracer.Emission1D[i].AddNode(ExposureRender::ColorNode::FromRGB(NodeValue[0], ExposureRender::ColorRGBf(NodeValue[1], NodeValue[2], NodeValue[3])));
		}
	}
	
	this->Tracer.RenderSettings.Traversal.StepFactorPrimary 	= this->GetStepFactorPrimary();
	this->Tracer.RenderSettings.Traversal.StepFactorShadow		= this->GetStepFactorShadow();
	this->Tracer.RenderSettings.Traversal.Shadows				= this->GetShadows();
	this->Tracer.RenderSettings.Shading.Type					= this->GetShadingMode();
	this->Tracer.RenderSettings.Shading.DensityScale			= this->GetDensityScale();
	this->Tracer.RenderSettings.Shading.OpacityModulated		= this->GetOpacityModulated();
	this->Tracer.RenderSettings.Shading.GradientMode			= this->GetGradientMode();
	this->Tracer.RenderSettings.Shading.GradientThreshold		= this->GetGradientThreshold();
	this->Tracer.RenderSettings.Shading.GradientFactor			= this->GetGradientFactor();

	vtkCamera* Camera = Renderer->GetActiveCamera();
	
	if (Camera->GetMTime() != this->LastCameraMTime)
	{
		this->Tracer.SetDirty();
		this->LastCameraMTime = Camera->GetMTime();
	}

	if (Camera)
	{
		this->Tracer.Camera.FilmSize		= Vec2i(Renderer->GetRenderWindow()->GetSize()[0], Renderer->GetRenderWindow()->GetSize()[1]);
		this->Tracer.Camera.Pos				= Vec3f(Camera->GetPosition()[0], Camera->GetPosition()[1], Camera->GetPosition()[2]);
		this->Tracer.Camera.Target			= Vec3f(Camera->GetFocalPoint()[0], Camera->GetFocalPoint()[1], Camera->GetFocalPoint()[2]);
		this->Tracer.Camera.Up				= Vec3f(Camera->GetViewUp()[0], Camera->GetViewUp()[1], Camera->GetViewUp()[2]);
		this->Tracer.Camera.ClipNear		= 0.0f;//Camera->GetClippingRange()[0];
		this->Tracer.Camera.ClipFar			= 10000.0f;//Camera->GetClippingRange()[1];
		this->Tracer.Camera.FOV				= Camera->GetViewAngle();
	}

	vtkErCamera* ErCamera = dynamic_cast<vtkErCamera*>(Camera);

	if (ErCamera)
	{
		this->Tracer.Camera.FocalDistance		= ErCamera->GetFocalDistance();
		this->Tracer.Camera.Exposure			= ErCamera->GetExposure();
		this->Tracer.Camera.Gamma				= ErCamera->GetGamma();
		this->Tracer.Camera.ApertureShape		= ErCamera->GetApertureShape();
		this->Tracer.Camera.ApertureSize		= ErCamera->GetApertureSize();
		this->Tracer.Camera.NoApertureBlades	= ErCamera->GetNoApertureBlades();
		this->Tracer.Camera.ApertureAngle		= ErCamera->GetApertureAngle();
	}
	
	const int NoVolumes = this->GetNumberOfInputConnections(VolumesPort);

	this->Tracer.VolumeIDs.Count = 0;

	for (int i = 0; i < NoVolumes; i++)
	{
		vtkErVolumeData* VolumeData = vtkErVolumeData::SafeDownCast(this->GetInputDataObject(VolumesPort, i));

		if (VolumeData)
		{
			this->Tracer.VolumeIDs[this->Tracer.VolumeIDs.Count] = VolumeData->Bindable.ID;
			this->Tracer.VolumeIDs.Count++;
		}
	}

	const int NoLights = this->GetNumberOfInputConnections(LightsPort);

	this->Tracer.LightIDs.Count = 0;

	for (int i = 0; i < NoLights; i++)
	{
		vtkErLightData* LightData = vtkErLightData::SafeDownCast(this->GetInputDataObject(LightsPort, i));

		if (LightData && LightData->Bindable.Enabled)
		{
			this->Tracer.LightIDs[this->Tracer.LightIDs.Count] = LightData->Bindable.ID;
			this->Tracer.LightIDs.Count++;
		}
	}

	const int NoObjects = this->GetNumberOfInputConnections(ObjectsPort);

	this->Tracer.ObjectIDs.Count = 0;
	
	for (int i = 0; i < NoObjects; i++)
	{
		vtkErObjectData* ObjectData = vtkErObjectData::SafeDownCast(this->GetInputDataObject(ObjectsPort, i));

		if (ObjectData && ObjectData->Bindable.Enabled)
		{
			this->Tracer.ObjectIDs[this->Tracer.ObjectIDs.Count] = ObjectData->Bindable.ID;
			this->Tracer.ObjectIDs.Count++;
		}
	}

	if (this->Tracer.GetDirty())
	{
		ER_CALL(ExposureRender::BindTracer(this->Tracer));
		this->Tracer.SetDirty(false);
	}
}

void vtkErTracer::Render(vtkRenderer* Renderer, vtkVolume* Volume)
{
	this->InvokeEvent(vtkCommand::VolumeMapperRenderStartEvent,0);

	this->BeforeRender(Renderer, Volume);

	int* WindowSize = Renderer->GetRenderWindow()->GetSize();

	if (WindowSize[0] != this->RenderSize[0] || WindowSize[1] != this->RenderSize[1])
	{
		RenderSize[0] = WindowSize[0];
		RenderSize[1] = WindowSize[1];

		delete[] this->ImageBuffer;
		this->ImageBuffer = new ExposureRender::ColorRGBAuc[this->RenderSize[0] * this->RenderSize[1]];
	}

	ER_CALL(ExposureRender::Render(this->Tracer.ID));
	ER_CALL(ExposureRender::GetRunningEstimate(this->Tracer.ID, this->ImageBuffer));

	glEnable(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, TextureID);
    
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, RenderSize[0], RenderSize[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char*)this->ImageBuffer);
	glBindTexture(GL_TEXTURE_2D, TextureID);

	double d = 0.5;
	
    Renderer->SetDisplayPoint(0,0,d);
    Renderer->DisplayToWorld();
    double coordinatesA[4];
    Renderer->GetWorldPoint(coordinatesA);

    Renderer->SetDisplayPoint(RenderSize[0],0,d);
    Renderer->DisplayToWorld();
    double coordinatesB[4];
    Renderer->GetWorldPoint(coordinatesB);

    Renderer->SetDisplayPoint(RenderSize[0], RenderSize[1],d);
    Renderer->DisplayToWorld();
    double coordinatesC[4];
    Renderer->GetWorldPoint(coordinatesC);

    Renderer->SetDisplayPoint(0,RenderSize[1],d);
    Renderer->DisplayToWorld();
    double coordinatesD[4];
    Renderer->GetWorldPoint(coordinatesD);
	
	glPushAttrib(GL_LIGHTING);
	glDisable(GL_LIGHTING);

	glBegin(GL_QUADS);
		glTexCoord2i(0, 0);
		glVertex4dv(coordinatesA);
		glTexCoord2i(1, 0);
		glVertex4dv(coordinatesB);
		glTexCoord2i(1, 1);
		glVertex4dv(coordinatesC);
		glTexCoord2i(0, 1);
		glVertex4dv(coordinatesD);
	glEnd();

	glPopAttrib();

	this->InvokeEvent(vtkCommand::VolumeMapperRenderEndEvent,0);
}
