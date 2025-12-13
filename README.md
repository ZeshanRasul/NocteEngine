# Nocte Engine - DXR Path Tracer

___

Nocte Engine is a real-time path tracing rendering engine build using DirectX Raytracing (DXR). The project was born out of my passion for pushing the boundaries of real-time physically accurate rendering techniques and to build a strong understanding of the real-world considerations involved in creating beautiful and realistic worlds in 3D interactive media. 

While I have previous experience with rasterisation-based engines made in DirectX 12 and OpenGL, my love of low-level programming along with a deep fascination of the science and mathematics behind ray tracing algorithms fueled my desire to create a real-time path tracer.



## Overview
___

Fundamentally, Nocte Engine was created not just as a learning experience in low-level graphics programming, but also as a project to demonstrate and showcase my understanding of cutting-edge rendering techniques and ability to build performant and complex architectural systems.

Nocte Engine gave me the opportunity to implement and experiment with advanced techniques, first starting with the core Whitted style ray tracing techniques of shadows, reflections and refractions and then build upon these foundations with Multiple Importance Sampling, Next Event Estimation and BSDF evaluation. These techniques allowed me to take a rasterization based D3D12 engine, create a basic real-time raytracer and then extend this groundwork into a high-fidelity real-time path tracer.

Over the course of two months, from the first D3D12 pipeline creation to the current stage of improving the denoising and visual clarity of the path tracer, I have been able to create a renderer which is highly relevant to the modern day enginess in the AAA games industry.

## Project Goals
___

As discussed, Nocte was created wuth a number of key goals, which organically and naturally developed over time. As I have found, the more you find yourself achieving, the greater your ambitions become and nothing fuels passion more than taking incremental steps that breakdown a project from achievable (STAR like) milestones to a adcanced system of many parts.

The core goals I set out to achieve on this journey were:

- Build a real-time path tracer using DirectX Raytracing
- Explore state-of-the-art rendering techniques such as Multiple Importance Sample and Next Event Estimation
- Reinforce and deepen my understanding of low-level programming in C++, DirectX 12 and HLSL
- Build project based experience in understand and leveraging the CPU and GPU to their full potential 
- Create a visually impressive piece that showcases not just my technical abilities but also my understanding of art and design principles in 3D rendering

## Key Technical Features 

___

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

## Materials and Shading

## Denoising and Temporal Accumulation

## Scene Management

## Performance and Profiling

## Tools, Debugging and Validation

## Technical Challenges and Solutions

## Visual Results

## Build and Run Instructions

## Future Work

## Acknowledgments, References, and Resources