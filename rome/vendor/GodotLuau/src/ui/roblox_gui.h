#ifndef ROBLOX_GUI_H
#define ROBLOX_GUI_H

// ════════════════════════════════════════════════════════════════════
//  Roblox-style GUI system for GodotLuau
//
//  ScreenGui    — Interface layer (CanvasLayer)
//  RobloxFrame  — Container/panel (Panel)
//  RobloxTextLabel   — Text (Label)
//  RobloxTextButton  — Text button (Button)
//  RobloxTextBox     — Input field (LineEdit)
//  RobloxImageLabel  — Image (TextureRect)
//  RobloxImageButton — Image button (Button + icon)
//  RobloxScrollingFrame — Scrollable container (Panel + clip + scroll)
//
//  NOTE: Position/size uses UDim2 (Scale + Offset):
//    Size     = UDim2.new(xScale, xOffset, yScale, yOffset)
//    Position = UDim2.new(xScale, xOffset, yScale, yOffset)
////
//  Sistema de GUI tipo Roblox para GodotLuau
//
//  Cada clase expone el juego COMPLETO de propiedades de su equivalente en
//  Roblox (LayoutOrder, ZIndex, SizeConstraint, AutomaticSize, ScaleType,
//  RichText, CanvasSize…). Se exponen con el nombre PascalCase de Roblox, asi
//  que el puente Lua (_gl_bridge) las lee y escribe igual que en Studio, el
//  importador de .rbxl las aplica directamente y salen en el inspector de Godot.
//
//  La colocacion (UDim2 -> rect) y las constraints viven en gl_gui_core.h,
//  porque los modificadores (UIListLayout, UIPadding…) usan la misma
//  semantica y se compilan antes.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/canvas_layer.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/text_edit.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_pan_gesture.hpp>
#include <godot_cpp/classes/rendering_server.hpp>   // ZIndex: indice de dibujo
#include "gl_asset.h"
#include "gl_gui_core.h"
#include "gl_fuentes.h"   // fuentes de Roblox con equivalentes libres
#include "gl_cola_ui.h"   // cola con presupuesto: ver el frame B de la oleada
#include <godot_cpp/classes/texture_button.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/style_box_empty.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "../core/gl_asset_fetch.h"
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <vector>

#include "lua.h"
#include "lualib.h"
#include "gl_errors.h"
#include "gl_runtime.h"

using namespace godot;

// Re-aplica los modificadores hijos (UICorner/UIStroke/UIGradient…) tras
// reconstruir el StyleBox del control: como _apply_style crea una caja nueva,
// el redondeo/borde se perdía en cuanto el fondo cambiaba (1.15).
inline void _gl_gui_reapply_modifiers(Control* c) {
    if (!c) return;
    for (int i = 0; i < c->get_child_count(); i++) {
        Node* ch = c->get_child(i);
        // A LA COLA CON PRESUPUESTO, no a call_deferred. Este place tiene 4.843
        // modificadores (UIStroke, UIGradient, UICorner) y una oleada clona un
        // monton de golpe: con call_deferred, Godot ejecutaba TODOS sus _apply
        // en el vaciado del frame siguiente, que medido son 99-117 ms.
        if (ch && ch->has_method("_apply")) ch->call_deferred("_apply");
    }
}

// Avisa al layout del padre (UIListLayout/UIGridLayout) de que hay que
// recolocar: es lo que hace que cambiar LayoutOrder desde un script reordene
// la lista, igual que en Roblox.
inline void _gl_gui_bump_parent_layout(Node* self) {
    if (!self) return;
    Node* p = self->get_parent();
    if (!p) return;
    for (int i = 0; i < p->get_child_count(); i++) {
        Node* c = p->get_child(i);
        if (!c) continue;
        if (c->has_method("_gl_invalidate")) c->call("_gl_invalidate");
    }
}

// Reconecta el re-layout: al padre si es Control, o al VIEWPORT si es top-level
// (padre CanvasLayer/ScreenGui). Sin esto una GUI top-level nunca se reajustaba
// al cambiar el tamaño de pantalla — el clásico "todo amontonado arriba-izquierda".
inline void _gl_gui_wire_relayout(Control* self, const char* method) {
    if (!self) return;
    // CONNECT_DEFERRED es imprescindible: set_offset() emite "resized" DENTRO
    // del propio layout, y con conexion directa cada ajuste reentraba en el
    // layout de los hijos. Con 253 constraints anidadas el juego se quedaba
    // clavado. Diferido, cada nivel se recoloca en el siguiente idle y
    // converge en un par de frames.
    const uint32_t DEFER = Object::CONNECT_DEFERRED;
    Control* parent_ctrl = gl_cast<Control>(self->get_parent());
    if (parent_ctrl) {
        if (!parent_ctrl->is_connected("resized", Callable(self, method)))
            parent_ctrl->connect("resized", Callable(self, method), DEFER);
    } else if (Viewport* vp = self->get_viewport()) {
        if (!vp->is_connected("size_changed", Callable(self, method)))
            vp->connect("size_changed", Callable(self, method), DEFER);
    }
    // El propio elemento tambien se re-mide cuando cambia de tamaño: es lo que
    // mantiene el pivote de Rotation y el ajuste de TextScaled al dia.
    if (!self->is_connected("resized", Callable(self, "_gl_on_self_resized")))
        self->connect("resized", Callable(self, "_gl_on_self_resized"), DEFER);
    // El primer recolocado tambien va a la cola: es UNO POR CONTROL creado, y
    // una oleada crea cientos. Mide el texto con get_string_size, asi que no es
    // barato. Los nodos que acaban de nacer pueden esperar unos frames.
    self->call_deferred(method);
}

// Quita las etiquetas de RichText de Roblox (<b>, <font color="#fff">…). Godot
// las pintaria literalmente en un Label; en Roblox con RichText=true forman
// parte del formato, no del texto.
inline String _gl_strip_rich_tags(const String& p_s) {
    if (p_s.find("<") < 0) return p_s;
    // <br/> es un SALTO DE LINEA, no una etiqueta de formato: borrarlo con las
    // demas pegaba lineas que en Roblox van separadas.
    String s = p_s.replace("<br/>", "\n").replace("<br />", "\n").replace("<br>", "\n");
    String out;
    bool in_tag = false;
    for (int i = 0; i < s.length(); i++) {
        const char32_t c = s[i];
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) out += String::chr(c);
    }
    out = out.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"")
             .replace("&apos;", "'").replace("&amp;", "&");
    return out;
}
#include "gl_richtext.h"   // RichText de verdad: la capa RichTextLabel encima
#include "gl_nueve.h"      // ScaleType.Slice (9-slice) de ImageLabel / ImageButton

// ────────────────────────────────────────────────────────────────────
//  Propiedades de GuiObject de Roblox que NO son simples setters de la clase.
//  Un solo sitio para el importador y para el repintado desde metadatos.
////  Roblox GuiObject properties that need translation (not plain setters).
// Las que traduce gl_apply_rbx_gui_extra y que las clases de texto (y las de
// todo GuiObject: recorte, giro) tienen tambien como propiedad con su campo.
inline bool gl_rbx_es_extra_gui(const String& prop) {
    static const char* k[] = { "ClipsDescendants", "Rotation", "TextXAlignment", "TextYAlignment",
        "TextWrapped", "TextTransparency", "TextStrokeTransparency", "TextStrokeColor3",
        "TextTruncate", "RichText", "LineHeight", "MaxVisibleGraphemes" };
    for (const char* s : k)
        if (prop == s) return true;
    return false;
}

inline bool gl_apply_rbx_gui_extra(Node* n, const String& prop, const Variant& v) {
    Control* c = gl_cast<Control>(n);
    if (!c) return false;

    // ── Cualquier GuiObject ──────────────────────────────────────────
    // Por su setter: solo con set_clip_contents, leer ClipsDescendants de algo
    // importado daba false aunque recortara.
    if (prop == "ClipsDescendants") {
        if (c->has_method("set_ClipsDescendants")) c->call("set_ClipsDescendants", (bool)v);
        else c->set_clip_contents((bool)v);
        return true;
    }
    if (prop == "Visible")          { c->set_visible((bool)v); return true; }
    if (prop == "Rotation") {
        if (c->has_method("set_rbx_rotation")) { c->call("set_rbx_rotation", (float)v); return true; }
        c->set_pivot_offset(c->get_size() * 0.5f);
        c->set_rotation((float)Math::deg_to_rad((double)v));
        return true;
    }

    // ── Texto (Label y Button) ───────────────────────────────────────
    // Enum.TextXAlignment de Roblox: Left=0, Right=1, Center=2 (¡Right antes que
    // Center!). Confundir ese orden descoloca todos los textos del juego.
    if (prop == "TextXAlignment") {
        const int rb = (int)(int64_t)v;
        HorizontalAlignment h = (rb == 1) ? HORIZONTAL_ALIGNMENT_RIGHT
                             : (rb == 2) ? HORIZONTAL_ALIGNMENT_CENTER
                                         : HORIZONTAL_ALIGNMENT_LEFT;
        if (Label* l = gl_cast<Label>(c))   { l->set_horizontal_alignment(h); return true; }
        if (Button* b = gl_cast<Button>(c)) { b->set_text_alignment(h); return true; }
        if (LineEdit* e = gl_cast<LineEdit>(c)) { e->set_horizontal_alignment(h); return true; }
        return false;
    }
    // Enum.TextYAlignment: Top=0, Center=1, Bottom=2
    if (prop == "TextYAlignment") {
        const int rb = (int)(int64_t)v;
        VerticalAlignment va = (rb == 1) ? VERTICAL_ALIGNMENT_CENTER
                            : (rb == 2) ? VERTICAL_ALIGNMENT_BOTTOM
                                        : VERTICAL_ALIGNMENT_TOP;
        if (Label* l = gl_cast<Label>(c)) { l->set_vertical_alignment(va); return true; }
        return false;
    }
    if (prop == "TextWrapped") {
        if (Label* l = gl_cast<Label>(c)) {
            l->set_autowrap_mode((bool)v ? TextServer::AUTOWRAP_WORD_SMART : TextServer::AUTOWRAP_OFF);
            return true;
        }
        return false;
    }
    // Enum.TextTruncate: None=0, AtEnd=1, SplitWord=2
    if (prop == "TextTruncate") {
        if (Label* l = gl_cast<Label>(c)) {
            const int t = (int)(int64_t)v;
            l->set_text_overrun_behavior(t == 0 ? TextServer::OVERRUN_NO_TRIMMING
                                                : TextServer::OVERRUN_TRIM_ELLIPSIS);
            return true;
        }
        return false;
    }
    if (prop == "RichText") {
        if (c->has_method("set_rich_text")) { c->call("set_rich_text", (bool)v); return true; }
        return false;
    }
    if (prop == "LineHeight") {
        Ref<Font> f = c->get_theme_font("font");
        const int base = c->get_theme_font_size("font_size");
        c->add_theme_constant_override("line_spacing", (int)(((float)v - 1.0f) * (float)MAX(1, base)));
        return true;
    }
    if (prop == "MaxVisibleGraphemes") {
        if (Label* l = gl_cast<Label>(c)) {
            const int g = (int)(int64_t)v;
            c->set_meta("_gl_max_graphemes", g < 0 ? -1 : g);
            // Con RichText el que pinta es la capa: el Label se queda en 0.
            if (c->has_meta("_gl_rich_activo")) {
                if (RichTextLabel* capa = gl_cast<RichTextLabel>(c->get_node_or_null(NodePath("__gl_rich__"))))
                    capa->set_visible_characters(g < 0 ? -1 : g);
                return true;
            }
            l->set_visible_characters(g < 0 ? -1 : g);
            return true;
        }
        return false;
    }
    // TextTransparency: 0 = opaco, 1 = invisible (al reves que el alfa de Godot)
    if (prop == "TextTransparency") {
        const float a = 1.0f - Math::clamp((float)v, 0.0f, 1.0f);
        Color col = c->get_theme_color("font_color");
        col.a = a;
        c->add_theme_color_override("font_color", col);
        return true;
    }
    // Contorno del texto: en Roblox se usa muchisimo para que el texto se lea
    // sobre cualquier fondo. TextStrokeTransparency 0 = contorno bien visible.
    // La transparencia del contorno es ALFA con grosor FIJO (en Roblox el stroke
    // del texto es de ~1 px siempre). Antes se traducia a TAMANO (2..8 px, y
    // siempre opaco): un stroke semitransparente salia como un halo negro gordo,
    // y el alfa no llegaba nunca al color.
    if (prop == "TextStrokeTransparency") {
        const float t = Math::clamp((float)v, 0.0f, 1.0f);
        c->set_meta("_gl_stroke_t", t);
        c->add_theme_constant_override("outline_size", t < 0.98f ? 4 : 0);   // 1 px por fuera (ver UIStroke)
        Color col = c->has_meta("_gl_stroke_col") ? (Color)c->get_meta("_gl_stroke_col") : Color(0, 0, 0);
        col.a = 1.0f - t;
        c->add_theme_color_override("font_outline_color", col);
        return true;
    }
    if (prop == "TextStrokeColor3") {
        if (v.get_type() != Variant::COLOR) return false;
        Color col = v;
        c->set_meta("_gl_stroke_col", col);
        const float t = c->has_meta("_gl_stroke_t") ? (float)c->get_meta("_gl_stroke_t") : 1.0f;
        col.a = 1.0f - t;
        c->add_theme_color_override("font_outline_color", col);
        return true;
    }
    return false;
}

