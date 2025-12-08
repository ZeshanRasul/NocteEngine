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

const int gNumFrameResources = 1;
const int gNumRayTypes = 2;

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
	CreateDebugController();
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

	CreateRenderTargetView();

	CreateDepthStencilView();

	CreateRootSignature();
	CreateComputeRootSignature();
	BuildShadersAndInputLayout();
	d3dUtil::LoadObjModel("Models/bunny.obj", m_DragonModel);

	CreateModelBuffers(m_DragonModel);
	BuildShapeGeometry();
	BuildSkullGeometry();
	CreatePlaneGeometry();
	BuildMaterials();
	BuildRenderItems();




	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = m_ClientWidth;
	vp.Height = m_ClientHeight;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;

	BuildFrameResources();
	m_CurrentFrameResource = 0;
	m_CurrentFrameResourceIndex = (m_CurrentFrameResourceIndex + 1) % gNumFrameResources;
	m_CurrentFrameResource = m_FrameResources[m_CurrentFrameResourceIndex].get();

	BuildPSOs();
	CreateComputePipelineStateObjects();
	CreateCameraBuffer();
	CreateFrameIndexRNGCBuffer();

	CreateAccelerationStructures();
	CreateRaytracingPipeline();
	CreatePerInstanceBuffers();
	CreateGlobalConstantBuffer();
	CreatePostProcessConstantBuffer();
	CreateAreaLightConstantBuffer();
	CreateRaytracingOutputBuffer();
	CreatePresentUAV();
	CreateAccumulationBuffer();
	CreateDenoisingResources();
	CreateShaderResourceHeap();
	CreateShaderResourceCPUHeap();
	CreateDenoiseConstantBuffer();
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
	//XMStoreFloat4x4(&m_PrevView, cam.GetView());

	XMVECTOR At = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
	XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	if (m_CurrentFrameResource->Fence != 0 && m_Fence->GetCompletedValue() < m_CurrentFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}



	//	m_AnimationCounter++;
	//	m_Instances[1].second = XMMatrixRotationAxis({ 0.0f, 1.0f, 0.0f }, static_cast<float>(m_AnimationCounter) / 1000.0f);
	//	m_Instances[2].second = XMMatrixRotationAxis({ 0.0f, 1.0f, 0.0f }, static_cast<float>(m_AnimationCounter) / -1000.0f) * XMMatrixTranslation(10.0f, -10.0f, 0.0f);;
	//	m_Instances[3].second = XMMatrixRotationAxis({ 0.0f, 1.0f, 0.0f }, static_cast<float>(m_AnimationCounter) / -1000.0f) * XMMatrixTranslation(-10.0f, -10.0f, 0.0f);;

		//	UpdateCameraBuffer();
	UpdateObjectCBs();
	UpdateMainPassCB();
	UpdateMaterialCBs();
}

