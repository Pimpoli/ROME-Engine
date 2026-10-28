#ifndef ROBLOX_GUI_IMAGEN_H
#define ROBLOX_GUI_IMAGEN_H

// GUI con imagen: ImageButton e ImageLabel, y sus macros de imagen.
// Va aparte de roblox_gui.h (pasaba de 2.000 lineas) y se incluye desde alli,
// despues de roblox_gui_texto.h: usa sus includes y las macros GUI_COMMON_*.

// ────────────────────────────────────────────────────────────────────
//  Macros de IMAGEN: ScaleType/SliceCenter/TileSize/ImageRect…
// ────────────────────────────────────────────────────────────────────
#define GUI_IMAGE_FIELDS                                       \
    String _image_path;                                        \
    float  _img_r = 1, _img_g = 1, _img_b = 1, _img_alpha = 1;  \
    int    _scale_type = 0;   /* Stretch=0,Slice=1,Tile=2,Fit=3,Crop=4 */ \
    float  _slice_scale = 1;                                   \
    Rect2  _slice_center;                                      \
    GuiUDim2 _tile_size = {1, 0, 1, 0};                        \
    Vector2 _img_rect_off;                                     \
    Vector2 _img_rect_size;                                    \
    int    _resample = 0;     /* Default=0, Pixelated=1 */

// ImageColor3/ImageTransparency pintan SOLO LA IMAGEN. Aqui se aplicaban con
// set_modulate, que en Godot se HEREDA a todo el subarbol: un ImageLabel o un
// ImageButton con ImageTransparency = 1 — el truco de toda la vida para tener
// una caja clicable invisible, o un contenedor con la foto apagada — borraba
// tambien sus TextLabel, sus Frame y sus botones. En Roblox cada hijo tiene su
// propia transparencia y no hereda nada.
// Cada clase lo pinta donde le toca (_gl_tint_imagen): el TextureRect en su
// self_modulate, el boton en el color del ICONO, para no llevarse por delante
// el BackgroundTransparency, que es la OTRA propiedad y es independiente.
#define GUI_IMAGE_METHODS(ClassName)                                    \
    void set_image_color(float r, float g, float b) {                   \
        _img_r = r; _img_g = g; _img_b = b;                             \
        _gl_tint_imagen();                                              \
    }                                                                   \
    void set_image_transparency(float a) {                              \
        _img_alpha = 1.0f - Math::clamp(a, 0.0f, 1.0f);                 \
        _gl_tint_imagen();                                              \
    }                                                                   \
    float get_image_transparency() const { return 1.0f - _img_alpha; }   \
    void set_Image(String p) { set_image(p); } \
    String get_Image() const { return _image_path; } \
    void set_ImageColor3(Color c) { set_image_color(c.r, c.g, c.b); } \
    Color get_ImageColor3() const { return Color(_img_r, _img_g, _img_b); } \
    void set_TileSize(Vector4 v) { _tile_size = {v.x, v.y, v.z, v.w}; queue_redraw(); } \
    Vector4 get_TileSize() const { \
        return Vector4(_tile_size.xs, _tile_size.xo, _tile_size.ys, _tile_size.yo); \
    } \
    void set_ScaleType(int v) { _scale_type = v; _gl_apply_image_mode(); } \
    int  get_ScaleType() const { return _scale_type; }                  \
    void set_SliceScale(float v) { _slice_scale = v; queue_redraw(); }  \
    float get_SliceScale() const { return _slice_scale; }               \
    void set_SliceCenter(Rect2 v) { _slice_center = v; queue_redraw(); } \
    Rect2 get_SliceCenter() const { return _slice_center; }             \
    void set_ImageRectOffset(Vector2 v) { _img_rect_off = v; _gl_apply_image_rect(); } \
    Vector2 get_ImageRectOffset() const { return _img_rect_off; }       \
    void set_ImageRectSize(Vector2 v) { _img_rect_size = v; _gl_apply_image_rect(); } \
    Vector2 get_ImageRectSize() const { return _img_rect_size; }        \
    void set_ResampleMode(int v) {                                      \
        _resample = v;                                                  \
        set_texture_filter(v == 1 ? CanvasItem::TEXTURE_FILTER_NEAREST   \
                                  : CanvasItem::TEXTURE_FILTER_PARENT_NODE); \
    }                                                                   \
    int  get_ResampleMode() const { return _resample; }                 \
    void set_gl_tile_size(float xs, float xo, float ys, float yo) { _tile_size = {xs, xo, ys, yo}; queue_redraw(); } \
    /* ImageContent con un objeto (EditableImage, gl_puente_editables.h): su   \
       textura compartida, que se actualiza sola al pintar. Image queda "". */ \
    void gl_set_textura_objeto(const Ref<Texture2D>& t) {                  \
        _image_path = String();                                             \
        _base_tex = t;                                                      \
        _gl_apply_image_rect();                                             \
    }                                                                       \
    /* Un clon (o lo que entra al arbol) con el objeto puesto pero sin       \
       textura: se la vuelve a pedir. */                                    \
    void _gl_reponer_objeto() {                                             \
        static const StringName k_obj("_glc_ImageContent");                 \
        if (_base_tex.is_valid() || !has_meta(k_obj)) return;               \
        Object* o = get_meta(k_obj);                                        \
        if (o && o->has_method("gl_textura")) gl_set_textura_objeto(o->call("gl_textura")); \
    }                                                                       \
    /* Image = "..." quita el objeto que hubiera en ImageContent. */         \
    void _gl_soltar_objeto() {                                              \
        static const StringName k_obj("_glc_ImageContent");                 \
        if (has_meta(k_obj)) remove_meta(k_obj);                            \
    }                                                                       \
    /* Llego su textura (gl_streaming_texturas.h): se vuelve a aplicar. */   \
    void _gl_textura_lista() { if (!_image_path.is_empty()) set_image(_image_path); } \
    /* Se pidio estando oculto: al dibujarse por primera vez, se pide ya. */ \
    void _gl_textura_si_pendiente() {                                       \
        static const StringName k_pend("_gl_tex_pendiente");                \
        static const StringName k_lista("_gl_textura_lista");               \
        if (!has_meta(k_pend)) return;                                      \
        remove_meta(k_pend);                                                \
        call_deferred(k_lista);                                             \
    }

