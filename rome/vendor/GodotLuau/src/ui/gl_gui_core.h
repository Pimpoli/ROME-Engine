#ifndef GL_GUI_CORE_H
#define GL_GUI_CORE_H

// ════════════════════════════════════════════════════════════════════
//  Semantica de layout de Roblox, en un solo sitio.
//
//  Lo usan TANTO las clases de GUI (roblox_gui.h) como los modificadores
//  de layout (UIListLayout / UIGridLayout / UIPadding en roblox_behavior.h),
//  que se compilan ANTES. Para no crear dependencias circulares, todo lo que
//  necesita hablar con otro nodo lo hace por NOMBRE de metodo/propiedad:
//
//    UIPadding          -> metodo  "_gl_pad_px"(Vector2 tam_padre) -> Vector4(l,t,r,b)
//    RobloxScrollingFrame -> metodo "_gl_scroll"() -> Vector2
//    cualquier GuiObject  -> propiedad "LayoutOrder" (int)
//
//  Reglas que se replican de Roblox y que antes no estaban:
//   · UDim2 de posicion/tamaño se mide contra el AREA DE CONTENIDO del padre,
//     que un UIPadding reduce por los 4 lados.
//   · SizeConstraint: RelativeXX/RelativeYY miden AMBOS ejes contra un solo
//     lado del padre (asi se hacen los cuadrados que escalan con el ancho).
//   · Rotation gira alrededor del CENTRO del elemento.
//   · SortOrder de los layouts: LayoutOrder (por defecto) o Name.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/control.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/canvas_item.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/object.hpp>   // ObjectDB (registro central de hover)
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector4.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include "gl_cola_ui.h"

using namespace godot;

struct GuiUDim2 { float xs = 0, xo = 0, ys = 0, yo = 0; };

// ── Puerta de dibujado (regla de Roblox) ─────────────────────────────
//  Un GuiObject se dibuja SOLO si cuelga de un ScreenGui (pantalla) o de un
//  SurfaceGui / BillboardGui (mundo 3D). Un Frame guardado en ReplicatedStorage
//  o en una carpeta suelta NO se ve en ninguna parte hasta que el juego lo
//  coloca donde toca.
//
//  Antes no habia esa regla y se dibujaba TODO: un place normal guarda docenas
//  de interfaces como plantilla (tiendas, popups, barras de arma, paneles de
//  mejora) y salian todas encima del HUD a la vez, sin que nada las hubiera
//  abierto. Se resuelve con visibility_layer + canvas_cull_mask del viewport,
//  asi que la propiedad Visible NO se toca y los scripts siguen leyendo la
//  verdad; solo deja de pintarse.
static const uint32_t GL_WORLD_GUI_CANVAS_LAYER = 1u << 19;   // "no en pantalla"

inline void gl_gui_cull_world_layer(Node* owner) {
    if (!owner) return;
    Viewport* vp = owner->get_viewport();
    if (!vp) return;
    const uint32_t mask = vp->get_canvas_cull_mask();
    if (mask & GL_WORLD_GUI_CANVAS_LAYER)
        vp->set_canvas_cull_mask(mask & ~GL_WORLD_GUI_CANVAS_LAYER);
}

inline void gl_gui_apply_render_gate(Control* c) {
    if (!c) return;
    if (Engine::get_singleton()->is_editor_hint()) return;   // en el editor se ve todo
    gl_gui_cull_world_layer(c);
    // La cadena tiene que ser ESTRICTA: padre GuiObject -> ... -> ScreenGui. Si un
    // eslabon no es GUI (un Script, un Folder, un Model...), Roblox NO dibuja
    // nada de ahi para abajo, aunque Visible sea true.
    //
    // Esto es lo que hacia que el HUD saliera con basura encima: el place guarda
    // la plantilla del boton de inventario DENTRO de su LocalScript
    // (CustomInventory/InventoryController/toolButton, un ImageButton con
    // Size=(1,1) y Visible=true). Al saltarme el eslabon del script, esa
    // plantilla se dibujaba a 648x648 -> el panel negro gigante, y su TextLabel
    // "1" con TextScaled a 222x186 -> el "1" enorme. En Roblox no se ve NADA de
    // eso porque su padre es un script.
    //
    // Se calcula heredando del padre: ENTER_TREE va de arriba a abajo, asi que
    // cuando llega el hijo el padre ya tiene su capa decidida.
    uint32_t layer = GL_WORLD_GUI_CANVAS_LAYER;              // por defecto: no se pinta
    Node* p = c->get_parent();
    if (p) {
        if (p->is_class("ScreenGui")) {
            layer = 1u;                                      // hijo directo de un ScreenGui
        } else if (p->is_class("SurfaceGui") || p->is_class("BillboardGui")) {
            layer = GL_WORLD_GUI_CANVAS_LAYER;               // UI de mundo
        } else if (Control* pc = gl_cast<Control>(p)) {
            layer = pc->get_visibility_layer();              // hereda la decision del padre
        }
        // Cualquier otro padre (Script, Folder, Model, servicio...) -> no se dibuja.
    }
    c->set_visibility_layer(layer);

    // …Y EL RATON TAMBIEN. Esta era la mitad que faltaba, y es la que rompia el
    // hover y el clic de todo el HUD.
    //
    // La puerta apagaba solo el DIBUJADO. Pero el reparto de input de la GUI de
    // Godot no mira visibility_layer: mira is_visible_in_tree() y mouse_filter.
    // Asi que lo que "no se pinta" seguia CAZANDO EL RATON.
    //
    // Medido en el place: 591 botones y cajas de texto invisibles pero clicables,
    // y 575 de ellos cuelgan de un SurfaceGui. Como un SurfaceGui es un Node3D y
    // no un Control, Godot los estira al tamano de la VENTANA y los registra como
    // controles raiz. O sea: la pantalla entera tapada por cientos de botones
    // fantasma a pantalla completa comiendose el mouse_entered/mouse_exited y el
    // pressed de la interfaz de verdad. En Roblox lo que no se dibuja tampoco
    // recibe raton, asi que esto ademas es lo 1:1.
    //
    // POR QUE MI SONDA DABA OK: solo creaba PlayerGui > ScreenGui > {boton,
    // frame}. Sin SurfaceGui, sin plantillas guardadas, sin Controls colgando de
    // un Script. Reproducia un mundo que no existe.
    if (layer == GL_WORLD_GUI_CANVAS_LAYER) {
        c->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    } else if (c->has_method("_gl_rewire_mouse")) {
        // Vuelve a la vida: una plantilla que el juego mueve DENTRO de un
        // ScreenGui tiene que recuperar el clic (ENTER_TREE corre otra vez al
        // reparentar). Cada clase sabe cual es SU filtro bueno — no se puede
        // usar uno comun: un boton nace Active=true y un Frame no.
        c->call("_gl_rewire_mouse");
    }
}

