#pragma	once

enum class SamplingMode
{
	BSDFHeavy = 0,
	Balanced = 1,
	LightHeavy = 2
};

struct RenderSettings
{
	SamplingMode SamplingStrategy = SamplingMode::Balanced;
	int MaxBounces = 12;
	int SamplesPerFrame = 1;

	bool useNEE = true;
	bool useTemporal = false;
	bool useDenoiser = false;
};