// ────────────────────────────────────────────────────────────────────
//  Aplica las props de GUI de Roblox que el importador dejo como METADATA.
//  NO se borran las metas: así sobreviven a duplicate() y el ScreenGui clonado
//  a PlayerGui (o un reparent) las vuelve a aplicar. Reaplicar es idempotente.
////  Apply Roblox GUI props the importer left as METADATA.
inline void gl_apply_rbx_gui_meta(Node* n) {
    if (!n) return;
    auto take = [&](const char* k) -> Variant { return n->has_meta(k) ? n->get_meta(k) : Variant(); };
    if (n->has_meta("Position")) { Variant v = take("Position");
        if (v.get_type() == Variant::VECTOR4 && n->has_method("set_udim2_pos")) {
            Vector4 u = v; n->call("set_udim2_pos", u.x, u.y, u.z, u.w); } }
    if (n->has_meta("Size")) { Variant v = take("Size");
        if (v.get_type() == Variant::VECTOR4 && n->has_method("set_udim2_size")) {
            Vector4 u = v; n->call("set_udim2_size", u.x, u.y, u.z, u.w); } }
    if (n->has_meta("AnchorPoint")) { Variant v = take("AnchorPoint");
        if (v.get_type() == Variant::VECTOR2 && n->has_method("set_anchor_point")) {
            Vector2 a = v; n->call("set_anchor_point", a.x, a.y); } }
    if (n->has_meta("BackgroundColor3")) { Variant v = take("BackgroundColor3");
        if (v.get_type() == Variant::COLOR && n->has_method("set_bg_color")) {
            Color c = v; n->call("set_bg_color", c.r, c.g, c.b); } }
    if (n->has_meta("BackgroundTransparency")) { Variant v = take("BackgroundTransparency");
        if (n->has_method("set_bg_alpha")) n->call("set_bg_alpha", (float)v); }
    if (n->has_meta("BorderColor3")) { Variant v = take("BorderColor3");
        if (v.get_type() == Variant::COLOR && n->has_method("set_border_color")) {
            Color c = v; n->call("set_border_color", c.r, c.g, c.b); } }
    if (n->has_meta("BorderSizePixel")) { Variant v = take("BorderSizePixel");
        if (n->has_method("set_border_px")) n->call("set_border_px", (int)(int64_t)v); }
    if (n->has_meta("Text")) { Variant v = take("Text");
        if (n->has_method("set_text")) n->call("set_text", String(v)); }
    if (n->has_meta("TextColor3")) { Variant v = take("TextColor3");
        if (v.get_type() == Variant::COLOR && n->has_method("set_text_color")) {
            Color c = v; n->call("set_text_color", c.r, c.g, c.b); } }
    if (n->has_meta("TextSize")) { Variant v = take("TextSize");
        if (n->has_method("set_text_size")) n->call("set_text_size", (int)(float)v); }
    if (n->has_meta("TextScaled")) { Variant v = take("TextScaled");
        if (n->has_method("set_text_scaled")) n->call("set_text_scaled", (bool)v); }
    if (n->has_meta("Image")) { Variant v = take("Image");
        if (n->has_method("set_image")) n->call("set_image", String(v)); }
    if (n->has_meta("ImageColor3")) { Variant v = take("ImageColor3");
        if (v.get_type() == Variant::COLOR && n->has_method("set_image_color")) {
            Color c = v; n->call("set_image_color", c.r, c.g, c.b); } }
    if (n->has_meta("ImageTransparency")) { Variant v = take("ImageTransparency");
        if (n->has_method("set_image_transparency")) n->call("set_image_transparency", (float)v); }

    // Props con setter PascalCase propio (LayoutOrder, ZIndex, ScaleType…):
    // se aplican por nombre para no repetir 30 ifs.
    static const char* kDirect[] = {
        "LayoutOrder", "ZIndex", "SizeConstraint", "AutomaticSize", "BorderMode",
        "Active", "Interactable", "Selectable", "Draggable", "Style", "FontFace",
        "ScaleType", "SliceScale", "SliceCenter", "ResampleMode",
        "ImageRectOffset", "ImageRectSize", "HoverImage", "PressedImage",
        "AutoButtonColor", "ScrollBarThickness", "ScrollingDirection",
        "ScrollingEnabled", "AutomaticCanvasSize", "ElasticBehavior",
        "CanvasPosition", "ScrollBarImageColor3", "ScrollBarImageTransparency",
        "VerticalScrollBarInset", "HorizontalScrollBarInset",
        "VerticalScrollBarPosition", "TopImage", "MidImage", "BottomImage",
        "PlaceholderText", "PlaceholderColor3", "ClearTextOnFocus", "MultiLine",
        "TextEditable"
    };
    // Los nombres se internan UNA vez, no en cada llamada. `has_meta(const char*)`
    // construye un StringName por llamada, y construir un StringName toma el
    // mutex global de internado: eran 39 + 12 = 51 por nodo de GUI, mas un
    // String("set_") + nombre por cada acierto. Un HUD de 1.400 nodos son ~71.000
    // internados de golpe, y se repite ENTERO en cada respawn (ResetOnSpawn).
    struct GLPropDirecta { StringName prop; StringName setter; };
    static LocalVector<GLPropDirecta> directas;
    if (directas.is_empty())
        for (int i = 0; i < 39; i++)
            directas.push_back({ StringName(kDirect[i]),
                                 StringName(String("set_") + kDirect[i]) });
    for (uint32_t i = 0; i < directas.size(); i++) {
        if (!n->has_meta(directas[i].prop)) continue;
        if (n->has_method(directas[i].setter))
            n->call(directas[i].setter, n->get_meta(directas[i].prop));
    }

    // Props que necesitan traduccion (alineacion, contorno, rotacion, recorte).
    static const char* kExtra[] = { "ClipsDescendants", "Rotation", "TextXAlignment",
        "TextYAlignment", "TextWrapped", "TextTransparency",
        "TextStrokeTransparency", "TextStrokeColor3", "TextTruncate", "RichText",
        "LineHeight", "MaxVisibleGraphemes" };
    static LocalVector<StringName> extras;
    if (extras.is_empty())
        for (int i = 0; i < 12; i++) extras.push_back(StringName(kExtra[i]));
    // Por el SETTER de la propiedad si la clase la tiene: el campo (lo que leen
    // Luau y Clone) queda igual que lo pintado. Aplicandolas solo al dibujo, un
    // TextLabel importado con TextXAlignment Left se veia bien, pero su Clone
    // salia centrado (en Build a Mountain, las cabeceras de seccion y los textos
    // de las tarjetas de la tienda, que se clonan de plantillas).
    for (uint32_t i = 0; i < extras.size(); i++) {
        if (!n->has_meta(extras[i])) continue;
        const Variant v = n->get_meta(extras[i]);
        if (n->get(extras[i]).get_type() != Variant::NIL) n->set(extras[i], v);
        else gl_apply_rbx_gui_extra(n, kExtra[i], v);
    }
}

// ── ZIndex 1:1 con Roblox ─────────────────────────────────────────────
// Sibling (el defecto): el ZIndex SOLO ordena hermanos y los hijos van siempre
// encima de su padre. Antes se metia en el z_index relativo de Godot, que SE
// SUMA por el arbol: en la pantalla de carga de Build a Mountain el sol
// (1+1+3+3 = 8) salia delante de la montana de bloques (3, de otra rama).
// Ahora: z_index 0 y el orden entre hermanos va en el indice de dibujo del
// RenderingServer (primero ZIndex, luego el orden en el arbol). Godot lo repone
// al mover hermanos, por eso tambien se llama en NOTIFICATION_MOVED_IN_PARENT.
// Global: el z_index absoluto de siempre. (El raton de Godot no mira ninguno.)
static inline void gl_gui_aplicar_z(CanvasItem* ci, int rbx_z) {
    if (!ci || !ci->is_inside_tree()) return;
    // El modo es el de su ScreenGui y se hereda del padre, que entro antes en el
    // arbol: lo que un script crea despues en un ScreenGui Global (el defecto de
    // Instance.new) tambien va en Global, no solo lo que habia al entrar.
    Node* padre = ci->get_parent();
    if (const CanvasItem* pc = gl_cast<CanvasItem>(padre)) {
        if (pc->is_z_relative() != ci->is_z_relative()) ci->set_z_as_relative(pc->is_z_relative());
    } else if (padre && padre->is_class("ScreenGui")) {
        const bool relativo = (int)padre->get("ZIndexBehavior") != 0;
        if (relativo != ci->is_z_relative()) ci->set_z_as_relative(relativo);
    }
    const int z = CLAMP(rbx_z, -4096, 4096);
    RenderingServer* rs = RenderingServer::get_singleton();
    if (ci->is_z_relative() && !gl_cast<CanvasItem>(padre)) {
        // Hijo directo de la ScreenGui (o de una Folder): es de nivel superior en
        // su capa y Godot le pisa el indice de dibujo con un contador de capa
        // (_top_level_raise_self) cada vez que cualquier hermano entra o se
        // mueve. Su ZIndex va en el z_index, que es por capa y nadie toca; sus
        // descendientes con z relativo 0 heredan ese z. En la pantalla de carga
        // de Build a Mountain la barra (Z 6) quedaba bajo el suelo (Z 2).
        if (ci->get_z_index() != z) ci->set_z_index(z);
    } else if (ci->is_z_relative()) {
        if (ci->get_z_index() != 0) ci->set_z_index(0);
        rs->canvas_item_set_draw_index(ci->get_canvas_item(), z * 65536 + ci->get_index());
    } else {
        if (ci->get_z_index() != z) ci->set_z_index(z);
        rs->canvas_item_set_draw_index(ci->get_canvas_item(), ci->get_index());
    }
}

