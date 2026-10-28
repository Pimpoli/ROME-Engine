#ifndef GL_CLIPS_INTEGRADOS_H
#define GL_CLIPS_INTEGRADOS_H

// ════════════════════════════════════════════════════════════════════
//  Animaciones por defecto de los avatares R6 y R15 (F8 de la v1.17)
//  ----------------------------------------------------------------
//  Hechas en Blender sobre el esqueleto de cada avatar (propias: ni las
//  mallas ni las curvas son de Roblox). Se guardan como el clip de una
//  KeyframeSequence (anims/<nombre>.json) y se resuelven por los ids de las
//  animaciones por defecto de Roblox: asi el Animate de un juego (el de Roblox
//  copiado tal cual, o uno propio con esos ids) las encuentra solas.
//
//  Va incluido desde gl_animador.h, despues de GLClipAnim.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include "../characters/gl_avatares_gen.h"

// Un archivo de los avatares (rig.json, mallas, anims). Manda la copia en
// disco (res://GodotLuau/assets/avatars/...): asi un avatar se cambia sin
// recompilar. Si no esta (juego exportado, proyecto sin la carpeta), la que va
// empotrada en la DLL.
inline String gl_ava_texto(const String& rel) {
    const String ruta = String("res://GodotLuau/assets/avatars/") + rel;
    if (FileAccess::file_exists(ruta)) {
        const String s = FileAccess::get_file_as_string(ruta);
        if (!s.is_empty()) return s;
    }
    for (int i = 0; i < GL_ARCHIVOS_AVATAR_N; i++)
        if (rel == GL_ARCHIVOS_AVATAR[i].ruta)
            return String::utf8(GL_ARCHIVOS_AVATAR[i].datos, GL_ARCHIVOS_AVATAR[i].largo);
    return String();
}

// Ids de las animaciones por defecto de Roblox -> clip de aqui. Las variantes
// (Animation2/3 de cada baile, ids viejos de walk/run/swim) van al mismo clip.
struct GLClipIntegrado { int64_t id; bool r15; const char* nombre; };
inline const GLClipIntegrado* gl_clips_integrados(int& n) {
    static const GLClipIntegrado t[] = {
        // R15
        { 507766388, true, "idle1" }, { 507766666, true, "idle2" }, { 913402848, true, "walk" },
        { 507777826, true, "walk" }, { 913376220, true, "run" }, { 507767714, true, "run" },
        { 507765000, true, "jump" }, { 507767968, true, "fall" }, { 507765644, true, "climb" },
        { 2506281703LL, true, "sit" }, { 913384386, true, "swim" }, { 507784897, true, "swim" },
        { 913389285, true, "swimidle" }, { 481825862, true, "swimidle" }, { 507768375, true, "toolnone" },
        { 522635514, true, "toolslash" }, { 522638767, true, "toollunge" }, { 507770239, true, "wave" },
        { 507770453, true, "point" }, { 507771019, true, "dance1" }, { 507771955, true, "dance1" },
        { 507772104, true, "dance1" }, { 507776043, true, "dance2" }, { 507776720, true, "dance2" },
        { 507776879, true, "dance2" }, { 507777268, true, "dance3" }, { 507777451, true, "dance3" },
        { 507777623, true, "dance3" }, { 507770818, true, "laugh" }, { 507770677, true, "cheer" },
        // R6
        { 180435571, false, "idle1" }, { 180435792, false, "idle2" }, { 180426354, false, "walk" },
        { 125750702, false, "jump" }, { 180436334, false, "climb" }, { 182393478, false, "toolnone" },
        { 180436148, false, "fall" }, { 178130996, false, "sit" }, { 129967390, false, "toolslash" },
        { 129967478, false, "toollunge" }, { 128777973, false, "wave" }, { 128853357, false, "point" },
        { 182435998, false, "dance1" }, { 182491037, false, "dance1" }, { 182491065, false, "dance1" },
        { 182436842, false, "dance2" }, { 182491248, false, "dance2" }, { 182491277, false, "dance2" },
        { 182436935, false, "dance3" }, { 182491368, false, "dance3" }, { 182491423, false, "dance3" },
        { 129423131, false, "laugh" }, { 129423030, false, "cheer" },
    };
    n = (int)(sizeof(t) / sizeof(t[0]));
    return t;
}

