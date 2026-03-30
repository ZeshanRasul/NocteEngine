#include "RLState.h"

namespace
{
	int BucketizeVariance(float variance)
	{
		if (variance < 1000.0f)
			return 0;
		else if (variance < 3000.0f)
			return 1;
		
		return 2;
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

}

DiscreteState BucketizeState(const FrameStats& stats, int maxIterations)
{
	DiscreteState state;
	state.VarianceBucket = BucketizeVariance(stats.LogLuminanceVariance);
	state.ProgressBucket = BucketizeProgress(stats.Iteration, maxIterations);
	return state;
}

const char* GetVarianceBucketName(int bucket)
{
	switch (bucket)
	{
	case 0: return "Low";
	case 1: return "Medium";
	case 2: return "High";
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