// Godot 4.7 ya no manda MOVED_IN_PARENT: al anadir, quitar o mover un hijo marca
// al padre y, DIFERIDO, vuelve a poner a TODOS sus hijos el indice de dibujo =
// get_index() (Viewport::_process_dirty_canvas_parent_orders; bajo un ScreenGui
// ademas los "sube" con otra llamada de grupo diferida). Eso pisaba el orden por
// ZIndex en cuanto un padre ganaba un hijo: en la tienda de Build a Mountain el
// papel (Z 2) tapaba todo el contenido (Z 3). Por cada cambio de hijos (senal
// child_order_changed, que Godot conecta antes que nosotros) se reaplica con DOS
// saltos diferidos, para caer detras de sus dos pasadas. Solo trabaja la ultima
// de cada padre (numero de serie): N altas seguidas = una pasada.
inline HashMap<uint64_t, uint32_t>& gl_z_series() {
    static HashMap<uint64_t, uint32_t> m;
    return m;
}
inline void gl_z_reaplicar(uint64_t padre_id, uint32_t serie) {
    HashMap<uint64_t, uint32_t>& m = gl_z_series();
    const uint32_t* s = m.getptr(padre_id);
    if (!s || *s != serie) return;
    m.erase(padre_id);
    Node* padre = gl_cast<Node>(ObjectDB::get_instance(ObjectID(padre_id)));
    if (!padre || !padre->is_inside_tree()) return;
    static const StringName k_z("ZIndex");
    for (int i = 0; i < padre->get_child_count(); i++) {
        CanvasItem* ci = gl_cast<CanvasItem>(padre->get_child(i));
        if (!ci) continue;
        const Variant z = ci->get(k_z);
        if (z.get_type() == Variant::INT) gl_gui_aplicar_z(ci, (int)z);
    }
}
inline void gl_z_salto(uint64_t padre_id, uint32_t serie) {
    callable_mp_static(&gl_z_reaplicar).bind(padre_id, serie).call_deferred();
}
inline void gl_z_marcar(uint64_t padre_id) {
    const uint32_t serie = ++gl_z_series()[padre_id];
    callable_mp_static(&gl_z_salto).bind(padre_id, serie).call_deferred();
}
// Una conexion por padre (con cuenta de referencias, como la de Godot).
inline void gl_z_vigilar(Node* n, bool si) {
    Node* p = n ? n->get_parent() : nullptr;
    if (!p) return;
    static const StringName k_sig("child_order_changed");
    const Callable cb = callable_mp_static(&gl_z_marcar).bind((uint64_t)p->get_instance_id());
    if (si) p->connect(k_sig, cb, Object::CONNECT_REFERENCE_COUNTED);
    else if (p->is_connected(k_sig, cb)) p->disconnect(k_sig, cb);
}
#define GUI_Z_NOTIFICATION                                                        \
    if (p_what == NOTIFICATION_ENTER_TREE) {                                      \
        gl_gui_aplicar_z(this, _rbx_z);                                           \
        gl_z_vigilar(this, true);                                                 \
    } else if (p_what == NOTIFICATION_EXIT_TREE) {                                \
        gl_z_vigilar(this, false);                                                \
    }

// ────────────────────────────────────────────────────────────────────
//  Macros: campos, metodos y binds comunes de GuiObject.
//  El juego COMPLETO de propiedades de Roblox esta aqui, para que las 7 clases
//  lo tengan identico y no se quede ninguna fuera.
// ────────────────────────────────────────────────────────────────────
// Los valores por defecto son los de Instance.new en Roblox (medidos en Studio
// el 08/10): tamano 0x0, fondo gris 163,162,165 opaco, borde de 1 px color
// 27,42,53 y ZIndex 1. Lo importado trae sus valores; esto manda en la UI que
// crean los scripts (el Shatter de Motor2D centra cada trozo contando con 0x0).
#define GUI_COMMON_FIELDS                                 \
    GuiUDim2 _pos;                                        \
    GuiUDim2 _sz = {0, 0, 0, 0};                          \
    float    _anchor_x = 0, _anchor_y = 0;                \
    float    _bg_r = 163.0f / 255.0f, _bg_g = 162.0f / 255.0f, _bg_b = 165.0f / 255.0f; \
    float    _bg_alpha = 0; /* 0=opaque, 1=transparent */ \
    float    _border_r = 27.0f / 255.0f, _border_g = 42.0f / 255.0f, _border_b = 53.0f / 255.0f; \
    int      _border_px = 1;                              \
    int      _border_mode = 0;   /* Outline=0, Middle=1, Inset=2 */ \
    bool     _clips_descendants = false;                  \
    int      _layout_order = 0;                           \
    int      _rbx_z = 1;                                  \
    int      _size_constraint = 0;                        \
    int      _auto_size = 0;                              \
    float    _rot_deg = 0;                                \
    /* Active=false ES el defecto de Roblox (Frame/Label/Image). El rbxl solo */ \
    /* guarda lo que difiere del defecto, asi que con true aqui TODOS los     */ \
    /* frames importados quedaban "activos" y se comian los clics.            */ \
    bool     _active = false;                             \
    bool     _interactable = true;                        \
    bool     _selectable = false;                         \
    bool     _draggable = false;                          \
    int      _style = 0;                                  \
    String   _font_face;                                  \
    Vector2  _last_mod_size = Vector2(-1, -1);            \
    int      _font_weight = 400;

#define GUI_COMMON_APPLY_LAYOUT \
    gl_gui_apply_layout(this, _pos, _sz, _anchor_x, _anchor_y, _size_constraint, _rot_deg);

#define GUI_COMMON_METHODS(ClassName)                              \
    void set_udim2_pos(float xs, float xo, float ys, float yo) {   \
        _pos = {xs, xo, ys, yo};                                   \
        if (is_inside_tree()) GUI_COMMON_APPLY_LAYOUT              \
    }                                                              \
    void set_udim2_size(float xs, float xo, float ys, float yo) {  \
        _sz = {xs, xo, ys, yo};                                    \
        if (is_inside_tree()) GUI_COMMON_APPLY_LAYOUT              \
    }                                                              \
    void set_anchor_point(float ax, float ay) {                    \
        _anchor_x = ax; _anchor_y = ay;                            \
        if (is_inside_tree()) GUI_COMMON_APPLY_LAYOUT              \
    }                                                              \
    GuiUDim2 get_udim2_pos()  const { return _pos; }               \
    GuiUDim2 get_udim2_size() const { return _sz; }                \
    void set_bg_color(float r, float g, float b) {                 \
        _bg_r = r; _bg_g = g; _bg_b = b;                           \
        _apply_style();                                            \
    }                                                              \
    void set_bg_alpha(float a) {                                   \
        _bg_alpha = Math::clamp(a, 0.0f, 1.0f);                    \
        _apply_style();                                            \
    }                                                              \
    void set_border_color(float r, float g, float b) {             \
        _border_r = r; _border_g = g; _border_b = b;               \
        _apply_style();                                            \
    }                                                              \
    void set_border_px(int px) { _border_px = px; _apply_style(); } \
    float get_bg_r() const   { return _bg_r; }                     \
    float get_bg_g() const   { return _bg_g; }                     \
    float get_bg_b() const   { return _bg_b; }                     \
    float get_bg_alpha() const { return _bg_alpha; }               \
    /* --- Propiedades PascalCase de Roblox --- */                 \
    /* Position/Size = UDim2 como Vector4(xScale,xOffset,yScale,yOffset). \
       Son propiedades REALES para que duplicate() y el guardado de escena las \
       conserven: sin esto el clon de StarterGui a PlayerGui perdia posicion, \
       tamano y colores y el HUD entero salia en 100x50 arriba-izquierda. */ \
    void set_Position(Vector4 v) { set_udim2_pos(v.x, v.y, v.z, v.w); } \
    Vector4 get_Position() const { return Vector4(_pos.xs, _pos.xo, _pos.ys, _pos.yo); } \
    void set_Size(Vector4 v) { set_udim2_size(v.x, v.y, v.z, v.w); } \
    Vector4 get_Size() const { return Vector4(_sz.xs, _sz.xo, _sz.ys, _sz.yo); } \
    void set_AnchorPoint(Vector2 v) { set_anchor_point(v.x, v.y); } \
    Vector2 get_AnchorPoint() const { return Vector2(_anchor_x, _anchor_y); } \
    void set_BackgroundColor3(Color c) { set_bg_color(c.r, c.g, c.b); } \
    Color get_BackgroundColor3() const { return Color(_bg_r, _bg_g, _bg_b); } \
    void set_BackgroundTransparency(float a) { set_bg_alpha(a); } \
    float get_BackgroundTransparency() const { return _bg_alpha; } \
    void set_BorderColor3(Color c) { set_border_color(c.r, c.g, c.b); } \
    Color get_BorderColor3() const { return Color(_border_r, _border_g, _border_b); } \
    void set_BorderSizePixel(int px) { set_border_px(px); } \
    int  get_BorderSizePixel() const { return _border_px; } \
    void set_LayoutOrder(int v) { _layout_order = v; _gl_gui_bump_parent_layout(this); } \
    int  get_LayoutOrder() const { return _layout_order; }         \
    void set_ZIndex(int v) { _rbx_z = v; gl_gui_aplicar_z(this, v); } \
    int  get_ZIndex() const { return _rbx_z; }                     \
    void set_SizeConstraint(int v) {                               \
        _size_constraint = v;                                      \
        if (is_inside_tree()) GUI_COMMON_APPLY_LAYOUT              \
    }                                                              \
    int  get_SizeConstraint() const { return _size_constraint; }   \
    void set_AutomaticSize(int v) {                                \
        _auto_size = v;                                            \
        if (is_inside_tree()) call_deferred("_on_parent_resized");  \
    }                                                              \
    int  get_AutomaticSize() const { return _auto_size; }          \
    void set_rbx_rotation(float d) {                               \
        _rot_deg = d;                                              \
        set_pivot_offset(get_size() * 0.5f);                       \
        set_rotation((float)Math::deg_to_rad((double)d));          \
    }                                                              \
    float get_rbx_rotation() const { return _rot_deg; }            \
    void set_ClipsDescendants(bool b) { _clips_descendants = b; set_clip_contents(b); } \
    bool get_ClipsDescendants() const { return _clips_descendants; } \
    void set_Visible(bool b) { set_visible(b); }                   \
    bool get_Visible() const { return is_visible(); }              \
    void set_BorderMode(int v) { _border_mode = v; _apply_style(); } \
    int  get_BorderMode() const { return _border_mode; }           \
    void set_Active(bool b) {                                      \
        _active = b;                                               \
        /* Roblox: Active=false = TRANSPARENTE al raton (el clic cae a lo de */ \
        /* debajo, aunque sea de OTRA rama). El PASS de Godot no es eso:     */ \
        /* PASS consume el evento para su rama y lo sube a los PADRES — un   */ \
        /* overlay decorativo con PASS bloqueaba el boton de debajo (la      */ \
        /* tienda del place no abria por esto). IGNORE si es lo de Roblox.   */ \
        set_mouse_filter(b ? Control::MOUSE_FILTER_STOP : Control::MOUSE_FILTER_IGNORE); \
    }                                                              \
    bool get_Active() const { return _active; }                    \
    void set_Interactable(bool b) {                                \
        _interactable = b;                                         \
        set_mouse_filter(b ? Control::MOUSE_FILTER_STOP : Control::MOUSE_FILTER_IGNORE); \
    }                                                              \
    bool get_Interactable() const { return _interactable; }        \
    /* Selectable de Roblox habilita la navegacion con MANDO, no el foco de   \
       teclado. Traducirlo a Control::FOCUS_ALL hacia que el control cogiera   \
       el foco al pulsarlo y desde ese momento cada Space (la accion          \
       'ui_accept' de Godot, que es Space Y Enter) volvia a activarlo. Como   \
       Space tambien es saltar, el menu se abria al saltar. En Roblox NINGUNA \
       tecla activa un GuiButton, asi que aqui no se toca el foco. */         \
    void set_Selectable(bool b) { _selectable = b; }                \
    bool get_Selectable() const { return _selectable; }            \
    void set_Draggable(bool b) { _draggable = b; }                 \
    bool get_Draggable() const { return _draggable; }              \
    void set_Style(int v) { _style = v; }                          \
    int  get_Style() const { return _style; }                      \
    void set_FontFace(String v) { _font_face = v; }                \
    String get_FontFace() const { return _font_face; }             \
    void _gl_on_self_resized() {                                   \
        set_pivot_offset(get_size() * 0.5f);                       \
        /* Los modificadores que dependen del TAMANO hay que rehacerlos: */ \
        /* el radio del UICorner de Roblox es por ESCALA (0.3 x lado corto, */ \
        /* 581 de los 662 del place lo usan asi) y el shader del UIGradient */ \
        /* necesita el rect. Antes solo se aplicaban una vez, cuando el     */ \
        /* elemento aun medía 0, y el redondeo salia siempre en 0.          */ \
        /* FRENO: cambiar el radio de la caja cambia el tamano MINIMO del   */ \
        /* control, y eso puede volver a disparar "resized" -> bucle. Solo  */ \
        /* se rehacen si el tamano cambio de verdad (>1 px).                */ \
        const Vector2 _sz_now = get_size();                        \
        if (Math::abs(_sz_now.x - _last_mod_size.x) > 1.0f ||       \
            Math::abs(_sz_now.y - _last_mod_size.y) > 1.0f) {       \
            _last_mod_size = _sz_now;                              \
            _gl_gui_reapply_modifiers(this);                       \
        }                                                          \
        if (_auto_size != GL_AUTO_NONE) return; /* lo hace el layout */ \
        if (has_method("_gl_refit_text")) call("_gl_refit_text");   \
    }

