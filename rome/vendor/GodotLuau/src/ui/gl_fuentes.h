#ifndef GL_FUENTES_H
#define GL_FUENTES_H

// Fuentes de Roblox con equivalentes libres (GodotLuau/assets/fonts; las
// licencias, en GodotLuau/licenses/fonts). Antes todo el texto salia con la
// fuente por defecto de Godot y sin peso: Gotham Black se veia como una sans
// normal. Las de Google Fonts que Roblox usa tal cual (Press Start 2P, Source
// Sans, Fredoka, Luckiest Guy, Roboto...) son las mismas; Gotham es de pago y
// va con Montserrat (la mas parecida), Arial con Arimo (mismas medidas).
//
// FontFace (familia + peso + estilo) manda; sin ella, la API vieja (Enum.Font);
// sin ninguna, Legacy, que es lo que trae un texto nuevo en Roblox.

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/font_file.hpp>
#include <godot_cpp/classes/font_variation.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/text_server_manager.hpp>   // gl_texto_escalado_exacto
#include <godot_cpp/templates/hash_map.hpp>
#include "gl_gui_core.h"   // gl_tema_tam: overrides solo si cambian

using namespace godot;

// Enum.Font -> Font.fromEnum de Roblox (Studio, 08/10).
struct GLFuenteEnum { int valor; const char* familia; int peso; int estilo; };
static const GLFuenteEnum GL_FUENTES_ENUM[] = {
    { 0, "LegacyArial", 400, 0 }, { 1, "Arial", 400, 0 }, { 2, "Arial", 700, 0 },
    { 3, "SourceSansPro", 400, 0 }, { 4, "SourceSansPro", 700, 0 }, { 5, "SourceSansPro", 300, 0 },
    { 6, "SourceSansPro", 400, 1 }, { 7, "AccanthisADFStd", 400, 0 }, { 8, "Guru", 400, 0 },
    { 9, "ComicNeueAngular", 400, 0 }, { 10, "Inconsolata", 400, 0 }, { 11, "HighwayGothic", 400, 0 },
    { 12, "Zekton", 400, 0 }, { 13, "PressStart2P", 400, 0 }, { 14, "Balthazar", 400, 0 },
    { 15, "RomanAntique", 400, 0 }, { 16, "SourceSansPro", 600, 0 }, { 17, "GothamSSm", 400, 0 },
    { 18, "GothamSSm", 500, 0 }, { 19, "GothamSSm", 700, 0 }, { 20, "GothamSSm", 900, 0 },
    { 21, "AmaticSC", 400, 0 }, { 22, "Bangers", 400, 0 }, { 23, "Creepster", 400, 0 },
    { 24, "DenkOne", 400, 0 }, { 25, "Fondamento", 400, 0 }, { 26, "FredokaOne", 400, 0 },
    { 27, "GrenzeGotisch", 400, 0 }, { 28, "IndieFlower", 400, 0 }, { 29, "JosefinSans", 400, 0 },
    { 30, "Jura", 400, 0 }, { 31, "Kalam", 400, 0 }, { 32, "LuckiestGuy", 400, 0 },
    { 33, "Merriweather", 400, 0 }, { 34, "Michroma", 400, 0 }, { 35, "Nunito", 400, 0 },
    { 36, "Oswald", 400, 0 }, { 37, "PatrickHand", 400, 0 }, { 38, "PermanentMarker", 400, 0 },
    { 39, "Roboto", 400, 0 }, { 40, "RobotoCondensed", 400, 0 }, { 41, "RobotoMono", 400, 0 },
    { 42, "Sarpanch", 400, 0 }, { 43, "SpecialElite", 400, 0 }, { 44, "TitilliumWeb", 400, 0 },
    { 45, "Ubuntu", 400, 0 }, { 46, "BuilderSans", 400, 0 }, { 47, "BuilderSans", 500, 0 },
    { 48, "BuilderSans", 700, 0 }, { 49, "BuilderSans", 800, 0 }, { 50, "Arimo", 400, 0 },
    { 51, "Arimo", 700, 0 },
};

