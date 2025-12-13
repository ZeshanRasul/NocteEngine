# Nocte Engine - DXR Path Tracer


Nocte Engine is a real-time path tracing rendering engine built using DirectX Raytracing (DXR). The project was born out of my passion for pushing the boundaries of real-time physically accurate rendering techniques and to build a strong understanding of the real-world considerations involved in creating beautiful and realistic worlds in 3D interactive media. 

While I have previous experience with rasterisation-based engines made in DirectX 12 and OpenGL, my love of low-level programming along with a deep fascination of the science and mathematics behind ray tracing algorithms fueled my desire to create a real-time path tracer.



## Overview

Fundamentally, Nocte Engine was created not just as a learning experience in low-level graphics programming, but also as a project to demonstrate and showcase my understanding of cutting-edge rendering techniques and ability to build performant and complex architectural systems.

Nocte Engine gave me the opportunity to implement and experiment with advanced techniques, first starting with the core Whitted style ray tracing techniques of shadows, reflections and refractions and then build upon these foundations with Multiple Importance Sampling, Next Event Estimation and BSDF evaluation. These techniques allowed me to take a rasterization based D3D12 engine, create a basic real-time raytracer and then extend this groundwork into a high-fidelity real-time path tracer.

Over the course of two months, from the first D3D12 pipeline creation to the current stage of improving the denoising and visual clarity of the path tracer, I have been able to create a renderer which is highly relevant to the modern day enginess in the AAA games industry.

## Project Goals

As discussed, Nocte was created wuth a number of key goals, which organically and naturally developed over time. As I have found, the more you find yourself achieving, the greater your ambitions become and nothing fuels passion more than taking incremental steps that breakdown a project from achievable (STAR like) milestones to a adcanced system of many parts.

The core goals I set out to achieve on this journey were:

- Build a real-time path tracer using DirectX Raytracing
- Explore state-of-the-art rendering techniques such as Multiple Importance Sample and Next Event Estimation
- Reinforce and deepen my understanding of low-level programming in C++, DirectX 12 and HLSL
- Build project based experience in understand and leveraging the CPU and GPU to their full potential 
- Create a visually impressive piece that showcases not just my technical abilities but also my understanding of art and design principles in 3D rendering

## Key Technical Features 

My Nocte Engine development journey involved the implementation of a number of advanced technique features as well establishment of robust architectural systems. These features include:

- Robust DXR Ray Tracing Pipeline using Acceleration Structures, ray tracing shaders, well defined shader binding tables and complex multi-pass rendering for path tracing, temporal accumulation and denoising
- Path tracing with Multiple Importance Sampling and Next Event Estimation for realistic lighting and global illumination building upon foundational Whitted style ray tracing
- Robust denoising in a compute pass with the A-Trous ping pong alogirthm that is customisable during runtime with a in engine GUI
- Support for .obj model loading, multiple texture binding and material use
- Performance measurement including FPS counter, average frame time and extensive GPU profiling with Nvidia Nsight Graphics
- Internal geometry creation tools for sphere, cubes and other primitives
- Refitting of acceleration structures for dynamic scene updates
- Post processing effects using the ACES technique

## Rendering Architecture

The rendering in Nocte is executed through a number of steps:

