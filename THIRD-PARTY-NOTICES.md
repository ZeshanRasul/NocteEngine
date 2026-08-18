# Third-party notices

The MIT licence in [LICENSE](LICENSE) covers the code written for this project —
everything under `src/`, excluding the vendored libraries listed below. It does
**not** cover the bundled third-party source in `include/`, nor the 3-D scene
assets, which carry their own terms.

## Vendored source (`include/`)

| Component | Origin | Licence |
|---|---|---|
| `nv_helpers_dx12/` | NVIDIA — DXR tutorial helpers | BSD-style (NVIDIA), see file headers |
| `manipulator.h` / `.cpp` | NVIDIA — camera manipulator | NVIDIA, see file header |
| `imgui/` | Omar Cornut — Dear ImGui | MIT, see `include/imgui/LICENSE.txt` |
| `tinyobj/tiny_obj_loader.h` | Syoyo Fujita — tinyobjloader | MIT |
| `stb/stb_image_load.h`, `stb/stb_image_write.h` | Sean Barrett — stb | Public domain (MIT alternative) |
| `d3dx12.h` | Microsoft | MIT |
| `DDSTextureLoader.*` | Microsoft — DirectXTK | MIT |
| `glm/` | G-Truc Creation — OpenGL Mathematics | MIT |

These provide the acceleration-structure builders, ray-tracing pipeline and
root-signature generators, shader binding table generator, camera manipulation,
UI, model loading, image I/O and maths. The renderer, all shaders under
`src/Shaders/`, and the engine architecture in `src/Renderer/` are my own work.

## Scene assets

| Asset | Source | Licence |
|---|---|---|
| Amazon Lumberyard Bistro | Amazon Lumberyard, via [NVIDIA ORCA](https://developer.nvidia.com/orca) / [Morgan McGuire's Computer Graphics Archive](https://casual-effects.com/data/) | CC BY 4.0 |

The Bistro mesh itself is **not** committed (see the asset instructions in the
README); the textures it uses are. Assets are redistributed under their
respective Creative Commons terms and remain the property of their original
authors. If you fork this repository, attribution obligations travel with them.

Earlier commits in this repository's history also contain Crytek Sponza assets
(Frank Meinl, original by Marko Dabrovic, modifications by Morgan McGuire,
CC BY 3.0). Those files were removed from the working tree because no Sponza
mesh ships with the project, but they remain reachable in the git history and
their attribution stands.
