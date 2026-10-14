#include "LightSource.h"

// ============================================================
// shader 源码
// ============================================================
// 3D 阶段：按"到玩家的世界距离"把远处压黑
// （这里不使用 matModel：场景全部用世界坐标直接画，vertexPosition 就是世界坐标）
static const char* FOG_VS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;

out vec3 fragWorldPos;
out vec2 fragTexCoord;
out vec4 fragColor;

void main()
{
    fragWorldPos = vertexPosition;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* FOG_FS = R"(#version 330
in vec3 fragWorldPos;
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec3  lightPos;       // 玩家眼睛位置（世界坐标）
uniform float lightRadius;    // 光照半径（米）
uniform float lightInner;     // 半径内这个比例范围内全亮
uniform float lightDarkness;  // 半径外压黑程度

out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord) * fragColor;
    float d = distance(fragWorldPos, lightPos);
    float t = smoothstep(lightRadius * lightInner, lightRadius, d);
    texel.rgb = mix(texel.rgb, texel.rgb * 0.03, clamp(t * lightDarkness, 0.0, 1.0));
    finalColor = texel;
}
)";

// 2D 后处理：屏幕越靠外围越模糊，并额外压暗（模拟"黑暗 + 看不清"）
static const char* BLUR_VS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* BLUR_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2  texSize;        // 画面尺寸（像素）
uniform float blurAmount;     // 模糊强度
uniform float edgeDarkness;   // 边缘额外压暗
uniform float lightInner;     // 这个比例以内保持清晰

out vec4 finalColor;

void main()
{
    vec2 uv = fragTexCoord;
    vec2 aspect = vec2(texSize.x / max(texSize.y, 1.0), 1.0);
    vec2 p = (uv - vec2(0.5)) * aspect;
    float r = clamp(length(p) / 0.5, 0.0, 1.0);      // 0 画面中心 -> 1 画面边缘
    float t = smoothstep(lightInner, 1.0, r);

    vec4 col = texture(texture0, uv);

    if (t > 0.004 && blurAmount > 0.004)
    {
        vec2 dir = uv - vec2(0.5);
        if (dot(dir, dir) < 1e-8) dir = vec2(0.0, 1.0);
        dir = normalize(dir);

        vec2 offset = dir * (t * blurAmount * 0.045);
        vec4 sum = vec4(0.0);
        for (int i = 0; i < 9; ++i)
        {
            float k = (float(i) / 8.0 - 0.5) * 2.0;   // -1 ~ 1
            sum += texture(texture0, uv + offset * k);
        }
        col = mix(col, sum / 9.0, clamp(t * 1.3, 0.0, 1.0));
    }

    col.rgb *= mix(1.0, 1.0 - edgeDarkness, t);
    finalColor = vec4(col.rgb, 1.0);
}
)";

// ============================================================
// 生命周期
// ============================================================
void LightSource::Init(int w, int h)
{
    width = (w > 0) ? w : 1;
    height = (h > 0) ? h : 1;

    target = LoadRenderTexture(width, height);
    fogShader = LoadShaderFromMemory(FOG_VS, FOG_FS);
    blurShader = LoadShaderFromMemory(BLUR_VS, BLUR_FS);

    // 取 uniform 位置（名字要和 shader 里一致）
    fogLightPosLoc = GetShaderLocation(fogShader, "lightPos");
    fogRadiusLoc = GetShaderLocation(fogShader, "lightRadius");
    fogInnerLoc = GetShaderLocation(fogShader, "lightInner");
    fogDarknessLoc = GetShaderLocation(fogShader, "lightDarkness");
    blurTexSizeLoc = GetShaderLocation(blurShader, "texSize");
    blurAmountLoc = GetShaderLocation(blurShader, "blurAmount");
    blurEdgeLoc = GetShaderLocation(blurShader, "edgeDarkness");
    blurInnerLoc = GetShaderLocation(blurShader, "lightInner");

    ready = (target.id != 0) && (fogShader.id != 0) && (blurShader.id != 0) &&
            (fogLightPosLoc >= 0) && (fogRadiusLoc >= 0) && (fogInnerLoc >= 0) &&
            (fogDarknessLoc >= 0) && (blurTexSizeLoc >= 0) && (blurAmountLoc >= 0) &&
            (blurEdgeLoc >= 0) && (blurInnerLoc >= 0);

    if (!ready)
    {
        TraceLog(LOG_WARNING,
                 "LightSource: shader or render texture unavailable -> fallback (no vision limit)");
    }
}

void LightSource::Unload()
{
    if (target.id != 0) UnloadRenderTexture(target);
    if (fogShader.id != 0) UnloadShader(fogShader);
    if (blurShader.id != 0) UnloadShader(blurShader);

    target = RenderTexture2D{};
    fogShader = Shader{};
    blurShader = Shader{};
    ready = false;
}

void LightSource::SetRadius(float meters)
{
    radius = (meters > 0.1f) ? meters : 0.1f;
}

// ============================================================
// 渲染
// ============================================================
void LightSource::BeginCapture(Vector3 playerPos)
{
    if (!ready) return;

    BeginTextureMode(target);
    ClearBackground(Color{ 4, 5, 9, 255 });   // 远处/背景先铺黑，避免露出一片亮底

    const float pos[3] = { playerPos.x, playerPos.y, playerPos.z };
    SetShaderValue(fogShader, fogLightPosLoc, pos, SHADER_UNIFORM_VEC3);
    SetShaderValue(fogShader, fogRadiusLoc, &radius, SHADER_UNIFORM_FLOAT);
    const float inner = LIGHT_INNER_RATIO;
    const float darkness = LIGHT_DARKNESS;
    SetShaderValue(fogShader, fogInnerLoc, &inner, SHADER_UNIFORM_FLOAT);
    SetShaderValue(fogShader, fogDarknessLoc, &darkness, SHADER_UNIFORM_FLOAT);

    BeginShaderMode(fogShader);
}

void LightSource::EndCapture()
{
    if (!ready) return;

    EndShaderMode();
    EndTextureMode();
}

void LightSource::Draw()
{
    if (!ready) return;

    const float texSize[2] = { (float)width, (float)height };
    const float blurAmount = LIGHT_BLUR_AMOUNT;
    const float edgeDarkness = LIGHT_EDGE_DARKNESS;
    const float inner = LIGHT_INNER_RATIO;
    SetShaderValue(blurShader, blurTexSizeLoc, texSize, SHADER_UNIFORM_VEC2);
    SetShaderValue(blurShader, blurAmountLoc, &blurAmount, SHADER_UNIFORM_FLOAT);
    SetShaderValue(blurShader, blurEdgeLoc, &edgeDarkness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(blurShader, blurInnerLoc, &inner, SHADER_UNIFORM_FLOAT);

    BeginShaderMode(blurShader);
    // 负高度把离屏（上下翻转）的画面正回来
    DrawTextureRec(target.texture,
                   Rectangle{ 0.0f, 0.0f, (float)width, -(float)height },
                   Vector2{ 0.0f, 0.0f },
                   WHITE);
    EndShaderMode();
}