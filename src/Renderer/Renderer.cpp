#include "DXRHelper.h"
#include "nv_helpers_dx12/BottomLevelASGenerator.h"
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"
#include "manipulator.h"
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "Renderer.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx12.h"

const int gNumFrameResources = 3;
const int gNumRayTypes = 3;

// Simple free list based allocator
struct ExampleDescriptorHeapAllocator
{
	ID3D12DescriptorHeap* Heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
	D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
	UINT                        HeapHandleIncrement;
	ImVector<int>               FreeIndices;

	void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
	{
		IM_ASSERT(Heap == nullptr && FreeIndices.empty());
		Heap = heap;
		D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
		HeapType = desc.Type;
		HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
		HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
		HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
		FreeIndices.reserve((int)desc.NumDescriptors);
		for (int n = desc.NumDescriptors; n > 0; n--)
			FreeIndices.push_back(n - 1);
	}
	void Destroy()
	{
		Heap = nullptr;
		FreeIndices.clear();
	}
	void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
	{
		IM_ASSERT(FreeIndices.Size > 0);
		int idx = FreeIndices.back();
		FreeIndices.pop_back();
		out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
		out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
	}
	void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
	{
		int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
		int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
		IM_ASSERT(cpu_idx == gpu_idx);
		FreeIndices.push_back(cpu_idx);
	}
};

static ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;

Renderer::Renderer(HWND& windowHandle, UINT width, UINT height)
	:m_Hwnd(windowHandle),
	m_ClientWidth(width),
	m_ClientHeight(height)
{
	m_Hwnd = windowHandle;
	InitializeD3D12(m_Hwnd);
}

Renderer::~Renderer()
{
	m_Device->Release();
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

bool Renderer::InitializeD3D12(HWND& windowHandle)
{
	//nv_helpers_dx12::Manipulator::Singleton().setWindowSize(m_ClientWidth, m_ClientHeight);
	//nv_helpers_dx12::Manipulator::Singleton().setLookat(glm::vec3(0.0f, 1.0f, -27.0f), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

#if defined(DEBUG) || defined(_DEBUG)
	//CreateDebugController();
#endif
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_DxgiFactory)));

	CreateDevice();
	CheckRaytracingSupport();

	CreateFence();

	GetDescriptorSizes();

	CheckMSAAQuality();

	CreateCommandObjects();

	m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	CreateSwapChain(windowHandle);

	CreateRtvAndDsvDescriptorHeaps();
	CreateGBufferPassRTVResources();
	CreateRenderTargetView();

	CreateDepthStencilView();

	CreateRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildSkullGeometry();
	CreatePlaneVB();
	BuildMaterials();
	BuildRenderItems();

	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = m_ClientWidth;
	vp.Height = m_ClientHeight;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 0.0f;

	BuildFrameResources();
	m_CurrentFrameResource = 0;
	m_CurrentFrameResourceIndex = (m_CurrentFrameResourceIndex + 1) % gNumFrameResources;
	m_CurrentFrameResource = m_FrameResources[m_CurrentFrameResourceIndex].get();

	BuildPSOs();
	CreateCameraBuffer();
	UpdateMaterialCBs();

	CreateAccelerationStructures();
	CreateRaytracingPipeline();
	CreatePerInstanceBuffers();
	CreateGlobalConstantBuffer();
	CreatePostProcessConstantBuffer();
	CreateAreaLightConstantBuffer();
	CreateRaytracingOutputBuffer();
	CreateShaderResourceHeap();
	CreateShaderBindingTable();
	CreateImGuiDescriptorHeap();
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	//	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // IF using Docking Branch

		// Setup Platform/Renderer backends
	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = m_Device.Get();
	init_info.CommandQueue = m_CommandQueue.Get();
	init_info.NumFramesInFlight = SwapChainBufferCount;
	init_info.RTVFormat = m_BackBufferFormat; // Or your render target format.

	//Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
	//The example_win32_directx12/main.cpp application include a simple free-list based allocator.
	g_pd3dSrvDescHeapAlloc.Create(m_Device.Get(), m_ImGuiSrvHeap.Get());
	init_info.SrvDescriptorHeap = m_ImGuiSrvHeap.Get();
	init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) { return g_pd3dSrvDescHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle); };
	init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) { return g_pd3dSrvDescHeapAlloc.Free(cpu_handle, gpu_handle);  };

	imguiCpuStart = m_ImGuiSrvHeap->GetCPUDescriptorHandleForHeapStart();
	imguiGpuStart = m_ImGuiSrvHeap->GetGPUDescriptorHandleForHeapStart();

	ImGui_ImplDX12_Init(&init_info);
	//ImGui_ImplDX12_Init(&init_info);

	//	ImGui::StyleColorsDark();
		//ImGui::StyleColorsLight();

		// Setup scaling


	ImGui_ImplWin32_Init(m_Hwnd);

	ThrowIfFailed(m_CommandList->Close());
	ID3D12CommandList* cmdLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	return true;
}

static inline UINT64 Align(UINT64 v, UINT64 alignment) {
	return (v + (alignment - 1)) & ~(alignment - 1);
}

