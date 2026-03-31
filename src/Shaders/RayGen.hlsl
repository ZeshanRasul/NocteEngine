#include "Common.hlsl"
#include "PathTracerCommon.hlsl"

#define MaxLights 16
#define NUM_ACTIONS 3

struct RLTransitionGPU
{
    uint StateIndex;
    uint ActionIndex;
    float Reward;
    uint NextStateIndex;
    uint Valid;
    uint Terminated;
};

struct RLQValue
{
    float Value;
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
StructuredBuffer<RLQValue> gQTable : register(t2);

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
    int gUseRL;

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

uint ChooseBestAction(uint stateIndex)
{
    uint baseIdx = stateIndex * NUM_ACTIONS;

    float q0 = gQTable[baseIdx + 0].Value;
    float q1 = gQTable[baseIdx + 1].Value;
    float q2 = gQTable[baseIdx + 2].Value;

    uint bestAction = 0;
    float bestQ = q0;

    if (q1 > bestQ)
    {
        bestQ = q1;
        bestAction = 1;
    }

    if (q2 > bestQ)
    {
        bestQ = q2;
        bestAction = 2;
    }

    return bestAction;
}

float HashToUnitFloat(uint x)
{
    x ^= x >> 17;
    x *= 0xed5ad4bb;
    x ^= x >> 11;
    x *= 0xac4c1b51;
    x ^= x >> 15;
    x *= 0x31848bab;
    x ^= x >> 14;
    return (x & 0x00FFFFFF) / 16777216.0f;
}

uint ChooseBestActionWithTieBreak(uint stateIndex, uint pixel, uint frameIndex)
{
    uint baseIdx = stateIndex * NUM_ACTIONS;

    float q0 = gQTable[baseIdx + 0].Value;
    float q1 = gQTable[baseIdx + 1].Value;
    float q2 = gQTable[baseIdx + 2].Value;

    float maxQ = max(q0, max(q1, q2));

    uint candidates[3];
    uint count = 0;

    if (q0 == maxQ)
        candidates[count++] = 0;
    if (q1 == maxQ)
        candidates[count++] = 1;
    if (q2 == maxQ)
        candidates[count++] = 2;

    float r = HashToUnitFloat(pixel + 7919u * frameIndex);
    uint pick = min((uint) (r * count), count - 1);

    return candidates[pick];
}

uint ChooseActionEpsilonGreedy(uint stateIndex, uint pixel, float epsilon)
{
    float r = HashToUnitFloat(pixel + frameIndex * 9781);

    if (r < epsilon)
    {
        return pixel % NUM_ACTIONS;
    }

    return ChooseBestActionWithTieBreak(stateIndex, pixel, frameIndex);
}

uint BucketizeBounce(uint bounce)
{
    if (bounce <= 1)
        return 0;
    if (bounce <= 3)
        return 1;
    return 2;
}

uint BucketizeSurfaceClass(bool isRefractive, bool isReflective, float roughness)
{
    if (isRefractive)
        return 2;
    if (isReflective || roughness < 0.08f)
        return 1;
    return 0;
}

uint BucketizeRoughness(float roughness)
{
    if (roughness < 0.05f)
        return 0;
    if (roughness < 0.3f)
        return 1;
    return 2;
}

float Luminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

uint BucketizeThroughput(float throughputLum)
{
    if (throughputLum < 0.1f)
        return 0;
    if (throughputLum < 0.5f)
        return 1;
    return 2;
}

uint BucketizeCosTheta(float cosTheta)
{
    cosTheta = abs(cosTheta);

    if (cosTheta < 0.25f)
        return 0;
    if (cosTheta < 0.75f)
        return 1;
    return 2;
}

uint ComputeStateIndex(
    uint bounce,
    bool isRefractive,
    bool isReflective,
    float roughness,
    float cosTheta,
    float3 throughput)
{
    uint bounceBucket = BucketizeBounce(bounce);
    uint surfaceBucket = BucketizeSurfaceClass(isRefractive, isReflective, roughness);
    uint cosThetaBucket = BucketizeCosTheta(cosTheta);
    uint throughputBucket = BucketizeThroughput(Luminance(throughput));
    uint roughnessBucket = BucketizeRoughness(roughness);

    return bounceBucket
         + 3 * surfaceBucket
         + 9 * cosThetaBucket
         + 27 * throughputBucket
         + 81 * roughnessBucket;
}

SamplingModeParams GetSamplingParams(uint actionIndex)
{
    SamplingModeParams p;

    if (actionIndex == 0)
    {
        p.bsdfProb = 0.8f;
        p.lightProb = 0.2f;
    }
    else if (actionIndex == 1)
    {
        p.bsdfProb = 0.5f;
        p.lightProb = 0.5f;
    }
    else
    {
        p.bsdfProb = 0.2f;
        p.lightProb = 0.8f;
    }

    return p;
}

[numthreads(8, 8, 1)]
[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;

    uint linearIndex = DispatchRaysIndex().y * dims.x + DispatchRaysIndex().x;

    SamplingModeParams params;

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

        uint currentState = ComputeStateIndex(
            0, // bounce / depth
            false, // isRefractive
            false, // isReflective
            0.5f, // neutral roughness placeholder
            1.0f, // neutral cosTheta
            float3(1.0f, 1.0f, 1.0f) // full throughput
        );
        
        for (int bounce = 0; bounce < MaxBounces; ++bounce)
        {
            RLTransitionGPU record;
            
            //uint state = ComputeStateIndex(
            // payload.depth,
            // payload.isRefractive,
            // payload.isReflective,
            // payload.matRoughness,
            // payload.cosTheta,
            // payload.throughput);
            
            record.StateIndex = currentState;
            
            uint actionSeed = linearIndex ^ (bounce * 16777619u) ^ (s * 374761393u) ^ (frameIndex * 2246822519u);

            uint action;
            if (gUseRL == 1)
            {
                action = ChooseActionEpsilonGreedy(currentState, actionSeed, 0.1f);
            }
            else
            {
                action = 1;
            }
            
            payload.prms = GetSamplingParams(action);
            
            payload.isReflective = 0;
            payload.isRefractive = 0;
            payload.matRoughness = 0.5f;
            payload.cosTheta = 1.0f;
            //payload.prms.bsdfProb = 0.5f;
            //payload.prms.lightProb = 0.5f;
            payload.done = 0;
            payload.emission = 0.0f;
            payload.bsdfOverPdf = 0.0f;
            payload.pdf = 1.0f;
            
            //record.StateIndex = ComputeStateIndex(
            //    payload.depth,
            //    payload.isRefractive,
            //    payload.isReflective,
            //    payload.matRoughness,
            //    payload.cosTheta,
            //    payload.throughput);


            //record.ActionIndex = ChooseActionEpsilonGreedy(record.StateIndex, actionSeed, 0.1f);

            //params = GetSamplingParams(record.ActionIndex);
            
            //payload.prms = params;
            
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
            
            uint index = (linearIndex * SPP + s) * MaxBounces + bounce;
            float3 bounceContrib = payload.throughput * payload.emission;
            
            float3 nextThroughput = payload.throughput;
            
            if (payload.done == 0)
            {
                nextThroughput *= payload.bsdfOverPdf;
            }


        
            uint nextState = ComputeStateIndex(
              payload.depth,
              payload.isRefractive,
              payload.isReflective,
              payload.matRoughness,
              payload.cosTheta,
              nextThroughput);
            
            float reward = length(bounceContrib) * 10.0f;
            
            if (payload.hitSomething == 1)
            {
                reward += 0.1f;
            }

            reward = log(1.0f + reward);
            record.Reward = reward;
            record.Valid = 1;
            record.ActionIndex = action;

            record.NextStateIndex = nextState;
            
            record.Terminated = payload.done ? 1 : 0;
            gRLTransitions[index] = record;
            if (record.Terminated == 1)
                break;
        
            payload.throughput = nextThroughput;
            
            currentState = nextState;
            
            //if (payload.done != 0)
            //    break;

        // Update throughput: multiply by f * cos / pdf
   //         payload.throughput *= payload.bsdfOverPdf;
        
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
                {
                    record.Terminated = 1;
                    gRLTransitions[index] = record;

                    break;
                }
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
    


    gAccumBuf[launchIndex] = float4(accumColor, 1.0f);
    gPresent[launchIndex] = float4(accumColor, 1.0f);
}

