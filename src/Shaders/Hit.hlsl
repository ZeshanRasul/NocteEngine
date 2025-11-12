#include "Common.hlsl"

struct ShadowHitInfo
{
    bool isHit;
};

#define MaxLights 16
struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};

struct STriVertex
{
    float3 Vertex;
    float3 Normal;
};

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t2);
StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);


cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float cbPerObjectPad2;
    float cbPerObjectPad3;
    float4 gAmbientLight;
    
    Light gLights[MaxLights];
};

cbuffer Colors : register(b1)
{
    float3 A[3];
    float3 B[3];
    float3 C[3];
}

float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);
    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye)
{
    const float m = 0.6 * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);
    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(0.3, halfVec, lightVec);
    float3 specularAlbedo = fresnelFactor * roughnessFactor;
    specularAlbedo = specularAlbedo / (specularAlbedo + 1.0f);
    return (float3(0.8, 0.7, 0.8) + specularAlbedo) * lightStrength;
}

float3 ComputeDirectionalLight(Light L, float3 normal, float3 toEye)
{
    float3 lightVec = -L.Direction;
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    return BlinnPhong(lightStrength, lightVec, normal, toEye);
}

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
      // Triangle index in this geometry
    const uint triIndex = PrimitiveIndex();
    const uint vbase = triIndex * 3;

    // Fetch the triangle’s vertices (object space)
    STriVertex v0 = BTriVertex[indices[vbase + 0]];

    STriVertex v1 = BTriVertex[indices[vbase + 1]];

    STriVertex v2 = BTriVertex[indices[vbase + 2]];

    // Full barycentric triple
    float3 bary = float3(1.0f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    // Interpolate vertex normal in object space
    float3 nObj = normalize(v0.Normal * bary.x + v1.Normal * bary.y + v2.Normal * bary.z);

    // Transform normal to world space with inverse-transpose of Object->World
    float3x3 w2o = (float3x3) WorldToObject3x4(); // 3x3
    float3x3 o2w = (float3x3) ObjectToWorld3x4(); // 3x3
    float3 nW = normalize(mul(nObj, transpose(w2o))); // inverse-transpose

    // Hit position in world space (from the ray)
    float3 pW = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    // To-eye vector (world)
    float3 toEye = normalize(gEyePosW - pW);

    Light L = gLights[0];
    
    float4 modulationFactor = gAmbientLight;
    
    if (InstanceID() < 3)
    {
        float3 hitColor = A[InstanceID()] * bary.x + B[InstanceID()] * bary.y + C[InstanceID()] * bary.z;
        modulationFactor = float4(hitColor.xyz, 1.0f);
    };
    
    float3 lit = ComputeDirectionalLight(L, nW, toEye);

    payload.colorAndDistance = float4(lit, RayTCurrent());
}

[shader("closesthit")]
void PlaneClosestHit(inout HitInfo payload, Attributes attrib)
{
    float3 bary = float3(1.0f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    Light L = gLights[0];

    //float3 lightPos = float3(0.0f, 200.0f, 0.0f);
    float3 lightPos = L.Direction;
    
    float3 worldOrigin = WorldRayOrigin() + (RayTCurrent() - 0.1) * WorldRayDirection();
    
   // float3 lightDir = normalize(lightPos - worldOrigin);
    float3 lightDir = normalize(lightPos);
    

    
       // Triangle index in this geometry
    const uint triIndex = PrimitiveIndex();
    const uint vbase = triIndex * 3;

    // Fetch the triangle’s vertices (object space)
    STriVertex v0 = BTriVertex[vbase + 2];

    STriVertex v1 = BTriVertex[vbase + 1];

    STriVertex v2 = BTriVertex[vbase + 0];
    
    
    // Interpolate vertex normal in object space
    float3 nObj = normalize(v0.Normal * bary.x + v1.Normal * bary.y + v2.Normal * bary.z);

    // Transform normal to world space with inverse-transpose of Object->World
    float3x3 w2o = (float3x3) WorldToObject3x4(); // 3x3
    float3x3 o2w = (float3x3) ObjectToWorld3x4(); // 3x3
    float3 nW = normalize(mul(nObj, transpose(w2o))); // inverse-transpose

    // Hit position in world space (from the ray)
    float3 pW = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    // To-eye vector (world)
    float3 toEye = normalize(gEyePosW - pW);

    float3 lit = ComputeDirectionalLight(L, nW, toEye);

    RayDesc ray;
    ray.Origin = worldOrigin;
    ray.Direction = lightDir;
    ray.TMin = 0.1f;
    ray.TMax = 100000.0f;
    bool hit = true;
    ShadowHitInfo shadowPayload;
    shadowPayload.isHit = false;
    
    TraceRay(
        SceneBVH,
        RAY_FLAG_NONE,
        0xFF,
        1,
        0,
        1,
        ray,
        shadowPayload);
    
    float factor = shadowPayload.isHit ? 0.0 : 1.0;

   // float4 hitColor = shadowPayload.isHit ? float4(float3(0.0, 1.0, 0.0), RayTCurrent()) : float4(float3(0.0, 0.0, 1.0), RayTCurrent());
    float4 hitColor = float4(lit * factor, RayTCurrent());
    
    payload.colorAndDistance = float4(hitColor);

}