void Renderer::Update(float dt, Camera& cam)
{
	m_EyePos = cam.GetPosition3f();
	//	cam.LookAt(m_EyePos, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
	cam.UpdateViewMatrix();
	XMStoreFloat4x4(&m_View, cam.GetView());
	XMStoreFloat4x4(&m_Proj, cam.GetProj());

	XMVECTOR At = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
	XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	if (m_CurrentFrameResource->Fence != 0 && m_Fence->GetCompletedValue() < m_CurrentFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	m_AnimationCounter++;
	//m_Instances[1].second = XMMatrixRotationAxis({ 0.0f, 1.0f, 0.0f }, static_cast<float>(m_AnimationCounter) / 1000.0f);
	//m_Instances[2].second = XMMatrixRotationAxis({ 0.0f, 1.0f, 0.0f }, static_cast<float>(m_AnimationCounter) / -1000.0f) * XMMatrixTranslation(10.0f, -10.0f, 0.0f);;
	//m_Instances[3].second = XMMatrixRotationAxis({ 0.0f, 1.0f, 0.0f }, static_cast<float>(m_AnimationCounter) / -1000.0f) * XMMatrixTranslation(-10.0f, -10.0f, 0.0f);;

	//	UpdateCameraBuffer();
	UpdateObjectCBs();
	UpdateMainPassCB();
	UpdateMaterialCBs();
}

void Renderer::Draw(bool useRaster)
{

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	showWindow = true;
	ImGui::ShowDemoWindow(&showWindow);
	ImGui::Render();



	auto cmdListAlloc = m_CurrentFrameResource->CmdListAlloc;

	ThrowIfFailed(cmdListAlloc->Reset());

	if (m_IsWireframe)
	{
		ThrowIfFailed(m_CommandList->Reset(cmdListAlloc.Get(), m_PipelineStateObjects["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(m_CommandList->Reset(cmdListAlloc.Get(), m_PipelineStateObjects["opaque"].Get()));
	}

	D3D12_RESOURCE_BARRIER pBarriers[2] = {};
	//	pBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferAlbedoMetal.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	//	pBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferNormalRough.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	//	m_CommandList->ResourceBarrier(2, pBarriers);
	pBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferAlbedoMetal.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	pBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferNormalRough.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_CommandList->ResourceBarrier(2, pBarriers);

	m_CommandList->RSSetViewports(1, &vp);

	m_ScissorRect = { 0, 0, static_cast<long>(m_ClientWidth), static_cast<long>(m_ClientHeight) };
	m_CommandList->RSSetScissorRects(1, &m_ScissorRect);


	//pBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferAlbedoMetal.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	//pBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferNormalRough.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	//m_CommandList->ResourceBarrier(2, pBarriers);

	D3D12_CPU_DESCRIPTOR_HANDLE gbufferRtvs[2] =
	{
		m_GBufferHandles[0],
		m_GBufferHandles[1]
	};

	m_CommandList->OMSetRenderTargets(2, gbufferRtvs, false, &DepthStencilView());



	std::vector<ID3D12DescriptorHeap*> heaps = { m_SrvUavHeap.Get() };
	m_CommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

	m_CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	FLOAT color[4] = { 0.22f, 0.33f, 0.44f, 0.f };

	m_CommandList->ClearRenderTargetView(gbufferRtvs[0], color, 0, nullptr);
	m_CommandList->ClearRenderTargetView(gbufferRtvs[1], color, 0, nullptr);
	m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

	auto passCB = m_CurrentFrameResource->PassCB->Resource();
	m_CommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
	m_CommandList->SetGraphicsRootConstantBufferView(3, m_CameraBuffer->GetGPUVirtualAddress());

	DrawRenderItems(m_CommandList.Get(), m_OpaqueRenderGeometry);
	//	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_CommandList.Get());

	//pBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferAlbedoMetal.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	//pBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferNormalRough.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	//m_CommandList->ResourceBarrier(2, pBarriers);



	pBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferAlbedoMetal.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	pBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferNormalRough.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_CommandList->ResourceBarrier(2, pBarriers);


	CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(m_DepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_CommandList->ResourceBarrier(1, &transition);


	//	CreateTopLevelAS(m_Instances, true);

			//	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST));

				//	m_CommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());


	transition = CD3DX12_RESOURCE_BARRIER::Transition(m_OutputResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	m_CommandList->ResourceBarrier(1, &transition);


	heaps = { m_SrvUavHeap.Get(), m_SamplerHeap.Get() };
	m_CommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

	D3D12_DISPATCH_RAYS_DESC desc = {};

	UINT64 rayGenerationSectionSizeInBytes = m_SbtHelper.GetRayGenSectionSize();
	desc.RayGenerationShaderRecord.StartAddress = m_SbtStorage->GetGPUVirtualAddress();
	desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;

	UINT64 missSectionSizeInBytes = m_SbtHelper.GetMissSectionSize();

	desc.MissShaderTable.StartAddress = Align(m_SbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
	desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
	desc.MissShaderTable.StrideInBytes = Align(m_SbtHelper.GetMissEntrySize(), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

	UINT64 hitGroupsSectionSize = m_SbtHelper.GetHitGroupSectionSize();
	desc.HitGroupTable.StartAddress = Align(m_SbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes + missSectionSizeInBytes, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
	desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
	desc.HitGroupTable.StrideInBytes = Align(m_SbtHelper.GetHitGroupEntrySize(), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

	desc.Width = m_ClientWidth;
	desc.Height = m_ClientHeight;
	desc.Depth = 1;

	m_CommandList->SetPipelineState1(m_RtStateObject.Get());

	m_CommandList->DispatchRays(&desc);


	transition = CD3DX12_RESOURCE_BARRIER::Transition(m_OutputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
	m_CommandList->ResourceBarrier(1, &transition);

	transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
	m_CommandList->ResourceBarrier(1, &transition);

	m_CommandList->CopyResource(CurrentBackBuffer(), m_OutputResource.Get());

	//	rtvHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE backbufferRtv =
		m_BackBufferHandles[m_CurrentBackBuffer];

	//m_CommandList->OMSetRenderTargets(1, &backbufferRtv, true, &DepthStencilView());

	transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	m_CommandList->ResourceBarrier(1, &transition);


	transition = CD3DX12_RESOURCE_BARRIER::Transition(m_DepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_CommandList->ResourceBarrier(1, &transition);


	//	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	//	pBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferAlbedoMetal.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PRESENT);
	//	pBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferNormalRough.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PRESENT);
	//	m_CommandList->ResourceBarrier(2, pBarriers);

	//	pBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferAlbedoMetal.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COMMON);
	//	pBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_GBufferNormalRough.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COMMON);
	//	m_CommandList->ResourceBarrier(2, pBarriers);

	ThrowIfFailed(m_CommandList->Close());

	ID3D12CommandList* cmdLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	HRESULT hr_present = m_SwapChain->Present(0, 0);
	GetDeviceRemovedReasonString(hr_present);
	m_CurrentBackBuffer = m_SwapChain3->GetCurrentBackBufferIndex();

	m_CurrentFrameResource->Fence = ++m_CurrentFence;

	m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence);

	//FlushCommandQueue();
}

void Renderer::CreateDebugController()
{
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&m_DebugController)));
	m_DebugController->EnableDebugLayer();
}

void Renderer::CreateDevice()
{
	HRESULT hardwareResult = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&m_Device));

	if (FAILED(hardwareResult))
	{
		ThrowIfFailed(m_DxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&m_WarpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(m_WarpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Device)));
	}
}

void Renderer::CheckRaytracingSupport()
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	ThrowIfFailed(m_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)));

	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
	{
		throw std::runtime_error("Raytracing is not supported on this device.");
	}
}

void Renderer::CreateFence()
{
	ThrowIfFailed(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)));
}

void Renderer::GetDescriptorSizes()
{
	m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DsvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_CbvSrvUavDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void Renderer::CheckMSAAQuality()
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = m_BackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;

	ThrowIfFailed(m_Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels)));

	m_4xMsaaQuality = msQualityLevels.NumQualityLevels;

	assert(m_4xMsaaQuality > 0 && "Unexpected MSAA quality level.");
}

void Renderer::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

	ThrowIfFailed(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_CommandAllocator.GetAddressOf())));

	ThrowIfFailed(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocator.Get(), nullptr, IID_PPV_ARGS(m_CommandList.GetAddressOf())));

	//	m_CommandList->Close();
}

void Renderer::CreateSwapChain(HWND& windowHandle)
{
	m_SwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	swapChainDesc.BufferDesc.Width = m_ClientWidth;
	swapChainDesc.BufferDesc.Height = m_ClientHeight;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferDesc.Format = m_BackBufferFormat;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = SwapChainBufferCount;
	swapChainDesc.OutputWindow = m_Hwnd;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	ThrowIfFailed(m_DxgiFactory->CreateSwapChain(m_CommandQueue.Get(), &swapChainDesc, m_SwapChain.GetAddressOf()));
	ThrowIfFailed(m_SwapChain.As(&m_SwapChain3));

}

void Renderer::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 2;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;

	ThrowIfFailed(m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(m_RtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;

	ThrowIfFailed(m_Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(m_DsvHeap.GetAddressOf())));
}

void Renderer::CreateGBufferPassRTVResources()
{
	D3D12_RESOURCE_DESC rtvDesc = {};
	rtvDesc.Width = m_ClientWidth;
	rtvDesc.Height = m_ClientHeight;
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.DepthOrArraySize = 1;

	rtvDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
	rtvDesc.MipLevels = 1;
	rtvDesc.SampleDesc.Count = 1;
	rtvDesc.SampleDesc.Quality = 0;
	rtvDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	FLOAT colour[4] = { 0.22f, 0.33f, 0.44f, 0.f };

	D3D12_CLEAR_VALUE rtvClearCol = {};
	rtvClearCol.Color[0] = colour[0];
	rtvClearCol.Color[1] = colour[1];
	rtvClearCol.Color[2] = colour[2];
	rtvClearCol.Color[3] = colour[3];
	rtvClearCol.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	HRESULT hr = m_Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&rtvDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&rtvClearCol,
		IID_PPV_ARGS(&m_GBufferAlbedoMetal));

	hr = m_Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&rtvDesc,
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		&rtvClearCol,
		IID_PPV_ARGS(&m_GBufferNormalRough));
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::CurrentBackBufferView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_RtvHeap->GetCPUDescriptorHandleForHeapStart(), m_CurrentBackBuffer, m_RtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::DepthStencilView() const
{
	return m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void Renderer::CreateRenderTargetView()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(m_RtvHeap->GetCPUDescriptorHandleForHeapStart());

	m_BackBufferHandles.resize(2);
	m_GBufferHandles.resize(2);
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapChainBuffer[i])));

		m_Device->CreateRenderTargetView(m_SwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		m_BackBufferHandles[i] = rtvHeapHandle;

		rtvHeapHandle.ptr += m_RtvDescriptorSize;
	}

	m_Device->CreateRenderTargetView(m_GBufferAlbedoMetal.Get(), nullptr, rtvHeapHandle);
	m_GBufferHandles[0] = rtvHeapHandle;
	rtvHeapHandle.ptr += m_RtvDescriptorSize;
	m_Device->CreateRenderTargetView(m_GBufferNormalRough.Get(), nullptr, rtvHeapHandle);
	m_GBufferHandles[1] = rtvHeapHandle;
	rtvHeapHandle.ptr += m_RtvDescriptorSize;


}

void Renderer::CreateDepthStencilView()
{
	D3D12_RESOURCE_DESC depthStencilDesc = {};
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = m_ClientWidth;
	depthStencilDesc.Height = m_ClientHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 0u;
	heapProps.VisibleNodeMask = 0u;

	D3D12_CLEAR_VALUE optClear = {};
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	ThrowIfFailed(m_Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthStencilDesc, D3D12_RESOURCE_STATE_COMMON, &optClear, IID_PPV_ARGS(&m_DepthStencilBuffer)));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	m_Device->CreateDepthStencilView(m_DepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_DepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
}

void Renderer::CreateIndexBufferView(SubmeshGeometry* sg)
{
	sg->IbView = {};
	sg->IbView.BufferLocation = sg->IndexBufferGPU->GetGPUVirtualAddress();
	sg->IbView.Format = DXGI_FORMAT_R32_UINT;
	sg->IbView.SizeInBytes = sg->IndexCount * sizeof(uint32_t);

	D3D12_INDEX_BUFFER_VIEW indexBuffers[1] = { sg->IbView };
	m_CommandList->IASetIndexBuffer(indexBuffers);

}

