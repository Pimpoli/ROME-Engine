#ifndef GL_VIEWPORT_FRAME_H
#define GL_VIEWPORT_FRAME_H

// ════════════════════════════════════════════════════════════════════
//  ViewportFrame de Roblox: un GuiObject que dibuja su contenido 3D.
//
//  Es un Frame entero (hereda de RobloxFrame: UDim2, fondo, borde, ZIndex,
//  Rotation, UICorner...) que ademas pinta, encima de su fondo, lo que se ve
//  de las piezas que cuelgan de el desde su CurrentCamera, con la luz de
//  Ambient + una luz direccional (LightColor, LightDirection), sin sombras ni
//  cielo, y tenido por ImageColor3 / ImageTransparency.
//
//  COMO SE DIBUJA, SIN COPIAR NADA:
//   · Cada visor tiene su propio escenario de render (scenario) y su propio
//     viewport del servidor de render, hechos a mano con RID: ni SubViewport,
//     ni World3D, ni nodos. Un World3D trae ademas un espacio de fisica de
//     Jolt (con su buffer temporal de 128 MB en este proyecto) que aqui no
//     sirve para nada, y los SubViewport de la clase vieja acababan guardados
//     en el .tscn y se duplicaban (ver godotluau-viewportframe-duplicados).
//   · Las piezas de dentro siguen siendo las de siempre: los scripts las
//     mueven con CFrame/PivotTo cada frame y les cambian Color, Material,
//     Size... Lo unico que cambia es EN QUE ESCENARIO pone Godot su instancia
//     de render: la de su visor en vez la del mundo. Un nodo 3D que cuelga de
//     un Control va en coordenadas del mundo, asi que su transform es su CFrame,
//     que es justo lo que se ve desde la CurrentCamera.
//   · Godot pone el escenario del mundo en cada ENTER_WORLD. Por eso el visor
//     recorre su contenido (al final del frame en que algo entra) y se engancha
//     a "tree_entered" de cada instancia que mueve: si vuelve a entrar al arbol
//     (un reparentado), la devuelve a su escenario en ese mismo momento.
//   · La pieza, al entrar al arbol, mira si tiene un visor encima antes de
//     llegar a Workspace (roblox_part.h): si lo tiene, se ve (en el escenario del
//     visor) pero sale de la fisica del juego, como en Roblox. Con un WorldModel
//     en medio sigue procesando: sus Motor6D y sus animaciones trabajan.
//   · El render solo existe si el visor esta en un ScreenGui de PlayerGui (o en
//     un SurfaceGui/BillboardGui del mundo) y solo se actualiza si su textura se
//     pinta (VIEWPORT_UPDATE_WHEN_VISIBLE). Oculto: tamano 0 (sin memoria) y
//     apagado. La resolucion es la del frame en pixeles de pantalla, con tope.
//
//  Retrato del menu (Subject, extension de GodotLuau): copia una vez las mallas
//  del personaje en el escenario del visor y encuadra la cara. Lo usa la
//  pestana Players del menu.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/rendering_server.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/style_box_empty.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include "roblox_gui.h"      // RobloxFrame, ScreenGui
#include "roblox_part.h"     // gl_color_pieza (gamma 2) y las piezas
#include "rbx_instances.h"   // RBXModel, la base de WorldModel
#include "gl_servicios.h"    // gl_en_workspace

using namespace godot;

// Lado maximo del render de un visor, en pixeles.
static const int GL_VF_MAX_PX = 2048;

// Visores con escenario propio ahora mismo. Con cero, una pieza que entra al
// arbol ni siquiera mira si tiene un visor encima.
inline int& gl_vf_activos_ref() { static int n = 0; return n; }
inline int gl_vf_activos() { return gl_vf_activos_ref(); }

// A/B de rendimiento: GL_VF_OFF=1 deja los visores como un Frame (sin render y
// con su contenido oculto, que es como estaban antes).
inline bool gl_vf_apagados() {
    static const bool off = OS::get_singleton() && OS::get_singleton()->get_environment("GL_VF_OFF") == "1";
    return off;
}

// ── Tono: gamma 2 a la salida ─────────────────────────────────────────
// El color de una pieza entra en lineal con gamma 2 (gl_color_pieza: c*c). Con
// el tonemap lineal de Godot, la salida es sRGB(c*c*luz), que no devuelve c:
// los tonos medios salian un 5-7% mas claros que el mismo color en un Frame.
// Esta LUT (ajuste de color 1D, en sRGB tras el tonemap) deshace el sRGB y
// codifica con gamma 2: una cara con luz 1 sale con el Color exacto de la
// pieza. Es una sola para todos los visores; se suelta en la descarga.
inline Ref<ImageTexture>& gl_vf_lut_ref() { static Ref<ImageTexture> t; return t; }
inline RID gl_vf_lut() {
    Ref<ImageTexture>& t = gl_vf_lut_ref();
    if (t.is_null()) {
        const int N = 1024;
        PackedByteArray d;
        d.resize(N * 3);
        uint8_t* w = d.ptrw();
        for (int i = 0; i < N; i++) {
            const float u = (i + 0.5f) / N;
            const float lin = u <= 0.04045f ? u / 12.92f : Math::pow((u + 0.055f) / 1.055f, 2.4f);
            const uint8_t v = (uint8_t)CLAMP((int)Math::round(Math::sqrt(lin) * 255.0f), 0, 255);
            w[i * 3] = v; w[i * 3 + 1] = v; w[i * 3 + 2] = v;
        }
        t = ImageTexture::create_from_image(Image::create_from_data(N, 1, false, Image::FORMAT_RGB8, d));
    }
    return t.is_valid() ? t->get_rid() : RID();
}
// La llama register_types.cpp en la descarga, con el servidor de render vivo.
inline void gl_vf_liberar() { gl_vf_lut_ref().unref(); }

