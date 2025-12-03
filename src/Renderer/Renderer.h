#pragma once
#include <vector>

#include "../Utils/d3dUtil.h"
#include "../Utils/GeometryGenerator.h"
#include "UploadBuffer.h"
#include "FrameResource.h"
#include "../Camera.h"
#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"
#include <dxcapi.h>

using namespace DirectX;

class Renderer {
public:
	Renderer(HWND& windowHandle, UINT width, UINT height);
	~Renderer();

	bool InitializeD3D12(HWND& windowHandle);
	bool Shutdown();
	void Update(float dt, Camera& cam);
	void Draw(bool useRaster);

private:
	void CreateDebugController();
	void CreateDevice();
	void CheckRaytracingSupport();
	void CreateFence();
	void GetDescriptorSizes();
	void CheckMSAAQuality();
	void CreateCommandObjects();
	void CreateSwapChain(HWND& hwnd);
	void CreateRtvAndDsvDescriptorHeaps();
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;
	void CreateRenderTargetView();
	void CreateDepthStencilView();

	void CreateVertexBuffer();
	void CreateVertexBufferView();

	void CreateIndexBuffer();
	void CreateIndexBufferView();

	void CreateCbvDescriptorHeaps();
	void CreateConstantBufferViews();

	void CreateRootSignature();

	void BuildShadersAndInputLayout();

	void BuildPSOs();