static inline void TransitionIfNeeded(
	ID3D12GraphicsCommandList* cl,
	ID3D12Resource* res,
	D3D12_RESOURCE_STATES before,
	D3D12_RESOURCE_STATES after)
{
	if (before == after) return;
	cl->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(res, before, after));
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

	m_CommandList->RSSetViewports(1, &vp);

	m_ScissorRect = { 0, 0, static_cast<long>(m_ClientWidth), static_cast<long>(m_ClientHeight) };
	m_CommandList->RSSetScissorRects(1, &m_ScissorRect);

	if (m_FrameIndex != 0)
	{
		//		m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	}


	m_CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	if (useRaster)
	{

	}
	else
	{

		std::vector<ID3D12DescriptorHeap*> heaps = { m_SrvUavHeap.Get() };
		m_CommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

		CD3DX12_RESOURCE_BARRIER transition;

		D3D12_DISPATCH_RAYS_DESC desc = {};

		UINT64 rayGenerationSectionSizeInBytes = m_SbtHelper.GetRayGenSectionSize();
		desc.RayGenerationShaderRecord.StartAddress = m_SbtStorage->GetGPUVirtualAddress();
		desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;

		UINT64 missSectionSizeInBytes = m_SbtHelper.GetMissSectionSize();

		desc.MissShaderTable.StartAddress = m_SbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
		desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
		desc.MissShaderTable.StrideInBytes = m_SbtHelper.GetMissEntrySize();

		UINT64 hitGroupsSectionSize = m_SbtHelper.GetHitGroupSectionSize();
		desc.HitGroupTable.StartAddress = m_SbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes + missSectionSizeInBytes;
		desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
		desc.HitGroupTable.StrideInBytes = m_SbtHelper.GetHitGroupEntrySize();

		desc.Width = m_ClientWidth;
		desc.Height = m_ClientHeight;
		desc.Depth = 1;

		bool hasViewChanged = false;

		if (m_View._11 != m_PrevView._11 || m_View._12 != m_PrevView._12 || m_View._13 != m_PrevView._13 || m_View._14 != m_PrevView._14 ||
			m_View._21 != m_PrevView._21 || m_View._22 != m_PrevView._22 || m_View._23 != m_PrevView._23 || m_View._24 != m_PrevView._24 ||
			m_View._31 != m_PrevView._31 || m_View._32 != m_PrevView._32 || m_View._33 != m_PrevView._33 || m_View._34 != m_PrevView._34 ||
			m_View._41 != m_PrevView._41 || m_View._42 != m_PrevView._42 || m_View._43 != m_PrevView._43 || m_View._44 != m_PrevView._44)
		{
			hasViewChanged = false;
		}
		if (m_FrameIndex != 0 && m_FrameIndex != 1)
		{
			D3D12_RESOURCE_BARRIER barriers[2];
			barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_AccumulationBuffer.Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_COPY_DEST);
			barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_FinalDenoiseBuffer,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_COPY_SOURCE);
			m_CommandList->ResourceBarrier(_countof(barriers), barriers);

			m_CommandList->CopyResource(m_AccumulationBuffer.Get(), m_FinalDenoiseBuffer);

			D3D12_RESOURCE_BARRIER barriers2[2];
			barriers2[0] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_AccumulationBuffer.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			barriers2[1] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_FinalDenoiseBuffer,
				D3D12_RESOURCE_STATE_COPY_SOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			m_CommandList->ResourceBarrier(_countof(barriers2), barriers2);

		}

		if (m_PrevCamPos.x != m_EyePos.x || m_PrevCamPos.y != m_EyePos.y || m_PrevCamPos.z != m_EyePos.z || hasViewChanged)
		{
			m_FrameIndex = 0;
			m_PrevCamPos = m_EyePos;
			XMStoreFloat4x4(&m_PrevView, XMLoadFloat4x4(&m_View));
			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			clearColor[0] = 0.0f;
			clearColor[1] = 0.0f;
			clearColor[2] = 0.0f;
			clearColor[3] = 0.0f;
			m_AccumulationBufferUavHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_SrvUavHeap->GetGPUDescriptorHandleForHeapStart(), 7, m_CbvSrvUavDescriptorSize);
			m_CommandList->ClearUnorderedAccessViewFloat(m_AccumulationBufferUavHandleGPU, m_AccumulationBufferUavHandleCPU, m_AccumulationBuffer.Get(), clearColor, 0, nullptr);

		}

		m_CommandList->SetPipelineState1(m_RtStateObject.Get());
		m_CommandList->DispatchRays(&desc);
		D3D12_RESOURCE_BARRIER barriers[3];

		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_AccumulationBuffer.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_NormalTex.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_DepthTex.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		m_CommandList->ResourceBarrier(_countof(barriers), barriers);

		if (m_FrameIndex != 0 && m_FrameIndex != 1)
		{

		}
		else if (m_FrameIndex != 0 && m_FrameIndex != 1)
		{

		}

		//ID3D12Resource* ping = m_DenoisePing.Get();
		//ID3D12Resource* pong = m_DenoisePong.Get();

		ID3D12Resource* src = nullptr;
		ID3D12Resource* dest = nullptr;
		const int numPasses = 4;

		for (int pass = 0; pass < numPasses; pass++)
		{
			UpdateDenoiseConstantBuffer(1 << pass, pass);


			if (pass == 0)
			{
				// First pass: read from accumulation, write to ping.
				src = m_AccumulationBuffer.Get();
				dest = m_DenoisePing.Get();
				D3D12_RESOURCE_BARRIER barriers[1];
				//barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
				//	src,
				//	D3D12_RESOURCE_STATE_UNORDERED_ACCESS, // or SRV from previous frame
				//	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				//m_CommandList->ResourceBarrier(_countof(barriers), barriers);


			}
			else if (pass < numPasses - 1)
			{
				// Intermediate passes: ping-pong between ping and pong.
				// If last pass was to ping, now read ping and write pong, and vice versa.
				src = (dest == m_DenoisePing.Get()) ? m_DenoisePing.Get() : m_DenoisePong.Get();
				dest = (dest == m_DenoisePing.Get()) ? m_DenoisePong.Get() : m_DenoisePing.Get();

				D3D12_RESOURCE_BARRIER barriers[1];
				barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
					src,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS, // or SRV from previous frame
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				m_CommandList->ResourceBarrier(_countof(barriers), barriers);

				if (pass != 1)
				{
					D3D12_RESOURCE_BARRIER barriers[1];
					barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
						dest,
						D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					m_CommandList->ResourceBarrier(_countof(barriers), barriers);
				}


			}
			else
			{
				// Last pass: write to present UAV.
				src = (dest == m_DenoisePong.Get()) ? m_DenoisePong.Get() : m_DenoisePing.Get();
				dest = m_PresentUAV.Get();
				D3D12_RESOURCE_BARRIER barriers[1];
				barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
					src,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS, // or SRV from previous frame
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				m_CommandList->ResourceBarrier(_countof(barriers), barriers);

				m_FinalDenoiseBuffer = src;
			}


			m_CommandList->SetPipelineState(m_DenoisePSO.Get());
			m_CommandList->SetComputeRootSignature(m_DenoiseRootSignature.Get());
			m_CommandList->SetDescriptorHeaps(1, m_SrvUavHeap.GetAddressOf());

			const auto heapStart = m_SrvUavHeap->GetGPUDescriptorHandleForHeapStart();
			int offsetFromStart = 0;
			if (dest == m_DenoisePing.Get())
			{
				// Denoise Ping UAV
				offsetFromStart = 0;
			}
			else if (dest == m_DenoisePong.Get())
			{
				// Denoise Pong UAV
				offsetFromStart = 1;
			}
			else if (dest == m_PresentUAV.Get())
			{
				// Present UAV
				offsetFromStart = 2;
			}

			int srvOffsetFromStart = 0;

			if (src == m_AccumulationBuffer.Get())
			{
				// Accumulation Buffer SRV
				srvOffsetFromStart = 2;
			}
			else if (src == m_DenoisePing.Get())
			{
				// Denoise Ping SRV
				srvOffsetFromStart = 0;
			}
			else if (src == m_DenoisePong.Get())
			{
				// Denoise Pong SRV
				srvOffsetFromStart = 1;
			}

			const auto uavTableBase = CD3DX12_GPU_DESCRIPTOR_HANDLE(heapStart, offsetFromStart, m_CbvSrvUavDescriptorSize);
			m_CommandList->SetComputeRootConstantBufferView(3, m_DenoiseCB->GetGPUVirtualAddress()); // denoise step
			m_CommandList->SetComputeRootDescriptorTable(0, uavTableBase);
			const auto srvTableBase = CD3DX12_GPU_DESCRIPTOR_HANDLE(heapStart, srvOffsetFromStart, m_CbvSrvUavDescriptorSize);
			m_CommandList->SetComputeRootDescriptorTable(1, srvTableBase);
			const auto pingpongSrvTableBase = CD3DX12_GPU_DESCRIPTOR_HANDLE(heapStart, srvOffsetFromStart, m_CbvSrvUavDescriptorSize);
			m_CommandList->SetComputeRootDescriptorTable(2, heapStart);



			UINT gx = (m_ClientWidth + 7) / 8;
			UINT gy = (m_ClientHeight + 7) / 8;
			m_CommandList->Dispatch(gx, gy, 1);


			if (pass == numPasses - 1)
			{

			}
		}
		{
			ID3D12Resource* finalSrc = src;
			ID3D12Resource* prevSrc = src == m_DenoisePing.Get() ? m_DenoisePong.Get() : m_DenoisePing.Get();
			D3D12_RESOURCE_BARRIER barriers[2];
			//barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			//	src,
			//	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			//	D3D12_RESOURCE_STATE_UNORDERED_ACCESS); // or SRV from previous frame
			barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_DenoisePing.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS); // or SRV from previous frame
			barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_DenoisePong.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS); // or SRV from previous frame
			m_CommandList->ResourceBarrier(_countof(barriers), barriers);
		}
		{

			D3D12_RESOURCE_BARRIER barriers[2];

			barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_PresentUAV.Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,    // last state we used it as UAV
				D3D12_RESOURCE_STATE_COPY_SOURCE);

			barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
				CurrentBackBuffer(),
				D3D12_RESOURCE_STATE_PRESENT,
				D3D12_RESOURCE_STATE_COPY_DEST);

			m_CommandList->ResourceBarrier(_countof(barriers), barriers);

			m_CommandList->CopyResource(CurrentBackBuffer(), m_PresentUAV.Get());

			m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				m_PresentUAV.Get(),
				D3D12_RESOURCE_STATE_COPY_SOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

			m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				CurrentBackBuffer(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_PRESENT));
		}

		{
			D3D12_RESOURCE_BARRIER barriers[3];

			barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_AccumulationBuffer.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_NormalTex.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_DepthTex.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			m_CommandList->ResourceBarrier(_countof(barriers), barriers);
		}

		UpdateFrameIndexRNGCBuffer();


		ThrowIfFailed(m_CommandList->Close());
		ID3D12CommandList* cmdLists[] = { m_CommandList.Get() };
		m_CommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

		ThrowIfFailed(m_SwapChain->Present(0, 0));
		m_CurrentBackBuffer = (m_CurrentBackBuffer + 1) % SwapChainBufferCount;

		m_CurrentFrameResource->Fence = ++m_CurrentFence;

		m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence);
	}
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
}