// Enum.SizeConstraint
enum GLSizeConstraint { GL_SC_RELATIVE_XY = 0, GL_SC_RELATIVE_XX = 1, GL_SC_RELATIVE_YY = 2 };
// Enum.AutomaticSize
enum GLAutomaticSize { GL_AUTO_NONE = 0, GL_AUTO_X = 1, GL_AUTO_Y = 2, GL_AUTO_XY = 3 };
// Enum.SortOrder
enum GLSortOrder { GL_SORT_NAME = 0, GL_SORT_CUSTOM = 1, GL_SORT_LAYOUT = 2 };

// Como mezcla Roblox los colores de un UIGradient: con GAMMA 2, la misma de sus
// colores de pieza. Medido en Studio sin perdida (08/10/2026): negro a blanco
// da 128, 180 y 220 a un cuarto, a la mitad y a tres cuartos (raiz de t); en
// sRGB serian 64/128/191 y con la curva sRGB lineal de Godot 137/188/225.
// Rojo a verde da (180,180,0) en la mitad. El alfa va lineal.
inline Color gl_mezcla_gamma2(const Color& a, const Color& b, float t) {
    return Color(Math::sqrt(Math::lerp(a.r * a.r, b.r * b.r, t)),
                 Math::sqrt(Math::lerp(a.g * a.g, b.g * b.g, t)),
                 Math::sqrt(Math::lerp(a.b * a.b, b.b * b.b, t)),
                 Math::lerp(a.a, b.a, t));
}

// ── Overrides de tema SOLO si cambian ───────────────────────────────
// add_theme_*_override de Godot no compara: cada llamada avisa THEME_CHANGED y
// un Label vuelve a dar forma a TODO su texto (y otra vez al medir su minimo).
// Un TextLabel ponia color, caja, fuente y tamano al entrar al arbol, casi
// siempre con el valor que ya tenia (un Clone los trae copiados): varias
// pasadas de forma por etiqueta para nada.
inline void gl_tema_tam(Control* c, const StringName& k, int v) {
    if (!c->has_theme_font_size_override(k) || c->get_theme_font_size(k) != v) c->add_theme_font_size_override(k, v);
}
inline void gl_tema_color(Control* c, const StringName& k, const Color& v) {
    if (!c->has_theme_color_override(k) || c->get_theme_color(k) != v) c->add_theme_color_override(k, v);
}
inline void gl_tema_caja(Control* c, const StringName& k, const Ref<StyleBox>& v) {
    if (!c->has_theme_stylebox_override(k) || c->get_theme_stylebox(k) != v) c->add_theme_stylebox_override(k, v);
}
inline void gl_tema_constante(Control* c, const StringName& k, int v) {
    if (!c->has_theme_constant_override(k) || c->get_theme_constant(k) != v) c->add_theme_constant_override(k, v);
}
// Varias cajas de una vez: solo las que cambian y, si son varias, en bloque
// (Godot avisa THEME_CHANGED una vez al final y no una por caja).
inline void gl_tema_cajas(Control* c, const StringName* claves, const Ref<StyleBox>* cajas, int n) {
    int cambian = 0;
    for (int i = 0; i < n; i++)
        if (!c->has_theme_stylebox_override(claves[i]) || c->get_theme_stylebox(claves[i]) != cajas[i]) cambian++;
    if (cambian == 0) return;
    if (cambian > 1) c->begin_bulk_theme_override();
    for (int i = 0; i < n; i++) gl_tema_caja(c, claves[i], cajas[i]);
    if (cambian > 1) c->end_bulk_theme_override();
}

// Los ScrollingFrame llevan esta marca desde el constructor: mirarla es una
// busqueda; has_method recorria la jerarquia de clases, y esto se pregunta en
// cada colocacion de cada GuiObject (gl_gui_scroll_of, gl_gui_contenido_px).
inline const StringName& gl_marca_scroll() {
    static const StringName k("__gl_sf");
    return k;
}

// ── Tamano contra el que se miden los hijos ─────────────────────────
// El del padre; en un ScrollingFrame, su LIENZO: en Roblox la escala de los
// hijos, de su UIPadding y de los layouts va contra AbsoluteCanvasSize, no
// contra la ventana (medido en Studio el 08/10).
inline Vector2 gl_gui_contenido_px(Control* parent) {
    if (!parent) return Vector2();
    static const StringName k_lienzo("_gl_lienzo_hijos");
    if (parent->has_meta(gl_marca_scroll())) return parent->call(k_lienzo);
    return parent->get_size();
}

// El contenido de un ScrollingFrame ha cambiado (un layout recoloco a sus
// hijos): con AutomaticCanvasSize su lienzo y su barra cambian con el.
inline void gl_gui_avisar_lienzo(Node* n) {
    static const StringName k_barra("_gl_pintar_barra");
    if (n && n->has_method(k_barra)) n->call(k_barra);
}

// ── Registro de modificadores por padre ─────────────────────────────
// El padding y el layout del padre se consultan en CADA colocacion de un
// GuiObject, y se buscaban recorriendo todos los hermanos con is_class (cinco
// por hermano): O(hermanos) por escritura. La pantalla de carga de Build a
// Mountain anima cientos de bloques hermanos, o sea O(N²) por frame. Ahora cada
// UIPadding y cada layout se apuntan en su padre al colgarse de el y se borran
// al soltarse: la consulta es una busqueda en una tabla.
enum GLModTipo { GL_MOD_PADDING = 0, GL_MOD_LAYOUT = 1 };
struct GLModsPadre { uint64_t id[2] = { 0, 0 }; };
inline HashMap<uint64_t, GLModsPadre>& gl_mods_registro() {
    static HashMap<uint64_t, GLModsPadre> m;
    return m;
}
inline int gl_mod_tipo(const Node* n) {
    if (!n) return -1;
    if (n->is_class("UIPadding")) return GL_MOD_PADDING;
    if (n->is_class("UIListLayout") || n->is_class("UIGridLayout") ||
        n->is_class("UIPageLayout") || n->is_class("UITableLayout")) return GL_MOD_LAYOUT;
    return -1;
}
// Lo llaman los propios modificadores en PARENTED (entra) y UNPARENTED (sale).
inline void gl_mods_apuntar(Node* mod, int tipo, bool entra) {
    Node* p = mod ? mod->get_parent() : nullptr;
    if (!p) return;
    HashMap<uint64_t, GLModsPadre>& r = gl_mods_registro();
    const uint64_t pid = (uint64_t)p->get_instance_id(), yo = (uint64_t)mod->get_instance_id();
    if (entra) {
        GLModsPadre& e = r[pid];
        if (!e.id[tipo]) e.id[tipo] = yo;   // con dos, manda el primero
        return;
    }
    GLModsPadre* e = r.getptr(pid);
    if (!e || e->id[tipo] != yo) return;
    e->id[tipo] = 0;
    for (int i = 0; i < p->get_child_count(); i++) {   // si queda otro, pasa a mandar ese
        Node* c = p->get_child(i);
        if (c != mod && gl_mod_tipo(c) == tipo) { e->id[tipo] = (uint64_t)c->get_instance_id(); break; }
    }
    if (!e->id[0] && !e->id[1]) r.erase(pid);
}
inline Node* gl_mods_de(const Node* parent, int tipo) {
    if (!parent) return nullptr;
    const GLModsPadre* e = gl_mods_registro().getptr((uint64_t)parent->get_instance_id());
    if (!e || !e->id[tipo]) return nullptr;
    Node* n = gl_cast<Node>(ObjectDB::get_instance(ObjectID(e->id[tipo])));
    return (n && n->get_parent() == parent) ? n : nullptr;
}

