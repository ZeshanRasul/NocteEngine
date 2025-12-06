#pragma once
#include "../Utils/d3dUtil.h"
#include "../../include/MathHelper.h"
#include "../../include/UploadBuffer.h"

using namespace DirectX;

struct DenoiseConstants
{
	float sigmaColor = 2.0f;
	float sigmaNormal = 128.0f;
	float sigmaDepth = 1.0f;
	int stepWidth = 1;
	XMFLOAT2 invResolution = { 0.0f, 0.0f };
	int pass = 0;
	int pad = 0;

};

struct ObjectConstants
{
	XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
};

struct PassConstants
{
	XMFLOAT4X4 View = MathHelper::Identity4x4();
	XMFLOAT4X4 InvView = MathHelper::Identity4x4();
	XMFLOAT4X4 Proj = MathHelper::Identity4x4();
	XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
	XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
	XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
	XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
	float cbPerObjectPad1 = 0.0f;
	XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
	XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
	float NearZ = 0.0f;
	float FarZ = 0.0f;
	float cbPerObjectPad2 = 0.0f;
	float cbPerObjectPad3 = 0.0f;
	XMFLOAT4 AmbientLight = { 0.2f, 0.2f, 1.0f, 1.0f };

	Light Lights[MaxLights];
};

struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Normal;
};

struct FrameResource
{
public:
	FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount);
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
	std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;

	UINT Fence = 0;
};

struct PostProcessData
{
	float Exposure = 1.0f;
	int ToneMapMode = 2;
	int DebugMode = 0;
	float pad;
};

struct AreaLight
{
	XMFLOAT3 Position;
	float Pad = 0.0f;
	XMFLOAT3 U;
	float Pad2 = 0.0f;
	XMFLOAT3 V;
	float Pad3 = 0.0f;
	XMFLOAT3 Radiance;
	float Area;
};

struct MaterialDataGPU
{
	DirectX::XMFLOAT4 DiffuseAlbedo;
	DirectX::XMFLOAT3 FresnelR0;
	float Ior;
	float Reflectivity;
	DirectX::XMFLOAT3 Absorption;
	float Shininess;
	float pad;
	float pad2;
	float metallic;
	bool isReflective = false;
};

struct RenderItem
{
	RenderItem() = default;

	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	int NumFramesDirty = gNumFrameResources;

	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};