void Renderer::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
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

	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapChainBuffer[i])));

		m_Device->CreateRenderTargetView(m_SwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);

		rtvHeapHandle.Offset(m_RtvDescriptorSize);
	}
}

void Renderer::CreateDepthStencilView()
{
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = m_ClientWidth;
	depthStencilDesc.Height = m_ClientHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = m_DepthStencilFormat;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = m_DepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	ThrowIfFailed(m_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &depthStencilDesc, D3D12_RESOURCE_STATE_COMMON, &optClear, IID_PPV_ARGS(m_DepthStencilBuffer.GetAddressOf())));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = m_DepthStencilFormat;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	m_Device->CreateDepthStencilView(m_DepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_DepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
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
	UINT objectCount = (UINT)m_OpaqueRenderItems.size();

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

	UINT objectCount = m_OpaqueRenderItems.size();

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
	//auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_ConstHeap->GetCPUDescriptorHandleForHeapStart())
}

void Renderer::CreateRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsConstantBufferView(2);
	slotRootParameter[3].InitAsConstantBufferView(3);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
	m_CsByteCode = d3dUtil::CompileShader(L"Shaders\\Denoise.hlsl", nullptr, "CSMain", "cs_5_0");

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
	boxMat->DiffuseAlbedo = XMFLOAT4(0.1, 0.3, 0.1, 1.0);
	boxMat->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	boxMat->Roughness = 0.99f;
	boxMat->metallic = 0.01f;
	boxMat->Ior = 1.0f;
	boxMat->IsReflective = false;

	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 1;
	bricks0->DiffuseSrvHeapIndex = 1;
	bricks0->DiffuseAlbedo = XMFLOAT4(Colors::Sienna);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.9f;
	bricks0->metallic = 0.1f;
	bricks0->IsReflective = false;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 2;
	stone0->DiffuseSrvHeapIndex = 5;
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
	skullMat->metallic = 0.1f;
	skullMat->Ior = 1.0f;
	skullMat->IsReflective = false;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 4;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(Colors::Aquamarine);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.8f;
	tile0->metallic = 0.05f;
	tile0->IsReflective = false;


	auto sphereMat = std::make_unique<Material>();
	sphereMat->Name = "sphere";
	sphereMat->MatCBIndex = 5;
	sphereMat->DiffuseSrvHeapIndex = 4;
	sphereMat->DiffuseAlbedo = XMFLOAT4(Colors::Violet);
	sphereMat->FresnelR0 = XMFLOAT3(0.06f, 0.06f, 0.06f);
	sphereMat->Roughness = 0.9f;
	sphereMat->metallic = 0.05f;
	skullMat->Ior = 1.5f;
	skullMat->IsReflective = false;

	auto tile1 = std::make_unique<Material>();
	tile1->Name = "tile1";
	tile1->MatCBIndex = 6;
	tile1->DiffuseSrvHeapIndex = 2;
	tile1->DiffuseAlbedo = XMFLOAT4(Colors::DarkSlateGray);
	tile1->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile1->Roughness = 0.8f;
	tile1->metallic = 0.05f;
	tile1->IsReflective = false;

	auto tile2 = std::make_unique<Material>();
	tile2->Name = "tile2";
	tile2->MatCBIndex = 7;
	tile2->DiffuseSrvHeapIndex = 2;
	tile2->DiffuseAlbedo = XMFLOAT4(0.63, 0.065, 0.05, 1.0f);
	tile2->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile2->Roughness = 0.8f;
	tile2->metallic = 0.05f;
	tile2->IsReflective = false;

	auto tile3 = std::make_unique<Material>();
	tile3->Name = "tile3";
	tile3->MatCBIndex = 8;
	tile3->DiffuseSrvHeapIndex = 2;
	tile3->DiffuseAlbedo = XMFLOAT4(0.14, 0.45, 0.591, 1.0f);
	tile3->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile3->Roughness = 0.8f;
	tile3->metallic = 0.05f;
	tile3->IsReflective = false;

	auto tile4 = std::make_unique<Material>();
	tile4->Name = "tile4";
	tile4->MatCBIndex = 9;
	tile4->DiffuseSrvHeapIndex = 2;
	tile4->DiffuseAlbedo = XMFLOAT4(Colors::DarkSlateGray);
	tile4->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile4->Roughness = 0.8f;
	tile4->metallic = 0.65f;
	tile4->IsReflective = true;

	auto tile5 = std::make_unique<Material>();
	tile5->Name = "tile5";
	tile5->MatCBIndex = 10;
	tile5->DiffuseSrvHeapIndex = 2;
	tile5->DiffuseAlbedo = XMFLOAT4(0.725, 0.725, 0.725, 1.0f);
	tile5->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile5->Roughness = 0.01f;
	tile5->metallic = 0.05f;
	tile5->IsReflective = false;
//	tile5->IsRefractive = true;
//	tile5->Ior = 1.5f;

	auto dragon = std::make_unique<Material>();
	dragon->Name = "dragon";
	dragon->MatCBIndex = 11;
	dragon->DiffuseSrvHeapIndex = 2;
	dragon->DiffuseAlbedo = XMFLOAT4(Colors::LightGoldenrodYellow);
	dragon->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	dragon->Roughness = 0.71f;
	dragon->metallic = 0.25f;