#define GUI_BIND_IMAGE(C)                                                                  \
    ClassDB::bind_method(D_METHOD("set_image","path"),            &C::set_image);            \
    ClassDB::bind_method(D_METHOD("_gl_textura_lista"),           &C::_gl_textura_lista);    \
    ClassDB::bind_method(D_METHOD("set_image_color","r","g","b"), &C::set_image_color);      \
    ClassDB::bind_method(D_METHOD("set_image_transparency","a"),  &C::set_image_transparency);\
    ClassDB::bind_method(D_METHOD("get_image_transparency"),      &C::get_image_transparency);\
    ClassDB::bind_method(D_METHOD("set_ScaleType","v"),           &C::set_ScaleType);        \
    ClassDB::bind_method(D_METHOD("get_ScaleType"),               &C::get_ScaleType);        \
    ClassDB::bind_method(D_METHOD("set_SliceScale","v"),          &C::set_SliceScale);       \
    ClassDB::bind_method(D_METHOD("get_SliceScale"),              &C::get_SliceScale);       \
    ClassDB::bind_method(D_METHOD("set_SliceCenter","v"),         &C::set_SliceCenter);      \
    ClassDB::bind_method(D_METHOD("get_SliceCenter"),             &C::get_SliceCenter);      \
    ClassDB::bind_method(D_METHOD("set_ImageRectOffset","v"),     &C::set_ImageRectOffset);  \
    ClassDB::bind_method(D_METHOD("get_ImageRectOffset"),         &C::get_ImageRectOffset);  \
    ClassDB::bind_method(D_METHOD("set_ImageRectSize","v"),       &C::set_ImageRectSize);    \
    ClassDB::bind_method(D_METHOD("get_ImageRectSize"),           &C::get_ImageRectSize);    \
    ClassDB::bind_method(D_METHOD("set_ResampleMode","v"),        &C::set_ResampleMode);     \
    ClassDB::bind_method(D_METHOD("get_ResampleMode"),            &C::get_ResampleMode);     \
    ClassDB::bind_method(D_METHOD("set_gl_tile_size","xs","xo","ys","yo"), &C::set_gl_tile_size); \
    ClassDB::bind_method(D_METHOD("_gl_apply_image_mode"),        &C::_gl_apply_image_mode); \
    ClassDB::bind_method(D_METHOD("_gl_apply_image_rect"),        &C::_gl_apply_image_rect); \
    ADD_PROPERTY(PropertyInfo(Variant::INT,    "ScaleType", PROPERTY_HINT_ENUM,"Stretch,Slice,Tile,Fit,Crop"), "set_ScaleType","get_ScaleType"); \
    ClassDB::bind_method(D_METHOD("set_Image","v"), &C::set_Image); \
    ClassDB::bind_method(D_METHOD("get_Image"),     &C::get_Image); \
    ClassDB::bind_method(D_METHOD("set_ImageColor3","v"), &C::set_ImageColor3); \
    ClassDB::bind_method(D_METHOD("get_ImageColor3"),     &C::get_ImageColor3); \
    ClassDB::bind_method(D_METHOD("set_TileSize","v"), &C::set_TileSize); \
    ClassDB::bind_method(D_METHOD("get_TileSize"),     &C::get_TileSize); \
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "SliceScale"),       "set_SliceScale","get_SliceScale"); \
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "Image"),        "set_Image","get_Image"); \
    ADD_PROPERTY(PropertyInfo(Variant::COLOR,  "ImageColor3"),  "set_ImageColor3","get_ImageColor3"); \
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR4,"TileSize"),     "set_TileSize","get_TileSize");        \
    ADD_PROPERTY(PropertyInfo(Variant::RECT2,  "SliceCenter"),      "set_SliceCenter","get_SliceCenter");      \
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2,"ImageRectOffset"),  "set_ImageRectOffset","get_ImageRectOffset"); \
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2,"ImageRectSize"),    "set_ImageRectSize","get_ImageRectSize");  \
    ADD_PROPERTY(PropertyInfo(Variant::INT,    "ResampleMode", PROPERTY_HINT_ENUM,"Default,Pixelated"), "set_ResampleMode","get_ResampleMode"); \
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "ImageTransparency"), "set_image_transparency","get_image_transparency");