// La base de una luz que viaja hacia `d` (su -Z apunta ahi).
inline Basis gl_vf_mirando(Vector3 d) {
    if (d.length_squared() < 1e-8f) d = Vector3(-1, -1, -1);
    d = d.normalized();
    return Basis::looking_at(d, Math::abs(d.y) > 0.99f ? Vector3(0, 0, 1) : Vector3(0, 1, 0));
}

// ════════════════════════════════════════════════════════════════════
//  Camera de Roblox (la de un ViewportFrame, o cualquiera que cree un script).
//  Es un Node3D y no una Camera3D a proposito: una Camera3D se apunta sola en
//  la lista de camaras del viewport principal, y si la del jugador sale del
//  arbol (al reaparecer) Godot podria hacer actual la del visor. Su CFrame es
//  su transform (el puente generico de Node3D); el resto va por _gl_bridge.
// ════════════════════════════════════════════════════════════════════
class Camera : public Node3D {
    GDCLASS(Camera, Node3D);
    float _fov = 70.0f;
    Transform3D _foco;
    int _tipo = 0;   // Enum.CameraType.Fixed, el de Instance.new

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_FieldOfView", "v"), &Camera::set_FieldOfView);
        ClassDB::bind_method(D_METHOD("get_FieldOfView"),      &Camera::get_FieldOfView);
        ClassDB::bind_method(D_METHOD("set_Focus", "v"),       &Camera::set_Focus);
        ClassDB::bind_method(D_METHOD("get_Focus"),            &Camera::get_Focus);
        ClassDB::bind_method(D_METHOD("set_CameraType", "v"),  &Camera::set_CameraType);
        ClassDB::bind_method(D_METHOD("get_CameraType"),       &Camera::get_CameraType);
        ClassDB::bind_method(D_METHOD("get_NearPlaneZ"),       &Camera::get_NearPlaneZ);
        ADD_GROUP("Camera", "");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "FieldOfView", PROPERTY_HINT_RANGE, "1,120,0.01"), "set_FieldOfView", "get_FieldOfView");
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "Focus"), "set_Focus", "get_Focus");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "CameraType", PROPERTY_HINT_ENUM,
                     "Fixed,Attach,Watch,Track,Follow,Custom,Scriptable,Orbital"), "set_CameraType", "get_CameraType");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "NearPlaneZ", PROPERTY_HINT_NONE, "",
                     PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_NearPlaneZ");
    }

public:
    Camera() { set_meta("_gl_bridge", true); }
    // El rango de Roblox (1..120).
    void  set_FieldOfView(float v) { _fov = CLAMP(v, 1.0f, 120.0f); }
    float get_FieldOfView() const { return _fov; }
    void  set_Focus(const Transform3D& t) { _foco = t; }
    Transform3D get_Focus() const { return _foco; }
    void  set_CameraType(int t) { _tipo = t; }
    int   get_CameraType() const { return _tipo; }
    float get_NearPlaneZ() const { return -0.1f; }
};

// ════════════════════════════════════════════════════════════════════
//  WorldModel: un Model (PivotTo, GetBoundingBox...) que dentro de un
//  ViewportFrame deja trabajar a lo suyo (juntas y animaciones). Los rayos y
//  la fisica propia de Roblox aqui no estan. Avisa a su visor cuando le meten
//  algo, para que lo pase a su escenario en ese mismo frame.
// ════════════════════════════════════════════════════════════════════
class WorldModel : public RBXModel {
    GDCLASS(WorldModel, RBXModel);
protected:
    static void _bind_methods() {}
    void _notification(int p_what);
public:
    WorldModel() {}
};

// ════════════════════════════════════════════════════════════════════
//  ViewportFrame
// ════════════════════════════════════════════════════════════════════
class ViewportFrame : public RobloxFrame {
    GDCLASS(ViewportFrame, RobloxFrame);

    // ── Propiedades de Roblox (los defectos de Instance.new) ──
    Color   _ambient   = Color(200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f);
    Color   _luz_color = Color(140.0f / 255.0f, 140.0f / 255.0f, 140.0f / 255.0f);
    Vector3 _luz_dir   = Vector3(-1, -1, -1);
    Color   _img_color = Color(1, 1, 1);
    float   _img_transp = 0.0f;
    // CurrentCamera: el objeto y su ruta relativa. La ruta es lo que se guarda
    // en la escena, la que pone el importador y la que copia Clone(); el
    // ObjectID es la verdad en marcha (si la camara muere, sale nil).
    ObjectID _cam_id;
    NodePath _cam_ruta;

    // ── Retrato del menu (Subject) ──
    ObjectID _sujeto;
    bool     _retrato = false;
    double   _retrato_espera = -1.0;
    LocalVector<RID> _retrato_inst;
    LocalVector<Ref<Resource>> _retrato_refs;   // mallas y materiales vivos mientras se dibujan
    Ref<StyleBoxEmpty> _sin_caja;

