cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
}

struct VertexIn
{
    float3 PosL : POSITION;
    float4 Color : COLOR;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
};
    


VertexOut VS(VertexIn vIn)
{
    VertexOut vOut;
    
    vOut.PosH = mul(float4(vIn.PosL, 1.0f), gWorldViewProj);
    
    //vOut.PosH = float4(vIn.PosL, 1.0f);
    
    vOut.Color = vIn.Color;
    
    return vOut;
}