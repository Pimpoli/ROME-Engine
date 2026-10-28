#ifndef ROBLOX_TERRAIN_H
#define ROBLOX_TERRAIN_H

// ════════════════════════════════════════════════════════════════════
//  RobloxTerrain — Workspace.Terrain (como Roblox)
//  API Luau:
//    Terrain:FillBlock(cframe, size, material)   — llena un bloque
//    Terrain:FillBall(center, radius, material)  — llena una esfera
//    Terrain:FillRegion(region3, resolution, material) / :FillRegion(min,max,mat)
//    Terrain:Clear()                             — vacía todo el terreno
//  material = Enum.Material.*  (Water = agua translúcida, Air = vaciar).
//  El framework MundoVoxel usa Terrain:FillBlock(CFrame, Size, Enum.Material.Water/Air).
//
//  Implementación: cada relleno es UNA malla de región (BoxMesh/SphereMesh) con
//  material COMPARTIDO por índice (evita explosión de materiales, como RobloxPart).
//  Los sólidos llevan colisión (StaticBody3D); el agua es translúcida y sin
//  colisión; Air borra las mallas cuyo centro cae dentro de la región.
//
//  El terreno de voxeles de un place importado (Terrain.SmoothGrid) va aparte:
//  el importador lo deja en un archivo (GLDatosTerreno) y aqui se convierte en
//  malla al entrar en el arbol (gl_terreno.h). Esos nodos son internos: no se
//  guardan en la escena, se rehacen cada vez desde el archivo.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node3d.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include "gl_capas.h"       // capas de las sombras propias
#include "gl_luz_propia.h"  // luz propia del terreno
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include "gl_terreno.h"

using namespace godot;

class RobloxTerrain : public Node3D {
    GDCLASS(RobloxTerrain, Node3D);

    Color water_color = Color(0.12f, 0.36f, 0.58f);
    float water_transparency = 0.45f;
    String datos_terreno;               // el SmoothGrid importado (assets/rbx/terreno)
    PackedColorArray colores_terreno;   // MaterialColors: un color por material
    bool terreno_montado = false;
    int64_t terreno_triangulos = 0;

    void _quitar_terreno() {
        for (int i = get_child_count(true) - 1; i >= 0; --i) {
            Node* c = get_child(i, true);
            if (c && c->has_meta("_gl_terreno")) { remove_child(c); c->queue_free(); }
        }
        terreno_montado = false;
        terreno_triangulos = 0;
    }
    void _montar_terreno() {
        if (terreno_montado || datos_terreno.is_empty() || !is_inside_tree()) return;
        terreno_montado = true;
        const PackedByteArray datos = FileAccess::get_file_as_bytes(datos_terreno);
        if (datos.size() < 2) return;
        terreno_triangulos = gl_terreno_construir(this, datos, colores_terreno, water_color, water_transparency);
    }

    // Cache de materiales de terreno por índice (compartidos entre rellenos).
    static HashMap<int, Ref<StandardMaterial3D>>& _terrain_mat_cache() {
        static HashMap<int, Ref<StandardMaterial3D>> c;
        return c;
    }

public:
    // Vaciarlo al descargar la extensión (1.15): si no, los materiales viven
    // hasta la descarga de la DLL, cuando el RenderingServer ya no existe → sus
    // RID se reportan como fugados y el destructor de BaseMaterial3D peta.
    static void _gl_clear_material_cache() { _terrain_mat_cache().clear(); }
private:

    // Color intrínseco por material de terreno (Roblox: la grama es verde, la
    // arena tostada, etc. — sin necesidad de que el usuario ponga Color).
    static Color _terrain_color(int mat) {
        switch (mat) {
            case 2:  return Color(0.85f, 0.95f, 1.0f);   // Neon
            case 5:  return Color(0.90f, 0.90f, 0.88f);  // Marble
            case 6:  return Color(0.38f, 0.40f, 0.44f);  // Slate
            case 7:  return Color(0.62f, 0.62f, 0.60f);  // Concrete
            case 8:  return Color(0.55f, 0.52f, 0.50f);  // Granite
            case 9:  return Color(0.55f, 0.30f, 0.24f);  // Brick
            case 14: return Color(0.33f, 0.52f, 0.22f);  // Grass
            case 15: return Color(0.78f, 0.88f, 0.94f);  // Ice
            case 17: return Color(0.80f, 0.72f, 0.52f);  // Sand
            case 19: return Color(0.42f, 0.42f, 0.44f);  // Rock
            case 20: return Color(0.95f, 0.96f, 0.98f);  // Snow
            case 21: return Color(0.46f, 0.46f, 0.46f);  // Cobblestone
            case 23: return Color(0.40f, 0.30f, 0.20f);  // Ground
            case 24: return Color(0.30f, 0.23f, 0.16f);  // Mud
            case 25: return Color(0.80f, 0.66f, 0.46f);  // Sandstone
            case 26: return Color(0.22f, 0.22f, 0.24f);  // Basalt
            case 27: return Color(0.55f, 0.20f, 0.10f);  // CrackedLava
            case 28: return Color(0.80f, 0.90f, 0.96f);  // Glacier
            case 29: return Color(0.28f, 0.48f, 0.20f);  // LeafyGrass
            case 30: return Color(0.92f, 0.92f, 0.94f);  // Salt
            case 31: return Color(0.78f, 0.76f, 0.68f);  // Limestone
            case 32: return Color(0.50f, 0.50f, 0.52f);  // Pavement
            case 33: return Color(0.20f, 0.20f, 0.22f);  // Asphalt
            default: return Color(0.55f, 0.55f, 0.55f);
        }
    }

