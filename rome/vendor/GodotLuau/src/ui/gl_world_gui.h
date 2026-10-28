#ifndef GL_WORLD_GUI_H
#define GL_WORLD_GUI_H

// ════════════════════════════════════════════════════════════════════
//  UI que vive EN EL MUNDO 3D: BillboardGui y SurfaceGui.
//
//  El bug que arregla: las dos eran Node3D vacios y sus hijos son Control
//  (CanvasItem). En Godot un CanvasItem se dibuja en el canvas del viewport
//  MAS CERCANO, no dentro de su padre 3D, asi que el contenido de los 1185
//  SurfaceGui y 31 BillboardGui de un place normal se pintaba TODO encima de la
//  pantalla: paneles gigantes tapando el HUD ("V.I.P!", "KILL EVERYONE",
//  "NUCLEAR BOMB", barras de vida...) aunque en Roblox estuvieran pegados a un
//  cartel al otro lado del mapa.
//
//  Lo que se hace ahora:
//   1. El subarbol Control se saca del canvas de PANTALLA con visibility_layer
//      + canvas_cull_mask del viewport. Asi NO se toca su propiedad Visible
//      (los scripts que la leen o escriben siguen viendo la verdad) y deja de
//      ensuciar la pantalla.
//   2. Su contenido se refleja en el mundo con nodos 3D: una malla de letras
//      con su shader para los textos (gl_wgui_texto) y Sprite3D para fondos e
//      imagenes, en el sitio y la escala que les toca. Con LOD
//      (visibility_range_end desde MaxDistance) para que 1185 carteles no
//      cuesten FPS.
//
//  Limitacion conocida y a la vista: es un REFLEJO del contenido (textos,
//  fondos e imagenes), no un render pixel a pixel del arbol de UI en la
//  superficie. Bordes, esquinas redondeadas y recortes se simplifican.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/sprite3d.hpp>
#include <godot_cpp/classes/canvas_item.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/font.hpp>   // TextScaled: medir el texto
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>      // el texto: una letra por cuadrado
#include <godot_cpp/classes/text_server_manager.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include "gl_gui_core.h"
#include "gl_fuentes.h"       // la fuente de cada texto, TextScaled y sus saltos de linea
#include "../core/gl_servicios.h"   // gl_en_workspace
#include "../core/gl_clone.h"       // GL_META_INTERNO: el reflejo no se clona
#include "../core/gl_graphics.h" // gl_wgui_dist_max: tope de dibujado del texto

using namespace godot;

// La capa reservada (GL_WORLD_GUI_CANVAS_LAYER) y el ajuste del cull_mask
// viven en gl_gui_core.h, que es donde tambien la usa la puerta de dibujado.

// Marca todo el subarbol CanvasItem para que el viewport lo descarte.
inline void gl_world_gui_mark(Node* n) {
    if (!n) return;
    if (CanvasItem* ci = gl_cast<CanvasItem>(n))
        ci->set_visibility_layer(GL_WORLD_GUI_CANVAS_LAYER);
    for (int i = 0; i < n->get_child_count(); i++) gl_world_gui_mark(n->get_child(i));
}

inline void gl_world_gui_cull(Node* owner) { gl_gui_cull_world_layer(owner); }

// La regla es la misma para todo lo que se dibuja, no solo para las GUI:
// vive en gl_servicios.h.
inline bool gl_world_gui_in_world(Node* n) { return gl_en_workspace(n); }

// ── ¿EL CARTEL TIENE A QUE AGARRARSE? ────────────────────────────────
// Un BillboardGui de Roblox se dibuja SOBRE algo: su Adornee, o su padre si no
// hay Adornee. Y ese algo tiene que tener posicion en el mundo, o sea ser una
// BasePart o un Attachment. Si cuelga de una CARPETA o de un Model, Roblox no
// dibuja NADA: no hay de donde sacar el sitio.
//
// Aqui si se dibujaba, y como una carpeta no tiene transform el cartel acababa
// en el ORIGEN DEL MUNDO. En el place son las plantillas `Workspace/Uis/
// BrainrotUi` y `PlayerUi`, que el juego clona sobre cada brainrot: se veian
// las dos, enteras, clavadas en el (0,0,0) encima del suelo.
//
// OJO: estan DENTRO de Workspace, asi que la regla de "solo se ve lo del
// mundo" no las pilla. Son dos cosas distintas y hacen falta las dos.
inline bool gl_world_gui_tiene_soporte(Node* host, Object* adornee) {
    if (adornee) return true;                      // el Adornee manda
    Node* p = host ? host->get_parent() : nullptr;
    if (!p) return false;
    // RBXMeshPart hereda de RobloxPart, asi que is_class los cubre a los dos.
    return p->is_class("RobloxPart") || p->is_class("Attachment");
}

// Quita el reflejo 3D (las mallas de texto y los Sprite3D que dibujan el
// cartel). Hace falta SUELTO, y no solo dentro de gl_world_gui_build, porque
// cuando un cartel deja de estar en el mundo la reconstruccion sale por la
// puerta de atras y sin esto los nodos viejos se quedaban dibujando.
inline void gl_world_gui_borrar_reflejo(Node* host) {
    if (!host) return;
    // Los reflejos son hijos INTERNOS (GetChildren no los ve): hay que pedirlos.
    for (int i = host->get_child_count(true) - 1; i >= 0; i--) {
        Node* c = host->get_child(i, true);
        if (c && c->has_meta("__gl_world_gui")) { host->remove_child(c); c->queue_free(); }
    }
}

// ── LA UI DEL MUNDO EMITE A 0,655 ────────────────────────────────────
// Medido el 08/10 en fotos SIN PERDIDA de Studio en Play (Build a Mountain,
// carteles "BLOCKS", "PIXEL PEAK", "LEADERBOARDS", "UPGRADES" y "FREE GIFT", de
// 60 a 165 studs y 4 camaras): con LightInfluence 0 y Brightness 1 Roblox pinta
// un texto o un fondo de color C como una superficie sin luz que EMITE
// 0,655 x C (en lineal), y despues le pasa lo mismo que al resto de la escena:
// niebla del Atmosphere, curva de tonos y ColorCorrection. Con ese modelo el
// relleno de los cinco sale a 2,8/255 de media de lo medido (ajuste por pixel,
// gui3d/ajuste_k3.py). Aqui la UI emitia 1,0 x C: tras la curva y el
// ColorCorrection los canales a 255 quemaban (el rojo de "BLOCKS" 254 contra
// 238, el verde de "FREE GIFT" 255 contra 240) y los medios subian 20-30
// niveles: el texto "lavado" y palido.
// La niebla SI la lleva, igual que las piezas: el negro del contorno sale
// (1,7,21) a 69 studs, (4,14,35) a 122 y (6,19,47) a 153.
static constexpr float GL_WGUI_EMISION = 0.655f;

// ── LA TRANSPARENCIA DE LA UI DEL MUNDO VA CON RAIZ ───────────────────
// Medido igual (sin perdida, Studio en Play): la sombra de los rotulos es texto
// negro con TextTransparency 0,45 (y su UIStroke igual) y sobre el cielo tapa
// como un 0,735 opaco, no un 0,55: el mismo 0,73-0,74 sacado canal a canal
// invirtiendo la curva y el ColorCorrection, y pareja en toda la franja. Es
// la raiz de la opacidad (0,742). El fondo de "SUMMIT PLAQUE" (Frame al 90 %
// sobre una tabla de ladrillo) lo confirma: en Roblox no se ve ni un ladrillo
// (desviacion 1/255) y aqui se veian todos.
// Va solo sobre las transparencias de Roblox (Text/Background/Image y la del
// UIStroke), no sobre la cobertura de las letras.
inline float gl_wgui_alfa(float a) {
    return Math::sqrt(CLAMP(a, 0.0f, 1.0f));
}

// Un color de la UI (sRGB) ya escalado a esa emision, para lo que va como color
// de vertice (los Sprite3D de fondos e imagenes, que lo pasan a lineal en su
// material), con su alfa ya pasado por gl_wgui_alfa. Brightness multiplica la
// emision. El color de vertice no pasa de 1: un Brightness por encima de 1,53
// se queda ahi (el texto, con su shader, no).
inline Color gl_wgui_emision_srgb(const Color& c, float brillo) {
    const float k = GL_WGUI_EMISION * MAX(brillo, 0.0f);
    const Color l = c.srgb_to_linear();
    Color s = Color(MIN(l.r * k, 1.0f), MIN(l.g * k, 1.0f), MIN(l.b * k, 1.0f)).linear_to_srgb();
    s.a = gl_wgui_alfa(c.a);
    return s;
}

// Datos de un elemento reflejado en el mundo.
struct GLWorldGuiItem {
    String  text;          // con los saltos de linea ya puestos (TextScaled / TextWrapped)
    Color   color = Color(1, 1, 1);        // TextColor3 / fondo / tinte de la imagen, con su alfa
    Color   stroke = Color(0, 0, 0, 0);    // el contorno con su alfa; alfa 0 = sin contorno
    float   stroke_px = 0.0f;  // grosor del contorno en pixeles del cartel (ya con el UIScale)
    Ref<Font> font;        // la del elemento, version 3D (gl_fuentes.h): Gotham, Luckiest Guy...
    Ref<Texture2D> grad_tex;   // el UIGradient del texto (gl_wgui_degradado), si lo hay
    Vector2 grad_dir = Vector2(1, 0);
    Vector2 grad_off;
    float   grad_escala = 1.0f;
    Ref<Texture2D> tex;
    Vector2 center_frac;   // centro del elemento, 0..1 sobre el area del GUI
    Vector2 size_frac;     // tamaño relativo
    float   text_frac = 0.0f;  // EM DE LA LETRA relativo al alto del cartel (no la caja)
    float   alto_px = 0.0f;    // esa misma em, en pixeles del cartel
    int     alin_x = 2;        // TextXAlignment de Roblox: Left 0, Right 1, Center 2
    int     alin_y = 1;        // TextYAlignment: Top 0, Center 1, Bottom 2
    float   interlineado = 1.0f;   // LineHeight
    float   ancho_rel = 1.0f;  // lo que es mas ancha la fuente de Roblox que la nuestra
    int     zindex = 0;
};