inline int gl_anim_estilo_de(const String& s) {
    if (s == "Constant") return 1;
    if (s == "Elastic") return 2;
    if (s == "Cubic") return 3;
    if (s == "Bounce") return 4;
    if (s == "CubicV2") return 5;
    return 0;   // Linear
}
inline int gl_anim_direccion_de(const String& s) {
    if (s == "Out") return 1;
    if (s == "InOut") return 2;
    return 0;   // In
}
inline int gl_anim_prioridad_de(const String& s) {
    if (s == "Core") return 1000;
    if (s == "Idle") return 0;
    if (s == "Movement") return 1;
    if (s == "Action2") return 3;
    if (s == "Action3") return 4;
    if (s == "Action4") return 5;
    return 2;   // Action
}

inline Transform3D gl_anim_cf_json(const Variant& v) {
    Transform3D t;
    if (v.get_type() != Variant::ARRAY) return t;
    const Array a = v;
    if (a.size() < 12) return t;
    t.origin = Vector3((float)(double)a[0], (float)(double)a[1], (float)(double)a[2]);
    t.basis.rows[0] = Vector3((float)(double)a[3], (float)(double)a[4], (float)(double)a[5]);
    t.basis.rows[1] = Vector3((float)(double)a[6], (float)(double)a[7], (float)(double)a[8]);
    t.basis.rows[2] = Vector3((float)(double)a[9], (float)(double)a[10], (float)(double)a[11]);
    return t;
}

// El arbol de Pose de un keyframe (como _gl_anim_poses con nodos).
inline void _gl_clip_json_poses(const Array& poses, float t, HashMap<StringName, int>& indice, GLClipAnim& c) {
    for (int i = 0; i < poses.size(); i++) {
        const Dictionary p = poses[i];
        const float peso = p.has("w") ? (float)(double)p["w"] : 1.0f;
        if (peso > 0.0f) {
            const StringName hueso(String(p.get("n", "")));
            const Transform3D x = gl_anim_cf_json(p.get("cf", Variant()));
            const int* idx = indice.getptr(hueso);
            int i_pista;
            if (idx) i_pista = *idx;
            else {
                i_pista = (int)c.pistas.size();
                indice.insert(hueso, i_pista);
                c.pistas.push_back({ hueso, {} });
            }
            c.pistas[i_pista].claves.push_back({ t, x.origin, x.basis.get_rotation_quaternion(),
                                                 gl_anim_estilo_de(String(p.get("es", "Linear"))),
                                                 gl_anim_direccion_de(String(p.get("ed", "In"))) });
        }
        const Variant hijas = p.get("h", Variant());
        if (hijas.get_type() == Variant::ARRAY) _gl_clip_json_poses(hijas, t, indice, c);
    }
}

// Un clip del formato de anims/<nombre>.json: { id, loop, prio, kf: [ { t, n, marcas, poses } ] }.
inline GLClipRef gl_clip_de_json(const String& texto) {
    if (texto.is_empty()) return nullptr;
    const Variant v = JSON::parse_string(texto);
    if (v.get_type() != Variant::DICTIONARY) return nullptr;
    const Dictionary d = v;
    GLClipRef c = std::make_shared<GLClipAnim>();
    c->bucle = d.get("loop", false).booleanize();
    c->prioridad = gl_anim_prioridad_de(String(d.get("prio", "Core")));
    HashMap<StringName, int> indice;
    const Array kf = d.get("kf", Array());
    for (int i = 0; i < kf.size(); i++) {
        const Dictionary k = kf[i];
        const float t = (float)(double)k.get("t", 0.0);
        c->duracion = MAX(c->duracion, t);
        const String nombre = k.get("n", "Keyframe");
        if (nombre != "Keyframe" && !nombre.is_empty()) c->marcas.push_back({ t, nombre, String(), true });
        const Array marcas = k.get("marcas", Array());
        for (int m = 0; m < marcas.size(); m++) {
            const Dictionary mk = marcas[m];
            c->marcas.push_back({ t, String(mk.get("n", "")), String(mk.get("v", "")), false });
        }
        _gl_clip_json_poses(k.get("poses", Array()), t, indice, *c);
    }
    for (GLPistaAnim& p : c->pistas)
        std::stable_sort(p.claves.begin(), p.claves.end(),
                         [](const GLClaveAnim& a, const GLClaveAnim& b) { return a.t < b.t; });
    std::stable_sort(c->marcas.begin(), c->marcas.end(),
                     [](const GLMarcaAnim& a, const GLMarcaAnim& b) { return a.t < b.t; });
    return c;
}