    Ref<StandardMaterial3D> _terrain_material(int mat) {
        HashMap<int, Ref<StandardMaterial3D>>& cache = _terrain_mat_cache();
        // El agua depende de props ajustables → material dedicado (no cacheado).
        if (mat == 35) {
            Ref<StandardMaterial3D> m; m.instantiate();
            m->set_albedo(Color(water_color.r, water_color.g, water_color.b, 1.0f - water_transparency));
            m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
            m->set_roughness(0.05f);
            m->set_metallic(0.10f);
            return m;
        }
        if (cache.has(mat)) return cache[mat];
        Ref<StandardMaterial3D> m; m.instantiate();
        m->set_albedo(_terrain_color(mat));
        m->set_metallic(0.0f);
        switch (mat) {
            case 2:  m->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
                     m->set_emission(_terrain_color(mat)); m->set_emission_energy_multiplier(2.0f);
                     m->set_roughness(0.5f); break;
            case 27: m->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
                     m->set_emission(Color(1.0f, 0.35f, 0.05f)); m->set_emission_energy_multiplier(1.6f);
                     m->set_roughness(0.8f); break;
            case 15: case 28: m->set_roughness(0.1f); m->set_metallic(0.05f); break;
            case 5:  m->set_roughness(0.2f); break;
            case 17: case 23: case 25: m->set_roughness(1.0f); break;
            default: m->set_roughness(0.9f); break;
        }
        cache[mat] = m;
        return m;
    }

    // Air (o carve): borra las mallas cuyo centro cae dentro de la región.
    void _carve(Vector3 pos, Vector3 size) {
        Vector3 mn = pos - size * 0.5f;
        Vector3 mx = pos + size * 0.5f;
        for (int i = get_child_count() - 1; i >= 0; --i) {
            Node3D* c = gl_cast<Node3D>(get_child(i));
            if (!c) continue;
            Vector3 p = c->get_position();
            if (p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y && p.z >= mn.z && p.z <= mx.z)
                c->queue_free();
        }
    }

    // Material (con la luz propia si esta activa) y capa de las sombras propias:
    // el suelo, quieto, al mapa guardado; el agua no proyecta.
    void _gl_vestir(MeshInstance3D* mi, int mat) {
        mi->set_material_override(gl_luz_si_toca(_terrain_material(mat)));
        mi->set_layer_mask(mat == 35 ? GL_CAPA_SIN_SOMBRA : GL_CAPA_MUNDO);
    }

    MeshInstance3D* _make_mesh(const Ref<Mesh>& mesh, Vector3 pos, int mat) {
        MeshInstance3D* mi = memnew(MeshInstance3D);
        mi->set_mesh(mesh);
        _gl_vestir(mi, mat);
        mi->set_position(pos);
        return mi;
    }

protected:
    void _notification(int what) {
        if (what == NOTIFICATION_READY) _montar_terreno();
    }

    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_datos_terreno", "ruta"), &RobloxTerrain::set_datos_terreno);
        ClassDB::bind_method(D_METHOD("get_datos_terreno"),        &RobloxTerrain::get_datos_terreno);
        ClassDB::bind_method(D_METHOD("set_colores_terreno", "c"), &RobloxTerrain::set_colores_terreno);
        ClassDB::bind_method(D_METHOD("get_colores_terreno"),      &RobloxTerrain::get_colores_terreno);
        ClassDB::bind_method(D_METHOD("gl_triangulos_terreno"),    &RobloxTerrain::gl_triangulos_terreno);
        // Se guardan en la escena; no salen en el inspector.
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "GLDatosTerreno", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
                     "set_datos_terreno", "get_datos_terreno");
        ADD_PROPERTY(PropertyInfo(Variant::PACKED_COLOR_ARRAY, "GLColoresTerreno", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
                     "set_colores_terreno", "get_colores_terreno");
        ClassDB::bind_method(D_METHOD("fill_block", "pos", "size", "material"),   &RobloxTerrain::fill_block);
        ClassDB::bind_method(D_METHOD("fill_ball", "center", "radius", "material"), &RobloxTerrain::fill_ball);
        ClassDB::bind_method(D_METHOD("fill_region", "min", "max", "material"),    &RobloxTerrain::fill_region);
        ClassDB::bind_method(D_METHOD("clear_terrain"),                            &RobloxTerrain::clear_terrain);
        ClassDB::bind_method(D_METHOD("set_water_color", "c"),   &RobloxTerrain::set_water_color);
        ClassDB::bind_method(D_METHOD("get_water_color"),        &RobloxTerrain::get_water_color);
        ClassDB::bind_method(D_METHOD("set_water_transparency", "v"), &RobloxTerrain::set_water_transparency);
        ClassDB::bind_method(D_METHOD("get_water_transparency"),     &RobloxTerrain::get_water_transparency);
        ADD_PROPERTY(PropertyInfo(Variant::COLOR, "WaterColor"), "set_water_color", "get_water_color");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "WaterTransparency"), "set_water_transparency", "get_water_transparency");
    }