// Un pixel blanco. Es la "textura" de los rectangulos de color: un fondo de
// Roblox se dibuja como una imagen blanca estirada y teñida, asi que el reflejo
// no necesita un camino aparte para las cajas. Se comparte: 1 textura para los
// miles de fondos del place.
// El static va en una funcion aparte para poder soltarlo al cerrar (ver
// uninitialize_luau_module): si no, se libera con la DLL, sin RenderingServer.
inline Ref<Texture2D>& gl_wgui_pixel_blanco_ref() {
    static Ref<Texture2D> t;
    return t;
}
inline Ref<Texture2D> gl_wgui_pixel_blanco() {
    Ref<Texture2D>& t = gl_wgui_pixel_blanco_ref();
    if (t.is_null()) {
        Ref<Image> img = Image::create_empty(1, 1, false, Image::FORMAT_RGBA8);
        img->fill(Color(1, 1, 1, 1));
        t = ImageTexture::create_from_image(img);
    }
    return t;
}

// El ZIndex de Roblox del GuiObject (el z_index de Godot ya no lo lleva: en
// modo Sibling va en el indice de dibujo, ver gl_gui_aplicar_z).
inline int gl_wgui_z(Control* c) {
    static const StringName k("ZIndex");
    const Variant v = c->get(k);
    return v.get_type() == Variant::INT ? (int)v : c->get_z_index();
}

// El color de fondo de un GuiObject de Roblox (BackgroundColor3 +
// BackgroundTransparency), con el alfa ya resuelto. a<=0 = no hay fondo.
inline Color gl_wgui_fondo(Control* c) {
    const Variant vc = c->get(StringName("BackgroundColor3"));
    if (vc.get_type() != Variant::COLOR) return Color(0, 0, 0, 0);
    Color col = vc;
    const Variant vt = c->get(StringName("BackgroundTransparency"));
    col.a = (vt.get_type() == Variant::FLOAT || vt.get_type() == Variant::INT)
          ? 1.0f - CLAMP((float)vt, 0.0f, 1.0f)
          : 1.0f;
    return col;
}

// La imagen de un GuiObject: la de un ImageLabel (TextureRect) o el icono de un
// ImageButton (Button). El segundo caso FALTABA, y como un ImageButton casi
// nunca lleva texto, no se reflejaba nada de el.
inline Ref<Texture2D> gl_wgui_imagen(Control* c) {
    if (TextureRect* tr = gl_cast<TextureRect>(c)) return tr->get_texture();
    if (Button* b = gl_cast<Button>(c))            return b->get_button_icon();
    return Ref<Texture2D>();
}

// El tinte de esa imagen (ImageColor3 + ImageTransparency), igual que
// gl_wgui_fondo hace con el fondo. Se leen las propiedades de Roblox y no el
// modulate del nodo: son DOS transparencias distintas e independientes, y cada
// clase pinta la suya donde le toca (el ImageLabel en su self_modulate, el
// ImageButton en el color del icono). Antes esto era `c->get_modulate()`, que
// ademas de mezclar las dos se llevaba la herencia de los padres.
inline Color gl_wgui_tinte_imagen(Control* c) {
    Color col(1, 1, 1, 1);
    const Variant vc = c->get(StringName("ImageColor3"));
    if (vc.get_type() == Variant::COLOR) col = vc;
    const Variant vt = c->get(StringName("ImageTransparency"));
    if (vt.get_type() == Variant::FLOAT || vt.get_type() == Variant::INT)
        col.a = 1.0f - CLAMP((float)vt, 0.0f, 1.0f);
    return col;
}

// ── EL CONTORNO DE UN TEXTO (UIStroke) ───────────────────────────────
// Se lee del propio UIStroke y no del tema de la UI 2D, porque el grosor de
// Roblox tiene DOS modos (StrokeSizingMode) y el tema solo sabia uno:
//   FixedSize (0): Thickness en pixeles (con el UIScale de arriba).
//   ScaledSize (1): Thickness es una FRACCION del lado corto de la caja.
// Los rotulos de Build a Mountain ("BLOCKS", "PIXEL PEAK", los ascensores...)
// van en ScaledSize con Thickness 0,045: leido como pixeles salia un contorno
// de 1 px que no se veia, y en Roblox es el borde negro grueso de las letras
// (medido sin perdida: 3,7 px sobre 51 de letra en "BLOCKS" a 69 studs;
// 0,045 x lado corto da 4,2). StrokeSizingMode es propiedad del UIStroke (la
// misma regla la aplica el UIStroke de la UI 2D en gl_ui_modifiers.h).
// Solo cuenta ApplyStrokeMode Contextual (0): el Border es el marco de la caja.
// `caja` es la del texto en pixeles del cartel, ya con el UIScale.
inline bool gl_wgui_contorno(Control* c, const Vector2& caja, float escala, Color& color, float& px) {
    static const StringName k_modo("ApplyStrokeMode"), k_on("Enabled"), k_grosor("Thickness"),
                            k_color("Color"), k_transp("Transparency"),
                            k_tam("StrokeSizingMode"), k_tam_escrito("_glp_StrokeSizingMode");
    for (int i = 0; i < c->get_child_count(); i++) {
        Node* s = c->get_child(i);
        const Variant modo = s->get(k_modo);   // solo un UIStroke tiene ApplyStrokeMode
        if (modo.get_type() != Variant::INT || (int)modo != 0) continue;
        const Variant on = s->get(k_on);
        if (on.get_type() == Variant::BOOL && !(bool)on) continue;
        const float grosor = (float)s->get(k_grosor);
        // Propiedad registrada del UIStroke; las metas quedan de respaldo (las
        // escribian el importador y los scripts antes de registrarla).
        const Variant prop = s->get(k_tam);
        const Variant tam = prop.get_type() != Variant::NIL ? prop
                          : s->has_meta(k_tam_escrito) ? s->get_meta(k_tam_escrito)
                          : s->has_meta(k_tam) ? s->get_meta(k_tam) : Variant();
        const bool escalado = (tam.get_type() == Variant::INT || tam.get_type() == Variant::FLOAT) && (int)tam == 1;
        px = escalado ? grosor * MIN(caja.x, caja.y) : grosor * escala;
        const Variant vc = s->get(k_color);
        color = vc.get_type() == Variant::COLOR ? (Color)vc : Color(0, 0, 0);
        const Variant vt = s->get(k_transp);
        color.a = (vt.get_type() == Variant::FLOAT || vt.get_type() == Variant::INT)
                ? 1.0f - CLAMP((float)vt, 0.0f, 1.0f) : 1.0f;
        return px > 0.0f && color.a > 0.004f;
    }
    return false;
}

// ── EL DEGRADADO (UIGradient) DE UN TEXTO, MEZCLADO CON GAMMA 2 ───────
// Roblox mezcla los colores del ColorSequence con gamma 2 (gl_mezcla_gamma2,
// medido con negro->blanco: raiz de t), no en sRGB. Medido sin perdida en
// "FREE GIFT" (verde (0,255,0) -> (143,255,58) -> (85,255,0)): en la fila de en
// medio de "FREE" el rojo sale 90 y mezclando en sRGB daba 37 (la mitad de 143
// en sRGB es 71; con gamma 2, 101). La textura de la UI 2D usa la misma mezcla
// (gl_degradado_textura); el texto del mundo hace la suya: 256 muestras ya
// mezcladas, guardadas en sRGB (el shader la lee como source_color y el filtro
// interpola en lineal entre muestras). El alfa (Transparency), lineal.
// Se guardan por contenido: los rotulos con el mismo degradado comparten una.
inline HashMap<uint64_t, Ref<ImageTexture>>& gl_wgui_degradados() {
    static HashMap<uint64_t, Ref<ImageTexture>> c;
    return c;
}
inline Ref<Texture2D> gl_wgui_degradado(Node* gr) {
    static const StringName k_t("get_gl_times"), k_c("get_gl_cols"), k_at("get_gl_alpha_times"),
                            k_av("get_gl_alpha_vals");
    const PackedFloat32Array ts = gr->call(k_t);
    const PackedColorArray cs = gr->call(k_c);
    const PackedFloat32Array ats = gr->call(k_at), avs = gr->call(k_av);
    if (cs.size() < 1 || ts.size() != cs.size()) return Ref<Texture2D>();
    uint64_t firma = 1469598103934665603ULL;
    auto mezclar = [&](float v) { firma = (firma ^ (uint64_t)(int64_t)Math::round(v * 65536.0f)) * 1099511628211ULL; };
    for (int i = 0; i < ts.size(); i++) { mezclar(ts[i]); mezclar(cs[i].r); mezclar(cs[i].g); mezclar(cs[i].b); }
    for (int i = 0; i < ats.size() && i < avs.size(); i++) { mezclar(ats[i] + 7.0f); mezclar(avs[i]); }
    HashMap<uint64_t, Ref<ImageTexture>>& cache = gl_wgui_degradados();
    if (const Ref<ImageTexture>* t = cache.getptr(firma)) return *t;
    if (cache.size() > 256) cache.clear();
    auto tramo = [](const PackedFloat32Array& xs, float t, int& i, float& a) {
        i = 0;
        while (i + 1 < xs.size() && t > xs[i + 1]) i++;
        const float span = (i + 1 < xs.size()) ? xs[i + 1] - xs[i] : 0.0f;
        a = span > 0.00001f ? CLAMP((t - xs[i]) / span, 0.0f, 1.0f) : 0.0f;
    };
    const int N = 256;
    Ref<Image> img = Image::create_empty(N, 1, false, Image::FORMAT_RGBA8);
    for (int x = 0; x < N; x++) {
        const float t = (float)x / (float)(N - 1);
        int i = 0;
        float a = 0.0f;
        Color c = cs[0];
        if (cs.size() > 1 && t > ts[0]) {
            tramo(ts, t, i, a);
            const int j = MIN(i + 1, (int)cs.size() - 1);
            c = gl_mezcla_gamma2(cs[i], cs[j], a);   // gamma 2, como Roblox (gl_gui_core.h)
        }
        float alfa = 1.0f;
        if (ats.size() > 0 && ats.size() == avs.size()) {
            float tr = avs[0];
            if (ats.size() > 1 && t > ats[0]) {
                tramo(ats, t, i, a);
                const int j = MIN(i + 1, (int)avs.size() - 1);
                tr = avs[i] + (avs[j] - avs[i]) * a;
            }
            alfa = 1.0f - CLAMP(tr, 0.0f, 1.0f);
        }
        img->set_pixel(x, 0, Color(c.r, c.g, c.b, alfa));
    }
    const Ref<ImageTexture> tex = ImageTexture::create_from_image(img);
    cache.insert(firma, tex);
    return tex;
}