inline const GLFuenteEnum& gl_fuente_enum(int valor) {
    for (const GLFuenteEnum& e : GL_FUENTES_ENUM)
        if (e.valor == valor) return e;
    return GL_FUENTES_ENUM[0];   // Legacy
}

// Familia de Roblox -> archivo. peso 0 = fuente variable (eje wght); si no, un
// archivo por peso y se coge el mas cercano. wght_fijo: el peso con el que se
// parece a la de Roblox cuando esta solo tiene uno (Fredoka One = Fredoka 600).
struct GLFamiliaArchivo { const char* familia; const char* archivo; int peso; int wght_fijo; };
static const GLFamiliaArchivo GL_FAMILIAS_ARCHIVO[] = {
    { "GothamSSm", "montserrat-vf.woff2", 0, 0 }, { "Montserrat", "montserrat-vf.woff2", 0, 0 },
    { "LegacyArial", "arimo-vf.woff2", 0, 0 }, { "Arial", "arimo-vf.woff2", 0, 0 }, { "Arimo", "arimo-vf.woff2", 0, 0 },
    { "SourceSansPro", "sourcesans3-vf.ttf", 0, 0 }, { "SourceSans3", "sourcesans3-vf.ttf", 0, 0 },
    { "PressStart2P", "pressstart2p-regular.ttf", 400, 0 }, { "Inconsolata", "inconsolata-vf.woff2", 0, 0 },
    { "FredokaOne", "fredoka-vf.woff2", 0, 600 }, { "Fredoka", "fredoka-vf.woff2", 0, 0 },
    { "LuckiestGuy", "luckiestguy-regular.ttf", 400, 0 }, { "Bangers", "bangers-regular.woff2", 400, 0 },
    { "Roboto", "roboto-vf.woff2", 0, 0 }, { "RobotoCondensed", "robotocondensed-vf.woff2", 0, 0 },
    { "RobotoMono", "robotomono-vf.woff2", 0, 0 }, { "Oswald", "oswald-vf.woff2", 0, 0 },
    { "Nunito", "nunito-vf.woff2", 0, 0 },
    { "Ubuntu", "ubuntu-regular.ttf", 400, 0 }, { "Ubuntu", "ubuntu-bold.ttf", 700, 0 },
    { "Merriweather", "ebgaramond-vf.woff2", 0, 0 }, { "Guru", "ebgaramond-vf.woff2", 0, 0 },
    { "PermanentMarker", "permanentmarker-regular.ttf", 400, 0 },
    { "TitilliumWeb", "titilliumweb-extralight.woff2", 200, 0 }, { "TitilliumWeb", "titilliumweb-light.woff2", 300, 0 },
    { "TitilliumWeb", "titilliumweb-regular.woff2", 400, 0 }, { "TitilliumWeb", "titilliumweb-semibold.woff2", 600, 0 },
    { "TitilliumWeb", "titilliumweb-bold.woff2", 700, 0 }, { "TitilliumWeb", "titilliumweb-black.woff2", 900, 0 },
    { "Michroma", "michroma-regular.woff2", 400, 0 }, { "PatrickHand", "patrickhand-regular.woff2", 400, 0 },
    { "Kalam", "kalam-light.woff2", 300, 0 }, { "Kalam", "kalam-regular.woff2", 400, 0 }, { "Kalam", "kalam-bold.woff2", 700, 0 },
    { "IndieFlower", "indieflower-regular.woff2", 400, 0 }, { "Creepster", "creepster-regular.ttf", 400, 0 },
    { "DenkOne", "denkone-regular.ttf", 400, 0 },
    { "AmaticSC", "amaticsc-regular.woff2", 400, 0 }, { "AmaticSC", "amaticsc-bold.woff2", 700, 0 },
    { "Jura", "jura-vf.woff2", 0, 0 },
    { "Sarpanch", "sarpanch-regular.woff2", 400, 0 }, { "Sarpanch", "sarpanch-medium.woff2", 500, 0 },
    { "Sarpanch", "sarpanch-semibold.woff2", 600, 0 }, { "Sarpanch", "sarpanch-bold.woff2", 700, 0 },
    { "Sarpanch", "sarpanch-extrabold.woff2", 800, 0 }, { "Sarpanch", "sarpanch-black.woff2", 900, 0 },
    { "SpecialElite", "specialelite-regular.ttf", 400, 0 }, { "GrenzeGotisch", "grenzegotisch-vf.woff2", 0, 0 },
    { "Fondamento", "fondamento-regular.ttf", 400, 0 }, { "FrederickaTheGreat", "frederickathegreat-regular.ttf", 400, 0 },
    { "Balthazar", "balthazar-regular.ttf", 400, 0 }, { "JosefinSans", "josefinsans-vf.ttf", 0, 0 },
    { "ComicNeueAngular", "comicneue-light.woff2", 300, 0 }, { "ComicNeueAngular", "comicneue-regular.woff2", 400, 0 },
    { "ComicNeueAngular", "comicneue-bold.woff2", 700, 0 },
    { "AccanthisADFStd", "librebodoni-vf.woff2", 0, 0 }, { "HighwayGothic", "overpass-vf.woff2", 0, 0 },
    { "Zekton", "orbitron-vf.woff2", 0, 0 }, { "RomanAntique", "cinzel-vf.woff2", 0, 0 },
    { "BuilderSans", "roboto-vf.woff2", 0, 0 },
};

