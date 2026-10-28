#ifndef RBX_INSTANCES_H
#define RBX_INSTANCES_H

// Clases que existen en Roblox pero todavia no en GodotLuau. Sin ellas un place
// real pierde miles de objetos: Model, MeshPart, Texture, Decal, Bone y las
// mallas especiales.
//
// La mas importante es RBXInstance: representa CUALQUIER clase sin equivalente
// nativo, de modo que ninguna instancia se pierde y el arbol queda igual al de
// Roblox. Asi los scripts que hacen FindFirstChild/WaitForChild siguen
// encontrando lo que buscan.

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include "gl_caras.h"      // Decal/Texture: la cara de la pieza
#include "gl_clone.h"      // GL_META_INTERNO
#include "roblox_gui.h"    // gl_gui_load_image: la imagen del Decal

#include "roblox_part.h"   // RobloxPart, base de RBXMeshPart

using namespace godot;

// ── Instancia generica ──────────────────────────────────────────────
class RBXInstance : public Node {
    GDCLASS(RBXInstance, Node);

    String rbx_class_name = "Instance";

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_rbx_class_name", "n"), &RBXInstance::set_rbx_class_name);
        ClassDB::bind_method(D_METHOD("get_rbx_class_name"),      &RBXInstance::get_rbx_class_name);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "ClassName"),
                     "set_rbx_class_name", "get_rbx_class_name");
    }

public:
    // Es el comodin del importador para las clases de Roblox que aqui todavia
    // no existen. Se marca como PUENTE para que sus propiedades se guarden y se
    // lean como metadatos en vez de reventar: en Roblox esas propiedades SI
    // existen, y un script que lee una de ellas no deberia morir por eso.
    RBXInstance() { set_meta("_gl_bridge", true); }
    void set_rbx_class_name(const String &n) { rbx_class_name = n; }
    String get_rbx_class_name() const { return rbx_class_name; }
};

// ── Model ───────────────────────────────────────────────────────────
class RBXModel : public Node3D {
    GDCLASS(RBXModel, Node3D);

    NodePath primary_part;
    Transform3D world_pivot;
    float scale_factor = 1.0f;

    // Un Model recien clonado TODAVIA no esta en el arbol (el idioma normal es
    // clonar, colocar y luego asignar Parent). Fuera del arbol, las funciones
    // "global" de Godot avisan por consola y devuelven la identidad, asi que en
    // ese caso se usa el transform local, que es lo que Roblox conserva.
    static Transform3D _xf(const Node3D *n) {
        if (!n) return Transform3D();
        return n->is_inside_tree() ? n->get_global_transform() : n->get_transform();
    }

    void _collect_aabb(Node *n, AABB &box, bool &first) const {
        RobloxPart *p = gl_cast<RobloxPart>(n);
        if (p) {
            AABB b(_xf(p).origin - p->get_size() * 0.5f, p->get_size());
            if (first) { box = b; first = false; }
            else box = box.merge(b);
        }
        for (int k = 0; k < n->get_child_count(); k++)
            _collect_aabb(n->get_child(k), box, first);
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_primary_part", "p"), &RBXModel::set_primary_part);
        ClassDB::bind_method(D_METHOD("get_primary_part"),      &RBXModel::get_primary_part);
        ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "PrimaryPart"),
                     "set_primary_part", "get_primary_part");

        ClassDB::bind_method(D_METHOD("set_world_pivot", "t"), &RBXModel::set_world_pivot);
        ClassDB::bind_method(D_METHOD("get_world_pivot"),      &RBXModel::get_world_pivot);
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "WorldPivot"),
                     "set_world_pivot", "get_world_pivot");

        ClassDB::bind_method(D_METHOD("set_scale_factor", "f"), &RBXModel::set_scale_factor);
        ClassDB::bind_method(D_METHOD("get_scale_factor"),      &RBXModel::get_scale_factor);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ScaleFactor"),
                     "set_scale_factor", "get_scale_factor");

        ClassDB::bind_method(D_METHOD("GetPivot"),           &RBXModel::get_pivot);
        ClassDB::bind_method(D_METHOD("PivotTo", "t"),       &RBXModel::pivot_to);
        ClassDB::bind_method(D_METHOD("MoveTo", "pos"),      &RBXModel::move_to);
        ClassDB::bind_method(D_METHOD("GetBoundingBox"),     &RBXModel::get_bounding_box);
        ClassDB::bind_method(D_METHOD("GetExtentsSize"),     &RBXModel::get_extents_size);
    }