    // ── Servidor de render ──
    RID _escena, _vp, _tex, _cam_rs, _entorno, _luz, _luz_inst;
    Vector2i _px;
    Vector2i _px_pedido;          // el tamaño nuevo que espera a quedarse quieto
    uint64_t _px_pedido_frame = 0;
    int   _modo = -1;
    bool  _hay_camara = false;
    Transform3D _cam_xf;
    float _cam_fov = -1.0f;

    // ── Contenido ──
    HashSet<uint64_t> _vis;      // instancias de render que ya estan en mi escenario
    bool _pendiente = false;
    int  _tick = 0;

    static StringName& _sn_entra() { static StringName s("tree_entered"); return s; }
    static StringName& _sn_metodo() { static StringName s("_gl_vf_vi_entra"); return s; }

    // ¿Se dibuja este visor en algun sitio? En un ScreenGui de PlayerGui/CoreGui
    // (lo de StarterGui y los almacenes son plantillas), o en un cartel del mundo.
    bool _gl_vf_en_pantalla() const {
        static const StringName SN_PG("PlayerGui"), SN_CG("CoreGui"), SN_SG("StarterGui");
        if (gl_vf_apagados()) return false;
        Node* p = get_parent();
        while (p && dynamic_cast<Control*>(p)) p = p->get_parent();
        if (!p) return false;
        const bool pantalla = dynamic_cast<ScreenGui*>(p) != nullptr;
        if (!pantalla && !p->is_class("SurfaceGui") && !p->is_class("BillboardGui")) return false;
        for (Node* a = p->get_parent(); a; a = a->get_parent()) {
            const StringName nm = a->get_name();
            if (nm == SN_PG || nm == SN_CG) return true;
            if (nm == SN_SG) return false;
        }
        return !pantalla && gl_en_workspace(p);
    }

    static Node* _raiz(const Node* n) {
        Node* r = const_cast<Node*>(n);
        while (r && r->get_parent()) r = r->get_parent();
        return r;
    }

    // ── Render ────────────────────────────────────────────────────────
    void _gl_vf_crear() {
        if (_escena.is_valid()) return;
        RenderingServer* rs = RenderingServer::get_singleton();
        _escena = rs->scenario_create();
        _entorno = rs->environment_create();
        rs->environment_set_background(_entorno, RenderingServer::ENV_BG_COLOR);
        rs->environment_set_bg_color(_entorno, Color(0, 0, 0, 0));
        rs->environment_set_tonemap(_entorno, RenderingServer::ENV_TONE_MAPPER_LINEAR, 1.0f, 1.0f);
        rs->environment_set_adjustment(_entorno, true, 1.0f, 1.0f, 1.0f, true, gl_vf_lut());
        rs->scenario_set_environment(_escena, _entorno);
        _luz = rs->directional_light_create();
        rs->light_set_shadow(_luz, false);
        rs->light_directional_set_sky_mode(_luz, RenderingServer::LIGHT_DIRECTIONAL_SKY_MODE_LIGHT_ONLY);
        _luz_inst = rs->instance_create2(_luz, _escena);
        _cam_rs = rs->camera_create();
        _vp = rs->viewport_create();
        rs->viewport_set_scenario(_vp, _escena);
        rs->viewport_attach_camera(_vp, _cam_rs);
        rs->viewport_set_transparent_background(_vp, true);
        rs->viewport_set_disable_2d(_vp, true);
        // El antialias del juego, y al menos 2x: un objeto pequeño sin suavizar
        // se ve dentado al lado de la UI.
        int msaa = RenderingServer::VIEWPORT_MSAA_2X;
        if (Viewport* v = get_viewport()) msaa = MAX(msaa, (int)v->get_msaa_3d());
        // A/B de rendimiento: GL_VF_MSAA=0..3 fuerza el nivel.
        static const String forzado = OS::get_singleton() ? OS::get_singleton()->get_environment("GL_VF_MSAA") : String();
        if (!forzado.is_empty()) msaa = CLAMP((int)forzado.to_int(), 0, 3);
        rs->viewport_set_msaa_3d(_vp, (RenderingServer::ViewportMSAA)msaa);
        rs->viewport_set_update_mode(_vp, RenderingServer::VIEWPORT_UPDATE_DISABLED);
        rs->viewport_set_active(_vp, true);
        _tex = rs->viewport_get_texture(_vp);
        _modo = RenderingServer::VIEWPORT_UPDATE_DISABLED;
        _px = Vector2i();
        _px_pedido = Vector2i();
        _cam_fov = -1.0f;
        _hay_camara = false;
        gl_vf_activos_ref()++;
        _gl_vf_luz();
        _gl_vf_visibilidad();
        if (_retrato) _retrato_espera = 0.0;
        _gl_vf_marcar();
    }

    // Solo servidor de render: vale tambien desde el destructor.
    void _gl_vf_soltar_rs() {
        RenderingServer* rs = RenderingServer::get_singleton();
        if (rs) {
            for (uint32_t i = 0; i < _retrato_inst.size(); i++) rs->free_rid(_retrato_inst[i]);
            if (_escena.is_valid()) {
                rs->free_rid(_luz_inst);
                rs->free_rid(_luz);
                rs->free_rid(_vp);
                rs->free_rid(_cam_rs);
                rs->free_rid(_entorno);
                rs->free_rid(_escena);
            }
        }
        _retrato_inst.clear();
        _retrato_refs.clear();
        if (_escena.is_valid()) gl_vf_activos_ref()--;
        _luz_inst = RID(); _luz = RID(); _vp = RID(); _cam_rs = RID();
        _entorno = RID(); _escena = RID(); _tex = RID();
        _px = Vector2i();
        _px_pedido = Vector2i();
        _modo = -1;
        _hay_camara = false;
        _cam_fov = -1.0f;
    }

