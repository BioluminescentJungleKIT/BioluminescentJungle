#version 450

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(set = 0, binding = 1) uniform TonemappingUBO {
    float exposure;
    float gamma;
    int mode;
} tonemapping;

vec3 hable_function(vec3 x)// from https://64.github.io/tonemapping/
{
    float A = 0.15f;
    float B = 0.50f;
    float C = 0.10f;
    float D = 0.20f;
    float E = 0.02f;
    float F = 0.30f;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

vec3 hable(vec3 color)// from https://64.github.io/tonemapping/
{
    vec3 white = vec3(11.2f);
    return hable_function(color)/hable_function(white);
}


// AgX experimental code adapted from https://github.com/sobotka/AgX-S2O3
float agxEquationHyperbolic(float term, float power) {
    return term / pow((1.0 + pow(term,power)), (1.0 / power));
}

float agxEquationTerm(float logChannel, float xPivot, float slopePivot, float scale) {
    return (slopePivot * (logChannel - xPivot)) / scale;
}

float agxEquationCurve(float logChannel, float xPivot, float yPivot, float slopePivot,
float toePower, float shoulderPower, float scale) {
    if (scale < 0.0) {
        return scale * agxEquationHyperbolic(
        agxEquationTerm(
        logChannel,
        xPivot,
        slopePivot,
        scale
        ),
        toePower
        ) + yPivot;
    } else {
        return scale * agxEquationHyperbolic(
        agxEquationTerm(
        logChannel,
        xPivot,
        slopePivot,
        scale
        ),
        shoulderPower
        ) + yPivot;
    }
}

float agxEquationScale(float xPivot, float yPivot, float slopePivot, float power) {
    return pow(pow((slopePivot * xPivot), -power) * (pow((slopePivot * (xPivot / yPivot)), power) - 1.0), -1.0 / power);
}

float agxCurve(float x) {
    const float xPivot = abs(10. / (6.5 + 10.));
    const float yPivot = 0.50;
    const float slopePivot = 2.;
    const float toePower = 3.;
    const float shoulderPower = 3.25;
    float scaleXPivot = x >= xPivot ? 1. - xPivot : xPivot;
    float scaleYPivot = x >= yPivot ? 1. - yPivot : yPivot;
    float scale = x >= xPivot ?
    agxEquationScale(scaleXPivot, scaleYPivot, slopePivot, shoulderPower) :
    -agxEquationScale(scaleXPivot, scaleYPivot, slopePivot, toePower);
    return agxEquationCurve(x, xPivot, yPivot, slopePivot, toePower, shoulderPower, scale);
}

vec3 agxCurve(vec3 rgb) {
    return vec3(agxCurve(rgb.r), agxCurve(rgb.g), agxCurve(rgb.b));
}

vec3 agx(vec3 rgb) {
    mat3 agxCompressedMatrix = mat3(0.84247906, 0.0784336, 0.07922375,
    0.04232824, 0.87846864, 0.07916613,
    0.04237565, 0.0784336, 0.87914297);
    vec3 compressed = transpose(agxCompressedMatrix) * rgb;
    vec3 log = (log2(compressed) + 12.4739311883) / (12.4739311883 + 4.02606881167);
    return agxCurve(log);
}

void main() {
    vec4 color = texelFetch(texSampler, ivec2(gl_FragCoord.xy), 0);
    color.rgb *= exp2(tonemapping.exposure);
    if (tonemapping.mode == 0) {
        //clamping is automatic
    }
    if (tonemapping.mode == 1) {
        color.rgb = hable(color.rgb);
    }
    if (tonemapping.mode == 2) {
        color.rgb = agx(color.rgb);
    }
    color.rgb = pow(color.rgb, vec3(tonemapping.gamma));
    outColor = color;
}