public:
    void set_primary_part(const NodePath &p) { primary_part = p; }
    NodePath get_primary_part() const { return primary_part; }
    void set_world_pivot(const Transform3D &t) { world_pivot = t; }
    Transform3D get_world_pivot() const { return world_pivot; }
    void set_scale_factor(float f) { scale_factor = f; }
    float get_scale_factor() const { return scale_factor; }

    // Si hay PrimaryPart el pivote es el suyo, como en Roblox.
    Transform3D get_pivot() const {
        if (!primary_part.is_empty()) {
            Node3D *pp = gl_cast<Node3D>(get_node_or_null(primary_part));
            if (pp) return _xf(pp);
        }
        return world_pivot;
    }

    // ── PivotTo 1:1 con Roblox ───────────────────────────────────────
    // Roblox mueve las PIEZAS: un Model no tiene posicion propia. Antes aqui se
    // movia el nodo del Model, y eso fallaba con todo lo que cuelga de una
    // Folder, porque en Godot un Node3D cuyo padre no es Node3D va en
    // coordenadas del mundo y no hereda nada. Build a Mountain clona su tema
    // (montanas del fondo, bosque, nubes, aldea) y hace
    // copia:PivotTo(CFrame.identity) para traerlo de z=11500 al mapa: el tema
    // entero se quedaba a 11.500 studs y no se veia.
    // Ahora el nodo del Model no se toca (sigue siendo identidad, como asume
    // ScaleTo) y el desplazamiento va a cada raiz espacial: los Node3D que no
    // arrastra ningun padre, o sea los hijos directos del Model y los de una
    // Folder. Los hijos de otro Node3D de dentro ya le siguen solos.
    void _mover_raices(Node *n, const Transform3D &delta, const Transform3D &m, const Transform3D &m_inv) {
        const bool n_es_3d = gl_cast<Node3D>(n) != nullptr;
        for (int k = 0; k < n->get_child_count(); k++) {
            Node *h = n->get_child(k);
            if (Node3D *h3 = gl_cast<Node3D>(h)) {
                if (n == this) h3->set_transform(m_inv * delta * m * h3->get_transform());
                else if (!n_es_3d) h3->set_transform(delta * h3->get_transform());
            }
            _mover_raices(h, delta, m, m_inv);
        }
    }

    void pivot_to(const Transform3D &t) {
        const Transform3D delta = t * get_pivot().affine_inverse();
        const Transform3D m = _xf(this);
        _mover_raices(this, delta, m, m.affine_inverse());
        world_pivot = t;
    }

    void move_to(const Vector3 &pos) {
        Transform3D t = get_pivot();
        t.origin = pos;
        pivot_to(t);
    }

    AABB get_bounding_box() const {
        AABB box; bool first = true;
        for (int k = 0; k < get_child_count(); k++)
            _collect_aabb(get_child(k), box, first);
        return box;
    }

    Vector3 get_extents_size() const { return get_bounding_box().size; }
};

// ── MeshPart ────────────────────────────────────────────────────────
// Hereda toda la fisica y el material de RobloxPart. La malla de verdad vive
// en la nube de Roblox: se dibuja si esta en assets/rbx/<id> (la deja ahi el
// importador de .gdlu, o el usuario a mano; ver gl_malla_propia.h). Si no, la
// pieza se queda con su caja y el id queda apuntado como asset que falta.
class RBXMeshPart : public RobloxPart {
    GDCLASS(RBXMeshPart, RobloxPart);