// ── Padding del padre (px) ───────────────────────────────────────────
// Roblox: un UIPadding hijo del padre reduce el area donde se colocan los
// hermanos (UIPadding se define despues: se llama por su metodo).
inline Vector4 gl_gui_padding_px(Control* parent) {
    if (!parent) return Vector4();
    Node* c = gl_mods_de(parent, GL_MOD_PADDING);
    if (!c) return Vector4();
    static const StringName k_pad("_gl_pad_px");
    return c->call(k_pad, gl_gui_contenido_px(parent));
}

// ── Desplazamiento de scroll del padre ──────────────────────────────
inline Vector2 gl_gui_scroll_of(Control* parent) {
    static const StringName k_scroll("_gl_scroll");
    if (parent && parent->has_meta(gl_marca_scroll())) return parent->call(k_scroll);
    return Vector2();
}

// ── Tamaño del area de contenido del padre ──────────────────────────
inline Vector2 gl_gui_parent_area(Control* ctrl, Vector4* out_pad = nullptr) {
    Node* padre_nodo = ctrl->get_parent();
    Control* parent = gl_cast<Control>(padre_nodo);
    Vector2 psz;
    if (parent) {
        psz = gl_gui_contenido_px(parent);
    } else if (padre_nodo && padre_nodo->has_method("_gl_lienzo_px")) {
        // Padre BillboardGui/SurfaceGui: es un Node3D, no un Control, asi que
        // aqui se cogia LA VENTANA. Y con la ventana todo lo que dependa de la
        // proporcion sale mal — que es como se construye un cartel en Roblox:
        // Frame a tamaño completo + UIAspectRatioConstraint de la proporcion
        // del cartel. Ver el comentario de BillboardGui::_gl_lienzo_px.
        psz = padre_nodo->call("_gl_lienzo_px");
    } else if (Viewport* vp = ctrl->get_viewport()) {
        psz = vp->get_visible_rect().size;
    }
    Vector4 pad = gl_gui_padding_px(parent);
    if (out_pad) *out_pad = pad;
    return Vector2(MAX(0.0f, psz.x - (float)pad.x - (float)pad.z),
                   MAX(0.0f, psz.y - (float)pad.y - (float)pad.w));
}

// Esquina inferior-derecha maxima que ocupan los hijos: la usa AutomaticSize y
// el AutomaticCanvasSize del ScrollingFrame.
inline Vector2 gl_gui_children_extent(Control* parent) {
    Vector2 m;
    if (!parent) return m;
    // En un ScrollingFrame los hijos estan corridos por el scroll: se mide en
    // el lienzo (si no, al bajar el lienzo automatico encogia y no llegaba al final).
    const Vector2 scr = gl_gui_scroll_of(parent);
    for (int i = 0; i < parent->get_child_count(); i++) {
        Control* c = gl_cast<Control>(parent->get_child(i));
        if (!c || !c->is_visible()) continue;
        if (c->has_meta("_gl_interno")) continue;
        const Vector2 e = c->get_position() + scr + c->get_size();
        m.x = MAX(m.x, e.x);
        m.y = MAX(m.y, e.y);
    }
    return m;
}

// Pide a todos los hijos Control que se vuelvan a colocar (lo usa UIPadding
// cuando cambia el inset: sus hermanos tienen que recalcular su rect).
inline void gl_gui_relayout_children(Control* parent) {
    if (!parent) return;
    for (int i = 0; i < parent->get_child_count(); i++) {
        Node* c = parent->get_child(i);
        if (c && c->has_method("_on_parent_resized")) c->call_deferred("_on_parent_resized");
    }
}

// ── Constraints que el propio GuiObject se aplica ────────────────────
//  UIAspectRatioConstraint y UISizeConstraint son HIJOS del elemento al que
//  limitan. Antes cada uno reescribia el tamaño de su padre en su _process, lo
//  que se peleaba con las anclas cada frame. Ahora se aplican aqui, justo
//  despues de colocar el elemento, ajustando los offsets y dejando fijo el
//  AnchorPoint (igual que Roblox).
// El tamaño que dejan las constraints propias (aspecto / min-max) a partir de
// `cur`; `cur` tal cual si no hay ninguna.
inline Vector2 gl_gui_constraint_tam(Control* ctrl, const Vector2& cur) {
    if (!ctrl) return cur;
    Node* arc = nullptr;
    Node* sc  = nullptr;
    for (int i = 0; i < ctrl->get_child_count(); i++) {
        Node* c = ctrl->get_child(i);
        if (!c) continue;
        if (!arc && c->is_class("UIAspectRatioConstraint")) arc = c;
        else if (!sc && c->is_class("UISizeConstraint"))    sc = c;
    }
    if (!arc && !sc) return cur;
    Vector2 want = cur;

    if (arc) {
        const double ratio = (double)arc->get(StringName("AspectRatio"));
        const int aspect_type = (int)(int64_t)arc->get(StringName("AspectType"));
        const int dominant    = (int)(int64_t)arc->get(StringName("DominantAxis"));
        if (ratio > 0.0001 && cur.x > 0.01f && cur.y > 0.01f) {
            if (aspect_type == 0) {
                // FitWithinMaxSize: encaja DENTRO del rect actual conservando la
                // proporcion. Es el modo por defecto y el que usa el HUD de los
                // juegos para que sus botones no se deformen. Antes solo se
                // aplicaba DominantAxis, asi que un boton de 382x35 con ratio 4
                // salia de 382x96 (gigante) en vez de 140x35.
                if ((double)cur.x / (double)cur.y > ratio) want = Vector2((float)(cur.y * ratio), cur.y);
                else                                       want = Vector2(cur.x, (float)(cur.x / ratio));
            } else {
                // ScaleWithParentSize: el eje DOMINANTE conserva el tamaño que
                // le da su propio Size (que ya escala con el padre por la parte
                // Scale del UDim2) y el OTRO eje se deriva de la proporcion.
                //
                // Antes el eje dominante se ponia al ANCHO COMPLETO del padre:
                // medido en la tienda del place, una etiqueta que declaraba
                // 136x45 salia de 334x110 (el ancho de la tarjeta, x2,46 en los
                // dos ejes) y todos los textos con este constraint reventaban su
                // tarjeta y se montaban entre si. En Roblox esa misma etiqueta
                // mide 136x45: "scale with parent" es escalar A TRAVES del
                // Size, no llenar el padre.
                if (dominant == 0) want = Vector2(cur.x, (float)(cur.x / ratio));
                else               want = Vector2((float)(cur.y * ratio), cur.y);
            }
        }
    }
    if (sc) {
        const Vector2 mn = sc->get(StringName("MinSize"));
        const Vector2 mx = sc->get(StringName("MaxSize"));
        want.x = Math::clamp(want.x, mn.x, mx.x);
        want.y = Math::clamp(want.y, mn.y, mx.y);
    }
    // Acotar SIEMPRE el resultado: un AspectRatio pequeno (0.01) sobre un rect
    // ancho daba tamanos astronomicos (cientos de miles de px). Godot intenta
    // reservar el canvas de ese elemento y el render se cae con una textura
    // vacia. Roblox tampoco dibuja nada mayor que la pantalla.
    if (!Math::is_finite(want.x) || !Math::is_finite(want.y)) return cur;
    want.x = Math::clamp(want.x, 0.0f, 8192.0f);
    want.y = Math::clamp(want.y, 0.0f, 8192.0f);
    if (Math::abs(want.x - cur.x) < 0.25f && Math::abs(want.y - cur.y) < 0.25f) return cur;
    return want;
}