// ── CUANTAS PIEZAS COMO MUCHO POR CARTEL ─────────────────────────────
// Eran 4 (SurfaceGui) y 6 (BillboardGui), de cuando el reflejo solo dibujaba
// textos e imagenes. Con los fondos dentro, un cartel corriente (panel + marco
// + titulo + valor + icono) ya se pasa, y el recorte se come justo lo de abajo,
// que es el contenido. El corte sigue siendo por orden de arbol — que en Roblox
// es tambien el orden de dibujado — asi que lo que se pierde es lo ultimo.
// Cada pieza es un nodo, asi que esto es un tope de gasto: subirlo mucho se
// paga en los ~200 carteles del mundo.
static const int GL_WGUI_TOPE_ITEMS_SURFACE   = 20;
static const int GL_WGUI_TOPE_ITEMS_BILLBOARD = 14;

// La caja de un GuiObject dentro del cartel: su UDim2 (anclas y offsets)
// contra la del padre, con los offsets a la escala acumulada de los UIScale de
// arriba, y luego su propio UIScale (aqui scale + pivot_offset, ver
// UIScale::_apply) alrededor de su pivote. `escala` sale con la de sus hijos.
// Antes el reflejo no miraba el UIScale: el cartel "BLOCKS" de Build a
// Mountain (contenido al 25 % ampliado x4, para que la letra salga nitida)
// salia a la cuarta parte.
inline Rect2 gl_wgui_caja(Control* c, Vector2 org, Vector2 ref, float& escala) {
    const float bl = c->get_anchor(SIDE_LEFT)   * ref.x + c->get_offset(SIDE_LEFT)   * escala;
    const float br = c->get_anchor(SIDE_RIGHT)  * ref.x + c->get_offset(SIDE_RIGHT)  * escala;
    const float bt = c->get_anchor(SIDE_TOP)    * ref.y + c->get_offset(SIDE_TOP)    * escala;
    const float bb = c->get_anchor(SIDE_BOTTOM) * ref.y + c->get_offset(SIDE_BOTTOM) * escala;
    Rect2 caja(org.x + bl, org.y + bt, br - bl, bb - bt);
    const Vector2 sc = c->get_scale();
    if (Math::abs(sc.x - 1.0f) > 0.0001f && sc.x > 0.0f) {
        const Vector2 tam = c->get_size();
        const Vector2 piv = (tam.x > 0.0f && tam.y > 0.0f) ? c->get_pivot_offset() / tam : Vector2();
        const Vector2 nuevo = caja.size * sc.x;
        caja.position += (caja.size - nuevo) * piv;
        caja.size = nuevo;
        escala *= sc.x;
    }
    return caja;
}

// ── EL TEXTO DE UN ELEMENTO: TAMANO, SALTOS DE LINEA Y CONTORNO ──────
// Rellena el texto de `it` (ya con su centro y su tamaño). false si no hay nada
// que dibujar. `caja` en pixeles del cartel con el UIScale; `escala` el UIScale
// acumulado (con el del propio elemento).
//
// El tamaño es el de Roblox, por LINEA: TextSize es el alto de una linea
// (TextBounds.Y), no el em de Godot (gl_tam_godot_de). Antes se tomaba el alto
// de la caja como em, y la linea salia un 22 % mas alta que la caja ("0% BUILT"
// mas grande que en Roblox y "PIXEL PEAK" mas pequeño, por el tope).
//
// TextScaled: el mayor tamaño que cabe ENVOLVIENDO por palabras, la misma regla
// que la UI 2D (gl_texto_escalado) pero contada en TextSize enteros de Roblox
// (gl_texto_escalado_exacto), y el texto se parte donde lo parte Roblox.
// "FREE GIFT" (caja 232x104) sale en dos lineas, "FREE" / "GIFT", como en Roblox;
// antes, en una y mas pequeño.
// El tope de 100 de Roblox va en pixeles de SU lienzo: en un SurfaceGui y en un
// BillboardGui de tamaño fijo es el lienzo del cartel, pero un BillboardGui con
// Size en studs mide en pixeles de PANTALLA, y a la distancia de juego su caja
// casi nunca llega a 100: la letra llena la caja. Medido sin perdida: la
// mayuscula de "PIXEL PEAK" (caja 131,7 de alto) y la de "0% BUILT" (76,5) miden
// 0,562 y 0,571 de su caja; con el tope de 100, "PIXEL PEAK" salia un 25 % mas
// pequeño. `sin_tope` es ese caso.
inline bool gl_wgui_texto_de(Control* c, const Rect2& caja, float escala, float raiz_y, bool sin_tope,
                             GLWorldGuiItem& it) {
    String txt;
    if (Label* l = gl_cast<Label>(c))        txt = l->get_text();
    else if (Button* b = gl_cast<Button>(c)) txt = b->get_text();
    if (txt.strip_edges().is_empty()) return false;
    static const StringName k_font("font"), k_font_size("font_size"), k_text_size("TextSize"),
                            k_font_color("font_color"), k_scaled("TextScaled"), k_wrapped("TextWrapped"), k_ax("TextXAlignment"),
                            k_ay("TextYAlignment"), k_lh("LineHeight"), k_min("MinTextSize"),
                            k_max("MaxTextSize"), k_outline("outline_size"), k_outline_col("font_outline_color");
    // TextTransparency = 1 no se refleja (vive en el alfa del font_color). Es la
    // tercera transparencia independiente del mismo elemento: fondo, imagen y
    // texto.
    it.color = c->get_theme_color(k_font_color);
    if (it.color.a <= 0.004f) return false;

    // La fuente con la que se MIDE es la de la UI 2D; la que se dibuja, su
    // version con mipmaps (en 3D la letra hormiguearia al alejarse). Un Control
    // sin fuente de Roblox va con Legacy (la de serie de Godot es MSDF).
    Ref<Font> f = c->get_theme_font(k_font);
    if (!gl_fuente_es_de_roblox(f)) f = gl_fuente("LegacyArial", 400, 0);
    if (f.is_null()) return false;
    it.font = gl_fuente_3d(f);
    String familia;
    int peso = 400, estilo = 0;
    gl_fuente_de(c, familia, peso, estilo);
    const bool legacy = gl_familia_legacy(familia);
    it.ancho_rel = gl_fuente_ancho_roblox(familia);

    const float esc = MAX(escala, 0.0001f);
    const Vector2 caja_pre = caja.size / esc;   // la caja antes del UIScale
    const Variant v_sc = c->get(k_scaled), v_wr = c->get(k_wrapped), v_ts = c->get(k_text_size);
    const bool escalado = v_sc.get_type() == Variant::BOOL && (bool)v_sc;
    const bool envuelto = escalado || (v_wr.get_type() == Variant::BOOL && (bool)v_wr);
    // La em de la letra, con decimales: la malla del texto se escala a cualquier
    // tamaño (los font_size enteros de Godot no hacen falta aqui, ver
    // gl_texto_escalado_exacto).
    float em = 0.0f;
    // Un Label de Godot sin TextSize (no de Roblox) va con su font_size.
    const int fs_godot = v_ts.get_type() == Variant::INT ? 0 : c->get_theme_font_size(k_font_size);
    if (!escalado && fs_godot > 0) {
        it.text = txt;
        em = (float)fs_godot;
    } else if (escalado || v_ts.get_type() != Variant::INT) {
        int mn = 1, mx = sin_tope ? 0 : 100;   // TextSize de Roblox
        for (int i = 0; i < c->get_child_count(); i++) {
            Node* ch = c->get_child(i);
            if (!ch || !ch->is_class("UITextSizeConstraint")) continue;
            mn = (int)(int64_t)ch->get(k_min);
            mx = (int)(int64_t)ch->get(k_max);
            break;
        }
        gl_texto_escalado_exacto(f, txt, caja_pre, mn, mx, legacy, it.ancho_rel, em, it.text);
    } else {
        // TextSize fijo: la em exacta de su alto de linea; con TextWrapped, sus
        // lineas a ese tamaño (la caja de alto sin limite: no encoge).
        const int ts = MAX(1, (int)v_ts);
        if (envuelto) {
            gl_texto_escalado_exacto(f, txt, Vector2(caja_pre.x, 1.0e9f), ts, ts, legacy, it.ancho_rel, em, it.text);
        } else {
            it.text = txt;
            em = gl_linea_roblox((float)ts, legacy) / gl_fuente_razon(f);
        }
    }
    it.alto_px = em * esc;
    it.text_frac = raiz_y > 1.0f ? it.alto_px / raiz_y : 0.0f;

    const Variant ax = c->get(k_ax), ay = c->get(k_ay), lh = c->get(k_lh);
    it.alin_x = ax.get_type() == Variant::INT ? CLAMP((int)ax, 0, 2) : 2;
    it.alin_y = ay.get_type() == Variant::INT ? CLAMP((int)ay, 0, 2) : 1;
    it.interlineado = (lh.get_type() == Variant::FLOAT || lh.get_type() == Variant::INT)
                    ? CLAMP((float)lh, 0.1f, 10.0f) : 1.0f;

    // UIGradient hijo: en Roblox tiñe el texto. Se lee del propio UIGradient
    // (colores, Transparency, Rotation, Offset y Scale), igual que lo monta su
    // material en la UI 2D (UIGradient::_apply), pero con la mezcla en lineal
    // (gl_wgui_degradado).
    for (int g = 0; g < c->get_child_count(); g++) {
        Node* gr = c->get_child(g);
        if (!gr->has_method("gl_color_medio")) continue;
        static const StringName k_on("Enabled"), k_rot("Rotation"), k_off("Offset"), k_esc("Scale");
        const Variant en = gr->get(k_on);
        if (en.get_type() == Variant::BOOL && !(bool)en) continue;
        it.grad_tex = gl_wgui_degradado(gr);
        if (it.grad_tex.is_null()) {
            const Color m = gr->call("gl_color_medio");
            it.color = Color(it.color.r * m.r, it.color.g * m.g, it.color.b * m.b, it.color.a);
            break;
        }
        const float rad = Math::deg_to_rad((float)gr->get(k_rot));
        it.grad_dir = Vector2(Math::cos(rad), Math::sin(rad));
        const Vector2 off = gr->get(k_off);
        it.grad_off = Vector2(CLAMP(off.x, -4.0f, 4.0f), CLAMP(off.y, -4.0f, 4.0f));
        it.grad_escala = CLAMP((float)gr->get(k_esc), 0.05f, 8.0f);
        break;
    }

    // El contorno: el UIStroke (con sus dos modos de grosor) y si no, el de
    // TextStrokeTransparency, que en la UI 2D es outline_size 4 = 1 px.
    Color sc;
    float spx = 0.0f;
    if (gl_wgui_contorno(c, caja.size, esc, sc, spx)) {
        it.stroke = sc;
        it.stroke_px = spx;
    } else {
        const int os = c->get_theme_constant(k_outline);
        if (os > 0) {
            it.stroke = c->get_theme_color(k_outline_col);
            it.stroke_px = (float)os * 0.25f * esc;
        }
    }
    return true;
}