// Un clip integrado por rig y nombre (idle1, walk...). Se lee una vez.
inline HashMap<String, GLClipRef>& gl_clips_integrados_cache() { static HashMap<String, GLClipRef> m; return m; }
inline GLClipRef gl_clip_integrado(bool r15, const String& nombre) {
    const String rel = String(r15 ? "r15" : "r6") + "/anims/" + nombre + ".json";
    if (GLClipRef* c = gl_clips_integrados_cache().getptr(rel)) return *c;
    GLClipRef c = gl_clip_de_json(gl_ava_texto(rel));
    gl_clips_integrados_cache().insert(rel, c);   // tambien el nulo: no se vuelve a buscar
    return c;
}

// Por id: el de una animacion por defecto de Roblox (rbxassetid://507766388,
// http://www.roblox.com/asset/?id=...), o uno propio: glanim://r15/walk.
inline GLClipRef gl_clip_integrado_de_id(const String& aid) {
    if (aid.begins_with("glanim://")) {
        const PackedStringArray p = aid.substr(9).split("/");
        if (p.size() == 2) return gl_clip_integrado(p[0] == "r15", p[1]);
        return nullptr;
    }
    const int64_t n = gl_anim_numero(aid);
    if (n <= 0) return nullptr;
    int total = 0;
    const GLClipIntegrado* t = gl_clips_integrados(total);
    for (int i = 0; i < total; i++)
        if (t[i].id == n) return gl_clip_integrado(t[i].r15, t[i].nombre);
    return nullptr;
}

// Si el Animate de un juego pide para un estado una animacion de la que aqui
// no hay datos (un id de la nube que no vino en el .gdlu), el personaje se
// quedaria rigido: se usa la de ese estado del avatar. En Roblox no pasa
// porque alli la animacion siempre esta; aqui faltar es lo normal.
inline GLClipRef gl_clip_integrado_de_ranura(Node* animacion, Node* dueno) {
    if (!animacion || !dueno) return nullptr;
    Node* ranura = animacion->get_parent();
    Node* script = ranura ? ranura->get_parent() : nullptr;
    if (!script || String(script->get_name()) != "Animate") return nullptr;
    String r = ranura->get_name();
    const String an = animacion->get_name();
    if (r == "idle") r = (an == "Animation2") ? "idle2" : "idle1";
    else if (r == "dance") r = "dance1";
    static const char* VALIDAS[] = { "idle1", "idle2", "walk", "run", "jump", "fall", "climb", "sit", "swim",
                                     "swimidle", "toolnone", "toolslash", "toollunge", "wave", "point",
                                     "dance1", "dance2", "dance3", "laugh", "cheer" };
    bool valida = false;
    for (const char* v : VALIDAS) if (r == v) { valida = true; break; }
    if (!valida) return nullptr;
    // El rig del personaje: R15 si tiene UpperTorso.
    Node* pers = dueno;
    for (Node* a = dueno; a; a = a->get_parent()) {
        if (a->get_node_or_null(NodePath("HumanoidRootPart"))) { pers = a; break; }
    }
    const bool r15 = pers->get_node_or_null(NodePath("UpperTorso")) != nullptr;
    if (!r15 && r == "run") r = "walk";             // el R6 corre con su walk
    if (!r15 && (r == "swim" || r == "swimidle")) return nullptr;
    return gl_clip_integrado(r15, r);
}

inline void gl_clips_integrados_limpiar() { gl_clips_integrados_cache().clear(); }

#endif // GL_CLIPS_INTEGRADOS_H