// Carga la textura de un id de Roblox, buscando primero una copia LOCAL puesta
// por el usuario en GodotLuau/assets/rbx/<id>.png (ver gl_local_asset_path).
// Textura BLANCA compartida: lo que Roblox pone por defecto en un ImageLabel/
// ImageButton y lo que se enseña mientras un asset no descargado no llega.
// Con NADA, el UIGradient de encima no tiene sobre que multiplicar y las
// tarjetas salian planas; con blanco, el degradado se ve aunque falte la foto.
inline Ref<ImageTexture>& gl_tex_blanca_ref() { static Ref<ImageTexture> t; return t; }
inline Ref<Texture2D> gl_tex_blanca() {
    Ref<ImageTexture>& t = gl_tex_blanca_ref();
    if (!t.is_valid()) {
        Ref<Image> im = Image::create_empty(2, 2, false, Image::FORMAT_RGBA8);
        im->fill(Color(1, 1, 1, 1));
        t = ImageTexture::create_from_image(im);
    }
    return t;
}
// Textura TRANSPARENTE compartida: lo que se pinta mientras una textura viene
// de camino por streaming (gl_streaming_texturas.h), donde la blanca seria un
// destello (un Beam sin textura es blanco en Roblox, pero cargando no).
inline Ref<ImageTexture>& gl_tex_vacia_ref() { static Ref<ImageTexture> t; return t; }
inline Ref<Texture2D> gl_tex_vacia() {
    Ref<ImageTexture>& t = gl_tex_vacia_ref();
    if (!t.is_valid()) {
        Ref<Image> im = Image::create_empty(2, 2, false, Image::FORMAT_RGBA8);
        im->fill(Color(1, 1, 1, 0));
        t = ImageTexture::create_from_image(im);
    }
    return t;
}
// La llama el deinit de la extension: una Ref estatica vive mas que el
// RenderingServer y al cerrar salia "1 RID Texture leaked" + un ERROR.
inline void gl_tex_blanca_libera() { gl_tex_blanca_ref().unref(); gl_tex_vacia_ref().unref(); }

// ScaleType.Tile: la imagen se repite en mosaicos del tamaño de TileSize (un
// UDim2 sobre el tamaño del propio GuiObject), desde la esquina de arriba a la
// izquierda. Se escala el lienzo para que un mosaico mida eso y Godot repite
// la textura (el CanvasItem lleva texture_repeat activado en modo Tile).
inline void gl_dibujar_mosaico(CanvasItem* ci, const Ref<Texture2D>& t, const Vector2& tam, const GuiUDim2& ts) {
    const Vector2 ttex = t->get_size();
    const Vector2 celda(ts.xs * tam.x + ts.xo, ts.ys * tam.y + ts.yo);
    if (celda.x < 0.5f || celda.y < 0.5f || ttex.x < 1.0f || ttex.y < 1.0f || tam.x <= 0.0f || tam.y <= 0.0f) return;
    const Vector2 esc(celda.x / ttex.x, celda.y / ttex.y);
    ci->draw_set_transform(Vector2(), 0.0f, esc);
    ci->draw_texture_rect(t, Rect2(Vector2(), Vector2(tam.x / esc.x, tam.y / esc.y)), true);
    ci->draw_set_transform(Vector2(), 0.0f, Vector2(1, 1));
}

