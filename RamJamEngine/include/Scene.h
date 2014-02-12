#pragma once

#include "Types.h"
#include "SceneLoader.h"
#include "Transform.h"
#include "Color.h"
#include "GameObject.h"

using namespace RJE_COLOR;

//////////////////////////////////////////////////////////////////////////
struct Scene
{
	SceneLoader				mSceneLoader;
	std::vector<unique_ptr<GameObject>> mGameObjects;
	
	//-----------------------------------------------
	// Editor Values
	Transform*		mGameObjectEditorTransform;
	std::string		mGameObjectEditorName;
	Vector3			mGameObjectEditorPos;
	Vector3			mGameObjectEditorScale;
	TwQuaternion	mGameObjectEditorRot;
	Color			mGameObjectEditorColor;
	//-----------------------------------------------

	//-----------------------------------------------
	// Scene parameters
	BOOL	mbDeferredRendering;
	Vector4	mAmbientLightColor;
	Vector4	mFogColor;
	float	mFogStart;
	float	mFogRange;
	BOOL	mbUseTexture;
	BOOL	mbUseBlending;
	BOOL	mbUseFog;
	BOOL	mbWireframe;
	//---------
	BOOL	mbUseFaceNormals;
	BOOL	mbOnlyPosition;
	BOOL	mbOnlyAlbedo;
	BOOL	mbOnlyNormals;
	BOOL	mbOnlySpecular;
	BOOL	mbViewPerSampleShading;
	BOOL	mbViewLightCount;
	//-----------------------------------------------
	
	Scene();
	void LoadFromFile(const char* pFile);
};
