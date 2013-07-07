#include "DX11Wrapper.h"

DX11Wrapper::DX11Wrapper()
{
	mDX11Device			= nullptr;
	mDX11DepthBuffer	= nullptr;
	mRenderTargetView	= nullptr;
	md3dDriverType		= D3D_DRIVER_TYPE_HARDWARE;
	ZeroMemory(&mScreenViewport, sizeof(D3D11_VIEWPORT));

	mVertexBuffer    = nullptr;
	mIndexBuffer     = nullptr;
	mFX              = nullptr;
	mTech            = nullptr;
	mfxWorldViewProj = nullptr;
	mInputLayout     = nullptr;
	mRasterizerState = nullptr;

	DirectX::XMMATRIX Id = DirectX::XMMatrixIdentity();
	XMStoreFloat4x4(&mWorld, Id);
	XMStoreFloat4x4(&mView, Id);
	XMStoreFloat4x4(&mProj, Id);
}

//////////////////////////////////////////////////////////////////////////
void DX11Wrapper::Initialize(HWND hMainWnd, int windowWidth, int windowHeight)
{
	mDX11Device = new DX11Device;
	mDX11DepthBuffer = new DX11DepthBuffer;

	// Create the device and device context.
	UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevel;
	RJE_CHECK_FOR_SUCCESS( D3D11CreateDevice(	0,					// default adapter
												md3dDriverType,
												0,					// no software device
												createDeviceFlags,
												0, 0,				// default feature level array
												D3D11_SDK_VERSION,
												&mDX11Device->md3dDevice,
												&featureLevel,
												&mDX11Device->md3dImmediateContext));

	if( featureLevel != D3D_FEATURE_LEVEL_11_0 )
	{
		RJE_MESSAGE_BOX(0, L"Direct3D Feature Level 11 unsupported.", 0, 0);
		return;
	}

	// Check 4X MSAA quality support for our back buffer format.
	// All Direct3D 11 capable devices support 4X MSAA for all render 
	// target formats, so we only need to check quality support.
	RJE_CHECK_FOR_SUCCESS(mDX11Device->md3dDevice->CheckMultisampleQualityLevels( DXGI_FORMAT_R8G8B8A8_UNORM, 4, &RJE_GLOBALS::g4xMsaaQuality));
	RJE_ASSERT( RJE_GLOBALS::g4xMsaaQuality > 0 );

	// Fill out a DXGI_SWAP_CHAIN_DESC to describe our swap chain.
	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width						= windowWidth;
	sd.BufferDesc.Height					= windowHeight;
	sd.BufferDesc.RefreshRate.Numerator		= 60;
	sd.BufferDesc.RefreshRate.Denominator	= 1;
	sd.BufferDesc.Format					= DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.ScanlineOrdering			= DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling					= DXGI_MODE_SCALING_UNSPECIFIED;

	// Use 4X MSAA? 
	if( RJE_GLOBALS::gUse4xMsaa )
	{
		sd.SampleDesc.Count   = 4;
		sd.SampleDesc.Quality = (RJE_GLOBALS::g4xMsaaQuality)-1;
	}
	else // No MSAA
	{
		sd.SampleDesc.Count   = 1;
		sd.SampleDesc.Quality = 0;
	}

	sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount  = 1;
	sd.OutputWindow = hMainWnd;
	sd.Windowed     = true;
	sd.SwapEffect   = DXGI_SWAP_EFFECT_DISCARD;
	sd.Flags        = 0;

	// To correctly create the swap chain, we must use the IDXGIFactory that was
	// used to create the device.  If we tried to use a different IDXGIFactory instance
	// (by calling CreateDXGIFactory), we get an error: "IDXGIFactory::CreateSwapChain: 
	// This function is being called with a device from a different IDXGIFactory."
	IDXGIDevice* dxgiDevice = nullptr;
	RJE_CHECK_FOR_SUCCESS(mDX11Device->md3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice));

	IDXGIAdapter* dxgiAdapter = nullptr;
	RJE_CHECK_FOR_SUCCESS(dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter));

	IDXGIFactory* dxgiFactory = nullptr;
	RJE_CHECK_FOR_SUCCESS(dxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&dxgiFactory));

	RJE_CHECK_FOR_SUCCESS(dxgiFactory->CreateSwapChain(mDX11Device->md3dDevice, &sd, &mSwapChain));

	RJE_CHECK_FOR_SUCCESS(dxgiFactory->MakeWindowAssociation(hMainWnd, DXGI_MWA_NO_WINDOW_CHANGES));

	RJE_SAFE_RELEASE(dxgiDevice);
	RJE_SAFE_RELEASE(dxgiAdapter);
	RJE_SAFE_RELEASE(dxgiFactory);

	// The remaining steps that need to be carried out for d3d creation
	// also need to be executed every time the window is resized.  So
	// just call the ResizeWindow method here to avoid code duplication.
	ResizeWindow(windowWidth, windowHeight);

	BuildGeometryBuffers();
	BuildFX();
	BuildVertexLayout();

	D3D11_RASTERIZER_DESC RasterizerDesc;
	ZeroMemory(&RasterizerDesc, sizeof(D3D11_RASTERIZER_DESC));
	RasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
	RasterizerDesc.CullMode = D3D11_CULL_NONE;
	RasterizerDesc.FrontCounterClockwise = false;
	RasterizerDesc.DepthClipEnable = true;

	RJE_CHECK_FOR_SUCCESS(mDX11Device->md3dDevice->CreateRasterizerState(&RasterizerDesc, &mRasterizerState));
}

