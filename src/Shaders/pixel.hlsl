struct PixelIn
{
    float4 PosH : SV_Position;
    float4 Color : COLOR;
};

float4 PS(PixelIn pIn) : SV_
{
    return pIn.Color;
}