// Binds compartidos: layout + todas las props PascalCase.
#define GUI_BIND_COMMON(C)                                                                    \
    ClassDB::bind_method(D_METHOD("_on_parent_resized"), &C::_on_parent_resized);              \
    ClassDB::bind_method(D_METHOD("_gl_on_self_resized"), &C::_gl_on_self_resized);            \
    ClassDB::bind_method(D_METHOD("set_udim2_pos","xs","xo","ys","yo"),  &C::set_udim2_pos);   \
    ClassDB::bind_method(D_METHOD("set_udim2_size","xs","xo","ys","yo"), &C::set_udim2_size);  \
    ClassDB::bind_method(D_METHOD("set_anchor_point","ax","ay"),         &C::set_anchor_point);\
    ClassDB::bind_method(D_METHOD("set_bg_color","r","g","b"),           &C::set_bg_color);    \
    ClassDB::bind_method(D_METHOD("set_bg_alpha","a"),                   &C::set_bg_alpha);    \
    ClassDB::bind_method(D_METHOD("set_border_color","r","g","b"),       &C::set_border_color);\
    ClassDB::bind_method(D_METHOD("set_border_px","px"),                 &C::set_border_px);   \
    ClassDB::bind_method(D_METHOD("set_LayoutOrder","v"),   &C::set_LayoutOrder);              \
    ClassDB::bind_method(D_METHOD("get_LayoutOrder"),       &C::get_LayoutOrder);              \
    ClassDB::bind_method(D_METHOD("set_ZIndex","v"),        &C::set_ZIndex);                   \
    ClassDB::bind_method(D_METHOD("get_ZIndex"),            &C::get_ZIndex);                   \
    ClassDB::bind_method(D_METHOD("set_SizeConstraint","v"),&C::set_SizeConstraint);           \
    ClassDB::bind_method(D_METHOD("get_SizeConstraint"),    &C::get_SizeConstraint);           \
    ClassDB::bind_method(D_METHOD("set_AutomaticSize","v"), &C::set_AutomaticSize);            \
    ClassDB::bind_method(D_METHOD("get_AutomaticSize"),     &C::get_AutomaticSize);            \
    ClassDB::bind_method(D_METHOD("set_rbx_rotation","d"),  &C::set_rbx_rotation);             \
    ClassDB::bind_method(D_METHOD("get_rbx_rotation"),      &C::get_rbx_rotation);             \
    ClassDB::bind_method(D_METHOD("set_ClipsDescendants","b"), &C::set_ClipsDescendants);      \
    ClassDB::bind_method(D_METHOD("get_ClipsDescendants"),  &C::get_ClipsDescendants);         \
    ClassDB::bind_method(D_METHOD("set_Visible","b"),       &C::set_Visible);                  \
    ClassDB::bind_method(D_METHOD("get_Visible"),           &C::get_Visible);                  \
    ClassDB::bind_method(D_METHOD("set_BorderMode","v"),    &C::set_BorderMode);               \
    ClassDB::bind_method(D_METHOD("get_BorderMode"),        &C::get_BorderMode);               \
    ClassDB::bind_method(D_METHOD("set_Active","b"),        &C::set_Active);                   \
    ClassDB::bind_method(D_METHOD("get_Active"),            &C::get_Active);                   \
    ClassDB::bind_method(D_METHOD("set_Interactable","b"),  &C::set_Interactable);             \
    ClassDB::bind_method(D_METHOD("get_Interactable"),      &C::get_Interactable);             \
    ClassDB::bind_method(D_METHOD("set_Selectable","b"),    &C::set_Selectable);               \
    ClassDB::bind_method(D_METHOD("get_Selectable"),        &C::get_Selectable);               \
    ClassDB::bind_method(D_METHOD("set_Draggable","b"),     &C::set_Draggable);                \
    ClassDB::bind_method(D_METHOD("get_Draggable"),         &C::get_Draggable);                \
    ClassDB::bind_method(D_METHOD("set_Style","v"),         &C::set_Style);                    \
    ClassDB::bind_method(D_METHOD("get_Style"),             &C::get_Style);                    \
    ClassDB::bind_method(D_METHOD("set_FontFace","v"),      &C::set_FontFace);                 \
    ClassDB::bind_method(D_METHOD("get_FontFace"),          &C::get_FontFace);                 \
    ClassDB::bind_method(D_METHOD("set_Position","v"),               &C::set_Position); \
    ClassDB::bind_method(D_METHOD("get_Position"),                   &C::get_Position); \
    ClassDB::bind_method(D_METHOD("set_Size","v"),                   &C::set_Size); \
    ClassDB::bind_method(D_METHOD("get_Size"),                       &C::get_Size); \
    ClassDB::bind_method(D_METHOD("set_AnchorPoint","v"),            &C::set_AnchorPoint); \
    ClassDB::bind_method(D_METHOD("get_AnchorPoint"),                &C::get_AnchorPoint); \
    ClassDB::bind_method(D_METHOD("set_BackgroundColor3","v"),       &C::set_BackgroundColor3); \
    ClassDB::bind_method(D_METHOD("get_BackgroundColor3"),           &C::get_BackgroundColor3); \
    ClassDB::bind_method(D_METHOD("set_BackgroundTransparency","v"), &C::set_BackgroundTransparency); \
    ClassDB::bind_method(D_METHOD("get_BackgroundTransparency"),     &C::get_BackgroundTransparency); \
    ClassDB::bind_method(D_METHOD("set_BorderColor3","v"),           &C::set_BorderColor3); \
    ClassDB::bind_method(D_METHOD("get_BorderColor3"),               &C::get_BorderColor3); \
    ClassDB::bind_method(D_METHOD("set_BorderSizePixel","v"),        &C::set_BorderSizePixel); \
    ClassDB::bind_method(D_METHOD("get_BorderSizePixel"),            &C::get_BorderSizePixel); \
    ADD_GROUP("Roblox GUI",""); \
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR4,"Position"),        "set_Position","get_Position"); \
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR4,"Size"),            "set_Size","get_Size"); \
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2,"AnchorPoint"),     "set_AnchorPoint","get_AnchorPoint"); \
    ADD_PROPERTY(PropertyInfo(Variant::COLOR, "BackgroundColor3"), "set_BackgroundColor3","get_BackgroundColor3"); \
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "BackgroundTransparency"), "set_BackgroundTransparency","get_BackgroundTransparency"); \
    ADD_PROPERTY(PropertyInfo(Variant::COLOR, "BorderColor3"),     "set_BorderColor3","get_BorderColor3"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,   "BorderSizePixel"),  "set_BorderSizePixel","get_BorderSizePixel");                                                               \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Visible"),          "set_Visible","get_Visible"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,   "LayoutOrder"),      "set_LayoutOrder","get_LayoutOrder"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,   "ZIndex"),           "set_ZIndex","get_ZIndex"); \
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Rotation"),         "set_rbx_rotation","get_rbx_rotation"); \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "ClipsDescendants"), "set_ClipsDescendants","get_ClipsDescendants"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,   "SizeConstraint",  PROPERTY_HINT_ENUM, "RelativeXY,RelativeXX,RelativeYY"), "set_SizeConstraint","get_SizeConstraint"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,   "AutomaticSize",   PROPERTY_HINT_ENUM, "None,X,Y,XY"), "set_AutomaticSize","get_AutomaticSize"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,   "BorderMode",      PROPERTY_HINT_ENUM, "Outline,Middle,Inset"), "set_BorderMode","get_BorderMode"); \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Active"),         "set_Active","get_Active");   \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Interactable"),   "set_Interactable","get_Interactable"); \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Selectable"),     "set_Selectable","get_Selectable"); \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "Draggable"),      "set_Draggable","get_Draggable"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,   "Style"),          "set_Style","get_Style");     \
    ADD_PROPERTY(PropertyInfo(Variant::STRING,"FontFace"),       "set_FontFace","get_FontFace");

// AutomaticSize: crece el elemento para que quepa su contenido.
#define GUI_APPLY_AUTO_SIZE(content)                                                   \
    if (_auto_size != GL_AUTO_NONE) {                                                  \
        const Vector2 _cur = get_size();                                               \
        Vector2 _want = _cur;                                                          \
        if (_auto_size == GL_AUTO_X || _auto_size == GL_AUTO_XY) _want.x = (content).x;  \
        if (_auto_size == GL_AUTO_Y || _auto_size == GL_AUTO_XY) _want.y = (content).y;  \
        if (_want != _cur) {                                                           \
            const float _dx = _want.x - _cur.x, _dy = _want.y - _cur.y;                \
            set_offset(SIDE_LEFT,   get_offset(SIDE_LEFT)   - _anchor_x * _dx);        \
            set_offset(SIDE_RIGHT,  get_offset(SIDE_RIGHT)  + (1.0f - _anchor_x) * _dx); \
            set_offset(SIDE_TOP,    get_offset(SIDE_TOP)    - _anchor_y * _dy);        \
            set_offset(SIDE_BOTTOM, get_offset(SIDE_BOTTOM) + (1.0f - _anchor_y) * _dy); \
        }                                                                              \
    }

// ────────────────────────────────────────────────────────────────────
//  ScreenGui — Interface layer (equivalent to Godot CanvasLayer)
////  ScreenGui — Capa de interfaz (equivalente al CanvasLayer de Godot)
// ────────────────────────────────────────────────────────────────────
class ScreenGui : public CanvasLayer {
    GDCLASS(ScreenGui, CanvasLayer);

    bool enabled          = true;
    bool reset_on_spawn   = true;
    bool ignore_gui_inset = false;
    int  display_order    = 0;
    // Los de Instance.new("ScreenGui") en Roblox (Studio, 08/10). Studio pone
    // Sibling al insertar uno a mano, pero eso viene guardado en el place.
    int  zindex_behavior  = 0;   // Global=0, Sibling=1
    bool clip_safe_area   = true;
    int  screen_insets    = 2;   // CoreUISafeInsets
    int  safe_area_compat = 1;   // SafeAreaCompatibility: FullscreenExtension (se guarda)

