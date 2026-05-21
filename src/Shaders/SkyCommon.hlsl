#ifndef SKY_COMMON_HLSL
#define SKY_COMMON_HLSL

// Preetham Analytical Sky Model
// Reference: "Rendering Outdoor Light Scattering in Real Time"
//            Preetham, Shirley, Smits — SIGGRAPH 1999, Table 1 & A5.
//
// All sky computations are in linear sRGB.
// sunDir  : unit vector pointing FROM the scene TOWARD the sun (world +Y = up).
// turbidity: atmospheric turbidity T, 1 (pure air) to 10 (heavy haze), typical 2.0–4.0.
// intensity: overall sky brightness scale; tune so sky energy matches scene lighting.

static const float PI = 3.14159265f;

// Perez sky-luminance distribution function.
float PerezF(float A, float B, float C, float D, float E,
             float theta, float gamma)
{
    float cosTheta = max(cos(theta), 1e-4f);
    float cosGamma = cos(gamma);
    return (1.0f + A * exp(B / cosTheta))
         * (1.0f + C * exp(D * gamma) + E * cosGamma * cosGamma);
}

// Evaluate chromaticity or luminance at (theta, gamma) relative to zenith value.
float PerezNorm(float A, float B, float C, float D, float E,
                float theta, float gamma, float thetaS)
{
    float denom = PerezF(A, B, C, D, E, 0.0f, thetaS);
    return PerezF(A, B, C, D, E, theta, gamma) / max(denom, 1e-6f);
}

// Convert CIE xyY to linear sRGB.
float3 xyYToSRGB(float x, float y, float Y)
{
    float safeY = max(y, 1e-6f);
    float3 XYZ;
    XYZ.x = (x / safeY) * Y;
    XYZ.y = Y;
    XYZ.z = ((1.0f - x - y) / safeY) * Y;

    float3 rgb;
    rgb.r =  3.2406f * XYZ.x - 1.5372f * XYZ.y - 0.4986f * XYZ.z;
    rgb.g = -0.9689f * XYZ.x + 1.8758f * XYZ.y + 0.0415f * XYZ.z;
    rgb.b =  0.0557f * XYZ.x - 0.2040f * XYZ.y + 1.0570f * XYZ.z;
    return max(rgb, 0.0f);
}

// Returns linear sRGB sky radiance for a given view direction.
// Returns 0 for directions below the horizon.
// The sun disc is NOT included here — add it separately in the miss shader.
float3 SamplePreethamSky(float3 dir, float3 sunDir, float turbidity, float intensity)
{
    // Directions below the horizon get a simple ground colour.
    if (dir.y < 0.0f)
        return float3(0.10f, 0.08f, 0.06f) * intensity;

    float T  = clamp(turbidity, 1.0f, 10.0f);
    float T2 = T * T;

    // Zenith angle of the sky sample (0 = straight up).
    float theta  = acos(saturate(dir.y));
    // Zenith angle of the sun (clamp to avoid below-horizon blow-up).
    float thetaS = acos(saturate(sunDir.y));
    // Angle between view direction and sun.
    float gamma  = acos(saturate(dot(dir, sunDir)));

    // --- Perez coefficients (Preetham 1999, Table 2) ---
    float AY = 0.17872f * T - 1.46303f;
    float BY = -0.35540f * T + 0.42749f;
    float CY = -0.02266f * T + 5.32505f;
    float DY = 0.12064f * T - 2.57705f;
    float EY = -0.06696f * T + 0.37027f;

    float Ax = -0.01925f * T - 0.25922f;
    float Bx = -0.06651f * T + 0.00081f;
    float Cx = -0.00041f * T + 0.21247f;
    float Dx = -0.06409f * T - 0.89887f;
    float Ex = -0.00325f * T + 0.04517f;

    float Ay = -0.01669f * T - 0.26078f;
    float By = -0.09495f * T + 0.00921f;
    float Cy = -0.00792f * T + 0.21023f;
    float Dy = -0.04405f * T - 1.65369f;
    float Ey = -0.01092f * T + 0.05291f;

    // --- Zenith luminance Yz (kcd/m²) ---
    float chi = (4.0f / 9.0f - T / 120.0f) * (PI - 2.0f * thetaS);
    float Yz = max((4.0453f * T - 4.9710f) * tan(chi) - 0.2155f * T + 2.4192f, 0.001f);

    // --- Zenith chromaticity from Preetham Table A5 ---
    float ts3 = thetaS * thetaS * thetaS;
    float ts2 = thetaS * thetaS;

    float xz = T2 * ( 0.00166f * ts3 - 0.00375f * ts2 + 0.00209f * thetaS + 0.0f)
             + T  * (-0.02903f * ts3 + 0.06377f * ts2 - 0.03202f * thetaS + 0.00394f)
             + 1  * ( 0.11693f * ts3 - 0.21196f * ts2 + 0.06052f * thetaS + 0.25886f);

    float yz = T2 * ( 0.00275f * ts3 - 0.00610f * ts2 + 0.00317f * thetaS + 0.0f)
             + T  * (-0.04214f * ts3 + 0.08970f * ts2 - 0.04153f * thetaS + 0.00516f)
             + 1  * ( 0.15346f * ts3 - 0.26756f * ts2 + 0.06670f * thetaS + 0.26688f);

    // --- Evaluate relative luminance and chromaticity ---
    // Normalise Y by Yz so that Y=1 at the zenith, then scale by intensity.
    float Yn = PerezNorm(AY, BY, CY, DY, EY, theta, gamma, thetaS);
    float xn = xz * PerezNorm(Ax, Bx, Cx, Dx, Ex, theta, gamma, thetaS);
    float yn = yz * PerezNorm(Ay, By, Cy, Dy, Ey, theta, gamma, thetaS);

    // Smooth the horizon so there's no hard edge when dir.y is near 0.
    float horizonFade = saturate(dir.y / 0.04f);
    Yn *= horizonFade;

    float3 sky = xyYToSRGB(xn, yn, Yn) * intensity;

    return sky;
}

#endif // SKY_COMMON_HLSL
