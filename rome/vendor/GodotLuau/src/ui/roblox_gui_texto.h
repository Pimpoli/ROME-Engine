#ifndef ROBLOX_GUI_TEXTO_H
#define ROBLOX_GUI_TEXTO_H
#include "gl_cast.h"

// GUI con texto: TextLabel, TextButton y TextBox, y sus macros de texto.
// Va aparte de roblox_gui.h (pasaba de 2.000 lineas) y se incluye desde alli:
// usa sus includes, GuiLuaCallback y las macros GUI_COMMON_*.

// ────────────────────────────────────────────────────────────────────
//  Macros de TEXTO: Text/TextColor3/TextSize/TextScaled/RichText/…
// ────────────────────────────────────────────────────────────────────
// Color 27,42,53 y tamano 8: lo que trae un texto nuevo en Roblox (Studio, 08/10).
#define GUI_TEXT_FIELDS                       \
    float _txt_r = 27.0f / 255.0f, _txt_g = 42.0f / 255.0f, _txt_b = 53.0f / 255.0f; \
    float _txt_alpha = 1;                     \
    bool  _text_scaled = false;               \
    bool  _rich_text = false;                 \
    int   _text_size = 8;                     \
    String _raw_text;                         \
    Color _stroke_col = Color(0, 0, 0);       \
    float _stroke_t = 1;                      \
    int   _tx_align = 2;   /* Center, como Roblox */ \
    int   _ty_align = 1;   /* Center */       \
    bool  _wrapped = false;                   \
    int   _truncate = 0;                      \
    float _line_height = 1;                   \
    int   _max_graphemes = -1;                \
    int   _text_dir = 0;   /* Enum.TextDirection: Auto=0, LeftToRight=1, RightToLeft=2 */ \
    uint64_t _refit_firma = 0;   /* texto+caja+fuente del ultimo ajuste de TextScaled */ \
    bool  _refit_pendiente = false;   /* oculta: se ajusta al verse (o al medirla) */ \
    bool  _refit_forzado = false;

// TextScaled de Roblox: la letra crece/encoge para llenar el rect. Se estima el
// tamaño de una medida y se refina con otra (dos medidas por elemento, no un
// barrido de 1..100: hay 1988 TextLabel en un place real).
// En el constructor: el texto, el tamano y el color de un texto nuevo de Roblox
// ("Label", "Button" o "TextBox"). Lo importado y lo clonado los pisa despues.
#define GUI_TEXT_DEFAULTS(texto)                                               \
    _raw_text = texto;                                                         \
    set_text(texto);                                                           \
    add_theme_font_size_override("font_size", _text_size);                     \
    add_theme_color_override("font_color", Color(_txt_r, _txt_g, _txt_b, _txt_alpha));