    // ZIndexBehavior.Global: el ZIndex es ABSOLUTO en toda la capa, asi que un
    // hijo con ZIndex bajo puede quedar por debajo de su padre. En Godot eso es
    // z_as_relative = false. Con Sibling (por defecto) el ZIndex solo ordena
    // hermanos y los hijos siempre van encima -> z_as_relative = true.
    void _apply_zindex_behavior(Node* n) {
        if (!n) return;
        if (CanvasItem* ci = gl_cast<CanvasItem>(n)) {
            ci->set_z_as_relative(zindex_behavior != 0);
            static const StringName k_z("ZIndex");
            const Variant z = ci->get(k_z);
            if (z.get_type() == Variant::INT) gl_gui_aplicar_z(ci, (int)z);
        }
        for (int i = 0; i < n->get_child_count(); i++) _apply_zindex_behavior(n->get_child(i));
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_sg_enabled","b"),          &ScreenGui::set_sg_enabled);
        ClassDB::bind_method(D_METHOD("get_sg_enabled"),               &ScreenGui::get_sg_enabled);
        ClassDB::bind_method(D_METHOD("set_reset_on_spawn","b"),      &ScreenGui::set_reset_on_spawn);
        ClassDB::bind_method(D_METHOD("get_reset_on_spawn"),           &ScreenGui::get_reset_on_spawn);
        ClassDB::bind_method(D_METHOD("set_ignore_gui_inset","b"),    &ScreenGui::set_ignore_gui_inset);
        ClassDB::bind_method(D_METHOD("get_ignore_gui_inset"),         &ScreenGui::get_ignore_gui_inset);
        ClassDB::bind_method(D_METHOD("set_display_order","o"),       &ScreenGui::set_display_order);
        ClassDB::bind_method(D_METHOD("get_display_order"),            &ScreenGui::get_display_order);
        ClassDB::bind_method(D_METHOD("set_ZIndexBehavior","v"),      &ScreenGui::set_ZIndexBehavior);
        ClassDB::bind_method(D_METHOD("get_ZIndexBehavior"),           &ScreenGui::get_ZIndexBehavior);
        ClassDB::bind_method(D_METHOD("set_ClipToDeviceSafeArea","v"),&ScreenGui::set_ClipToDeviceSafeArea);
        ClassDB::bind_method(D_METHOD("get_ClipToDeviceSafeArea"),     &ScreenGui::get_ClipToDeviceSafeArea);
        ClassDB::bind_method(D_METHOD("set_ScreenInsets","v"),        &ScreenGui::set_ScreenInsets);
        ClassDB::bind_method(D_METHOD("get_ScreenInsets"),             &ScreenGui::get_ScreenInsets);
        ClassDB::bind_method(D_METHOD("_gl_refresh_zindex"),           &ScreenGui::_gl_refresh_zindex);

        ADD_GROUP("ScreenGui","");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"Enabled"),         "set_sg_enabled","get_sg_enabled");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"ResetOnSpawn"),    "set_reset_on_spawn","get_reset_on_spawn");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"IgnoreGuiInset"),  "set_ignore_gui_inset","get_ignore_gui_inset");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "DisplayOrder"),    "set_display_order","get_display_order");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "ZIndexBehavior",   PROPERTY_HINT_ENUM,"Global,Sibling"), "set_ZIndexBehavior","get_ZIndexBehavior");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"ClipToDeviceSafeArea"), "set_ClipToDeviceSafeArea","get_ClipToDeviceSafeArea");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "ScreenInsets"),    "set_ScreenInsets","get_ScreenInsets");
        ClassDB::bind_method(D_METHOD("set_SafeAreaCompatibility","v"), &ScreenGui::set_SafeAreaCompatibility);
        ClassDB::bind_method(D_METHOD("get_SafeAreaCompatibility"),     &ScreenGui::get_SafeAreaCompatibility);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "SafeAreaCompatibility"), "set_SafeAreaCompatibility","get_SafeAreaCompatibility");
    }

    // REGLA DE ROBLOX: un ScreenGui SOLO se dibuja si esta dentro de PlayerGui
    // (o CoreGui). En cualquier otro sitio es una PLANTILLA guardada y no se ve:
    //   · StarterGui        -> se clona a PlayerGui al entrar; el original no se ve.
    //   · ReplicatedStorage / ReplicatedFirst / ServerStorage / Workspace ->
    //     interfaces que el juego guarda y clona cuando toca (tiendas, popups,
    //     la pantalla de carga...). Antes se dibujaban TODAS a la vez y la
    //     pantalla salia con paneles encima de paneles sin que nada los hubiera
    //     abierto: es justo lo que se veia en el place de prueba.
    bool _gl_should_render() const {
        if (Engine::get_singleton()->is_editor_hint()) return true;
        for (const Node* p = get_parent(); p; p = p->get_parent()) {
            const StringName nm = p->get_name();
            if (nm == StringName("PlayerGui") || nm == StringName("CoreGui")) return true;
            if (nm == StringName("StarterGui")) return false;
        }
        return false;
    }

protected:
    void _notification(int p_what) {
        if (p_what != NOTIFICATION_ENTER_TREE) return;
        // La COPIA hereda visible=false al duplicarse del original oculto, y sin
        // esto el HUD del jugador NUNCA aparecia. Al entrar en PlayerGui ya deja
        // de ser plantilla -> visible segun Enabled.
        set_visible(enabled && _gl_should_render());
        call_deferred("_gl_refresh_zindex");
    }

public:
    void set_sg_enabled(bool b) {
        enabled = b;
        set_visible(b && _gl_should_render());
    }
    bool get_sg_enabled() const          { return enabled; }
    void set_reset_on_spawn(bool b)      { reset_on_spawn = b; }
    bool get_reset_on_spawn() const      { return reset_on_spawn; }
    void set_ignore_gui_inset(bool b)    { ignore_gui_inset = b; }
    bool get_ignore_gui_inset() const    { return ignore_gui_inset; }
    // DisplayOrder de Roblox no tiene tope (el place usa 99999 para "siempre
    // encima"); el layer de un CanvasLayer si, asi que se recorta conservando
    // el orden relativo y se guarda el valor original para que los scripts lo lean.
    void set_display_order(int o) {
        display_order = o;
        set_layer(CLAMP(o, -128, 128));
    }
    int  get_display_order() const       { return display_order; }
    void set_ZIndexBehavior(int v) {
        zindex_behavior = v;
        if (is_inside_tree()) call_deferred("_gl_refresh_zindex");
    }
    int  get_ZIndexBehavior() const      { return zindex_behavior; }
    void set_ClipToDeviceSafeArea(bool b){ clip_safe_area = b; }
    bool get_ClipToDeviceSafeArea() const{ return clip_safe_area; }
    void set_ScreenInsets(int v)         { screen_insets = v; }
    void set_SafeAreaCompatibility(int v) { safe_area_compat = v; }
    int  get_SafeAreaCompatibility() const { return safe_area_compat; }
    int  get_ScreenInsets() const        { return screen_insets; }
    void _gl_refresh_zindex()            { _apply_zindex_behavior(this); }
};

// ────────────────────────────────────────────────────────────────────
//  RobloxFrame — Container/panel
////  RobloxFrame — Contenedor/panel
// ────────────────────────────────────────────────────────────────────
// ────────────────────────────────────────────────────────────────────
//  Callbacks for GUI signals from Luau
////  Callbacks para señales de GUI desde Luau
// ────────────────────────────────────────────────────────────────────
struct GuiLuaCallback {
    lua_State* main_L;
    int ref;
    bool active = true;
};

inline void _fire_gui_cbs(std::vector<GuiLuaCallback>& cbs) {
    gl_fire_cbs(cbs);
}

// InputBegan/InputEnded de GuiObject: los usa el ProximityPromptScript OFICIAL
// de Roblox para el modo tactil/clicable de los prompts. Se dispara con un
// InputObject minimo {UserInputType, UserInputState} — el script filtra por
// esos dos campos y nada mas. Se manda MouseButton1 (el filtro del script
// acepta MouseButton1 O Touch, asi que vale para los dos mundos). OJO: en el
// Enum.UserInputType de aqui MouseButton1 vale 1; con el 0 llegaba None y el
// filtro no lo dejaba pasar nunca.
inline void _fire_gui_cbs_input(std::vector<GuiLuaCallback>& cbs, bool began) {
    gl_fire_cbs(cbs, [began](lua_State* th) {
            lua_newtable(th);
            lua_getglobal(th, "__gl_enum_item");
            if (lua_isfunction(th, -1)) {
                lua_pushstring(th, "UserInputType");
                lua_pushinteger(th, 1);              // MouseButton1
                lua_call(th, 2, 1);
            } else { lua_pop(th, 1); lua_pushnil(th); }
            lua_setfield(th, -2, "UserInputType");
            lua_getglobal(th, "__gl_enum_item");
            if (lua_isfunction(th, -1)) {
                lua_pushstring(th, "UserInputState");
                lua_pushinteger(th, began ? 0 : 2);  // Begin : End
                lua_call(th, 2, 1);
            } else { lua_pop(th, 1); lua_pushnil(th); }
            lua_setfield(th, -2, "UserInputState");
            return 1;
    });
}

// ── Hover (MouseEnter / MouseLeave) para cualquier GuiObject ─────────
//  En Roblox TODOS los GuiObject tienen estas dos señales. Aquí solo las
//  tenían los botones: en un Frame estaban declaradas pero sin cablear (el
//  callback no se llamaba nunca y Connect no devolvía conexión), y en
//  TextLabel/ImageLabel/ScrollingFrame no existían — leerlas reventaba con
//  "MouseEnter is not a valid member of TextLabel".
#define GUI_HOVER_FIELDS                     \
    std::vector<GuiLuaCallback> _enter_cbs;  \
    std::vector<GuiLuaCallback> _leave_cbs;

#define GUI_HOVER_BIND(C)                                                            \
    ClassDB::bind_method(D_METHOD("_on_mouse_enter"), &C::_on_mouse_enter);           \
    ClassDB::bind_method(D_METHOD("_on_mouse_leave"), &C::_on_mouse_leave);           \
    ADD_SIGNAL(MethodInfo("MouseEnter"));                                             \
    ADD_SIGNAL(MethodInfo("MouseLeave"));

