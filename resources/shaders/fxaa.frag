// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Based on this: https://github.com/kosua20/Rendu/blob/master/resources/common/shaders/screens/fxaa.frag

#include "common/tone-mapping.gsl"

// Settings for FXAA.
#define EDGE_THRESHOLD_MIN 0.0312f
#define EDGE_THRESHOLD_MAX 0.125f
#define QUALITY(q) ((q) < 5 ? 1.0f : ((q) > 5 ? ((q) < 10 ? 2.0f : ((q) < 11 ? 4.0f : 8.0f)) : 1.5f))
#define ITERATIONS 12
#define SUBPIXEL_QUALITY 0.75f
// TODO: set these values using spec consts?

pipelineState
{
	faceCulling = off;
}

uniform pushConstants
{
	float2 invFrameSize;
} pc;

in noperspective float2 fs.texCoords;
out float4 fb.ldr;

uniform sampler2D
{
	filter = linear;
} hdrBuffer;
uniform sampler2D
{
	filter = linear;
} ldrBuffer;

// TODO: try to calc luma using compute and store it to the hdr buffer .a, could be faster.

//**********************************************************************************************************************
// Performs FXAA post-process anti-aliasing as described in the Nvidia
// FXAA white paper and the associated shader code.
void main()
{
	// Color and luma at the current fragment
	float3 colorCenter = textureLod(ldrBuffer, fs.texCoords, 0.0f).rgb;
	float lumaCenter = rgbToLuma(textureLod(hdrBuffer, fs.texCoords, 0.0f).rgb);
	
	// Luma at the four direct neighbours of the current fragment.
	float lumaDown 	= rgbToLuma(textureLodOffset(hdrBuffer, fs.texCoords, 0.0f, int2( 0, -1)).rgb);
	float lumaUp 	= rgbToLuma(textureLodOffset(hdrBuffer, fs.texCoords, 0.0f, int2( 0,  1)).rgb);
	float lumaLeft 	= rgbToLuma(textureLodOffset(hdrBuffer, fs.texCoords, 0.0f, int2(-1,  0)).rgb);
	float lumaRight = rgbToLuma(textureLodOffset(hdrBuffer, fs.texCoords, 0.0f, int2( 1,  0)).rgb);
	
	// Find the maximum and minimum luma around the current fragment.
	float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
	float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
	
	// Compute the delta.
	float lumaRange = lumaMax - lumaMin;
	
	// If the luma variation is lower that a threshold (or if we are in a
	// really dark area), we are not on an edge, don't perform any AA.
	if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX))
	{
		fb.ldr = float4(colorCenter, 1.0f);
		return;
	}
	
	// Query the 4 remaining corners lumas.
	float lumaDownLeft 	= rgbToLuma(textureLodOffset(hdrBuffer, fs.texCoords, 0.0f, int2(-1, -1)).rgb);
	float lumaUpRight 	= rgbToLuma(textureLodOffset(hdrBuffer, fs.texCoords, 0.0f, int2( 1,  1)).rgb);
	float lumaUpLeft 	= rgbToLuma(textureLodOffset(hdrBuffer, fs.texCoords, 0.0f, int2(-1,  1)).rgb);
	float lumaDownRight = rgbToLuma(textureLodOffset(hdrBuffer, fs.texCoords, 0.0f, int2( 1, -1)).rgb);
	
	// Combine the four edges lumas (using intermediary
	// variables for future computations with the same values).
	float lumaDownUp = lumaDown + lumaUp;
	float lumaLeftRight = lumaLeft + lumaRight;
	
	// Same for corners
	float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
	float lumaDownCorners = lumaDownLeft + lumaDownRight;
	float lumaRightCorners = lumaDownRight + lumaUpRight;
	float lumaUpCorners = lumaUpRight + lumaUpLeft;
	
	// Compute an estimation of the gradient along the horizontal and vertical axis.
	float edgeHorizontal = abs(-2.0f * lumaLeft + lumaLeftCorners) +
		abs(-2.0f * lumaCenter + lumaDownUp) * 2.0f + abs(-2.0f * lumaRight + lumaRightCorners);
	float edgeVertical = abs(-2.0f * lumaUp + lumaUpCorners) +
		abs(-2.0f * lumaCenter + lumaLeftRight) * 2.0f + abs(-2.0f * lumaDown + lumaDownCorners);
	
	// Is the local edge horizontal or vertical ?
	bool isHorizontal = edgeHorizontal >= edgeVertical;
	
	// Choose the step size (one pixel) accordingly.
	float stepLength = isHorizontal ? pc.invFrameSize.y : pc.invFrameSize.x;
	
	// Select the two neighboring texels lumas in the opposite direction to the local edge.
	float luma1 = isHorizontal ? lumaDown : lumaLeft;
	float luma2 = isHorizontal ? lumaUp : lumaRight;
	// Compute gradients in this direction.
	float gradient1 = luma1 - lumaCenter;
	float gradient2 = luma2 - lumaCenter;
	
	// Which direction is the steepest ?
	bool is1Steepest = abs(gradient1) >= abs(gradient2);
	
	// Gradient in the corresponding direction, normalized.
	float gradientScaled = 0.25f * max(abs(gradient1), abs(gradient2));
	
	// Average luma in the correct direction.
	float lumaLocalAverage = 0.0f;
	if (is1Steepest)
	{
		// Switch the direction
		stepLength = -stepLength;
		lumaLocalAverage = 0.5f * (luma1 + lumaCenter);
	}
	else
	{
		lumaLocalAverage = 0.5f * (luma2 + lumaCenter);
	}
	
	// Shift UV in the correct direction by half a pixel.
	float2 currentUV = fs.texCoords;
	if (isHorizontal)
		currentUV.y += stepLength * 0.5f;
	else
		currentUV.x += stepLength * 0.5f;
	
	// Compute offset (for each iteration step) in the right direction.
	float2 offset = isHorizontal ?
		float2(pc.invFrameSize.x, 0.0f) : float2(0.0f, pc.invFrameSize.y);

	// Compute UVs to explore on each side of the edge,
	// orthogonally. The QUALITY allows us to step faster.
	float2 uv1 = currentUV - offset * QUALITY(0);
	float2 uv2 = currentUV + offset * QUALITY(0);
	
	// Read the lumas at both current extremities of the exploration
	// segment, and compute the delta wrt to the local average luma.
	float lumaEnd1 = rgbToLuma(textureLod(hdrBuffer, uv1, 0.0f).rgb);
	float lumaEnd2 = rgbToLuma(textureLod(hdrBuffer, uv2, 0.0f).rgb);
	lumaEnd1 -= lumaLocalAverage;
	lumaEnd2 -= lumaLocalAverage;
	
	// If the luma deltas at the current extremities is larger than
	// the local gradient, we have reached the side of the edge.
	bool reached1 = abs(lumaEnd1) >= gradientScaled;
	bool reached2 = abs(lumaEnd2) >= gradientScaled;
	bool reachedBoth = reached1 && reached2;
	
	// If the side is not reached, we continue to explore in this direction.
	if (!reached1)
		uv1 -= offset * QUALITY(1);
	if (!reached2)
		uv2 += offset * QUALITY(1);
	
	// If both sides have not been reached, continue to explore.
	if (!reachedBoth)
	{	
		for (int32 i = 2; i < ITERATIONS; i++)
		{
			// If needed, read luma in 1st direction, compute delta.
			if (!reached1)
			{
				lumaEnd1 = rgbToLuma(textureLod(hdrBuffer, uv1, 0.0f).rgb);
				lumaEnd1 = lumaEnd1 - lumaLocalAverage;
			}

			// If needed, read luma in opposite direction, compute delta.
			if (!reached2)
			{
				lumaEnd2 = rgbToLuma(textureLod(hdrBuffer, uv2, 0.0f).rgb);
				lumaEnd2 = lumaEnd2 - lumaLocalAverage;
			}

			// If the luma deltas at the current extremities is
			// larger than the local gradient, we have reached the side of the edge.
			reached1 = abs(lumaEnd1) >= gradientScaled;
			reached2 = abs(lumaEnd2) >= gradientScaled;
			reachedBoth = reached1 && reached2;
			
			// If the side is not reached, we continue to
			// explore in this direction, with a variable quality.
			if (!reached1)
				uv1 -= offset * QUALITY(i);
			if (!reached2)
				uv2 += offset * QUALITY(i);
			
			// If both sides have been reached, stop the exploration.
			if (reachedBoth)
				break;
		}
	}
	
	// Compute the distances to each side edge of the edge (!).
	float distance1 = isHorizontal ?
		(fs.texCoords.x - uv1.x) : (fs.texCoords.y - uv1.y);
	float distance2 = isHorizontal ?
		(uv2.x - fs.texCoords.x) : (uv2.y - fs.texCoords.y);
	
	// In which direction is the side of the edge closer ?
	bool isDirection1 = distance1 < distance2;
	float distanceFinal = min(distance1, distance2);
	
	// Thickness of the edge.
	float edgeThickness = distance1 + distance2;
	
	// Is the luma at center smaller than the local average ?
	bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;
	
	// If the luma at center is smaller than at its neighbour,
	// the delta luma at each end should be positive (same variation).
	bool correctVariation1 = (lumaEnd1 < 0.0) != isLumaCenterSmaller;
	bool correctVariation2 = (lumaEnd2 < 0.0) != isLumaCenterSmaller;
	
	// Only keep the result in the direction of the closer side of the edge.
	bool correctVariation = isDirection1 ? correctVariation1 : correctVariation2;
	
	// UV offset: read in the direction of the closest side of the edge.
	float pixelOffset = -distanceFinal / edgeThickness + 0.5f;
	
	// If the luma variation is incorrect, do not offset.
	float finalOffset = correctVariation ? pixelOffset : 0.0f;
	
	// Sub-pixel shifting
	// Full weighted average of the luma over the 3x3 neighborhood.
	float lumaAverage = (1.0f / 12.0f) * (2.0f *
		(lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);

	// Ratio of the delta between the global average and
	// the center luma, over the luma range in the 3x3 neighborhood.
	float subPixelOffset1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0f, 1.0f);
	float subPixelOffset2 = (-2.0f * subPixelOffset1 + 3.0f) * subPixelOffset1 * subPixelOffset1;
	// Compute a sub-pixel offset based on this delta.
	float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * SUBPIXEL_QUALITY;
	
	// Pick the biggest of the two offsets.
	finalOffset = max(finalOffset, subPixelOffsetFinal);
	
	// Compute the final UV coordinates.
	float2 finalUV = fs.texCoords;
	if (isHorizontal)
		finalUV.y += finalOffset * stepLength;
	else
		finalUV.x += finalOffset * stepLength;
	
	// Read the color at the new UV coordinates, and use it.
	float3 finalColor = textureLod(ldrBuffer, finalUV, 0.0f).rgb;
	fb.ldr = float4(finalColor, 1.0f);
}