    void _gl_vf_destruir() {
        _gl_vf_soltar_vis();
        _gl_vf_soltar_rs();
        set_process(false);
    }

    void _gl_vf_luz() {
        if (!_entorno.is_valid()) return;
        RenderingServer* rs = RenderingServer::get_singleton();
        // El retrato del menu mantiene su luz de siempre: clara, pareja y desde
        // la camara. Lo demas, la de Roblox: Ambient en lo que no da la luz y
        // Ambient + LightColor en lo que la mira de frente, sumadas en lineal
        // con gamma 2 como el resto del mundo.
        const Color amb = _retrato ? Color(0.85f, 0.85f, 0.85f) : _ambient;
        const Color luz = _retrato ? Color(0.55f, 0.55f, 0.55f) : _luz_color;
        const Vector3 dir = _retrato ? Vector3(0.0f, -0.34f, 0.94f) : _luz_dir;
        rs->environment_set_ambient_light(_entorno, gl_color_pieza(amb), RenderingServer::ENV_AMBIENT_SOURCE_COLOR,
                                          1.0f, 0.0f, RenderingServer::ENV_REFLECTION_SOURCE_DISABLED);
        rs->light_set_color(_luz, gl_color_pieza(luz));
        rs->light_set_param(_luz, RenderingServer::LIGHT_PARAM_ENERGY, 1.0f);
        rs->instance_set_transform(_luz_inst, Transform3D(gl_vf_mirando(dir), Vector3()));
    }

    // Tamaño del render = tamaño del frame en pixeles de pantalla (con el
    // escalado del lienzo y de los UIScale). Oculto: 0, que suelta la memoria.
    // Cambiar el tamaño rehace las texturas del render, asi que mientras el
    // frame se anima (un panel que se abre con un tween de UIScale o de Size)
    // no se rehace en cada frame: la imagen se estira y el tamaño nuevo se
    // aplica cuando lleva 8 frames quieto. Solo va de inmediato si se pasa de
    // o a nada, o si el render se ha quedado en menos de la mitad (borroso).
    void _gl_vf_tamano() {
        if (!_vp.is_valid()) return;
        Vector2i px;
        if (is_visible_in_tree()) {
            const Vector2 tam = get_size();
            Vector2 esc(1, 1);
            if (Viewport* v = get_viewport()) esc = (v->get_final_transform() * get_global_transform_with_canvas()).get_scale().abs();
            px.x = CLAMP((int)Math::round(tam.x * esc.x), 0, GL_VF_MAX_PX);
            px.y = CLAMP((int)Math::round(tam.y * esc.y), 0, GL_VF_MAX_PX);
        }
        if (px == _px) { _px_pedido = px; return; }
        const bool nada = _px.x <= 1 || _px.y <= 1 || px.x <= 1 || px.y <= 1;
        const bool borroso = px.x > _px.x * 2 || px.y > _px.y * 2;
        if (!nada && !borroso) {
            const uint64_t frame = Engine::get_singleton()->get_process_frames();
            if (px != _px_pedido) { _px_pedido = px; _px_pedido_frame = frame; return; }
            if (frame - _px_pedido_frame < 8) return;
        }
        _px = px;
        _px_pedido = px;
        RenderingServer::get_singleton()->viewport_set_size(_vp, px.x, px.y);
        _gl_vf_modo();
        queue_redraw();
    }

    // Se renderiza solo si hay algo que ver: camara, tamaño, imagen no del todo
    // transparente y el frame visible. Aun asi, WHEN_VISIBLE: si su textura no
    // se pinta (fuera de pantalla, recortado), el servidor no lo renderiza.
    void _gl_vf_modo() {
        if (!_vp.is_valid()) return;
        const bool dibuja = _hay_camara && _px.x > 1 && _px.y > 1 && _img_transp < 0.999f && is_visible_in_tree();
        const int modo = dibuja ? RenderingServer::VIEWPORT_UPDATE_WHEN_VISIBLE : RenderingServer::VIEWPORT_UPDATE_DISABLED;
        if (modo == _modo) return;
        _modo = modo;
        RenderingServer::get_singleton()->viewport_set_update_mode(_vp, (RenderingServer::ViewportUpdateMode)modo);
        queue_redraw();
    }

    void _gl_vf_visibilidad() {
        if (!_escena.is_valid()) return;
        const bool ve = is_visible_in_tree();
        _gl_vf_tamano();
        _gl_vf_camara();
        _gl_vf_modo();
        set_process(ve);
    }

    // La CurrentCamera, cada frame que se ve: el CFrame y el FieldOfView de la
    // camara (de cualquier Node3D; de una Camera3D, el suyo). Solo se habla con
    // el servidor si algo cambio.
    void _gl_vf_camara() {
        if (!_cam_rs.is_valid() || _retrato) return;
        Node3D* c = dynamic_cast<Node3D*>(gl_camara());
        const bool hay = c != nullptr;
        if (hay) {
            const Transform3D xf = gl_xf(c).orthonormalized();
            float fov = 70.0f;
            if (Camera* rc = dynamic_cast<Camera*>(c)) fov = rc->get_FieldOfView();
            else if (Camera3D* c3 = dynamic_cast<Camera3D*>(c)) fov = c3->get_fov();
            RenderingServer* rs = RenderingServer::get_singleton();
            if (!xf.is_equal_approx(_cam_xf) || !_hay_camara) { _cam_xf = xf; rs->camera_set_transform(_cam_rs, xf); }
            if (fov != _cam_fov) { _cam_fov = fov; rs->camera_set_perspective(_cam_rs, fov, 0.1f, 10000.0f); }
        }
        if (hay != _hay_camara) { _hay_camara = hay; _gl_vf_modo(); queue_redraw(); }
    }

