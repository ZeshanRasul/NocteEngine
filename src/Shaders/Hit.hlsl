#include "Common.hlsl"
#include "MicrofacetBRDFUtils.hlsl"

#define NumLights 1

struct ShadowHitInfo
{
    bool isHit;
    uint depth;
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
    float metallic;
    bool IsReflective;
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

bool BuildLightSample(
    int lightIndex,
    float3 P,
    float3 N,
    out float3 L,
    out float3 Li,
    out float NdotL)
{
    Light light = gLights[lightIndex];

    L = 0;
    Li = 0;
    NdotL = 0;

    bool isDirectional = true;

    if (isDirectional)
    {
        float3 D = normalize(light.Direction);
        L = -D;

        NdotL = saturate(dot(N, L));
        if (NdotL <= 0)
            return false;

        Li = light.Strength;
        return true;
    }

    // Point or spot light:
    float3 toLight = light.Position - P;
    float distSq = max(dot(toLight, toLight), 1e-6);
    float dist = sqrt(distSq);

    L = toLight / dist;
    NdotL = saturate(dot(N, L));
    if (NdotL <= 0)
        return false;

    // Inverse-square attenuation
    float invSq = 1.0 / distSq;

    float rangeAtt = 1.0;
    if (light.FalloffEnd > light.FalloffStart)
    {
        rangeAtt = saturate((light.FalloffEnd - dist) /
                            (light.FalloffEnd - light.FalloffStart));
    }

    Li = light.Strength * invSq * rangeAtt;

    if (light.SpotPower > 0)
    {
        float3 spotDir = normalize(-light.Direction);
        float cosAngle = saturate(dot(L, spotDir));
        float spotFactor = pow(cosAngle, light.SpotPower);
        if (spotFactor <= 0)
            return false;
        Li *= spotFactor;
    }

    return true;
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
    float3 V = normalize(gEyePosW - pW);
    float3 N = normalize(mul((float3x3) ObjectToWorld3x4(), nObj));

    
    float3 Lo = 0.0;
    
    for (int i = 0; i < 1; ++i)
    {
        float3 L;
        float3 Li = 0.0;
        float NdotL = 0.0;
        
        if (!BuildLightSample(i, pW, N, L, Li, NdotL))
            continue;
        
        float3 H = normalize(V + L);
        float NdotV = saturate(dot(N, V));
        float NdotH = saturate(dot(N, H));
        float LdotH = saturate(dot(L, H));
        
        Material mat = materials[materialIndex];
        
        float roughness = 1 - mat.Shininess;
        
        float3 Cd;
        float3 F0;
        ComputeDisneyMetalWorkflow(mat.DiffuseAlbedo.xyz, mat.metallic, Cd, F0);
        
        float3 F = Fresnel_Schlick(F0, LdotH);
        float D = GGX_D(NdotH, roughness);
        float G = GGX_G_Smith(NdotV, NdotL, roughness);
        
        float denom = max(4.0 * NdotL * NdotV, 1e-4);
        float3 specBRDF = (D * G * F) / denom;
        
        float diffBRDF = DisneyDiffuse(NdotV, NdotL, LdotH, roughness);
        float3 diffTerm = Cd * diffBRDF;

        float3 f = diffTerm + specBRDF;

        Lo += f * Li * NdotL; 
    }
    
    float3 radiance = 0.0;
    radiance += Lo;

    payload.depth += 1;
    
    payload.eta = materials[materialIndex].Ior;
    if (payload.depth >= 5)
    {
        payload.colorAndDistance = float4(radiance, RayTCurrent());
        return;
    }

    payload.colorAndDistance = float4(radiance, RayTCurrent());
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
    shadowPayload.depth = payload.depth;

    TraceRay(
        SceneBVH,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        /*InstanceInclusionMask*/ 0xff,
        /*RayContributionToHitGroupIndex*/ 1,
        /*MultiplierForGeometryContributionToHitGroupIndex*/ 3,
        /*MissShaderIndex*/ 1, // ShadowMiss (2nd miss in SBT)
        shadowRay,
        shadowPayload
    );

    // If we hit something between the plane and the light, we are in shadow
    float shadowFactor = shadowPayload.isHit ? 0.3f : 1.0f;

    float3 finalColor = lit * shadowFactor;

    payload.depth++;
    payload.eta = materials[materialIndex].Ior;
    if (shadowPayload.depth >= 5 || payload.depth >= 5)
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
    STriVertex v0 = BTriVertex[indices[vbase + 0]];

    STriVertex v1 = BTriVertex[indices[vbase + 1]];

    STriVertex v2 = BTriVertex[indices[vbase + 2]];

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
    payload.eta = eta_t;


    
    // To-eye vector (world)
    float3 toEye = normalize(gEyePosW - pW);

    Light L = gLights[0];
    float3 lit = ComputeDirectionalLight(L, nObj, toEye, materials[materialIndex]);
    payload.depth += 1;
    payload.eta = materials[materialIndex].Ior;

    if (payload.depth >= 3)
    {
        payload.colorAndDistance = float4(payload.colorAndDistance.xyz, RayTCurrent());
        return;
    }
    reflectionPayload.depth++;
    if (reflectionPayload.depth >= 3)
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
    /*RayContributionToHitGroupIndex*/ 2,
        /*MultiplierForGeometryContributionToHitGroupIndex*/ 3,
        /*MissShaderIndex*/ 0,
        reflectionRay,
        reflectionPayload
    );
    if (reflectionPayload.depth >= 3)
    {
        payload.colorAndDistance = float4(payload.colorAndDistance.xyz, RayTCurrent());
        return;
    }
    if (refrPayload.depth >= 3)
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
            0,
            3,
            0,
    refractionRay,
    refrPayload
        );
    }
    
    float3 finalColor = reflectionPayload.colorAndDistance.xyz;
    
    if (refrPayload.colorAndDistance.w < tMax)
    {
        finalColor += refrPayload.colorAndDistance.xyz;
    }
    
    

    payload.colorAndDistance = float4(finalColor, RayTCurrent());
}