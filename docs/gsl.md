# Garden Shading Language Documentation

GSL is a custom shader language based on GLSL. It was created for 
the Garden game engine to simplify and standardize shader development.

## Variable Types

All GLSL default variable types vec2, ivec3, mat4... are replaced with 
more pleasant looking variants float2, int3, float4x4. Also built in variables gl_Xxx with gl.xxx. Don't be angry :)

You can use #include directive it's backed by the shaderc compiler internally.

## Vertex attributes

Shader parser automatically gets and calculates vertex attributes, so we do not need to explicitly specify layout. 
Input attributes format can be specified using this syntax:

```
in float3 vs.variableName : f32;
```

With one of these formats: [ f32 | f64 | i8 | i16 | i32 | i64 | u8 | u16 | u32 | u64 ]

* #attributeOffset X - Add offset to the vertex attributes. (in bytes)

## Pipeline state

Shader parser gets pipeline state from the declared "pipelineState" properties in the shader.
Also you can override pipeline state properties when loading pipeline in the code.

```
pipelineState
{
    depthTesting = off;
    faceCulling = on;
}
```

* discarding [ on | off ] - Controls whether primitives are discarded immediately before the rasterization stage. (off)

### Input assembly

* topology [ triangleList | triangleStrip | lineList | lineStrip | pointList ] -
	Specify what kind of primitives to render. (triangleList)
* polygon [ fill | line | point ] - Specify how polygons will be rasterized. (fill)

### Depth

* depthTesting [ on | off ] - If enabled, do depth comparisons and update the depth buffer. (off)
* depthWriting [ on | off ] - Enable or disable writing into the depth buffer. (off)
* depthClamping [ on | off ] - Controls whether to clamp the fragment’s depth values. (off)
* depthBiasing [ on | off ] - Controls whether to bias fragment depth values. (off)

* depthCompare [ never | less | equal | lessOrEqual | greater | notEqual | greaterOrEqual | always ] -
	Specify the value used for depth buffer comparisons. (greater)

// TODO: depthBiasConstant, depthBiasSlope, depthBiasClamp.

### Culling

* faceCulling [ on | off ] - If enabled, cull polygons based on their winding in window coordinates. (on)
* cullFace [ front | back | frontAndBack ] - Specify whether front or back-facing facets can be culled. (back)
* frontFace [ clockwise | counterClockwise ] - Define front and back-facing polygons. (counterClockwise)

### Blending

* colorMaskX [ all | none | r | rg | rb | rgba | ... ] - Enable and disable writing of framebuffer color components. (all)
* blendingX [ on | off] - If enabled, blend the computed fragment color values with the values in the color buffers. (off)
* blendOperationX [ add | sub | revSub | min | max ] - Specify the RGB color and alpha blend equation. (add)
* colorOperationX [ add | sub | revSub | min | max ] - Specify the RGB color blend equation. (add)
* alphaOperationX [ add | sub | revSub | min | max ] - Specify the alpha blend equation. (add)
* srcBlendFactorX [FACTOR] - Specify pixel arithmetic for source RGB color and alpha. (srcAlpha, one)
* dstBlendFactorX [FACTOR] - Specify pixel arithmetic for destination RGB color and alpha. (oneMinusSrcAlpha, zero)
* srcColorFactorX [FACTOR] - Specify pixel arithmetic for source RGB color. (srcAlpha)
* dstColorFactorX [FACTOR] - Specify pixel arithmetic for destination RGB color. (oneMinusSrcAlpha)
* srcAlphaFactorX [FACTOR] - Specify pixel arithmetic for source alpha. (one)
* dstAlphaFactorX [FACTOR] - Specify pixel arithmetic for destination alpha. (zero)

Where [X] is index of the framebuffer attachment.

Blending scale [FACTOR]: [ zero | one | srcColor | oneMinusSrcColor | dstColor | oneMinusDstColor | srcAlpha | 
    oneMinusSrcAlpha | dstAlpha | oneMinusDstAlpha | constColor | oneMinusConstColor | constAlpha | 
    oneMinusConstAlpha | src1Color | oneMinusSrc1Color | src1Alpha | oneMinusSrc1Alpha | srcAlphaSaturate ]

revSub - Subtracts the source from the destination (O = dD - sS).
Color blending example: Orgb = srgb * Srgb + drgb * Drgb.
Alpha blending example: Oa = sa * Sa + da * Da.

// TODO: blending constant color.

## Sampler

Shader parser gets sampler state from the properties, writen inside sampler declaration block.

```
uniform sampler2D
{
    filter = nearest;
} samplerName;

uniform set1 samplerCube someSampler;
```

* filter [ nearest | linear ] - Specify the texture minifying and magnification function. (nearest)
* filterMin [ nearest | linear ] - Specify the minifying function. (nearest)
* filterMag [ nearest | linear ] - Specify the magnification function. (nearest)
* filterMipmap [ nearest | linear ] - Specify the mipmap function. (nearest)
* wrap [TYPE] - Specify the wrap parameter for texture coordinate X, Y, and Z. (clampToEdge)
* wrapX [TYPE] - Specify the wrap parameter for texture coordinate X. (clampToEdge)
* wrapY [TYPE] - Specify the wrap parameter for texture coordinate Y. (clampToEdge)
* wrapZ [TYPE] - Specify the wrap parameter for texture coordinate Z. (clampToEdge)
* borderColor [ floatTransparentBlack | intTransparentBlack | floatOpaqueBlack | 
	intOpaqueBlack | floatOpaqueWhite | intOpaqueWhite ] - Specify the border clamp color. (floatTransparentBlack)
* comparison [ on | off ] - Enable or disable comparison against a reference value during lookups. (off)
* anisoFiltering [ on | off ] - Enable or disable texel anisotropic filtering. (off)
* unnormCoords [ on | off ] - Controls whether to use unnormalized texel coordinates to address texels of the image. (off)
* compareOperation [ never | less | equal | lessOrEqual | greater | notEqual | greaterOrEqual | always ] -
	Specify the value to apply to fetched data before filtering. (less)

Texture wrap [TYPE]: [repeat | mirroredRepeat | clampToEdge | clampToBorder | mirrorClampToEdge]

* #attachmentOffset X - Add offset to the subpassInput index.

## Image / Texture

```
uniform image2D someImage : f16rgba;
```

Image data shader interpretation formats: [ f16rgba | f32rgba | f16rg | f32rg | f16r |
    f32r | i8rgba | i16rgba | i32rgba | i8rg | i16rg | i32rg | i8r | i16r | i32r |
    u8rgba | u16rgba |u32rgba | u8rg | u16rg | u32rg | u8r | u16r | u32r ]

TODO: support r11f_g11f_b10f, rgba16, rgba16_snorm...

## Compute Shader

Compute shader local group size is specified a lot simplier:

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

* #variantCount X - Specify shader variant count.

```
#variantCount 2

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

Specialization constant is a way of injecting an int, bool, float, int4... constant 
into a halfway-compiled version of a shader right before pipeline creation to optimize it.

```
spec const bool USE_FAST_FUNC = false;
spec const float SOME_THRESHOLD = 0.5f;
```

## Extension / Feature

To enable specific GLSL feature extensions use this shorting:

* #feature [NAME] - Require specific GLSL feature extensions.

Feature [NAME]: [ bindless ]
TODO: ray tracing features