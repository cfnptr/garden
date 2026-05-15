// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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

// Based on this: https://github.com/iryoku/smaa/blob/master/SMAA.hlsl

spec const int32 MAX_SEARCH_STEPS_DIAG = 8;
spec const int32 CORNER_ROUNDING = 25;

// Non-Configurable Defines
#define AREA_LUT_MAX_DISTANCE 16
#define AREA_LUT_MAX_DISTANCE_DIAG 20
#define AREA_LUT_PIXEL_SIZE float2(1.0 / 160.0, 1.0 / 560.0)
#define AREA_LUT_SUBTEX_SIZE (1.0 / 7.0)
#define SEARCH_LUT_SIZE float2(66.0f, 33.0f)
#define SEARCH_LUT_PACKED_SIZE float2(64.0f, 16.0f)
#define CORNER_ROUNDING_NORM (float(CORNER_ROUNDING) / 100.0)

in noperspective float2 fs.texCoords;
in noperspective float2 fs.pixCoords;
in noperspective float4 fs.offset0;
in noperspective float4 fs.offset1;
in noperspective float4 fs.offset2;

out float4 fb.weights;

uniform pushConstants
{
	float2 invFrameSize;
	float2 frameSize;
} pc;

uniform sampler2D
{
	filter = linear;
} areaLUT;
uniform sampler2D
{
	filter = linear;
} searchLUT;
uniform sampler2D
{
	filter = linear;
} edgesBuffer;

float2 area(float2 dist, float e1, float e2)
{
	// Rounding prevents precision errors of bilinear filtering:
	float2 texCoords = fma(round(float2(e1, e2) * 4.0f), float2(AREA_LUT_MAX_DISTANCE), dist);
    
	// We do a scale and bias for mapping to texel space:
	texCoords = fma(texCoords, AREA_LUT_PIXEL_SIZE, AREA_LUT_PIXEL_SIZE / 2.0f);
	return textureLod(areaLUT, texCoords, 0.0f).rg;
}

// Similar to area(), this calculates the area corresponding to a certain diagonal distance and crossing edges 'e'.
float2 areaDiag(float2 dist, float2 e)
{
	float2 texCoords = fma(e, float2(AREA_LUT_MAX_DISTANCE_DIAG), dist);

	// We do a scale and bias for mapping to texel space:
	texCoords = fma(texCoords, AREA_LUT_PIXEL_SIZE, AREA_LUT_PIXEL_SIZE / 2.0f);

	// Diagonal areas are on the second half of the texture:
	texCoords.x += 0.5f;
	return textureLod(areaLUT, texCoords, 0.0f).rg;
}

//**********************************************************************************************************************
// Allows to decode two binary values from a bilinear-filtered access.
float2 decodeDiagBilinearAccess(float2 e)
{
	// Bilinear access for fetching 'e' have a 0.25 offset, and we are
	// interested in the R and G edges:
	//
	// +---G---+-------+
	// |   x o R   x   |
	// +-------+-------+
	//
	// Then, if one of these edge is enabled:
	//   Red:   (0.75 * X + 0.25 * 1) => 0.25 or 1.0
	//   Green: (0.75 * 1 + 0.25 * X) => 0.75 or 1.0
	//
	// This function will unpack the values (fma + mul + round):
	// wolframalpha.com: round(x * abs(5 * x - 5 * 0.75)) plot 0 to 1
	e.r = e.r * abs(fma(e.r, 5.0f, -3.75f));
	return round(e);
}
float4 decodeDiagBilinearAccess(float4 e)
{
	e.rb = e.rb * abs(fma(e.rb, float2(5.0f), float2(-3.75f)));
	return round(e);
}