// Las caches viven en funciones (nada de estaticos globales de Godot en un
// header: la DLL no carga) y se sueltan al descargar la extension, con el
// servidor de texto aun vivo; si no, las fuentes salen como fugas al salir.
inline HashMap<String, Ref<FontFile>>& gl_fuentes_cache_archivos() {
    static HashMap<String, Ref<FontFile>> c;
    return c;
}
inline HashMap<String, Ref<Font>>& gl_fuentes_cache() {
    static HashMap<String, Ref<Font>> c;
    return c;
}
// De que familia, peso y estilo salio cada fuente 2D (por id): el texto de los
// carteles 3D pide la misma fuente en su version con mipmaps.
struct GLFuenteOrigen { String familia; int peso = 400; int estilo = 0; };
inline HashMap<uint64_t, GLFuenteOrigen>& gl_fuentes_origen() {
    static HashMap<uint64_t, GLFuenteOrigen> c;
    return c;
}
// ── ALTO DE LINEA DE ROBLOX QUE NO SALE DEL ARCHIVO ──────────────────
// Arcade (Press Start 2P): el archivo dice linea = 1 em (hhea, typo y win
// ascent; es lo que mide Godot) y Roblox la dibuja con em = TextSize / 1,763.
// Medido el 18/10 en la intro de Just Press it grabada (TextSize 30 bajo un
// UIScale de 2,803): paso de letra 47,7 px = 17,0 del lienzo, y los UIStroke de
// 5 y 2 salen 14,0 y 5,6 px (la escala cuadra). Con el alto del archivo, todo
// texto Arcade salia 1,76 veces mas grande que en Roblox. Va por fuente (id),
// en sus versiones 2D y 3D: lo lee gl_fuente_razon.
struct GLLineaFamilia { const char* familia; float linea_por_em; };
static const GLLineaFamilia GL_LINEA_FAMILIA[] = { { "PressStart2P", 1.763f } };
inline HashMap<uint64_t, float>& gl_fuentes_linea() {
    static HashMap<uint64_t, float> c;
    return c;
}
inline void gl_fuentes_soltar() {
    gl_fuentes_cache().clear();
    gl_fuentes_cache_archivos().clear();
    gl_fuentes_origen().clear();
    gl_fuentes_linea().clear();
}