void Renderer::CreateIndexBufferView()
{
	m_IbView.BufferLocation = m_IndexBufferGPU->GetGPUVirtualAddress();
	m_IbView.Format = DXGI_FORMAT_R16_UINT;
	m_IbView.SizeInBytes = m_IbByteSize;

	D3D12_INDEX_BUFFER_VIEW indexBuffers[1] = { m_IbView };
	m_CommandList->IASetIndexBuffer(indexBuffers);
}

void Renderer::CreateCbvDescriptorHeaps()
{
	UINT objectCount = (UINT)m_OpaqueRenderGeometry.size();

	UINT numDescriptors = (objectCount + 1) * gNumFrameResources;

	m_PassCbvOffset = objectCount * gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;

	m_Device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_CbvHeap));
}

void Renderer::CreateConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objectCount = m_OpaqueRenderGeometry.size();

	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = m_FrameResources[frameIndex]->ObjectCB->Resource();

		for (UINT i = 0; i < objectCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cBufAddress = objectCB->GetGPUVirtualAddress();

			cBufAddress += i * objCBByteSize;

			int heapIndex = frameIndex * objectCount + i;

			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_CbvHeap->GetCPUDescriptorHandleForHeapStart());

			handle.Offset(heapIndex * m_CbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cBufAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			m_Device->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = m_FrameResources[frameIndex]->PassCB->Resource();

		D3D12_GPU_VIRTUAL_ADDRESS passBufAddress = passCB->GetGPUVirtualAddress();

		int heapIndex = m_PassCbvOffset + frameIndex;

		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_CbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, m_CbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = passBufAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		m_Device->CreateConstantBufferView(&cbvDesc, handle);
	}

	//UINT camCBByteSize = d3dUtil::CalcConstantBufferByteSize(m_CameraBufferSize);

	//auto camCB = m_CameraBuffer;

	//D3D12_GPU_VIRTUAL_ADDRESS camBufAddress = camCB->GetGPUVirtualAddress();

	//int heapIndex = camBufOffset;
	//int heapIndex = camBufOffset;
	//auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_ConstHeap->GetCPUDescriptorHandleForHeapStart())
}

void Renderer::CreateRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[6];

	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsConstantBufferView(2);
	slotRootParameter[3].InitAsConstantBufferView(3);
	slotRootParameter[4].InitAsShaderResourceView(0);
	slotRootParameter[5].InitAsShaderResourceView(1);

	auto samplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(6, slotRootParameter, (UINT)samplers.size(), samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	ThrowIfFailed(m_Device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(m_RootSignature.GetAddressOf())));
}

void Renderer::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	HRESULT hr = S_OK;

	m_VsByteCode = d3dUtil::CompileShader(L"Shaders\\vertex.hlsl", nullptr, "VS", "vs_5_0");
	m_PsByteCode = d3dUtil::CompileShader(L"Shaders\\pixel.hlsl", nullptr, "PS", "ps_5_0");

	m_InputLayoutDescs =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};


}
void Renderer::BuildMaterials()
{

	auto boxMat = std::make_unique<Material>();
	boxMat->Name = "box";
	boxMat->MatCBIndex = 0;
	boxMat->DiffuseSrvHeapIndex = 0;
	boxMat->DiffuseAlbedo = XMFLOAT4(Colors::ForestGreen);
	boxMat->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	boxMat->Roughness = 0.7f;
	boxMat->metallic = 0.3f;

	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 1;
	bricks0->DiffuseSrvHeapIndex = 1;
	bricks0->DiffuseAlbedo = XMFLOAT4(Colors::Sienna);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.9f;
	bricks0->metallic = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 2;
	stone0->DiffuseSrvHeapIndex = 2;
	stone0->DiffuseAlbedo = XMFLOAT4(Colors::Crimson);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.9f;
	stone0->metallic = 0.1f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 3;
	skullMat->DiffuseSrvHeapIndex = 3;
	skullMat->DiffuseAlbedo = XMFLOAT4(Colors::BlanchedAlmond);
	skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	skullMat->Roughness = 0.7f;
	skullMat->Ior = 1.5f;
	skullMat->IsReflective = true;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 4;
	tile0->DiffuseSrvHeapIndex = 4;
	tile0->DiffuseAlbedo = XMFLOAT4(Colors::Aquamarine);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.3f;
	tile0->metallic = 0.7f;


	auto sphereMat = std::make_unique<Material>();
	sphereMat->Name = "sphere";
	sphereMat->MatCBIndex = 5;
	sphereMat->DiffuseSrvHeapIndex = 5;
	sphereMat->DiffuseAlbedo = XMFLOAT4(Colors::Violet);
	sphereMat->FresnelR0 = XMFLOAT3(0.06f, 0.06f, 0.06f);
	sphereMat->Roughness = 0.85f;
	sphereMat->metallic = 0.2f;
	sphereMat->Ior = 1.5f;
	sphereMat->IsReflective = true;

	m_Materials["box"] = std::move(boxMat);
	//	m_FrameResources[m_CurrentFrameResourceIndex]->matCount++;
	m_Materials["bricks0"] = std::move(bricks0);
	//	m_FrameResources[m_CurrentFrameResourceIndex]->matCount++;
	m_Materials["stone0"] = std::move(stone0);
	//	m_FrameResources[m_CurrentFrameResourceIndex]->matCount++;
	m_Materials["tile0"] = std::move(tile0);
	//	m_FrameResources[m_CurrentFrameResourceIndex]->matCount++;
	m_Materials["skullMat"] = std::move(skullMat);
	//	m_FrameResources[m_CurrentFrameResourceIndex]->matCount++;
	m_Materials["sphere"] = std::move(sphereMat);
	//	m_FrameResources[m_CurrentFrameResourceIndex]->matCount++;
}
void Renderer::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(10.5f, 10.5f, 10.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	// Define the SubmeshGeometry that cover different 
	// regions of the vertex/index buffers.

	boxSubmesh = new SubmeshGeometry();

	boxSubmesh->IndexCount = (UINT64)box.Indices32.size();
	boxSubmesh->StartIndexLocation = boxIndexOffset;
	boxSubmesh->BaseVertexLocation = boxVertexOffset;
	boxSubmesh->VertexCount = (UINT64)box.Vertices.size();

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;
	gridSubmesh.VertexCount = (UINT)grid.Vertices.size();

	//SubmeshGeometry sphereSubmesh;
	//sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	//sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	//sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
	//sphereSubmesh.VertexCount = (UINT)sphere.Vertices.size();

	sphereSubmesh = new SubmeshGeometry();

	sphereSubmesh->IndexCount = (UINT64)sphere.Indices32.size();
	sphereSubmesh->StartIndexLocation = 0;
	sphereSubmesh->BaseVertexLocation = 0;
	sphereSubmesh->VertexCount = (UINT64)sphere.Vertices.size();

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;
	cylinderSubmesh.VertexCount = (UINT)cylinder.Vertices.size();

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);
	std::vector<Vertex> boxVertices(box.Vertices.size());
	std::vector<Vertex> gridVertices(grid.Vertices.size());
	std::vector<Vertex> sphereVertices(sphere.Vertices.size());
	std::vector<Vertex> cylinderVertices(cylinder.Vertices.size());

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		boxVertices[k].Pos = box.Vertices[i].Position;
		boxVertices[k].Normal = box.Vertices[i].Normal;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
	}
	k = 0;
	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		sphereVertices[k].Pos = sphere.Vertices[i].Position;
		sphereVertices[k].Normal = sphere.Vertices[i].Normal;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	boxSubmesh->VertexByteStride = sizeof(Vertex);
	boxSubmesh->BaseVertexLocation = 0;
	boxSubmesh->VertexBufferByteSize = (UINT)boxVertices.size() * sizeof(Vertex);
	boxSubmesh->IndexBufferByteSize = box.Indices32.size() * sizeof(uint32_t);
	boxSubmesh->IndexFormat = DXGI_FORMAT_R32_UINT;
	boxSubmesh->InstanceCount = 1;

	ThrowIfFailed(D3DCreateBlob(boxSubmesh->VertexBufferByteSize, &boxSubmesh->VertexBufferCPU));
	CopyMemory(boxSubmesh->VertexBufferCPU->GetBufferPointer(), boxVertices.data(), boxSubmesh->VertexBufferByteSize);

	ThrowIfFailed(D3DCreateBlob(boxSubmesh->IndexBufferByteSize, &boxSubmesh->IndexBufferCPU));
	CopyMemory(boxSubmesh->IndexBufferCPU->GetBufferPointer(), box.Indices32.data(), boxSubmesh->IndexBufferByteSize);

	boxSubmesh->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), boxVertices.data(), boxSubmesh->VertexBufferByteSize, boxSubmesh->VertexBufferUploader);

	boxSubmesh->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), box.Indices32.data(), boxSubmesh->IndexBufferByteSize, boxSubmesh->IndexBufferUploader);
	//	boxSubmesh->Material = m_Materials["box"].get();
		//XMMATRIX newWorld; 
			//XMStoreMatrix(&newWorld, MathHelper::Identity4x4());
	boxSubmesh->ObjCBIndex = 0;

	boxSubmesh->World.push_back(XMMatrixScaling(30.0f, 1.0f, 30.0f) * XMMatrixTranslation(0.0f, -15.0f, 0.0f));
	boxSubmesh->InstanceOffset = m_InstanceOffset;
	for (UINT i = 0; i < boxSubmesh->InstanceCount; i++)
	{
		InstanceData inst;
		inst.InstanceID = i;
		inst.MaterialIndex = boxSubmesh->ObjCBIndex;
		inst.World = boxSubmesh->World[i];

		m_InstanceData.push_back(inst);
		boxSubmesh->InstanceData.push_back(inst);
		m_InstanceOffset++;
	}

	m_RenderGeometry.push_back(boxSubmesh);

	sphereSubmesh->VertexByteStride = sizeof(Vertex);
	sphereSubmesh->BaseVertexLocation = 0;
	sphereSubmesh->VertexBufferByteSize = (UINT)sphereVertices.size() * sizeof(Vertex);
	sphereSubmesh->IndexBufferByteSize = sphere.Indices32.size() * sizeof(uint32_t);
	sphereSubmesh->IndexFormat = DXGI_FORMAT_R32_UINT;
	sphereSubmesh->InstanceCount = 1;
	sphereSubmesh->ObjCBIndex = 1;
	//	sphereSubmesh->Material->MatCBIndex = 1;
	sphereSubmesh->InstanceOffset = m_InstanceOffset;


	ThrowIfFailed(D3DCreateBlob(sphereSubmesh->VertexBufferByteSize, &sphereSubmesh->VertexBufferCPU));
	CopyMemory(sphereSubmesh->VertexBufferCPU->GetBufferPointer(), sphereVertices.data(), sphereSubmesh->VertexBufferByteSize);

	ThrowIfFailed(D3DCreateBlob(sphereSubmesh->IndexBufferByteSize, &sphereSubmesh->IndexBufferCPU));
	CopyMemory(sphereSubmesh->IndexBufferCPU->GetBufferPointer(), sphere.Indices32.data(), sphereSubmesh->IndexBufferByteSize);

	sphereSubmesh->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), sphereVertices.data(), sphereSubmesh->VertexBufferByteSize, sphereSubmesh->VertexBufferUploader);

	sphereSubmesh->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), sphere.Indices32.data(), sphereSubmesh->IndexBufferByteSize, sphereSubmesh->IndexBufferUploader);
	sphereSubmesh->World.push_back(XMMatrixScaling(5.0f, 5.0f, 5.0f) * XMMatrixTranslation(-14.0f, 4.0f, -4.0f));

	for (UINT i = 0; i < sphereSubmesh->InstanceCount; i++)
	{
		InstanceData inst;
		inst.InstanceID = i + m_InstanceOffset;
		inst.World = sphereSubmesh->World[i];
		inst.MaterialIndex = sphereSubmesh->ObjCBIndex;
		sphereSubmesh->InstanceData.push_back(inst);

		m_InstanceData.push_back(inst);
		m_InstanceOffset++;
	}

	m_RenderGeometry.push_back(sphereSubmesh);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	//geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	//geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	m_vertexCount += totalVertexCount;

	//geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	//geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	m_Geometries[geo->Name] = std::move(geo);
}