// Lo mismo aplicado al rect ya puesto (para quien no pasa por gl_gui_apply_layout).
inline void gl_gui_apply_constraints(Control* ctrl, float ax, float ay,
                                     const Vector2& rect_calc = Vector2()) {
    if (!ctrl) return;
    const Vector2 cur = (rect_calc.x > 0.01f && rect_calc.y > 0.01f)
                            ? rect_calc : ctrl->get_size();
    const Vector2 want = gl_gui_constraint_tam(ctrl, cur);
    if (want == cur) return;

    // Se mantiene fijo el punto de ancla: L' = xa - ax*w
    const float L = ctrl->get_offset(SIDE_LEFT),  R = ctrl->get_offset(SIDE_RIGHT);
    const float T = ctrl->get_offset(SIDE_TOP),   B = ctrl->get_offset(SIDE_BOTTOM);
    const float dx = (want.x - cur.x), dy = (want.y - cur.y);
    ctrl->set_offset(SIDE_LEFT,   L - ax * dx);
    ctrl->set_offset(SIDE_RIGHT,  R + (1.0f - ax) * dx);
    ctrl->set_offset(SIDE_TOP,    T - ay * dy);
    ctrl->set_offset(SIDE_BOTTOM, B + (1.0f - ay) * dy);
    ctrl->set_pivot_offset(ctrl->get_size() * 0.5f);
}

// Tope de reentrada del layout: set_offset() emite "resized" en el acto y eso
// puede volver a entrar aqui por los manejadores. Se corta a 3 niveles; lo que
// falte se recalcula en el siguiente frame (las anclas de Godot ya mantienen el
// rect correcto por su cuenta mientras tanto).
inline int& gl_gui_layout_depth() { static int d = 0; return d; }

// Firma de las ENTRADAS de la ultima colocacion de cada elemento.
//
// Sin esto el layout se reaplicaba en cascada eternamente: la constraint de
// aspecto cambia el tamaño del elemento -> eso emite "resized" -> sus hijos se
// recolocan -> vuelven a partir del UDim2 (deshaciendo la constraint) -> y otra
// vez. Medido en el place real: 31.444 aplicaciones de constraint en una
// partida de 17 s, sin converger nunca, y el juego iba a tirones.
// Con la firma, un elemento solo se recoloca cuando algo de lo que depende
// cambia de verdad (area del padre, su UDim2, el padding o el scroll).
inline HashMap<uint64_t, uint64_t>& gl_gui_layout_sig() {
    static HashMap<uint64_t, uint64_t> m;
    return m;
}

// La firma resume "de que depende la colocacion", pero NO puede resumir las
// constraints hijas (UIAspectRatioConstraint / UISizeConstraint): son otros
// nodos y sus propiedades cambian por su cuenta. Sin esto, añadir un
// UIAspectRatioConstraint a un elemento ya colocado no hacia NADA — la firma
// seguia igual, se salia antes de aplicar constraints y el elemento se quedaba
// con su rect crudo. Medido: un Frame de 200x60 con AspectRatio 2 se quedaba
// en 200x60 (3,33) en vez de 120x60, incluso forzando el relayout a mano.
// Las constraints llaman aqui cuando cambian; asi la memoizacion sigue siendo
// barata y ademas es CORRECTA.
inline void gl_gui_olvidar_firma(Object* ctrl) {
    if (!ctrl) return;
    gl_gui_layout_sig().erase((uint64_t)ctrl->get_instance_id());
}

// ── Colocacion EXACTA de un UDim2 ────────────────────────────────────
//  Base: anclas + offsets NATIVOS de Godot, para que el motor resuelva la
//  cadena de padres el solo (no depende de que el padre ya tenga tamaño en el
//  instante de aplicarlo, que era la causa de que un Size=(1,1) saliera
//  100x50). Sobre esa base se suman correcciones en pixeles para lo que las
//  anclas NO pueden expresar: el padding del padre, SizeConstraint y el scroll.
//  Esas correcciones se recalculan cuando el padre cambia de tamaño
//  (_on_parent_resized ya esta conectado), asi que siguen siendo exactas.
// ── ¿Manda un layout sobre mi posicion? ──────────────────────────────────────
// En Roblox, si el PADRE tiene un UIListLayout / UIGridLayout / UIPageLayout /
// UITableLayout, la propiedad Position del hijo se IGNORA por completo: la
// posicion la decide el layout y punto. Aqui no se comprobaba, asi que cada
// hijo se colocaba con su UDim2 y ademas el layout lo recolocaba: los dos
// escribian sobre lo mismo cada frame y el resultado eran las UIs moviendose y
// montandose unas encima de otras.
inline bool gl_gui_padre_manda_layout(Control* ctrl) {
    Node* p = ctrl ? ctrl->get_parent() : nullptr;
    if (!p) return false;
    // OJO: el layout SALTA los hijos invisibles (gl_gui_sorted_children), asi
    // que si a uno oculto tambien le quitaramos su posicion se quedaria clavado
    // donde estuviera y al volver a mostrarse apareceria en el sitio de antes.
    // Un frame oculto se coloca solo con su UDim2; cuando se hace visible, la
    // huella cambia, el layout corre y lo recoloca.
    if (!ctrl->is_visible()) return false;
    return gl_mods_de(p, GL_MOD_LAYOUT) != nullptr;   // registro: sin recorrer hermanos
}