    // La imagen va encima del fondo del Frame (Panel ya pinto su caja). Con
    // UICorner se recorta con el mismo radio, como en Roblox.
    void _gl_vf_dibujar() {
        if (!_tex.is_valid() || _modo != RenderingServer::VIEWPORT_UPDATE_WHEN_VISIBLE) return;
        const Color mod(_img_color.r, _img_color.g, _img_color.b, 1.0f - _img_transp);
        const Vector2 tam = get_size();
        RenderingServer* rs = RenderingServer::get_singleton();
        const RID ci = get_canvas_item();
        float radio = 0.0f;
        Ref<StyleBoxFlat> caja = get_theme_stylebox("panel");
        if (caja.is_valid()) radio = (float)caja->get_corner_radius(CORNER_TOP_LEFT);
        radio = MIN(radio, MIN(tam.x, tam.y) * 0.5f);
        if (radio < 0.5f) {
            rs->canvas_item_add_texture_rect(ci, Rect2(Vector2(), tam), _tex, false, mod);
            return;
        }
        // Rectangulo redondeado: 8 tramos por esquina, en orden y convexo.
        const int tramos = 8;
        PackedVector2Array pts, uvs;
        const Vector2 centros[4] = { Vector2(tam.x - radio, radio), Vector2(tam.x - radio, tam.y - radio),
                                     Vector2(radio, tam.y - radio), Vector2(radio, radio) };
        for (int e = 0; e < 4; e++) {
            const float a0 = (float)Math_PI * (-0.5f + 0.5f * e);
            for (int k = 0; k <= tramos; k++) {
                const float a = a0 + (float)Math_PI * 0.5f * k / tramos;
                const Vector2 p = centros[e] + Vector2(Math::cos(a), Math::sin(a)) * radio;
                pts.push_back(p);
                uvs.push_back(Vector2(p.x / MAX(tam.x, 1.0f), p.y / MAX(tam.y, 1.0f)));
            }
        }
        PackedColorArray cols;
        cols.push_back(mod);
        rs->canvas_item_add_polygon(ci, pts, cols, uvs, _tex);
    }

    void _gl_vf_tick(double dt) {
        if (!_escena.is_valid()) { set_process(false); return; }
        _gl_vf_tamano();
        _gl_vf_camara();
        if (_retrato && _retrato_espera >= 0.0) {
            _retrato_espera -= dt;
            if (_retrato_espera <= 0.0) _gl_vf_retrato();
        }
        // Repaso de seguridad: lo que aparece dentro sin que entre una pieza ni
        // cambien mis hijos (la cara de un Decal al llegar su imagen, un modelo
        // metido en un Model del visor...). Cada medio segundo; solo cuesta
        // recorrer el arbol del visor, el servidor solo oye lo nuevo.
        if (++_tick >= 30) { _tick = 0; _gl_vf_reasignar(); }
    }

    // ── Contenido: las instancias de render pasan a mi escenario ──────
    void _gl_vf_recorrer(Node* n, HashSet<uint64_t>& vistos) {
        RenderingServer* rs = RenderingServer::get_singleton();
        const int c = n->get_child_count(true);
        for (int i = 0; i < c; i++) {
            Node* h = n->get_child(i, true);
            // Un visor dentro de otro se ocupa de lo suyo; un SubViewport tiene
            // su propio mundo.
            if (!h || dynamic_cast<ViewportFrame*>(h) || dynamic_cast<SubViewport*>(h)) continue;
            // Solo geometria: las luces de Roblox no alumbran un visor (y fuera
            // de Workspace ya estan apagadas).
            if (GeometryInstance3D* g = dynamic_cast<GeometryInstance3D*>(h)) {
                const uint64_t id = (uint64_t)g->get_instance_id();
                vistos.insert(id);
                if (!_vis.has(id)) {
                    rs->instance_set_scenario(g->get_instance(), _escena);
                    const Callable cb = Callable(this, _sn_metodo()).bind((int64_t)id);
                    if (!g->is_connected(_sn_entra(), cb)) g->connect(_sn_entra(), cb);
                }
            }
            _gl_vf_recorrer(h, vistos);
        }
    }

    void _gl_vf_desconectar(uint64_t id) {
        Node* g = gl_cast<Node>(ObjectDB::get_instance(ObjectID(id)));
        if (!g) return;
        const Callable cb = Callable(this, _sn_metodo()).bind((int64_t)id);
        if (g->is_connected(_sn_entra(), cb)) g->disconnect(_sn_entra(), cb);
    }

    void _gl_vf_soltar_vis() {
        for (const uint64_t& id : _vis) _gl_vf_desconectar(id);
        _vis.clear();
    }

    // ── Retrato (Subject) ─────────────────────────────────────────────
    void _gl_vf_retrato_soltar() {
        RenderingServer* rs = RenderingServer::get_singleton();
        if (rs) for (uint32_t i = 0; i < _retrato_inst.size(); i++) rs->free_rid(_retrato_inst[i]);
        _retrato_inst.clear();
        _retrato_refs.clear();
    }

