#include "Common.hlsl"

struct ShadowHitInfo
{
    bool isHit;
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;
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
StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
RaytracingAccelerationStructure SceneBVH : register(t2);
StructuredBuffer<Material> materials : register(t3);


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

cbuffer PerInstance : register(b2)
{
    int materialIndex;
}

float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    
    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);
    
    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);
    
    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);
    
    float3 specularAlbedo = fresnelFactor * roughnessFactor;
    
    specularAlbedo = specularAlbedo / (specularAlbedo + 1.0f);
    
    return (mat.DiffuseAlbedo.rgb + specularAlbedo) * lightStrength;
//    return mat.DiffuseAlbedo.rgb  * lightStrength;
}

float3 ComputeDirectionalLight(Light L, float3 normal, float3 toEye, Material mat)
{
    float3 lightVec = -L.Direction;
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
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
    
    float3 lit = ComputeDirectionalLight(L, nObj, toEye, materials[materialIndex - 4]);

    payload.colorAndDistance = float4(lit, RayTCurrent());
    
    //uint iid = InstanceID();
    //uint mid = materialIndex; // from per-instance CB
    //float color = (mid + 1) / 4.0; // normalize 1..4

    //payload.colorAndDistance = float4(color, 0, 0, 1.0); // visualize material index
}

[shader("closesthit")]
void PlaneClosestHit(inout HitInfo payload, Attributes attrib)
{
    // Full barycentric triple
    float3 bary = float3(1.0f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    Light L = gLights[0];

    // Triangle index in this geometry
    const uint triIndex = PrimitiveIndex();
    const uint vbase = triIndex * 3;

    // Fetch triangle vertices (object space)
    STriVertex v0 = BTriVertex[indices[vbase + 0]];
    STriVertex v1 = BTriVertex[indices[vbase + 1]];
    STriVertex v2 = BTriVertex[indices[vbase + 2]];

    // Interpolate vertex normal in object space
    float3 nObj = normalize(v0.Normal * bary.x +
                            v1.Normal * bary.y +
                            v2.Normal * bary.z);

    // Transform normal to world space with inverse-transpose of Object->World
  //  nW = float3(0.0, 1.0, 0.0); // inverse-transpose

    
    // Hit position in world space
    float3 pW = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    // To-eye vector
    float3 toEye = normalize(gEyePosW - pW);

    // Normal Blinn-Phong lighting from your directional light
    float3 lit = ComputeDirectionalLight(L, nObj, toEye, materials[materialIndex - 4]);

    // Directional light: L.Direction is the direction the light shines.
    // Vector from surface toward the light is -L.Direction.
    float3 toLight = normalize(-L.Direction);

    // Shadow ray (world space)
    RayDesc shadowRay;
    shadowRay.Origin = pW + nObj * 0.001f; // bias to avoid self-shadowing
    shadowRay.Direction = toLight;
    shadowRay.TMin = 0.0f;
    shadowRay.TMax = 1e5f;

    ShadowHitInfo shadowPayload;
    shadowPayload.isHit = true; // assume occluded; miss will clear to false

    TraceRay(
        SceneBVH,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        /*InstanceInclusionMask*/ 0xff, // or 0x1 if you later mask out the plane
        /*RayContributionToHitGroupIndex*/ 1,
        /*MultiplierForGeometryContributionToHitGroupIndex*/ 1,
        /*MissShaderIndex*/ 1, // ShadowMiss (2nd miss in SBT)
        shadowRay,
        shadowPayload
    );

    // If we hit something between the plane and the light, we are in shadow
    float shadowFactor = shadowPayload.isHit ? 0.0f : 1.0f;

    float3 finalColor = lit * shadowFactor;

    payload.colorAndDistance = float4(finalColor, RayTCurrent());
    
    //uint iid = InstanceID();
    //uint mid = materialIndex; // from per-instance CB
    //float color = (mid + 1) / 4.0; // normalize 1..4

    //payload.colorAndDistance = float4(color, 0, 0, 1.0); // visualize material index

}