inline void gl_gui_apply_layout(Control* ctrl, const GuiUDim2& pos, const GuiUDim2& sz,
                                float ax = 0, float ay = 0,
                                int size_constraint = GL_SC_RELATIVE_XY,
                                float rotation_deg = 0.0f) {
    if (!ctrl) return;
    if (gl_gui_layout_depth() > 3) return;
    gl_gui_layout_depth()++;
    struct Guard { ~Guard() { gl_gui_layout_depth()--; } } _guard;

    Vector4 pad;
    const Vector2 area = gl_gui_parent_area(ctrl, &pad);
    const Vector2 scroll = gl_gui_scroll_of(gl_cast<Control>(ctrl->get_parent()));
    const bool manda_layout = gl_gui_padre_manda_layout(ctrl);

    {
        uint64_t h = 1469598103934665603ULL;
        h ^= manda_layout ? 0x9E3779B9ULL : 0x85EBCA6BULL;
        auto mix = [&h](double v) {
            h ^= (uint64_t)(int64_t)(v * 64.0);
            h *= 1099511628211ULL;
        };
        mix(area.x); mix(area.y);
        mix(pos.xs); mix(pos.xo); mix(pos.ys); mix(pos.yo);
        mix(sz.xs);  mix(sz.xo);  mix(sz.ys);  mix(sz.yo);
        mix(ax); mix(ay); mix(size_constraint); mix(rotation_deg);
        mix(pad.x); mix(pad.y); mix(pad.z); mix(pad.w);
        mix(scroll.x); mix(scroll.y);
        HashMap<uint64_t, uint64_t>& sig = gl_gui_layout_sig();
        const uint64_t id = (uint64_t)ctrl->get_instance_id();
        HashMap<uint64_t, uint64_t>::Iterator it = sig.find(id);
        if (it && it->value == h) return;                 // nada ha cambiado
        if (sig.size() > 200000) sig.clear();             // tope: nodos ya liberados
        sig[id] = h;
    }

    //  rect_izq  = posScale + (lado==lejano ? (1-a)*sizeScale : -a*sizeScale)
    float aL = pos.xs - ax * sz.xs;
    float aR = pos.xs + (1.0f - ax) * sz.xs;
    float aT = pos.ys - ay * sz.ys;
    float aB = pos.ys + (1.0f - ay) * sz.ys;
    float oL = pos.xo - ax * sz.xo;
    float oR = pos.xo + (1.0f - ax) * sz.xo;
    float oT = pos.yo - ay * sz.yo;
    float oB = pos.yo + (1.0f - ay) * sz.yo;

    // SizeConstraint: el tamaño de un eje se mide contra el OTRO lado del padre.
    // Se saca de las anclas y se mete en offsets con el tamaño real del padre.
    if (size_constraint == GL_SC_RELATIVE_XX) {
        aT = pos.ys;  aB = pos.ys;
        const float h = sz.ys * area.x;
        oT = pos.yo - ay * (sz.yo + h);
        oB = pos.yo + (1.0f - ay) * (sz.yo + h);
    } else if (size_constraint == GL_SC_RELATIVE_YY) {
        aL = pos.xs;  aR = pos.xs;
        const float w = sz.xs * area.y;
        oL = pos.xo - ax * (sz.xo + w);
        oR = pos.xo + (1.0f - ax) * (sz.xo + w);
    }

    // Padding del padre: x = pad_l + A*a + o  ==>  o' = o + pad_l*(1-a) - pad_r*a
    const float pl = (float)pad.x, pt = (float)pad.y, pr = (float)pad.z, pb = (float)pad.w;
    if (pl != 0.0f || pr != 0.0f) {
        oL += pl * (1.0f - aL) - pr * aL;
        oR += pl * (1.0f - aR) - pr * aR;
    }
    if (pt != 0.0f || pb != 0.0f) {
        oT += pt * (1.0f - aT) - pb * aT;
        oB += pt * (1.0f - aB) - pb * aB;
    }
    // Scroll del padre (ScrollingFrame): traslada todo el contenido.
    if (scroll != Vector2()) {
        oL -= scroll.x; oR -= scroll.x;
        oT -= scroll.y; oB -= scroll.y;
    }

    // ── EL RECT, EN PIXELES Y CON ANCLAS A CERO ─────────────────────────
    // Se resuelve aqui contra el CONTENIDO del padre (area + su padding: el
    // lienzo si es un ScrollingFrame, como en Roblox) y se pone con anclas a
    // cero: una o dos recolocaciones de Godot por escritura (set_position y
    // set_size) en vez de ocho (cuatro anclas y cuatro offsets, cada una con su
    // _size_changed) y otras cuatro si habia constraint. La pantalla de carga de
    // Build a Mountain anima cientos de bloques con Position en escala: cada
    // escritura costaba unos 20 µs. Si el padre cambia de tamaño,
    // _on_parent_resized vuelve a pasar por aqui (_gl_gui_wire_relayout).
    // Con anclas Godot ademas estiraba los hijos de un cartel (Control colgando
    // de un Node3D) contra la VENTANA y no contra el lienzo del cartel.
    const Vector2 lleno(area.x + (float)pad.x + (float)pad.z, area.y + (float)pad.y + (float)pad.w);
    float x0 = aL * lleno.x + oL, y0 = aT * lleno.y + oT;
    Vector2 tam((aR - aL) * lleno.x + (oR - oL), (aB - aT) * lleno.y + (oB - oT));
    // Constraints propias (aspecto / min-max) sobre el rect QUE ACABAMOS DE
    // CALCULAR (no el puesto), con el punto de ancla fijo. Comprobado por
    // Diag/layouts.gd: 14/14 casos con los numeros de Roblox.
    const Vector2 limitado = gl_gui_constraint_tam(ctrl, tam);
    if (limitado != tam) {
        x0 -= ax * (limitado.x - tam.x);
        y0 -= ay * (limitado.y - tam.y);
        tam = limitado;
    }
    tam = Vector2(MAX(0.0f, tam.x), MAX(0.0f, tam.y));
    if (ctrl->get_anchor(SIDE_LEFT) != 0.0f || ctrl->get_anchor(SIDE_TOP) != 0.0f ||
        ctrl->get_anchor(SIDE_RIGHT) != 0.0f || ctrl->get_anchor(SIDE_BOTTOM) != 0.0f) {
        ctrl->set_anchor(SIDE_LEFT, 0);  ctrl->set_anchor(SIDE_TOP, 0);
        ctrl->set_anchor(SIDE_RIGHT, 0); ctrl->set_anchor(SIDE_BOTTOM, 0);
    }
    ctrl->set_custom_minimum_size(Vector2(0, 0));
    // Con un layout en el padre la posicion es suya (gl_gui_padre_manda_layout):
    // aqui solo el tamaño.
    if (!manda_layout && ctrl->get_position() != Vector2(x0, y0)) ctrl->set_position(Vector2(x0, y0));
    if (ctrl->get_size() != tam) ctrl->set_size(tam);

    // Roblox gira alrededor del centro; el pivote hay que refrescarlo cada vez
    // que cambia el tamaño o la rotacion se descoloca.
    ctrl->set_pivot_offset(ctrl->get_size() * 0.5f);
    ctrl->set_rotation((float)Math::deg_to_rad((double)rotation_deg));
}

// ── LayoutOrder / orden de los layouts ──────────────────────────────
inline int gl_gui_layout_order(Node* n) {
    if (!n) return 0;
    static const StringName k_orden("LayoutOrder");
    Variant v = n->get(k_orden);
    if (v.get_type() == Variant::INT) return (int)(int64_t)v;
    if (n->has_meta("LayoutOrder")) return (int)(int64_t)n->get_meta("LayoutOrder");
    return 0;
}