#define GUI_HOVER_METHODS                                                            \
    void _on_mouse_enter() { emit_signal("MouseEnter"); _fire_gui_cbs(_enter_cbs); }  \
    void _on_mouse_leave() { emit_signal("MouseLeave"); _fire_gui_cbs(_leave_cbs); }  \
    /* Conectar MouseEnter/MouseLeave registra el elemento en el hover CENTRAL */ \
    /* (gl_hover_tick): SOLO los elementos con conexiones se vigilan, el resto */ \
    /* no cuesta nada — y el filtro del raton se queda en IGNORE, que es lo    */ \
    /* que no bloquea los clics de lo que haya debajo (como Roblox).           */ \
    void add_enter_cb(lua_State* L, int ref) { _enter_cbs.push_back({L, ref, true}); gl_hover_registrar(this); }\
    void add_leave_cb(lua_State* L, int ref) { _leave_cbs.push_back({L, ref, true}); gl_hover_registrar(this); }\
    void _gl_hover_disconnect(int ref, int64_t vm) {                                              \
        for (auto& cb : _enter_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;             \
        for (auto& cb : _leave_cbs) if (cb.ref == ref && GL_MISMA_VM(cb)) cb.active = false;             \
    }

// El filtro del raton segun Active (Roblox): false = invisible al input,
// true = lo hunde (sink). ANTES esto promocionaba IGNORE→PASS y conectaba
// mouse_entered en TODA Frame/Label — cada elemento escuchaba hover aunque
// nadie lo pidiera, y de paso bloqueaba los clics de lo que tuviera debajo.
// El hover ahora es central y por registro (ver gl_hover_tick).
// Devuelve el raton al filtro que le toca a esta clase. Lo llama la puerta de
// dibujado cuando un nodo VUELVE a estar dentro de un ScreenGui: al reparentar,
// ENTER_TREE corre otra vez y hay que rehacer lo que la puerta apago.
#define GUI_REWIRE_MOUSE_PASIVO                                                          void _gl_rewire_mouse() {                                                                set_mouse_filter(_active ? Control::MOUSE_FILTER_STOP : Control::MOUSE_FILTER_IGNORE);     }

// Un boton de Roblox nace con Active = true, asi que su sitio es STOP. Usar el
// macro pasivo aqui dejaria MUDA toda la UI creada por script (Active arranca
// en false) — es la trampa que senalo la revision.
#define GUI_REWIRE_MOUSE_BOTON                                                           void _gl_rewire_mouse() { set_mouse_filter(Control::MOUSE_FILTER_STOP); }

// Cinturon Y tirantes para lo del menu que se abria con Space. Aunque el boton
// ya nace con FOCUS_NONE, algo puede llamarle grab_focus() por codigo; si eso
// pasa, Godot le manda 'ui_accept' (Space/Enter) y BaseButton lo convierte en
// 'pressed' → MouseButton1Click. En Roblox una TECLA no activa un GuiButton
// jamas, asi que se traga aqui. El MANDO si puede (Roblox tambien lo permite
// sobre el objeto seleccionado), por eso solo se filtra InputEventKey.
// Selected y Modal de GuiButton: se guardan (Selected es el estado de la
// seleccion con mando; Modal suelta el raton en primera persona). Leerlos
// reventaba el script ("is not a valid member").
#define GUI_BOTON_EXTRA_FIELDS bool _selected = false; bool _modal = false;
#define GUI_BOTON_EXTRA_METHODS                                                  \
    void set_Selected(bool v) { _selected = v; }                                  \
    bool get_Selected() const { return _selected; }                               \
    void set_Modal(bool v) { _modal = v; }                                        \
    bool get_Modal() const { return _modal; }
#define GUI_BOTON_EXTRA_BIND(C)                                                  \
    ClassDB::bind_method(D_METHOD("set_Selected", "v"), &C::set_Selected);        \
    ClassDB::bind_method(D_METHOD("get_Selected"), &C::get_Selected);             \
    ClassDB::bind_method(D_METHOD("set_Modal", "v"), &C::set_Modal);              \
    ClassDB::bind_method(D_METHOD("get_Modal"), &C::get_Modal);                   \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Selected"), "set_Selected", "get_Selected"); \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Modal"), "set_Modal", "get_Modal");

#define GUI_BOTON_NO_TECLADO                                                     \
    void _gui_input(const Ref<InputEvent>& event) override {                      \
        if (event.is_valid() && gl_cast<InputEventKey>(*event) &&          \
            event->is_action("ui_accept", true)) { accept_event(); return; }       \
    }

#define GUI_HOVER_WIRE                                                               \
    set_mouse_filter(_active ? Control::MOUSE_FILTER_STOP : Control::MOUSE_FILTER_IGNORE);

class RobloxFrame : public Panel {
    GDCLASS(RobloxFrame, Panel);
    GUI_COMMON_FIELDS
    GUI_HOVER_FIELDS

    void _apply_style() {
        // Fuera del arbol NO se toca el tema: cada set de una propiedad de estilo
        // creaba un StyleBoxFlat que se tiraba enseguida. Al DUPLICAR un nodo,
        // Godot copia todas sus propiedades una a una, asi que un solo TextLabel
        // clonado creaba media docena de estilos para nada. ENTER_TREE lo aplica
        // una vez, que es cuando de verdad se ve.
        if (!is_inside_tree()) return;
        // La caja sale de la cache compartida: misma pinta -> mismo recurso.
        // Casi toda la UI de un place es fondo transparente y sin borde, o sea
        // que miles de elementos acaban apuntando a UNA sola caja en vez de
        // fabricarse cada uno la suya al entrar en el arbol.
        gl_tema_caja(this, gl_claves_caja()[0], gl_estilo_compartido(
            Color(_bg_r, _bg_g, _bg_b, 1.0f - _bg_alpha),
            Color(_border_r, _border_g, _border_b, 1.0f - _bg_alpha),
            gl_borde_px(this, _border_px)));
        _gl_estilo_puesto();
        _gl_gui_reapply_modifiers(this);
    }

protected:
    // Un CanvasGroup cambia aqui el fondo por su mascara (el fondo lo pinta aparte).
    virtual void _gl_estilo_puesto() {}

    void _notification(int p_what) {
        GUI_Z_NOTIFICATION
        if (p_what == NOTIFICATION_ENTER_TREE) {
            gl_apply_rbx_gui_meta(this);   // props importadas guardadas como meta
            gl_gui_apply_render_gate(this);  // regla de Roblox: solo dentro de un *Gui
            GUI_COMMON_APPLY_LAYOUT
            _apply_style();
            _gl_gui_wire_relayout(this, "_on_parent_resized");
            GUI_HOVER_WIRE
        }
    }

protected:
    static void _bind_methods() {
        GUI_BIND_COMMON(RobloxFrame)
        GUI_HOVER_BIND(RobloxFrame)
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &RobloxFrame::_gl_disconnect);
    }

public:
    GUI_REWIRE_MOUSE_PASIVO
    GUI_COMMON_METHODS(RobloxFrame)
    GUI_HOVER_METHODS
    void _gl_disconnect(int ref, int64_t vm) { _gl_hover_disconnect(ref, vm); }

    void _on_parent_resized() {
        GUI_COMMON_APPLY_LAYOUT
        GUI_APPLY_AUTO_SIZE(gl_gui_children_extent(this))
    }
};

#include "roblox_gui_texto.h"    // TextLabel, TextButton, TextBox
#include "roblox_gui_imagen.h"   // ImageButton, ImageLabel

// ────────────────────────────────────────────────────────────────────
//  RobloxScrollingFrame — Scrollable container
//  Antes heredaba de ScrollContainer, que es un CONTENEDOR de Godot: forzaba
//  el rect de sus hijos y por eso todo lo que hubiera dentro de un
//  ScrollingFrame importado salia estirado y encima de lo demas. Ahora es un
//  Panel con recorte + desplazamiento propio: los hijos conservan su UDim2 y
//  el scroll los traslada (gl_gui_apply_layout consulta _gl_scroll).
////  RobloxScrollingFrame — Contenedor con scroll
// ────────────────────────────────────────────────────────────────────
class RobloxScrollingFrame : public Panel {
    GDCLASS(RobloxScrollingFrame, Panel);
    GUI_COMMON_FIELDS
    GUI_HOVER_FIELDS
    GuiUDim2 _canvas_size = {0, 0, 2, 0};   // el de Roblox: el doble de alto
    Vector2  _canvas_pos;
    bool     _scrolling_enabled = true;
    int      _scroll_dir = 4;      // Enum.ScrollingDirection: X=1, Y=2, XY=4
    int      _bar_thickness = 12;
    Color    _bar_color = Color(0.7f, 0.7f, 0.7f);
    float    _bar_alpha = 1.0f;
    int      _auto_canvas = 0;     // None=0, X=1, Y=2, XY=3
    int      _elastic = 0;         // Enum.ElasticBehavior: WhenScrollable=0, Always=1, Never=2
    int      _v_inset = 0, _h_inset = 0, _v_bar_pos = 0;
    String   _top_image = "rbxasset://textures/ui/Scroll/scroll-top.png";
    String   _mid_image = "rbxasset://textures/ui/Scroll/scroll-middle.png";
    String   _bottom_image = "rbxasset://textures/ui/Scroll/scroll-bottom.png";

    void _apply_style() {
        // Fuera del arbol NO se toca el tema: cada set de una propiedad de estilo
        // creaba un StyleBoxFlat que se tiraba enseguida. Al DUPLICAR un nodo,
        // Godot copia todas sus propiedades una a una, asi que un solo TextLabel
        // clonado creaba media docena de estilos para nada. ENTER_TREE lo aplica
        // una vez, que es cuando de verdad se ve.
        if (!is_inside_tree()) return;
        // La caja sale de la cache compartida: misma pinta -> mismo recurso.
        // Casi toda la UI de un place es fondo transparente y sin borde, o sea
        // que miles de elementos acaban apuntando a UNA sola caja en vez de
        // fabricarse cada uno la suya al entrar en el arbol.
        gl_tema_caja(this, gl_claves_caja()[0], gl_estilo_compartido(
            Color(_bg_r, _bg_g, _bg_b, 1.0f - _bg_alpha),
            Color(_border_r, _border_g, _border_b, 1.0f - _bg_alpha),
            gl_borde_px(this, _border_px)));
        _gl_gui_reapply_modifiers(this);
    }

protected:
    void _notification(int p_what) {
        GUI_Z_NOTIFICATION
        if (p_what == NOTIFICATION_ENTER_TREE) {
            gl_apply_rbx_gui_meta(this);   // props importadas guardadas como meta
            gl_gui_apply_render_gate(this);  // regla de Roblox: solo dentro de un *Gui
            set_clip_contents(true);       // un ScrollingFrame de Roblox recorta siempre
            GUI_COMMON_APPLY_LAYOUT
            _apply_style();
            _gl_gui_wire_relayout(this, "_on_parent_resized");
            GUI_HOVER_WIRE
            // DESPUES del WIRE (que pone el filtro segun Active): la rueda solo
            // llega a _gui_input con filtro STOP. Un ScrollingFrame que scrollea
            // captura el raton aunque Active sea false — igual que en Roblox,
            // donde ScrollingEnabled manda sobre el input de la rueda.
            //
            // PERO NO SI NO SE DIBUJA: si la puerta lo mando a la capa de mundo,
            // este era uno de los pocos que se saltaba el IGNORE y seguia
            // cazando el raton sin verse.
            if (_scrolling_enabled && get_visibility_layer() != GL_WORLD_GUI_CANVAS_LAYER)
                set_mouse_filter(Control::MOUSE_FILTER_STOP);
            // Alta en el registro: es lo que consulta la rueda para saber si el
            // cursor esta sobre un ScrollingFrame antes de mover la camara.
            gl_scroll_registrar(this);
            _gl_pintar_barra();
        } else if (p_what == NOTIFICATION_EXIT_TREE) {
            gl_scroll_olvidar(this);
        } else if (p_what == NOTIFICATION_RESIZED) {
            _gl_pintar_barra();
        } else if (p_what == NOTIFICATION_CHILD_ORDER_CHANGED && _auto_canvas != 0) {
            _gl_barra_luego();   // el lienzo automatico depende de los hijos
        }
    }

    // ¿Esta rueda es MIA? Solo si de verdad tengo algo que desplazar en ese eje.
    //
    // Antes bastaba con que el cursor cayera encima de CUALQUIER ScrollingFrame
    // para que la camara dejara de hacer zoom, y el place tiene frames de estos
    // por todo el HUD: el resultado era que la rueda no acercaba ni alejaba la
    // camara casi en ningun sitio. En Roblox un ScrollingFrame sin desbordamiento
    // no se come la rueda. Si SI hay desbordamiento se la queda entera, aunque ya
    // este tocando el tope (eso tambien es Roblox).
    bool _gl_puede_rueda(bool vertical) const {
        if (!_scrolling_enabled) return false;
        if (get_visibility_layer() == GL_WORLD_GUI_CANVAS_LAYER) return false;
        const bool can_x = (_scroll_dir == 1 || _scroll_dir == 4);
        const bool can_y = (_scroll_dir == 2 || _scroll_dir == 4);
        if (vertical && !can_y) return false;
        if (!vertical && !can_x) return false;
        const Vector2 mx = _max_scroll();
        return vertical ? (mx.y > 0.5f) : (mx.x > 0.5f);
    }