//	dragon->IsReflective = false;
//	dragon->IsRefractive = true;
//	dragon->Ior = 1.5f;


	m_Materials.push_back(std::move(boxMat));
	m_Materials.push_back(std::move(bricks0));
	m_Materials.push_back(std::move(stone0));
	m_Materials.push_back(std::move(tile0));
	m_Materials.push_back(std::move(skullMat));
	m_Materials.push_back(std::move(sphereMat));
	m_Materials.push_back(std::move(tile1));
	m_Materials.push_back(std::move(tile2));
	m_Materials.push_back(std::move(tile3));
	m_Materials.push_back(std::move(tile4));
	m_Materials.push_back(std::move(tile5));
	m_Materials.push_back(std::move(dragon));
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

	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;
	boxSubmesh.VertexCount = (UINT)box.Vertices.size();

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

	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
	sphereSubmesh.VertexCount = (UINT)sphere.Vertices.size();

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

	boxSubmesh.VertexByteStride = sizeof(Vertex);
	boxSubmesh.BaseVertexLocation = 0;
	boxSubmesh.VertexBufferByteSize = (UINT)boxVertices.size() * sizeof(Vertex);
	boxSubmesh.IndexBufferByteSize = box.Indices32.size() * sizeof(uint32_t);
	boxSubmesh.IndexFormat = DXGI_FORMAT_R32_UINT;

	ThrowIfFailed(D3DCreateBlob(boxSubmesh.VertexBufferByteSize, &boxSubmesh.VertexBufferCPU));
	CopyMemory(boxSubmesh.VertexBufferCPU->GetBufferPointer(), boxVertices.data(), boxSubmesh.VertexBufferByteSize);

	ThrowIfFailed(D3DCreateBlob(boxSubmesh.IndexBufferByteSize, &boxSubmesh.IndexBufferCPU));
	CopyMemory(boxSubmesh.IndexBufferCPU->GetBufferPointer(), box.Indices32.data(), boxSubmesh.IndexBufferByteSize);

	boxSubmesh.VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), boxVertices.data(), boxSubmesh.VertexBufferByteSize, boxSubmesh.VertexBufferUploader);

	boxSubmesh.IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), box.Indices32.data(), boxSubmesh.IndexBufferByteSize, boxSubmesh.IndexBufferUploader);


	sphereSubmesh.VertexByteStride = sizeof(Vertex);
	sphereSubmesh.BaseVertexLocation = 0;
	sphereSubmesh.VertexBufferByteSize = (UINT)sphereVertices.size() * sizeof(Vertex);
	sphereSubmesh.IndexBufferByteSize = sphere.Indices32.size() * sizeof(uint32_t);
	sphereSubmesh.IndexFormat = DXGI_FORMAT_R32_UINT;

	ThrowIfFailed(D3DCreateBlob(sphereSubmesh.VertexBufferByteSize, &sphereSubmesh.VertexBufferCPU));
	CopyMemory(sphereSubmesh.VertexBufferCPU->GetBufferPointer(), sphereVertices.data(), sphereSubmesh.VertexBufferByteSize);

	ThrowIfFailed(D3DCreateBlob(sphereSubmesh.IndexBufferByteSize, &sphereSubmesh.IndexBufferCPU));
	CopyMemory(sphereSubmesh.IndexBufferCPU->GetBufferPointer(), sphere.Indices32.data(), sphereSubmesh.IndexBufferByteSize);

	sphereSubmesh.VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), sphereVertices.data(), sphereSubmesh.VertexBufferByteSize, sphereSubmesh.VertexBufferUploader);

	sphereSubmesh.IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), sphere.Indices32.data(), sphereSubmesh.IndexBufferByteSize, sphereSubmesh.IndexBufferUploader);



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

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	m_vertexCount += totalVertexCount;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
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

	//auto boxRitem = std::make_unique<RenderItem>();
	//XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	//XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	//boxRitem->ObjCBIndex = 1;
	//boxRitem->Mat = m_Materials["stone0"].get();
	//boxRitem->Geo = m_Geometries["shapeGeo"].get();
	//boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	//boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	//boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	//m_AllRenderItems.push_back(std::move(boxRitem));


	//auto skullRitem = std::make_unique<RenderItem>();
	//XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	//skullRitem->TexTransform = MathHelper::Identity4x4();
	//skullRitem->ObjCBIndex = 2;
	//skullRitem->Mat = m_Materials["skullMat"].get();
	//skullRitem->Geo = m_Geometries["skullGeo"].get();
	//skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	//skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	//skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	//m_AllRenderItems.push_back(std::move(skullRitem));

	//XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	//UINT objCBIndex = 3;
	//for (int i = 0; i < 5; ++i)
	//{
	//	auto leftCylRitem = std::make_unique<RenderItem>();
	//	auto rightCylRitem = std::make_unique<RenderItem>();
	//	auto leftSphereRitem = std::make_unique<RenderItem>();
	//	auto rightSphereRitem = std::make_unique<RenderItem>();

	//	XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
	//	XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

	//	XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
	//	XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

	//	XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
	//	XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
	//	leftCylRitem->ObjCBIndex = objCBIndex++;
	//	leftCylRitem->Mat = m_Materials["bricks0"].get();
	//	leftCylRitem->Geo = m_Geometries["shapeGeo"].get();
	//	leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//	leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	//	leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	//	leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

	//	XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
	//	XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
	//	rightCylRitem->ObjCBIndex = objCBIndex++;
	//	rightCylRitem->Mat = m_Materials["bricks0"].get();
	//	rightCylRitem->Geo = m_Geometries["shapeGeo"].get();
	//	rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//	rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	//	rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	//	rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

	//	XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
	//	leftSphereRitem->TexTransform = MathHelper::Identity4x4();
	//	leftSphereRitem->ObjCBIndex = objCBIndex++;
	//	leftSphereRitem->Mat = m_Materials["stone0"].get();
	//	leftSphereRitem->Geo = m_Geometries["shapeGeo"].get();
	//	leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//	leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	//	leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	//	leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	//	XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
	//	rightSphereRitem->TexTransform = MathHelper::Identity4x4();
	//	rightSphereRitem->ObjCBIndex = objCBIndex++;
	//	rightSphereRitem->Mat = m_Materials["stone0"].get();
	//	rightSphereRitem->Geo = m_Geometries["shapeGeo"].get();
	//	rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//	rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	//	rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	//	rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	//	m_AllRenderItems.push_back(std::move(leftCylRitem));
	//	m_AllRenderItems.push_back(std::move(rightCylRitem));
	//	m_AllRenderItems.push_back(std::move(leftSphereRitem));
	//	m_AllRenderItems.push_back(std::move(rightSphereRitem));
	//}

	//// All the render items are opaque.
	//for (auto& e : m_AllRenderItems)
	//	m_OpaqueRenderItems.push_back(e.get());
}

void Renderer::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& riItems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = m_CurrentFrameResource->ObjectCB->Resource();
	auto matCB = m_CurrentFrameResource->MaterialCB->Resource();

	for (size_t i = 0; i < riItems.size(); ++i)
	{
		auto ri = riItems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(1, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
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
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = m_BackBufferFormat;
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
		m_FrameResources.push_back(std::make_unique<FrameResource>(m_Device.Get(), 1, (UINT)12));
	}
}
void Renderer::UpdateObjectCBs()
{
	auto currObjectCB = m_CurrentFrameResource->ObjectCB.get();

	for (auto& e : m_AllRenderItems)
	{
		if (e->NumFramesDirty)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			e->NumFramesDirty--;
		}
	}
}
void Renderer::UpdateMaterialCBs()
{
	auto currentMaterialCB = m_CurrentFrameResource->MaterialCB.get();

	for (auto& e : m_Materials)
	{
		Material* mat = e.get();

		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			matConstants.MatTransform = mat->MatTransform;

			currentMaterialCB->CopyData(mat->MatCBIndex, matConstants);

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

	m_MainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	m_MainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	m_MainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

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

ID3D12Resource* Renderer::CurrentBackBuffer() const
{
	return m_SwapChainBuffer[m_CurrentBackBuffer].Get();
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> Renderer::CreateRayGenSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 5);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 3);
	rsc.AddHeapRangesParameter(
		{ { 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0 },
		{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1},
		{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 3},
		{ 1, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4},
		{ 2, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5},
		{ 3, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 6},
		}
	);


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
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 5);
	rsc.AddHeapRangesParameter({ { 3, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2} });

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

	//	m_ShadowLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders\\ShadowRay.hlsl");
	//	pipeline.AddLibrary(m_ShadowLibrary.Get(), { L"ShadowMiss" });
	//	m_ShadowSignature = CreateMissSignature();

	pipeline.AddLibrary(m_RayGenLibrary.Get(), { L"RayGen" });
	pipeline.AddLibrary(m_MissLibrary.Get(), { L"Miss",  L"ShadowMiss" });
	pipeline.AddLibrary(m_HitLibrary.Get(), { L"ClosestHit", L"ShadowClosestHit" });

	m_RayGenSignature = CreateRayGenSignature();
	m_MissSignature = CreateMissSignature();
	m_HitSignature = CreateHitSignature();
	//	m_ReflectionSignature = CreateHitSignature();

	pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");
	pipeline.AddHitGroup(L"ShadowHitGroup", L"ShadowClosestHit");

	pipeline.AddRootSignatureAssociation(m_RayGenSignature.Get(), { L"RayGen" });
	pipeline.AddRootSignatureAssociation(m_MissSignature.Get(), { L"Miss", L"ShadowMiss" });
	pipeline.AddRootSignatureAssociation(m_HitSignature.Get(), { L"HitGroup", L"ShadowHitGroup" });

	pipeline.SetMaxPayloadSize(48 * sizeof(float));
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

	ThrowIfFailed(m_Device->CreateCommittedResource(&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_OutputResource)));
}