// Recoge (hasta un tope) los textos e imagenes visibles del subarbol.
//
// La medida se resuelve CONTRA EL CARTEL, no contra la ventana. Los hijos de un
// BillboardGui/SurfaceGui son Control colgando de un Node3D, asi que Godot los
// estira al tamaño de la VENTANA: leer su rect ya resuelto daba siempre "ocupa
// el 100%" y cada texto se dibujaba con el ALTO ENTERO del cartel. Por eso
// salian los "$ 17.17 M" y "Level 1 > Level 2" gigantes tapando el HUD.
// Aqui se recalcula el rect a partir de las anclas y los offsets — que es
// exactamente el UDim2 de Roblox, tal cual lo dejo gl_gui_apply_layout — contra
// la caja del padre, empezando por la del propio cartel.
//   org / ref : esquina y tamaño (en pixeles) de la caja del PADRE
//   raiz      : tamaño (en pixeles) del cartel entero, para sacar las fracciones
//   sin_tope  : TextScaled sin el tope de 100 (BillboardGui con Size en studs)
inline void gl_world_gui_collect(Node* n, Vector2 org, Vector2 ref, Vector2 raiz,
                                 LocalVector<GLWorldGuiItem>& out, int max_items, float escala = 1.0f,
                                 bool sin_tope = false) {
    if (!n || (int)out.size() >= max_items) return;
    Vector2 hijo_org = org, hijo_ref = ref;
    float escala_hijos = escala;
    if (Control* c = gl_cast<Control>(n)) {
        if (!c->is_visible()) return;   // oculto en Roblox -> no se refleja
        const Rect2 caja = gl_wgui_caja(c, org, ref, escala_hijos);
        // Una caja vacia o NEGATIVA no puede borrar el subarbol. Pasaba con los
        // contenedores cuyo offset venia de otro sistema de coordenadas: un
        // alto de -126 hacia que ni el Frame ni NADA de lo que llevaba dentro
        // se dibujara, y el cartel entero desaparecia sin un solo error.
        // Si la caja no sirve, los hijos siguen midiendo contra la del padre.
        if (caja.size.x > 0.5f && caja.size.y > 0.5f) {
            hijo_org = caja.position;
            hijo_ref = caja.size;
        }
        if (raiz.x > 1.0f && raiz.y > 1.0f && caja.size.x > 0.5f && caja.size.y > 0.5f) {
            GLWorldGuiItem it;
            it.center_frac = Vector2((caja.position.x + caja.size.x * 0.5f) / raiz.x,
                                     (caja.position.y + caja.size.y * 0.5f) / raiz.y);
            it.size_frac = Vector2(caja.size.x / raiz.x, caja.size.y / raiz.y);
            it.zindex = gl_wgui_z(c);

            // ── EL FONDO TAMBIEN ES CONTENIDO ────────────────────────────
            // Aqui solo se reflejaba TEXTO e IMAGEN. Pero en Roblox un Frame
            // ES un rectangulo de color: no "contiene" el dibujo, el dibujo es
            // el. Nuestro RobloxFrame hereda de Panel, que no es ni Label ni
            // Button ni TextureRect, asi que no producia NADA — y con el se
            // perdian los fondos de todos los demas (un TextLabel sobre un
            // panel oscuro salia flotando sobre el mundo).
            // Medido en el place: 80 de los 198 SurfaceGui del Workspace no
            // dibujaban una sola cosa, todos con un Frame como unico hijo.
            // Va ANTES que el texto y la imagen del mismo control para quedar
            // detras de ellos, como en Roblox.
            const Color fondo = gl_wgui_fondo(c);
            if (fondo.a > 0.004f) {
                GLWorldGuiItem bg = it;
                bg.color = fondo;
                bg.tex = gl_wgui_pixel_blanco();
                out.push_back(bg);
                if ((int)out.size() >= max_items) return;
            }

            // ── LA IMAGEN ────────────────────────────────────────────────
            // Un ImageButton es un Button, no un TextureRect: caia en la rama
            // del texto, se iba con el texto vacio, y el cast a TextureRect de
            // despues fallaba. Su icono no se dibujaba NUNCA.
            // ImageTransparency = 1 no se refleja: en Roblox no se ve nada, y
            // aqui se dibujaba OPACA porque el color ni se miraba.
            const Ref<Texture2D> img = gl_wgui_imagen(c);
            if (img.is_valid()) {
                const Color tinte = gl_wgui_tinte_imagen(c);
                if (tinte.a > 0.004f) {
                    GLWorldGuiItem im = it;
                    im.tex = img;
                    im.color = tinte;
                    // ScaleType.Fit (3): la imagen entera, con su proporcion,
                    // dentro de la caja. Se estiraba siempre: los trofeos de la
                    // placa de la cumbre (256x256 en una caja de 57x45) salian
                    // anchos.
                    static const StringName k_escala_tipo("ScaleType");
                    const Variant st = c->get(k_escala_tipo);
                    const Vector2 tsz = img->get_size();
                    if (st.get_type() == Variant::INT && (int)st == 3 && tsz.x > 0.5f && tsz.y > 0.5f) {
                        const float prop = tsz.x / tsz.y;
                        Vector2 dentro = caja.size;
                        if (dentro.x / dentro.y > prop) dentro.x = dentro.y * prop;
                        else dentro.y = dentro.x / prop;
                        im.size_frac = Vector2(dentro.x / raiz.x, dentro.y / raiz.y);
                    }
                    out.push_back(im);
                    if ((int)out.size() >= max_items) return;
                }
            }

            if (gl_wgui_texto_de(c, caja, escala_hijos, raiz.y, sin_tope, it)) out.push_back(it);
        }
    }
    for (int i = 0; i < n->get_child_count(); i++) {
        Node* h = n->get_child(i);
        if (h && !h->has_meta("__gl_world_gui"))
            gl_world_gui_collect(h, hijo_org, hijo_ref, raiz, out, max_items, escala_hijos, sin_tope);
    }
}