inline Ref<Texture2D> gl_gui_load_image(Node* owner, const String& path) {
    if (path.is_empty()) return Ref<Texture2D>();
    String real = path;
    // Si la ruta sale de los indices de assets ya se sabe que es: un recurso
    // importado (1) o un archivo suelto (2), sin preguntarle al disco.
    int tipo = 0;
    if (gl_is_roblox_asset(path)) {
        real = gl_local_asset_path(path);          // 1) copia manual del usuario
        tipo = 1;
        // 1b) archivo suelto en assets/rbx/ que Godot aun no ha importado:
        //     es el caso de las imagenes que acaba de escribir un .gdlu.
        if (real.is_empty()) { real = gl_local_asset_file(path); tipo = 2; }
        if (real.is_empty()) { real = gl_cache_asset_path(path); tipo = 2; }   // 2) cache descargado
        if (real.is_empty()) {
            // 3) no esta: se pide a la nube de Roblox y cuando llegue el
            // fetcher re-llama set_image en este nodo (ver gl_asset_fetch.h).
            // Mientras tanto, BLANCO (como el Image por defecto de Roblox).
            if (owner) owner->set_meta("__rbx_missing_asset", path);
            if (GLAssetFetcher* fx = GLAssetFetcher::singleton()) fx->pedir(path, owner);
            // En Roblox una imagen que no carga NO PINTA NADA. Devolver una
        // textura BLANCA dejaba un recuadro blanco tapando la UI — y para
        // siempre, porque los assets propios del juego dan 401 y nadie
        // retiraba el relleno. Blanco SOLO mientras la descarga esta en
        // vuelo (asi el hueco no parpadea); si ya fallo, o estamos en el
        // editor (donde no se descarga nada), se deja VACIO.
        {
            GLAssetFetcher* _fx = GLAssetFetcher::singleton();
            const bool en_vuelo = _fx && !Engine::get_singleton()->is_editor_hint() &&
                                  !_fx->ha_fallado(gl_asset_id_number(path));
            if (!en_vuelo) return Ref<Texture2D>();
        }
        return gl_tex_blanca();
        }
        if (owner) owner->remove_meta("__rbx_missing_asset");
    }
    // Archivos sueltos (las capturas de user://screenshots): no son recursos
    // importados, asi que ResourceLoader no los conoce. Se cargan como imagen
    // cruda; sin esto la Galeria contaba capturas pero no las enseñaba.
    // Lo ya resuelto, por ruta: ResourceLoader::exists() mira el disco en cada
    // llamada (54 ms del primer frame de Build a Mountain).
    if (Ref<Texture2D>* hay = gl_texturas_crudas().getptr(real)) return *hay;
    ResourceLoader* rl = ResourceLoader::get_singleton();
    // Streaming (gl_streaming_texturas.h): quien sabe esperar no carga nada en
    // el hilo principal. Oculto, ni la pide: lo hara al dibujarse.
    if (owner && gl_tex_streaming_activo() && gl_tex_sabe_esperar(owner)) {
        const bool importada = tipo == 1 || (tipo == 0 && rl && rl->exists(real));
        // Una ruta suelta que no existe: nada, sin errores (como gl_textura_cruda).
        if (tipo == 0 && !importada && !FileAccess::file_exists(real)) return Ref<Texture2D>();
        // Godot 4.8+ con su streaming: lo importado lo carga el motor.
        if (!(importada && gl_tex_motor_streaming())) {
            static const StringName k_pend("_gl_tex_pendiente");
            if (!gl_tex_se_ve(owner)) {
                owner->set_meta(k_pend, true);
                return Ref<Texture2D>();
            }
            if (owner->has_meta(k_pend)) owner->remove_meta(k_pend);
            return gl_tex_obtener(real, importada, owner);
        }
    }
    if (tipo == 2) return gl_textura_cruda(real);
    // Recurso importado por Godot: el camino normal.
    if (tipo == 1 || (rl && rl->exists(real))) {
        const Ref<Texture2D> t = rl->load(real);
        if (t.is_valid()) gl_texturas_guardar(real, t);
        return t;
    }
    // Archivo suelto (capturas en user://, imagenes recien escritas por el
    // importador de .gdlu): no son recursos importados, asi que se cargan
    // crudos. Sin esto la Galeria contaba capturas sin enseñarlas y un .gdlu
    // recien importado se veia sin texturas hasta reabrir el proyecto.
    // Leido una sola vez por ruta (gl_textura_cruda).
    return gl_textura_cruda(real);
}