    String mesh_id, texture_id;
    String malla_csg;   // Union: la malla horneada de sus piezas (rbx_uniones.h)
    Vector3 mesh_size = Vector3(1, 1, 1);
    int render_fidelity = 0;
    int collision_fidelity = 0;
    bool double_sided = false;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_mesh_id", "s"), &RBXMeshPart::set_mesh_id);
        ClassDB::bind_method(D_METHOD("get_mesh_id"),      &RBXMeshPart::get_mesh_id);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "MeshId"), "set_mesh_id", "get_mesh_id");

        // Se guarda en la escena pero no sale en el inspector: es la ruta de la
        // malla que el importador horneo para esta union.
        ClassDB::bind_method(D_METHOD("set_gl_malla_csg", "s"), &RBXMeshPart::set_gl_malla_csg);
        ClassDB::bind_method(D_METHOD("get_gl_malla_csg"),      &RBXMeshPart::get_gl_malla_csg);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "gl_malla_csg", PROPERTY_HINT_NONE, "",
                     PROPERTY_USAGE_STORAGE), "set_gl_malla_csg", "get_gl_malla_csg");

        ClassDB::bind_method(D_METHOD("set_texture_id", "s"), &RBXMeshPart::set_texture_id);
        ClassDB::bind_method(D_METHOD("get_texture_id"),      &RBXMeshPart::get_texture_id);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "TextureId"), "set_texture_id", "get_texture_id");
        ClassDB::bind_method(D_METHOD("_gl_textura_lista"),   &RBXMeshPart::_gl_textura_lista);

        ClassDB::bind_method(D_METHOD("set_mesh_size", "v"), &RBXMeshPart::set_mesh_size);
        ClassDB::bind_method(D_METHOD("get_mesh_size"),      &RBXMeshPart::get_mesh_size);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "MeshSize"), "set_mesh_size", "get_mesh_size");

        ClassDB::bind_method(D_METHOD("set_render_fidelity", "i"), &RBXMeshPart::set_render_fidelity);
        ClassDB::bind_method(D_METHOD("get_render_fidelity"),      &RBXMeshPart::get_render_fidelity);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "RenderFidelity"),
                     "set_render_fidelity", "get_render_fidelity");

        ClassDB::bind_method(D_METHOD("set_collision_fidelity", "i"), &RBXMeshPart::set_collision_fidelity);
        ClassDB::bind_method(D_METHOD("get_collision_fidelity"),      &RBXMeshPart::get_collision_fidelity);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "CollisionFidelity"),
                     "set_collision_fidelity", "get_collision_fidelity");

        ClassDB::bind_method(D_METHOD("set_double_sided", "b"), &RBXMeshPart::set_double_sided);
        ClassDB::bind_method(D_METHOD("get_double_sided"),      &RBXMeshPart::get_double_sided);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "DoubleSided"),
                     "set_double_sided", "get_double_sided");
    }

    void _cargar_malla() {
        const bool es_union = !malla_csg.is_empty();
        const Ref<Mesh> m = gl_malla_local(es_union ? malla_csg : mesh_id);
        // CollisionFidelity: Default 0, Hull 1, Box 2, PreciseConvexDecomposition 3.
        gl_malla_propia(m, true, collision_fidelity == 2);
        const Ref<ArrayMesh> am = m;
        gl_malla_con_colores(am.is_valid() && am->get_surface_count() > 0 &&
                             (am->surface_get_format(0) & Mesh::ARRAY_FORMAT_COLOR));
        if (es_union) return;
        if (m.is_valid()) { if (has_meta("__rbx_missing_asset")) remove_meta("__rbx_missing_asset"); }
        else if (!mesh_id.is_empty()) set_meta("__rbx_missing_asset", mesh_id);
    }

public:
    void set_mesh_id(const String &s) {
        mesh_id = s;
        _cargar_malla();
    }
    String get_mesh_id() const { return mesh_id; }
    void set_gl_malla_csg(const String &s) { malla_csg = s; _cargar_malla(); }
    String get_gl_malla_csg() const { return malla_csg; }
    void set_texture_id(const String &s) {
        texture_id = s;
        // TextureID manda sobre una EditableImage puesta en TextureContent.
        static const StringName k_obj("_glc_TextureContent");
        if (has_meta(k_obj)) remove_meta(k_obj);
        // Por streaming (gl_streaming_texturas.h): llega sola y con el detalle
        // que pide la distancia.
        gl_textura_de_malla(gl_textura_local_3d(s, this, get_size().length() * 0.5f));
    }
    String get_texture_id() const { return texture_id; }
    // Llego la textura pedida por streaming.
    void _gl_textura_lista() {
        static const StringName k_obj("_glc_TextureContent");
        if (texture_id.is_empty() || has_meta(k_obj)) return;   // manda la EditableImage
        gl_textura_de_malla(gl_textura_local_3d(texture_id, this, get_size().length() * 0.5f));
    }
    void set_mesh_size(const Vector3 &v) { mesh_size = v; }
    Vector3 get_mesh_size() const { return mesh_size; }
    void set_render_fidelity(int i) { render_fidelity = i; }
    int get_render_fidelity() const { return render_fidelity; }
    void set_collision_fidelity(int i) {
        if (collision_fidelity == i) return;
        collision_fidelity = i;
        if (gl_malla_propia_actual().is_valid()) _cargar_malla();
    }
    int get_collision_fidelity() const { return collision_fidelity; }
    void set_double_sided(bool b) { double_sided = b; }
    bool get_double_sided() const { return double_sided; }
};