void Renderer::BuildSkullGeometry()
{
	std::ifstream fin("Models/skull.txt");

	if (!fin)
	{
		MessageBox(0, L"Models/skull.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
	}

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);
	for (UINT i = 0; i < tcount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
	skullUB = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);


	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	skullSubmesh = new SubmeshGeometry();
	skullSubmesh->VertexByteStride = sizeof(Vertex);
	skullSubmesh->VertexCount = (UINT64)vertices.size();
	skullSubmesh->BaseVertexLocation = 0;
	skullSubmesh->VertexBufferByteSize = (UINT64)vertices.size() * sizeof(Vertex);
	skullSubmesh->IndexBufferByteSize = indices.size() * sizeof(uint32_t);
	skullSubmesh->IndexFormat = DXGI_FORMAT_R32_UINT;
	skullSubmesh->IndexCount = (UINT64)indices.size();
	skullSubmesh->ObjCBIndex = 2;
	//	skullSubmesh->Material->MatCBIndex = 2;

	ThrowIfFailed(D3DCreateBlob(skullSubmesh->VertexBufferByteSize, &skullSubmesh->VertexBufferCPU));
	CopyMemory(skullSubmesh->VertexBufferCPU->GetBufferPointer(), vertices.data(), skullSubmesh->VertexBufferByteSize);

	ThrowIfFailed(D3DCreateBlob(skullSubmesh->IndexBufferByteSize, &skullSubmesh->IndexBufferCPU));
	CopyMemory(skullSubmesh->IndexBufferCPU->GetBufferPointer(), indices.data(), skullSubmesh->IndexBufferByteSize);

	skullSubmesh->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), vertices.data(), skullSubmesh->VertexBufferByteSize, skullSubmesh->VertexBufferUploader);

	skullSubmesh->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), indices.data(), skullSubmesh->IndexBufferByteSize, skullSubmesh->IndexBufferUploader);

	skullSubmesh->InstanceCount = 4;

	skullSubmesh->World.push_back(XMMatrixTranslation(-6.0f, 10.0f, 0.0f));
	skullSubmesh->World.push_back(XMMatrixTranslation(6.0f, 20.0f, 0.0f));
	skullSubmesh->World.push_back(XMMatrixTranslation(0.0f, 13.0f, 10.0f));
	skullSubmesh->World.push_back(XMMatrixTranslation(10.0f, 11.0f, 0.0f));

	skullSubmesh->InstanceOffset = m_InstanceOffset;

	for (UINT i = 0; i < skullSubmesh->InstanceCount; i++)
	{
		InstanceData inst;
		inst.InstanceID = i + m_InstanceOffset;
		inst.MaterialIndex = skullSubmesh->ObjCBIndex;
		inst.World = skullSubmesh->World[i];
		skullSubmesh->InstanceData.push_back(inst);

		m_InstanceData.push_back(inst);
		m_InstanceOffset++;
	}

	m_RenderGeometry.push_back(skullSubmesh);


	geo->DrawArgs["skull"] = submesh;

	m_vertexCount += vertices.size();
	m_skullVertCount = vertices.size();

	m_Geometries[geo->Name] = std::move(geo);
}

void Renderer::BuildRenderItems()
{
	//auto gridRitem = std::make_unique<RenderItem>();
	//gridRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 1.0f, .0f));
	//gridRitem->ObjCBIndex = 0;
	//gridRitem->Mat = m_Materials["tile0"].get();
	//gridRitem->Geo = m_Geometries["shapeGeo"].get();
	//gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	//gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	//gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	//m_AllRenderItems.push_back(std::move(gridRitem));

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	boxRitem->ObjCBIndex = 0;
	boxRitem->Mat = m_Materials["box"].get();
	boxRitem->Geo = m_Geometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	m_AllRenderItems.push_back(std::move(boxRitem));


	auto skullRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = 2;
	skullRitem->Mat = m_Materials["skullMat"].get();
	skullRitem->Geo = m_Geometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	m_AllRenderItems.push_back(std::move(skullRitem));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	UINT objCBIndex = 3;
	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = m_Materials["bricks0"].get();
		leftCylRitem->Geo = m_Geometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = m_Materials["bricks0"].get();
		rightCylRitem->Geo = m_Geometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = m_Materials["stone0"].get();
		leftSphereRitem->Geo = m_Geometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = m_Materials["stone0"].get();
		rightSphereRitem->Geo = m_Geometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		m_AllRenderItems.push_back(std::move(leftCylRitem));
		m_AllRenderItems.push_back(std::move(rightCylRitem));
		m_AllRenderItems.push_back(std::move(leftSphereRitem));
		m_AllRenderItems.push_back(std::move(rightSphereRitem));
	}

	// All the render items are opaque.
	for (auto& e : m_RenderGeometry)
		m_OpaqueRenderGeometry.push_back(e);
}

