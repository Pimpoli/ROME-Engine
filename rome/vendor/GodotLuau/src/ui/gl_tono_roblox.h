#ifndef GL_TONO_ROBLOX_H
#define GL_TONO_ROBLOX_H

// ── Curva de tonos 1:1 con Roblox ─────────────────────────────────────
// Medido con una carta de grises y de colores en Play de Studio (2026-10-08),
// con ExposureCompensation de -3 a 0: Roblox aplica la curva ACES (la misma de
// Godot con white 2) a CADA CANAL POR SEPARADO. Error medio de 2,2 sobre 255
// en las 499 muestras de todos los colores.
// El ACES de Godot pasa antes el color por una matriz que mezcla los canales:
// un verde puro al sol salia (197,251,143) en vez del (35,252,50) de Roblox,
// y todo el mundo se veia palido y lavado. Godot no trae esa curva por canal,
// asi que se monta asi:
//   tonemap REINHARD con un white enorme: y = x / (1 + x), por canal y
//   reversible;
//   LUT de ajuste de color: x = y / (1 - y) -> curva de Roblox -> sRGB.
// Sin ColorCorrectionEffect la LUT es 1D (1024 de ancho, por canal). Con
// ellos es 3D (48^3) y lleva tambien sus formulas, en orden (ver
// ColorCorrectionEffect::_formula). Las texturas viven como metadatos del
// Environment, asi se liberan con el.

#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/image_texture3d.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

// Un ColorCorrectionEffect de Roblox (neutro: 0, 0, 0, blanco).
struct GLCorreccion {
    float brillo = 0.0f, contraste = 0.0f, saturacion = 0.0f;
    Color tinte = Color(1, 1, 1);
};

inline float gl_tono_lineal(float c) {
    return c <= 0.04045f ? c / 12.92f : Math::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float gl_tono_srgb(float c) {
    c = Math::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * Math::pow(c, 1.0f / 2.4f) - 0.055f;
}
// ACES de Godot (el ajuste de MJP, con su exposure bias 1.8) para un gris,
// normalizado con white = 2.
inline float gl_tono_aces(float x) {
    const float A = 0.0245786f, B = 0.000090537f, C = 0.983729f, D = 0.432951f, E = 0.238081f;
    const float v = 1.8f * Math::max(x, 0.0f);
    const float f = (v * (v + A) - B) / (v * (C * v + D) + E);
    const float w = 3.6f;   // white 2 x 1.8
    const float fw = (w * (w + A) - B) / (w * (C * w + D) + E);
    return Math::clamp(f / fw, 0.0f, 1.0f);
}
// Del valor que deja el Reinhard (ya en sRGB) al de Roblox (en sRGB).
inline float gl_tono_canal(float u) {
    const float y = Math::min(gl_tono_lineal(u), 0.999999f);
    return gl_tono_srgb(gl_tono_aces(y / (1.0f - y)));
}
// ColorCorrectionEffect de Roblox, en pantalla (0..1), medido: error 0,55/255.
inline void gl_tono_correccion(const GLCorreccion& k, float* c) {
    for (int i = 0; i < 3; i++) c[i] = (c[i] - 0.5f) * (1.0f + k.contraste) + 0.5f + k.brillo;
    const float l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
    for (int i = 0; i < 3; i++) c[i] = l + (c[i] - l) * (1.0f + k.saturacion);
    c[0] *= k.tinte.r; c[1] *= k.tinte.g; c[2] *= k.tinte.b;
    for (int i = 0; i < 3; i++) c[i] = Math::clamp(c[i], 0.0f, 1.0f);
}

inline void gl_tono_aplicar(const Ref<Environment>& env, const LocalVector<GLCorreccion>& cc) {
    if (!env.is_valid()) return;
    env->set_tonemapper(Environment::TONE_MAPPER_REINHARDT);
    env->set_tonemap_white(1000.0f);
    env->set_adjustment_enabled(true);
    env->set_adjustment_brightness(1.0f);
    env->set_adjustment_contrast(1.0f);
    env->set_adjustment_saturation(1.0f);
    // Cada texel guarda el resultado en SU CENTRO: Godot muestrea la LUT con
    // el color tal cual, sin medio texel.
    if (cc.is_empty()) {
        static const StringName k1d("_gl_lut_tono1d");
        Ref<ImageTexture> lut = env->has_meta(k1d) ? Ref<ImageTexture>(env->get_meta(k1d)) : Ref<ImageTexture>();
        if (!lut.is_valid()) {
            const int N = 1024;
            PackedByteArray d;
            d.resize(N * 3);
            uint8_t* w = d.ptrw();
            for (int i = 0; i < N; i++) {
                const uint8_t v = (uint8_t)CLAMP((int)Math::round(gl_tono_canal((i + 0.5f) / N) * 255.0f), 0, 255);
                w[i * 3] = v; w[i * 3 + 1] = v; w[i * 3 + 2] = v;
            }
            lut = ImageTexture::create_from_image(Image::create_from_data(N, 1, false, Image::FORMAT_RGB8, d));
            env->set_meta(k1d, lut);
        }
        env->set_adjustment_color_correction(lut);
        return;
    }
    const int N = 48;
    float curva[N];
    for (int i = 0; i < N; i++) curva[i] = gl_tono_canal((i + 0.5f) / N);
    TypedArray<Image> capas;
    PackedByteArray d;
    d.resize(N * N * 3);
    for (int z = 0; z < N; z++) {
        uint8_t* w = d.ptrw();
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                float c[3] = { curva[x], curva[y], curva[z] };
                for (const GLCorreccion& k : cc) gl_tono_correccion(k, c);
                for (int i = 0; i < 3; i++) w[(y * N + x) * 3 + i] = (uint8_t)CLAMP((int)Math::round(c[i] * 255.0f), 0, 255);
            }
        }
        capas.push_back(Image::create_from_data(N, N, false, Image::FORMAT_RGB8, d));
    }
    static const StringName k3d("_gl_lut_tono3d");
    Ref<ImageTexture3D> lut = env->has_meta(k3d) ? Ref<ImageTexture3D>(env->get_meta(k3d)) : Ref<ImageTexture3D>();
    if (!lut.is_valid()) {
        lut.instantiate();
        lut->create(Image::FORMAT_RGB8, N, N, N, false, capas);
        env->set_meta(k3d, lut);
    } else {
        lut->update(capas);
    }
    env->set_adjustment_color_correction(lut);
}

#endif // GL_TONO_ROBLOX_H