// ── Decal y Texture: un quad sobre una cara de la pieza ──────────────
// Antes solo guardaban sus propiedades: ni los importados ni los creados por
// script se dibujaban. Ahora son un quad pegado a la cara (Face) de la pieza
// padre, con su imagen (copia local, cache o descarga, como un ImageLabel).
// Mientras la imagen no llega no se pinta nada, como en Roblox. Si la pieza
// cambia de Size despues, el quad no se entera hasta volver a tocar la
// calcomania (pasa poco: casi siempre se pegan a piezas ya hechas).
struct GLCalco {
    MeshInstance3D*         malla = nullptr;
    Ref<QuadMesh>           quad;
    Ref<StandardMaterial3D> mat;
    Ref<ShaderMaterial>     mat_luz;   // el de la luz propia (core/gl_luz_propia.h), si esta activa
};
// `directa`: la textura de una EditableImage (TextureContent), que manda
// sobre el texto.
inline void gl_calco_dibujar(Node3D* nodo, GLCalco& c, const String& textura, int cara,
                             const Color& color, float transparencia, int z,
                             bool mosaico, const Vector2& studs, const Vector2& desplaza,
                             const Ref<Texture2D>& directa = Ref<Texture2D>()) {
    Vector3 tam;
    Node* padre = nodo->get_parent();
    if (padre && padre->is_class("RobloxPart")) tam = (Vector3)padre->get("Size");
    Ref<Texture2D> tx;
    if (tam != Vector3()) tx = directa;
    if (tx.is_null() && !textura.is_empty() && tam != Vector3()) {
        // Por streaming 3D (gl_streaming_texturas.h): el PNG suelto llega solo,
        // con mipmaps (antes, sin ellos, de lejos titilaba) y con el detalle que
        // pide la distancia. Lo importado o descargado, como un ImageLabel.
        String cruda = gl_is_roblox_asset(textura) ? gl_local_asset_file(textura) : String();
        if (cruda.is_empty() && gl_is_roblox_asset(textura)) cruda = gl_cache_asset_path(textura);
        if (!cruda.is_empty() && gl_tex_streaming_activo() && gl_tex_sabe_esperar(nodo) &&
            !(gl_tex_motor_streaming() && !gl_local_asset_path(textura).is_empty()))
            tx = gl_tex_obtener_3d(cruda, nodo, MAX(tam.x, MAX(tam.y, tam.z)) * 0.5f);
        else
            tx = gl_gui_load_image(nodo, textura);
    }
    if (tx.is_null() || tx.ptr() == gl_tex_blanca().ptr()) {
        if (c.malla) c.malla->set_visible(false);
        return;
    }
    if (!c.malla) {
        c.quad.instantiate();
        c.mat.instantiate();
        c.mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        c.quad->set_material(c.mat);
        c.malla = memnew(MeshInstance3D);
        c.malla->set_meta(GL_META_INTERNO, true);
        c.malla->set_mesh(c.quad);
        c.malla->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        nodo->add_child(c.malla, false, Node::INTERNAL_MODE_BACK);
    }
    const Vector3 n = gl_normal_de_cara(cara);
    const Vector2 dims = Math::abs(n.x) > 0.5f ? Vector2(tam.z, tam.y)
                       : (Math::abs(n.y) > 0.5f ? Vector2(tam.x, tam.z) : Vector2(tam.x, tam.y));
    c.quad->set_size(dims);
    // El quad mira a +Z: se gira para que +Z sea la normal de la cara, y se
    // separa un pelo (mas cuanto mayor el ZIndex) para no pelearse con ella.
    const Vector3 arriba = Math::abs(n.y) > 0.99f ? Vector3(0, 0, -1) : Vector3(0, 1, 0);
    c.malla->set_transform(Transform3D(Basis::looking_at(-n, arriba),
                                       n * (gl_media_cara(tam, cara) + 0.01f + 0.001f * (float)CLAMP(z, 0, 100))));
    // El tinte Color3 pasa a lineal con gamma 2, como el Color de una pieza.
    Color col = gl_color_pieza(color);
    col.a = Math::clamp(1.0f - transparencia, 0.0f, 1.0f);
    c.mat->set_albedo(col);
    c.mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tx);
    c.mat->set_render_priority(CLAMP(z, -128, 127));
    if (mosaico) {
        const Vector2 s(Math::max(studs.x, 0.01f), Math::max(studs.y, 0.01f));
        c.mat->set_uv1_scale(Vector3(dims.x / s.x, dims.y / s.y, 1.0f));
        c.mat->set_uv1_offset(Vector3(desplaza.x / s.x, desplaza.y / s.y, 0.0f));
    }
    // Con la luz propia, la misma cuenta que la pieza de debajo (y su sombra).
    if (gl_luz_propia_activa()) c.quad->set_material(gl_luz_de(c.mat, &c.mat_luz));
    else if (c.quad->get_material() != c.mat) c.quad->set_material(c.mat);
    c.malla->set_visible(true);
}