void Renderer::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<SubmeshGeometry*>& renderGeo)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = m_CurrentFrameResource->ObjectCB->Resource();
	auto matCB = m_MaterialsGPU;
	auto matGPUCB = m_UploadCBuffer;
	auto instaGPUCB = m_InstanceBuffer;
	D3D12_GPU_VIRTUAL_ADDRESS matGPUAdrress = matGPUCB->GetGPUVirtualAddress();
	D3D12_GPU_VIRTUAL_ADDRESS instGPUAdrress = instaGPUCB->GetGPUVirtualAddress();


	for (size_t i = 0; i < renderGeo.size(); ++i)
	{
		auto* rg = renderGeo[i];
		CreateVertexBufferView(rg);
		CreateIndexBufferView(rg);
		cmdList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + (rg->ObjCBIndex * objCBByteSize);
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matGPUCB->GetGPUVirtualAddress();
		D3D12_GPU_VIRTUAL_ADDRESS instGPUAdrress = instaGPUCB->GetGPUVirtualAddress();

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(1, matCBAddress);
		cmdList->SetGraphicsRootShaderResourceView(4, matGPUAdrress);
		cmdList->SetGraphicsRootShaderResourceView(5, instGPUAdrress);
		cmdList->DrawIndexedInstanced(rg->IndexCount, rg->InstanceCount, rg->StartIndexLocation, rg->BaseVertexLocation, 0);
	}
	//	CreateVertexBufferView(boxSubmesh);
	//	CreateIndexBufferView(boxSubmesh);
	//	cmdList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	//
	//	D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + (boxSubmesh->ObjCBIndex) * objCBByteSize;
	////	D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + boxSubmesh->Material->MatCBIndex * matCBByteSize;
	//
	//	cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
	////	cmdList->SetGraphicsRootConstantBufferView(1, matCBAddress);
	//
	//	cmdList->DrawIndexedInstanced(boxSubmesh->IndexCount, boxSubmesh->InstanceCount, boxSubmesh->StartIndexLocation, boxSubmesh->BaseVertexLocation, 0);

}

void Renderer::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
	;
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = {
		m_InputLayoutDescs.data(), (UINT)m_InputLayoutDescs.size()
	};
	opaquePsoDesc.pRootSignature = m_RootSignature.Get();
	opaquePsoDesc.VS = {
		reinterpret_cast<BYTE*>(m_VsByteCode->GetBufferPointer()),
		m_VsByteCode->GetBufferSize()
	};
	opaquePsoDesc.PS = {
		reinterpret_cast<BYTE*>(m_PsByteCode->GetBufferPointer()),
		m_PsByteCode->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 3;
	opaquePsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	opaquePsoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
	opaquePsoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
	opaquePsoDesc.SampleDesc.Count = 1;
	opaquePsoDesc.SampleDesc.Quality = 0;
	opaquePsoDesc.DSVFormat = m_DepthStencilFormat;

	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["opaque"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["opaque_wireframe"])));

}
void Renderer::BuildFrameResources()
{
	for (int i = 0; i < NumFrameResources; ++i)
	{
		m_FrameResources.push_back(std::make_unique<FrameResource>(m_Device.Get(), 1, (UINT)m_AllRenderItems.size()));
	}
}
void Renderer::UpdateObjectCBs()
{
	auto currObjectCB = m_CurrentFrameResource->ObjectCB.get();

	for (auto& e : m_RenderGeometry)
	{
		std::vector<ObjectConstants> objConstants;
		objConstants.resize(m_InstanceData.size());
		if (e->NumFramesDirty > 0)
		{
			for (int i = 0; i < e->InstanceData.size(); i++)
			{

				XMStoreFloat4x4(&objConstants[i].World, XMMatrixTranspose(e->World[i]));
				XMStoreFloat4x4(&objConstants[i].InvWorld, XMMatrixInverse(&XMMatrixDeterminant(e->World[i]), e->World[i]));
				
				objConstants[i].MatIndex = e->InstanceData[i].MaterialIndex;
				objConstants[i].InstanceOffset = e->InstanceOffset;
				objConstants[i].InstanceID = i;
				objConstants[i].pad = 1;


				currObjectCB->CopyData(e->ObjCBIndex, objConstants[i]);
			}
			e->NumFramesDirty--;

		}
	}
}



void Renderer::UpdateMaterialCBs()
{
	auto currentMaterialCB = m_CurrentFrameResource->Materials.data();

	for (auto& e : m_Materials)
	{
		Material* mat = e.second.get();

		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			matConstants.Ior = mat->Ior;
			matConstants.Absorption = mat->Absorption;
			matConstants.Shininess = 1 - mat->Roughness;
			matConstants.pad = 1.0f;
			matConstants.pad1 = 1.0f;
			matConstants.metallic = mat->metallic;
			matConstants.IsReflective = mat->IsReflective;

			currentMaterialCB[mat->MatCBIndex]->CopyData(mat->MatCBIndex, matConstants);

			mat->NumFramesDirty--;
		}
	}
}
void Renderer::UpdateMainPassCB()
{
	XMMATRIX view = XMLoadFloat4x4(&m_View);
	XMMATRIX proj = XMLoadFloat4x4(&m_Proj);


	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&m_MainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&m_MainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&m_MainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&m_MainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&m_MainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&m_MainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

	m_MainPassCB.EyePosW = m_EyePos;
	m_MainPassCB.cbPerObjectPad1 = 0.5f;
	m_MainPassCB.RenderTargetSize = XMFLOAT2((float)m_ClientWidth, (float)m_ClientHeight);
	m_MainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / m_ClientWidth, 1.0f / m_ClientHeight);
	m_MainPassCB.NearZ = 1.0f;
	m_MainPassCB.FarZ = 1000.0f;
	m_MainPassCB.cbPerObjectPad2 = 0.5f;
	m_MainPassCB.cbPerObjectPad3 = 0.5f;
	m_MainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	m_MainPassCB.Lights[0].Strength = { 4.6f, 4.6f, 4.6f };
	m_MainPassCB.Lights[0].Direction = { 0.3f, -0.46f, 0.7f };
	m_MainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };

	m_MainPassCB.Lights[1].Strength = { 2.6f, 2.6f, 2.6f };
	m_MainPassCB.Lights[2].Direction = { 0.0f, 0.707f, -0.707f };
	m_MainPassCB.Lights[2].Strength = { 2.6f, 2.6f, 2.6f };

	auto currPassCB = m_CurrentFrameResource->PassCB.get();
	currPassCB->CopyData(0, m_MainPassCB);
};

void Renderer::CreateVertexBufferView()
{
	m_VbView.BufferLocation = m_VertexBufferGPU->GetGPUVirtualAddress();
	m_VbView.StrideInBytes = sizeof(Vertex);
	m_VbView.SizeInBytes = m_VbByteSize;

	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[1] = { m_VbView };
	m_CommandList->IASetVertexBuffers(0, 1, vertexBuffers);
}

void Renderer::CreateVertexBufferView(SubmeshGeometry* sg)
{
	sg->VbView.BufferLocation = sg->VertexBufferGPU->GetGPUVirtualAddress();
	sg->VbView.StrideInBytes = sizeof(Vertex);
	sg->VbView.SizeInBytes = sg->VertexCount * sg->VertexByteStride;

	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[1] = { sg->VbView };
	m_CommandList->IASetVertexBuffers(0, 1, vertexBuffers);

}

void Renderer::FlushCommandQueue()
{
	m_CurrentFence++;

	ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence));

	if (m_Fence->GetCompletedValue() < m_CurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFence, eventHandle));

		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

ID3D12Resource* Renderer::CurrentBackBuffer()
{
	return m_SwapChainBuffer[m_CurrentBackBuffer].Get();
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> Renderer::CreateRayGenSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	rsc.AddHeapRangesParameter(
		{ { 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0 },
		{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1},
		{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 4},
		{ 4, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5},
		{ 5, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6},
		{ 6, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 7},
		}
	);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 3);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 4);
	rsc.AddHeapRangesParameter({ { 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0 }
		});

	return rsc.Generate(m_Device.Get(), true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> Renderer::CreateHitSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 2);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 1);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 2);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 3);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 4);
	rsc.AddHeapRangesParameter({ { 3, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2} });
	rsc.AddHeapRangesParameter({ { 4, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5} });
	rsc.AddHeapRangesParameter({ { 5, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6} });
	rsc.AddHeapRangesParameter({ { 6, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 7} });
	return rsc.Generate(m_Device.Get(), true);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> Renderer::CreateMissSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	return rsc.Generate(m_Device.Get(), true);
}