// ── Que boton hay bajo un punto del cartel ───────────────────────────────
// En Roblox los botones de un SurfaceGui pegado a una pieza SI se pulsan. Aqui
// el cartel era solo un reflejo dibujado, asi que ningun MouseButton1Click
// llegaba jamas: en el place de prueba eso dejaba muerto el boton que sube el
// nivel del tycoon, que es la unica forma de subirlo.
//
// El punto llega en pixeles del lienzo (X derecha, Y ABAJO) y se compara contra
// el MISMO rect de anclas/offsets que dibuja gl_world_gui_collect, para que lo
// que se ve y lo que se pulsa no puedan separarse.
inline bool gl_world_gui_pulsable(Control* c) {
    // Todo GuiButton nuestro (TextButton/ImageButton) expone _on_pressed.
    if (!c || !c->has_method("_on_pressed")) return false;
    // Interactable de Roblox: el boton se ve pero no se puede pulsar. Se
    // respetaba en pantalla (via mouse_filter) y no aqui, asi que un boton
    // desactivado seguia disparando su Activated desde el mundo.
    const Variant inter = c->get(StringName("Interactable"));
    if (inter.get_type() == Variant::BOOL && !(bool)inter) return false;
    return true;
}

inline void gl_world_gui_pick(Node* n, Vector2 org, Vector2 ref, Vector2 punto,
                              Control*& mejor, int& mejor_z, int prof = 0, float escala = 1.0f) {
    if (!n || prof > 24) return;
    Vector2 hijo_org = org, hijo_ref = ref;
    float escala_hijos = escala;
    if (Control* c = gl_cast<Control>(n)) {
        if (!c->is_visible()) return;   // oculto en Roblox -> no se pulsa
        const Rect2 caja = gl_wgui_caja(c, org, ref, escala_hijos);
        // La MISMA red de seguridad que en gl_world_gui_collect: si la caja del
        // padre es vacia o NEGATIVA, los hijos siguen midiendo contra la de mas
        // arriba. Sin esto lo que se ve y lo que se pulsa se separaban justo en
        // los carteles con un contenedor mal medido: se dibujaban bien y el clic
        // caia siempre fuera.
        if (caja.size.x > 0.5f && caja.size.y > 0.5f) {
            hijo_org = caja.position;
            hijo_ref = caja.size;
        }
        // Se queda el ultimo que gane: los hijos se visitan despues del padre,
        // asi que a igual ZIndex manda el mas profundo, como en Roblox.
        if (caja.has_point(punto) && gl_world_gui_pulsable(c)) {
            const int z = gl_wgui_z(c);
            if (!mejor || z >= mejor_z) { mejor = c; mejor_z = z; }
        }
    }
    for (int i = 0; i < n->get_child_count(); i++) {
        Node* h = n->get_child(i);
        if (h && !h->has_meta("__gl_world_gui"))
            gl_world_gui_pick(h, hijo_org, hijo_ref, punto, mejor, mejor_z, prof + 1, escala_hijos);
    }
}

// Construye los nodos 3D del reflejo. `w`/`h` en studs, `normal` es el eje al
// que mira la cara (para un SurfaceGui) o Vector3() para un billboard.
// Roblox usa INFINITO como "sin limite" en MaxDistance, y es su valor por
// defecto. Todo lo que pase de aqui se trata como infinito (un .rbxl que traiga
// un inf de verdad tambien cae dentro).
static constexpr float GL_WGUI_SIN_LIMITE = 1.0e9f;

// ── EL TEXTO DE LOS CARTELES: UNA MALLA DE LETRAS CON SU SHADER ──────
// Antes era un Label3D (y encima, para el degradado, una malla con este mismo
// tipo de shader). El Label3D no deja ponerle material (el color va por
// vertice, en 8 bits, y su material no se toca), y dibuja el contorno y la
// letra en DOS pasadas: con un texto semitransparente con contorno (las
// sombras de los rotulos de Build a Mountain, negro al 55 % con UIStroke al
// 55 %) la franja donde se pisan salia al 80 % y el resto al 55 %. En Roblox la
// sombra es UNA capa: pareja, medida (44,96,140) sobre el cielo en toda la
// franja.
// Ahora cada letra es UN cuadrado que lee las dos paginas de la cache de la
// fuente, la del contorno (UV) y la del relleno (UV2, en el mismo cuadrado; el
// rect del relleno en CUSTOM0 para no leer la letra de al lado), y el shader
// pone el relleno encima del contorno en la misma capa y emite a 0,655 (ver
// GL_WGUI_EMISION). La niebla la pone Godot como a cualquier pieza (sin
// fog_disabled: Roblox tambien la pone). Cuatro variantes: con prueba de
// profundidad o sin ella (AlwaysOnTop) y de una cara (SurfaceGui: en Roblox no
// se ve por detras) o de dos (billboard).
inline constexpr char GL_WGUI_SHADER_TEXTO[] = R"(
// Las letras: la cobertura va en el alfa de la pagina de la cache de la fuente.
uniform sampler2D tex_relleno : filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D tex_contorno : filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D grad_tex : source_color, filter_linear, repeat_disable;
uniform bool usar_grad = false;
uniform vec2 grad_dir = vec2(1.0, 0.0);
uniform vec2 grad_off = vec2(0.0);
uniform float grad_scale = 1.0;
uniform vec2 caja = vec2(1.0);
uniform vec4 relleno : source_color = vec4(1.0);
uniform vec4 contorno : source_color = vec4(0.0, 0.0, 0.0, 0.0);
uniform bool hay_contorno = false;
uniform float emision = 0.655;
uniform bool billboard = true;
uniform bool fijo = false;
// Donde va este elemento dentro del cartel (studs, en el plano del cartel).
uniform vec2 centro = vec2(0.0);
varying vec2 v_caja;
varying vec4 v_rect;
// La cobertura de la letra, nitida a cualquier tamano. Con los mipmaps una letra
// pequena llega desenfocada (un palo de 3 px se queda al 80 %) y el contorno
// negro de debajo asomaba por el relleno: "LEADERBOARDS" a 150 studs salia
// ocre. Roblox rasteriza la letra a su tamano en pantalla y no le pasa. Se
// vuelve a cortar en 0,5 con una rampa de un pixel de pantalla, como una SDF:
// el borde sigue donde estaba.
float nitida(float a) {
    float w = clamp(fwidth(a) * 0.5, 0.02, 0.5);
    return smoothstep(0.5 - w, 0.5 + w, a);
}
void vertex() {
    // La caja del texto en 2D (Y hacia abajo), como el degradado de la UI.
    v_caja = vec2(VERTEX.x / caja.x + 0.5, 0.5 - VERTEX.y / caja.y);
    v_rect = CUSTOM0;
    // El sitio en el cartel se suma ANTES de girar hacia la camara: el cartel
    // entero gira alrededor de su centro (ver gl_world_gui_build).
    VERTEX.xy += centro;
    if (billboard) MODELVIEW_MATRIX = VIEW_MATRIX * mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);
    if (fijo) {
        float sc = -MODELVIEW_MATRIX[3].z;
        MODELVIEW_MATRIX[0] *= sc; MODELVIEW_MATRIX[1] *= sc; MODELVIEW_MATRIX[2] *= sc;
    }
}
void fragment() {
    float dentro = step(v_rect.x, UV2.x) * step(UV2.x, v_rect.z) * step(v_rect.y, UV2.y) * step(UV2.y, v_rect.w);
    float cf = nitida(texture(tex_relleno, UV2).a) * dentro;
    float co = hay_contorno ? nitida(texture(tex_contorno, UV).a) : 0.0;
    vec3 col = relleno.rgb;
    float a_rel = relleno.a;
    if (usar_grad) {
        vec2 d = (v_caja - vec2(0.5) - grad_off) / max(grad_scale, 0.001);
        float t = clamp(0.5 + dot(d, grad_dir), 0.0, 1.0);
        vec4 g = texture(grad_tex, vec2(t, 0.5));
        col *= g.rgb;
        a_rel *= g.a;
    }
    // La transparencia de Roblox (no la cobertura) va con raiz: ver gl_wgui_alfa.
    a_rel = sqrt(clamp(a_rel, 0.0, 1.0));
    float a_con = sqrt(clamp(contorno.a, 0.0, 1.0));
    // El relleno encima del contorno, en una sola capa.
    float af = cf * a_rel;
    float ac = (1.0 - cf) * co * a_con;
    float a = af + ac;
    ALBEDO = (a > 0.0001 ? (col * af + contorno.rgb * ac) / a : col) * emision;
    ALPHA = a;
}
)";
inline Ref<Shader>& gl_wgui_shader_texto_ref(int i) {
    static Ref<Shader> sh[4];
    return sh[i];
}
inline Ref<Shader> gl_wgui_shader_texto(bool encima, bool doble) {
    Ref<Shader>& s = gl_wgui_shader_texto_ref((encima ? 1 : 0) + (doble ? 2 : 0));
    if (s.is_null()) {
        s.instantiate();
        String codigo = "shader_type spatial;\nrender_mode unshaded, shadows_disabled";
        codigo += doble ? ", cull_disabled" : ", cull_back";
        if (encima) codigo += ", depth_test_disabled";
        codigo += ";\n";
        codigo += String::utf8(GL_WGUI_SHADER_TEXTO);
        s->set_code(codigo);
    }
    return s;
}
// Lo llama uninitialize_luau_module (el nombre viene de cuando solo existia el
// shader del degradado): sin esto los shaders y las texturas de los degradados
// se liberan con la DLL, sin RenderingServer.
inline void gl_wgui_soltar_degradado() {
    for (int i = 0; i < 4; i++) gl_wgui_shader_texto_ref(i).unref();
    gl_wgui_degradados().clear();
}

