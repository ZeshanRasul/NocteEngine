#include "Common.hlsl"
#include "PathTracerCommon.hlsl"

#define MaxLights 16

// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> gOutput : register(u0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

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

    // Initialize payload
    PathPayload payload;
    payload.radiance = 0.0f;
    payload.throughput = 1.0f;
    payload.depth = 0;
    payload.done = 0;
    payload.seed =
        (launchIndex.x * 73856093u) ^
        (launchIndex.y * 19349663u) ^
        (frameIndex * 83492791u);

    RayDesc ray;
    ray.Origin = originWS;
    ray.Direction = dirWS;
    ray.TMin = 0.1f;
    ray.TMax = 1e38f;

    float3 finalRadiance = 0.0f;

    const int MaxBounces = 8;

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
            3, // multiplier for geometry contribution
            0, // miss shader index
            ray,
            payload
        );

        // If the ray missed or we decided to stop, accumulate emission and break
        finalRadiance += payload.throughput * payload.emission;
 //       finalRadiance += payload.throughput;

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

      //  finalRadiance += payload.throughput;
        
        // Set up next ray
        ray.Origin = payload.hitPos + payload.normal * 0.001f;
        ray.Direction = normalize(payload.wi);
        ray.TMin = 0.001f;
        ray.TMax = 1e38f;
    }

    float3 finalColor = PostProcessColor(finalRadiance);
    gOutput[launchIndex] = float4(finalColor, 1.0f);
}