void Renderer::CreateShaderResourceCPUHeap()
{
	m_SrvUavCPUHeap = nv_helpers_dx12::CreateDescriptorHeap(m_Device.Get(), 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_SrvUavCPUHeap->GetCPUDescriptorHandleForHeapStart();


	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	m_AccumulationBuffer->SetName(L"Accumulation Buffer CPU UAV");
	m_Device->CreateUnorderedAccessView(m_AccumulationBuffer.Get(), nullptr, &uavDesc, srvHandle);

	m_AccumulationBufferUavHandleCPU = srvHandle;

}

void Renderer::CreateShaderResourceHeap()
{
	m_SrvUavHeap = nv_helpers_dx12::CreateDescriptorHeap(m_Device.Get(), 15, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

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


	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_FrameResources[m_CurrentFrameResourceIndex]->PassCB->Resource()->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = Align(sizeof(PassConstants), 256);
	m_Device->CreateConstantBufferView(&cbvDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//cbvDesc = {};
	//cbvDesc.BufferLocation = m_CameraBuffer->GetGPUVirtualAddress();
	//cbvDesc.SizeInBytes = m_CameraBufferSize;
	//m_Device->CreateConstantBufferView(&cbvDesc, srvHandle);


	uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	m_AccumulationBuffer->SetName(L"Accumulation Buffer UAV");
	m_Device->CreateUnorderedAccessView(m_AccumulationBuffer.Get(), nullptr, &uavDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_NormalTex->SetName(L"Normal Texture UAV");
	m_Device->CreateUnorderedAccessView(m_NormalTex.Get(), nullptr, &uavDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
	m_DepthTex->SetName(L"Depth Texture UAV");

	m_Device->CreateUnorderedAccessView(m_DepthTex.Get(), nullptr, &uavDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	m_DenoisePing->SetName(L"Denoise Ping UAV");
	m_Device->CreateUnorderedAccessView(m_DenoisePing.Get(), nullptr, &uavDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	m_DenoisePong->SetName(L"Denoise Pong UAV");
	m_Device->CreateUnorderedAccessView(m_DenoisePong.Get(), nullptr, &uavDesc, srvHandle);


	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_PresentUAV->SetName(L"Present UAV");
	m_Device->CreateUnorderedAccessView(m_PresentUAV.Get(), nullptr, &uavDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_NormalTex->SetName(L"Normal Texture SRV");
	m_Device->CreateShaderResourceView(m_NormalTex.Get(), &srvDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	m_DepthTex->SetName(L"Depth Texture SRV");
	m_Device->CreateShaderResourceView(m_DepthTex.Get(), &srvDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	m_DenoisePing->SetName(L"Denoise Ping SRV");
	m_Device->CreateShaderResourceView(m_DenoisePing.Get(), &srvDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	m_DenoisePong->SetName(L"Denoise Pong SRV");
	m_Device->CreateShaderResourceView(m_DenoisePong.Get(), &srvDesc, srvHandle);

	srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
	srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

	m_AccumulationBuffer->SetName(L"Accumulation Buffer SRV");
	m_Device->CreateShaderResourceView(m_AccumulationBuffer.Get(), &srvDesc, srvHandle);

	//srvHandle.ptr += m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

}

void Renderer::CreateAccumulationBuffer()
{
	D3D12_RESOURCE_DESC resDesc = {};

	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Width = m_ClientWidth;
	resDesc.Height = m_ClientHeight;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;

	ThrowIfFailed(m_Device->CreateCommittedResource(&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_AccumulationBuffer)));

}

void Renderer::CreateDenoisingResources()
{
	D3D12_RESOURCE_DESC resDesc = {};

	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.DepthOrArraySize = 1;
	resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Width = m_ClientWidth;
	resDesc.Height = m_ClientHeight;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	resDesc.SampleDesc.Quality = 0;
	resDesc.Alignment = 0;


	ThrowIfFailed(m_Device->CreateCommittedResource(&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_NormalTex)));

	resDesc = {};

	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Format = DXGI_FORMAT_R32_FLOAT;

	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Width = m_ClientWidth;
	resDesc.Height = m_ClientHeight;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	resDesc.SampleDesc.Quality = 0;
	resDesc.Alignment = 0;


	ThrowIfFailed(m_Device->CreateCommittedResource(&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_DepthTex)));

	resDesc = {};

	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Width = m_ClientWidth;
	resDesc.Height = m_ClientHeight;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	resDesc.SampleDesc.Quality = 0;
	resDesc.Alignment = 0;

	ThrowIfFailed(m_Device->CreateCommittedResource(&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_DenoisePing)));
	resDesc = {};

	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Width = m_ClientWidth;
	resDesc.Height = m_ClientHeight;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	resDesc.SampleDesc.Quality = 0;
	resDesc.Alignment = 0;


	ThrowIfFailed(m_Device->CreateCommittedResource(&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_DenoisePong)));

}

void Renderer::CreateComputeRootSignature()
{
	//nv_helpers_dx12::RootSignatureGenerator rsc;
	//rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0); // Denoise constants

	//rsc.AddHeapRangesParameter({
	//	{0, 3, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 7}, // Output: denoise ping, denoise pong
	//	{0, 3, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10}, // Input: accumulation, normal, depth
	//	});
	CD3DX12_DESCRIPTOR_RANGE table = {};
	table.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 7);
	CD3DX12_DESCRIPTOR_RANGE table2 = {};
	table2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 12);
	CD3DX12_DESCRIPTOR_RANGE table3 = {};
	table3.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, 10);

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	slotRootParameter[0].InitAsDescriptorTable(1, &table);
	slotRootParameter[1].InitAsDescriptorTable(1, &table2);
	slotRootParameter[2].InitAsDescriptorTable(1, &table3);
	slotRootParameter[3].InitAsConstantBufferView(0);


	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	ThrowIfFailed(m_Device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(m_DenoiseRootSignature.GetAddressOf())));

}

void Renderer::CreateComputePipelineStateObjects()
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
	computePsoDesc.pRootSignature = m_DenoiseRootSignature.Get();
	computePsoDesc.CS = {
		reinterpret_cast<BYTE*>(m_CsByteCode->GetBufferPointer()),
		m_CsByteCode->GetBufferSize()
	};
	computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	ThrowIfFailed(m_Device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_DenoisePSO)));
}