// ────────────────────────────────────────────────────────────────────
//  RobloxImageButton — Image button (clickable)
////  RobloxImageButton — Botón con imagen, señales tipo Roblox.
// ────────────────────────────────────────────────────────────────────
class RobloxImageButton : public Button {
    GDCLASS(RobloxImageButton, Button);
    GUI_COMMON_FIELDS
    GUI_IMAGE_FIELDS
    String _hover_image, _pressed_image;
    bool   _auto_button_color = true;
    GUI_BOTON_EXTRA_FIELDS
    Ref<Texture2D> _base_tex;

protected:
    // Ver la nota de RobloxImageLabel: la textura se deriva de Image y NO se
    // guarda en la escena (si no, se horneaba la blanca de relleno).
    void _validate_property(PropertyInfo &p) const {
        if (p.name == StringName("icon") || p.name == StringName("texture"))
            p.usage &= ~PROPERTY_USAGE_STORAGE;
        // 'modulate' NO se guarda: no existe en Roblox y se hereda a los hijos.
        // La version vieja lo usaba para ImageTransparency y quedo horneado en
        // los .tscn ya importados; que no vuelva a pasar al reimportar.
        if (p.name == StringName("modulate"))
            p.usage &= ~PROPERTY_USAGE_STORAGE;
    }

private:

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
        // La caja compartida (sin relleno si es transparente, y sin margen: el
        // borde no encoge la imagen), la misma en los tres estados y puesta solo
        // si cambia. Antes cada vez se fabricaba una caja nueva y se ponia tres
        // veces, con su aviso de tema cada una.
        const Ref<StyleBox> caja = gl_estilo_compartido(Color(_bg_r, _bg_g, _bg_b, 1.0f - _bg_alpha),
                                                        Color(_border_r, _border_g, _border_b, 1.0f - _bg_alpha),
                                                        gl_borde_px(this, _border_px));
        const StringName* k = gl_claves_caja();
        const StringName claves[3] = { k[1], k[2], k[3] };   // normal, hover, pressed
        const Ref<StyleBox> cajas[3] = { caja, caja, caja };
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
            set_expand_icon(true);   // el icono (imagen) llena el boton, como Roblox
            _gl_reponer_objeto();
            _gl_tint_imagen();
            if (!is_connected("pressed", Callable(this, "_on_pressed"))) {
                connect("pressed",       Callable(this, "_on_pressed"));
                connect("button_down",   Callable(this, "_on_button_down"));
                connect("button_up",     Callable(this, "_on_button_up"));
                // (hover: lo lleva gl_hover_tick por geometria, ver add_enter_cb)
            }
            _gl_gui_wire_relayout(this, "_on_parent_resized");
        } else if (p_what == NOTIFICATION_DRAW) {
            _gl_textura_si_pendiente();
        }
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &RobloxImageButton::_gl_disconnect);
        ClassDB::bind_method(D_METHOD("_on_pressed"),        &RobloxImageButton::_on_pressed);
        ClassDB::bind_method(D_METHOD("_on_button_down"),    &RobloxImageButton::_on_button_down);
        ClassDB::bind_method(D_METHOD("_on_button_up"),      &RobloxImageButton::_on_button_up);
        ClassDB::bind_method(D_METHOD("_on_mouse_enter"),    &RobloxImageButton::_on_mouse_enter);
        ClassDB::bind_method(D_METHOD("_on_mouse_leave"),    &RobloxImageButton::_on_mouse_leave);
        ClassDB::bind_method(D_METHOD("set_HoverImage","v"),   &RobloxImageButton::set_HoverImage);
        ClassDB::bind_method(D_METHOD("get_HoverImage"),       &RobloxImageButton::get_HoverImage);
        ClassDB::bind_method(D_METHOD("set_PressedImage","v"), &RobloxImageButton::set_PressedImage);
        ClassDB::bind_method(D_METHOD("get_PressedImage"),     &RobloxImageButton::get_PressedImage);
        ClassDB::bind_method(D_METHOD("set_AutoButtonColor","b"), &RobloxImageButton::set_AutoButtonColor);
        ClassDB::bind_method(D_METHOD("get_AutoButtonColor"),     &RobloxImageButton::get_AutoButtonColor);
        GUI_BIND_COMMON(RobloxImageButton)
        GUI_BIND_IMAGE(RobloxImageButton)
        GUI_BOTON_EXTRA_BIND(RobloxImageButton)
        ADD_PROPERTY(PropertyInfo(Variant::STRING,"HoverImage"),   "set_HoverImage","get_HoverImage");
        ADD_PROPERTY(PropertyInfo(Variant::STRING,"PressedImage"), "set_PressedImage","get_PressedImage");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL,  "AutoButtonColor"), "set_AutoButtonColor","get_AutoButtonColor");
        ADD_SIGNAL(MethodInfo("MouseButton1Click"));
        ADD_SIGNAL(MethodInfo("Activated"));
        ADD_SIGNAL(MethodInfo("MouseButton1Down"));
        ADD_SIGNAL(MethodInfo("MouseButton1Up"));
        ADD_SIGNAL(MethodInfo("MouseEnter"));
        ADD_SIGNAL(MethodInfo("MouseLeave"));
    }