#define GUI_TEXT_METHODS(ClassName)                                            \
    /* Todas las props de texto de Roblox, como propiedades reales: el clon \
       de StarterGui a PlayerGui las copia y el HUD sale igual. */ \
    void set_TextColor3(Color c) { set_text_color(c.r, c.g, c.b); } \
    Color get_TextColor3() const { return Color(_txt_r, _txt_g, _txt_b); } \
    void set_TextTransparency(float v) { \
        _txt_alpha = 1.0f - Math::clamp(v, 0.0f, 1.0f); \
        gl_tema_color(this, "font_color", Color(_txt_r, _txt_g, _txt_b, _txt_alpha)); \
    } \
    float get_TextTransparency() const { return 1.0f - _txt_alpha; } \
    void set_TextStrokeColor3(Color c) { \
        _stroke_col = c; \
        gl_tema_color(this, "font_outline_color", c); \
    } \
    Color get_TextStrokeColor3() const { return _stroke_col; } \
    void set_TextStrokeTransparency(float v) { \
        _stroke_t = v; \
        gl_apply_rbx_gui_extra(this, "TextStrokeTransparency", v); \
    } \
    float get_TextStrokeTransparency() const { return _stroke_t; } \
    void set_TextXAlignment(int v) { \
        _tx_align = v; \
        gl_apply_rbx_gui_extra(this, "TextXAlignment", (int64_t)v); \
    } \
    int  get_TextXAlignment() const { return _tx_align; } \
    void set_TextYAlignment(int v) { \
        _ty_align = v; \
        gl_apply_rbx_gui_extra(this, "TextYAlignment", (int64_t)v); \
    } \
    int  get_TextYAlignment() const { return _ty_align; } \
    void set_TextWrapped(bool b) { \
        _wrapped = b; \
        gl_apply_rbx_gui_extra(this, "TextWrapped", b); \
    } \
    bool get_TextWrapped() const { return _wrapped; } \
    void set_TextTruncate(int v) { \
        _truncate = v; \
        gl_apply_rbx_gui_extra(this, "TextTruncate", (int64_t)v); \
    } \
    int  get_TextTruncate() const { return _truncate; } \
    void set_LineHeight(float v) { \
        _line_height = v; \
        gl_apply_rbx_gui_extra(this, "LineHeight", v); \
    } \
    float get_LineHeight() const { return _line_height; } \
    void set_MaxVisibleGraphemes(int v) { \
        _max_graphemes = v; \
        gl_apply_rbx_gui_extra(this, "MaxVisibleGraphemes", (int64_t)v); \
    } \
    int  get_MaxVisibleGraphemes() const { return _max_graphemes; } \
    /* Los mismos numeros que Control::TextDirection de Godot. */ \
    void set_TextDirection(int v) { _text_dir = CLAMP(v, 0, 2); set_text_direction((Control::TextDirection)_text_dir); } \
    int  get_TextDirection() const { return _text_dir; } \
    void set_text_color(float r, float g, float b) {                           \
        _txt_r = r; _txt_g = g; _txt_b = b;                                    \
        gl_tema_color(this, "font_color", Color(r, g, b, _txt_alpha));     \
    }                                                                          \
    /* TextSize es el alto de LINEA de Roblox, no el em de Godot (gl_tam_godot). \
       Fuera del arbol solo se guarda: ENTER_TREE lo pone con la fuente ya     \
       puesta (gl_aplicar_fuente), que es la que dice cuanto mide la linea. */  \
    void set_text_size(int s) {                                                \
        _text_size = s;                                                        \
        _refit_firma = 0;                                                      \
        if (!is_inside_tree()) return;                                         \
        if (!_text_scaled) gl_tema_tam(this, "font_size", gl_tam_godot(this, (float)s)); \
        else _gl_refit_text();                                                 \
    }                                                                          \
    int  get_text_size() const { return _text_size; }                          \
    void set_text_scaled(bool b) {                                            \
        _text_scaled = b;                                                     \
        _refit_firma = 0;   /* el tamano puesto ya no es el del ajuste */      \
        /* Con TextScaled Roblox envuelve siempre; sin activar el autowrap    \
           del Label la medida multilinea y el dibujo no coincidirian. */     \
        if (Label* _l = gl_cast<Label>(this))                         \
            _l->set_autowrap_mode(b ? TextServer::AUTOWRAP_WORD_SMART         \
                                    : (_wrapped ? TextServer::AUTOWRAP_WORD_SMART\
                                                : TextServer::AUTOWRAP_OFF)); \
        if (b) _gl_refit_text();                                              \
        else if (is_inside_tree()) gl_tema_tam(this, "font_size", gl_tam_godot(this, (float)_text_size)); \
    }                                                                         \
    bool get_text_scaled() const { return _text_scaled; }                      \
    void set_rich_text(bool b) {                                               \
        _rich_text = b;                                                        \
        _gl_push_text();                                                       \
    }                                                                          \
    bool get_rich_text() const { return _rich_text; }                          \
    void set_rbx_text(String t) { _raw_text = t; _gl_push_text(); }             \
    String get_rbx_text() const { return _raw_text; }                          \
    /* La lectura de TextBounds/TextFits: si quedo pendiente, ajustar ya. */   \
    void _gl_refit_si_pendiente() {                                            \
        if (!_refit_pendiente) return;                                         \
        _refit_forzado = true;                                                 \
        _gl_refit_text();                                                      \
        _refit_forzado = false;                                                \
    }                                                                          \
    void _gl_refit_text() {                                                    \
        if (!_text_scaled) return;                                             \
        /* Oculta (un panel cerrado, una plantilla): se ajusta al verse        \
           (VISIBILITY_CHANGED) o al medirla. Al entrar al arbol casi toda la  \
           UI de un juego esta oculta, y medir el texto era lo que mas costaba \
           (unos 80 us por TextLabel en la carga de Build a Mountain). */      \
        if (!_refit_forzado && is_inside_tree() && !is_visible_in_tree()) { _refit_pendiente = true; return; } \
        _refit_pendiente = false;                                              \
        const Vector2 box = get_size();                                        \
        if (box.x < 2.0f || box.y < 2.0f) return;                              \
        Ref<Font> f = get_theme_font("font");                                   \
        if (f.is_null()) return;                                               \
        /* Los topes son TextSize de Roblox (100 sin constraint): en tamano de \
           Godot, con el alto de linea de la fuente (gl_tam_godot). Por debajo \
           de 6 px el glifo sale vacio. */                                     \
        int mn = 6, mx = gl_tam_godot(this, 100.0f);                           \
        for (int i = 0; i < get_child_count(); i++) {                          \
            Node* ch = get_child(i);                                           \
            if (ch && ch->is_class("UITextSizeConstraint")) {                  \
                mn = gl_tam_godot(this, (float)(int64_t)ch->get(StringName("MinTextSize"))); \
                mx = gl_tam_godot(this, (float)(int64_t)ch->get(StringName("MaxTextSize"))); \
                break;                                                        \
            }                                                                 \
        }                                                                     \
        String t = _gl_display_text();                                        \
        if (t.is_empty()) return;                                             \
        /* Mismo texto, caja, fuente y topes que la ultima vez: mismo tamano. \
           Los contadores que reescriben su Text cada frame no miden nada.  */\
        uint64_t firma = (uint64_t)t.hash();                                  \
        firma = (firma * 1099511628211ULL) ^ (uint64_t)(int64_t)(box.x * 4.0f); \
        firma = (firma * 1099511628211ULL) ^ (uint64_t)(int64_t)(box.y * 4.0f); \
        firma = (firma * 1099511628211ULL) ^ (uint64_t)f->get_instance_id();  \
        firma = (firma * 1099511628211ULL) ^ (uint64_t)(int64_t)(mn * 4096 + mx); \
        if (firma == _refit_firma && has_theme_font_size_override("font_size")) return; \
        _refit_firma = firma;                                                 \
        /* Roblox SIEMPRE envuelve con TextScaled (ignora TextWrapped): el    \
           mayor tamano que cabe midiendo MULTILINEA con el ancho de la caja.  \
           La busqueda (unas 3 medidas) la comparte con los carteles del      \
           mundo: gl_texto_escalado en gl_fuentes.h. */                       \
        gl_tema_tam(this, "font_size", gl_texto_escalado(f, t, box, mn, mx)); \
    }