// El archivo de fuente, cargado una vez. Importado (editor y juego exportado)
// o, si aun no lo esta, directo del disco.
//
// SIN MSDF (la fuente por defecto si lo lleva, ver register_types): las
// variables de Google Fonts tienen contornos que se solapan y con MSDF la "B"
// de Montserrat a 900 salia con manchas dentro (probado en Label3D: con MSDF
// mancha con y sin contorno; sin MSDF, limpia). Probado tambien (08/10) dibujar
// solo el texto GRANDE en MSDF con las fuentes estaticas: quita ~150 ms de
// esperas a la GPU del primer frame, pero con el juego cargado cuesta +0,4 ms de
// GPU, +65 llamadas de dibujo y tirones al generar letras nuevas. Descartado.
//
// MIPMAPS SOLO EN LA VERSION 3D (`tres_d`), que es otra copia de la fuente. Los
// carteles 3D los necesitan para no hormiguear al alejarse; la UI 2D no (se
// dibuja a su tamano), y le salian carisimos: cada vez que una etiqueta dibuja
// letras nuevas, Godot rehace el atlas de la fuente ENTERO con sus mipmaps y lo
// vuelve a subir a la GPU. Medido en el primer frame de Build a Mountain: 186 ms
// de mipmaps y 273 ms esperando a la GPU, etiqueta tras etiqueta.
inline Ref<FontFile> gl_fuente_archivo(const char* archivo, bool tres_d = false) {
    HashMap<String, Ref<FontFile>>& cache = gl_fuentes_cache_archivos();
    const String nombre(archivo);
    const String clave = tres_d ? nombre + String("|3d") : nombre;
    if (Ref<FontFile>* f = cache.getptr(clave)) return *f;
    Ref<FontFile> f;
    if (tres_d) {
        // Otra fuente con los mismos datos y ajustes: el recurso cargado lo
        // comparte todo el que lo use. Sin duplicate(), que copia tambien la
        // cache de letras ya pintadas (37 ms medidos).
        const Ref<FontFile> base = gl_fuente_archivo(archivo, false);
        if (base.is_valid()) {
            f.instantiate();
            f->set_data(base->get_data());
            static const char* kAjustes[] = {
                "antialiasing", "hinting", "subpixel_positioning", "oversampling", "fixed_size",
                "fixed_size_scale_mode", "force_autohinter", "allow_system_fallback",
                "disable_embedded_bitmaps", "keep_rounding_remainders", "modulate_color_glyphs",
                "opentype_feature_overrides", "fallbacks", nullptr };
            for (int i = 0; kAjustes[i]; i++) {
                const Variant v = base->get(kAjustes[i]);
                if (v.get_type() != Variant::NIL) f->set(kAjustes[i], v);
            }
            f->set_generate_mipmaps(true);
        }
    } else {
        const String ruta = String("res://GodotLuau/assets/fonts/") + nombre;
        ResourceLoader* rl = ResourceLoader::get_singleton();
        if (rl->exists(ruta)) f = rl->load(ruta);
        if (f.is_null()) {
            Ref<FontFile> crudo;
            crudo.instantiate();
            if (crudo->load_dynamic_font(ProjectSettings::get_singleton()->globalize_path(ruta)) == OK) f = crudo;
        }
        if (f.is_valid() && f->get_generate_mipmaps()) f->set_generate_mipmaps(false);
    }
    cache.insert(clave, f);
    return f;
}

// La fuente de una familia de Roblox ("rbxasset://fonts/families/GothamSSm.json"
// o "GothamSSm") con su peso (100..900) y estilo (0 normal, 1 cursiva). Cada
// combinacion se crea una vez. `tres_d`: la version con mipmaps, para carteles.
// El archivo de una familia de Roblox para un peso: el mas cercano de la tabla.
inline const GLFamiliaArchivo* gl_familia_elegida(const String& nombre, int peso) {
    const GLFamiliaArchivo* elegido = nullptr;
    int distancia = 1 << 30;
    const CharString nombre_c = nombre.utf8();
    for (const GLFamiliaArchivo& a : GL_FAMILIAS_ARCHIVO) {
        if (strcmp(a.familia, nombre_c.get_data()) != 0) continue;
        const int d = a.peso == 0 ? 0 : ABS(a.peso - peso);
        if (d < distancia) { distancia = d; elegido = &a; }
    }
    // Una familia que no conocemos: la de un texto nuevo (Legacy).
    return elegido ? elegido : &GL_FAMILIAS_ARCHIVO[2];
}