	void BuildMaterials();
	void BuildShapeGeometry();
	void BuildSkullGeometry();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& riItems);

	void BuildFrameResources();
	void UpdateObjectCBs();
	void UpdateMaterialCBs();
	void UpdateMainPassCB();

	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer() const;

	Microsoft::WRL::ComPtr<ID3D12Device5> m_Device;
	Microsoft::WRL::ComPtr<IDXGIAdapter> m_WarpAdapter;
	Microsoft::WRL::ComPtr<ID3D12Debug> m_DebugController;

	Microsoft::WRL::ComPtr<IDXGIFactory4> m_DxgiFactory;

	Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> m_CommandList;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CbvHeap;

	Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
	static const int SwapChainBufferCount = 2;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_SwapChainBuffer[SwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthStencilBuffer;
	int m_CurrentBackBuffer = 0;

	D3D12_RECT m_ScissorRect;

	Microsoft::WRL::ComPtr<ID3DBlob> m_VertexBufferCPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBufferUploader = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_VbView;
	UINT64 m_VbByteSize = 0;

	Microsoft::WRL::ComPtr<ID3DBlob> m_IndexBufferCPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBufferUploader = nullptr;
	D3D12_INDEX_BUFFER_VIEW m_IbView;
	UINT64 m_IbByteSize = 0;

	UINT m_CbufferElementByteSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_UploadCBuffer = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> m_ObjectCB = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_RNGUploadCBuffer = nullptr;
	UINT m_PassCbvOffset;

	UINT m_RtvDescriptorSize = 0;
	UINT m_DsvDescriptorSize = 0;
	UINT m_CbvSrvUavDescriptorSize = 0;

	UINT m_CurrentFence = 0;

	UINT m_4xMsaaQuality = 0;
	bool m_MsaaState = false;

	bool m_IsWireframe = false;

	DXGI_FORMAT m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	HWND& m_Hwnd;
	UINT m_ClientWidth;
	UINT m_ClientHeight;


	Microsoft::WRL::ComPtr<ID3DBlob> m_VsByteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> m_PsByteCode;
	std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputLayoutDescs;

	XMFLOAT4X4 m_World = MathHelper::Identity4x4();
	XMFLOAT4X4 m_View = MathHelper::Identity4x4();
	XMFLOAT4X4 m_Proj = MathHelper::Identity4x4();

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects;

	float m_Theta = 1.5f * DirectX::XM_PI;
	float m_Phi = DirectX::XM_PIDIV4;
	float m_Radius = 5.0f;
	D3D12_VIEWPORT vp;

	static const int NumFrameResources = 3;
	std::vector<std::unique_ptr<FrameResource>> m_FrameResources;
	FrameResource* m_CurrentFrameResource = nullptr;
	int m_CurrentFrameResourceIndex = 0;
	XMFLOAT3 m_EyePos;

	std::vector<std::unique_ptr<RenderItem>> m_AllRenderItems;

	std::vector<RenderItem*> m_OpaqueRenderItems;
	std::vector<RenderItem*> m_TransparentRenderItems;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> m_Geometries;
	std::unordered_map<std::string, std::shared_ptr<Material>> m_Materials;

	PassConstants m_MainPassCB;
	UINT m_skullVertCount = 0;

	////////////////
	/// DXR
	////////////////

	struct AccelerationStructureBuffers
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> pScratch;
		Microsoft::WRL::ComPtr<ID3D12Resource> pResult;
		Microsoft::WRL::ComPtr<ID3D12Resource> pInstanceDesc;
	};

	Microsoft::WRL::ComPtr<ID3D12Resource> m_BottomLevelAS;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_PlaneBottomLevelAS;

	nv_helpers_dx12::TopLevelASGenerator m_topLevelASGenerator;
	AccelerationStructureBuffers m_topLevelASBuffers;
	std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> m_Instances;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRayGenSignature();
	Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateHitSignature();
	Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateMissSignature();

	void CreateRaytracingPipeline();

	Microsoft::WRL::ComPtr<IDxcBlob> m_RayGenLibrary;
	Microsoft::WRL::ComPtr<IDxcBlob> m_HitLibrary;
	Microsoft::WRL::ComPtr<IDxcBlob> m_MissLibrary;
	Microsoft::WRL::ComPtr<IDxcBlob> m_ShadowLibrary;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RayGenSignature;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_HitSignature;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_MissSignature;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_ShadowSignature;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_ReflectionSignature;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_SphereSignature;

	Microsoft::WRL::ComPtr<ID3D12StateObject> m_RtStateObject;
	Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> m_RtStateObjectProps;

	void CreateRaytracingOutputBuffer();
	void CreateShaderResourceHeap();

	void CreateAccumulationBuffer();

	Microsoft::WRL::ComPtr<ID3D12Resource> m_OutputResource;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_AccumulationBuffer;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvUavHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_ImguiHeap;

	/// <summary>
	///  Denoising
	/// </summary>
	
	void CreateDenoisingResources();

	Microsoft::WRL::ComPtr<ID3D12Resource> m_NormalTex;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthTex;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_DenoisePing;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_DenoisePong;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_DenoiseCB;

	void CreateComputeRootSignature();
	void CreateComputePipelineStateObjects();
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_DenoiseRootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_DenoisePSO;
	Microsoft::WRL::ComPtr<ID3DBlob> m_CsByteCode;
	void CreateComputeShaderResourceHeap();
	D3D12_GPU_DESCRIPTOR_HANDLE m_ComputeSrvHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE m_ComputeUavHandle;

	void CreatePresentUAV();
	Microsoft::WRL::ComPtr<ID3D12Resource> m_PresentUAV;

	int m_DenoiseStep = 1;

	void CreateDenoiseConstantBuffer();
	void UpdateDenoiseConstantBuffer(int step);

	void CreateShaderBindingTable();

	nv_helpers_dx12::ShaderBindingTableGenerator m_SbtHelper;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_SbtStorage;

	std::pair< Microsoft::WRL::ComPtr<ID3D12Resource>, uint32_t> rtVerts;
	std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, uint32_t>> m_BlasVertInput;
	AccelerationStructureBuffers CreateBottomLevelAS(std::vector <std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers, std::vector <std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers);
	void CreateTopLevelAS(std::vector <std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances, bool updateOnly = false);
	void CreateAccelerationStructures();
	UINT m_vertexCount = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> skullUB;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_PlaneBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_PlaneBufferView;

	void CreatePlaneVB();

	void CreateCameraBuffer();
	void UpdateCameraBuffer();

	Microsoft::WRL::ComPtr<ID3D12Resource> m_CameraBuffer;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_ConstHeap;
	uint32_t m_CameraBufferSize = 0;
	UINT camBufOffset;
	float m_AspectRatio;

	void OnMouseMove(WPARAM btnState, int x, int y);

	void CreateGlobalConstantBuffer();
	Microsoft::WRL::ComPtr<ID3D12Resource> m_GlobalConstantBuffer;

	void CreatePostProcessConstantBuffer();
	PostProcessData m_PostProcessData;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_PostProcessConstantBuffer;

	void CreateAreaLightConstantBuffer();
	AreaLight m_AreaLightData;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_AreaLightConstantBuffer;

	uint32_t m_AnimationCounter = 0;

	void CreatePerInstanceBuffers();
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_PerInstanceCBs;
	UINT m_PerInstanceCBCount = 6;
	UINT m_SkullCount = 4;
	UINT m_SphereCount = 1;

	void CreateFrameIndexRNGCBuffer();
	void UpdateFrameIndexRNGCBuffer();
	Microsoft::WRL::ComPtr<ID3D12Resource> m_FrameIndexCB;
	UINT m_FrameIndex = 0;

	std::vector<MaterialDataGPU> m_MaterialsGPU;

	SubmeshGeometry boxSubmesh;
	SubmeshGeometry sphereSubmesh;

	bool showWindow = true;
	void CreateImGuiDescriptorHeap();
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_ImGuiSrvHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE imguiCpuStart;
	D3D12_GPU_DESCRIPTOR_HANDLE imguiGpuStart;
	std::vector<bool> m_IsInstanceReflective;
};

struct PerInstanceData
{
	int materialIndex;
	float pad[3];
};