// Las letras de un texto de cartel, a tamaño 64 (el de la cache de la fuente) y
// `ps` studs por pixel de letra: un cuadrado por letra, agrupados por el par de
// paginas de la cache que leen (contorno, relleno); cada grupo es una
// superficie. El texto ya trae sus saltos de linea. Cada linea se coloca en la
// caja (`caja`, studs, centrada en el origen; Y hacia arriba) con la alineacion
// de Roblox, y el bloque igual en vertical. `extension` sale con la mitad del
// tamaño de lo dibujado, para la caja de recorte del billboard.
struct GLWguiLote {
    RID contorno, relleno;
    PackedVector3Array v;
    PackedVector2Array uv, uv2;
    PackedFloat32Array rect;
    PackedInt32Array idx;
};
inline Ref<ArrayMesh> gl_wgui_malla_texto(const String& texto, const Ref<Font>& fuente, int contorno, float ps,
                                          float ancho_rel, const Vector2& caja, int alin_x, int alin_y,
                                          float interlineado, LocalVector<RID>& tex_contorno,
                                          LocalVector<RID>& tex_relleno, Vector2& extension) {
    tex_contorno.clear();
    tex_relleno.clear();
    extension = Vector2();
    Ref<ArrayMesh> malla;
    TextServer* ts = TextServerManager::get_singleton()->get_primary_interface().ptr();
    if (!ts || fuente.is_null() || texto.is_empty()) return malla;
    // En horizontal, la letra lleva el ancho de la fuente de Roblox (ver
    // gl_fuente_ancho_roblox): el mismo con el que se midio y se partio.
    const Vector2 esc(ps * MAX(ancho_rel, 0.1f), ps);
    const int TAM = 64;
    const RID forma = ts->create_shaped_text();
    ts->shaped_text_add_string(forma, texto, fuente->get_rids(), TAM, fuente->get_opentype_features(), "");
    const PackedInt32Array cortes = ts->shaped_text_get_line_breaks(forma, 1.0e6, 0,
        TextServer::BREAK_MANDATORY | TextServer::BREAK_TRIM_EDGE_SPACES);
    LocalVector<RID> lineas;
    for (int i = 0; i + 1 < cortes.size(); i += 2)
        lineas.push_back(ts->shaped_text_substr(forma, cortes[i], cortes[i + 1] - cortes[i]));
    static const StringName k_rep("repeat"), k_frid("font_rid"), k_fsz("font_size"), k_ind("index"),
                            k_off("offset"), k_adv("advance");
    // Todas las letras pintadas en la cache ANTES de pedir una textura: cada
    // letra nueva marca la pagina, y Godot la resube ENTERA (con sus mipmaps)
    // la primera vez que alguien pide su textura. Pedirlas una a una entre
    // letra y letra eran 55 ms del primer frame de Build a Mountain.
    for (uint32_t li = 0; li < lineas.size(); li++) {
        const TypedArray<Dictionary> glifos = ts->shaped_text_get_glyphs(lineas[li]);
        for (int g = 0; g < glifos.size(); g++) {
            const Dictionary d = glifos[g];
            const RID frid = d[k_frid];
            if (!frid.is_valid()) continue;
            const int64_t indice = d[k_ind];
            const int fsz = (int)(int64_t)d[k_fsz];
            ts->font_render_glyph(frid, Vector2i(fsz, 0), indice);
            if (contorno > 0) ts->font_render_glyph(frid, Vector2i(fsz, contorno), indice);
        }
    }
    // El bloque: cada linea mide su ascenso + descenso (x LineHeight).
    float alto = 0.0f;
    for (uint32_t i = 0; i < lineas.size(); i++)
        alto += (ts->shaped_text_get_ascent(lineas[i]) + ts->shaped_text_get_descent(lineas[i])) * ps * interlineado;
    float y = alin_y == 0 ? caja.y * 0.5f : (alin_y == 2 ? -caja.y * 0.5f + alto : alto * 0.5f);
    LocalVector<GLWguiLote> lotes;
    for (uint32_t li = 0; li < lineas.size(); li++) {
        const float asc = ts->shaped_text_get_ascent(lineas[li]) * ps;
        const float desc = ts->shaped_text_get_descent(lineas[li]) * ps;
        const float ancho = ts->shaped_text_get_width(lineas[li]) * esc.x;
        float x = alin_x == 0 ? -caja.x * 0.5f : (alin_x == 1 ? caja.x * 0.5f - ancho : -ancho * 0.5f);
        const float base = y - asc;
        const TypedArray<Dictionary> glifos = ts->shaped_text_get_glyphs(lineas[li]);
        for (int g = 0; g < glifos.size(); g++) {
            const Dictionary d = glifos[g];
            const RID frid = d[k_frid];
            const int64_t indice = d[k_ind];
            const int fsz = (int)(int64_t)d[k_fsz];
            const int repite = MAX(1, (int)(int64_t)d[k_rep]);
            const float avance = (float)(double)d[k_adv] * esc.x;
            const Vector2 desp = (Vector2)d[k_off];
            for (int r = 0; r < repite; r++, x += avance) {
                if (!frid.is_valid()) continue;
                const Vector2i k0(fsz, 0), k1(fsz, contorno);
                const RID tr = ts->font_get_glyph_texture_rid(frid, k0, indice);
                if (!tr.is_valid()) continue;
                const Vector2 of = (ts->font_get_glyph_offset(frid, k0, indice) + desp) * esc;
                const Vector2 sz = ts->font_get_glyph_size(frid, k0, indice) * esc;
                const Rect2 uvr = ts->font_get_glyph_uv_rect(frid, k0, indice);
                const Vector2 texs = ts->font_get_glyph_texture_size(frid, k0, indice);
                if (sz.x <= 0.0f || sz.y <= 0.0f || texs.x <= 0.0f || texs.y <= 0.0f) continue;
                // El relleno: su rect en el plano (Y arriba) y en su pagina.
                const float fx0 = x + of.x, fx1 = fx0 + sz.x, fy1 = base - of.y, fy0 = fy1 - sz.y;
                const float fu0 = uvr.position.x / texs.x, fu1 = (uvr.position.x + uvr.size.x) / texs.x;
                const float fv0 = uvr.position.y / texs.y, fv1 = (uvr.position.y + uvr.size.y) / texs.y;
                // El cuadrado: el de la letra con contorno, que es mas grande.
                float qx0 = fx0, qx1 = fx1, qy0 = fy0, qy1 = fy1;
                float ou0 = fu0, ou1 = fu1, ov0 = fv0, ov1 = fv1;
                RID tc;
                if (contorno > 0) {
                    tc = ts->font_get_glyph_texture_rid(frid, k1, indice);
                    const Vector2 oto = ts->font_get_glyph_texture_size(frid, k1, indice);
                    if (tc.is_valid() && oto.x > 0.0f && oto.y > 0.0f) {
                        const Vector2 oof = (ts->font_get_glyph_offset(frid, k1, indice) + desp) * esc;
                        const Vector2 osz = ts->font_get_glyph_size(frid, k1, indice) * esc;
                        const Rect2 our = ts->font_get_glyph_uv_rect(frid, k1, indice);
                        qx0 = x + oof.x; qx1 = qx0 + osz.x; qy1 = base - oof.y; qy0 = qy1 - osz.y;
                        ou0 = our.position.x / oto.x; ou1 = (our.position.x + our.size.x) / oto.x;
                        ov0 = our.position.y / oto.y; ov1 = (our.position.y + our.size.y) / oto.y;
                    } else {
                        tc = RID();
                    }
                }
                uint32_t p = 0;
                while (p < lotes.size() && !(lotes[p].contorno == tc && lotes[p].relleno == tr)) p++;
                if (p == lotes.size()) {
                    lotes.push_back(GLWguiLote());
                    lotes[p].contorno = tc;
                    lotes[p].relleno = tr;
                }
                GLWguiLote& lt = lotes[p];
                const int b0 = lt.v.size();
                // En el sentido de las agujas mirando desde +Z (la cara de
                // delante en Godot): arriba-izq, arriba-der, abajo-der, abajo-izq.
                const float xs[4] = { qx0, qx1, qx1, qx0 };
                const float ys[4] = { qy1, qy1, qy0, qy0 };
                const float us[4] = { ou0, ou1, ou1, ou0 };
                const float vs[4] = { ov0, ov0, ov1, ov1 };
                for (int k = 0; k < 4; k++) {
                    lt.v.push_back(Vector3(xs[k], ys[k], 0.0f));
                    lt.uv.push_back(Vector2(us[k], vs[k]));
                    // El relleno en el mismo cuadrado: su UV, prolongada fuera de su rect.
                    lt.uv2.push_back(Vector2(fu0 + (xs[k] - fx0) / (fx1 - fx0) * (fu1 - fu0),
                                             fv0 + (fy1 - ys[k]) / (fy1 - fy0) * (fv1 - fv0)));
                    lt.rect.push_back(fu0); lt.rect.push_back(fv0); lt.rect.push_back(fu1); lt.rect.push_back(fv1);
                    extension.x = MAX(extension.x, Math::abs(xs[k]));
                    extension.y = MAX(extension.y, Math::abs(ys[k]));
                }
                lt.idx.push_back(b0); lt.idx.push_back(b0 + 1); lt.idx.push_back(b0 + 2);
                lt.idx.push_back(b0); lt.idx.push_back(b0 + 2); lt.idx.push_back(b0 + 3);
            }
        }
        y -= (asc + desc) * interlineado;
    }
    for (uint32_t i = 0; i < lineas.size(); i++) ts->free_rid(lineas[i]);
    ts->free_rid(forma);
    if (lotes.is_empty()) return malla;
    malla.instantiate();
    const BitField<Mesh::ArrayFormat> formato((int64_t)Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT);
    for (uint32_t p = 0; p < lotes.size(); p++) {
        Array a;
        a.resize(Mesh::ARRAY_MAX);
        a[Mesh::ARRAY_VERTEX] = lotes[p].v;
        a[Mesh::ARRAY_TEX_UV] = lotes[p].uv;
        a[Mesh::ARRAY_TEX_UV2] = lotes[p].uv2;
        a[Mesh::ARRAY_CUSTOM0] = lotes[p].rect;
        a[Mesh::ARRAY_INDEX] = lotes[p].idx;
        malla->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, a, TypedArray<Array>(), Dictionary(), formato);
        tex_contorno.push_back(lotes[p].contorno);
        tex_relleno.push_back(lotes[p].relleno);
    }
    return malla;
}