#define GUI_BIND_TEXT(C)                                                            \
    ClassDB::bind_method(D_METHOD("set_text_color","r","g","b"), &C::set_text_color); \
    ClassDB::bind_method(D_METHOD("set_text_size","s"),          &C::set_text_size);  \
    ClassDB::bind_method(D_METHOD("get_text_size"),              &C::get_text_size);  \
    ClassDB::bind_method(D_METHOD("set_text_scaled","b"),        &C::set_text_scaled);\
    ClassDB::bind_method(D_METHOD("get_text_scaled"),            &C::get_text_scaled);\
    ClassDB::bind_method(D_METHOD("set_rich_text","b"),          &C::set_rich_text);  \
    ClassDB::bind_method(D_METHOD("get_rich_text"),              &C::get_rich_text);  \
    ClassDB::bind_method(D_METHOD("set_rbx_text","t"),           &C::set_rbx_text);   \
    ClassDB::bind_method(D_METHOD("get_rbx_text"),               &C::get_rbx_text);   \
    ClassDB::bind_method(D_METHOD("_gl_refit_text"),             &C::_gl_refit_text); \
    ClassDB::bind_method(D_METHOD("_gl_refit_si_pendiente"),     &C::_gl_refit_si_pendiente); \
    ClassDB::bind_method(D_METHOD("set_TextColor3","v"), &C::set_TextColor3); \
    ClassDB::bind_method(D_METHOD("get_TextColor3"),     &C::get_TextColor3); \
    ClassDB::bind_method(D_METHOD("set_TextTransparency","v"), &C::set_TextTransparency); \
    ClassDB::bind_method(D_METHOD("get_TextTransparency"),     &C::get_TextTransparency); \
    ClassDB::bind_method(D_METHOD("set_TextStrokeColor3","v"), &C::set_TextStrokeColor3); \
    ClassDB::bind_method(D_METHOD("get_TextStrokeColor3"),     &C::get_TextStrokeColor3); \
    ClassDB::bind_method(D_METHOD("set_TextStrokeTransparency","v"), &C::set_TextStrokeTransparency); \
    ClassDB::bind_method(D_METHOD("get_TextStrokeTransparency"),     &C::get_TextStrokeTransparency); \
    ClassDB::bind_method(D_METHOD("set_TextXAlignment","v"), &C::set_TextXAlignment); \
    ClassDB::bind_method(D_METHOD("get_TextXAlignment"),     &C::get_TextXAlignment); \
    ClassDB::bind_method(D_METHOD("set_TextYAlignment","v"), &C::set_TextYAlignment); \
    ClassDB::bind_method(D_METHOD("get_TextYAlignment"),     &C::get_TextYAlignment); \
    ClassDB::bind_method(D_METHOD("set_TextWrapped","v"), &C::set_TextWrapped); \
    ClassDB::bind_method(D_METHOD("get_TextWrapped"),     &C::get_TextWrapped); \
    ClassDB::bind_method(D_METHOD("set_TextTruncate","v"), &C::set_TextTruncate); \
    ClassDB::bind_method(D_METHOD("get_TextTruncate"),     &C::get_TextTruncate); \
    ClassDB::bind_method(D_METHOD("set_LineHeight","v"), &C::set_LineHeight); \
    ClassDB::bind_method(D_METHOD("get_LineHeight"),     &C::get_LineHeight); \
    ClassDB::bind_method(D_METHOD("set_MaxVisibleGraphemes","v"), &C::set_MaxVisibleGraphemes); \
    ClassDB::bind_method(D_METHOD("get_MaxVisibleGraphemes"),     &C::get_MaxVisibleGraphemes); \
    ClassDB::bind_method(D_METHOD("set_TextDirection","v"), &C::set_TextDirection); \
    ClassDB::bind_method(D_METHOD("get_TextDirection"),     &C::get_TextDirection); \
    /* "Text" NO estaba registrada como propiedad: solo existia el metodo. Se */ \
    /* podia escribir desde Luau (el puente llama a set_rbx_text por nombre)  */ \
    /* pero NO desde el inspector de Godot ni con node.set("Text", ...), y    */ \
    /* leerla devolvia null. Va la PRIMERA del grupo, como en Roblox.         */ \
    ADD_PROPERTY(PropertyInfo(Variant::STRING,"Text", PROPERTY_HINT_MULTILINE_TEXT), "set_rbx_text","get_rbx_text"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,  "TextSize"),   "set_text_size","get_text_size"); \
    ADD_PROPERTY(PropertyInfo(Variant::COLOR,"TextColor3"),       "set_TextColor3","get_TextColor3"); \
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"TextTransparency"), "set_TextTransparency","get_TextTransparency"); \
    ADD_PROPERTY(PropertyInfo(Variant::COLOR,"TextStrokeColor3"), "set_TextStrokeColor3","get_TextStrokeColor3"); \
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"TextStrokeTransparency"), "set_TextStrokeTransparency","get_TextStrokeTransparency"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,  "TextXAlignment", PROPERTY_HINT_ENUM,"Left,Right,Center"), "set_TextXAlignment","get_TextXAlignment"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,  "TextYAlignment", PROPERTY_HINT_ENUM,"Top,Center,Bottom"), "set_TextYAlignment","get_TextYAlignment"); \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "TextWrapped"),      "set_TextWrapped","get_TextWrapped"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,  "TextTruncate", PROPERTY_HINT_ENUM,"None,AtEnd,SplitWord"), "set_TextTruncate","get_TextTruncate"); \
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"LineHeight"),       "set_LineHeight","get_LineHeight"); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,  "MaxVisibleGraphemes"), "set_MaxVisibleGraphemes","get_MaxVisibleGraphemes");   \
    ADD_PROPERTY(PropertyInfo(Variant::INT,  "TextDirection"), "set_TextDirection","get_TextDirection"); \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "TextScaled"), "set_text_scaled","get_text_scaled"); \
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "RichText"),   "set_rich_text","get_rich_text");

