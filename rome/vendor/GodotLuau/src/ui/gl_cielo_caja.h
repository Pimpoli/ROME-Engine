#ifndef GL_CIELO_CAJA_H
#define GL_CIELO_CAJA_H

// ── Skybox de Roblox ──────────────────────────────────────────────────
// Sky.SkyboxBk/Dn/Ft/Lf/Rt/Up son seis imagenes, una por cara de un cubo
// alrededor de la camara. Hasta ahora el Sky de GodotLuau era siempre
// procedural y las caras se guardaban sin usarse: en Build a Mountain el cielo
// (un degradado azul muy saturado arriba y blanco en el horizonte) salia con
// otros colores.
//
// Caras (convencion de Roblox): Ft = -Z, Bk = +Z, Rt = +X, Lf = -X, Up = +Y,
// Dn = -Y. Las de los lados se ven derechas desde dentro (arriba de la imagen
// = +Y). Up y Dn van giradas como las rota la herramienta habitual de la
// comunidad (px/nx/pz/nz -> Rt/Lf/Bk/Ft; py 90 grados a la derecha -> Up; ny
// 90 a la izquierda -> Dn): Up tiene el borde de arriba hacia -X y Dn hacia +X.
// https://create.roblox.com/docs/environment/skybox
// https://devforum.roblox.com/t/custom-skyboxes-101/2849003
//
// El sol y la luna son sprites (SunTextureId, MoonTextureId) centrados en la
// direccion que da Lighting:GetSunDirection(), de SunAngularSize/MoonAngularSize
// grados. Las texturas por defecto de Roblox (rbxasset://sky/...) no se copian:
// sin textura del juego se dibuja un disco suave hecho aqui.

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include "../core/gl_asset.h"   // gl_local_asset_file / _path (gl_cielo_cara)
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>

using namespace godot;

inline const char* gl_cielo_caja_codigo() { return R"SHADER(shader_type sky;
uniform sampler2D cara_rt : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D cara_lf : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D cara_up : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D cara_dn : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D cara_bk : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D cara_ft : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D sol_tex : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D luna_tex : source_color, filter_linear_mipmap, repeat_disable;
uniform bool sol_con_tex = false;
uniform bool luna_con_tex = false;
uniform bool astros = true;
uniform float sol_tam = 0.09;
uniform float luna_tam = 0.05;
uniform vec3 sol_dir = vec3(0.0, 1.0, 0.0);
uniform vec3 luna_dir = vec3(0.0, -1.0, 0.0);
// Roblox pinta el skybox mas apagado que la textura tal cual: medido en Play
// (columna de cielo de Build a Mountain sin Atmosphere ni efectos, 48 puntos),
// la luz lineal es 0,79 de la de la textura (mediana; p25 0,69, p75 0,85).
uniform float brillo = 0.79;
uniform float estrellas = 0.0;
uniform mat3 giro = mat3(1.0);
// Velo del Atmosphere (Haze): color de la bruma (lineal) y su espesor; mas
// cuanto mas cerca del horizonte (ver AtmosphereNode::_apply).
uniform vec3 bruma_col = vec3(0.0);
uniform float bruma_k = 0.0;
// Pegado al horizonte Roblox pinta la bruma ENTERA y mas clara que la que pone
// a los objetos: en Build a Mountain la franja azul que asoma tras las islas
// (de 0 a 3 grados, camaras 2 y 4) mide (0.103, 0.214, 0.534) en lineal, el
// tono de la bruma x1.44: peso 1 hasta 0,8 grados, la mitad a 2, nada a 3,5.
// No viene del skybox (por debajo de su horizonte es blanco y gris) ni de un
// mar: no hay agua ni piezas alli. 0 = sin Atmosphere.
uniform float bruma_suelo = 0.0;

vec3 caja(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        if (d.x > 0.0) return texture(cara_rt, vec2(0.5 + 0.5 * d.z / a.x, 0.5 - 0.5 * d.y / a.x)).rgb;
        return texture(cara_lf, vec2(0.5 - 0.5 * d.z / a.x, 0.5 - 0.5 * d.y / a.x)).rgb;
    }
    if (a.y >= a.z) {
        if (d.y > 0.0) return texture(cara_up, vec2(0.5 - 0.5 * d.z / a.y, 0.5 + 0.5 * d.x / a.y)).rgb;
        return texture(cara_dn, vec2(0.5 - 0.5 * d.z / a.y, 0.5 - 0.5 * d.x / a.y)).rgb;
    }
    if (d.z > 0.0) return texture(cara_bk, vec2(0.5 - 0.5 * d.x / a.z, 0.5 - 0.5 * d.y / a.z)).rgb;
    return texture(cara_ft, vec2(0.5 + 0.5 * d.x / a.z, 0.5 - 0.5 * d.y / a.z)).rgb;
}

