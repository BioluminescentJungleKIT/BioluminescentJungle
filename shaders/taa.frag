#version 450

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D currentFrame;

layout(set = 0, binding = 1) uniform TAAUBO {
    float alpha;
    int mode;
    uint width;
    uint height;
} taa;

layout(set = 0, binding = 2) uniform sampler2D albedo;
layout(set = 0, binding = 3) uniform sampler2D depth;
layout(set = 0, binding = 4) uniform sampler2D normal;
layout(set = 0, binding = 5) uniform sampler2D motion;

layout(set = 0, binding = 6) uniform sampler2D lastFrame;

// from incg assignment
vec2
sample_motion(int radius)
{
    float longest_square = 0;
    vec2 longest_vector = vec2(0, 0);
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            vec2 vector = texelFetch(motion, ivec2(gl_FragCoord.xy) + ivec2(x, y), 0).xy;
            float length_square = dot(vector, vector);
            if (length_square > longest_square) {
                longest_square = length_square;
                longest_vector = vector;
            }
        }
    }
    return longest_vector * 0.5;
}
vec3
merge_frames(
vec3 current_color,
vec3 previous_color,
float alpha,
uint clamp_method)
{
    switch (clamp_method) {
        default :
        case 0:// off
        break;
        case 1:// min max
        int clamp_radius_minmax = 1;
        vec3 minRGB = vec3(1, 1, 1);
        vec3 maxRGB = vec3(0, 0, 0);
        for (int y = -clamp_radius_minmax; y <= clamp_radius_minmax; y++) {
            for (int x = -clamp_radius_minmax; x <= clamp_radius_minmax; x++) {
                vec3 rgb = texelFetch(currentFrame, ivec2(gl_FragCoord.xy) + ivec2(x, y), 0).rgb;
                minRGB = min(minRGB, rgb);
                maxRGB = max(maxRGB, rgb);
            }
        }
        previous_color = min(maxRGB, max(minRGB, previous_color));
        break;
        case 2:// moments
        int clamp_radius_moments = 1;
        vec3 EX = vec3(0, 0, 0);
        vec3 EXX = vec3(0, 0, 0);
        for (int y = -clamp_radius_moments; y <= clamp_radius_moments; y++) {
            for (int x = -clamp_radius_moments; x <= clamp_radius_moments; x++) {
                vec3 rgb = texelFetch(currentFrame, ivec2(gl_FragCoord.xy) + ivec2(x, y), 0).rgb;
                EX += rgb;
                EXX += rgb * rgb;
            }
        }
        int clamp_diameter = clamp_radius_moments * 2 + 1;
        int clamp_area = clamp_diameter * clamp_diameter;
        EX /= clamp_area;
        EXX /= clamp_area;
        vec3 sigma = sqrt(EXX - (EX*EX));
        vec3 rgbBBmin = EX - sigma;
        vec3 rgbBBmax = EX + sigma;
        previous_color = min(rgbBBmax, max(rgbBBmin, previous_color));
        break;
    }
    current_color = previous_color * (1-alpha) + current_color * (alpha);
    return current_color;
}

// adapted from https://www.shadertoy.com/view/MtVGWz

// note: entirely stolen from https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
//
// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
// See http://vec3.ca/bicubic-filtering-in-fewer-taps/ for more details
vec4 sampleCatmullRom(sampler2D tex, vec2 uv)
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    vec2 texSize = textureSize(tex, 0);
    vec2 samplePos = uv * texSize;
    vec2 texPos1 = floor(samplePos - 0.5) + 0.5;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    vec2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5*f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5*f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5*f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    vec2 texPos0 = texPos1 - vec2(1.0);
    vec2 texPos3 = texPos1 + vec2(2.0);
    vec2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    vec4 result = vec4(0.0);
    result += textureLod(tex, vec2(texPos0.x, texPos0.y), 0) * w0.x * w0.y;
    result += textureLod(tex, vec2(texPos12.x, texPos0.y), 0) * w12.x * w0.y;
    result += textureLod(tex, vec2(texPos3.x, texPos0.y), 0) * w3.x * w0.y;

    result += textureLod(tex, vec2(texPos0.x, texPos12.y), 0) * w0.x * w12.y;
    result += textureLod(tex, vec2(texPos12.x, texPos12.y), 0) * w12.x * w12.y;
    result += textureLod(tex, vec2(texPos3.x, texPos12.y), 0) * w3.x * w12.y;

    result += textureLod(tex, vec2(texPos0.x, texPos3.y), 0) * w0.x * w3.y;
    result += textureLod(tex, vec2(texPos12.x, texPos3.y), 0) * w12.x * w3.y;
    result += textureLod(tex, vec2(texPos3.x, texPos3.y), 0) * w3.x * w3.y;

    return result;
}

void
main()
{
    vec2 tex_coord = gl_FragCoord.xy / vec2(taa.width, taa.height);
    vec2 motion_vec = sample_motion(1);

    // color in the current frame
    vec3 current_color = texture(currentFrame, tex_coord).rgb;
    // color of the previous frame
    vec3 previous_color = sampleCatmullRom(lastFrame, tex_coord + motion_vec).rgb;
    // texture is never initialized and might contain NaN
    if (any(isnan(previous_color)))
    previous_color = vec3(0);

    // perform blending between previous and current frame
    vec3 merged_color = merge_frames(current_color, previous_color, taa.alpha, taa.mode);

    outColor = vec4(merged_color, 1.0);
}