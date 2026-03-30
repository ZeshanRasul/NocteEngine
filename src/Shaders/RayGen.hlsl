#include "Common.hlsl"
#include "PathTracerCommon.hlsl"

#define MaxLights 16

struct RLTransitionGPU
{
    uint StateIndex;
    uint ActionIndex;
    float Reward;
    uint Valid;
};

// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> gOutput : register(u0);
RWTexture2D<float4> gAccumBuf : register(u1);
RWTexture2D<float4> gNormal : register(u2);
RWTexture2D<float> gDepth : register(u3);
RWTexture2D<float> gPresent : register(u4);
RWStructuredBuffer<RLTransitionGPU> gRLTransitions : register(u5);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);
Texture2D<float4> gAccumHistory : register(t1);
StructuredBuffer<float> gQTable : register(t2);


cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4x4 gPrevViewProj;
    float3 gEyePosW;
    int SPP;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float cbPerObjectPad2;
    float cbPerObjectPad3;
    float4 gAmbientLight;
    int directPresent;
    int SamplingMode;
    float BSDFSampleProbability;
    float LightSampleProbability;

    int MaxBounces;
    int FrameIndex;
    int UseNEE;
    int cbPerObjectPad4;

    Light gLights[MaxLights];
};

cbuffer FrameData : register(b5)
{
    uint frameIndex;
}

cbuffer MediumParams : register(b6)
{
    float gSigmaA;
    float gSigmaS;
    float gSigmaT;
    int gUseFog;

    float gFogMaxDistance;
    float3 gFogPadding;
}

[numthreads(8, 8, 1)]
[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;
    
    uint linearIndex = DispatchRaysIndex().y * 1920 + DispatchRaysIndex().x;

    RLTransitionGPU record;
    record.StateIndex = stateIndex;
    record.ActionIndex = actionIndex;
    record.Reward = reward;
    record.Valid = 1;

    gRLTransitions[linearIndex] = record;
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

    // Capture first-hit guides for denoising
    float3 primaryNormal = float3(0, 0, 1);
    float primaryDepth = 1.0f;
    bool primarySet = false;
    float3 finalColor = 0.0f;
    float3 sppSum = 0.0f;
    int isemissive = 0;
    for (int s = 0; s < SPP; ++s)
    {
        seed += s * 374761393u; // change seed per sample
    // Initialize payload
        PathPayload payload;
        payload.radiance = 0.0f;
        payload.throughput = 1.0f;
        payload.depth = 0;
        payload.done = 0;
        payload.seed = Hash(seed + s * 9781u);
        payload.lastBounceWasDelta = 0;
        payload.prevBsdfPdf = 0.0f;
        payload.prevHitPos = originWS;
        payload.hitPos = originWS;
        payload.normal = float3(0.0f, 0.0f, 1.0f);
        payload.wi = dirWS;
        payload.bsdfOverPdf = 0.0f;
        payload.emission = 0.0f;
        payload.pdf = 1.0f;
        payload.isEmissive = 0.0f;
        RayDesc ray;
        ray.Origin = originWS;
        ray.Direction = dirWS;
        ray.TMin = 0.1f;
        ray.TMax = 1e38f;

        float3 finalRadiance = 0.0f;

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

            float travelDistance = payload.hitSomething ? payload.tHit : gFogMaxDistance;
            
            if (gUseFog != 0)
            {
                float T = ComputeTransmittance(gSigmaT, travelDistance);
                payload.throughput *= T + (1 - T) * float3(0.30f, 0.32f, 0.31f);
            }
            
        // If the ray missed or we decided to stop, accumulate emission and break
            finalRadiance += payload.throughput * payload.emission;
        
          // Store first-hit normal/depth once
            if (s == 0 && !primarySet)
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
            if (bounce >= 4)
            {
                float pCont = max(payload.throughput.x,
                           max(payload.throughput.y, payload.throughput.z));
                pCont = clamp(pCont, 0.05f, 0.95f);

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
        sppSum += finalRadiance;
        float3 viewPos = mul(float4(payload.hitPos, 1.0f), gView).xyz;
        primaryDepth = viewPos.z;
     //   finalColor = payload.emission;
        
        if (payload.isEmissive == 1)
        {
            isemissive = 1;
        }
        else
        {
            isemissive = 0;
        }
    }
   
    finalColor = sppSum / (float) SPP;
    
    float3 nEncoded = primarySet ? (primaryNormal * 0.5f + 0.5f) : float3(0.5f, 0.5f, 1.0f);
    gNormal[launchIndex] = float4(nEncoded, float(isemissive));

    gDepth[launchIndex] = primaryDepth;

// Progressive accumulation
    float3 accumColor;
    if (FrameIndex <= 1)
    {
        accumColor = finalColor;
    }
    else
    {
        float3 prevAccum = gAccumHistory[launchIndex].rgb;
        float n = (float) FrameIndex;
        accumColor = (((n - 1.0f) * prevAccum) + finalColor) / n;
    }
    
    gRLTransitions[linearIndex] = record;
    gAccumBuf[launchIndex] = float4(accumColor, 1.0f);
    gPresent[launchIndex] = float4(accumColor, 1.0f);
}