inline Ref<Font> gl_fuente(const String& familia, int peso, int estilo, bool tres_d = false) {
    HashMap<String, Ref<Font>>& cache = gl_fuentes_cache();
    const String nombre = familia.get_file().get_basename();
    const String clave = nombre + String("|") + itos(peso) + String("|") + itos(estilo) + (tres_d ? "|3d" : "");
    if (Ref<Font>* f = cache.getptr(clave)) return *f;

    const GLFamiliaArchivo* elegido = gl_familia_elegida(nombre, peso);

    Ref<Font> resultado;
    Ref<FontFile> base = gl_fuente_archivo(elegido->archivo, tres_d);
    if (base.is_valid()) {
        Ref<FontVariation> v;
        v.instantiate();
        v->set_base_font(base);
        if (elegido->peso == 0) {
            // La etiqueta OpenType 'wght' (lo que da TextServer::name_to_tag).
            static const int64_t WGHT = ((int64_t)'w' << 24) | ((int64_t)'g' << 16) | ((int64_t)'h' << 8) | (int64_t)'t';
            Dictionary ejes;
            ejes[WGHT] = (float)(elegido->wght_fijo ? elegido->wght_fijo : CLAMP(peso, 100, 1000));
            v->set_variation_opentype(ejes);
        }
        // Cursiva sintetica: se inclina (los archivos rectos son la mitad de peso).
        if (estilo == 1) v->set_variation_transform(Transform2D(Vector2(1, 0), Vector2(0.2f, 1), Vector2()));
        resultado = v;
        for (const GLLineaFamilia& l : GL_LINEA_FAMILIA)
            if (nombre == l.familia) gl_fuentes_linea().insert((uint64_t)v->get_instance_id(), l.linea_por_em);
        if (!tres_d) {
            GLFuenteOrigen o;
            o.familia = familia;
            o.peso = peso;
            o.estilo = estilo;
            gl_fuentes_origen().insert((uint64_t)v->get_instance_id(), o);
        }
    }
    cache.insert(clave, resultado);
    return resultado;
}

// La version para carteles 3D (con mipmaps) de una fuente de la UI. Las que no
// salieron de gl_fuente (la de serie de Godot, que ya va con MSDF) se quedan.
inline Ref<Font> gl_fuente_3d(const Ref<Font>& f) {
    if (f.is_null()) return f;
    const GLFuenteOrigen* o = gl_fuentes_origen().getptr((uint64_t)f->get_instance_id());
    if (!o) return f;
    const Ref<Font> f3 = gl_fuente(o->familia, o->peso, o->estilo, true);
    return f3.is_valid() ? f3 : f;
}

// ¿La fuente salio de gl_fuente (una de Roblox)? La de serie de Godot va con
// MSDF y el texto de los carteles del mundo lee la COBERTURA de la letra, no una
// distancia: un Control sin fuente de Roblox se mide y se dibuja con Legacy,
// que es la de un texto nuevo en Roblox.
inline bool gl_fuente_es_de_roblox(const Ref<Font>& f) {
    return f.is_valid() && gl_fuentes_origen().has((uint64_t)f->get_instance_id());
}

// Familia, peso y estilo de un texto: FontFace; sin ella, Enum.Font (escrito o
// importado); sin nada, Legacy.
inline void gl_fuente_de(const Control* c, String& familia, int& peso, int& estilo) {
    static const StringName k_face("FontFace"), k_peso("_gl_font_weight"), k_estilo("_gl_font_style");
    static const StringName k_font_escrito("_glp_Font"), k_font_importado("Font");
    familia = c->get(k_face);
    peso = c->has_meta(k_peso) ? (int)c->get_meta(k_peso) : 400;
    estilo = c->has_meta(k_estilo) ? (int)c->get_meta(k_estilo) : 0;
    if (familia.is_empty()) {
        const int v = c->has_meta(k_font_escrito) ? (int)c->get_meta(k_font_escrito)
                    : c->has_meta(k_font_importado) ? (int)c->get_meta(k_font_importado) : 0;
        const GLFuenteEnum& e = gl_fuente_enum(v);
        familia = e.familia;
        peso = e.peso;
        estilo = e.estilo;
    }
}