// ────────────────────────────────────────────────────────────────────
//  RobloxTextLabel — Static text
////  RobloxTextLabel — Texto estático
// ────────────────────────────────────────────────────────────────────
class RobloxTextLabel : public Label {
    GDCLASS(RobloxTextLabel, Label);
    GUI_COMMON_FIELDS
    GUI_HOVER_FIELDS
    GUI_TEXT_FIELDS

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
        gl_tema_caja(this, gl_claves_caja()[1], gl_estilo_compartido(
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
            // clip_text: sin esto el mínimo del Label es el ancho del texto, que
            // clampaba el tamaño UDim2 y descuadraba el anchor.
            set_clip_text(true);
            GUI_COMMON_APPLY_LAYOUT
            _apply_style();
            gl_tema_color(this, "font_color", Color(_txt_r, _txt_g, _txt_b, _txt_alpha));
            gl_aplicar_fuente(this);   // fuente y tamano (con TextScaled, el ajuste)
            _gl_gui_wire_relayout(this, "_on_parent_resized");
            GUI_HOVER_WIRE
        }
        // El ajuste de TextScaled que quedo pendiente mientras estaba oculta.
        if (p_what == NOTIFICATION_VISIBILITY_CHANGED && _refit_pendiente && is_visible_in_tree()) _gl_refit_text();
        // RichText: la capa copia color, fuente y caja del Label.
        if (_rich_text && (p_what == NOTIFICATION_THEME_CHANGED || p_what == NOTIFICATION_RESIZED))
            gl_rich_sincronizar(this, _rich_text, _raw_text);
    }

protected:
    static void _bind_methods() {
        GUI_BIND_COMMON(RobloxTextLabel)
        GUI_HOVER_BIND(RobloxTextLabel)
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &RobloxTextLabel::_gl_disconnect);
        GUI_BIND_TEXT(RobloxTextLabel)
    }

public:
    GUI_REWIRE_MOUSE_PASIVO
    GUI_COMMON_METHODS(RobloxTextLabel)
    GUI_HOVER_METHODS
    void _gl_disconnect(int ref, int64_t vm) { _gl_hover_disconnect(ref, vm); }
    GUI_TEXT_METHODS(RobloxTextLabel)

    // El default de Roblox para TextLabel/TextButton es Center/Center, y los
    // campos _tx_align/_ty_align ya lo decian — pero el Label de Godot nace
    // Left/Top y nadie los aplicaba: todo texto creado por script salia pegado
    // arriba a la izquierda (lo importado se salva porque el .rbxl serializa la
    // alineacion explicita, que se aplica despues y manda).
    RobloxTextLabel() {
        set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
        set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
        GUI_TEXT_DEFAULTS("Label")
    }

    String _gl_display_text() const { return get_text(); }
    void _gl_push_text() {
        set_text(gl_rich_texto_base(this, _rich_text, _raw_text));
        _gl_refit_text();
        gl_rich_sincronizar(this, _rich_text, _raw_text);
        // Si esta etiqueta vive dentro de un cartel del mundo, hay que rehacer
        // su reflejo 3D: la propiedad cambia pero el Label3D no se entera solo.
        gl_world_gui_avisar_cambio(this);
    }

    void _on_parent_resized() {
        GUI_COMMON_APPLY_LAYOUT
        if (_auto_size != GL_AUTO_NONE) {
            Vector2 want = get_minimum_size();
            Ref<Font> f = get_theme_font("font");
            if (f.is_valid())
                want = f->get_string_size(get_text(), HORIZONTAL_ALIGNMENT_LEFT, -1,
                                          get_theme_font_size("font_size"));
            GUI_APPLY_AUTO_SIZE(want)
        }
        _gl_refit_text();
    }
};

