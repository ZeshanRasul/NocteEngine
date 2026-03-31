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

uint BucketizeCosTheta(float cosTheta)
{
    cosTheta = abs(cosTheta);

    if (cosTheta < 0.25f)
        return 0;
    if (cosTheta < 0.75f)
        return 1;
    return 2;
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