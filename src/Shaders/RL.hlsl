uint ChooseBestAction(uint stateIndex)
{
    uint baseIdx = stateIndex * NUM_ACTIONS;

    float q0 = gQTable[baseIdx + 0].Value;
    float q1 = gQTable[baseIdx + 1].Value;
    float q1 = gQTable[baseIdx + 2].Value;
    float q1 = gQTable[baseIdx + 3].Value;
    float q2 = gQTable[baseIdx + 4].Value;

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
    
    if (q3 > bestQ)
    {
        bestQ = q3;
        bestAction = 3;
    }
  
    if (q4 > bestQ)
    {
        bestQ = q4;
        bestAction = 4;
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

uint ChooseActionEpsilonGreedy(uint stateIndex, uint pixel, float epsilon)
{
    float r = HashToUnitFloat(pixel + frameIndex * 9781);

    if (r < epsilon)
    {
        return pixel % NUM_ACTIONS;
    }

    return ChooseBestAction(stateIndex);
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

uint BucketizeSPP(int FrameIndex)
{
    if (FrameIndex < 16)
        return 0;
    if (FrameIndex < 64)
        return 1;
    return 2;
}

uint ComputeStateIndex(
    uint bounce,
    bool isRefractive,
    bool isReflective,
    float roughness,
    float cosTheta,
    float3 throughput,
    uint frameIndex)
{
    uint bounceBucket = BucketizeBounce(bounce);
    uint surfaceBucket = BucketizeSurfaceClass(isRefractive, isReflective, roughness);
    uint cosThetaBucket = BucketizeCosTheta(cosTheta);
    uint throughputBucket = BucketizeThroughput(Luminance(throughput));
    uint roughnessBucket = BucketizeRoughness(roughness);
    uint sppBucket = BucketizeSPP(frameIndex);

    return bounceBucket
         + 3 * surfaceBucket
         + 9 * cosThetaBucket
         + 27 * throughputBucket
         + 81 * roughnessBucket
         + 243 * sppBucket;
}

struct SamplingModeParams
{
    float bsdfProb;
    float lightProb;
};

SamplingModeParams GetSamplingParams(uint actionIndex)
{
    SamplingModeParams p;

    if (actionIndex == 0)
    {
        p.bsdfProb = 0.9f;
        p.lightProb = 0.1f;
    }
    else if (actionIndex == 1)
    {
        p.bsdfProb = 0.7f;
        p.lightProb = 0.3f;
    }
    else if (actionIndex == 2)
    {
        p.bsdfProb = 0.5f;
        p.lightProb = 0.5f;
    }
    else if (actionIndex == 3)
    {
        p.bsdfProb = 0.3;
        p.lightProb = 0.7;
    }
    else if (actionIndex == 4)
    {
        p.bsdfProb = 0.1f;
        p.lightProb = 0.9f;
    }
    else
    {
        p.bsdfProb = 0.5f;
        p.lightProb = 0.5f;
    }

    return p;
}