void Renderer::CreatePresentUAV()
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

	ThrowIfFailed(m_Device->CreateCommittedResource(
		&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE,
		&resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr, IID_PPV_ARGS(&m_PresentUAV)));
}

void Renderer::CreateDenoiseConstantBuffer()
{
	DenoiseConstants denoiseConstants = {};
	denoiseConstants.sigmaColor = 2.0f;
	denoiseConstants.sigmaNormal = 128.0f;
	denoiseConstants.sigmaDepth = 2.0f;
	denoiseConstants.stepWidth = 1;
	denoiseConstants.invResolution = { 0.0f, 0.0f };
	denoiseConstants.pass = 0;
	denoiseConstants.pad = 0;

	const uint32_t bufferSize = sizeof(DenoiseConstants);

	m_DenoiseCB = nv_helpers_dx12::CreateBuffer(
		m_Device.Get(), sizeof(denoiseConstants), D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);


	uint8_t* pData;

	m_DenoiseCB->Map(0, nullptr, (void**)&pData);
	memcpy(pData, &denoiseConstants, bufferSize);
	m_DenoiseCB->Unmap(0, nullptr);
}

void Renderer::UpdateDenoiseConstantBuffer(int step, int pass)
{
	const float invStep = 1.0f / float(std::max(1, step));
	const float minScale = 0.25f;
	const float scale = std::max(invStep, minScale);

	const float baseSigmaColor = 6.0f;
	const float baseSigmaNormal = 4.0f;
	const float baseSigmaDepth = 1.0f;

	DenoiseConstants denoiseConstants = {};
	denoiseConstants.sigmaColor = baseSigmaColor;
	denoiseConstants.sigmaNormal = baseSigmaNormal;
	denoiseConstants.sigmaDepth = baseSigmaDepth;
	denoiseConstants.stepWidth = 1; // 1
	denoiseConstants.invResolution = m_MainPassCB.InvRenderTargetSize;
	denoiseConstants.pass = pass;
	denoiseConstants.pad = 0;

	const uint32_t bufferSize = sizeof(DenoiseConstants);
	uint8_t* pData = nullptr;
	m_DenoiseCB->Map(0, nullptr, reinterpret_cast<void**>(&pData));
	memcpy(pData, &denoiseConstants, bufferSize);
	m_DenoiseCB->Unmap(0, nullptr);
}

