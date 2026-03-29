//FrameStats ComputeFrameStats(const std::vector<float4>& image, int iteration) {
//    FrameStats stats;
//    // Compute mean luminance
//    for (const auto& pixel : image) {
//        stats.MeanLuminance += pixel.y; // Assuming pixel.y is the luminance
//    }
//    stats.MeanLuminance /= image.size();
//
//    // Compute luminance variance
//    for (const auto& pixel : image) {
//        float luminance = pixel.y;
//        stats.LuminanceVariance += (luminance - stats.MeanLuminance) * (luminance - stats.MeanLuminance);
//    }
//    stats.LuminanceVariance /= image.size();
//
//    // Compute bright pixel ratio
//    for (const auto& pixel : image) {
//        if (pixel.y > 1.0f) { // Assuming bright pixels have luminance > 1.0
//            stats.BrightPixelRatio++;
//        }
//    }
//    stats.BrightPixelRatio /= image.size();
//
//    stats.Iteration = iteration;
//    return stats;
//};