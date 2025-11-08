#pragma once
#include "../Utils/d3dUtil.h"
#include "UploadBuffer.h"

using namespace DirectX;

class Renderer {
public:
	Renderer(HWND& windowHandle, UINT width, UINT height);
	~Renderer() = default;

	bool InitializeD3D12(HWND& windowHandle);
	bool Shutdown();
	void Update();
	void Draw();

private:
	void CreateDebugController();
	void CreateDevice();
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

	void CreateCbvDescriptorHeap();
	void CreateConstantBuffer();

	void CreateRootSignature();

	void BuildShadersAndInputLayout();

	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer() const;

	Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
	Microsoft::WRL::ComPtr<IDXGIAdapter> m_WarpAdapter;
	Microsoft::WRL::ComPtr<ID3D12Debug> m_DebugController;

	Microsoft::WRL::ComPtr<IDXGIFactory4> m_DxgiFactory;

	Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CbvHeap;

	Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
	static const int SwapChainBufferCount = 2;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_SwapChainBuffer[SwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthStencilBuffer;
	int m_CurrentBackBuffer = 0;

	D3D12_RECT m_ScissorRect;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBufferUploader = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_VbView;
	UINT64 m_VbByteSize = 0;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBufferUploader = nullptr;
	D3D12_INDEX_BUFFER_VIEW m_IbView;
	UINT64 m_IbByteSize = 0;

	struct ObjectConstants
	{
		XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
	};

	UINT m_CbufferElementByteSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_UploadCBuffer = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> m_ObjectCB = nullptr;


	UINT m_RtvDescriptorSize = 0;
	UINT m_DsvDescriptorSize = 0;
	UINT m_CbvSrvDescriptorSize = 0;

	UINT m_CurrentFence = 0;

	UINT m_4xMsaaQuality = 0;
	bool m_MsaaState = false;

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

	struct Vertex
	{
		DirectX::XMFLOAT3 Pos;
		DirectX::XMFLOAT4 Color;
	};

	std::array<Vertex, 8> vertices =
	{
		Vertex({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White) }),
		Vertex({ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black) }),
		Vertex({ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red) }),
		Vertex({ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green) }),
		Vertex({ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue) }),
		Vertex({ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow) }),
		Vertex({ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan) }),
		Vertex({ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta) })
	};

	std::array<std::uint16_t, 36> indices =
	{
		// front face
		0, 1, 2,
		0, 2, 3,

		// back face
		4, 6, 5,
		4, 7, 6,

		// left face
		4, 5, 1,
		4, 1, 0,

		// right face
		3, 2, 6,
		3, 6, 7,

		// top face
		1, 5, 6,
		1, 6, 2,

		// bottom face
		4, 0, 3,
		4, 3, 7
	};
};