void Renderer::CreateShaderBindingTable()
{
	m_SbtHelper.Reset();

	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = m_SrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	auto heapPointer = reinterpret_cast<void*>(srvUavHeapHandle.ptr);

	m_SbtHelper.AddRayGenerationProgram(L"RayGen", {
				(void*)m_PostProcessConstantBuffer->GetGPUVirtualAddress(),
				(void*)m_RNGUploadCBuffer->GetGPUVirtualAddress(),
				heapPointer,
		});

	m_SbtHelper.AddMissProgram(L"Miss", {});
	m_SbtHelper.AddMissProgram(L"ShadowMiss", {});

	for (UINT i = 0; i < m_Instances.size(); i++)
	{
		D3D12_GPU_VIRTUAL_ADDRESS vb = 0;
		D3D12_GPU_VIRTUAL_ADDRESS ib = 0;
		D3D12_GPU_VIRTUAL_ADDRESS perInstanceCB = m_PerInstanceCBs[i]->GetGPUVirtualAddress();

	/*	if (i == 0)
		{
			vb = boxSubmesh.VertexBufferGPU->GetGPUVirtualAddress();
			ib = boxSubmesh.IndexBufferGPU->GetGPUVirtualAddress();
		}
		else */
		if (i >= 9 && i < 11)
		{
			vb = m_Geometries["skullGeo"]->VertexBufferGPU->GetGPUVirtualAddress();
			ib = m_Geometries["skullGeo"]->IndexBufferGPU->GetGPUVirtualAddress();
		
		}
		else if (i >= 7 && i < 9)
		{
			vb = sphereSubmesh.VertexBufferGPU->GetGPUVirtualAddress();
			ib = sphereSubmesh.IndexBufferGPU->GetGPUVirtualAddress();

		}
		else if (i < 7)
		{
			vb = m_PlaneVertexBuffer->GetGPUVirtualAddress();
			ib = m_PlaneIndexBuffer->GetGPUVirtualAddress();
		}
		else
		{
			vb = m_DragonVertexBuffer->GetGPUVirtualAddress();
			ib = m_DragonIndexBuffer->GetGPUVirtualAddress();
		}

		m_SbtHelper.AddHitGroup(L"HitGroup", { (void*)vb,(void*)ib,
			(void*)m_topLevelASBuffers.pResult->GetGPUVirtualAddress(),
			(void*)m_CurrentFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
			(void*)m_GlobalConstantBuffer->GetGPUVirtualAddress(),
			(void*)perInstanceCB,
			(void*)m_PostProcessConstantBuffer->GetGPUVirtualAddress(),
			(void*)m_AreaLightConstantBuffer->GetGPUVirtualAddress(),
			(void*)m_RNGUploadCBuffer->GetGPUVirtualAddress(),
			heapPointer
			});

		m_SbtHelper.AddHitGroup(L"ShadowHitGroup", { (void*)vb,(void*)ib,
			(void*)m_topLevelASBuffers.pResult->GetGPUVirtualAddress(),
			(void*)m_CurrentFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
			(void*)m_GlobalConstantBuffer->GetGPUVirtualAddress(),
			(void*)perInstanceCB,
			(void*)m_PostProcessConstantBuffer->GetGPUVirtualAddress(),
			(void*)m_AreaLightConstantBuffer->GetGPUVirtualAddress(),
			(void*)m_RNGUploadCBuffer->GetGPUVirtualAddress(),
			heapPointer
			});


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
	if (!updateOnly)
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

			m_topLevelASGenerator.AddInstance(instances[i].first.Get(), instances[i].second, static_cast<UINT>(i), static_cast<UINT>(i * 2));
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
	AccelerationStructureBuffers bottomLevelBuffers = CreateBottomLevelAS({ { m_DragonVertexBuffer, m_DragonModel.vertices.size()}}, {{m_DragonIndexBuffer, m_DragonModel.indices.size()}
		});
	AccelerationStructureBuffers skull0BottomLevelBuffers = CreateBottomLevelAS({ { m_Geometries["skullGeo"]->VertexBufferGPU, m_skullVertCount} }, { {m_Geometries["skullGeo"]->IndexBufferGPU, m_Geometries["skullGeo"]->DrawArgs["skull"].IndexCount} });

	AccelerationStructureBuffers sphereBottomLevelBuffers = CreateBottomLevelAS({ { sphereSubmesh.VertexBufferGPU, sphereSubmesh.VertexCount} }, { {sphereSubmesh.IndexBufferGPU, sphereSubmesh.IndexCount} });
	AccelerationStructureBuffers boxBottomLevelBuffers = CreateBottomLevelAS({ { boxSubmesh.VertexBufferGPU, boxSubmesh.VertexCount} }, { {boxSubmesh.IndexBufferGPU, boxSubmesh.IndexCount} });
	AccelerationStructureBuffers planeBottomLevelBuffers = CreateBottomLevelAS({ { m_PlaneVertexBuffer, 4} }, { { m_PlaneIndexBuffer, 6 }
});


	m_Instances =
	{
		// Floor (y = 0)
		{ planeBottomLevelBuffers.pResult,
		  XMMatrixScaling(40.0f, 1.0f, 40.0f) *
		  XMMatrixTranslation(0.0f, 0.0f, 0.0f) },

		// Ceiling (y = 40)
		{ planeBottomLevelBuffers.pResult,
		  XMMatrixScaling(50.0f, 1.0f, 50.0f) *
		  XMMatrixRotationAxis({1, 0, 0}, XMConvertToRadians(180.0f)) *
		  XMMatrixTranslation(0.0f, 60.0f, 0.0f) },

		// Back wall (z = -20), normal pointing into the box (+Z)
		{ planeBottomLevelBuffers.pResult,
		  XMMatrixScaling(50.0f, 1.0f, 50.0f) *
		  XMMatrixRotationAxis({1, 0, 0}, XMConvertToRadians(-90.0f)) *
		  XMMatrixTranslation(0.0f, 60.0f, -120.0f) },

		// Front wall (z = +20), normal pointing into the box (-Z)
		{ planeBottomLevelBuffers.pResult,
		  XMMatrixScaling(60.0f, 1.0f, 60.0f) *
		  XMMatrixRotationAxis({1, 0, 0}, XMConvertToRadians(90.0f)) *
		  XMMatrixTranslation(0.0f, 60.0f, 60.0f) },

		// Left wall (x = -20), normal pointing into the box (+X)
		{ planeBottomLevelBuffers.pResult,
		  XMMatrixScaling(60.0f, 1.0f, 60.0f) *
		  XMMatrixRotationAxis({0, 0, 1}, XMConvertToRadians(90.0f)) *
		  XMMatrixTranslation(-60.0f, 60.0f, 0.0f) },

		// Right wall (x = +20), normal pointing into the box (-X)
		{ planeBottomLevelBuffers.pResult,
		  XMMatrixScaling(60.0f, 1.0f, 60.0f) *
		  XMMatrixRotationAxis({0, 0, 1}, XMConvertToRadians(-90.0f)) *
		  XMMatrixTranslation(60.0f, 60.0f, 0.0f) },

		// ----------------------------------------------------
		// Objects on the floor: sphere (left) + skull (right)
		// ----------------------------------------------------

		{ planeBottomLevelBuffers.pResult,
		  XMMatrixScaling(6.0f, 1.0f, 6.0f) *
		  XMMatrixTranslation(12.0f, 0.0f, 8.0f) },
		
		// Sphere on the left: radius ~3 at y = 3
		{ sphereBottomLevelBuffers.pResult,
		  XMMatrixScaling(6.0f, 6.0f, 6.0f) *
		  XMMatrixTranslation(-17.0f, 3.0f, -5.0f) },

		// Sphere on the right: radius ~3 at y = 3
		{ sphereBottomLevelBuffers.pResult,
		  XMMatrixScaling(6.0f, 6.0f, 6.0f) *
		  XMMatrixTranslation(25.0f, 3.0f, -5.0f) },

		// Skull on the right
		{ skull0BottomLevelBuffers.pResult,
		  XMMatrixScaling(4.0f, 4.0f, 4.0f) *
		  XMMatrixTranslation(12.0f, 2.0f, 8.0f) },
		
		// Skull on the left
		{ skull0BottomLevelBuffers.pResult,
		  XMMatrixScaling(4.0f, 4.0f, 4.0f) *
		  XMMatrixTranslation(-20.0f, 2.0f, 15.0f) },

		{ bottomLevelBuffers.pResult,
		  XMMatrixScaling(10.0f, 10.0f, 10.0f) *
		  XMMatrixTranslation(0.0f, 0.0f, -25.0f)}
	};


	m_IsInstanceReflective = {
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
		false,
	};

	CreateTopLevelAS(m_Instances);

	//m_CommandList->Close();
	//ID3D12CommandList* ppCommandLists[] = { m_CommandList.Get() };
	//m_CommandQueue->ExecuteCommandLists(1, ppCommandLists);

	//m_FrameResources[m_CurrentFrameResourceIndex].get()->Fence++;
	//m_CommandQueue->Signal(m_Fence.Get(), m_FrameResources[m_CurrentFrameResourceIndex].get()->Fence);

	//ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), m_FrameResources[m_CurrentFrameResourceIndex].get()->Fence));

	//if (m_Fence->GetCompletedValue() < m_FrameResources[m_CurrentFrameResourceIndex].get()->Fence)
	//{
	//	HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

	//	ThrowIfFailed(m_Fence->SetEventOnCompletion(m_FrameResources[m_CurrentFrameResourceIndex].get()->Fence, eventHandle));

	//	WaitForSingleObject(eventHandle, INFINITE);
	//	CloseHandle(eventHandle);
	//}


	//ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator.Get(), m_PipelineStateObjects["opaque"].Get()));

	m_BottomLevelAS = bottomLevelBuffers.pResult;
	m_PlaneBottomLevelAS = skull0BottomLevelBuffers.pResult;
}

void Renderer::CreatePlaneGeometry()
{
	// 4 unique vertices for the plane
	Vertex planeVertices[] =
	{
		{{-1.5f, -0.8f,  1.5f}, { 0.0f, -1.0f, 0.0f }}, // 0
		{{-1.5f, -0.8f, -1.5f}, { 0.0f, -1.0f, 0.0f }}, // 1
		{{ 1.5f, -0.8f,  1.5f}, { 0.0f, -1.0f, 0.0f }}, // 2
		{{ 1.5f, -0.8f, -1.5f}, { 0.0f, -1.0f, 0.0f }}, // 3
	};

	// Two triangles: (0,1,2) and (2,1,3) – matches your original winding
	uint32_t planeIndices[] =
	{
		0, 1, 2,
		2, 1, 3
	};

	const UINT vbSize = sizeof(planeVertices);
	const UINT ibSize = sizeof(planeIndices);

	// Vertex buffer (upload for simplicity)
	{
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

		ThrowIfFailed(m_Device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_PlaneVertexBuffer)));

		UINT8* pDataBegin = nullptr;
		CD3DX12_RANGE readRange(0, 0); // CPU will not read back

		ThrowIfFailed(m_PlaneVertexBuffer->Map(0, &readRange,
			reinterpret_cast<void**>(&pDataBegin)));
		memcpy(pDataBegin, planeVertices, vbSize);
		m_PlaneVertexBuffer->Unmap(0, nullptr);

		m_PlaneVBView.BufferLocation = m_PlaneVertexBuffer->GetGPUVirtualAddress();
		m_PlaneVBView.StrideInBytes = sizeof(Vertex);
		m_PlaneVBView.SizeInBytes = vbSize;
	}

	// Index buffer (16-bit, upload for now)
	{
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

		ThrowIfFailed(m_Device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_PlaneIndexBuffer)));

		UINT8* pDataBegin = nullptr;
		CD3DX12_RANGE readRange(0, 0);

		ThrowIfFailed(m_PlaneIndexBuffer->Map(0, &readRange,
			reinterpret_cast<void**>(&pDataBegin)));
		memcpy(pDataBegin, planeIndices, ibSize);
		m_PlaneIndexBuffer->Unmap(0, nullptr);

		m_PlaneIBView.BufferLocation = m_PlaneIndexBuffer->GetGPUVirtualAddress();
		m_PlaneIBView.Format = DXGI_FORMAT_R32_UINT;
		m_PlaneIBView.SizeInBytes = ibSize;
	}
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
	m_AreaLightData.Position = XMFLOAT3(0.0f, 39.0f, 0.0f);
	m_AreaLightData.Radiance = XMFLOAT3(35.0f, 35.0f, 35.0f);
	m_AreaLightData.U = XMFLOAT3(10.0f, 0.0f, 0.0f);
	m_AreaLightData.V = XMFLOAT3(0.0f, 0.0f, 10.0f);

	float lenU = sqrtf(m_AreaLightData.U.x * m_AreaLightData.U.x +
		m_AreaLightData.U.y * m_AreaLightData.U.y +
		m_AreaLightData.U.z * m_AreaLightData.U.z);
	float lenV = sqrtf(m_AreaLightData.V.x * m_AreaLightData.V.x +
		m_AreaLightData.V.y * m_AreaLightData.V.y +
		m_AreaLightData.V.z * m_AreaLightData.V.z);

	m_AreaLightData.Area = lenU * lenV;

	m_AreaLightConstantBuffer = nv_helpers_dx12::CreateBuffer(
		m_Device.Get(), sizeof(m_AreaLightData), D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	uint8_t* pData;
	ThrowIfFailed(m_AreaLightConstantBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, (void*)&m_AreaLightData, sizeof(m_AreaLightData));
	m_AreaLightConstantBuffer->Unmap(0, nullptr);

}