// ── TAMANO: TextSize de Roblox -> font_size de Godot ─────────────────
// Roblox mide el texto por LINEA: con toda fuente moderna una linea mide
// TextSize (TextBounds.Y, medido en Studio el 08/10 de 8 a 100) y con
// LegacyArial (Enum.Font.Legacy) 1,5 x TextSize. Godot mide por em: la linea de
// Montserrat a font_size N mide 1,22 N. Con font_size = TextSize todo texto de
// tamano fijo salia un 17-26 % mas grande que en Roblox (TextScaled ya ajustaba
// por la linea y cuadraba). El font_size que da la linea de Roblox:
//   font_size = TextSize x (1,5 si Legacy) / (alto de linea / em)
inline float gl_fuente_razon(const Ref<Font>& f) {
    if (f.is_null()) return 1.0f;
    static HashMap<uint64_t, float> cache;   // solo numeros: nada que soltar al salir
    const uint64_t id = f->get_instance_id();
    if (const float* l = gl_fuentes_linea().getptr(id)) return *l;
    if (const float* r = cache.getptr(id)) return *r;
    const float r = CLAMP((float)f->get_height(100) / 100.0f, 0.5f, 3.0f);
    cache.insert(id, r);
    return r;
}
inline bool gl_familia_legacy(const String& familia) {
    return familia.get_file().get_basename() == "LegacyArial";
}
// ── EL ANCHO DE LA FUENTE DE ROBLOX FRENTE A LA QUE LA SUSTITUYE ──────
// Gotham (GothamSSm) va con Montserrat, y a igual TextSize la de Roblox es algo
// mas ANCHA. Medido el 08/10 sin perdida en dos rotulos de Build a Mountain que
// llenan su caja de alto (mismo TextSize en los dos motores): "PIXEL PEAK" 242
// px contra 239 (+1,3 %) y "0% BUILT" 111 contra 108 (+2,7 %), con la misma
// altura de letra. La media, +2 %. Decide cortes de linea al limite: "FREE GIFT"
// a TextSize 52 mide 230,7 en Montserrat para una caja de 232 (cabe en una
// linea) y en Roblox no cabe (dos lineas). Lo usan los carteles del mundo al
// medir y al dibujar (gl_world_gui.h). Las demas familias, 1.
inline float gl_fuente_ancho_roblox(const String& familia) {
    return familia.get_file().get_basename() == "GothamSSm" ? 1.02f : 1.0f;
}
// Alto de una linea en pixeles, como TextBounds.Y de Roblox.
inline float gl_linea_roblox(float text_size, bool legacy) {
    return text_size * (legacy ? 1.5f : 1.0f);
}
inline int gl_tam_godot_de(const Ref<Font>& f, bool legacy, float text_size) {
    return MAX(1, (int)Math::round(gl_linea_roblox(text_size, legacy) / gl_fuente_razon(f)));
}
// El de un texto con su fuente puesta. Tope de 6 por abajo: el glifo sale vacio.
inline int gl_tam_godot(const Control* c, float text_size) {
    String familia;
    int peso = 400, estilo = 0;
    gl_fuente_de(c, familia, peso, estilo);
    static const StringName k_font("font");
    return CLAMP(gl_tam_godot_de(c->get_theme_font(k_font), gl_familia_legacy(familia), text_size), 6, 400);
}