    void _gl_vf_retrato_copiar(Node* n, const Transform3D& inv, AABB& caja, bool& primero) {
        RenderingServer* rs = RenderingServer::get_singleton();
        const int c = n->get_child_count(true);
        for (int i = 0; i < c; i++) {
            Node* h = n->get_child(i, true);
            if (!h) continue;
            MeshInstance3D* mi = dynamic_cast<MeshInstance3D*>(h);
            Ref<Mesh> m = mi ? mi->get_mesh() : Ref<Mesh>();
            if (m.is_valid() && mi->is_visible_in_tree()) {
                const Transform3D xf = inv * gl_xf(mi);
                const RID inst = rs->instance_create2(m->get_rid(), _escena);
                rs->instance_set_transform(inst, xf);
                Ref<Material> mo = gl_luz_godot(mi->get_material_override());   // el visor tiene sus luces
                if (mo.is_valid()) {
                    rs->instance_geometry_set_material_override(inst, mo->get_rid());
                    _retrato_refs.push_back(mo);
                } else {
                    for (int s = 0; s < m->get_surface_count(); s++) {
                        Ref<Material> ma = mi->get_active_material(s);
                        if (ma.is_null()) continue;
                        rs->instance_set_surface_override_material(inst, s, ma->get_rid());
                        _retrato_refs.push_back(ma);
                    }
                }
                _retrato_refs.push_back(m);
                _retrato_inst.push_back(inst);
                const AABB a = xf.xform(m->get_aabb());
                if (primero) { caja = a; primero = false; } else caja = caja.merge(a);
            }
            _gl_vf_retrato_copiar(h, inv, caja, primero);
        }
    }

    // Una sola vez por Subject (como antes): si el personaje aun no tiene
    // mallas, se reintenta cada 0,6 s.
    void _gl_vf_retrato() {
        _gl_vf_retrato_soltar();
        _retrato_espera = 0.6;
        Node* s = gl_cast<Node>(ObjectDB::get_instance(_sujeto));
        if (!s || !s->is_inside_tree() || !_escena.is_valid()) return;
        Node3D* s3 = gl_cast<Node3D>(s);
        const Transform3D inv = s3 ? gl_xf(s3).affine_inverse() : Transform3D();
        AABB caja;
        bool primero = true;
        _gl_vf_retrato_copiar(s, inv, caja, primero);
        if (primero) return;
        _retrato_espera = -1.0;
        // Encuadre de retrato: la cara esta arriba; la camara, de FRENTE (el
        // personaje mira hacia -Z, convencion de Roblox).
        const Vector3 centro = caja.get_center();
        const float alto = MAX(caja.size.y, 0.5f);
        const Vector3 foco = centro + Vector3(0, alto * 0.25f, 0);
        Transform3D t;
        t.origin = foco + Vector3(0, 0, -alto * 1.35f);
        t = t.looking_at(foco, Vector3(0, 1, 0));
        RenderingServer* rs = RenderingServer::get_singleton();
        rs->camera_set_transform(_cam_rs, t);
        rs->camera_set_perspective(_cam_rs, 35.0f, 0.05f, 1000.0f);
        _hay_camara = true;
        _gl_vf_modo();
        queue_redraw();
    }

protected:
    // El retrato no lleva fondo ni borde (era un SubViewportContainer pelado).
    void _gl_estilo_puesto() override {
        if (!_retrato) return;
        if (_sin_caja.is_null()) _sin_caja.instantiate();
        add_theme_stylebox_override("panel", _sin_caja);
    }