public:
    GUI_REWIRE_MOUSE_BOTON
    GUI_BOTON_NO_TECLADO
    GUI_COMMON_METHODS(RobloxImageButton)
    GUI_IMAGE_METHODS(RobloxImageButton)
    GUI_BOTON_EXTRA_METHODS

    // Un ImageButton nuevo de Roblox es Active y Selectable.
    RobloxImageButton() {
        _active = true;
        _selectable = true;
    }

    void _on_parent_resized() {
        GUI_COMMON_APPLY_LAYOUT
        GUI_APPLY_AUTO_SIZE(get_minimum_size())
    }
    // Ver la nota de RobloxTextButton: en Roblox son tres momentos distintos.
    void _on_button_down() { emit_signal("MouseButton1Down");  _fire_gui_cbs(_down_cbs); _fire_gui_cbs_input(_input_began_cbs, true); }
    void _on_button_up()   { emit_signal("MouseButton1Up");    _fire_gui_cbs(_up_cbs);   _fire_gui_cbs_input(_input_ended_cbs, false); }

    std::vector<GuiLuaCallback> _input_began_cbs;
    std::vector<GuiLuaCallback> _input_ended_cbs;
    void add_input_began_cb(lua_State* L, int ref) { _input_began_cbs.push_back({L, ref, true}); }
    void add_input_ended_cb(lua_State* L, int ref) { _input_ended_cbs.push_back({L, ref, true}); }
    void _on_pressed()     { emit_signal("MouseButton1Click"); emit_signal("Activated"); _fire_gui_cbs(_click_cbs); }
    void _on_mouse_enter() { emit_signal("MouseEnter"); _fire_gui_cbs(_enter_cbs); }
    void _on_mouse_leave() { emit_signal("MouseLeave"); _fire_gui_cbs(_leave_cbs); }

    void set_image(String path) {
        _gl_soltar_objeto();
        _image_path = path;
        _base_tex = gl_gui_load_image(this, path);
        _gl_apply_image_rect();
    }
    String get_image() const { return _image_path; }
    void set_HoverImage(String v)   { _hover_image = v; }
    String get_HoverImage() const   { return _hover_image; }
    void set_PressedImage(String v) { _pressed_image = v; }
    String get_PressedImage() const { return _pressed_image; }
    void set_AutoButtonColor(bool b) { _auto_button_color = b; }
    bool get_AutoButtonColor() const { return _auto_button_color; }

    // ImageColor3/ImageTransparency de un ImageButton pintan EL ICONO, no el
    // boton entero: el fondo lo manda BackgroundTransparency (el StyleBox) y
    // son dos propiedades distintas. Con set_modulate se apagaban las dos a la
    // vez, y ademas todos los hijos del boton.
    void _gl_tint_imagen() {
        const Color c(_img_r, _img_g, _img_b, _img_alpha);
        static const char* keys[] = { "icon_normal_color", "icon_hover_color",
                                      "icon_pressed_color", "icon_focus_color",
                                      "icon_disabled_color" };
        for (int i = 0; i < 5; i++) add_theme_color_override(keys[i], c);
        // Ver RobloxImageLabel::_gl_tint_imagen: se limpia el modulate viejo
        // horneado en el .tscn, que apagaba el boton entero y a sus hijos.
        if (get_modulate() != Color(1, 1, 1, 1)) set_modulate(Color(1, 1, 1, 1));
    }

    // ImageRectOffset/Size recortan un trozo del atlas (los sprite-sheets de
    // iconos de Roblox). Sin esto salia la hoja entera dentro del boton.
    void _gl_apply_image_rect() {
        // Sin textura hay que LIMPIAR la que hubiera: `Image = ""` (limpiar un
        // hueco de la hotbar, quitar un icono) dejaba pintado el icono anterior.
        if (_base_tex.is_null()) { _gl_poner_icono(Ref<Texture2D>()); return; }
        const Vector2 _asz = _base_tex->get_size();
        const bool _fits = _img_rect_size.x > 0.5f && _img_rect_size.y > 0.5f &&
                           _img_rect_off.x >= 0.0f && _img_rect_off.y >= 0.0f &&
                           _img_rect_off.x + _img_rect_size.x <= _asz.x + 0.5f &&
                           _img_rect_off.y + _img_rect_size.y <= _asz.y + 0.5f;
        // Si la region NO cabe en el atlas, la textura sale vacia y el hilo de
        // render de Godot se cae. En ese caso se usa la imagen entera.
        if (_fits) {
            Ref<AtlasTexture> at; at.instantiate();
            at->set_atlas(_base_tex);
            at->set_region(Rect2(_img_rect_off, _img_rect_size));
            _gl_poner_icono(at);
        } else {
            _gl_poner_icono(_base_tex);
        }
    }
    // ScaleType.Slice: el icono no lo pinta el Button sino _draw, en nueve
    // trozos y con el tinte de la imagen (ImageColor3 / ImageTransparency).
    Ref<Texture2D> _slice_tex;
    void _gl_poner_icono(const Ref<Texture2D>& t) {
        if (_scale_type == 1 && t.is_valid()) { _slice_tex = t; set_button_icon(Ref<Texture2D>()); }
        else { _slice_tex.unref(); set_button_icon(t); }
        queue_redraw();
    }
    void _draw() override {
        if (_slice_tex.is_valid())
            gl_dibujar_nueve(this, _slice_tex, Rect2(Vector2(), get_size()), _slice_center, _slice_scale,
                             Color(_img_r, _img_g, _img_b, _img_alpha));
    }
    // ScaleType en un boton: el icono se estira (Stretch) o conserva su
    // proporcion (Fit/Crop).
    void _gl_apply_image_mode() {
        set_expand_icon(_scale_type != 3);
        set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER);
        _gl_apply_image_rect();   // Slice entra o sale: el icono cambia de camino
    }

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
//  RobloxImageLabel — Static image
////  RobloxImageLabel — Imagen estática
// ────────────────────────────────────────────────────────────────────
class RobloxImageLabel : public TextureRect {
    GDCLASS(RobloxImageLabel, TextureRect);
    GUI_COMMON_FIELDS
    GUI_HOVER_FIELDS
    GUI_IMAGE_FIELDS
    Ref<Texture2D> _base_tex;

protected:
    // La textura SIEMPRE se deriva de Image: no debe guardarse en la escena.
    // Si se guarda, se hornea lo que hubiera en ese momento — y al importar un
    // .gdlu eso era la BLANCA de relleno (los PNG se escriben despues de leer
    // las propiedades), que ademas pisaba al Image al cargar. Ademas evita
    // meter megas de pixeles dentro del .tscn.
    void _validate_property(PropertyInfo &p) const {
        if (p.name == StringName("texture"))
            p.usage &= ~PROPERTY_USAGE_STORAGE;
        // Ver RobloxImageButton: 'modulate' no es de Roblox y se hereda.
        if (p.name == StringName("modulate"))
            p.usage &= ~PROPERTY_USAGE_STORAGE;
    }

private:

    void _apply_style() {
        // Fuera del arbol NO se toca el tema: cada set de una propiedad de estilo
        // creaba un StyleBoxFlat que se tiraba enseguida. Al DUPLICAR un nodo,
        // Godot copia todas sus propiedades una a una, asi que un solo TextLabel
        // clonado creaba media docena de estilos para nada. ENTER_TREE lo aplica
        // una vez, que es cuando de verdad se ve.
        if (!is_inside_tree()) return;
        _gl_tint_imagen();
        queue_redraw();   // el fondo (ver _pintar_fondo)
    }

    // ── EL FONDO DE UN ImageLabel ─────────────────────────────────────
    // Roblox pinta BackgroundColor3 detras de la imagen; un TextureRect no tiene
    // caja, asi que no salia nunca (un icono sobre un cuadro de color perdia el
    // cuadro). Va en un canvas item hijo pintado DETRAS del padre, fuera del
    // self_modulate de la imagen: ImageTransparency no apaga el fondo.
    RID _fondo;
    void _pintar_fondo() {
        if (!_fondo.is_valid()) return;
        RenderingServer* rs = RenderingServer::get_singleton();
        rs->canvas_item_clear(_fondo);
        rs->canvas_item_set_visibility_layer(_fondo, get_visibility_layer());
        const float a = 1.0f - _bg_alpha;
        if (a <= 0.0f) return;
        Ref<StyleBoxFlat> sb = gl_estilo_compartido(Color(_bg_r, _bg_g, _bg_b, a),
            Color(_border_r, _border_g, _border_b, a), gl_borde_px(this, _border_px));
        const float r = gl_radio_uicorner(this);
        if (r > 0.0f) {
            sb = sb->duplicate();
            sb->set_corner_radius_all((int)Math::round(r));
        }
        sb->draw(_fondo, Rect2(Vector2(), get_size()));
    }

    // self_modulate y NO modulate: en Godot modulate se hereda a los hijos, y
    // en Roblox ImageTransparency solo apaga ESTA imagen.
    void _gl_tint_imagen() {
        set_self_modulate(Color(_img_r, _img_g, _img_b, _img_alpha));
        // Y se limpia el modulate. Un GuiObject de Roblox NO tiene esa
        // propiedad — todo su color sale de BackgroundColor3/ImageColor3/
        // TextColor3 — asi que cualquier valor ahi es basura de la version
        // vieja HORNEADA en el .tscn del place ya importado (39 elementos
        // medidos). Sin esto el arreglo no se nota en un place existente.
        if (get_modulate() != Color(1, 1, 1, 1)) set_modulate(Color(1, 1, 1, 1));
    }

protected:
    void _notification(int p_what) {
        GUI_Z_NOTIFICATION
        if (p_what == NOTIFICATION_ENTER_TREE) {
            // Como Roblox: la imagen SIEMPRE se ajusta al tamano del label. El
            // TextureRect de Godot, por defecto, impone como minimo el tamano de
            // la TEXTURA: un PNG de 4096px convertia un icono de 34px en un
            // gigante que empujaba al resto del layout fuera de la pantalla.
            set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
            gl_apply_rbx_gui_meta(this);   // props importadas guardadas como meta
            gl_gui_apply_render_gate(this);  // regla de Roblox: solo dentro de un *Gui
            GUI_COMMON_APPLY_LAYOUT
            _apply_style();
            _gl_reponer_objeto();
            _gl_apply_image_mode();
            _gl_gui_wire_relayout(this, "_on_parent_resized");
            GUI_HOVER_WIRE
        } else if (p_what == NOTIFICATION_DRAW) {
            _pintar_fondo();
            _gl_textura_si_pendiente();
        }
    }

protected:
    static void _bind_methods() {
        GUI_BIND_COMMON(RobloxImageLabel)
        GUI_HOVER_BIND(RobloxImageLabel)
        ClassDB::bind_method(D_METHOD("_gl_disconnect","ref","vm"), &RobloxImageLabel::_gl_disconnect);
        GUI_BIND_IMAGE(RobloxImageLabel)
    }

public:
    GUI_REWIRE_MOUSE_PASIVO
    GUI_COMMON_METHODS(RobloxImageLabel)
    GUI_HOVER_METHODS
    void _gl_disconnect(int ref, int64_t vm) { _gl_hover_disconnect(ref, vm); }
    GUI_IMAGE_METHODS(RobloxImageLabel)