- TLAS update where instance transforms are updated (used in early development in the Whitted style scene)
- Ray dispatch stage executing the Raygen and any further DXR shaders
- Temporal Accumulation with a dedicated pipeline using the compute shader (currently WIP)
- Denoising A-Trous compute shader pass executed N number of times per frame where N is exposed to the engine GUI.
- Tonemapping (currently in final denoise pass but will be moved to it's own final pass)
- UI rendering pass using ImGui

The engine leverages a number of buffers and resources each with a clear and defined role through the multi-pass rendering pipeline.
The raytracing pass writes to an Accumulation Buffer and reads from an Accumulation History Buffer which is leveraged for the work in progress temporal accumulation work.
This allows the final Accumulation Buffer to contain a blended value between the previous frame and current frame. The Accumulation Buffer is then copied into the Accumulation History Buffer in order to be used in the next frame.

The temporal accumulation pass then reads from the Accumulation Buffer and writes to a Temporal Accumulation Radiance buffer while using mean and mean squared moment vectors.
Finally the TA radiance is used as the input of the first of N denoising passes, with subsequent passes using a ping pong pair of buffers which alternate as input and output.

## Ray Tracing Pipeline

The DXR ray tracing pipeline in Nocte Engine is built around a well defined set of shaders and a shader binding table (SBT) that maps the shaders to the geometry in the scene. Although the SBT felt complex at first, I was able to break down the process into manageable steps and create a robust system that could be extended as needed.

The pipeline consists of the following shaders:
- Ray Generation Shader: This is the entry point for ray tracing. It generates rays for each pixel on the screen and initiates the path tracing process.
- Miss Shader: This shader is invoked when a ray does not intersect any geometry in the scene. It curtrently returns a background gradient and will be extended to sample environment map for more realistic lighting effects.
- Closest Hit shader: This shader is one of the most interesting parts of the pipeline. When a ray intersects with geometry in the acceleration structure this shader is executed and populates the ray payload with a breadth of information after sampling area lights, checking wherether a pixel is occluded (and thus in shadow) and whether a reflection or refraction ray should be dispatched.

A number of helpers are also used for path tracing to evaluation the BSDF, sample lights and perform MIS calculations.

In terms of acceleration structures, the engine builds a bottom level acceleration structure (BLAS) for each mesh in the scene and top level acceleration structure (TLAS) that contains the entire scene. The TLAS creation is set up in such a way that it is possible to refit the TLAS at runtime if necessary. There is an example of this in practice in the visual demonstration section, and TLAS refitting was used in the Whitted style phase of development.
 
## Lighting and Global Illumination

Nocte Engine implements physically based lighting models in order to achieve realistic lighting and global illumination effects. The techniques used allow ray and path tracers to achieve high-fidelity that rasterisation alogirthms would struggle or be unable to achieve, highlighting the importance of ray tracing to the future of 3D game, film and visualisation technologies.
The shaders utilise both direct and indirect illumination algorithms resulting in beautifully rendered scenes with diffuse interrelections, soft shadows, reflections and refractions.

Direct lighting is implemented using Next Event Estimation (NEE) where the area light sources in the scene are sampled directly from the ray-surface intersection point and shadow rays are traced in order to evaluate whethere an intersection point is occluded by geometry and thus in shadow.

Global illumination is achieved through path tracing with Multiple Importance Sampling (MIS). This technique allows the engine to sample both the BSDF and light sources in order to reduce variance and noise in the final render. By combining these two sampling strategies, the engine can produce high-quality images with fewer samples per pixel, making real-time path tracing feasible. As mentioned in the references section, the MIS 101 chapter of Ray Tracing Gams II was an extremely valuable resource to understanding the difference and impact of using MIS compared to solely using BSDF sampling or light sampling alone.

## Materials and Shading

Material evaluation in Nocte Engine is based on the now industry standard Physically Based Rendering (PBR) techniques. The engine supports a range of material properties including albedo, roughness, metallic and will be (easily) extended to include emissive properties. These properties are used in the BSDF evaluation to calculate how light interacts with surfaces in the scene.

The BSDF implementation uses Lambertian reflectange for diffuse surfaces along with the Disney GGX microfacet model for specular reflections, leveraging the Schlick Fresnel approximation. Probability density functions (PDFs) are calculated for both the BSDF and light sampling strategies resulting in improved Multiple Importance Sampling.

## Denoising and Temporal Accumulation

As with all path tracers, Nocte experiences the same inherent noise at low SPP (samples per pixel) due to the lack of convergence of the algorithm. To counter this noise, Nocte has spatial denoising and work in progress temporal accumulation, used to improve the image quality and convergence.

The temporal accumulation approach is to accumulate and blend between radiance values from multiple frames when the scene is stationary. The present approach resets this accumulation on camera movement in order to ensure stale radiance values from previous camera perspectives do not blend with new perspectives.

The spatial denoising has been implemented an using A-Trous wavelet filter on the compute shader using normal and depth data to preserve edges while reducing noise. It is widely configurable with a multi-pass ping pong system and parameters that are exposed through the GUI. This ensures users and developers can quickly iterate and experiment with a range of parameter combinations and thus reach an optimal denoising state for their needs.

## Scene Management

The scenes in Nocte consist of mesh instances, their transforms and materials as well as acceleration structure references. Meshes can either be loaded using the engines OBJ loader, leveraging the lightweight tinyobj header only library or through the engines geometry generator which leverages a system from the leading text Frank Luna's Introduction to Game Programming with DirectX 12.

All instance materials are stored in a structured buffer which is accessed using a per instance material index bound to the shaders as a constant buffer. This ensures constant buffer sizers do not grow excessively as instance counts increase.

The acceleration structure architecture allows for runtime updates to the TLAS known as refittin as can be demonstrated from an early development video of rotating skulls. 

Furthermore, core renderer settings such as camera controls, area light parameters and the previously discussed denoising parameters are exposed to the developer through a GUI. This ensures Nocte is practical and easily modifiable by the users and demonstrates the steps taken to mirror industry standard engine workflows in debugging, feature development and the many tweaks required in a graphics engine to achieve the best possible renders.

## Performance and Profiling

When building an engine as computationally expensive as a real-time path tracer, it is essential to montior performance, design architecture in a way that results optimal efficiency and tie profiling systems and considertions within the projects core. As such, performance metrics have been monitors on both the CPU and GPU sides.

Nocte records frame times, average frame times and frames per second counters to under which scenarios and setups have a meaningful impact on performance, both positive and negative. By exposing performance impacting parameters such as the number of denoising passes executed, the developer is able monitor and find balance in performance and quality of the final real-time rendered scene. Furthermore, by accessing these parameters through the GUI, quick iteration loops and subtle tuning and experimentation become easy for the developers and users.

NVIDIA Nsight Graphics has been used extensively to inspect render times, GPU processing performance, shader execution times and memory access patterns. This identification and understanding of GPU bottlenecks is invaluable in confirming and validating (or refuting) developer assumptions about performance trade-offs and is an essential skill for any computationally demanding scenario particularly GPU related programming.

## Tools, Debugging and Validation

Creating systems that are easy to debug and validate is just as important as building the engine core features and as such a strong emphasis was placed on debugging, whether it is through debug visualisations or tracking and exposing key parameters to the GUI. A core part of debugging included using Visual Studio's built in debugger to measure and review variables within the code, order of execution and follow the call stack in order to reason and rationalise any bugs that will inevitably arise in any computer program. 

Furthermore, the D3D12 debug controllewr was enabled in all debug builds to ensure that any misuse of the DirectX API was quickly identified and solved. Such API debug tools are invaluable in any graphics project and as such it was treated as a core priority to become familiar with the various types of common (and not so common) warnings and errors and handle and solve them diligently.

As mentioned earlier, Nsight Graphics was also essential in debugging GPU side computations and was deeply beneficial to monitor and confirm successful binding of buffers and resources to shaders, ensure acceleration structures were correctly built, modified and integrated into the engine and to confirm that root parameters were valid throughout the pipeline.

Finally, the rendering of scenes was developed in such a way as to incrementally increase in complexity. Starting with a simply Whitted style raytracer setup with spheres, boxes, planes and in this case, the skull mesh from Luna's text it was simple to implement, test and debug the core raytracing features, from determining visibilty to tracing reflection and refraction rays. MIS and NEE were implemented using  simple Cornell box style scene which allowed for feature testing with a small number of meshes within an enclosed space with a single area light. Once these more straightforward scenes were found to be working correctly, more complex models were integrated such as the Chinese Dragon mesh and Crytek Sponza environment, both sourced from Morgan McGuire's 3D model respoitory (referenced below).

## Technical Challenges and Solutions

## Visual Results

## Build and Run Instructions

## Future Work

## Acknowledgments, References, and Resources