// ── TextScaled: el mayor tamano con el que el texto CABE ──────────────
// En font_size de Godot (em), entre `lo` y `mx`. Roblox SIEMPRE envuelve con
// TextScaled (ignora TextWrapped): se mide MULTILINEA con el ancho de la caja y
// una palabra que no cabe en el ancho no se parte, encoge. Lo usa la UI 2D
// (_gl_refit_text), que necesita un font_size entero; los carteles del mundo
// usan la misma regla contada en TextSize de Roblox (gl_texto_escalado_exacto,
// abajo): antes tenian su propia cuenta en UNA linea y "FREE GIFT" (caja
// 232x104) salia en una linea y pequeno donde Roblox pone "FREE" / "GIFT".
// El tope que llena el alto con UNA linea es alto / razon. Una medida a ese
// tope da el tamano que cabe EN UNA LINEA (casi siempre la respuesta); solo si
// con salto de linea cabe uno mayor se busca entre ese y el tope. Unas 3
// medidas, no un barrido de 1..100 (hay 1988 TextLabel en un place real).
inline int gl_texto_escalado(const Ref<Font>& f, const String& t, const Vector2& box, int lo, int mx) {
    lo = MAX(1, lo);
    if (f.is_null() || t.is_empty() || box.x < 1.0f || box.y < 1.0f) return lo;
    const int hi = CLAMP((int)(box.y / gl_fuente_razon(f)) + 1, lo, MAX(lo, mx));
    auto cabe = [&](int s) -> bool {
        const Vector2 m = f->get_multiline_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, box.x, s);
        return m.y <= box.y && m.x <= box.x + 0.5f;
    };
    const float ancho_hi = f->get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, hi).x;
    int s1 = (ancho_hi <= box.x + 0.5f) ? hi : (int)((float)hi * box.x / MAX(1.0f, ancho_hi));
    s1 = CLAMP(s1, lo, hi);
    while (s1 > lo && !cabe(s1)) s1--;
    int res = s1;
    if (s1 < hi && cabe(s1 + 1)) {
        int a = s1 + 2, b = hi;
        res = s1 + 1;
        while (a <= b) {
            const int mid = (a + b) / 2;
            if (cabe(mid)) { res = mid; a = mid + 1; } else b = mid - 1;
        }
    }
    return res;
}

