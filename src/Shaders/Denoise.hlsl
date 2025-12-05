//cbuffer DenoiseParams : register(b0)
//{
//    float gColorSigma;
//    float gNormalSigma;
//    float gDepthSigma;
//    int gStepSize;
//    float2 invResolution;
//    float2 Pad;
//};

Texture2D<float4> Accumulation : register(t0);
//Texture2D<float4> Normal : register(t1);
//Texture2D<float> Depth : register(t2);

//RWTexture2D<float4> PingIn : register(u0);
//RWTexture2D<float4> PongOut : register(u1);
RWTexture2D<unorm float4> PresentOut : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width, height;
    PresentOut.GetDimensions(width, height);

    uint aWidth, aHeight;
    Accumulation.GetDimensions(aWidth, aHeight);
    
    uint2 coord = dispatchThreadId.xy;
  //  if (coord.x >= width || coord.y >= height)
    //    return;

    // Simple debug: magenta
    PresentOut[coord] = Accumulation[coord];
}