    void _notification(int p_what) {
        switch (p_what) {
            case NOTIFICATION_ENTER_TREE: {
                if (Engine::get_singleton()->is_editor_hint()) break;
                // Los SubViewport que la clase vieja dejo guardados en escenas:
                // fuera (cada uno arrastraba un mundo 3D que se dibujaba).
                for (int i = get_child_count(true) - 1; i >= 0; i--) {
                    if (SubViewport* sv = gl_cast<SubViewport>(get_child(i, true))) {
                        remove_child(sv);
                        sv->queue_free();
                    }
                }
                if (_gl_vf_en_pantalla()) _gl_vf_crear();
            } break;
            case NOTIFICATION_EXIT_TREE: _gl_vf_destruir(); break;
            case NOTIFICATION_RESIZED: _gl_vf_tamano(); break;
            case NOTIFICATION_VISIBILITY_CHANGED: _gl_vf_visibilidad(); break;
            case NOTIFICATION_DRAW: _gl_vf_dibujar(); break;
            case NOTIFICATION_PROCESS: _gl_vf_tick(get_process_delta_time()); break;
            case NOTIFICATION_CHILD_ORDER_CHANGED: _gl_vf_marcar(); break;
            default: break;
        }
    }

    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_CurrentCamera", "v"),     &ViewportFrame::set_CurrentCamera);
        ClassDB::bind_method(D_METHOD("get_CurrentCamera"),          &ViewportFrame::get_CurrentCamera);
        ClassDB::bind_method(D_METHOD("set_Ambient", "v"),           &ViewportFrame::set_Ambient);
        ClassDB::bind_method(D_METHOD("get_Ambient"),                &ViewportFrame::get_Ambient);
        ClassDB::bind_method(D_METHOD("set_LightColor", "v"),        &ViewportFrame::set_LightColor);
        ClassDB::bind_method(D_METHOD("get_LightColor"),             &ViewportFrame::get_LightColor);
        ClassDB::bind_method(D_METHOD("set_LightDirection", "v"),    &ViewportFrame::set_LightDirection);
        ClassDB::bind_method(D_METHOD("get_LightDirection"),         &ViewportFrame::get_LightDirection);
        ClassDB::bind_method(D_METHOD("set_ImageColor3", "v"),       &ViewportFrame::set_ImageColor3);
        ClassDB::bind_method(D_METHOD("get_ImageColor3"),            &ViewportFrame::get_ImageColor3);
        ClassDB::bind_method(D_METHOD("set_ImageTransparency", "v"), &ViewportFrame::set_ImageTransparency);
        ClassDB::bind_method(D_METHOD("get_ImageTransparency"),      &ViewportFrame::get_ImageTransparency);
        // Las que usan el importador y el repintado desde metadatos (como ImageLabel).
        ClassDB::bind_method(D_METHOD("set_image_color", "r", "g", "b"), &ViewportFrame::set_image_color);
        ClassDB::bind_method(D_METHOD("set_image_transparency", "t"),    &ViewportFrame::set_image_transparency);
        ClassDB::bind_method(D_METHOD("SetSubject", "node"),         &ViewportFrame::set_subject);
        ClassDB::bind_method(D_METHOD("_gl_vf_reasignar"),           &ViewportFrame::_gl_vf_reasignar);
        ClassDB::bind_method(D_METHOD("_gl_vf_vi_entra", "id"),      &ViewportFrame::_gl_vf_vi_entra);
        ClassDB::bind_method(D_METHOD("gl_vf_activo"),               &ViewportFrame::gl_vf_activo);
        ClassDB::bind_method(D_METHOD("gl_vf_render_px"),            &ViewportFrame::gl_vf_render_px);
        ClassDB::bind_method(D_METHOD("gl_vf_en_escena"),            &ViewportFrame::gl_vf_en_escena);
        ClassDB::bind_method(D_METHOD("gl_vf_textura"),              &ViewportFrame::gl_vf_textura);
        ClassDB::bind_method(D_METHOD("gl_vf_modo"),                 &ViewportFrame::gl_vf_modo);
        ClassDB::bind_method(D_METHOD("gl_vf_viewport"),             &ViewportFrame::gl_vf_viewport);
        ADD_GROUP("ViewportFrame", "");
        ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "CurrentCamera"), "set_CurrentCamera", "get_CurrentCamera");
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,   "Ambient"),         "set_Ambient", "get_Ambient");
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,   "LightColor"),      "set_LightColor", "get_LightColor");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "LightDirection"),  "set_LightDirection", "get_LightDirection");
        ADD_PROPERTY(PropertyInfo(Variant::COLOR,   "ImageColor3"),     "set_ImageColor3", "get_ImageColor3");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,   "ImageTransparency", PROPERTY_HINT_RANGE, "0,1,0.01"),
                     "set_ImageTransparency", "get_ImageTransparency");
    }