// ────────────────────────────────────────────────────────────────────
//  RobloxTextButton — Text button with Roblox-style signals
////  RobloxTextButton — Botón con texto, señales tipo Roblox
// ────────────────────────────────────────────────────────────────────
class RobloxTextButton : public Button {
    GDCLASS(RobloxTextButton, Button);
    GUI_COMMON_FIELDS
    GUI_TEXT_FIELDS
    bool _auto_button_color = true;
    GUI_BOTON_EXTRA_FIELDS

    std::vector<GuiLuaCallback> _click_cbs;
    // Down y Up tienen su PROPIA lista: en Roblox son momentos distintos
    // (apretar y soltar), no el mismo instante. Ver _on_button_down.
    std::vector<GuiLuaCallback> _down_cbs;
    std::vector<GuiLuaCallback> _up_cbs;
    std::vector<GuiLuaCallback> _enter_cbs;
    std::vector<GuiLuaCallback> _leave_cbs;

    void _apply_style() {
        // Fuera del arbol NO se toca el tema: cada set de una propiedad de estilo
        // creaba un StyleBoxFlat que se tiraba enseguida. Al DUPLICAR un nodo,
        // Godot copia todas sus propiedades una a una, asi que un solo TextLabel
        // clonado creaba media docena de estilos para nada. ENTER_TREE lo aplica
        // una vez, que es cuando de verdad se ve.
        if (!is_inside_tree()) return;
        const float d = _auto_button_color ? 0.1f : 0.0f;
        // Un boton gastaba TRES cajas propias. Con la cache, todos los botones
        // que se ven igual comparten las mismas tres.
        const Color borde(_border_r, _border_g, _border_b, 1.0f - _bg_alpha);
        const int px = gl_borde_px(this, _border_px);
        const StringName* k = gl_claves_caja();
        const StringName claves[3] = { k[1], k[2], k[3] };   // normal, hover, pressed
        const Ref<StyleBox> cajas[3] = {
            gl_estilo_compartido(Color(_bg_r, _bg_g, _bg_b, 1.0f - _bg_alpha), borde, px),
            gl_estilo_compartido(Color(Math::clamp(_bg_r + d, 0.f, 1.f),
                                       Math::clamp(_bg_g + d, 0.f, 1.f),
                                       Math::clamp(_bg_b + d, 0.f, 1.f), 1.0f - _bg_alpha), borde, px),
            // La de "pressed" nunca fijaba color de borde, o sea que se quedaba con
            // el que trae StyleBoxFlat de fabrica. Ese valor NO se escribe a mano
            // aqui: se lee del propio Godot una vez (gl_estilo_borde_defecto), asi
            // el boton pulsado sigue viendose exactamente igual que antes.
            gl_estilo_compartido(Color(Math::clamp(_bg_r - d, 0.f, 1.f),
                                       Math::clamp(_bg_g - d, 0.f, 1.f),
                                       Math::clamp(_bg_b - d, 0.f, 1.f), 1.0f - _bg_alpha),
                                 gl_estilo_borde_defecto(), px),
        };
        gl_tema_cajas(this, claves, cajas, 3);
        _gl_gui_reapply_modifiers(this);
    }

protected:
    void _notification(int p_what) {
        GUI_Z_NOTIFICATION
        if (p_what == NOTIFICATION_ENTER_TREE) {
            gl_apply_rbx_gui_meta(this);   // props importadas guardadas como meta
            gl_gui_apply_render_gate(this);  // regla de Roblox: solo dentro de un *Gui
            GUI_COMMON_APPLY_LAYOUT
            _apply_style();
            // EN ROBLOX UN BOTON NO SE ACTIVA CON EL TECLADO. Aqui heredan de
            // Button de Godot, que nace con FOCUS_ALL, y la accion ui_accept de
            // Godot (Space Y Enter por defecto) emite 'pressed' sobre el control
            // ENFOCADO — y 'pressed' lo convertimos en MouseButton1Click. O sea
            // que tras pulsar el boton del logo una vez, ese boton se quedaba
            // con el foco y CADA Space (que ademas es la tecla de saltar) volvia
            // a abrir el menu. Antes esto miraba _selectable, pero los botones
            // que vienen de un .rbxl traen Selectable = true (es el valor por
            // defecto en Roblox) y volvian a coger el foco. Selectable es la
            // navegacion con MANDO, no el foco de teclado: NUNCA se enfoca.
            set_focus_mode(Control::FOCUS_NONE);
            set_clip_text(true);   // el box vale su Size, no el ancho del texto
            gl_tema_color(this, "font_color", Color(_txt_r, _txt_g, _txt_b, _txt_alpha));
            gl_aplicar_fuente(this);   // fuente y tamano (con TextScaled, el ajuste)
            if (!is_connected("pressed", Callable(this, "_on_pressed"))) {
                connect("pressed",      Callable(this, "_on_pressed"));
                // MANTENER PULSADO: en Roblox, Down salta al APRETAR y Up al
                // SOLTAR. Godot tiene esas dos por separado (button_down /
                // button_up); "pressed" es el clic completo.
                connect("button_down",  Callable(this, "_on_button_down"));
                connect("button_up",    Callable(this, "_on_button_up"));
                // (hover: lo lleva gl_hover_tick por geometria, ver add_enter_cb)
            }
            _gl_gui_wire_relayout(this, "_on_parent_resized");
        }
        // El ajuste de TextScaled que quedo pendiente mientras estaba oculto.
        if (p_what == NOTIFICATION_VISIBILITY_CHANGED && _refit_pendiente && is_visible_in_tree()) _gl_refit_text();
        // RichText: la capa copia color, fuente y caja del boton.
        if (_rich_text && (p_what == NOTIFICATION_THEME_CHANGED || p_what == NOTIFICATION_RESIZED))
            gl_rich_sincronizar(this, _rich_text, _raw_text);
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &RobloxTextButton::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("_on_pressed"),        &RobloxTextButton::_on_pressed);
        ClassDB::bind_method(D_METHOD("_on_button_down"),    &RobloxTextButton::_on_button_down);
        ClassDB::bind_method(D_METHOD("_on_button_up"),      &RobloxTextButton::_on_button_up);
        ClassDB::bind_method(D_METHOD("_on_mouse_enter"),    &RobloxTextButton::_on_mouse_enter);
        ClassDB::bind_method(D_METHOD("_on_mouse_leave"),    &RobloxTextButton::_on_mouse_leave);
        ClassDB::bind_method(D_METHOD("set_AutoButtonColor","b"), &RobloxTextButton::set_AutoButtonColor);
        ClassDB::bind_method(D_METHOD("get_AutoButtonColor"),     &RobloxTextButton::get_AutoButtonColor);
        GUI_BIND_COMMON(RobloxTextButton)
        GUI_BIND_TEXT(RobloxTextButton)
        GUI_BOTON_EXTRA_BIND(RobloxTextButton)
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,"AutoButtonColor"), "set_AutoButtonColor","get_AutoButtonColor");
        ADD_SIGNAL(MethodInfo("MouseButton1Click"));
        ADD_SIGNAL(MethodInfo("Activated"));   // Roblox GuiButton.Activated
        ADD_SIGNAL(MethodInfo("MouseButton1Down"));
        ADD_SIGNAL(MethodInfo("MouseButton1Up"));
        ADD_SIGNAL(MethodInfo("MouseEnter"));
        ADD_SIGNAL(MethodInfo("MouseLeave"));
    }

