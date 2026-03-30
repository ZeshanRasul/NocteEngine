#include "FrameStats.h"

//FrameStats ComputeFrameStats(const std::vector<DirectX::XMFLOAT4>& image, int iteration)
//{
//    FrameStats stats;
//    stats.Iteration = iteration;
//
//    if (image.empty())
//        return stats;
//
//    const size_t pixelCount = image.size();
//
//    double sum = 0.0;
//    double sumSq = 0.0;
//    int brightCount = 0;
//
//    // --- First pass ---
//    for (const auto& p : image)
//    {
//        float L = ComputeLuminance(p.x, p.y, p.z);
//
//        sum += L;
//        sumSq += (double)L * (double)L;
//
//        // simple threshold for "bright"
//        if (L > 1.0f)
//            brightCount++;
//    }
//
//    double mean = sum / (double)pixelCount;
//    double variance = (sumSq / (double)pixelCount) - (mean * mean);
//
//    // Clamp to avoid negative due to precision
//    if (variance < 0.0)
//        variance = 0.0;
//
//    stats.MeanLuminance = (float)mean;
//    stats.LuminanceVariance = (float)variance;
//    stats.LogLuminanceVariance = (float)logf(1.0f + variance);
//    stats.BrightPixelRatio = (float)brightCount / (float)pixelCount;
//
//    return stats;
//}