float2 searchDiag1(float2 dir, out float2 e)
{
	float4 coords = float4(fs.texCoords, -1.0f, 1.0f);
	float3 t = float3(pc.invFrameSize, 1.0f);

	while (coords.z < float(MAX_SEARCH_STEPS_DIAG - 1) && coords.w > 0.9f)
	{
		coords.xyz = fma(t, float3(dir, 1.0f), coords.xyz);
		e = textureLod(edgesBuffer, coords.xy, 0.0f).rg;
		coords.w = dot(e, float2(0.5f));
	}
	return coords.zw;
}
float2 searchDiag2(float2 dir, out float2 e)
{
	float4 coords = float4(fs.texCoords, -1.0f, 1.0f);
	coords.x = fma(pc.invFrameSize.x, 0.25f, coords.x);
	float3 t = float3(pc.invFrameSize, 1.0f);

	while (coords.z < float(MAX_SEARCH_STEPS_DIAG - 1) && coords.w > 0.9f)
	{
		coords.xyz = fma(t, float3(dir, 1.0f), coords.xyz);

		// Fetch both edges at once using bilinear filtering:
		e = textureLod(edgesBuffer, coords.xy, 0.0f).rg;
		e = decodeDiagBilinearAccess(e);
		coords.w = dot(e, float2(0.5f));
	}
	return coords.zw;
}

//**********************************************************************************************************************
// This allows to determine how much length should we add in the last step of the searches. It takes the 
// bilinearly interpolated edge, and adds 0, 1 or 2, depending on which edges and crossing edges are active.
float searchLength(float2 e, float offset)
{
	// The texture is flipped vertically, with left and right cases taking half of the space horizontally:
	const float2 scale = (float2(0.5f, -1.0f) * SEARCH_LUT_SIZE + 
		float2(-1.0f, 1.0f)) * (1.0f / SEARCH_LUT_PACKED_SIZE);
	float2 bias = fma(float2(offset, 1.0f), SEARCH_LUT_SIZE, float2(0.5f, -0.5f)); 

	// Convert from pixel coordinates to texcoords:
	// (We use SEARCH_LUT_PACKED_SIZE because the texture is cropped)
	bias *= 1.0f / SEARCH_LUT_PACKED_SIZE;
	return textureLod(searchLUT, fma(e, scale, bias), 0.0f).r;
}

// Horizontal/vertical search functions for the 2nd pass.
float searchLeftX(float2 texCoords, float end)
{
	// This texCoords has been offset by (-0.25, -0.125) in the vertex shader to sample between edge, thus fetching 
	// four edges in a row. Sampling with different offsets in each direction allows to disambiguate which edges are 
	// active from the four fetched ones.

	float2 e = float2(0.0f, 1.0f);
	while (texCoords.x > end && 
		e.g > 0.8281f && // Is there some edge not activated?
		e.r == 0.0f) // Or is there a crossing edge that breaks the line?
	{ 
		e = textureLod(edgesBuffer, texCoords, 0.0f).rg;
		texCoords = fma(pc.invFrameSize, float2(-2.0f, 0.0f), texCoords);
	}
	float offset = fma(searchLength(e, 0.0f), -(255.0 / 127.0), 3.25f);
	return fma(offset, pc.invFrameSize.x, texCoords.x);
}
float searchRightX(float2 texCoords, float end)
{
	float2 e = float2(0.0f, 1.0f);
	while (texCoords.x < end && e.g > 0.8281f && e.r == 0.0f)
	{
		e = textureLod(edgesBuffer, texCoords, 0.0f).rg;
		texCoords = fma(pc.invFrameSize, float2(2.0f, 0.0f), texCoords);
	}
	float offset = fma(searchLength(e, 0.5f), -(255.0 / 127.0), 3.25f);
	return fma(offset, -pc.invFrameSize.x, texCoords.x);
}
float searchUpY(float2 texCoords, float end)
{
	float2 e = float2(1.0f, 0.0f);
	while (texCoords.y > end && e.r > 0.8281f && e.g == 0.0f)
	{
		e = textureLod(edgesBuffer, texCoords, 0.0f).rg;
		texCoords = fma(pc.invFrameSize, float2(0.0f, -2.0f), texCoords);
	}
	float offset = fma(searchLength(e.gr, 0.0f), -(255.0 / 127.0), 3.25f);
	return fma(offset, pc.invFrameSize.y, texCoords.y);
}
float searchDownY(float2 texCoords, float end)
{
	float2 e = float2(1.0f, 0.0f);
	while (texCoords.y < end && e.r > 0.8281f && e.g == 0.0f)
	{
		e = textureLod(edgesBuffer, texCoords, 0.0f).rg;
		texCoords = fma(pc.invFrameSize, float2(0.0, 2.0f), texCoords);
	}
	float offset = fma(searchLength(e.gr, 0.5f), -(255.0 / 127.0), 3.25f);
	return fma(offset, -pc.invFrameSize.y, texCoords.y);
}