//////////////////////////////////////////////////////////////////////////
void DX11Wrapper::BuildGeometryBuffers()
{
	GeometryGenerator::MeshData grid;

	GeometryGenerator geoGen;
	geoGen.CreateGrid(160.0f, 160.0f, 250, 250, grid);

	mIndexCount = (UINT) grid.Indices.size();

	//
	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  In addition, color the vertices based on their height so we have
	// sandy looking beaches, grassy low hills, and snow mountain peaks.
	//

	std::vector<Vertex> vertices(grid.Vertices.size());
	for(size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		XMFLOAT3 p = grid.Vertices[i].Position;

		p.y = GetHeight(p.x, p.z);

		vertices[i].Pos   = p;

		// Color the vertex based on its height.
		if( p.y < -10.0f )
		{
			// Sandy beach color.
			vertices[i].Color = XMFLOAT4(1.0f, 0.96f, 0.62f, 1.0f);
		}
		else if( p.y < 5.0f )
		{
			// Light yellow-green.
			vertices[i].Color = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
		}
		else if( p.y < 12.0f )
		{
			// Dark yellow-green.
			vertices[i].Color = XMFLOAT4(0.1f, 0.48f, 0.19f, 1.0f);
		}
		else if( p.y < 20.0f )
		{
			// Dark brown.
			vertices[i].Color = XMFLOAT4(0.45f, 0.39f, 0.34f, 1.0f);
		}
		else
		{
			// White snow.
			vertices[i].Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = (UINT) (sizeof(Vertex) * grid.Vertices.size());
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];
	RJE_CHECK_FOR_SUCCESS(mDX11Device->md3dDevice->CreateBuffer(&vbd, &vinitData, &mVertexBuffer));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(UINT) * mIndexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &grid.Indices[0];
	RJE_CHECK_FOR_SUCCESS(mDX11Device->md3dDevice->CreateBuffer(&ibd, &iinitData, &mIndexBuffer));
}

//////////////////////////////////////////////////////////////////////////
void DX11Wrapper::BuildFX()
{
#if COMPILE_SHADER == COMPILE_SHADER_RUNTIME
	DWORD shaderFlags = 0;
#if defined( DEBUG ) || defined( _DEBUG )
	shaderFlags |= D3D10_SHADER_DEBUG;
	shaderFlags |= D3D10_SHADER_SKIP_OPTIMIZATION;
#endif

	ID3D10Blob* compiledShader = nullptr;
	ID3D10Blob* compilationMsgs = nullptr;
	RJE_CHECK_FOR_SUCCESS( D3DCompileFromFile(	L"..\\..\\RamJamEngine\\data\\shaders\\HLSL\\color.fx",
												0,
												0,
												0,
												"fx_5_0",
												shaderFlags,
												0,
												&compiledShader,
												&compilationMsgs));

	// compilationMsgs can store errors or warnings.
	if( compilationMsgs != 0 )
	{
		MessageBoxA(0, (char*)compilationMsgs->GetBufferPointer(), 0, 0);
		RJE_SAFE_RELEASE(compilationMsgs);
	}
	
	RJE_CHECK_FOR_SUCCESS(D3DX11CreateEffectFromMemory(	compiledShader->GetBufferPointer(),
														compiledShader->GetBufferSize(),
														0,
														mDX11Device->md3dDevice,
														&mFX));

	// Done with compiled shader.
	RJE_SAFE_RELEASE(compiledShader);
#else
	std::ifstream shader("..\\..\\RamJamEngine\\data\\shaders\\HLSL\\color.cso", std::ios::binary);

	shader.seekg(0, std::ios_base::end);
	int size = (int)shader.tellg();
	shader.seekg(0, std::ios_base::beg);
	std::vector<char> compiledShader(size);

	shader.read(&compiledShader[0], size);
	shader.close();

	RJE_CHECK_FOR_SUCCESS(D3DX11CreateEffectFromMemory(	&compiledShader[0],
														size,
														0,
														mDX11Device->md3dDevice,
														&mFX));
#endif


	mTech = mFX->GetTechniqueByName("ColorTech");
	mfxWorldViewProj = mFX->GetVariableByName("gWorldViewProj")->AsMatrix();
}

//////////////////////////////////////////////////////////////////////////
void DX11Wrapper::BuildVertexLayout()
{
	HRESULT hr;
	BOOL isValid = false;
	// Create the vertex input layout.
	D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};

	// Create the input layout
	D3DX11_PASS_DESC passDesc;
	ID3DX11EffectPass* pass;
	pass = mTech->GetPassByIndex(0);
	RJE_ASSERT(pass->IsValid());
	hr = pass->GetDesc(&passDesc);
	RJE_CHECK_FOR_SUCCESS(hr);
	RJE_CHECK_FOR_SUCCESS(mDX11Device->md3dDevice->CreateInputLayout(	vertexDesc,
																		2,
																		passDesc.pIAInputSignature,
																		passDesc.IAInputSignatureSize,
																		&mInputLayout));
}