void Renderer::CreatePerInstanceBuffers()
{

	m_PerInstanceCBs.resize(m_PerInstanceCBCount);

	int i(0);

	for (int i = 0; i < m_PerInstanceCBCount; i++)
	{
		const uint32_t bufferSize = sizeof(PerInstanceData);

		m_PerInstanceCBs[i] = nv_helpers_dx12::CreateBuffer(m_Device.Get(), bufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

		PerInstanceData data{};
		data.materialIndex = i;

		uint8_t* pData;
		ThrowIfFailed(m_PerInstanceCBs[i]->Map(0, nullptr, (void**)&pData));
		memcpy(pData, &data, bufferSize);
		m_PerInstanceCBs[i]->Unmap(0, nullptr);
	}

	m_MaterialsGPU.reserve(m_PerInstanceCBCount);

	for (auto& m : m_Materials)
	{
		MaterialDataGPU matGpu{};
		Material* mat = m.get();
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
		matGpu.isRefractive = mat->IsRefractive;
		matGpu.pad3 = { 0.0f, 0.0f };

		m_MaterialsGPU.push_back(std::move(matGpu));
	}

	const uint32_t bufferSize = m_MaterialsGPU.size() * sizeof(MaterialDataGPU);

	m_UploadCBuffer = nv_helpers_dx12::CreateBuffer(m_Device.Get(), bufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	uint8_t* pData;
	ThrowIfFailed(m_UploadCBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, m_MaterialsGPU.data(), bufferSize);
	m_UploadCBuffer->Unmap(0, nullptr);

}

void Renderer::CreateFrameIndexRNGCBuffer()
{
	const uint32_t bufferSize = sizeof(UINT);


	m_RNGUploadCBuffer = nv_helpers_dx12::CreateBuffer(m_Device.Get(), bufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	uint8_t* pData;
	ThrowIfFailed(m_RNGUploadCBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, &m_FrameIndex, bufferSize);
	m_RNGUploadCBuffer->Unmap(0, nullptr);
}

void Renderer::UpdateFrameIndexRNGCBuffer()
{
	const uint32_t bufferSize = sizeof(UINT);

	m_FrameIndex++;
	uint8_t* pData;

	m_RNGUploadCBuffer->Map(0, nullptr, (void**)&pData);
	memcpy(pData, &m_FrameIndex, bufferSize);
	m_RNGUploadCBuffer->Unmap(0, nullptr);
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

void Renderer::CreateModelBuffers(Model& model)
{
	const UINT vbSize = model.vertices.size() * sizeof(VertexObj);
	const UINT ibSize = model.indices.size()* sizeof(uint32_t);

	// Vertex buffer (upload for simplicity)
	{
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

		ThrowIfFailed(m_Device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_DragonVertexBuffer)));

		UINT8* pDataBegin = nullptr;
		CD3DX12_RANGE readRange(0, 0); // CPU will not read back

		ThrowIfFailed(m_DragonVertexBuffer->Map(0, &readRange,
			reinterpret_cast<void**>(&pDataBegin)));
		memcpy(pDataBegin, model.vertices.data(), vbSize);
		m_DragonVertexBuffer->Unmap(0, nullptr);

		m_DragonVBView.BufferLocation = m_DragonVertexBuffer->GetGPUVirtualAddress();
		m_DragonVBView.StrideInBytes = sizeof(VertexObj);
		m_DragonVBView.SizeInBytes = vbSize;
	}

	// Index buffer (16-bit, upload for now)
	{
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

		ThrowIfFailed(m_Device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_DragonIndexBuffer)));

		UINT8* pDataBegin = nullptr;
		CD3DX12_RANGE readRange(0, 0);

		ThrowIfFailed(m_DragonIndexBuffer->Map(0, &readRange,
			reinterpret_cast<void**>(&pDataBegin)));
		memcpy(pDataBegin, model.indices.data(), ibSize);
		m_DragonIndexBuffer->Unmap(0, nullptr);

		m_DragonIBView.BufferLocation = m_DragonIndexBuffer->GetGPUVirtualAddress();
		m_DragonIBView.Format = DXGI_FORMAT_R32_UINT;
		m_DragonIBView.SizeInBytes = ibSize;
	}
}