public:
    ViewportFrame() {}
    ~ViewportFrame() { _gl_vf_soltar_rs(); }

    // ── CurrentCamera ─────────────────────────────────────────────────
    // nil si nunca se puso o si su camara ya no existe (como en Roblox). Lo
    // importado y lo clonado llega como ruta y se resuelve al usarlo.
    Node* gl_camara() const {
        if (_cam_id.is_valid()) {
            Node* c = gl_cast<Node>(ObjectDB::get_instance(_cam_id));
            if (!c) {
                ViewportFrame* yo = const_cast<ViewportFrame*>(this);
                yo->_cam_id = ObjectID();
                yo->_cam_ruta = NodePath();
            }
            return c;
        }
        // Una ruta relativa vale tambien fuera del arbol: `plantilla:Clone()`
        // ya tiene su camara antes de colgarlo de nada, como en Roblox.
        if (_cam_ruta.is_empty() || (_cam_ruta.is_absolute() && !is_inside_tree())) return nullptr;
        Node* c = get_node_or_null(_cam_ruta);
        if (c) const_cast<ViewportFrame*>(this)->_cam_id = c->get_instance_id();
        return c;
    }
    void gl_set_camara(Node* c) {
        _cam_id = c ? c->get_instance_id() : ObjectID();
        _cam_ruta = (c && _raiz(this) == _raiz(c)) ? get_path_to(c) : NodePath();
        _cam_fov = -1.0f;
        _gl_vf_camara();
        queue_redraw();
    }
    void set_CurrentCamera(const NodePath& p) {
        _cam_ruta = p;
        _cam_id = ObjectID();
        _cam_fov = -1.0f;
        if (is_inside_tree()) { _gl_vf_camara(); queue_redraw(); }
    }
    NodePath get_CurrentCamera() const {
        Node* c = gl_camara();
        if (!c) return _cam_id.is_valid() ? NodePath() : _cam_ruta;
        return _raiz(this) == _raiz(c) ? get_path_to(c) : _cam_ruta;
    }

    // ── Luz e imagen ──────────────────────────────────────────────────
    void set_Ambient(const Color& c) { _ambient = c; _gl_vf_luz(); }
    Color get_Ambient() const { return _ambient; }
    void set_LightColor(const Color& c) { _luz_color = c; _gl_vf_luz(); }
    Color get_LightColor() const { return _luz_color; }
    void set_LightDirection(const Vector3& d) { _luz_dir = d; _gl_vf_luz(); }
    Vector3 get_LightDirection() const { return _luz_dir; }
    void set_ImageColor3(const Color& c) { _img_color = Color(c.r, c.g, c.b); queue_redraw(); }
    Color get_ImageColor3() const { return _img_color; }
    void set_ImageTransparency(float t) { _img_transp = t; _gl_vf_modo(); queue_redraw(); }
    float get_ImageTransparency() const { return _img_transp; }
    void set_image_color(float r, float g, float b) { set_ImageColor3(Color(r, g, b)); }
    void set_image_transparency(float t) { set_ImageTransparency(t); }

    // ── Retrato del menu ──────────────────────────────────────────────
    void set_subject(Node* n) {
        _sujeto = n ? n->get_instance_id() : ObjectID();
        const bool era = _retrato;
        _retrato = n != nullptr;
        _gl_vf_retrato_soltar();
        _retrato_espera = _retrato ? 0.0 : -1.0;
        if (!_retrato) { _hay_camara = false; _cam_fov = -1.0f; }
        // Reponer la caja (con retrato, ninguna) y la luz que toca.
        if (era != _retrato) set_bg_alpha(get_bg_alpha());
        _gl_vf_luz();
        _gl_vf_camara();
        _gl_vf_modo();
        if (_escena.is_valid() && is_visible_in_tree()) set_process(true);
    }

    // ── Para el resto del motor ───────────────────────────────────────
    bool gl_vf_activo() const { return _escena.is_valid(); }
    Vector2i gl_vf_render_px() const { return _px; }
    // Cuantas instancias de render hay ahora en mi escenario, la textura del
    // render y su modo de actualizacion (las sondas).
    int gl_vf_en_escena() const { return (int)_vis.size(); }
    RID gl_vf_textura() const { return _tex; }
    int gl_vf_modo() const { return _modo; }
    RID gl_vf_viewport() const { return _vp; }

    // Algo entro en mi arbol: repaso al final del frame (antes de dibujar), una
    // vez aunque entren cien piezas.
    void _gl_vf_marcar() {
        if (_pendiente || !_escena.is_valid()) return;
        _pendiente = true;
        call_deferred("_gl_vf_reasignar");
    }

    void _gl_vf_reasignar() {
        _pendiente = false;
        if (!_escena.is_valid() || !is_inside_tree()) return;
        HashSet<uint64_t> vistos;
        _gl_vf_recorrer(this, vistos);
        // Lo que ya no cuelga de mi: sin conexion. (Lo que murio ya la perdio.)
        for (const uint64_t& id : _vis) if (!vistos.has(id)) _gl_vf_desconectar(id);
        _vis = vistos;
    }

    // Una de mis instancias volvio a entrar al arbol: Godot la acaba de poner en
    // el escenario del mundo. Si sigue colgando de mi, vuelve al mio ya mismo;
    // si se fue a otro sitio, deja de ser mia.
    void _gl_vf_vi_entra(int64_t id);
};

// ── Lo que usa roblox_part.h (declarado alli) ────────────────────────
// El visor del que cuelga `n`: el primero hacia arriba, sin pasar de
// Workspace. `bajo_worldmodel` dice si hay un WorldModel entre los dos.
inline Node* gl_vf_de(const Node* n, bool* bajo_worldmodel) {
    static const StringName SN_WS("Workspace");
    bool wm = false;
    Node* vf = nullptr;
    for (Node* p = n ? n->get_parent() : nullptr; p; p = p->get_parent()) {
        if (dynamic_cast<ViewportFrame*>(p)) { vf = p; break; }
        if (dynamic_cast<WorldModel*>(p)) wm = true;
        if (p->get_name() == SN_WS) break;
    }
    if (bajo_worldmodel) *bajo_worldmodel = vf != nullptr && wm;
    return vf;
}
inline bool gl_vf_dibuja(Node* vf) {
    const ViewportFrame* v = dynamic_cast<ViewportFrame*>(vf);
    return v && v->gl_vf_activo();
}
inline void gl_vf_pieza_entra(Node* vf) {
    if (ViewportFrame* v = dynamic_cast<ViewportFrame*>(vf)) v->_gl_vf_marcar();
}

inline void ViewportFrame::_gl_vf_vi_entra(int64_t id) {
    GeometryInstance3D* g = gl_cast<GeometryInstance3D>(ObjectDB::get_instance(ObjectID((uint64_t)id)));
    if (!g) { _vis.erase((uint64_t)id); return; }
    Node* dueno = gl_vf_de(g, nullptr);
    if (dueno == this && _escena.is_valid()) {
        RenderingServer::get_singleton()->instance_set_scenario(g->get_instance(), _escena);
        return;
    }
    _vis.erase((uint64_t)id);
    _gl_vf_desconectar((uint64_t)id);
    if (dueno) gl_vf_pieza_entra(dueno);
}

inline void WorldModel::_notification(int p_what) {
    if (p_what != NOTIFICATION_CHILD_ORDER_CHANGED || gl_vf_activos() == 0 || !is_inside_tree()) return;
    if (Node* vf = gl_vf_de(this, nullptr)) gl_vf_pieza_entra(vf);
}

// Registro (lo llama gl_registro_extra, con RobloxFrame y RBXModel ya dados de alta).
inline void gl_register_viewport_classes() {
    ClassDB::register_class<Camera>();
    ClassDB::register_class<WorldModel>();
    ClassDB::register_class<ViewportFrame>();
}

#endif // GL_VIEWPORT_FRAME_H