// Monta (o pone al dia) la malla de un texto de cartel. La malla solo se rehace
// si cambia lo que coloca las letras (texto, fuente, tamaño, contorno, caja,
// alineacion o la variante del shader); color, degradado y emision son
// uniformes: cambiar un digito de un contador rehace solo su malla, y cambiar
// un color no rehace nada.
inline void gl_wgui_texto(MeshInstance3D* mi, const GLWorldGuiItem& it, bool billboard, bool encima,
                          bool fijo, float w, float h, float brillo, const Vector2& centro) {
    static const StringName k_firma("__gl_wgui_texto_firma"), k_ext("__gl_wgui_texto_ext"),
                            k_trel("tex_relleno"), k_tcon("tex_contorno");
    static const StringName k_rel("relleno"), k_con("contorno"), k_hay("hay_contorno"), k_usar("usar_grad"),
                            k_tex("grad_tex"), k_dir("grad_dir"), k_off("grad_off"), k_esc("grad_scale"),
                            k_caja("caja"), k_emi("emision"), k_bill("billboard"), k_fijo("fijo"),
                            k_centro("centro");
    // La em de la letra en studs; la malla va a 64 pixeles por em.
    const float em = CLAMP(it.text_frac * h, 0.002f, 400.0f);
    const float ps = em / 64.0f;
    // Contorno: el de Godot sale outline_size / 4 pixeles por fuera de la letra
    // (el stroker de FreeType, ver godotluau-ui-defaults-roblox), en el espacio
    // de la letra a 64.
    int contorno = 0;
    if (it.stroke.a > 0.004f && it.stroke_px > 0.0f && it.alto_px > 0.0f)
        contorno = CLAMP((int)Math::round(4.0f * it.stroke_px * 64.0f / it.alto_px), 1, 127);
    const Vector2 caja(MAX(0.001f, it.size_frac.x * w), MAX(0.001f, it.size_frac.y * h));
    const Ref<Shader> sh = gl_wgui_shader_texto(encima, billboard);
    const int64_t id_fuente = it.font.is_valid() ? (int64_t)it.font->get_instance_id() : 0;
    const String firma = it.text + String("|") + itos(id_fuente) + String("|") +
                         String::num(ps, 6) + String("|") + itos(contorno) + String("|") +
                         itos(it.alin_x * 3 + it.alin_y) + String("|") + String::num(caja.x, 4) + String("x") +
                         String::num(caja.y, 4) + String("|") + String::num(it.interlineado, 3) + String("|") +
                         String::num(it.ancho_rel, 3) + String("|") + itos((int64_t)sh->get_instance_id());
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!mi->has_meta(k_firma) || String(mi->get_meta(k_firma)) != firma || mi->get_mesh().is_null()) {
        LocalVector<RID> tc, tr;
        Vector2 ext;
        const Ref<ArrayMesh> malla = gl_wgui_malla_texto(it.text, it.font, contorno, ps, it.ancho_rel, caja, it.alin_x,
                                                         it.alin_y, it.interlineado, tc, tr, ext);
        // Los materiales de antes se reusan (un contador que cambia de digito
        // rehace la malla, no sus materiales): solo cambian las paginas.
        LocalVector<Ref<ShaderMaterial>> previos;
        const Ref<Mesh> vieja = mi->get_mesh();
        for (int p = 0; vieja.is_valid() && p < vieja->get_surface_count(); p++)
            previos.push_back(mi->get_surface_override_material(p));
        mi->set_mesh(malla);
        mi->set_meta(k_firma, firma);
        mi->set_meta(k_ext, ext);
        for (uint32_t p = 0; p < tr.size(); p++) {
            Ref<ShaderMaterial> m = p < previos.size() ? previos[p] : Ref<ShaderMaterial>();
            if (m.is_null() || m->get_shader() != sh) {
                m.instantiate();
                m->set_shader(sh);
            }
            mi->set_surface_override_material((int)p, m);
            // Las paginas van por su RID: son las de la cache de la fuente.
            rs->material_set_param(m->get_rid(), k_trel, tr[p]);
            rs->material_set_param(m->get_rid(), k_tcon, tc[p]);
            // Una letra sin pagina de contorno (la fuente no la genero) va en su
            // propia superficie y sin contorno: la textura por defecto es blanca y
            // pintaria el cuadrado entero del color del contorno.
            m->set_shader_parameter(k_hay, contorno > 0 && tc[p].is_valid());
        }
    }
    // La caja de recorte: el shader suma `centro` a la malla, asi que la de la
    // malla no sirve. En un billboard ademas la gira hacia la camara: tiene que
    // valer en cualquier giro (la plana del plano XY se salia del frustum de
    // canto y el texto parpadeaba en los bordes).
    const Vector2 ext = mi->has_meta(k_ext) ? (Vector2)mi->get_meta(k_ext) : Vector2();
    if (fijo || ext.x <= 0.0f) {
        mi->set_custom_aabb(AABB());
    } else if (billboard) {
        const float r = centro.length() + MAX(ext.x, ext.y);
        mi->set_custom_aabb(AABB(Vector3(-r, -r, -r), Vector3(r, r, r) * 2.0f));
    } else {
        mi->set_custom_aabb(AABB(Vector3(centro.x - ext.x, centro.y - ext.y, -0.01f),
                                 Vector3(ext.x * 2.0f, ext.y * 2.0f, 0.02f)));
    }
    const Ref<Mesh> malla = mi->get_mesh();
    const int superficies = malla.is_valid() ? malla->get_surface_count() : 0;
    const bool con_grad = it.grad_tex.is_valid();
    for (int p = 0; p < superficies; p++) {
        Ref<ShaderMaterial> m = mi->get_surface_override_material(p);
        if (m.is_null()) continue;
        m->set_shader_parameter(k_rel, it.color);
        m->set_shader_parameter(k_con, it.stroke);
        m->set_shader_parameter(k_usar, con_grad);
        if (con_grad) {
            m->set_shader_parameter(k_tex, it.grad_tex);
            m->set_shader_parameter(k_dir, it.grad_dir);
            m->set_shader_parameter(k_off, it.grad_off);
            m->set_shader_parameter(k_esc, it.grad_escala);
        }
        m->set_shader_parameter(k_caja, caja);
        m->set_shader_parameter(k_emi, GL_WGUI_EMISION * MAX(brillo, 0.0f));
        m->set_shader_parameter(k_bill, billboard);
        m->set_shader_parameter(k_fijo, fijo);
        m->set_shader_parameter(k_centro, centro);
    }
}