public:
    RobloxTerrain() { set_meta("_gl_bridge", true); }

    void set_datos_terreno(const String& ruta) {
        if (ruta == datos_terreno) return;
        _quitar_terreno();
        datos_terreno = ruta;
        _montar_terreno();
    }
    String get_datos_terreno() const { return datos_terreno; }
    void set_colores_terreno(const PackedColorArray& c) { colores_terreno = c; }
    PackedColorArray get_colores_terreno() const { return colores_terreno; }
    int64_t gl_triangulos_terreno() const { return terreno_triangulos; }

    void set_water_color(const Color& c) { water_color = c; }
    Color get_water_color() const { return water_color; }
    void set_water_transparency(float v) { water_transparency = v; }
    float get_water_transparency() const { return water_transparency; }

    // FillBlock(position, size, material). Air (36) vacía; Water (35) sin colisión.
    void fill_block(Vector3 pos, Vector3 size, int material) {
        if (material == 36) { _carve(pos, size); return; }
        bool is_water = (material == 35);
        Ref<BoxMesh> bm; bm.instantiate(); bm->set_size(size);
        if (is_water) {
            add_child(_make_mesh(bm, pos, material));
            return;
        }
        StaticBody3D* body = memnew(StaticBody3D);
        body->set_position(pos);
        MeshInstance3D* mi = memnew(MeshInstance3D);
        mi->set_mesh(bm);
        _gl_vestir(mi, material);
        body->add_child(mi);
        CollisionShape3D* cs = memnew(CollisionShape3D);
        Ref<BoxShape3D> bs; bs.instantiate(); bs->set_size(size);
        cs->set_shape(bs);
        body->add_child(cs);
        add_child(body);
    }

    void fill_ball(Vector3 center, float radius, int material) {
        if (material == 36) { _carve(center, Vector3(radius * 2, radius * 2, radius * 2)); return; }
        bool is_water = (material == 35);
        Ref<SphereMesh> sm; sm.instantiate();
        sm->set_radius(radius); sm->set_height(radius * 2.0f);
        if (is_water) {
            add_child(_make_mesh(sm, center, material));
            return;
        }
        StaticBody3D* body = memnew(StaticBody3D);
        body->set_position(center);
        MeshInstance3D* mi = memnew(MeshInstance3D);
        mi->set_mesh(sm);
        _gl_vestir(mi, material);
        body->add_child(mi);
        CollisionShape3D* cs = memnew(CollisionShape3D);
        Ref<SphereShape3D> ss; ss.instantiate(); ss->set_radius(radius);
        cs->set_shape(ss);
        body->add_child(cs);
        add_child(body);
    }

    // FillRegion por caja min/max (equivalente a un FillBlock del volumen).
    void fill_region(Vector3 min_c, Vector3 max_c, int material) {
        Vector3 size = (max_c - min_c).abs();
        Vector3 pos = (min_c + max_c) * 0.5f;
        if (size.x < 0.01f) size.x = 0.01f;
        if (size.y < 0.01f) size.y = 0.01f;
        if (size.z < 0.01f) size.z = 0.01f;
        fill_block(pos, size, material);
    }

    void clear_terrain() {
        for (int i = get_child_count() - 1; i >= 0; --i)
            get_child(i)->queue_free();
        // Terrain:Clear() tambien borra el terreno de voxeles del place.
        _quitar_terreno();
        datos_terreno = String();
    }
};


// El nombre que ve el usuario, sin "Roblox" delante. Subclase fina: hereda todo
// y sigue siendo un RobloxTerrain, asi que ningun cast ni is_class de por ahi se
// entera. Ver core/gl_nombres_limpios.h para el porque y para el resto.
class Terrain : public RobloxTerrain {
    GDCLASS(Terrain, RobloxTerrain);
protected:
    static void _bind_methods() {}
public:
    Terrain() {}
};

#endif // ROBLOX_TERRAIN_H
