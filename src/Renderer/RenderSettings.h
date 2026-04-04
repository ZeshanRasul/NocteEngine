#pragma	once

enum class SamplingMode
{
	BSDFHeavy = 0,
	BSDFGentle = 1,
	Balanced = 2,
	LightGentle = 3,
	LightHeavy = 4
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