//////////////////////////////////////////////////////////////////////////
void DX11Wrapper::UpdateScene( float dt, float theta, float phi, float radius )
{
	// Convert Spherical to Cartesian coordinates.
	float x = radius*sinf(phi)*cosf(theta);
	float z = radius*sinf(phi)*sinf(theta);
	float y = radius*cosf(phi);

	// Build the view matrix.
	DirectX::XMVECTOR pos    = DirectX::XMVectorSet(x, y, z, 1.0f);
	DirectX::XMVECTOR target = DirectX::XMVectorZero();
	DirectX::XMVECTOR up     = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	DirectX::XMMATRIX V = DirectX::XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, V);
}

//////////////////////////////////////////////////////////////////////////
void DX11Wrapper::DrawScene()
{
	RJE_ASSERT(mDX11Device->md3dImmediateContext);
	RJE_ASSERT(mSwapChain);

	mDX11Device->md3dImmediateContext->ClearRenderTargetView(mRenderTargetView, DirectX::Colors::LightSteelBlue);
	mDX11Device->md3dImmediateContext->ClearDepthStencilView(mDX11DepthBuffer->mDepthStencilView, D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, 1.0f, 0);

	mDX11Device->md3dImmediateContext->IASetInputLayout(mInputLayout);
	mDX11Device->md3dImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	mDX11Device->md3dImmediateContext->RSSetState(mRasterizerState);

	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	mDX11Device->md3dImmediateContext->IASetVertexBuffers(0, 1, &mVertexBuffer, &stride, &offset);
	mDX11Device->md3dImmediateContext->IASetIndexBuffer(mIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

	// Set constants
	DirectX::XMMATRIX world = XMLoadFloat4x4(&mWorld);
	DirectX::XMMATRIX view  = XMLoadFloat4x4(&mView);
	DirectX::XMMATRIX proj  = XMLoadFloat4x4(&mProj);
	DirectX::XMMATRIX worldViewProj = world*view*proj;

	mfxWorldViewProj->SetMatrix(reinterpret_cast<float*>(&worldViewProj));

	D3DX11_TECHNIQUE_DESC techDesc;
	mTech->GetDesc( &techDesc );
	for(UINT p = 0; p < techDesc.Passes; ++p)
	{
		mTech->GetPassByIndex(p)->Apply(0, mDX11Device->md3dImmediateContext);
		mDX11Device->md3dImmediateContext->DrawIndexed(mIndexCount, 0, 0);
	}

	if (RJE_GLOBALS::gVsyncEnabled)
	{
		RJE_CHECK_FOR_SUCCESS(mSwapChain->Present(1, 0));
	}
	else
	{
		RJE_CHECK_FOR_SUCCESS(mSwapChain->Present(0, 0));
	}
}

//////////////////////////////////////////////////////////////////////////
void DX11Wrapper::Shutdown()
{
	RJE_SAFE_RELEASE(mVertexBuffer);
	RJE_SAFE_RELEASE(mIndexBuffer);
	RJE_SAFE_RELEASE(mFX);
	RJE_SAFE_RELEASE(mInputLayout);
	RJE_SAFE_RELEASE(mRasterizerState);

	RJE_SAFE_RELEASE(mRenderTargetView);
	mDX11DepthBuffer->Release();
	RJE_SAFE_RELEASE(mSwapChain);
	mDX11Device->Release();

	RJE_SAFE_DELETE(mDX11DepthBuffer);
	RJE_SAFE_DELETE(mDX11Device);
}

//////////////////////////////////////////////////////////////////////////
void DX11Wrapper::ResizeWindow(int newSizeWidth, int newSizeHeight)
{
	RJE_ASSERT(mDX11Device->md3dImmediateContext);
	RJE_ASSERT(mDX11Device->md3dDevice);
	RJE_ASSERT(mSwapChain);

	// Release the old views, as they hold references to the buffers we
	// will be destroying.  Also release the old depth/stencil buffer.
	RJE_SAFE_RELEASE(mRenderTargetView);
	RJE_SAFE_RELEASE(mDX11DepthBuffer->mDepthStencilView);
	RJE_SAFE_RELEASE(mDX11DepthBuffer->mDepthStencilBuffer);

	// Resize the swap chain and recreate the render target view.
	RJE_CHECK_FOR_SUCCESS(mSwapChain->ResizeBuffers(1, newSizeWidth, newSizeHeight, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH))
	ID3D11Texture2D* backBuffer;
	RJE_CHECK_FOR_SUCCESS(mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)));
	RJE_CHECK_FOR_SUCCESS(mDX11Device->md3dDevice->CreateRenderTargetView(backBuffer, 0, &mRenderTargetView));
	RJE_SAFE_RELEASE(backBuffer);

	// Create the depth/stencil buffer and view.
	D3D11_TEXTURE2D_DESC depthStencilDesc;

	depthStencilDesc.Width     = newSizeWidth;
	depthStencilDesc.Height    = newSizeHeight;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.ArraySize = 1;
	depthStencilDesc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// Use 4X MSAA? --must match swap chain MSAA values.
	if( RJE_GLOBALS::gUse4xMsaa )
	{
		depthStencilDesc.SampleDesc.Count   = 4;
		depthStencilDesc.SampleDesc.Quality = (RJE_GLOBALS::g4xMsaaQuality)-1;
	}
	else // No MSAA
	{
		depthStencilDesc.SampleDesc.Count   = 1;
		depthStencilDesc.SampleDesc.Quality = 0;
	}

	depthStencilDesc.Usage          = D3D11_USAGE_DEFAULT;
	depthStencilDesc.BindFlags      = D3D11_BIND_DEPTH_STENCIL;
	depthStencilDesc.CPUAccessFlags = 0; 
	depthStencilDesc.MiscFlags      = 0;

	RJE_CHECK_FOR_SUCCESS(mDX11Device->md3dDevice->CreateTexture2D(&depthStencilDesc, 0, &(mDX11DepthBuffer->mDepthStencilBuffer)));
	RJE_CHECK_FOR_SUCCESS(mDX11Device->md3dDevice->CreateDepthStencilView(mDX11DepthBuffer->mDepthStencilBuffer, 0, &(mDX11DepthBuffer->mDepthStencilView)));


	// Bind the render target view and depth/stencil view to the pipeline.
	mDX11Device->md3dImmediateContext->OMSetRenderTargets(1, &mRenderTargetView, mDX11DepthBuffer->mDepthStencilView);

	// Set the viewport transform.
	mScreenViewport.TopLeftX = 0;
	mScreenViewport.TopLeftY = 0;
	mScreenViewport.Width    = static_cast<float>(newSizeWidth);
	mScreenViewport.Height   = static_cast<float>(newSizeHeight);
	mScreenViewport.MinDepth = 0.0f;
	mScreenViewport.MaxDepth = 1.0f;

	mDX11Device->md3dImmediateContext->RSSetViewports(1, &mScreenViewport);

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	DirectX::XMMATRIX P = DirectX::XMMatrixPerspectiveFovLH(0.25f*RJE::Math::Pi, static_cast<float>(newSizeWidth) / newSizeHeight, 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

float DX11Wrapper::GetHeight(float x, float z) const
{
	return 0.3f*( z*sinf(0.1f*x) + x*cosf(0.1f*z) );
}