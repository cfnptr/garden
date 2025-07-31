# Garden Shading Language Documentation

GSL is a custom shader language based on the [GLSL](https://www.khronos.org/opengl/wiki/Core_Language_(GLSL)) [subset](https://github.com/KhronosGroup/GLSL/blob/main/extensions/khr/GL_KHR_vulkan_glsl.txt) for the [Vulkan API](https://github.com/KhronosGroup/GLSL/blob/main/extensions/khr/GL_KHR_vulkan_glsl.txt). It was created for 
the [Garden](https://github.com/cfnptr/garden) game engine to simplify and standardize shader development.

## Variable Types

All GLSL default variable types **vec2, ivec3, mat4**... are replaced with 
more pleasant looking variants **float2, int3, float4x4**. Also built in variables **gl_Xxx** with **gl.xxx**. Don't be angry :)

You can use `#include` directive, it's backed by the shaderc compiler internally.

## Vertex attributes

Shader parser automatically gets and calculates vertex attributes, so we do not need to explicitly specify layout. 
Vertex input attributes format can be specified using this syntax:

```
in float3 vs.variableName : f32;
```

**With one of these formats**: [ f8 | f16 | f32 | f64 | i8 | i16 | i32 | i64 | u8 | u16 | u32 | u64 ]

You can add offset to the vertex attributes. (in bytes)

```
#attributeOffset 16
```

## Pipeline state

Shader parser gets pipeline state from the declared `pipelineState` properties in the shader.
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
* **stencilTesting** [ on | off ] - If enabled, do stencil comparisons and update the stencil buffer. (off)

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
Also you can use dynamic samplers by marking uniform with `mutable` keyword.

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
* **Signed integer formats**: [ sintR8, sintR8G8, sintR8G8B8A8, sintR16, sintR16G16, sintR16G16B16A16, sintR32, sintR32G32, sintR32G32B32A32 ]</br>
* **Unsigned integer formats normalized to (0.0, 1.0)**: [ unormR8, unormR8G8, unormR8G8B8A8, unormR16, unormR16G16, unormR16G16B16A16, unormA2R10G10B10 ]</br>
* **Signed integer formats normalized to (-1.0, 1.0)**: [ snormR8, snormR8G8, snormR8G8B8A8, snormR16, snormR16G16, snormR16G16B16A16 ]</br>
* **Floating point formats**: [ sfloatR16, sfloatR16G16, sfloatR16G16B16A16, sfloatR32, sfloatR32G32, sfloatR32G32B32A32, ufloatB10G11R11 ]</br>

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

Use `setX` keyword to set which descriptor set to use inside shader, where **X** is the index of the DS.

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

You will only make the depth smaller, compared to `gl.fragCoord.z`:

```
depthLess out float gl.fragDepth;
```

You will only make the depth larger, compared to `gl.fragCoord.z`:

```
depthGreater out float gl.fragDepth;
```

Forces depth and stencil tests to run before the fragment shader executes, when use `discard;`:

```
earlyFragmentTests in;
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

## Ray Tracing

All ray tracing built-ins and functions are written without the **EXT** postfix.

```
uniform accelerationStructure tlas;
rayPayload float4 payload;
```

## Bindless

Allows to declare uniforms without specified array size.

```
#feature ext.bindless

uniform sampler2D textures[];
```

## Buffer Reference

You can access GPU storage buffer data by it device address, which can 
be passed to the shader using push constants or another storage buffer.

```
#feature ext.bufferReference
#feature ext.scalarLayout

buffer reference scalar VertexBuffer
{
    float3 vertices[];
};
uniform pushConstants
{
    VertexBuffer vertexBuffer;
} pc;
```

## Subgroup Vote

TODO:

## Extension / Feature

To enable specific GLSL extensions use this shorting:

```
#feature ext.bindless
```

**Features**:

* ext.debugPrintf - [GLSL_EXT_debug_printf](https://github.com/KhronosGroup/GLSL/blob/main/extensions/ext/GLSL_EXT_debug_printf.txt)
* ext.explicitTypes - [GL_EXT_shader_explicit_arithmetic_types](https://github.com/KhronosGroup/GLSL/blob/main/extensions/ext/GL_EXT_shader_explicit_arithmetic_types.txt)
* ext.int8BitStorage - [GL_EXT_shader_8bit_storage](https://github.com/KhronosGroup/GLSL/blob/main/extensions/ext/GL_EXT_shader_16bit_storage.txt)
* ext.int16BitStorage - [GL_EXT_shader_16bit_storage](https://github.com/KhronosGroup/GLSL/blob/main/extensions/ext/GL_EXT_shader_16bit_storage.txt)
* ext.bindless - [GL_EXT_nonuniform_qualifier](https://github.com/KhronosGroup/GLSL/blob/main/extensions/ext/GL_EXT_nonuniform_qualifier.txt)
* ext.scalarLayout - [GL_EXT_scalar_block_layout](https://github.com/KhronosGroup/GLSL/blob/main/extensions/ext/GL_EXT_scalar_block_layout.txt)
* ext.bufferReference - [GL_EXT_buffer_reference](https://github.com/KhronosGroup/GLSL/blob/main/extensions/ext/GLSL_EXT_buffer_reference.txt)
* ext.subgroupBasic - [GL_KHR_shader_subgroup_basic](https://github.com/KhronosGroup/GLSL/blob/main/extensions/khr/GL_KHR_shader_subgroup.txt)
* ext.subgroupVote - [GL_KHR_shader_subgroup_vote](https://github.com/KhronosGroup/GLSL/blob/main/extensions/khr/GL_KHR_shader_subgroup.txt)
* ext.rayQuery - [GL_EXT_ray_query](https://github.com/KhronosGroup/GLSL/blob/main/extensions/ext/GLSL_EXT_ray_query.txt)