public:
    GUI_REWIRE_MOUSE_BOTON
    GUI_BOTON_NO_TECLADO
    GUI_COMMON_METHODS(RobloxTextButton)
    GUI_TEXT_METHODS(RobloxTextButton)
    GUI_BOTON_EXTRA_METHODS

    // Un TextButton nuevo de Roblox es Active y Selectable y pone "Button".
    RobloxTextButton() {
        _active = true;
        _selectable = true;
        GUI_TEXT_DEFAULTS("Button")
    }

    // Con RichText el texto propio del boton va vacio (lo pinta la capa): el
    // cartel 3D y las medidas leen el texto plano.
    String _gl_display_text() const { return _rich_text ? gl_rich_entidades(_gl_strip_rich_tags(_raw_text)) : get_text(); }
    void _gl_push_text() {
        set_text(gl_rich_texto_base(this, _rich_text, _raw_text));
        _gl_refit_text();
        gl_rich_sincronizar(this, _rich_text, _raw_text);
        // Si esta etiqueta vive dentro de un cartel del mundo, hay que rehacer
        // su reflejo 3D: la propiedad cambia pero el Label3D no se entera solo.
        gl_world_gui_avisar_cambio(this);
    }
    void set_AutoButtonColor(bool b) { _auto_button_color = b; _apply_style(); }
    bool get_AutoButtonColor() const { return _auto_button_color; }

    void _on_parent_resized() {
        GUI_COMMON_APPLY_LAYOUT
        GUI_APPLY_AUTO_SIZE(get_minimum_size())
        _gl_refit_text();
    }
    // ── LOS CUATRO EVENTOS DEL BOTON, POR SEPARADO ────────────────────
    // Antes los cuatro salian DE GOLPE en el clic: Down, Click, Activated y Up
    // seguidos, en el mismo instante. Un juego que hace "Down -> empieza a
    // comprar en bucle" y "Up -> para" compraba UNA vez y paraba sola: mantener
    // pulsado no existia. En Roblox son tres momentos distintos.
    void _on_button_down()    { emit_signal("MouseButton1Down");  _fire_gui_cbs(_down_cbs); _fire_gui_cbs_input(_input_began_cbs, true); }
    void _on_button_up()      { emit_signal("MouseButton1Up");    _fire_gui_cbs(_up_cbs);   _fire_gui_cbs_input(_input_ended_cbs, false); }
    void _on_pressed()        { emit_signal("MouseButton1Click"); emit_signal("Activated"); _fire_gui_cbs(_click_cbs); }
    void _on_mouse_enter()    { emit_signal("MouseEnter");        _fire_gui_cbs(_enter_cbs); }
    void _on_mouse_leave()    { emit_signal("MouseLeave");        _fire_gui_cbs(_leave_cbs); }

    std::vector<GuiLuaCallback> _input_began_cbs;
    std::vector<GuiLuaCallback> _input_ended_cbs;
    void add_input_began_cb(lua_State* L, int ref) { _input_began_cbs.push_back({L, ref, true}); }
    void add_input_ended_cb(lua_State* L, int ref) { _input_ended_cbs.push_back({L, ref, true}); }

    void add_click_cb(lua_State* L, int ref)  { _click_cbs.push_back({L, ref, true}); }
    void add_down_cb(lua_State* L, int ref)   { _down_cbs.push_back({L, ref, true}); }
    void add_up_cb(lua_State* L, int ref)     { _up_cbs.push_back({L, ref, true}); }
    // gl_hover_registrar: los botones se quedaron FUERA del hover central y su
    // MouseEnter/Leave dependia del mouse_entered de Godot, que solo se
    // reevalua cuando llega un evento de MOVIMIENTO: un boton que aparece,
    // se desplaza o ESCALA bajo el cursor quieto no recibia el evento hasta
    // mover el raton (y cualquier Control con Active=true encima se lo comia).
    void add_enter_cb(lua_State* L, int ref)  { _enter_cbs.push_back({L, ref, true}); gl_hover_registrar(this); }
    void add_leave_cb(lua_State* L, int ref)  { _leave_cbs.push_back({L, ref, true}); gl_hover_registrar(this); }
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& cb : _click_cbs) if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : _down_cbs)  if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : _up_cbs)    if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : _enter_cbs) if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : _leave_cbs) if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : _input_began_cbs) if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : _input_ended_cbs) if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
    }
};