inline void gl_world_gui_build(Node3D* host, const LocalVector<GLWorldGuiItem>& items,
                               float w, float h, bool billboard, float max_dist,
                               float min_dist = 0.0f, bool siempre_encima = false,
                               bool tam_fijo = false, Vector2 desplazamiento = Vector2(),
                               float brillo = 1.0f) {
    // Distancias de Roblox -> rango de visibilidad de Godot.
    //
    // OJO CON EL CERO, que estaba al reves: Godot entiende
    // visibility_range_end == 0 como "sin limite", mientras que en Roblox 0 es
    // "no se ve nunca". Antes el codigo hacia `if (max_dist > 0.5f)` y por tanto
    // un MaxDistance de 0 (ocultar) acababa siendo visible SIEMPRE. El infinito
    // de Roblox es el que se traduce a 0, no al reves.
    // DistanceLowerLimit es el otro extremo: mas cerca de eso tampoco se dibuja.
    // Los dos se guardaban y no los leia nadie.
    // ...y el 0 REAL de Roblox ("no se ve nunca") no puede pasarse a Godot tal
    // cual (0 = sin limite): se traduce a un rango minusculo que en la practica
    // lo oculta siempre.
    float rango_fin = (max_dist >= GL_WGUI_SIN_LIMITE) ? 0.0f
                    : (max_dist <= 0.001f)             ? 0.001f
                                                       : max_dist;
    // Cuando el juego NO dijo nada (MaxDistance infinito, el valor de fabrica de
    // Roblox), se pone el tope de la calidad. Sin esto se dibuja TODO cartel del
    // mapa a cualquier distancia: medido, 1.730 Label3D visibles de golpe con
    // una mediana de 4.721 studs, casi todas las llamadas de dibujo del frame.
    //
    // Si el juego SI puso un MaxDistance, ese manda y no se toca — puede querer
    // una flecha de objetivo visible a 2.000 studs, y no somos quien para
    // recortarsela. Ver gl_wgui_dist_max() en gl_graphics.h.
    if (max_dist >= GL_WGUI_SIN_LIMITE) {
        const float tope = gl_wgui_dist_max();
        if (tope > 0.0f) rango_fin = tope;
    }
    const float rango_ini = MAX(min_dist, 0.0f);
    auto aplicar_rango = [&](GeometryInstance3D* g) {
        g->set_visibility_range_end(rango_fin);
        g->set_visibility_range_begin(rango_ini);
    };
    // (el borrado del reflejo anterior se decide mas abajo, tras calcular la
    //  firma de topologia: si no cambio, los nodos se REUTILIZAN)

    // ── ZINDEX: QUIEN VA DELANTE ─────────────────────────────────────────
    // Se recogia en el item y no lo leia NADIE: el orden era el del arbol, asi
    // que en cuanto los fondos entraron en el reflejo, un panel declarado
    // despues tapaba el texto de antes. Ordenacion por insercion, ESTABLE: a
    // igual ZIndex manda el orden del arbol, como en Roblox.
    LocalVector<uint32_t> orden;
    for (uint32_t i = 0; i < items.size(); i++) orden.push_back(i);
    for (uint32_t i = 1; i < orden.size(); i++) {
        const uint32_t v = orden[i];
        int j = (int)i - 1;
        while (j >= 0 && items[orden[j]].zindex > items[v].zindex) {
            orden[j + 1] = orden[j];
            j--;
        }
        orden[j + 1] = v;
    }

    // ── REUTILIZAR EN VEZ DE REHACER ─────────────────────────────────────
    // Escribir .Text en una etiqueta de un cartel llega hasta aqui, y antes
    // esto BORRABA y volvia a crear el reflejo ENTERO (hasta 14 nodos por
    // cartel). Un juego con 105 carteles actualizando su contador cada segundo
    // liberaba y creaba ~1.470 nodos 3D en un mismo frame: el tiron periodico y
    // cronometrable.
    //
    // Si la TOPOLOGIA no cambia (mismo numero de piezas y del mismo tipo, en el
    // mismo orden), se reusan los nodos y solo se reescriben texto, color,
    // posicion y tamaño. Cambiar un digito pasa de 28 operaciones de arbol a
    // rehacer UNA malla de letras.
    String firma;
    for (uint32_t k = 0; k < orden.size(); k++)
        firma += items[orden[k]].text.is_empty() ? "S" : "T";

    LocalVector<Node*> previos;
    for (int i = 0; i < host->get_child_count(true); i++) {
        Node* c = host->get_child(i, true);
        if (c && c->has_meta("__gl_world_gui")) previos.push_back(c);
    }
    bool reusar = (previos.size() == orden.size()) && !orden.is_empty() &&
                  host->has_meta("__gl_wgui_firma") &&
                  String(host->get_meta("__gl_wgui_firma")) == firma;
    if (reusar) {
        // Comprobacion defensiva: la firma dice el tipo, pero si algo dejo un
        // nodo de otra clase se rehace todo en vez de arriesgarse.
        for (uint32_t k = 0; k < orden.size() && reusar; k++) {
            const bool es_texto = !items[orden[k]].text.is_empty();
            if (es_texto  && !gl_cast<MeshInstance3D>(previos[k])) reusar = false;
            if (!es_texto && !gl_cast<Sprite3D>(previos[k]))       reusar = false;
        }
    }
    if (!reusar) {
        gl_world_gui_borrar_reflejo(host);
        previos.clear();
    }
    host->set_meta("__gl_wgui_firma", firma);

    for (uint32_t k = 0; k < orden.size(); k++) {
        const GLWorldGuiItem& it = items[orden[k]];
        // Roblox: X crece a la derecha, Y crece hacia ABAJO.
        // desplazamiento: el SizeOffset de un BillboardGui (fraccion del tamaño;
        // la Y de Roblox ahi crece hacia ARRIBA).
        // ── TODAS LAS PIEZAS EN EL CENTRO DEL CARTEL ─────────────────────
        // Cada pieza era un nodo en SU sitio del plano XY del cartel. En un
        // billboard cada una giraba hacia la camara alrededor de SU centro, asi
        // que mirando de lado sus X se volvian profundidad: el cartel se
        // amontonaba y la sombra de "LEADERBOARDS" (0,6 studs a la derecha de su
        // texto) quedaba mas cerca de la camara que el texto y se dibujaba
        // ENCIMA, con la letra al 59 % (medido: (137,118,55) contra
        // (233,214,93) en Roblox, camara 4). Ahora el nodo va en el centro del
        // cartel y el sitio de la pieza se suma en el plano que mira a la
        // camara (`centro` en el shader del texto, `offset` en el Sprite3D): el
        // cartel gira entero, y todas las capas tienen la misma profundidad,
        // asi que su orden lo decide solo sorting_offset (tambien en un
        // SurfaceGui mirado de canto).
        // El paso en Z separa las capas sin despegar el cartel de la pieza: con
        // el tope de items subido, 0.002 por capa acababa sobresaliendo varios
        // centimetros de la cara.
        const Vector2 sitio((it.center_frac.x - 0.5f + desplazamiento.x) * w,
                            (0.5f - it.center_frac.y + desplazamiento.y) * h);
        const Vector3 pos(0.0f, 0.0f, 0.0006f * (float)(k + 1));
        if (!it.text.is_empty()) {
            MeshInstance3D* mi = reusar ? gl_cast<MeshInstance3D>(previos[k]) : nullptr;
            const bool nuevo_nodo = (mi == nullptr);
            if (nuevo_nodo) {
                mi = memnew(MeshInstance3D);
                mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
            }
            mi->set_meta("__gl_world_gui", true);
            // Y ademas INTERNO: Clone() no debe copiarlo. El reflejo lo
            // rehace la clase sola; copiarlo dejaba nodos huerfanos que
            // seguian dibujando aunque el clon acabara en ReplicatedStorage.
            mi->set_meta(GL_META_INTERNO, true);
            mi->set_position(pos);
            // Orden de las capas al ordenar por profundidad: el paso en Z de pos
            // no basta en un billboard (gira hacia la camara y desde unos angulos
            // la capa de atras queda delante: la sombra oscura de "BLOCKS" tapaba
            // las letras amarillas: la sombra va desplazada y queda medio stud mas
            // cerca de la camara). sorting_offset adelanta cada capa medio stud mas
            // que la anterior, mas de lo que las separan entre si.
            // Y se ordena por la POSICION del nodo (la misma para todas las capas
            // del cartel), no por el centro de su caja: con la caja, en la placa
            // de la cumbre mirada de lado el trofeo de la derecha quedaba "mas
            // lejos" que el fondo oscuro del cartel y se dibujaba debajo de el (el
            // trofeo salia oscuro).
            mi->set_sorting_use_aabb_center(false);
            mi->set_sorting_offset(0.5f * (float)(k + 1));
            aplicar_rango(mi);
            gl_wgui_texto(mi, it, billboard, siempre_encima, tam_fijo, w, h, brillo, sitio);
            if (nuevo_nodo) host->add_child(mi, false, Node::INTERNAL_MODE_BACK);
        } else if (it.tex.is_valid()) {
            Sprite3D* s = reusar ? gl_cast<Sprite3D>(previos[k]) : nullptr;
            const bool nuevo_nodo = (s == nullptr);
            if (nuevo_nodo) s = memnew(Sprite3D);
            s->set_meta("__gl_world_gui", true);
            // Y ademas INTERNO: Clone() no debe copiarlo (ver el texto).
            s->set_meta(GL_META_INTERNO, true);
            s->set_texture(it.tex);
            // Emite a 0,655 como el texto (GL_WGUI_EMISION); Brightness la sube.
            s->set_modulate(gl_wgui_emision_srgb(it.color, brillo));
            // LA IMAGEN LLENA SU CAJA, que es el ScaleType.Stretch de Roblox (el
            // valor por defecto de un ImageLabel). Antes solo se ajustaba el
            // ANCHO y el alto salia de la proporcion de la textura: un icono
            // cuadrado dentro de una barra ancha se dibujaba cuadrado y se salia.
            // Se normaliza el tamaño natural a 1 stud y se escala a la caja; asi
            // el pixel_size no depende del tamaño de la textura y el pixel blanco
            // de 1x1 de los fondos no se sale de rango.
            const Vector2 tsz = it.tex->get_size();
            if (tsz.x > 0.5f && tsz.y > 0.5f) {
                const float ps = 1.0f / MAX(tsz.x, tsz.y);
                const Vector2 esc(MAX(it.size_frac.x * w, 0.001f) / (tsz.x * ps),
                                  MAX(it.size_frac.y * h, 0.001f) / (tsz.y * ps));
                s->set_pixel_size(ps);
                s->set_scale(Vector3(esc.x, esc.y, 1.0f));
                // El sitio en el cartel, en pixeles de la textura (el offset del
                // Sprite3D va antes de la escala y del giro del billboard).
                s->set_offset(Vector2(sitio.x / (ps * esc.x), sitio.y / (ps * esc.y)));
            }
            s->set_billboard_mode(billboard ? BaseMaterial3D::BILLBOARD_ENABLED
                                            : BaseMaterial3D::BILLBOARD_DISABLED);
            s->set_draw_flag(Sprite3D::FLAG_DOUBLE_SIDED, billboard);
            s->set_draw_flag(Sprite3D::FLAG_DISABLE_DEPTH_TEST, siempre_encima);
            s->set_draw_flag(Sprite3D::FLAG_FIXED_SIZE, tam_fijo);
            s->set_position(pos);
            s->set_sorting_use_aabb_center(false);          // ver el texto
            s->set_sorting_offset(0.5f * (float)(k + 1));
            aplicar_rango(s);
            if (nuevo_nodo) host->add_child(s, false, Node::INTERNAL_MODE_BACK);
        }
    }
}

#endif // GL_WORLD_GUI_H
