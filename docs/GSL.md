# Garden Shading Language Documentation

GSL is a custom shader language based on the [GLSL](https://www.khronos.org/opengl/wiki/Core_Language_(GLSL)) for [Vulkan API](https://github.com/KhronosGroup/GLSL/blob/main/extensions/khr/GL_KHR_vulkan_glsl.txt). It was created for 
the [Garden](https://github.com/cfnptr/garden) game engine to simplify and standardize shader development.

## Variable Types

All GLSL default variable types **vec2, ivec3, mat4**... are replaced with 
more pleasant looking variants **float2, int3, float4x4**. Also built in variables **gl_Xxx** with **gl.xxx**. Don't be angry :)

You can use **#include** directive, it's backed by the shaderc compiler internally.

## Vertex attributes

Shader parser automatically gets and calculates vertex attributes, so we do not need to explicitly specify layout. 
Input attributes format can be specified using this syntax:

```
in float3 vs.variableName : f32;
```

**With one of these formats**: [ f8 | f16 | f32 | f64 | i8 | i16 | i32 | i64 | u8 | u16 | u32 | u64 ]

You can add offset to the vertex attributes. (in bytes)

```
#attributeOffset 16
```

## Pipeline state

Shader parser gets pipeline state from the declared "pipelineState" properties in the shader.
Also you can override pipeline state properties when loading pipeline in the code.

```
pipelineState
{
    depthTesting = on;
    depthWriting = on;
    faceCulling = off;
}
```

* **discarding** [ on | off ] - Controls whether primitives are discarded immediately before the rasterization stage. (off)

### Input assembly

* **topology** [ triangleList | triangleStrip | lineList | lineStrip | pointList ] -
	Specify what kind of primitives to render. (triangleList)
* **polygon** [ fill | line | point ] - Specify how polygons will be rasterized. (fill)

### Depth

* **depthTesting** [ on | off ] - If enabled, do depth comparisons and update the depth buffer. (off)
* **depthWriting** [ on | off ] - Enable or disable writing into the depth buffer. (off)
* **depthClamping** [ on | off ] - Controls whether to clamp the fragment’s depth values. (off)
* **depthBiasing** [ on | off ] - Controls whether to bias fragment depth values. (off)

* **depthCompare** [ never | less | equal | lessOrEqual | greater | notEqual | greaterOrEqual | always ] -
	Specify the value used for depth buffer comparisons. (greater)

> // TODO: depthBiasConstant, depthBiasSlope, depthBiasClamp.

### Culling

* **faceCulling** [ on | off ] - If enabled, cull polygons based on their winding in window coordinates. (on)
* **cullFace** [ front | back | frontAndBack ] - Specify whether front or back-facing facets can be culled. (back)
* **frontFace** [ clockwise | counterClockwise ] - Define front and back-facing polygons. (counterClockwise)

### Blending

* **colorMaskX** [ all | none | r | rg | rb | rgba | ... ] - Enable and disable writing of framebuffer color components. (all)
* **blendingX** [ on | off ] - If enabled, blend the computed fragment color values with the values in the color buffers. (off)
* **blendOperationX** [ add | sub | revSub | min | max ] - Specify the RGB color and alpha blend equation. (add)
* **colorOperationX** [ add | sub | revSub | min | max ] - Specify the RGB color blend equation. (add)
* **alphaOperationX** [ add | sub | revSub | min | max ] - Specify the alpha blend equation. (add)
* **srcBlendFactorX** [FACTOR] - Specify pixel arithmetic for source RGB color and alpha. (srcAlpha, one)
* **dstBlendFactorX** [FACTOR] - Specify pixel arithmetic for destination RGB color and alpha. (oneMinusSrcAlpha, zero)
* **srcColorFactorX** [FACTOR] - Specify pixel arithmetic for source RGB color. (srcAlpha)
* **dstColorFactorX** [FACTOR] - Specify pixel arithmetic for destination RGB color. (oneMinusSrcAlpha)
* **srcAlphaFactorX** [FACTOR] - Specify pixel arithmetic for source alpha. (one)
* **dstAlphaFactorX** [FACTOR] - Specify pixel arithmetic for destination alpha. (zero)

> Where [X] is index of the framebuffer attachment.

**Blending scale factors**: [ zero | one | srcColor | oneMinusSrcColor | dstColor | oneMinusDstColor | srcAlpha | 
    oneMinusSrcAlpha | dstAlpha | oneMinusDstAlpha | constColor | oneMinusConstColor | constAlpha | 
    oneMinusConstAlpha | src1Color | oneMinusSrc1Color | src1Alpha | oneMinusSrc1Alpha | srcAlphaSaturate ]

**revSub** - Subtracts the source from the destination (O = dD - sS).

Color blending example: Orgb = srgb * Srgb + drgb * Drgb.</br>
Alpha blending example: Oa = sa * Sa + da * Da.

> // TODO: blending constant color.

## Push Constants

Push constants allow to pass some small data (128 bytes) to the shader for each draw call.

```
uniform pushConstants
{
    uint32 instanceIndex;
    float alphaCutoff;
} pc;
```

## Sampler

Shader parser gets sampler state from the properties, written inside sampler declaration block.
Also you can use dynamic samplers by marking uniform with ```mutable``` keyword.

```
uniform sampler2D
{
    filter = linear;
} samplerName;

uniform samplerCube someSampler;
```

* **filter** [ nearest | linear ] - Specify the texture minifying and magnification function. (nearest)
* **filterMin** [ nearest | linear ] - Specify the minifying function. (nearest)
* **filterMag** [ nearest | linear ] - Specify the magnification function. (nearest)
* **filterMipmap** [ nearest | linear ] - Specify the mipmap function. (nearest)
* **addressMode** [TYPE] - Specify the address mode parameter for texture coordinate X, Y, and Z. (clampToEdge)
* **addressModeX** [TYPE] - Specify the address mode parameter for texture coordinate X. (clampToEdge)
* **addressModeY** [TYPE] - Specify the address mode parameter for texture coordinate Y. (clampToEdge)
* **addressModeZ** [TYPE] - Specify the address mode parameter for texture coordinate Z. (clampToEdge)
* **borderColor** [ floatTransparentBlack | intTransparentBlack | floatOpaqueBlack | 
	intOpaqueBlack | floatOpaqueWhite | intOpaqueWhite ] - Specify the border clamp color. (floatTransparentBlack)
* **comparison** [ on | off ] - Enable or disable comparison against a reference value during lookups. (off)
* **anisoFiltering** [ on | off ] - Enable or disable texel anisotropic filtering. (off)
* **maxAnisotropy** [ 1 | 2 | 4 | 8 | 16 ] - Anisotropy value clamp used by the sampler. (1)
* **unnormCoords** [ on | off ] - Controls whether to use unnormalized texel coordinates to address texels of the image. (off)
* **compareOperation** [ never | less | equal | lessOrEqual | greater | notEqual | greaterOrEqual | always ] -
	Specify the value to apply to fetched data before filtering. (less)
* **mipLodBias** [ 0.0 | 1.0 | -1.0 | ... ] - Bias to be added to mipmap LOD calculation. (0.0)
* **minLod** [ 0.0 | 0.5 | 2.0 | ... ] - Used to clamp the minimum of the computed LOD value. (0.0)
* **maxLod** [ 0.0 | 1.5 | 5.0 | inf | ... ] - Used to clamp the maximum of the computed LOD value. (inf)

**Texture address mode types**: [ repeat | mirroredRepeat | clampToEdge | clampToBorder | mirrorClampToEdge ]

### You can add offset to the subpassInput index:

```
#attachmentOffset 1
```

## Image / Texture

```
uniform image2D someImage : unormR8G8B8A8;
```

### Image data shader interpretation formats:

* **Unsigned integer formats**: [ uintR8, uintR8G8, uintR8G8B8A8, uintR16, uintR16G16, uintR16G16B16A16, uintR32, uintR32G32, uintR32G32B32A32, uintA2R10G10B10 ]</br>
* **Signed integer formats**: [ intR8, intR8G8, intR8G8B8A8, intR16, intR16G16, intR16G16B16A16, intR32, intR32G32, intR32G32B32A32 ]</br>
* **Unsigned integer formats normalized to (0.0, 1.0)**: [ unormR8, unormR8G8, unormR8G8B8A8, unormR16, unormR16G16, unormR16G16B16A16, unormA2R10G10B10 ]</br>
* **Signed integer formats normalized to (-1.0, 1.0)**: [ snormR8, snormR8G8, snormR8G8B8A8, snormR16, snormR16G16, snormR16G16B16A16 ]</br>
* **Floating point formats**: [ floatR16, floatR16G16, floatR16G16B16A16, floatR32, floatR32G32, floatR32G32B32A32, ufloatB10G11R11 ]</br>

## Storage Buffer

```
struct InstanceData
{
    float4x4 mvp;
    float4x4 model;
};

buffer readonly Instance
{
    InstanceData data[];
} instance;
```

## Descriptor Set

Use ```setX``` keyword to set which descriptor set to use inside shader, where **X** is the index of the DS.

```
uniform set1 sampler2D someSampler;

uniform set2 SomeBuffer
{
    float someValue;
    int someInteger;
} uniformBuffer;
```

## Compute Shader

Compute shader local group size is specified a lot simpler:

```
localSize = 16, 16, 1;
```

## Early Depth Test

Early fragment tests, as an optimization, exist to prevent unnecessary executions of the Fragment Shader.

You will only make the depth smaller, compared to gl.fragCoord.z:

```
depthLess out float gl.fragDepth;
```

You will only make the depth larger, compared to gl.fragCoord.z:

```
depthGreater out float gl.fragDepth;
```

## Shader Variant

You can implement different shader code paths, permutations using variants. Each variant is a separate 
instance of the pipeline in runtime. And we can choose which variant to use during the pipeline binding.

```
#variantCount 2
```

```
#define FIRST_VARIANT 0
#define SECOND_VARIANT 1

void main()
{
    // This evaluates at compile time.
    if (gsl.variant == FIRST_VARIANT)
    {
        // first variant...
    }
    else
    {
        // second variant...
    }
}
```

## Spec Const

Specialization constant is a way of injecting an bool, int, uint and float constant 
into a halfway-compiled version of a shader right before pipeline creation to optimize it.

```
spec const bool USE_FAST_FUNC = false;
spec const float SOME_THRESHOLD = 0.5f;
```

## Extension / Feature

To enable specific GLSL feature extensions use this shorting:

```
#feature bindless
```

**Features**: [ bindless, subgroupBasic, subgroupVote ]

* bindless - GL_EXT_nonuniform_qualifier
* subgroupBasic - GL_KHR_shader_subgroup_basic
* subgroupVote - GL_KHR_shader_subgroup_vote