    // Extremo maximo al que se puede desplazar: lienzo menos ventana.
    Vector2 _max_scroll() const {
        const Vector2 canvas = gl_lienzo_px();
        const Vector2 win = gl_ventana_px();
        return Vector2(MAX(0.0f, canvas.x - win.x), MAX(0.0f, canvas.y - win.y));
    }

    // ── LIENZO Y VENTANA, como Roblox (medido en Studio el 08/10) ──────
    // · La escala de CanvasSize se mide contra el PADRE del ScrollingFrame, como
    //   la de Size (CanvasSize 1,2 en un padre de 500 px = 600 px), no contra el
    //   propio frame.
    // · El lienzo nunca es menor que la ventana.
    // · Ventana (AbsoluteWindowSize) = el frame menos la barra (grosor + borde) en
    //   cada eje con barra; hay barra si el eje se desplaza y el lienzo lo desborda.
    // · Con ScrollBarInset ScrollBar o Always el lienzo pierde tambien ese hueco;
    //   con None (el de serie) la barra va encima del contenido.
    // Los hijos, su UIPadding y los layouts se miden contra el lienzo
    // (gl_gui_contenido_px): en la tienda de Build a Mountain las tarjetas salian
    // de 110 px de alto en vez de 135.
    Vector2 _gl_lienzo_bruto(bool con_auto) const {
        Vector2 padre;
        if (const Control* p = gl_cast<Control>(get_parent())) padre = p->get_size();
        else if (Viewport* vp = get_viewport()) padre = vp->get_visible_rect().size;
        Vector2 c(_canvas_size.xs * padre.x + _canvas_size.xo, _canvas_size.ys * padre.y + _canvas_size.yo);
        if (con_auto && _auto_canvas != 0) {
            const Vector2 ext = gl_gui_children_extent(const_cast<RobloxScrollingFrame*>(this));
            if (_auto_canvas == 1 || _auto_canvas == 3) c.x = MAX(c.x, ext.x);
            if (_auto_canvas == 2 || _auto_canvas == 3) c.y = MAX(c.y, ext.y);
        }
        return c;
    }
    // Barras que se ven: h = la horizontal (abajo), v = la vertical.
    void _gl_barras(const Vector2& bruto, bool& h, bool& v) const {
        const Vector2 own = get_size();
        const bool can_x = (_scroll_dir == 1 || _scroll_dir == 4);
        const bool can_y = (_scroll_dir == 2 || _scroll_dir == 4);
        h = _bar_thickness > 0 && can_x && bruto.x > own.x + 0.5f;
        v = _bar_thickness > 0 && can_y && bruto.y > own.y + 0.5f;
    }
    float _gl_hueco_barra() const { return (float)(_bar_thickness + MAX(0, _border_px)); }
    Vector2 _gl_lienzo(bool con_auto) const {
        const Vector2 own = get_size();
        const Vector2 bruto = _gl_lienzo_bruto(con_auto);
        bool h, v;
        _gl_barras(bruto, h, v);
        const float hueco = _gl_hueco_barra();
        return Vector2(MAX(bruto.x, own.x - ((v && _v_inset != 0) ? hueco : 0.0f)),
                       MAX(bruto.y, own.y - ((h && _h_inset != 0) ? hueco : 0.0f)));
    }

public:
    // El lienzo en pixeles (AbsoluteCanvasSize), con AutomaticCanvasSize.
    Vector2 gl_lienzo_px() const { return _gl_lienzo(true); }
    // Contra lo que se miden los hijos: el lienzo sin AutomaticCanvasSize (con
    // el, un hijo con tamano en escala agrandaba el lienzo, que lo agrandaba a
    // el, y asi sin fin).
    Vector2 _gl_lienzo_hijos() const { return _gl_lienzo(false); }
    // La ventana en pixeles (AbsoluteWindowSize).
    Vector2 gl_ventana_px() const {
        const Vector2 own = get_size();
        bool h, v;
        _gl_barras(_gl_lienzo_bruto(true), h, v);
        const float hueco = _gl_hueco_barra();
        return Vector2(MAX(0.0f, own.x - (v ? hueco : 0.0f)), MAX(0.0f, own.y - (h ? hueco : 0.0f)));
    }

    // ── LA BARRA ──────────────────────────────────────────────────────
    // No se dibujaba. Va en un canvas item propio por ENCIMA de los hijos (como
    // en Roblox), con el color de ScrollBarImageColor3. La imagen de serie de
    // Roblox es blanca al 50 % (medido: alfa = (1 - ScrollBarImageTransparency)
    // x 0,5); no se usa: aqui es una pastilla dibujada, con ese mismo alfa.
    RID _barra;
    Ref<StyleBoxFlat> _barra_estilo;
    void _gl_pintar_barra() {
        if (!_barra.is_valid() || !is_inside_tree()) return;
        RenderingServer* rs = RenderingServer::get_singleton();
        rs->canvas_item_clear(_barra);
        rs->canvas_item_set_visibility_layer(_barra, get_visibility_layer());
        if (_bar_alpha <= 0.0f) return;
        const Vector2 own = get_size();
        const Vector2 canvas = gl_lienzo_px();
        const Vector2 win = gl_ventana_px();
        bool h, v;
        _gl_barras(_gl_lienzo_bruto(true), h, v);
        if (!h && !v) return;
        const float t = (float)_bar_thickness;
        if (_barra_estilo.is_null()) {
            _barra_estilo.instantiate();
            _barra_estilo->set_anti_aliased(true);
        }
        _barra_estilo->set_bg_color(Color(_bar_color.r, _bar_color.g, _bar_color.b, _bar_alpha * 0.5f));
        _barra_estilo->set_corner_radius_all((int)Math::ceil(t * 0.5f));
        if (v) {
            const float pista = win.y;
            const float largo = CLAMP(pista * win.y / MAX(1.0f, canvas.y), MIN(t, pista), pista);
            const float maxs = canvas.y - win.y;
            const float y = maxs > 0.0f ? (pista - largo) * CLAMP(_canvas_pos.y / maxs, 0.0f, 1.0f) : 0.0f;
            const float x = (_v_bar_pos == 1) ? 0.0f : own.x - t;   // VerticalScrollBarPosition: Right=0, Left=1
            _barra_estilo->draw(_barra, Rect2(x, y, t, largo));
        }
        if (h) {
            const float pista = win.x;
            const float largo = CLAMP(pista * win.x / MAX(1.0f, canvas.x), MIN(t, pista), pista);
            const float maxs = canvas.x - win.x;
            const float x = maxs > 0.0f ? (pista - largo) * CLAMP(_canvas_pos.x / maxs, 0.0f, 1.0f) : 0.0f;
            _barra_estilo->draw(_barra, Rect2(x, own.y - t, largo, t));
        }
    }
    // Repintado de la barra al final del frame, uno solo aunque lo pidan cien
    // hijos seguidos (llenar una lista de golpe).
    bool _barra_pendiente = false;
    void _gl_barra_luego() {
        if (_barra_pendiente) return;
        _barra_pendiente = true;
        callable_mp(this, &RobloxScrollingFrame::_gl_barra_ya).call_deferred();
    }
    void _gl_barra_ya() { _barra_pendiente = false; _gl_pintar_barra(); }
    // Algo que cambia el lienzo: los hijos se vuelven a medir y la barra se repinta.
    void _gl_lienzo_cambio() {
        if (!is_inside_tree()) return;
        gl_gui_relayout_children(this);
        _gl_pintar_barra();
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_puede_rueda","vertical"), &RobloxScrollingFrame::_gl_puede_rueda);
        ClassDB::bind_method(D_METHOD("set_ScrollingEnabled","b"),  &RobloxScrollingFrame::set_ScrollingEnabled);
        ClassDB::bind_method(D_METHOD("get_ScrollingEnabled"),      &RobloxScrollingFrame::get_ScrollingEnabled);
        ClassDB::bind_method(D_METHOD("set_gl_canvas_size","xs","xo","ys","yo"), &RobloxScrollingFrame::set_gl_canvas_size);
        ClassDB::bind_method(D_METHOD("set_CanvasSize","v"),        &RobloxScrollingFrame::set_CanvasSize);
        ClassDB::bind_method(D_METHOD("get_CanvasSize"),            &RobloxScrollingFrame::get_CanvasSize);
        ClassDB::bind_method(D_METHOD("set_CanvasPosition","v"),    &RobloxScrollingFrame::set_CanvasPosition);
        ClassDB::bind_method(D_METHOD("get_CanvasPosition"),        &RobloxScrollingFrame::get_CanvasPosition);
        ClassDB::bind_method(D_METHOD("set_ScrollBarThickness","v"),&RobloxScrollingFrame::set_ScrollBarThickness);
        ClassDB::bind_method(D_METHOD("get_ScrollBarThickness"),    &RobloxScrollingFrame::get_ScrollBarThickness);
        ClassDB::bind_method(D_METHOD("set_ScrollBarImageColor3","v"), &RobloxScrollingFrame::set_ScrollBarImageColor3);
        ClassDB::bind_method(D_METHOD("get_ScrollBarImageColor3"),     &RobloxScrollingFrame::get_ScrollBarImageColor3);
        ClassDB::bind_method(D_METHOD("set_ScrollBarImageTransparency","v"), &RobloxScrollingFrame::set_ScrollBarImageTransparency);
        ClassDB::bind_method(D_METHOD("get_ScrollBarImageTransparency"),     &RobloxScrollingFrame::get_ScrollBarImageTransparency);
        ClassDB::bind_method(D_METHOD("set_ScrollingDirection","v"),&RobloxScrollingFrame::set_ScrollingDirection);
        ClassDB::bind_method(D_METHOD("get_ScrollingDirection"),    &RobloxScrollingFrame::get_ScrollingDirection);
        ClassDB::bind_method(D_METHOD("set_AutomaticCanvasSize","v"), &RobloxScrollingFrame::set_AutomaticCanvasSize);
        ClassDB::bind_method(D_METHOD("get_AutomaticCanvasSize"),     &RobloxScrollingFrame::get_AutomaticCanvasSize);
        ClassDB::bind_method(D_METHOD("set_ElasticBehavior","v"),  &RobloxScrollingFrame::set_ElasticBehavior);
        ClassDB::bind_method(D_METHOD("get_ElasticBehavior"),      &RobloxScrollingFrame::get_ElasticBehavior);
        ClassDB::bind_method(D_METHOD("set_VerticalScrollBarInset","v"),   &RobloxScrollingFrame::set_VerticalScrollBarInset);
        ClassDB::bind_method(D_METHOD("get_VerticalScrollBarInset"),       &RobloxScrollingFrame::get_VerticalScrollBarInset);
        ClassDB::bind_method(D_METHOD("set_HorizontalScrollBarInset","v"), &RobloxScrollingFrame::set_HorizontalScrollBarInset);
        ClassDB::bind_method(D_METHOD("get_HorizontalScrollBarInset"),     &RobloxScrollingFrame::get_HorizontalScrollBarInset);
        ClassDB::bind_method(D_METHOD("set_VerticalScrollBarPosition","v"),&RobloxScrollingFrame::set_VerticalScrollBarPosition);
        ClassDB::bind_method(D_METHOD("get_VerticalScrollBarPosition"),    &RobloxScrollingFrame::get_VerticalScrollBarPosition);
        ClassDB::bind_method(D_METHOD("set_TopImage","v"),    &RobloxScrollingFrame::set_TopImage);
        ClassDB::bind_method(D_METHOD("get_TopImage"),        &RobloxScrollingFrame::get_TopImage);
        ClassDB::bind_method(D_METHOD("set_MidImage","v"),    &RobloxScrollingFrame::set_MidImage);
        ClassDB::bind_method(D_METHOD("get_MidImage"),        &RobloxScrollingFrame::get_MidImage);
        ClassDB::bind_method(D_METHOD("set_BottomImage","v"), &RobloxScrollingFrame::set_BottomImage);
        ClassDB::bind_method(D_METHOD("get_BottomImage"),     &RobloxScrollingFrame::get_BottomImage);
        ClassDB::bind_method(D_METHOD("_gl_scroll"),          &RobloxScrollingFrame::_gl_scroll);
        ClassDB::bind_method(D_METHOD("_gl_lienzo_hijos"),    &RobloxScrollingFrame::_gl_lienzo_hijos);
        ClassDB::bind_method(D_METHOD("_gl_pintar_barra"),    &RobloxScrollingFrame::_gl_barra_luego);
        GUI_BIND_COMMON(RobloxScrollingFrame)
        GUI_HOVER_BIND(RobloxScrollingFrame)
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &RobloxScrollingFrame::_gl_disconnect);