void Renderer::CreateRaytracingPipeline()
{
	nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_Device.Get());

	m_RayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders\\RayGen.hlsl");
	m_MissLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders\\Miss.hlsl");
	m_HitLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders\\Hit.hlsl");

	m_ShadowLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders\\ShadowRay.hlsl");
	pipeline.AddLibrary(m_ShadowLibrary.Get(), { L"ShadowMiss" });
	m_ShadowSignature = CreateMissSignature();

	pipeline.AddLibrary(m_RayGenLibrary.Get(), { L"RayGen" });
	pipeline.AddLibrary(m_MissLibrary.Get(), { L"Miss" });
	pipeline.AddLibrary(m_HitLibrary.Get(), { L"ClosestHit", L"PlaneClosestHit", L"ReflectionClosestHit" });

	m_RayGenSignature = CreateRayGenSignature();
	m_MissSignature = CreateMissSignature();
	m_HitSignature = CreateHitSignature();
	m_ReflectionSignature = CreateHitSignature();

	pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");
	pipeline.AddHitGroup(L"PlaneHitGroup", L"PlaneClosestHit");
	pipeline.AddHitGroup(L"ShadowHitGroup", L"");
	pipeline.AddHitGroup(L"ReflectionHitGroup", L"ReflectionClosestHit");

	pipeline.AddRootSignatureAssociation(m_RayGenSignature.Get(), { L"RayGen" });
	pipeline.AddRootSignatureAssociation(m_MissSignature.Get(), { L"ShadowMiss" });

	pipeline.AddRootSignatureAssociation(m_ShadowSignature.Get(), { L"ShadowHitGroup" });
	pipeline.AddRootSignatureAssociation(m_MissSignature.Get(), { L"Miss", L"ShadowMiss" });
	pipeline.AddRootSignatureAssociation(m_HitSignature.Get(), { L"HitGroup",  L"PlaneHitGroup" });

	pipeline.AddRootSignatureAssociation(m_ReflectionSignature.Get(), { L"ReflectionHitGroup" });
	pipeline.AddRootSignatureAssociation(m_MissSignature.Get(), { L"Miss" });
	pipeline.AddRootSignatureAssociation(m_HitSignature.Get(), { L"HitGroup" });

	pipeline.SetMaxPayloadSize(8 * sizeof(float));
	pipeline.SetMaxAttributeSize(2 * sizeof(float));
	pipeline.SetMaxRecursionDepth(6);


	m_RtStateObject = pipeline.Generate();

	ThrowIfFailed(m_RtStateObject->QueryInterface(IID_PPV_ARGS(&m_RtStateObjectProps)));
}

void Renderer::CreateRaytracingOutputBuffer()
{
	D3D12_RESOURCE_DESC resDesc = {};

	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Width = m_ClientWidth;
	resDesc.Height = m_ClientHeight;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;

	ThrowIfFailed(m_Device->CreateCommittedResource(&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_OutputResource)));
}

