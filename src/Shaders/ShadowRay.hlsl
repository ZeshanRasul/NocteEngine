struct ShadowHitInfo
{
    bool isHit;
    uint depth;
};

struct Attributes
{
    float2 uv;
};

[shader("closesthit")]
void ShadowClosestHit(inout ShadowHitInfo hit, in Attributes attr)
{
    hit.isHit = true;
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo hit)
{
    hit.isHit = false;
}