        ADD_GROUP("ScrollingFrame","");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,   "ScrollingEnabled"),  "set_ScrollingEnabled","get_ScrollingEnabled");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR4,"CanvasSize"),        "set_CanvasSize","get_CanvasSize");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR2,"CanvasPosition"),    "set_CanvasPosition","get_CanvasPosition");
        ADD_PROPERTY(PropertyInfo(Variant::INT,    "ScrollBarThickness"),"set_ScrollBarThickness","get_ScrollBarThickness");
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,  "ScrollBarImageColor3"),      "set_ScrollBarImageColor3","get_ScrollBarImageColor3");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "ScrollBarImageTransparency"),"set_ScrollBarImageTransparency","get_ScrollBarImageTransparency");
        ADD_PROPERTY(PropertyInfo(Variant::INT,    "ScrollingDirection", PROPERTY_HINT_ENUM,"None,X,Y,,XY"), "set_ScrollingDirection","get_ScrollingDirection");
        ADD_PROPERTY(PropertyInfo(Variant::INT,    "AutomaticCanvasSize",PROPERTY_HINT_ENUM,"None,X,Y,XY"), "set_AutomaticCanvasSize","get_AutomaticCanvasSize");
        ADD_PROPERTY(PropertyInfo(Variant::INT,    "ElasticBehavior"),   "set_ElasticBehavior","get_ElasticBehavior");
        ADD_PROPERTY(PropertyInfo(Variant::INT,    "VerticalScrollBarInset"),   "set_VerticalScrollBarInset","get_VerticalScrollBarInset");
        ADD_PROPERTY(PropertyInfo(Variant::INT,    "HorizontalScrollBarInset"), "set_HorizontalScrollBarInset","get_HorizontalScrollBarInset");
        ADD_PROPERTY(PropertyInfo(Variant::INT,    "VerticalScrollBarPosition"),"set_VerticalScrollBarPosition","get_VerticalScrollBarPosition");
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "TopImage"),    "set_TopImage","get_TopImage");
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "MidImage"),    "set_MidImage","get_MidImage");
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "BottomImage"), "set_BottomImage","get_BottomImage");
    }

public:
    // Un ScrollingFrame nuevo de Roblox recorta y es Selectable.
    RobloxScrollingFrame() {
        _clips_descendants = true;
        _selectable = true;
        set_meta(gl_marca_scroll(), true);   // gl_gui_scroll_of / gl_gui_contenido_px
        // La barra, encima de todos los hijos: Godot solo reordena los indices de
        // los nodos hijos, no este canvas item suelto.
        RenderingServer* rs = RenderingServer::get_singleton();
        _barra = rs->canvas_item_create();
        rs->canvas_item_set_parent(_barra, get_canvas_item());
        rs->canvas_item_set_draw_index(_barra, INT32_MAX);
    }
    ~RobloxScrollingFrame() {
        if (_barra.is_valid() && RenderingServer::get_singleton()) RenderingServer::get_singleton()->free_rid(_barra);
    }
    // NO usa el macro pasivo. Ese pone MOUSE_FILTER_IGNORE cuando Active es
    // false, y en Roblox ScrollingFrame.Active nace en FALSE — pero la rueda
    // sigue funcionando. Con el macro pasivo, la puerta de dibujado lo dejaba en
    // IGNORE, _gui_input no se llamaba nunca y el menu de Settings no scrolleaba.
    void _gl_rewire_mouse() {
        if (get_visibility_layer() == GL_WORLD_GUI_CANVAS_LAYER) {
            set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        } else if (_scrolling_enabled || _active) {
            set_mouse_filter(Control::MOUSE_FILTER_STOP);
        } else {
            set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        }
    }
    GUI_COMMON_METHODS(RobloxScrollingFrame)
    GUI_HOVER_METHODS
    void _gl_disconnect(int ref, int64_t vm) { _gl_hover_disconnect(ref, vm); }

    // El lienzo depende del tamano del PADRE: aunque este frame no cambie, sus
    // hijos se vuelven a medir.
    void _on_parent_resized() {
        GUI_COMMON_APPLY_LAYOUT
        _gl_lienzo_cambio();
    }

    Vector2 _gl_scroll() const { return _canvas_pos; }

    void set_ScrollingEnabled(bool b) {
        _scrolling_enabled = b;
        // El filtro de raton TIENE que ser STOP para que Godot entregue la rueda a
        // _gui_input. Sin esto el handler de scroll existia pero no lo llamaba
        // nadie: el contenido no se movia por mucho que sobrara canvas.
        set_mouse_filter(b ? Control::MOUSE_FILTER_STOP : Control::MOUSE_FILTER_PASS);
    }
    bool get_ScrollingEnabled() const { return _scrolling_enabled; }
    void set_gl_canvas_size(float xs, float xo, float ys, float yo) { _canvas_size = {xs, xo, ys, yo}; _gl_lienzo_cambio(); }
    void set_CanvasPosition(Vector2 v) {
        const Vector2 mx = _max_scroll();
        _canvas_pos = Vector2(Math::clamp(v.x, 0.0f, mx.x), Math::clamp(v.y, 0.0f, mx.y));
        gl_gui_relayout_children(this);
        _gl_pintar_barra();
    }
    Vector2 get_CanvasPosition() const { return _canvas_pos; }
    void set_CanvasSize(Vector4 v) { _canvas_size = {v.x, v.y, v.z, v.w}; _gl_lienzo_cambio(); }
    Vector4 get_CanvasSize() const {
        return Vector4(_canvas_size.xs, _canvas_size.xo, _canvas_size.ys, _canvas_size.yo);
    }
    void set_ScrollBarThickness(int v) { _bar_thickness = v; _gl_lienzo_cambio(); }
    int  get_ScrollBarThickness() const { return _bar_thickness; }
    void set_ScrollBarImageColor3(Color c) { _bar_color = c; _gl_pintar_barra(); }
    Color get_ScrollBarImageColor3() const { return _bar_color; }
    void set_ScrollBarImageTransparency(float v) { _bar_alpha = 1.0f - Math::clamp(v, 0.0f, 1.0f); _gl_pintar_barra(); }
    float get_ScrollBarImageTransparency() const { return 1.0f - _bar_alpha; }
    void set_ScrollingDirection(int v) { _scroll_dir = v; _gl_lienzo_cambio(); }
    int  get_ScrollingDirection() const { return _scroll_dir; }
    void set_AutomaticCanvasSize(int v) { _auto_canvas = v; _gl_lienzo_cambio(); }
    int  get_AutomaticCanvasSize() const { return _auto_canvas; }
    void set_ElasticBehavior(int v) { _elastic = v; }
    int  get_ElasticBehavior() const { return _elastic; }
    void set_VerticalScrollBarInset(int v) { _v_inset = v; _gl_lienzo_cambio(); }
    int  get_VerticalScrollBarInset() const { return _v_inset; }
    void set_HorizontalScrollBarInset(int v) { _h_inset = v; _gl_lienzo_cambio(); }
    int  get_HorizontalScrollBarInset() const { return _h_inset; }
    void set_VerticalScrollBarPosition(int v) { _v_bar_pos = v; _gl_pintar_barra(); }
    int  get_VerticalScrollBarPosition() const { return _v_bar_pos; }
    void set_TopImage(String v) { _top_image = v; }
    String get_TopImage() const { return _top_image; }
    void set_MidImage(String v) { _mid_image = v; }
    String get_MidImage() const { return _mid_image; }
    void set_BottomImage(String v) { _bottom_image = v; }
    String get_BottomImage() const { return _bottom_image; }

    void _gui_input(const Ref<InputEvent>& event) override {
        if (!_scrolling_enabled) return;
        Ref<InputEventMouseButton> mb = event;
        if (mb.is_null() || !mb->is_pressed()) return;
        const float step = 40.0f;
        Vector2 next = _canvas_pos;
        const bool can_x = (_scroll_dir == 1 || _scroll_dir == 4);
        const bool can_y = (_scroll_dir == 2 || _scroll_dir == 4);
        switch (mb->get_button_index()) {
            case MOUSE_BUTTON_WHEEL_UP:    if (can_y) next.y -= step; break;
            case MOUSE_BUTTON_WHEEL_DOWN:  if (can_y) next.y += step; break;
            case MOUSE_BUTTON_WHEEL_LEFT:  if (can_x) next.x -= step; break;
            case MOUSE_BUTTON_WHEEL_RIGHT: if (can_x) next.x += step; break;
            default: return;
        }
        if (next == _canvas_pos) return;
        set_CanvasPosition(next);
        accept_event();
    }
};

// ════════════════════════════════════════════════════════════════════
//  TextService — medir texto antes de dibujarlo (1.17)
//
//  GetTextSize(texto, tamaño, fuente, marco) → Vector2
//  GetTextBoundsAsync(params)                → Vector2
//
//  Es de lo primero que usa cualquier UI que se ajusta al contenido (listas
//  de modificadores, descripciones de items...). Faltaba entero, asi que
//  game:GetService("TextService") devolvia nil y el modulo entero moria.
//  La medida sale de la MISMA fuente con la que se dibujan los TextLabel,
//  asi que el ancho que devuelve es el de verdad.
// ════════════════════════════════════════════════════════════════════
class TextService : public Node {
    GDCLASS(TextService, Node);
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("MeasureText", "text", "size", "wrap_width"), &TextService::measure_text);
    }
public:
    Vector2 measure_text(const String& text, int size, float wrap_width) const {
        return measure_text_font(text, size, wrap_width, Ref<Font>());
    }
    // Con la fuente que se pide (la de Enum.Font o Font): medir con otra daba
    // cajas de otro tamano que las que luego se dibujan. size es el TextSize de
    // Roblox (alto de LINEA): se mide con el font_size de Godot que da esa linea,
    // como al dibujar (gl_tam_godot_de), y el alto sale como en Roblox: lineas x
    // TextSize (x 1,5 en Legacy), que es lo que esperan las UIs que se ajustan.
    Vector2 measure_text_font(const String& text, int size, float wrap_width, Ref<Font> f, bool legacy = false) const {
        if (size <= 0) size = 14;
        if (f.is_null()) if (ThemeDB* db = ThemeDB::get_singleton()) f = db->get_fallback_font();
        const float linea = gl_linea_roblox((float)size, legacy);
        if (f.is_null()) {
            // Sin fuente (arranque muy temprano): aproximacion, mejor que nada.
            return Vector2((float)text.length() * linea * 0.5f, linea);
        }
        const int fs = gl_tam_godot_de(f, legacy, (float)size);
        // wrap_width <= 0 → una sola linea sin ajuste
        const float w = (wrap_width > 0.0f) ? wrap_width : -1.0f;
        const Vector2 m = f->get_multiline_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, w, fs);
        const int lineas = MAX(1, (int)Math::round(m.y / MAX(1.0f, (float)f->get_height(fs))));
        return Vector2(m.x, (float)lineas * linea);
    }
};

#endif // ROBLOX_GUI_H
