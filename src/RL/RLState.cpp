#include "RLState.h"

namespace
{
	int BucketizeVariance(float variance)
	{
		if (variance < 6.5792f) return 0;
		if (variance < 6.5798f) return 1;
		if (variance < 6.5804f) return 2;
		if (variance < 6.5810f) return 3;
		return 4;
	}

	int BucketizeProgress(int iteration, int maxIterations)
	{
		if (maxIterations <= 0)
			return 0;

		float progress = static_cast<float>(iteration) / static_cast<float>(maxIterations);

		if (progress < 0.33f)
			return 0;
		else if (progress < 0.66f)
			return 1;

		return 2;
	}

	int BucketizeLuminance(float meanLum)
	{
		if (meanLum < 3.20f) return 0;
		if (meanLum < 3.24f) return 1;
		return 2;
	}

	int BucketizeBrightPixelRatio(float ratio)
	{
		if (ratio < 0.060f) return 0;
		if (ratio < 0.064f) return 1;
		return 2;
	}
}

DiscreteState BucketizeState(const FrameStats& stats, int maxIterations)
{
	DiscreteState state;
	state.VarianceBucket = BucketizeVariance(stats.LogLuminanceVariance);
	state.ProgressBucket = BucketizeProgress(stats.Iteration, maxIterations);
	state.LuminanceBucket = BucketizeLuminance(stats.MeanLuminance);
	state.BrightPixelRatioBucket = BucketizeBrightPixelRatio(stats.BrightPixelRatio);
	return state;
}

const char* GetVarianceBucketName(int bucket)
{
	switch (bucket)
	{
	case 0: return "Low";
	case 1: return "Low-Medium";
	case 2: return "Medium";
	case 3: return "Medium-High";
	case 4: return "High";
	default: return "Unknown";
	}
}

const char* GetProgressBucketName(int bucket)
{
	switch (bucket)
	{
	case 0: return "Early";
	case 1: return "Mid";
	case 2: return "Late";
	default: return "Unknown";
	}
}

const char* GetLuminanceBucketName(int bucket)
{
	switch (bucket)
	{
	case 0: return "Low";
	case 1: return "Medium";
	case 2: return "High";
	default: return "Unknown";
	}
}

const char* GetBrightPixelRatioBucketName(int bucket)
{
	switch (bucket)
	{
	case 0: return "Low";
	case 1: return "Medium";
	case 2: return "High";
	default: return "Unknown";
	}
}