// Sprite de un astro: d en el plano perpendicular a su direccion, en unidades
// del radio del disco. Devuelve color (rgb) y cobertura (a).
vec4 astro(vec3 d, vec3 dir, float tam, sampler2D tex, bool con_tex) {
    float c = dot(d, dir);
    if (c <= 0.0 || tam <= 0.0) return vec4(0.0);
    vec3 t1 = normalize(abs(dir.y) > 0.99 ? cross(dir, vec3(0.0, 0.0, 1.0)) : cross(dir, vec3(0.0, 1.0, 0.0)));
    vec3 t2 = cross(t1, dir);
    vec2 p = vec2(dot(d, t1), dot(d, t2)) / (c * tam);
    if (abs(p.x) > 1.0 || abs(p.y) > 1.0) return vec4(0.0);
    if (con_tex) return texture(tex, p * 0.5 + 0.5);
    float r = length(p);
    return vec4(vec3(1.0, 0.97, 0.9), smoothstep(1.0, 0.6, r));
}

float hash3(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }

void sky() {
    vec3 d = giro * EYEDIR;
    vec3 col = caja(d) * brillo;
    // La bruma y los astros son del cielo que SE VE. El mapa de radiancia
    // (AT_CUBEMAP_PASS) da a Godot la luz ambiente y los reflejos, y Roblox
    // ilumina con el skybox limpio: en su carta, con y sin Atmosphere, una
    // pared sale igual (0,386 contra 0,395). Con la bruma dentro, la mitad de
    // abajo del cielo era azul oscuro y las caras que no miran al sol salian
    // oscuras y azuladas (pared de la mina 101,121,156 contra 118,133,159);
    // y el disco del sol x4 blanqueaba la luz de las caras de arriba.
    // (Godot no admite return en sky(): de ahi el if.)
    bool visible = !AT_CUBEMAP_PASS;
    if (visible && bruma_k > 0.0) col = mix(col, bruma_col, 1.0 - exp(-bruma_k / max(EYEDIR.y, 0.02)));
    if (visible && bruma_suelo > 0.0) col = mix(col, bruma_col * bruma_suelo, 1.0 - smoothstep(0.008, 0.06, EYEDIR.y));
    if (visible && astros) {
        // Estrellas: solo cuando el sol se ha puesto (como en Roblox).
        float noche = clamp(-sol_dir.y * 6.0, 0.0, 1.0);
        if (estrellas > 0.0 && noche > 0.0 && d.y > 0.0) {
            vec3 celda = floor(d * 220.0);
            float h = hash3(celda);
            if (h < estrellas * 0.02) col += vec3(0.9) * noche * smoothstep(0.02, 0.0, length(fract(d * 220.0) - 0.5) * 0.05);
        }
        vec4 s = astro(EYEDIR, sol_dir, sol_tam, sol_tex, sol_con_tex);
        col += s.rgb * s.a * 4.0 * clamp(sol_dir.y * 8.0 + 1.0, 0.0, 1.0);
        vec4 l = astro(EYEDIR, luna_dir, luna_tam, luna_tex, luna_con_tex);
        col = mix(col, l.rgb * 1.2, l.a * clamp(luna_dir.y * 8.0 + 1.0, 0.0, 1.0));
    }
    COLOR = col;
}
)SHADER"; }

// La cara de un id leida de su archivo (asset del place), con mipmaps. Nula si
// no hay archivo legible (descarga en curso, juego exportado con la imagen ya
// importada): entonces se pasa por gl_gui_load_image y gl_cielo_con_mipmaps.
inline Ref<Texture2D> gl_cielo_cara(const String& id) {
    String ruta = id;
    if (gl_is_roblox_asset(id)) {
        ruta = gl_local_asset_file(id);
        if (ruta.is_empty()) ruta = gl_local_asset_path(id);
    }
    if (ruta.is_empty() || !FileAccess::file_exists(ruta)) return Ref<Texture2D>();
    const String ext = ruta.get_extension().to_lower();
    if (ext != "png" && ext != "jpg" && ext != "jpeg" && ext != "webp") return Ref<Texture2D>();
    Ref<Image> img = Image::load_from_file(ruta);
    if (img.is_null() || img->is_empty() || img->is_compressed()) return Ref<Texture2D>();
    if (!img->has_mipmaps()) img->generate_mipmaps();
    return ImageTexture::create_from_image(img);
}

// Textura de una cara con mipmaps (las caras son de 1024x1024 y el cielo se
// ve a todas las distancias angulares). Se cachea por id dentro del nodo.
inline Ref<Texture2D> gl_cielo_con_mipmaps(const Ref<Texture2D>& t) {
    if (!t.is_valid()) return t;
    Ref<Image> img = t->get_image();
    if (!img.is_valid() || img->is_empty()) return t;
    if (img->is_compressed()) return t;
    img = img->duplicate();
    if (!img->has_mipmaps()) img->generate_mipmaps();
    return ImageTexture::create_from_image(img);
}

#endif // GL_CIELO_CAJA_H