// ── Texture (tileada sobre una cara) ────────────────────────────────
class RBXTexture : public Node3D {
    GDCLASS(RBXTexture, Node3D);

    String texture;
    int face = 5;                 // Front
    float studs_u = 2.0f, studs_v = 2.0f;
    float offset_u = 0.0f, offset_v = 0.0f;
    float transparency = 0.0f;
    Color color = Color(1, 1, 1);
    int z_index = 1;
    GLCalco calco;
    Ref<Texture2D> tex_objeto;    // TextureContent con una EditableImage

    void _dibujar() {
        if (is_inside_tree())
            gl_calco_dibujar(this, calco, texture, face, color, transparency, z_index,
                             true, Vector2(studs_u, studs_v), Vector2(offset_u, offset_v), tex_objeto);
    }
    // Llego la textura pedida por streaming.
    void _gl_textura_lista() { _dibujar(); }

protected:
    void _notification(int p_what) { if (p_what == NOTIFICATION_ENTER_TREE) _dibujar(); }
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_textura_lista"), &RBXTexture::_gl_textura_lista);
        ClassDB::bind_method(D_METHOD("set_texture_str", "s"), &RBXTexture::set_texture_str);
        ClassDB::bind_method(D_METHOD("get_texture_str"),      &RBXTexture::get_texture_str);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "Texture"), "set_texture_str", "get_texture_str");

        ClassDB::bind_method(D_METHOD("set_face", "i"), &RBXTexture::set_face);
        ClassDB::bind_method(D_METHOD("get_face"),      &RBXTexture::get_face);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "Face", PROPERTY_HINT_ENUM,
                     "Right,Top,Back,Left,Bottom,Front"), "set_face", "get_face");

        ClassDB::bind_method(D_METHOD("set_studs_u", "f"), &RBXTexture::set_studs_u);
        ClassDB::bind_method(D_METHOD("get_studs_u"),      &RBXTexture::get_studs_u);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "StudsPerTileU"), "set_studs_u", "get_studs_u");

        ClassDB::bind_method(D_METHOD("set_studs_v", "f"), &RBXTexture::set_studs_v);
        ClassDB::bind_method(D_METHOD("get_studs_v"),      &RBXTexture::get_studs_v);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "StudsPerTileV"), "set_studs_v", "get_studs_v");

        ClassDB::bind_method(D_METHOD("set_offset_u", "f"), &RBXTexture::set_offset_u);
        ClassDB::bind_method(D_METHOD("get_offset_u"),      &RBXTexture::get_offset_u);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "OffsetStudsU"), "set_offset_u", "get_offset_u");

        ClassDB::bind_method(D_METHOD("set_offset_v", "f"), &RBXTexture::set_offset_v);
        ClassDB::bind_method(D_METHOD("get_offset_v"),      &RBXTexture::get_offset_v);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "OffsetStudsV"), "set_offset_v", "get_offset_v");

        ClassDB::bind_method(D_METHOD("set_transparency", "f"), &RBXTexture::set_transparency);
        ClassDB::bind_method(D_METHOD("get_transparency"),      &RBXTexture::get_transparency);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Transparency"),
                     "set_transparency", "get_transparency");

        ClassDB::bind_method(D_METHOD("set_color3", "c"), &RBXTexture::set_color3);
        ClassDB::bind_method(D_METHOD("get_color3"),      &RBXTexture::get_color3);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR, "Color3"), "set_color3", "get_color3");

        ClassDB::bind_method(D_METHOD("set_z_index", "i"), &RBXTexture::set_z_index);
        ClassDB::bind_method(D_METHOD("get_z_index"),      &RBXTexture::get_z_index);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "ZIndex"), "set_z_index", "get_z_index");

        // El fetcher de assets llama set_image al terminar la descarga.
        ClassDB::bind_method(D_METHOD("set_image", "id"), &RBXTexture::set_image);
        ClassDB::bind_method(D_METHOD("gl_set_textura_objeto", "t"), &RBXTexture::gl_set_textura_objeto);
    }

