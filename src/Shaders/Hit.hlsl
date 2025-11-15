#include "Common.hlsl"

struct ShadowHitInfo
{
    bool isHit;
};

struct ReflectionHitInfo
{
    float4 colorAndDistance;
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Ior;
    float Reflectivity;
    float3 Absorption;
    float Shininess;
    float pad;
    float pad1;
    float pad2;
    bool IsReflective;
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

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
RaytracingAccelerationStructure SceneBVH : register(t2);
StructuredBuffer<Material> materials : register(t3);
StructuredBuffer<STriVertex> CTriVertex : register(t4);
StructuredBuffer<int> cIndices : register(t5);


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
    
    float3 lit = ComputeDirectionalLight(L, nObj, toEye, materials[materialIndex]);

    payload.depth += 1;
    payload.eta = materials[materialIndex].Ior;
    if (payload.depth >= 5)
    {
        payload.colorAndDistance = float4(lit.xyz, RayTCurrent());
        return;
    }

    payload.colorAndDistance = float4(lit.xyz, RayTCurrent());
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

    
    float3 pW = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    float3 toEye = normalize(gEyePosW - pW);

    float3 lit = ComputeDirectionalLight(L, nObj, toEye, materials[materialIndex]);

    float3 toLight = normalize(-L.Direction);

    // Shadow ray (world space)
    RayDesc shadowRay;
    shadowRay.Origin = pW + nObj * 0.001f; // bias to avoid self-shadowing
    shadowRay.Direction = toLight;
    shadowRay.TMin = 0.0f;
    shadowRay.TMax = 1e5f;

    ShadowHitInfo shadowPayload;
    shadowPayload.isHit = true;

    TraceRay(
        SceneBVH,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        /*InstanceInclusionMask*/ 0xff,
        /*RayContributionToHitGroupIndex*/ 1,
        /*MultiplierForGeometryContributionToHitGroupIndex*/ 1,
        /*MissShaderIndex*/ 1, // ShadowMiss (2nd miss in SBT)
        shadowRay,
        shadowPayload
    );

    // If we hit something between the plane and the light, we are in shadow
    float shadowFactor = shadowPayload.isHit ? 0.3f : 1.0f;

    float3 finalColor = lit * shadowFactor;

    payload.depth += 1;
    payload.eta = materials[materialIndex].Ior;
    if (payload.depth >= 5)
    {
        payload.colorAndDistance = float4(payload.colorAndDistance.xyz, RayTCurrent());
        return;
    }
    
    payload.colorAndDistance = float4(finalColor, RayTCurrent());
    
}

[shader("closesthit")]
void ReflectionClosestHit(inout HitInfo payload, Attributes attrib)
{
      // Triangle index in this geometry
    const uint triIndex = PrimitiveIndex();
    const uint vbase = triIndex * 3;

    // Fetch the triangle’s vertices (object space)
    STriVertex v0 = CTriVertex[cIndices[vbase + 0]];

    STriVertex v1 = CTriVertex[cIndices[vbase + 1]];

    STriVertex v2 = CTriVertex[cIndices[vbase + 2]];

    // Full barycentric triple
    float3 bary = float3(1.0f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    // Interpolate vertex normal in object space
    float3 nObj = normalize(v0.Normal * bary.x + v1.Normal * bary.y + v2.Normal * bary.z);
    float3 N = normalize(mul((float3x3) ObjectToWorld3x4(), nObj));
    float3 wo = -normalize(WorldRayDirection());
    
    Material mat = materials[materialIndex];
    
    float3 Nf = N;
    
    float eta_i = payload.eta;
    float eta_t = mat.Ior;
    
    if (dot(wo, N) < 0.0f)
    {
        Nf = -N;
        eta_i = mat.Ior;
        eta_t = 1.0f;
    }
    
    float eta = eta_i / eta_t;
    
    // Hit position in world space (from the ray)
    float3 pW = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    float3 incidentRay = WorldRayDirection();
    HitInfo refrPayload = payload;
    HitInfo reflectionPayload = payload;
    
    refrPayload.depth++;
    payload.eta = materials[materialIndex].Ior;

    
    
    
    float3 reflectionDir = incidentRay - (2 * (dot(incidentRay, Nf) * Nf));
    
    float cosThetaI = dot(-wo, Nf);
    float sin2ThetaI = max(0.0f, 1.0f - cosThetaI * cosThetaI);
    float sin2ThetaT = eta * eta * sin2ThetaI;

    float3 refractDir;
    bool totalInternalReflection = (sin2ThetaT > 1.0f);

    if (!totalInternalReflection)
    {
        refractDir = refract(-wo, Nf, eta);
        refractDir = normalize(refractDir);
    }
    
    // Update medium for refracted ray
    refrPayload.eta = eta_t;


    
    // To-eye vector (world)
    float3 toEye = normalize(gEyePosW - pW);

    Light L = gLights[0];
    float3 lit = ComputeDirectionalLight(L, nObj, toEye, materials[materialIndex]);

    if (refrPayload.depth >= 5)
    {
        payload.colorAndDistance = float4(payload.colorAndDistance.xyz, RayTCurrent());
        return;
    }
    reflectionPayload.depth++;
    if (reflectionPayload.depth >= 5)
    {
        payload.colorAndDistance = float4(lit, RayTCurrent());
        return;
    }
    
    // Shadow ray (world space)
    RayDesc reflectionRay;
    reflectionRay.Origin = pW + reflectionDir * 0.001f; // bias to avoid self-shadowing
    reflectionRay.Direction = reflectionDir;
    reflectionRay.TMin = 0.0f;
    reflectionRay.TMax = 1e5f;


    TraceRay(
        SceneBVH,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        /*InstanceInclusionMask*/ 0xff,
    /*RayContributionToHitGroupIndex*/ 6,
        /*MultiplierForGeometryContributionToHitGroupIndex*/ 1,
        /*MissShaderIndex*/ 0,
        reflectionRay,
        reflectionPayload
    );
    payload.depth += 1;
    payload.eta = materials[materialIndex].Ior;
    if (payload.depth >= 5)
    {
        payload.colorAndDistance = float4(payload.colorAndDistance.xyz, RayTCurrent());
        return;
    }
    
    float tMin = 0.001f;
    float tMax = 1e27f;

    if (!totalInternalReflection)
    {
        
    
        RayDesc refractionRay;
        refractionRay.Origin = pW + refractDir * 0.01f; // bias to avoid self-shadowing
        refractionRay.Direction = refractDir;
        refractionRay.TMin = tMin;
        refractionRay.TMax = tMax;

    
        TraceRay(
            SceneBVH,
            RAY_FLAG_NONE,
            0xff,
            6,
            1,
            0,
    refractionRay,
    refrPayload
        );
    }
    
    float3 finalColor = lit + reflectionPayload.colorAndDistance.xyz;
    
    if (refrPayload.colorAndDistance.w < tMax)
    {
    }
        finalColor += refrPayload.colorAndDistance.xyz;
    
    

    payload.colorAndDistance = float4(finalColor, RayTCurrent());
}