// Hijos Control visibles del padre, en el orden que usaria Roblox.
// Enum.SortOrder: Name=0, Custom=1, LayoutOrder=2 (el de por defecto).
// Ordenacion ESTABLE: a igual clave se conserva el orden del arbol, igual que
// hace Roblox.
inline void gl_gui_sorted_children(Control* parent, int sort_order,
                                   LocalVector<Control*>& out) {
    out.clear();
    if (!parent) return;
    const int cnt = parent->get_child_count();
    for (int i = 0; i < cnt; i++) {
        Control* c = gl_cast<Control>(parent->get_child(i));
        if (c && c->is_visible()) out.push_back(c);
    }
    if (sort_order == GL_SORT_CUSTOM) return;   // orden del arbol tal cual

    const int n = (int)out.size();
    for (int i = 1; i < n; i++) {              // insercion: estable y n pequeño
        Control* key = out[i];
        int j = i - 1;
        while (j >= 0) {
            bool greater;
            if (sort_order == GL_SORT_NAME)
                greater = String(out[j]->get_name()) > String(key->get_name());
            else
                greater = gl_gui_layout_order(out[j]) > gl_gui_layout_order(key);
            if (!greater) break;
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
}

// ── Alineacion en el eje transversal ────────────────────────────────
// Enum.HorizontalAlignment: Center=0, Left=1, Right=2
// Enum.VerticalAlignment:   Center=0, Top=1,  Bottom=2
inline float gl_gui_cross(int align, float total, float item) {
    if (align == 0) return (total - item) * 0.5f;
    if (align == 2) return total - item;
    return 0.0f;
}

// ── Huella de estado, para no recolocar cada frame ──────────────────
// Recolocar dispara una cascada de notificaciones en Godot; con 1400 Frames
// eso solo se puede hacer cuando algo cambia de verdad.
inline uint64_t gl_gui_children_stamp(Control* parent) {
    if (!parent) return 0;
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ULL; };
    const Vector2 psz = gl_gui_contenido_px(parent);   // en un ScrollingFrame, el lienzo
    mix((uint64_t)(int)psz.x); mix((uint64_t)(int)psz.y);
    // El CANVAS del ScrollingFrame tambien es un cambio: sin esto, mover la
    // rueda cambiaba CanvasPosition pero la huella salia identica, el layout
    // no volvia a correr y la lista (la tienda) se quedaba clavada.
    const Vector2 scr = gl_gui_scroll_of(parent);
    mix((uint64_t)(int)(scr.x * 4.0f)); mix((uint64_t)(int)(scr.y * 4.0f));
    const int cnt = parent->get_child_count();
    mix((uint64_t)cnt);
    for (int i = 0; i < cnt; i++) {
        Control* c = gl_cast<Control>(parent->get_child(i));
        if (!c) continue;
        const Vector2 s = c->get_size();
        mix((uint64_t)(int)(s.x * 4.0f));
        mix((uint64_t)(int)(s.y * 4.0f));
        mix(c->is_visible() ? 3ULL : 7ULL);
        mix((uint64_t)(gl_gui_layout_order(c) + 4096));
    }
    return h;
}

// La huella recorre todos los hijos, y en Build a Mountain casi todas las listas
// estan en paneles cerrados (0,2 ms por frame entre todas). Con el padre oculto
// se mira uno de cada 8 frames; visible, cada frame. Lo que lee un script
// (AbsoluteContentSize) se recoloca al leerlo, asi que nunca ve un valor viejo.
inline bool gl_gui_layout_toca(Control* p, uint32_t& turno) {
    if (p->is_visible_in_tree()) return true;
    return (++turno & 7) == 0;
}
inline uint32_t gl_gui_layout_turno_nuevo() {
    static uint32_t n = 0;
    return n++;
}
struct GLGuiColocando {
    bool& b;
    explicit GLGuiColocando(bool& x) : b(x) { b = true; }
    ~GLGuiColocando() { b = false; }
};

// ── Hover CENTRAL (MouseEnter/MouseLeave sin que todo el mundo escuche) ──────
//  En Roblox, MouseEnter/MouseLeave saltan sobre cualquier GuiObject por
//  GEOMETRIA (este o no Active, lo tape o no otro elemento). El mouse_entered
//  de Godot no sirve para eso: exige un mouse_filter != IGNORE, y un filtro
//  PASS CONSUME el evento para su rama — el overlay decorativo de encima de un
//  boton se comia el clic (asi es como "la tienda no abria").
//
//  Modelo nuevo:
//   · Los GuiObject no-boton son MOUSE_FILTER_IGNORE: invisibles al raton.
//     (Active=true → STOP, que es el "sink input" de Roblox.)
//   · SOLO los elementos donde un script CONECTA MouseEnter/MouseLeave se
//     registran aqui, y un unico tick por frame les mira el rectangulo.
//     Cientos de botones sin conexiones = costo CERO, como pide Roblox.
struct GLHoverWatch { uint64_t id; bool dentro; };
inline LocalVector<GLHoverWatch>& gl_hover_lista() {
    static LocalVector<GLHoverWatch> l; return l;
}
// Indice de los ya vigilados. Antes el alta recorria TODA la lista para no
// duplicar, o sea O(n) por alta y O(n^2) por tanda. Un place que clona UI sin
// parar (cada brainrot trae su cartel con botones) llegaba a miles de
// entradas: medido en una partida real, gl_hover_tick se comio 170 ms en un
// solo frame. Con un conjunto, el alta es constante.
inline HashMap<uint64_t, bool>& gl_hover_indice() {
    static HashMap<uint64_t, bool> m; return m;
}

inline void gl_hover_registrar(Object* o) {
    if (!o) return;
    const uint64_t id = (uint64_t)o->get_instance_id();
    if (gl_hover_indice().has(id)) return;
    gl_hover_indice()[id] = true;
    gl_hover_lista().push_back({ id, false });
}
inline void gl_hover_tick() {
    LocalVector<GLHoverWatch>& l = gl_hover_lista();
    if (l.is_empty()) return;

    // Con el raton quieto solo hay entrada o salida si la UI se mueve bajo el:
    // basta mirarlo uno de cada 4 frames (20 ms a 200 FPS). La lista son cientos
    // de botones (AnimUi conecta MouseEnter en todos) y recorrerla entera eran
    // 0,1 ms por frame.
    static Vector2 raton_antes(-1.0e9f, -1.0e9f);
    static uint32_t quieto = 0;
    if (SceneTree* st = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop())) {
        if (Window* w = st->get_root()) {
            const Vector2 r = w->get_mouse_position();
            if (r != raton_antes) { raton_antes = r; quieto = 0; }
            else if ((++quieto & 3) != 0) return;
        }
    }

    // La posicion del raton se pide UNA vez, no una por elemento: get_viewport()
    // sube por la cadena de padres y cruza la frontera de la extension cada
    // vez. Con miles de controles vigilados eso solo ya era decenas de ms.
    Viewport* vp = nullptr;
    Vector2 raton;

    for (int i = (int)l.size() - 1; i >= 0; i--) {
        Object* o = ObjectDB::get_instance(ObjectID(l[i].id));
        Control* c = gl_cast<Control>(o);
        if (!c) {                                       // elemento destruido
            gl_hover_indice().erase(l[i].id);
            l.remove_at(i);
            continue;
        }
        // is_visible() es LOCAL (un bool); is_visible_in_tree() recorre a los
        // padres. Preguntando primero por el barato se descarta la mayoria de
        // la UI oculta sin pagar el recorrido.
        if (!c->is_visible() || !c->is_inside_tree() || !c->is_visible_in_tree()) {
            if (l[i].dentro) { l[i].dentro = false; c->call("_on_mouse_leave"); }
            continue;
        }
        if (!vp) {
            vp = c->get_viewport();
            if (!vp) continue;
            raton = vp->get_mouse_position();
        }
        const bool dentro = c->get_global_rect().has_point(raton);
        if (dentro != l[i].dentro) {
            l[i].dentro = dentro;
            c->call(dentro ? "_on_mouse_enter" : "_on_mouse_leave");
        }
    }
}