public:
    // Texture = "..." quita la EditableImage que hubiera en TextureContent.
    void set_texture_str(const String &s) {
        texture = s;
        tex_objeto.unref();
        static const StringName k_obj("_glc_TextureContent");
        if (has_meta(k_obj)) remove_meta(k_obj);
        _dibujar();
    }
    void gl_set_textura_objeto(const Ref<Texture2D>& t) { tex_objeto = t; _dibujar(); }
    String get_texture_str() const { return texture; }
    void set_face(int i) { face = CLAMP(i, 0, 5); _dibujar(); }
    int get_face() const { return face; }
    void set_studs_u(float f) { studs_u = f; _dibujar(); }
    float get_studs_u() const { return studs_u; }
    void set_studs_v(float f) { studs_v = f; _dibujar(); }
    float get_studs_v() const { return studs_v; }
    void set_offset_u(float f) { offset_u = f; _dibujar(); }
    float get_offset_u() const { return offset_u; }
    void set_offset_v(float f) { offset_v = f; _dibujar(); }
    float get_offset_v() const { return offset_v; }
    void set_transparency(float f) { transparency = f; _dibujar(); }
    float get_transparency() const { return transparency; }
    void set_color3(const Color &c) { color = c; _dibujar(); }
    Color get_color3() const { return color; }
    void set_z_index(int i) { z_index = i; _dibujar(); }
    int get_z_index() const { return z_index; }
    void set_image(const String& id) { if (id == texture) _dibujar(); }
    RBXTexture() { set_meta("_gl_bridge", true); set_meta("_gl_efecto", true); }
};

// ── Decal (sin tiling) ──────────────────────────────────────────────
class RBXDecal : public Node3D {
    GDCLASS(RBXDecal, Node3D);

    String texture;
    int face = 5;
    float transparency = 0.0f;
    Color color = Color(1, 1, 1);
    int z_index = 1;
    GLCalco calco;
    Ref<Texture2D> tex_objeto;    // TextureContent con una EditableImage

    void _dibujar() {
        if (is_inside_tree())
            gl_calco_dibujar(this, calco, texture, face, color, transparency, z_index,
                             false, Vector2(), Vector2(), tex_objeto);
    }
    // Llego la textura pedida por streaming.
    void _gl_textura_lista() { _dibujar(); }

protected:
    void _notification(int p_what) { if (p_what == NOTIFICATION_ENTER_TREE) _dibujar(); }
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_textura_lista"), &RBXDecal::_gl_textura_lista);
        ClassDB::bind_method(D_METHOD("set_texture_str", "s"), &RBXDecal::set_texture_str);
        ClassDB::bind_method(D_METHOD("get_texture_str"),      &RBXDecal::get_texture_str);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "Texture"), "set_texture_str", "get_texture_str");

        ClassDB::bind_method(D_METHOD("set_face", "i"), &RBXDecal::set_face);
        ClassDB::bind_method(D_METHOD("get_face"),      &RBXDecal::get_face);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "Face", PROPERTY_HINT_ENUM,
                     "Right,Top,Back,Left,Bottom,Front"), "set_face", "get_face");

        ClassDB::bind_method(D_METHOD("set_transparency", "f"), &RBXDecal::set_transparency);
        ClassDB::bind_method(D_METHOD("get_transparency"),      &RBXDecal::get_transparency);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "Transparency"),
                     "set_transparency", "get_transparency");

        ClassDB::bind_method(D_METHOD("set_color3", "c"), &RBXDecal::set_color3);
        ClassDB::bind_method(D_METHOD("get_color3"),      &RBXDecal::get_color3);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR, "Color3"), "set_color3", "get_color3");

        ClassDB::bind_method(D_METHOD("set_z_index", "i"), &RBXDecal::set_z_index);
        ClassDB::bind_method(D_METHOD("get_z_index"),      &RBXDecal::get_z_index);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "ZIndex"), "set_z_index", "get_z_index");

        ClassDB::bind_method(D_METHOD("set_image", "id"), &RBXDecal::set_image);
        ClassDB::bind_method(D_METHOD("gl_set_textura_objeto", "t"), &RBXDecal::gl_set_textura_objeto);
    }