void Renderer::CreateShaderResourceHeap()
{
	m_SrvUavHeap = nv_helpers_dx12::CreateDescriptorHeap(m_Device.Get(), 8, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_SrvUavHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	m_Device->CreateUnorderedAccessView(m_OutputResource.Get(), nullptr, &uavDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = m_topLevelASBuffers.pResult->GetGPUVirtualAddress();

	m_Device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(m_MaterialsGPU.size());
	srvDesc.Buffer.StructureByteStride = sizeof(MaterialDataGPU);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	m_Device->CreateShaderResourceView(m_UploadCBuffer.Get(), &srvDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(m_InstanceData.size());
	srvDesc.Buffer.StructureByteStride = sizeof(InstanceData);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	m_Device->CreateShaderResourceView(m_InstanceBuffer.Get(), &srvDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_FrameResources[m_CurrentFrameResourceIndex]->PassCB->Resource()->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = Align(sizeof(PassConstants), 256);
	m_Device->CreateConstantBufferView(&cbvDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	//
	//cbvDesc = {};
	//cbvDesc.BufferLocation = m_CameraBuffer->GetGPUVirtualAddress();
	//cbvDesc.SizeInBytes = m_CameraBufferSize;
	//m_Device->CreateConstantBufferView(&cbvDesc, srvHandle);
	//
	//srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_SHADER_RESOURCE_VIEW_DESC gBufferAlbedoMetalDesc = {};
	gBufferAlbedoMetalDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	gBufferAlbedoMetalDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	gBufferAlbedoMetalDesc.Texture2D.MipLevels = 1;
	gBufferAlbedoMetalDesc.Texture2D.MostDetailedMip = 0;
	gBufferAlbedoMetalDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	m_Device->CreateShaderResourceView(m_GBufferAlbedoMetal.Get(), &gBufferAlbedoMetalDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_SHADER_RESOURCE_VIEW_DESC gBufferNormalRoughDesc = {};
	gBufferNormalRoughDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	gBufferNormalRoughDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	gBufferNormalRoughDesc.Texture2D.MipLevels = 1;
	gBufferNormalRoughDesc.Texture2D.MostDetailedMip = 0;
	gBufferNormalRoughDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	m_Device->CreateShaderResourceView(m_GBufferNormalRough.Get(), &gBufferNormalRoughDesc, srvHandle);
	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_SHADER_RESOURCE_VIEW_DESC depthBufferSRVDesc = {};
	depthBufferSRVDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depthBufferSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthBufferSRVDesc.Texture2D.MipLevels = 1;
	depthBufferSRVDesc.Texture2D.MostDetailedMip = 0;
	depthBufferSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	m_Device->CreateShaderResourceView(m_DepthStencilBuffer.Get(), &depthBufferSRVDesc, srvHandle);
	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_SamplerHeap = nv_helpers_dx12::CreateDescriptorHeap(m_Device.Get(), 1, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true);
	D3D12_CPU_DESCRIPTOR_HANDLE samplerHandle = m_SamplerHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_SAMPLER_DESC samplerDesc = {};
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	m_Device->CreateSampler(&samplerDesc, samplerHandle);

	samplerHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

}

void Renderer::CreateShaderBindingTable()
{
	m_SbtHelper.Reset();

	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = m_SrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	auto heapPointer = reinterpret_cast<void*>(srvUavHeapHandle.ptr);
	D3D12_GPU_DESCRIPTOR_HANDLE samplerHeapHandle = m_SamplerHeap->GetGPUDescriptorHandleForHeapStart();
	auto samplerHeapPointer = reinterpret_cast<void*>(samplerHeapHandle.ptr);

	m_SbtHelper.AddRayGenerationProgram(L"RayGen", {
	heapPointer,
	(void*)m_PostProcessConstantBuffer->GetGPUVirtualAddress(),
	(void*)m_AreaLightConstantBuffer->GetGPUVirtualAddress(),
samplerHeapPointer });

	m_SbtHelper.AddMissProgram(L"Miss", {});
	m_SbtHelper.AddMissProgram(L"ShadowMiss", {});

	for (UINT i = 0; i < m_Instances.size(); i++)
	{
		D3D12_GPU_VIRTUAL_ADDRESS vb = 0;
		D3D12_GPU_VIRTUAL_ADDRESS ib = 0;
		D3D12_GPU_VIRTUAL_ADDRESS perInstanceCB = m_PerInstanceCBs[i]->GetGPUVirtualAddress();

		if (i == 0)
		{
			vb = boxSubmesh->VertexBufferGPU->GetGPUVirtualAddress();
			ib = boxSubmesh->IndexBufferGPU->GetGPUVirtualAddress();
		}
		else if (i > 0 && i < m_SkullCount)
		{
			vb = skullSubmesh->VertexBufferGPU->GetGPUVirtualAddress();
			ib = skullSubmesh->IndexBufferGPU->GetGPUVirtualAddress();

		}
		else if (i > m_SkullCount && i < m_SkullCount + m_SphereCount)
		{
			vb = sphereSubmesh->VertexBufferGPU->GetGPUVirtualAddress();
			ib = sphereSubmesh->IndexBufferGPU->GetGPUVirtualAddress();
		}



		if (m_IsInstanceReflective[i])
		{
			m_SbtHelper.AddHitGroup(L"ReflectionHitGroup", { (void*)vb,(void*)ib,
				(void*)m_topLevelASBuffers.pResult->GetGPUVirtualAddress(),
				(void*)m_CurrentFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
				(void*)m_GlobalConstantBuffer->GetGPUVirtualAddress(),
				(void*)perInstanceCB,
				(void*)m_PostProcessConstantBuffer->GetGPUVirtualAddress(),
				(void*)m_AreaLightConstantBuffer->GetGPUVirtualAddress(),
				heapPointer,
				});

			m_SbtHelper.AddHitGroup(L"ShadowHitGroup", {});

			m_SbtHelper.AddHitGroup(L"ReflectionHitGroup", { (void*)vb,(void*)ib,
				(void*)m_topLevelASBuffers.pResult->GetGPUVirtualAddress(),
				(void*)m_CurrentFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
				(void*)m_GlobalConstantBuffer->GetGPUVirtualAddress(),
				(void*)perInstanceCB,
				(void*)m_PostProcessConstantBuffer->GetGPUVirtualAddress(),
				(void*)m_AreaLightConstantBuffer->GetGPUVirtualAddress(),
				heapPointer,
				});
		}
		else
		{
			m_SbtHelper.AddHitGroup(L"PlaneHitGroup", { (void*)vb,(void*)ib,
				(void*)m_topLevelASBuffers.pResult->GetGPUVirtualAddress(),
				(void*)m_CurrentFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
				(void*)m_GlobalConstantBuffer->GetGPUVirtualAddress(),
				(void*)perInstanceCB,
				(void*)m_PostProcessConstantBuffer->GetGPUVirtualAddress(),
				(void*)m_AreaLightConstantBuffer->GetGPUVirtualAddress(),
				heapPointer,
				});

			m_SbtHelper.AddHitGroup(L"ShadowHitGroup", {});

			m_SbtHelper.AddHitGroup(L"HitGroup", { (void*)vb,(void*)ib,
				(void*)m_topLevelASBuffers.pResult->GetGPUVirtualAddress(),
				(void*)m_CurrentFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
				(void*)m_GlobalConstantBuffer->GetGPUVirtualAddress(),
				(void*)perInstanceCB,
				(void*)m_PostProcessConstantBuffer->GetGPUVirtualAddress(),
				(void*)m_AreaLightConstantBuffer->GetGPUVirtualAddress(),
				heapPointer,
				});

		}
	}

	uint32_t sbtSize = m_SbtHelper.ComputeSBTSize();

	m_SbtStorage = nv_helpers_dx12::CreateBuffer(m_Device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	if (!m_SbtStorage)
	{
		throw std::logic_error("Could not allocate the shader binding table.");
	}

	m_SbtHelper.Generate(m_SbtStorage.Get(), m_RtStateObjectProps.Get());
}

Renderer::AccelerationStructureBuffers Renderer::CreateBottomLevelAS(std::vector <std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers, std::vector <std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers)
{
	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;

	for (size_t i = 0; i < vVertexBuffers.size(); i++) {
		// for (const auto &buffer : vVertexBuffers) {
		if (i < vIndexBuffers.size() && vIndexBuffers[i].second > 0)
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), offsetof(Vertex, Pos), vVertexBuffers[i].second, sizeof(Vertex), vIndexBuffers[i].first.Get(), 0, vIndexBuffers[i].second, nullptr, 0);
		else
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), offsetof(Vertex, Pos), vVertexBuffers[i].second, sizeof(Vertex), nullptr, 0);
	}

	UINT64 scratchSizeInBytes = 0;

	UINT64 resultSizeInBytes = 0;

	bottomLevelAS.ComputeASBufferSizes(m_Device.Get(), false, &scratchSizeInBytes, &resultSizeInBytes);

	AccelerationStructureBuffers buffers;

	buffers.pScratch = nv_helpers_dx12::CreateBuffer(m_Device.Get(), scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, nv_helpers_dx12::kDefaultHeapProps);
	buffers.pResult = nv_helpers_dx12::CreateBuffer(m_Device.Get(), resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nv_helpers_dx12::kDefaultHeapProps);

	bottomLevelAS.Generate(m_CommandList.Get(), buffers.pScratch.Get(), buffers.pResult.Get(), false, nullptr);

	return buffers;
}

void Renderer::CreateTopLevelAS(std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances, bool updateOnly)
{
	if (true)
	{
		for (size_t i = 0; i < instances.size(); i++)
		{
			UINT hitGroupIndex = i;
			//if (i < m_SkullCount)
			//{
			//	hitGroupIndex = i;
			//}

			//if (i >= m_SkullCount)
			//{
			//	hitGroupIndex = i;
			//}

			m_topLevelASGenerator.AddInstance(instances[i].first.Get(), instances[i].second, static_cast<UINT>(i), static_cast<UINT>(i * gNumRayTypes));
		}

		UINT64 scratchSizeInBytes = 0;

		UINT64 resultSizeInBytes = 0;

		UINT64 instanceDescsSizeInBytes = 0;

		m_topLevelASGenerator.ComputeASBufferSizes(m_Device.Get(), true, &scratchSizeInBytes, &resultSizeInBytes, &instanceDescsSizeInBytes);

		m_topLevelASBuffers.pScratch = nv_helpers_dx12::CreateBuffer(m_Device.Get(), scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, nv_helpers_dx12::kDefaultHeapProps);
		m_topLevelASBuffers.pResult = nv_helpers_dx12::CreateBuffer(m_Device.Get(), resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nv_helpers_dx12::kDefaultHeapProps);
		m_topLevelASBuffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer(m_Device.Get(), instanceDescsSizeInBytes, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	}

	m_topLevelASGenerator.Generate(m_CommandList.Get(), m_topLevelASBuffers.pScratch.Get(), m_topLevelASBuffers.pResult.Get(), m_topLevelASBuffers.pInstanceDesc.Get(), updateOnly, m_topLevelASBuffers.pResult.Get());
}

void Renderer::CreateAccelerationStructures()
{
	AccelerationStructureBuffers bottomLevelBuffers = CreateBottomLevelAS({ { skullSubmesh->VertexBufferGPU, skullSubmesh->VertexCount} }, { {skullSubmesh->IndexBufferGPU, skullSubmesh->IndexCount }
		});
	AccelerationStructureBuffers planeBottomLevelBuffers = CreateBottomLevelAS({ { skullSubmesh->VertexBufferGPU, skullSubmesh->VertexCount} }, { {skullSubmesh->IndexBufferGPU, skullSubmesh->IndexCount} });

	AccelerationStructureBuffers sphereBottomLevelBuffers = CreateBottomLevelAS({ { sphereSubmesh->VertexBufferGPU, sphereSubmesh->VertexCount} }, { {sphereSubmesh->IndexBufferGPU, sphereSubmesh->IndexCount} });
	AccelerationStructureBuffers boxBottomLevelBuffers = CreateBottomLevelAS({ { boxSubmesh->VertexBufferGPU, boxSubmesh->VertexCount} }, { {boxSubmesh->IndexBufferGPU, boxSubmesh->IndexCount} });


	m_Instances = {
		{ boxBottomLevelBuffers.pResult, boxSubmesh->World[0]},
		{bottomLevelBuffers.pResult, skullSubmesh->World[0]}, {bottomLevelBuffers.pResult, skullSubmesh->World[1]}, {bottomLevelBuffers.pResult, skullSubmesh->World[2]},

		{ planeBottomLevelBuffers.pResult, skullSubmesh->World[3] },
		{ sphereBottomLevelBuffers.pResult, sphereSubmesh->World[0] },
	};

	m_IsInstanceReflective = {
		false,
		false,
		false,
		false,
		true,
		true
	};

	CreateTopLevelAS(m_Instances);

	m_CommandList->Close();
	ID3D12CommandList* ppCommandLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(1, ppCommandLists);

	m_FrameResources[m_CurrentFrameResourceIndex].get()->Fence++;
	m_CommandQueue->Signal(m_Fence.Get(), m_FrameResources[m_CurrentFrameResourceIndex].get()->Fence);

	ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), m_FrameResources[m_CurrentFrameResourceIndex].get()->Fence));

	if (m_Fence->GetCompletedValue() < m_FrameResources[m_CurrentFrameResourceIndex].get()->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_FrameResources[m_CurrentFrameResourceIndex].get()->Fence, eventHandle));

		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}


	ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator.Get(), m_PipelineStateObjects["opaque"].Get()));

	m_BottomLevelAS = bottomLevelBuffers.pResult;
	m_PlaneBottomLevelAS = planeBottomLevelBuffers.pResult;
	m_BoxBottomLevelAS = boxBottomLevelBuffers.pResult;
	m_SphereBottomLevelAS = sphereBottomLevelBuffers.pResult;
	m_BottomLevelASInst = bottomLevelBuffers.pInstanceDesc;
	m_PlaneBottomLevelASInst = planeBottomLevelBuffers.pInstanceDesc;
	m_BoxBottomLevelASInst = boxBottomLevelBuffers.pInstanceDesc;
	m_SphereBottomLevelASInst = sphereBottomLevelBuffers.pInstanceDesc;
}

void Renderer::CreatePlaneVB()
{
	Vertex planeVertices[] = {
		 {{-1.5f, -.8f, 01.5f}, { 0.0f, -1.0f, 0.0f }}, // 0
		 {{-1.5f, -.8f, -1.5f}, { 0.0f, -1.0f, 0.0f }}, // 1
		 {{01.5f, -.8f, 01.5f}, { 0.0f, -1.0f, 0.0f }}, // 2
		 {{01.5f, -.8f, 01.5f}, { 0.0f, -1.0f, 0.0f }}, // 2
		 {{-1.5f, -.8f, -1.5f}, { 0.0f, -1.0f, 0.0f }}, // 1
		 {{01.5f, -.8f, -1.5f}, { 0.0f, -1.0f, 0.0f }},  // 4
	};

	const UINT planeBufferSize = sizeof(planeVertices);

	CD3DX12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC bufferResource = CD3DX12_RESOURCE_DESC::Buffer(planeBufferSize);

	ThrowIfFailed(m_Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferResource, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_PlaneBuffer)));

	UINT8* pVertexDataBegin;

	CD3DX12_RANGE readRange(0, 0);

	ThrowIfFailed(m_PlaneBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));

	memcpy(pVertexDataBegin, planeVertices, sizeof(planeVertices));
	m_PlaneBuffer->Unmap(0, nullptr);

	m_PlaneBufferView.BufferLocation = m_PlaneBuffer->GetGPUVirtualAddress();
	m_PlaneBufferView.StrideInBytes = sizeof(Vertex);
	m_PlaneBufferView.SizeInBytes = planeBufferSize;
}