void detectHorizontalCornerPattern(inout float2 weights, float4 texCoords, float2 d)
{
	float2 leftRight = step(d.xy, d.yx);
	float2 rounding = leftRight * (1.0 - CORNER_ROUNDING_NORM);
	rounding /= leftRight.x + leftRight.y; // Reduce blending for pixels in the center of a line.
	rounding = -rounding;

	float2 factor = float2(1.0f);
	factor.x += rounding.x * textureLodOffset(edgesBuffer, texCoords.xy, 0.0f, int2(0,  1)).r;
	factor.x += rounding.y * textureLodOffset(edgesBuffer, texCoords.zw, 0.0f, int2(1,  1)).r;
	factor.y += rounding.x * textureLodOffset(edgesBuffer, texCoords.xy, 0.0f, int2(0, -2)).r;
	factor.y += rounding.y * textureLodOffset(edgesBuffer, texCoords.zw, 0.0f, int2(1, -2)).r;
	weights *= saturate(factor);
}
void detectVerticalCornerPattern(inout float2 weights, float4 texCoords, float2 d)
{
	float2 leftRight = step(d.xy, d.yx);
	float2 rounding = leftRight * (1.0 - CORNER_ROUNDING_NORM);
	rounding /= leftRight.x + leftRight.y;
	rounding = -rounding;

	float2 factor = float2(1.0f);
	factor.x += rounding.x * textureLodOffset(edgesBuffer, texCoords.xy, 0.0f, int2( 1, 0)).g;
	factor.x += rounding.y * textureLodOffset(edgesBuffer, texCoords.zw, 0.0f, int2( 1, 1)).g;
	factor.y += rounding.x * textureLodOffset(edgesBuffer, texCoords.xy, 0.0f, int2(-2, 0)).g;
	factor.y += rounding.y * textureLodOffset(edgesBuffer, texCoords.zw, 0.0f, int2(-2, 1)).g;
	weights *= saturate(factor);
}

//**********************************************************************************************************************
float2 calculateDiagWeights(float2 e)
{
	float2 weights = float2(0.0f);

	// Search for the line ends:
	float4 d; float2 end;
	if (e.r > 0.0f)
	{
		d.xz = searchDiag1(float2(-1.0f, 1.0f), end);
		d.x += float(end.y > 0.9f);
	}
	else d.xz = float2(0.0f);

	d.yw = searchDiag1(float2(1.0, -1.0), end);

	if (d.x + d.y > 2.0f) // d.x + d.y + 1 > 3
	{ 
		// Fetch the crossing edges:
		float4 coords = fma(float4(-d.x + 0.25f, d.x, d.y, -d.y - 0.25f), pc.invFrameSize.xyxy, fs.texCoords.xyxy);

		float4 c;
		c.xy = textureLodOffset(edgesBuffer, coords.xy, 0.0f, int2(-1,  0)).rg;
		c.zw = textureLodOffset(edgesBuffer, coords.zw, 0.0f, int2( 1,  0)).rg;
		c.yxwz = decodeDiagBilinearAccess(c);

		// Merge crossing edges at each side into a single value:
		float2 cc = fma(c.xz, float2(2.0f), c.yw);

		// Remove the crossing edge if we didn't found the end of the line:
		cc = lerp(cc, float2(0.0f), step(float2(0.9f), d.zw));

		// Fetch the areas for this line:
		weights += areaDiag(d.xy, cc);
	}

	// Search for the line ends:
	d.xz = searchDiag2(float2(-1.0f), end);

	if (textureLodOffset(edgesBuffer, fs.texCoords, 0.0f, int2(1, 0)).r > 0.0f)
	{
		d.yw = searchDiag2(float2(1.0f), end);
		d.y += float(end.y > 0.9f);
	}
	else d.yw = float2(0.0f);

	if (d.x + d.y > 2.0f) // d.x + d.y + 1 > 3
	{
		// Fetch the crossing edges:
		float4 coords = fma(float4(-d.x, -d.x, d.y, d.y), pc.invFrameSize.xyxy, fs.texCoords.xyxy);

		float4 c;
		c.x  = textureLodOffset(edgesBuffer, coords.xy, 0.0f, int2(-1,  0)).g;
		c.y  = textureLodOffset(edgesBuffer, coords.xy, 0.0f, int2( 0, -1)).r;
		c.zw = textureLodOffset(edgesBuffer, coords.zw, 0.0f, int2( 1,  0)).gr;
		float2 cc = fma(c.xz, float2(2.0f), c.yw);

		// Remove the crossing edge if we didn't found the end of the line:
		cc = lerp(cc, float2(0.0f), step(float2(0.9f), d.zw));

		// Fetch the areas for this line:
		weights += areaDiag(d.xy, cc).gr;
	}

	return weights;
}