public:
    // Texture = "..." quita la EditableImage que hubiera en TextureContent.
    void set_texture_str(const String &s) {
        texture = s;
        tex_objeto.unref();
        static const StringName k_obj("_glc_TextureContent");
        if (has_meta(k_obj)) remove_meta(k_obj);
        _dibujar();
    }
    void gl_set_textura_objeto(const Ref<Texture2D>& t) { tex_objeto = t; _dibujar(); }
    String get_texture_str() const { return texture; }
    void set_face(int i) { face = CLAMP(i, 0, 5); _dibujar(); }
    int get_face() const { return face; }
    void set_transparency(float f) { transparency = f; _dibujar(); }
    float get_transparency() const { return transparency; }
    void set_color3(const Color &c) { color = c; _dibujar(); }
    Color get_color3() const { return color; }
    void set_z_index(int i) { z_index = i; _dibujar(); }
    int get_z_index() const { return z_index; }
    void set_image(const String& id) { if (id == texture) _dibujar(); }
    RBXDecal() { set_meta("_gl_bridge", true); set_meta("_gl_efecto", true); }
};

// ── SpecialMesh / CylinderMesh / BlockMesh ──────────────────────────
// En Roblox cambian como SE VE la pieza que las contiene: Scale y Offset solo
// tocan el dibujo (antes se multiplicaba el Size de la pieza, que es fisica y
// lo leen los scripts, y un :Clone() lo volvia a multiplicar). La forma si se
// le pasa a la pieza (Sphere, Cylinder, Wedge). Al quitar la malla, la pieza
// vuelve a ser como era.
class RBXMesh : public Node3D {
    GDCLASS(RBXMesh, Node3D);

    int mesh_type = 6;            // Brick
    String mesh_id, texture_id;
    Vector3 mesh_scale = Vector3(1, 1, 1);
    Vector3 offset;
    Vector3 vertex_color = Vector3(1, 1, 1);
    uint64_t pieza_id = 0;        // a que pieza se le aplico
    int forma_original = -1;      // su Shape antes de la malla
    bool puso_textura = false;    // le puso su TextureId a la pieza

    RobloxPart* _pieza() const {
        return pieza_id ? gl_cast<RobloxPart>(ObjectDB::get_instance(ObjectID(pieza_id))) : nullptr;
    }
    void _deshacer() {
        if (RobloxPart* p = _pieza()) {
            p->gl_malla_especial(Vector3(1, 1, 1), Vector3());
            p->gl_malla_propia(Ref<Mesh>(), true);
            if (puso_textura) p->gl_textura_de_malla(Ref<Texture2D>());
            p->gl_sin_malla_de_nube(false);
            if (forma_original >= 0 && p->get_roblox_shape() != forma_original) p->set_roblox_shape(forma_original);
        }
        pieza_id = 0;
        forma_original = -1;
        puso_textura = false;
    }
    void _aplicar() {
        if (!is_inside_tree() || Engine::get_singleton()->is_editor_hint()) return;
        RobloxPart *p = gl_cast<RobloxPart>(get_parent());
        if (!p) { _deshacer(); return; }
        if ((uint64_t)p->get_instance_id() != pieza_id) {
            _deshacer();
            pieza_id = (uint64_t)p->get_instance_id();
            forma_original = p->get_roblox_shape();
        }
        // ── FileMesh: la caja NO se dibuja ───────────────────────────────
        // MeshType 5 = FileMesh: la geometria de verdad es una malla de la nube
        // (rbxassetid://) que SUSTITUYE a la caja. Mientras no se descarguen
        // las mallas, no dibujar es mucho mejor que dibujar una caja: medido en
        // el place de prueba, los accesorios del NPC Jandel salian como losas
        // grises de cientos de studs cruzando el cielo.
        // Con la malla en assets/rbx/<id> (gl_malla_propia.h) se dibuja ELLA, a su
        // tamaño por Scale; sin ella, nada.
        const Ref<Mesh> malla = (mesh_type == 5) ? gl_malla_local(mesh_id) : Ref<Mesh>();
        p->gl_sin_malla_de_nube(mesh_type == 5 && malla.is_null());
        p->gl_malla_propia(malla, false);
        if (malla.is_valid() && !texture_id.is_empty()) {
            p->gl_textura_de_malla(gl_textura_local_3d(texture_id, this, p->get_size().length() * 0.5f));
            puso_textura = true;
        }
        if (mesh_type == 5) { p->gl_malla_especial(mesh_scale, offset); return; }
        int forma = forma_original;
        switch (mesh_type) {
            case 3: forma = 1; break;   // Sphere -> Ball
            case 4: forma = 2; break;   // Cylinder
            case 2: forma = 3; break;   // Wedge
            default: break;             // Head/Torso/Brick: la de la pieza
        }
        if (p->get_roblox_shape() != forma) p->set_roblox_shape(forma);
        p->gl_malla_especial(mesh_scale, offset);
    }

