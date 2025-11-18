struct ShadowHitInfo
{
    bool isHit;
    uint depth;
};

struct Attributes
{
    float2 uv;
};

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo hit)
{
    hit.isHit = false;
}