void Renderer::CreateCameraBuffer()
{
	uint32_t nbMatrix = 4;
	m_CameraBufferSize = nbMatrix * sizeof(XMMATRIX);

	m_CameraBuffer = nv_helpers_dx12::CreateBuffer(m_Device.Get(), m_CameraBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	m_ConstHeap = nv_helpers_dx12::CreateDescriptorHeap(m_Device.Get(), 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_CameraBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = m_CameraBufferSize;

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_ConstHeap->GetCPUDescriptorHandleForHeapStart();
	m_Device->CreateConstantBufferView(&cbvDesc, srvHandle);
}

void Renderer::UpdateCameraBuffer()
{
	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(m_EyePos.x, m_EyePos.y, m_EyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&m_View, view);

	std::vector<XMMATRIX> matrices(4);

	XMVECTOR Eye = XMVectorSet(0.0f, 1.0f, -7.5f, 1.0f);
	XMVECTOR At = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
	XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	matrices[0] = XMMatrixLookAtLH(Eye, At, Up);
	//const glm::mat4& mat = nv_helpers_dx12::CameraManip.getMatrix();
	//memcpy(&matrices[0].r->m128_f32[0], glm::value_ptr(mat), 16 * sizeof(float));


//	matrices[0] = XMLoadFloat4x4(matrices[]);
	//XMVECTOR pos = XMVectorSet(0.0f, 1.0f, -27.0f, 1.0f);
	//XMVECTOR target = XMVectorZero();
	//XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	//XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	//XMStoreFloat4x4(&m_View, view);

	//XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, (float)(m_ClientWidth / m_ClientHeight), 0.1f, 1000.0f);
	//XMStoreFloat4x4(&m_Proj, P);
	float fovAngleY = 45.0f * XM_PI / 180.0f;
	m_AspectRatio = (float)m_ClientWidth / (float)m_ClientHeight;
	matrices[1] = XMMatrixPerspectiveFovLH(fovAngleY, m_AspectRatio, 0.1f, 1000.0f);

	XMVECTOR det;
	matrices[2] = XMMatrixInverse(&det, matrices[0]);
	matrices[3] = XMMatrixInverse(&det, matrices[1]);

	uint8_t* pData;
	ThrowIfFailed(m_CameraBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, matrices.data(), m_CameraBufferSize);
	m_CameraBuffer->Unmap(0, nullptr);
}

void Renderer::CreateGlobalConstantBuffer()
{
	XMVECTOR bufferData[] = {
		// A
		XMVECTOR{1.0f, 0.0f, 0.0f, 1.0f},
		XMVECTOR{0.7f, 0.4f, 0.0f, 1.0f},
		XMVECTOR{0.4f, 0.7f, 0.0f, 1.0f},

		// B
		XMVECTOR{0.0f, 1.0f, 0.0f, 1.0f},
		XMVECTOR{0.0f, 0.7f, 0.4f, 1.0f},
		XMVECTOR{0.0f, 0.4f, 0.7f, 1.0f},

		// C
		XMVECTOR{0.0f, 0.0f, 1.0f, 1.0f},
		XMVECTOR{0.4f, 0.0f, 0.7f, 1.0f},
		XMVECTOR{0.7f, 0.0f, 0.4f, 1.0f},
	};

	m_GlobalConstantBuffer = nv_helpers_dx12::CreateBuffer(
		m_Device.Get(), sizeof(bufferData), D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	uint8_t* pData;
	ThrowIfFailed(m_GlobalConstantBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, bufferData, sizeof(bufferData));
	m_GlobalConstantBuffer->Unmap(0, nullptr);
}

void Renderer::CreatePostProcessConstantBuffer()
{
	m_PostProcessData.Exposure = 0.0f;
	m_PostProcessData.ToneMapMode = 2;
	m_PostProcessData.DebugMode = 0;
	m_PostProcessData.pad = 1.0f;

	m_PostProcessConstantBuffer = nv_helpers_dx12::CreateBuffer(
		m_Device.Get(), sizeof(m_PostProcessData), D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	uint8_t* pData;
	ThrowIfFailed(m_PostProcessConstantBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, (void*)&m_PostProcessData, sizeof(m_PostProcessData));
	m_PostProcessConstantBuffer->Unmap(0, nullptr);

}

void Renderer::CreateAreaLightConstantBuffer()
{
	m_AreaLightDataCollection.reserve(1);
	m_AreaLightData = new AreaLight();
	m_AreaLightData->Position = XMFLOAT3(0.0f, 60.0f, -5.0f);
	m_AreaLightData->Radiance = XMFLOAT3(0.5f, 0.5f, 0.5f);
	m_AreaLightData->U = XMFLOAT3(15.0f, 0.0f, 0.0f);
	m_AreaLightData->V = XMFLOAT3(0.0f, 0.0f, 15.0f);

	float lenU = sqrtf(m_AreaLightData->U.x * m_AreaLightData->U.x +
		m_AreaLightData->U.y * m_AreaLightData->U.y +
		m_AreaLightData->U.z * m_AreaLightData->U.z);
	float lenV = sqrtf(m_AreaLightData->V.x * m_AreaLightData->V.x +
		m_AreaLightData->V.y * m_AreaLightData->V.y +
		m_AreaLightData->V.z * m_AreaLightData->V.z);

	m_AreaLightData->Area = 4.0f * lenU * lenV;

	m_AreaLightData->Pad = 1.0f;
	m_AreaLightData->Pad2 = 1.0f;
	m_AreaLightData->Pad3 = 1.0f;

	m_AreaLightDataCollection.push_back(std::move(*m_AreaLightData));

	const uint32_t bufferSize = sizeof(AreaLight);

	m_AreaLightConstantBuffer = nv_helpers_dx12::CreateBuffer(
		m_Device.Get(), bufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	uint8_t* pData;
	ThrowIfFailed(m_AreaLightConstantBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, (void*)&m_AreaLightDataCollection[0], bufferSize);
	m_AreaLightConstantBuffer->Unmap(0, nullptr);

}

void Renderer::CreatePerInstanceBuffers()
{

	m_PerInstanceCBs.resize(m_PerInstanceCBCount);

	int i(0);

	for (auto& cb : m_PerInstanceCBs)
	{
		const uint32_t bufferSize = sizeof(int);

		cb = nv_helpers_dx12::CreateBuffer(m_Device.Get(), bufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

		uint8_t* pData;
		ThrowIfFailed(cb->Map(0, nullptr, (void**)&pData));
		memcpy(pData, &i, bufferSize);
		cb->Unmap(0, nullptr);
		++i;
	}

	m_MaterialsGPU.reserve(m_PerInstanceCBCount);

	for (auto& m : m_Materials)
	{
		MaterialDataGPU matGpu{};
		Material* mat = m.second.get();
		matGpu.DiffuseAlbedo = mat->DiffuseAlbedo;
		matGpu.FresnelR0 = mat->FresnelR0;
		matGpu.Ior = mat->Ior;
		matGpu.Reflectivity = mat->Reflectivity;
		matGpu.Absorption = mat->Absorption;
		matGpu.Shininess = 1.0f - mat->Roughness;
		matGpu.pad = 1.0f;
		matGpu.pad2 = 1.0f;
		matGpu.metallic = mat->metallic;
		matGpu.isReflective = mat->IsReflective;

		m_MaterialsGPU.push_back(std::move(matGpu));
	}

	uint32_t bufferSize = m_MaterialsGPU.size() * sizeof(MaterialDataGPU);

	m_UploadCBuffer = nv_helpers_dx12::CreateBuffer(m_Device.Get(), bufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	uint8_t* pData;
	ThrowIfFailed(m_UploadCBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, m_MaterialsGPU.data(), bufferSize);
	m_UploadCBuffer->Unmap(0, nullptr);



	bufferSize = m_InstanceData.size() * sizeof(InstanceData);
	m_InstanceBuffer = nv_helpers_dx12::CreateBuffer(m_Device.Get(), bufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	uint8_t* pData2;
	ThrowIfFailed(m_InstanceBuffer->Map(0, nullptr, (void**)&pData2));
	memcpy(pData2, m_InstanceData.data(), bufferSize);
	m_InstanceBuffer->Unmap(0, nullptr);

}

void Renderer::CreateImGuiDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 1;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	desc.NodeMask = 0;

	ThrowIfFailed(m_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_ImGuiSrvHeap.GetAddressOf())));

}