    // Llego la textura pedida por streaming (gl_streaming_texturas.h).
    void _gl_textura_lista() {
        RobloxPart* p = _pieza();
        if (p && puso_textura && !texture_id.is_empty())
            p->gl_textura_de_malla(gl_textura_local_3d(texture_id, this, p->get_size().length() * 0.5f));
    }

protected:
    void _notification(int what) {
        if (what == NOTIFICATION_ENTER_TREE) _aplicar();
        else if (what == NOTIFICATION_EXIT_TREE) _deshacer();
    }

    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_textura_lista"), &RBXMesh::_gl_textura_lista);
        ClassDB::bind_method(D_METHOD("set_mesh_type", "i"), &RBXMesh::set_mesh_type);
        ClassDB::bind_method(D_METHOD("get_mesh_type"),      &RBXMesh::get_mesh_type);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "MeshType", PROPERTY_HINT_ENUM,
                     "Head,Torso,Wedge,Sphere,Cylinder,FileMesh,Brick"),
                     "set_mesh_type", "get_mesh_type");

        ClassDB::bind_method(D_METHOD("set_mesh_id", "s"), &RBXMesh::set_mesh_id);
        ClassDB::bind_method(D_METHOD("get_mesh_id"),      &RBXMesh::get_mesh_id);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "MeshId"), "set_mesh_id", "get_mesh_id");

        ClassDB::bind_method(D_METHOD("set_texture_id", "s"), &RBXMesh::set_texture_id);
        ClassDB::bind_method(D_METHOD("get_texture_id"),      &RBXMesh::get_texture_id);
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "TextureId"), "set_texture_id", "get_texture_id");

        ClassDB::bind_method(D_METHOD("set_mesh_scale", "v"), &RBXMesh::set_mesh_scale);
        ClassDB::bind_method(D_METHOD("get_mesh_scale"),      &RBXMesh::get_mesh_scale);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "Scale"), "set_mesh_scale", "get_mesh_scale");

        ClassDB::bind_method(D_METHOD("set_offset", "v"), &RBXMesh::set_offset);
        ClassDB::bind_method(D_METHOD("get_offset"),      &RBXMesh::get_offset);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "Offset"), "set_offset", "get_offset");

        ClassDB::bind_method(D_METHOD("set_vertex_color", "v"), &RBXMesh::set_vertex_color);
        ClassDB::bind_method(D_METHOD("get_vertex_color"),      &RBXMesh::get_vertex_color);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "VertexColor"),
                     "set_vertex_color", "get_vertex_color");
    }

public:
    void set_mesh_type(int i) { mesh_type = i; _aplicar(); }
    int get_mesh_type() const { return mesh_type; }
    void set_mesh_id(const String &s) {
        mesh_id = s;
        if (!s.is_empty() && gl_malla_local(s).is_null()) set_meta("__rbx_missing_asset", s);
        else if (has_meta("__rbx_missing_asset")) remove_meta("__rbx_missing_asset");
        _aplicar();
    }
    String get_mesh_id() const { return mesh_id; }
    void set_texture_id(const String &s) { texture_id = s; _aplicar(); }
    String get_texture_id() const { return texture_id; }
    void set_mesh_scale(const Vector3 &v) { mesh_scale = v; _aplicar(); }
    Vector3 get_mesh_scale() const { return mesh_scale; }
    void set_offset(const Vector3 &v) { offset = v; _aplicar(); }
    Vector3 get_offset() const { return offset; }
    void set_vertex_color(const Vector3 &v) { vertex_color = v; }
    Vector3 get_vertex_color() const { return vertex_color; }
    RBXMesh() { set_meta("_gl_bridge", true); set_meta("_gl_efecto", true); }
};

// ── Bone (esqueleto de MeshPart skinneado) ──────────────────────────
class RBXBone : public Node3D {
    GDCLASS(RBXBone, Node3D);

    Transform3D bone_transform;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_bone_transform", "t"), &RBXBone::set_bone_transform);
        ClassDB::bind_method(D_METHOD("get_bone_transform"),      &RBXBone::get_bone_transform);
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "Transform"),
                     "set_bone_transform", "get_bone_transform");
    }

public:
    void set_bone_transform(const Transform3D &t) { bone_transform = t; }
    Transform3D get_bone_transform() const { return bone_transform; }
};

#endif // RBX_INSTANCES_H