// ── AVISO DE "ESTO HA CAMBIADO" ──────────────────────────────────────
// Un SurfaceGui/BillboardGui no se dibuja: se REFLEJA en Label3D y Sprite3D que
// se construyen UNA vez. Si luego un script cambia el .Text de una etiqueta de
// dentro, el reflejo se queda con el texto viejo — la propiedad tiene el valor
// nuevo y la pantalla enseña el de antes.
//
// Medido en el place de prueba: el juego escribia "$ 1 M" en el cartel del
// tycoon y en pantalla seguia "$ 17.17 M", que es el valor de diseno. Sin un
// solo error: el dato estaba bien, lo que fallaba era el repintado. De ahi la
// sensacion de "los modules no funcionan" cuando en realidad si corrian.
//
// Esto sube por los padres hasta el primer cartel de mundo y le pide que se
// rehaga. Va DIFERIDO y con marca de sucio para que veinte cambios en el mismo
// frame cuesten UNA reconstruccion, no veinte.
inline void gl_world_gui_avisar_cambio(Node* control) {
    for (Node* p = control; p; p = p->get_parent()) {
        if (!p->has_method("_gl_rebuild_world")) continue;
        if (p->has_meta("_gl_wgui_sucio")) return;   // ya hay una en cola
        p->set_meta("_gl_wgui_sucio", true);
        p->call_deferred("_gl_rebuild_world");
        return;
    }
}

// Una UI de mundo solo se pinta si esta DENTRO del Workspace. En un place hay
// carteles guardados en ReplicatedStorage/ServerStorage como plantillas y no
// deben aparecer en ninguna parte hasta que el juego los coloque.



// Registro de los ScrollingFrame vivos, para poder preguntar rapido cual esta
// bajo el raton sin recorrer el arbol entero cada vez que gira la rueda.
inline LocalVector<uint64_t>& gl_scroll_lista() {
    static LocalVector<uint64_t> v;
    return v;
}
inline void gl_scroll_registrar(Object* o) {
    if (!o) return;
    const uint64_t id = (uint64_t)o->get_instance_id();
    LocalVector<uint64_t>& v = gl_scroll_lista();
    for (uint32_t i = 0; i < v.size(); i++) if (v[i] == id) return;
    v.push_back(id);
}
inline void gl_scroll_olvidar(Object* o) {
    if (!o) return;
    const uint64_t id = (uint64_t)o->get_instance_id();
    LocalVector<uint64_t>& v = gl_scroll_lista();
    for (uint32_t i = 0; i < v.size(); i++)
        if (v[i] == id) { v[i] = v[v.size() - 1]; v.resize(v.size() - 1); return; }
}

// ── QUIEN SE QUEDA LA RUEDA DEL RATON ────────────────────────────────
// En Roblox, con el cursor encima de un ScrollingFrame la rueda es SUYA y la
// camara no se mueve — aunque haya otras cosas por delante. Y si hay varios
// encajados, manda el de mas adentro.
//
// Se resuelve por GEOMETRIA a proposito, NO con gui_get_hovered_control() de
// Godot: ese devuelve el Control mas alto que HUNDE el input, que puede ser un
// boton de otra rama y taparia al ScrollingFrame de debajo — justo el caso que
// el usuario dice que no debe pasar.
//
// Devuelve el Control ganador (o null). El llamante decide que hacer.
// `vertical` = la rueda que se ha girado es la de arriba/abajo. Solo se
// devuelve un frame que PUEDA desplazarse en ese eje: si no, la rueda no es
// suya y tiene que seguir su camino hasta la camara (era el bug de "no puedo
// alejar la camara en casi ningun sitio").
inline Control* gl_scroll_bajo_raton(Viewport* vp, bool vertical = true) {
    if (!vp) return nullptr;
    const Vector2 raton = vp->get_mouse_position();
    Control* mejor = nullptr;
    int mejor_prof = -1;
    LocalVector<uint64_t>& lista = gl_scroll_lista();
    for (uint32_t i = 0; i < lista.size();) {
        Object* o = ObjectDB::get_instance(ObjectID(lista[i]));
        Control* c = gl_cast<Control>(o);
        if (!c || !c->is_inside_tree()) {           // muerto: fuera de la lista
            lista[i] = lista[lista.size() - 1];
            lista.resize(lista.size() - 1);
            continue;
        }
        i++;
        if (!c->is_visible_in_tree()) continue;
        if (!c->get_global_rect().has_point(raton)) continue;
        // Sin desbordamiento en ese eje, la rueda NO es suya.
        if (c->has_method("_gl_puede_rueda") && !(bool)c->call("_gl_puede_rueda", vertical)) continue;
        // Tiene que caer DENTRO de todos los ancestros que recortan: un frame
        // desplazado fuera de su padre recortado no se ve, y no debe ganar.
        bool tapado = false;
        for (Node* p = c->get_parent(); p; p = p->get_parent()) {
            Control* pc = gl_cast<Control>(p);
            if (!pc) break;
            if (pc->is_clipping_contents() && !pc->get_global_rect().has_point(raton)) { tapado = true; break; }
        }
        if (tapado) continue;
        // Gana el mas ANIDADO: es el "de encima" que pide Roblox.
        int prof = 0;
        for (Node* p = c->get_parent(); p; p = p->get_parent()) prof++;
        if (prof > mejor_prof) { mejor_prof = prof; mejor = c; }
    }
    return mejor;
}

// ────────────────────────────────────────────────────────────────────
//  Cache de StyleBox — un recurso por ASPECTO, no uno por elemento
// ────────────────────────────────────────────────────────────────────
// Cada GuiObject se fabricaba su propia caja al entrar en el arbol (y un boton
// TRES: normal, hover y pressed). En un place real eso son miles de recursos
// distintos con exactamente los mismos numeros dentro: casi toda la UI de un
// juego de Roblox es fondo transparente sin borde.
//
// Medido en el place de prueba: por cada 1.700 nodos creados, RobloxTextLabel
// costaba 259 ms y RobloxFrame 115 ms, contra 27 ms de RobloxPart. El tiron de
// la oleada NO son las piezas, es la UI — y la mayor parte de ese coste es
// crear el recurso y colgarlo del tema, no dibujarlo.
//
// Es la misma idea que ya usan las mallas y las formas de colision: misma
// combinacion de valores -> mismo recurso compartido.
struct GLEstiloClave {
    uint32_t bg, borde;
    int px;
    bool operator==(const GLEstiloClave& o) const {
        return bg == o.bg && borde == o.borde && px == o.px;
    }
};