// ────────────────────────────────────────────────────────────────────
//  RobloxTextBox — Text input field
////  RobloxTextBox — Campo de entrada de texto
// ────────────────────────────────────────────────────────────────────
class RobloxTextBox : public LineEdit {
    GDCLASS(RobloxTextBox, LineEdit);
    GUI_COMMON_FIELDS
    GUI_TEXT_FIELDS
    Color _placeholder_color = Color(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);   // como Roblox
    bool  _clear_on_focus = true;
    bool  _multi_line = false;
    bool  _text_editable = true;
    bool  _show_native_input = true;   // ShowNativeInput (teclado del movil): se guarda
    bool  _enter_pulsado = false;   // el proximo FocusLost lleva enterPressed = true

    std::vector<GuiLuaCallback> _focus_lost_cbs;
    std::vector<GuiLuaCallback> _focus_gained_cbs;
    std::vector<GuiLuaCallback> _changed_cbs;

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
        gl_tema_caja(this, gl_claves_caja()[1], gl_estilo_compartido(
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
            GUI_COMMON_APPLY_LAYOUT
            _apply_style();
            gl_tema_color(this, "font_color", Color(_txt_r, _txt_g, _txt_b, _txt_alpha));
            gl_aplicar_fuente(this);
            if (!is_connected("focus_exited", Callable(this, "_on_focus_lost"))) {
                connect("focus_exited",  Callable(this, "_on_focus_lost"));
                connect("focus_entered", Callable(this, "_on_focus_gained"));
                connect("text_changed",  Callable(this, "_on_changed"));
                connect("text_submitted", Callable(this, "_on_submitted"));
            }
            _gl_gui_wire_relayout(this, "_on_parent_resized");
        }
        // El ajuste de TextScaled que quedo pendiente mientras estaba oculto.
        if (p_what == NOTIFICATION_VISIBILITY_CHANGED && _refit_pendiente && is_visible_in_tree()) _gl_refit_text();
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &RobloxTextBox::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("_on_focus_lost"),     &RobloxTextBox::_on_focus_lost);
        ClassDB::bind_method(D_METHOD("set_ShowNativeInput", "v"), &RobloxTextBox::set_ShowNativeInput);
        ClassDB::bind_method(D_METHOD("get_ShowNativeInput"),      &RobloxTextBox::get_ShowNativeInput);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ShowNativeInput"), "set_ShowNativeInput", "get_ShowNativeInput");
        ClassDB::bind_method(D_METHOD("_on_focus_gained"),   &RobloxTextBox::_on_focus_gained);
        ClassDB::bind_method(D_METHOD("_on_changed","t"),    &RobloxTextBox::_on_changed);
        ClassDB::bind_method(D_METHOD("_on_submitted","t"),  &RobloxTextBox::_on_submitted);
        ClassDB::bind_method(D_METHOD("set_PlaceholderText","v"),   &RobloxTextBox::set_PlaceholderText);
        ClassDB::bind_method(D_METHOD("get_PlaceholderText"),       &RobloxTextBox::get_PlaceholderText);
        ClassDB::bind_method(D_METHOD("set_PlaceholderColor3","v"), &RobloxTextBox::set_PlaceholderColor3);
        ClassDB::bind_method(D_METHOD("get_PlaceholderColor3"),     &RobloxTextBox::get_PlaceholderColor3);
        ClassDB::bind_method(D_METHOD("set_ClearTextOnFocus","v"),  &RobloxTextBox::set_ClearTextOnFocus);
        ClassDB::bind_method(D_METHOD("get_ClearTextOnFocus"),      &RobloxTextBox::get_ClearTextOnFocus);
        ClassDB::bind_method(D_METHOD("set_MultiLine","v"),         &RobloxTextBox::set_MultiLine);
        ClassDB::bind_method(D_METHOD("get_MultiLine"),             &RobloxTextBox::get_MultiLine);
        ClassDB::bind_method(D_METHOD("set_TextEditable","v"),      &RobloxTextBox::set_TextEditable);
        ClassDB::bind_method(D_METHOD("get_TextEditable"),          &RobloxTextBox::get_TextEditable);
        ClassDB::bind_method(D_METHOD("CaptureFocus"),              &RobloxTextBox::CaptureFocus);
        ClassDB::bind_method(D_METHOD("ReleaseFocus"),              &RobloxTextBox::ReleaseFocusRbx);
        GUI_BIND_COMMON(RobloxTextBox)
        GUI_BIND_TEXT(RobloxTextBox)
        ADD_PROPERTY(PropertyInfo(Variant::STRING,"PlaceholderText"),  "set_PlaceholderText","get_PlaceholderText");
        ADD_PROPERTY(PropertyInfo(Variant::COLOR, "PlaceholderColor3"),"set_PlaceholderColor3","get_PlaceholderColor3");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "ClearTextOnFocus"), "set_ClearTextOnFocus","get_ClearTextOnFocus");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "MultiLine"),        "set_MultiLine","get_MultiLine");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "TextEditable"),     "set_TextEditable","get_TextEditable");
        ADD_SIGNAL(MethodInfo("FocusLost"));
        ADD_SIGNAL(MethodInfo("FocusGained"));
        ADD_SIGNAL(MethodInfo("Changed", PropertyInfo(Variant::STRING,"text")));
    }

