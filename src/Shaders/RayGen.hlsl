#include "Common.hlsl"
#include "PathTracerCommon.hlsl"

#define MaxLights 16

// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> gOutput : register(u0);
RWTexture2D<float4> gAccumBuf : register(u1);
RWTexture2D<float4> gNormal : register(u2);
RWTexture2D<float> gDepth : register(u3);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);
Texture2D<float4> gAccumHistory : register(t1);

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



cbuffer FrameData : register(b5)
{
    uint frameIndex;
}

[numthreads(8, 8, 1)]
[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;

    float2 pixel = (float2) DispatchRaysIndex() + 0.5f;
    float2 ndc = pixel / float2(DispatchRaysDimensions().xy);
    ndc = ndc * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 pClip = float4(ndc, 1, 1);
    float4 pView = mul(pClip, gInvProj);
    pView /= pView.w;

    float3 originWS = gEyePosW;
    float3 dirWS = normalize(mul(float4(pView.xyz, 0), gInvView).xyz);

    uint seed = launchIndex.x * 1973u ^
            launchIndex.y * 9277u ^
            frameIndex * 26699u;

    seed ^= (launchIndex.x + launchIndex.y) * 1013904223u;

    // Initialize payload
    PathPayload payload;
    payload.radiance = 0.0f;
    payload.throughput = 1.0f;
    payload.depth = 0;
    payload.done = 0;
    payload.seed = seed;
    
    RayDesc ray;
    ray.Origin = originWS;
    ray.Direction = dirWS;
    ray.TMin = 0.1f;
    ray.TMax = 1e38f;

    float3 finalRadiance = 0.0f;

    // Capture first-hit guides for denoising
    float3 primaryNormal = float3(0, 0, 1);
    float primaryDepth = 1.0f;
    bool primarySet = false;

    
    const int MaxBounces = 12;

    for (int bounce = 0; bounce < MaxBounces; ++bounce)
    {
        payload.done = 0;
        payload.emission = 0.0f;
        payload.bsdfOverPdf = 0.0f;
        payload.pdf = 1.0f;

        TraceRay(
            SceneBVH,
            RAY_FLAG_NONE,
            0xFF,
            0, // ray contribution index
            2, // multiplier for geometry contribution
            0, // miss shader index
            ray,
            payload
        );

        // If the ray missed or we decided to stop, accumulate emission and break
        finalRadiance += payload.throughput * payload.emission;
        
          // Store first-hit normal/depth once
        if (!primarySet)
        {
            primaryNormal = payload.normal; // in [-1,1]
            primaryDepth = length(payload.hitPos - gEyePosW); // world units
            primarySet = true;
        }
        
        if (payload.done != 0)
            break;

        // Update throughput: multiply by f * cos / pdf
        payload.throughput *= payload.bsdfOverPdf;
        
        // Russian roulette after a few bounces
        if (bounce >= 3)
        {
            float pCont = max(payload.throughput.x,
                           max(payload.throughput.y, payload.throughput.z));
            pCont = saturate(pCont);

            if (pCont < 1e-3f)
                break;

            float r = Rand(payload.seed);
            if (r > pCont)
                break;

            payload.throughput /= pCont;
        }
        float3 offsetDir = (dot(payload.wi, payload.normal) > 0.0f)
         ? payload.normal   // going to the “outside” side of the surface
         : -payload.normal; // going inside
        ray.Origin = payload.hitPos + offsetDir * 0.001f;
        ray.Direction = normalize(payload.wi);
        ray.TMin = 0.001f;
        ray.TMax = 1e38f;
    }
    
    float maxRadianceLum = 50.0f; // try 20–100 depending on your scene
    float lum = dot(finalRadiance, float3(0.2126, 0.7152, 0.0722));
    if (lum > maxRadianceLum)
    {
        finalRadiance *= maxRadianceLum / lum;
    }
    
    float3 finalColor = finalRadiance;
     
    float4 prev = gAccumHistory[launchIndex];
    float4 current = float4(finalColor, 1.0f);
    
    float blend = (frameIndex == 0) ? 1.0f : 0.1f;

    float3 nEncoded = primarySet ? (primaryNormal * 0.5f + 0.5f) : float3(0.5f, 0.5f, 1.0f);
    gNormal[launchIndex] = float4(nEncoded, 1.0f);
    
    // Linear depth normalized to [0,1]
    float d = primarySet ? (primaryDepth / gFarZ) : 1.0f;
    gDepth[launchIndex] = saturate(d);
    
    gAccumBuf[launchIndex] = lerp(prev, current, blend);
}