    RobloxImageLabel() {
        RenderingServer* rs = RenderingServer::get_singleton();
        _fondo = rs->canvas_item_create();
        rs->canvas_item_set_parent(_fondo, get_canvas_item());
        rs->canvas_item_set_draw_behind_parent(_fondo, true);
    }
    ~RobloxImageLabel() {
        if (_fondo.is_valid() && RenderingServer::get_singleton()) RenderingServer::get_singleton()->free_rid(_fondo);
    }

    void _on_parent_resized() {
        GUI_COMMON_APPLY_LAYOUT
        GUI_APPLY_AUTO_SIZE(_base_tex.is_valid() ? _base_tex->get_size() : get_size())
    }

    void set_image(String path) {
        _gl_soltar_objeto();
        _image_path = path;
        _base_tex = gl_gui_load_image(this, path);
        _gl_apply_image_rect();
    }
    String get_image() const { return _image_path; }

    void _gl_apply_image_rect() {
        // Sin textura hay que LIMPIAR la que hubiera: `Image = ""` (limpiar un
        // hueco de la hotbar, quitar un icono) dejaba pintado el icono anterior.
        if (_base_tex.is_null()) { _gl_poner_textura(Ref<Texture2D>()); return; }
        const Vector2 _asz = _base_tex->get_size();
        const bool _fits = _img_rect_size.x > 0.5f && _img_rect_size.y > 0.5f &&
                           _img_rect_off.x >= 0.0f && _img_rect_off.y >= 0.0f &&
                           _img_rect_off.x + _img_rect_size.x <= _asz.x + 0.5f &&
                           _img_rect_off.y + _img_rect_size.y <= _asz.y + 0.5f;
        // Si la region NO cabe en el atlas, la textura sale vacia y el hilo de
        // render de Godot se cae. En ese caso se usa la imagen entera.
        if (_fits) {
            Ref<AtlasTexture> at; at.instantiate();
            at->set_atlas(_base_tex);
            at->set_region(Rect2(_img_rect_off, _img_rect_size));
            _gl_poner_textura(at);
        } else {
            _gl_poner_textura(_base_tex);
        }
    }
    // ScaleType.Slice: la textura no la pinta el TextureRect sino _draw, en
    // nueve trozos (el self_modulate de ImageColor3/ImageTransparency vale igual).
    // ScaleType.Tile tambien va por _draw: el TextureRect solo sabe repetir la
    // textura a su tamaño en pixeles, y en Roblox cada mosaico mide TileSize.
    Ref<Texture2D> _slice_tex;
    Ref<Texture2D> _tile_tex;
    void _gl_poner_textura(const Ref<Texture2D>& t) {
        _slice_tex.unref();
        _tile_tex.unref();
        if (_scale_type == 1 && t.is_valid()) { _slice_tex = t; set_texture(Ref<Texture2D>()); }
        else if (_scale_type == 2 && t.is_valid()) { _tile_tex = t; set_texture(Ref<Texture2D>()); }
        else set_texture(t);
        queue_redraw();
    }
    void _draw() override {
        if (_slice_tex.is_valid())
            gl_dibujar_nueve(this, _slice_tex, Rect2(Vector2(), get_size()), _slice_center, _slice_scale, Color(1, 1, 1, 1));
        else if (_tile_tex.is_valid())
            gl_dibujar_mosaico(this, _tile_tex, get_size(), _tile_size);
    }

    // Enum.ScaleType: Stretch=0, Slice=1, Tile=2, Fit=3, Crop=4.
    // Sin esto TODAS las imagenes se estiraban: los iconos redondos salian
    // ovalados y los fondos con patron perdian el mosaico.
    void _gl_apply_image_mode() {
        switch (_scale_type) {
            case 2:  set_stretch_mode(TextureRect::STRETCH_TILE); break;
            case 3:  set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED); break;
            case 4:  set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_COVERED); break;
            default: set_stretch_mode(TextureRect::STRETCH_SCALE); break;
        }
        // El mosaico de _draw necesita que la textura se repita.
        set_texture_repeat(_scale_type == 2 ? CanvasItem::TEXTURE_REPEAT_ENABLED
                                            : CanvasItem::TEXTURE_REPEAT_PARENT_NODE);
        _gl_apply_image_rect();   // Slice o Tile entran o salen: la textura cambia de camino
    }
};

#endif // ROBLOX_GUI_IMAGEN_H