public:
    GUI_REWIRE_MOUSE_BOTON
    GUI_COMMON_METHODS(RobloxTextBox)
    GUI_TEXT_METHODS(RobloxTextBox)
    void set_ShowNativeInput(bool v) { _show_native_input = v; }
    bool get_ShowNativeInput() const { return _show_native_input; }

    // Un TextBox nuevo de Roblox es Active y Selectable y pone "TextBox".
    RobloxTextBox() {
        _active = true;
        _selectable = true;
        GUI_TEXT_DEFAULTS("TextBox")
        add_theme_color_override("font_placeholder_color", _placeholder_color);
    }

    String _gl_display_text() const { return get_text(); }
    void _gl_push_text() { set_text(_raw_text); }

    void _on_parent_resized() { GUI_COMMON_APPLY_LAYOUT _gl_refit_text(); }
    // FocusLost(enterPressed, inputThatCausedFocusLoss). Con Enter, Roblox
    // suelta el foco y avisa con enterPressed = true: es como sabe un chat o
    // un buscador que hay que enviar. Antes llegaba sin argumentos (nil).
    void _on_focus_lost() {
        const bool enter = _enter_pulsado;
        _enter_pulsado = false;
        emit_signal("FocusLost");
        if (GLAvisoFocoTexto f = gl_aviso_foco_texto()) f(this, false);
        gl_fire_cbs(_focus_lost_cbs, [enter](lua_State* th) {
            lua_pushboolean(th, enter ? 1 : 0);
            lua_pushnil(th);
            return 2;
        });
    }
    void _on_submitted(String) {
        _enter_pulsado = true;
        call_deferred("release_focus");   // LineEdit se queda con el foco tras Enter
    }
    void _on_focus_gained() {
        if (_clear_on_focus) set_text("");
        emit_signal("FocusGained"); _fire_gui_cbs(_focus_gained_cbs);
        if (GLAvisoFocoTexto f = gl_aviso_foco_texto()) f(this, true);
    }
    // text_changed solo salta cuando teclea el usuario (set_text no lo emite):
    // en Roblox eso dispara GetPropertyChangedSignal("Text"), que es la forma
    // normal de leer lo que se escribe en una caja.
    void _on_changed(String t) { emit_signal("Changed", t); gl_fire_prop_changed(this, "Text"); }

    void set_PlaceholderText(String v) { set_placeholder(v); }
    String get_PlaceholderText() const { return get_placeholder(); }
    void set_PlaceholderColor3(Color c) {
        _placeholder_color = c;
        add_theme_color_override("font_placeholder_color", c);
    }
    Color get_PlaceholderColor3() const { return _placeholder_color; }
    void set_ClearTextOnFocus(bool b) { _clear_on_focus = b; }
    bool get_ClearTextOnFocus() const { return _clear_on_focus; }
    void set_MultiLine(bool b) { _multi_line = b; }
    bool get_MultiLine() const { return _multi_line; }
    void set_TextEditable(bool b) { _text_editable = b; set_editable(b); }
    bool get_TextEditable() const { return _text_editable; }
    void CaptureFocus() { grab_focus(); }
    void ReleaseFocusRbx() { release_focus(); }

    void add_focus_lost_cb(lua_State* L, int ref)   { _focus_lost_cbs.push_back({L, ref, true}); }
    void add_focus_gained_cb(lua_State* L, int ref) { _focus_gained_cbs.push_back({L, ref, true}); }
    void _gl_disconnect(int ref, int64_t vm) {
        for (auto& cb : _focus_lost_cbs)   if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : _focus_gained_cbs) if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
        for (auto& cb : _changed_cbs)      if (cb.ref==ref && GL_MISMA_VM(cb)) cb.active=false;
    }
};

#endif // ROBLOX_GUI_TEXTO_H