struct GLEstiloHash {
    static uint32_t hash(const GLEstiloClave& k) {
        uint32_t h = k.bg * 2654435761u;
        h ^= k.borde + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= (uint32_t)k.px + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

inline HashMap<GLEstiloClave, Ref<StyleBoxFlat>, GLEstiloHash>& gl_estilos_cache() {
    static HashMap<GLEstiloClave, Ref<StyleBoxFlat>, GLEstiloHash> m;
    return m;
}

// El color de borde que trae StyleBoxFlat de fabrica. Se pregunta a Godot en
// vez de copiarlo a mano, que es como se cuelan las diferencias visuales al
// refactorizar. Un solo recurso para todo el programa.
inline Color gl_estilo_borde_defecto() {
    static Color c = []() {
        Ref<StyleBoxFlat> sb; sb.instantiate();
        return sb->get_border_color();
    }();
    return c;
}

// La caja compartida para un aspecto dado. Viene MARCADA: quien quiera
// modificarla (UICorner, UIStroke) tiene que sacarse antes una copia propia
// con gl_estilo_privado(), o le cambiaria el aspecto a media pantalla.
inline Ref<StyleBoxFlat> gl_estilo_compartido(const Color& fondo, const Color& borde, int borde_px) {
    GLEstiloClave k;
    k.bg    = fondo.to_rgba32();
    k.borde = borde.to_rgba32();
    k.px    = borde_px;
    HashMap<GLEstiloClave, Ref<StyleBoxFlat>, GLEstiloHash>& c = gl_estilos_cache();
    if (Ref<StyleBoxFlat>* f = c.getptr(k)) return *f;

    Ref<StyleBoxFlat> sb;
    sb.instantiate();
    sb->set_bg_color(fondo);
    sb->set_border_color(borde);
    sb->set_border_width_all(borde_px);
    sb->set_corner_radius_all(0);
    // Fondo transparente: sin relleno. StyleBoxFlat no se salta el alfa 0, asi
    // que cada contenedor transparente a pantalla completa (los hay a docenas)
    // era un quad mezclado que la GPU pintaba entero para nada, y dentro de un
    // CanvasGroup inflaba el rect que el grupo tiene que copiar.
    sb->set_draw_center(fondo.a > 0.0f);
    // En Roblox el borde no le quita sitio al texto. Con el margen por defecto
    // (= grosor del borde) el Label perdia 2 px de alto y, con TextScaled ajustado
    // a la caja entera, la linea ya no cabia y no se dibujaba (pestanas de la
    // tienda de Build a Mountain con BorderSizePixel 1 y fondo transparente).
    sb->set_content_margin_all(0);
    sb->set_meta("__gl_compartido", true);

    // Tope, por si un juego genera colores a lo bestia (barras de vida con el
    // color calculado cada frame, por ejemplo). Se sueltan solo las que ya no
    // usa nadie: si alguien la tiene puesta, se queda.
    if (c.size() >= 2048) {
        LocalVector<GLEstiloClave> sobran;
        for (const KeyValue<GLEstiloClave, Ref<StyleBoxFlat>>& kv : c)
            if (kv.value.is_valid() && kv.value->get_reference_count() <= 1)
                sobran.push_back(kv.key);
        for (uint32_t i = 0; i < sobran.size(); i++) c.erase(sobran[i]);
    }
    c[k] = sb;
    return sb;
}

// Grosor del borde que se pinta de verdad: Roblox no dibuja el BorderSizePixel
// de un GuiObject con UICorner (para eso esta el UIStroke). Con el borde de
// 1 px por defecto, sin esto toda caja redondeada creada por script salia con
// un contorno oscuro que en Roblox no tiene.
inline int gl_borde_px(const Control* c, int px) {
    if (px <= 0 || !c) return 0;
    for (int i = 0; i < c->get_child_count(); i++)
        if (c->get_child(i)->get_class() == "UICorner") return 0;
    return px;
}

// Radio en pixeles del UICorner hijo de un control (0 si no tiene), con la
// misma cuenta que UICorner::_apply: escala sobre el lado corto, tope en la
// pastilla. Lo usan los fondos que no van en el StyleBox del control.
inline float gl_radio_uicorner(const Control* c) {
    if (!c) return 0.0f;
    for (int i = 0; i < c->get_child_count(); i++) {
        Node* h = c->get_child(i);
        if (h->get_class() != "UICorner") continue;
        const Vector2 u = h->call("get_RadiusUDim");
        const Vector2 s = c->get_size();
        const float lado = MIN(s.x, s.y);
        const float r = u.x * lado + u.y;
        return lado > 0.5f ? CLAMP(r, 0.0f, lado * 0.5f) : MAX(r, 0.0f);
    }
    return 0.0f;
}

// Las cajas de estilo que puede llevar un GuiObject (Panel, Label, Button...),
// como StringName hechos una vez: construirlos desde el texto en cada llamada
// pasa por la tabla global de Godot, con su cerrojo.
inline const StringName* gl_claves_caja() {
    static const StringName k[5] = { StringName("panel"), StringName("normal"), StringName("hover"),
                                     StringName("pressed"), StringName("focus") };
    return k;
}

// Devuelve una caja que se puede MODIFICAR sin miedo. Si la que hay puesta es
// compartida, se clona y se vuelve a colgar del control; si ya era suya, se
// devuelve tal cual. Asi un UICorner solo redondea SU elemento.
inline Ref<StyleBoxFlat> gl_estilo_privado(Control* c, const StringName& clave) {
    if (!c) return Ref<StyleBoxFlat>();
    Ref<StyleBoxFlat> sb = c->get_theme_stylebox(clave);
    if (sb.is_null()) return sb;
    static const StringName k_compartido("__gl_compartido");
    if (!sb->has_meta(k_compartido)) return sb;
    // Una compartida no se toca nunca: su aspecto son los valores con los que la
    // hizo gl_estilo_compartido, y se rehace con ellos. duplicate() copiaba por
    // reflexion las ~30 propiedades de la caja (28 ms del primer frame de Build a
    // Mountain, uno por cada UICorner).
    Ref<StyleBoxFlat> propia;
    propia.instantiate();
    propia->set_bg_color(sb->get_bg_color());
    propia->set_border_color(sb->get_border_color());
    propia->set_border_width_all(sb->get_border_width(SIDE_LEFT));
    propia->set_draw_center(sb->is_draw_center_enabled());
    propia->set_content_margin_all(0);
    c->add_theme_stylebox_override(clave, propia);
    return propia;
}

#endif // GL_GUI_CORE_H