//**********************************************************************************************************************
void main()
{
	float4 weights = float4(0.0f);
	float2 e = textureLod(edgesBuffer, fs.texCoords, 0.0f).rg;

	if (e.g > 0.0f) // Edge at north
	{ 
		// Diagonals have both north and west edges, so searching for them in one of the boundaries is enough.
		if (MAX_SEARCH_STEPS_DIAG > 0)
			weights.rg = calculateDiagWeights(e);

		// We give priority to diagonals, so if we find a diagonal we skip horizontal/vertical processing.
		if (MAX_SEARCH_STEPS_DIAG == 0 || weights.r == -weights.g) // weights.r + weights.g == 0
		{ 
			float2 d;

			// Find the distance to the left:
			float3 coords;
			coords.x = searchLeftX(fs.offset0.xy, fs.offset2.x);
			coords.y = fs.offset1.y; // fs.offset1.y = fs.texCoords.y - 0.25 * pc.invFrameSize.y
			d.x = coords.x;

			// Now fetch the left crossing edges, two at a time using bilinear filtering. 
			// Sampling at -0.25 enables to discern what value each edge has:
			float e1 = textureLod(edgesBuffer, coords.xy, 0.0f).r;

			// Find the distance to the right:
			coords.z = searchRightX(fs.offset0.zw, fs.offset2.y);
			d.y = coords.z;

			// We want the distances to be in pixel units (doing this here 
			// allow to better interleave arithmetic and memory accesses):
			d = abs(round(fma(d, pc.frameSize.xx, -fs.pixCoords.xx)));

			// area() below needs a sqrt, as the areas texture is compressed quadratically:
			float2 sqrtD = sqrt(d);

			// Fetch the right crossing edges:
			float e2 = textureLodOffset(edgesBuffer, coords.zy, 0.0f, int2(1, 0)).r;

			// Ok, we know how this pattern looks like, now it is time for getting the actual area:
			weights.rg = area(sqrtD, e1, e2);

			// Fix corners:
			coords.y = fs.texCoords.y;

			if (CORNER_ROUNDING < 100)
				detectHorizontalCornerPattern(weights.rg, coords.xyzy, d);
		}
		else e.r = 0.0f; // Skip vertical processing.
	}
	if (e.r > 0.0) // Edge at west
	{ 
		float2 d;

		// Find the distance to the top:
		float3 coords;
		coords.y = searchUpY(fs.offset1.xy, fs.offset2.z);
		coords.x = fs.offset0.x; // fs.offset1.x = fs.texCoords.x - 0.25 * pc.invFrameSize.x;
		d.x = coords.y;

		// Fetch the top crossing edges:
		float e1 = textureLod(edgesBuffer, coords.xy, 0.0f).g;

		// Find the distance to the bottom:
		coords.z = searchDownY(fs.offset1.zw, fs.offset2.w);
		d.y = coords.z;

		// We want the distances to be in pixel units:
		d = abs(round(fma(d, pc.frameSize.yy, -fs.pixCoords.yy)));

		// area() below needs a sqrt, as the areas texture is compressed quadratically:
		float2 sqrtD = sqrt(d);

		// Fetch the bottom crossing edges:
		float e2 = textureLodOffset(edgesBuffer, coords.xz, 0.0f, int2(0, 1)).g;

		// Get the area for this direction:
		weights.ba = area(sqrtD, e1, e2);

		// Fix corners:
		coords.x = fs.texCoords.x;

		if (CORNER_ROUNDING < 100)
			detectVerticalCornerPattern(weights.ba, coords.xyxz, d);
	}

	fb.weights = weights;
}