// ── TextScaled EXACTO, en unidades de Roblox (para lo que se dibuja a escala) ──
// Roblox busca el mayor TextSize ENTERO, y TextSize es el alto de la LINEA. Los
// font_size de Godot (em) tambien son enteros, y entre dos seguidos la linea
// salta 1,2 px: "FREE GIFT" en 232x104 (el cartel del regalo de Build a
// Mountain) cabia en UNA linea a em 42 (linea 51,2) y a em 43 las dos lineas
// median 104,8 y no cabian, asi que salia en una linea. En Roblox, TextSize 52:
// dos lineas de 52 justas, "FREE" / "GIFT". Los carteles del mundo dibujan la
// letra con una malla escalada (cualquier tamaño vale), asi que se busca el
// TextSize entero como Roblox y la em sale con decimales.
// Se da forma al texto UNA vez, a em 100, y cada tamaño probado solo lo vuelve
// a cortar a su ancho (lo caro es dar la forma). Devuelve el TextSize; `em` sale
// con el font_size de Godot y `partido` con el texto y sus saltos de linea.
// s_max <= 0: sin tope (lo pone el alto de la caja). `ancho_rel`: lo que mide de
// mas la fuente de Roblox (gl_fuente_ancho_roblox).
inline int gl_texto_escalado_exacto(const Ref<Font>& f, const String& t, const Vector2& box, int s_min, int s_max,
                                    bool legacy, float ancho_rel, float& em, String& partido) {
    const float por_linea = legacy ? 1.5f : 1.0f;     // linea de Roblox por TextSize
    const float razon = gl_fuente_razon(f);            // linea de Godot por em
    partido = t;
    s_min = MAX(1, s_min);
    em = (float)s_min * por_linea / razon;
    TextServer* ts = TextServerManager::get_singleton()->get_primary_interface().ptr();
    if (!ts || f.is_null() || t.is_empty() || box.x < 1.0f || box.y < 1.0f) return s_min;
    const int REF = 100;
    const RID forma = ts->create_shaped_text();
    ts->shaped_text_add_string(forma, t, f->get_rids(), REF, f->get_opentype_features(), "");
    // Avance de cada letra (a em 100) y donde empieza en el texto: el ancho de
    // una linea es la suma de las suyas.
    LocalVector<int> inicio;
    LocalVector<float> avance;
    {
        static const StringName k_start("start"), k_adv("advance"), k_rep("repeat");
        const TypedArray<Dictionary> glifos = ts->shaped_text_get_glyphs(forma);
        for (int g = 0; g < glifos.size(); g++) {
            const Dictionary d = glifos[g];
            inicio.push_back((int)(int64_t)d[k_start]);
            avance.push_back((float)(double)d[k_adv] * (float)MAX(1, (int)(int64_t)d[k_rep]));
        }
    }
    const BitField<TextServer::LineBreakFlag> flags = TextServer::BREAK_MANDATORY | TextServer::BREAK_WORD_BOUND |
                                                      TextServer::BREAK_TRIM_EDGE_SPACES;
    ancho_rel = MAX(ancho_rel, 0.1f);
    auto cortar = [&](int s) -> PackedInt32Array {
        const float k = ((float)s * por_linea / razon) / (float)REF * ancho_rel;   // de em 100 a este tamaño
        return ts->shaped_text_get_line_breaks(forma, box.x / k, 0, flags);
    };
    auto cabe = [&](int s) -> bool {
        const PackedInt32Array c = cortar(s);
        const int n = MAX(1, c.size() / 2);
        if ((float)n * (float)s * por_linea > box.y + 0.01f) return false;
        const float k = ((float)s * por_linea / razon) / (float)REF * ancho_rel;
        for (int i = 0; i + 1 < c.size(); i += 2) {
            float ancho = 0.0f;
            for (uint32_t g = 0; g < inicio.size(); g++)
                if (inicio[g] >= c[i] && inicio[g] < c[i + 1]) ancho += avance[g];
            if (ancho * k > box.x + 0.5f) return false;   // una palabra mas ancha que la caja
        }
        return true;
    };
    int hi = (int)(box.y / por_linea + 0.0001f);
    if (s_max > 0) hi = MIN(hi, s_max);
    int res = s_min;
    if (hi >= s_min) {
        int a = s_min, b = hi;
        while (a <= b) {
            const int mid = (a + b) / 2;
            if (cabe(mid)) { res = mid; a = mid + 1; } else b = mid - 1;
        }
    }
    const PackedInt32Array c = cortar(res);
    String lineas;
    for (int i = 0; i + 1 < c.size(); i += 2) {
        if (i > 0) lineas += "\n";
        lineas += t.substr(c[i], c[i + 1] - c[i]).strip_edges();
    }
    ts->free_rid(forma);
    if (!lineas.is_empty()) partido = lineas;
    em = (float)res * por_linea / razon;
    return res;
}

// Pone a un texto (TextLabel, TextButton, TextBox) la fuente que le toca, y su
// tamano: cambia con la fuente (cada una tiene su alto de linea).
inline void gl_aplicar_fuente(Control* c) {
    if (!c) return;
    static const StringName k_font("font"), k_font_size("font_size");
    static const StringName k_scaled("TextScaled"), k_size("TextSize"), k_refit("_gl_refit_text");
    String familia;
    int peso = 400, estilo = 0;
    gl_fuente_de(c, familia, peso, estilo);
    const Ref<Font> f = gl_fuente(familia, peso, estilo);
    if (f.is_valid() && (!c->has_theme_font_override(k_font) || c->get_theme_font(k_font) != f))
        c->add_theme_font_override(k_font, f);
    if ((bool)c->get(k_scaled)) {
        c->call(k_refit);
    } else {
        const Variant ts = c->get(k_size);
        if (ts.get_type() == Variant::INT || ts.get_type() == Variant::FLOAT)
            gl_tema_tam(c, k_font_size,
                CLAMP(gl_tam_godot_de(c->get_theme_font(k_font), gl_familia_legacy(familia), (float)ts), 6, 400));
    }
}

#endif // GL_FUENTES_H
