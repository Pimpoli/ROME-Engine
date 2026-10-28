#ifndef RBX_IMPORTER_H
#define RBX_IMPORTER_H

// Convierte un .rbxl en un arbol de nodos de Godot.
//
// Trabaja POR FASES y POR LOTES para no congelar el editor con un place de
// 90.000 instancias. El flujo es:
//
//   Begin(ruta)            abre el archivo y prepara los indices
//   Step()  -> bool        procesa un lote; false cuando ya no queda nada
//   GetProgress()/GetPhaseName()/GetCurrentItem()  para pintar la barra
//   GetResult()            el nodo raiz
//
// Fases: Leyendo -> Importando -> Propiedades -> Jerarquia -> Referencias ->
//        Comprobando -> Recolocando -> Listo
//
// "Comprobando" verifica que TODA instancia del archivo acabo en el arbol.
// "Recolocando" fusiona el resultado con la estructura Roblox que ya tengas en
// la escena (reutiliza tu Workspace, Players, etc. en vez de duplicarlos) y crea
// la que falte.
//
// Nada de lo que venga del archivo puede tumbar el editor: si algo no cuadra se
// salta esa columna y se anota en el reporte.

#include "rbx_reader.h"
#include "gl_cast.h"
#include "rbx_prop.h"
#include "rbx_classmap.h"
#include "rbx_instances.h"
#include "rbx_uniones.h"
#include "luau_script.h"
#include "roblox_workspace.h"
#include "roblox_datamodel.h"   // gl_ensure_default_player_scripts

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

class RBXImporter : public RefCounted {
    GDCLASS(RBXImporter, RefCounted);

public:
    // PH_ANALYZED: el archivo ya esta leido y se sabe TODO lo que trae, pero no
    // se ha creado ni un nodo. Es donde se enseña la lista al usuario y espera
    // a que diga Continue o Reject. Nada de lo de despues ocurre sin su OK.
    enum Phase {
        PH_IDLE = 0, PH_PARSE, PH_ANALYZED, PH_INSTANCES, PH_PROPERTIES, PH_HIERARCHY,
        PH_REFS, PH_VERIFY, PH_PLACE, PH_DONE, PH_ERROR
    };

private:
    // Tamaño de lote por fase. Ajustados para que un step ronde los pocos ms:
    // suficiente para avanzar rapido sin que se note el tiron en el editor.
    static const int BATCH_INSTANCES = 2500;
    static const int BATCH_VALUES    = 4000;
    static const int BATCH_LINKS     = 6000;
    static const int BATCH_REFS      = 3000;

    // ── RITMO ADAPTATIVO ─────────────────────────────────────────────────
    //  Un tamaño de lote FIJO no puede estar bien en todas las maquinas: en un
    //  PC lento traba el editor y en uno rapido va mas despacio de lo que
    //  podria. En vez de adivinar el disco o la CPU, se MIDE lo que tardo el
    //  ultimo lote y se corrige para acercarse al objetivo. Es la misma idea
    //  que el modo Automatic de calidad.
    //
    //  Objetivo 12 ms: se nota fluido (>60 FPS de refresco de la ventana de
    //  progreso) y aprovecha bien la maquina. El factor va de 0,15x a 4x para
    //  que ni se pare del todo ni se coma un frame entero de golpe.
    float  ritmo = 1.0f;          // multiplicador del tamaño de lote
    uint64_t t_lote = 0;          // reloj del lote en curso
    static constexpr float RITMO_OBJETIVO_MS = 12.0f;

    bool pausar_tras_analisis = false;   // parar en PH_ANALYZED y esperar al usuario

    // A que seccion de la ventana de analisis va cada clase de Roblox.
    static int _seccion_de(const String &c) {
        if (c.ends_with("Script")) return 2;                       // Scripts
        if (c.begins_with("UI") || c.ends_with("Gui") || c.ends_with("Label") ||
            c.ends_with("Button") || c.ends_with("Frame") || c == "TextBox") return 3;
        if (c.ends_with("Part") || c == "Part" || c == "Terrain" ||
            c == "SpawnLocation" || c == "Seat" || c == "Model" ||
            c == "Folder" || c == "Configuration") return (c == "Model") ? 1 : 0;
        if (c.ends_with("Mesh") || c == "MeshPart" || c == "Decal" ||
            c == "Texture" || c == "Accessory" || c == "CharacterMesh") return 1;
        if (c.ends_with("Effect") || c == "Sound" || c == "SoundGroup" ||
            c == "Beam" || c == "Trail" || c == "Sparkles" || c == "Fire" ||
            c == "Smoke" || c == "Explosion" || c == "Highlight" ||
            c == "PointLight" || c == "SpotLight" || c == "SurfaceLight" ||
            c == "Atmosphere" || c == "Sky" || c == "ParticleEmitter" ||
            c == "Animation" || c == "AnimationTrack" || c == "SurfaceAppearance") return 4;
        if (c.ends_with("Constraint") || c.ends_with("Weld") || c == "Motor6D" ||
            c == "Attachment" || c == "Bone" || c == "Animator" ||
            c == "AnimationController" || c == "Humanoid" || c == "BodyVelocity" ||
            c == "AlignPosition" || c == "AlignOrientation") return 5;
        if (c.ends_with("Service") || c.ends_with("Storage") || c.ends_with("Value") ||
            c == "RemoteEvent" || c == "RemoteFunction" || c == "BindableEvent" ||
            c == "BindableFunction" || c == "Workspace" || c == "Lighting" ||
            c == "Players" || c == "Teams" || c == "StarterGui" ||
            c == "StarterPack" || c == "StarterPlayer") return 6;
        return 7;                                                   // Other
    }

    void _ritmo_empezar() { t_lote = Time::get_singleton()->get_ticks_usec(); }
    void _ritmo_ajustar() {
        const float ms = (float)(Time::get_singleton()->get_ticks_usec() - t_lote) / 1000.0f;
        if (ms <= 0.01f) return;
        // Correccion suave (30%): si se corrigiera de golpe, el tamaño oscilaria
        // entre dos extremos en vez de asentarse.
        const float ideal = ritmo * (RITMO_OBJETIVO_MS / ms);
        ritmo = CLAMP(ritmo + (ideal - ritmo) * 0.30f, 0.15f, 4.0f);
    }
    int _lote(int base) const { return MAX(64, (int)(base * ritmo)); }
    static const int BATCH_VERIFY    = 6000;

    struct Inst {
        String rbx_class;
        Node *node = nullptr;
    };
    struct PendingRef {
        Node *node = nullptr;
        String prop;
        int32_t target = -1;
    };

    // ── Estado que sobrevive entre Step() ───────────────────────────
    rbx::RBXFile file;
    String path;
    Phase phase = PH_IDLE;
    int cursor = 0;          // indice dentro de la fase
    int sub_cursor = 0;      // indice dentro del chunk actual (fase PROPERTIES)

    Vector<int> inst_chunks, prop_chunks, prnt_chunks;
    HashMap<uint32_t, String> class_names;
    HashMap<uint32_t, Vector<int32_t>> class_refs;
    Vector<uint32_t> flat_class;     // por instancia: su class_index
    Vector<int32_t>  flat_ref;       // por instancia: su referent

    // Chunk PROP en curso
    bool prop_loaded = false;
    Vector<Variant> cur_vals;
    String cur_prop, cur_class;
    uint8_t cur_type = 0;
    Vector<int32_t> cur_refs;

    // Enlaces del PRNT ya decodificados
    Vector<int32_t> link_child, link_parent;

    HashMap<int32_t, Inst> by_ref;
    HashMap<int32_t, PackedByteArray> datos_union;   // ChildData de cada union (rbx_uniones.h)
    PackedByteArray terreno_grid, terreno_colores;   // Terrain.SmoothGrid y MaterialColors
    int32_t terreno_ref = -1;
    int terreno_chunks = 0;
    String terreno_error;
    int uniones_horneadas = 0, uniones_sin_piezas = 0;
    HashMap<String, HashSet<String>> prop_cache;
    Vector<PendingRef> pending_refs;

    Node *root = nullptr;
    Node *target_game = nullptr;     // Game existente con el que fusionar

    String last_error, current_item;
    Dictionary report, class_counts, missing_classes, ignored_props;
    Array missing_assets, verify_errors;
    int scripts_imported = 0, scripts_total = 0, scripts_seen = 0;
    int tags_applied = 0, orphans = 0, attrs_applied = 0, refs_ok = 0;
    int reused_services = 0, created_services = 0, verified_ok = 0;
    int renamed_siblings = 0, hidden_grouped = 0, defaults_replaced = 0;
    int dup_conflicts = 0;
    uint64_t t_start = 0;
    bool verbose = false;

    // ── Utilidades ──────────────────────────────────────────────────
    const HashSet<String> &_props_of(Node *n) {
        String cls = n->get_class();
        HashMap<String, HashSet<String>>::Iterator it = prop_cache.find(cls);
        if (it) return it->value;
        HashSet<String> set;
        TypedArray<Dictionary> pl = n->get_property_list();
        for (int k = 0; k < pl.size(); k++) {
            Dictionary d = pl[k];
            set.insert(d.get("name", String()));
        }
        prop_cache.insert(cls, set);
        return prop_cache.find(cls)->value;
    }

    static String _safe_node_name(const String &raw, const String &fallback) {
        String s = raw.validate_node_name().strip_edges();
        if (s.is_empty()) s = fallback.is_empty() ? String("Instance") : fallback;
        return s;
    }

    // Godot exige identificador valido en los metadatos, pero un atributo de
    // Roblox puede llamarse "Bevel Roundness".
    static String _safe_meta_name(const String &raw) {
        String out;
        for (int k = 0; k < raw.length(); k++) {
            char32_t c = raw[k];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_';
            out += String::chr(ok ? c : '_');
        }
        if (out.is_empty()) return String("_");
        char32_t f = out[0];
        if (f >= '0' && f <= '9') out = String("_") + out;
        return out;
    }

    void _set_meta_safe(Node *n, const String &name, const Variant &v) {
        String safe = _safe_meta_name(name);
        n->set_meta(StringName(safe), v);
        if (safe != name) {
            Dictionary alias = n->get_meta("__rbx_meta_alias", Dictionary());
            alias[safe] = name;
            n->set_meta("__rbx_meta_alias", alias);
        }
    }

    void _bump(Dictionary &d, const String &k) { d[k] = (int)d.get(k, 0) + 1; }

    // Cuelga child de parent anotando el nombre original si Godot lo renombra:
    // Roblox PERMITE hermanos con el mismo nombre y Godot no (los deja como
    // "@Nombre@2"). El nombre real de Roblox queda en __rbx_name para que la
    // capa Lua y el reporte puedan saberlo; en el place de prueba pasa con
    // ~8000 nodos, asi que no es un caso raro.
    void _attach(Node *parent, Node *child) {
        const String wanted = child->get_name();
        parent->add_child(child);
        if (String(child->get_name()) != wanted) {
            if (!child->has_meta("__rbx_name")) child->set_meta("__rbx_name", wanted);
            renamed_siblings++;
        }
    }

    // Contenedor para lo IRRECUPERABLE: instancias cuyo padre esta referenciado
    // en el archivo pero no definido (archivo recortado/corrupto). Antes se
    // colgaban de Game a pelo y parecian parte del juego; ahora quedan juntas,
    // identificadas y contadas en report["huerfanos"]. No se descartan porque
    // pueden llevar scripts o modelos que el usuario quiera rescatar a mano.
    Node *_orphan_container() {
        if (Node *c = root->get_node_or_null(NodePath("RecoveredInstances"))) return c;
        Node *c = gl_cast<Node>(ClassDB::instantiate(StringName("Folder")));
        if (!c) c = memnew(Node);
        c->set_name("RecoveredInstances");
        root->add_child(c);
        return c;
    }

    // ── Marca de plantilla ──────────────────────────────────────────
    // RobloxTemplate marca con _gl_template_generated TODO lo que genera
    // (v1.17, ver roblox_datamodel.h). El importador SOLO puede sustituir o
    // quitar nodos con esa marca: un nodo hecho a mano por el usuario en Godot
    // no la lleva y no se toca jamas. Proyectos de versiones viejas tampoco la
    // llevan: se tratan como del usuario (lo conservador).
    static bool _is_marked(Node *n) { return n && n->has_meta("_gl_template_generated"); }

    // ¿El subarbol es plantilla intacta? Los hijos SIN owner no cuentan: son
    // internos auto-creados por la clase (la malla/colision de una Part), no
    // contenido del usuario.
    static bool _subtree_all_marked(Node *n) {
        for (int k = 0; k < n->get_child_count(); k++) {
            Node *c = n->get_child(k);
            if (!c->get_owner()) continue;
            if (!_is_marked(c) || !_subtree_all_marked(c)) return false;
        }
        return true;
    }

    void _apply_tags(Node *n, const PackedByteArray &raw) {
        if (raw.size() == 0) return;
        PackedStringArray tags;
        const uint8_t *p = raw.ptr();
        int start = 0;
        for (int k = 0; k <= raw.size(); k++) {
            if (k == raw.size() || p[k] == 0) {
                if (k > start) tags.push_back(String::utf8((const char *)(p + start), k - start));
                start = k + 1;
            }
        }
        if (tags.size() == 0) return;
        n->set_meta("__rbx_tags", tags);
        tags_applied += tags.size();
    }

    // AttributesSerialize: u32 numero, luego (u32 len + nombre, u8 tipo, valor).
    // Los codigos de tipo son los de rbx_types::Variant, NO los del formato de
    // propiedades (0x11 = Vector3). Formato de cada uno:
    // https://github.com/rojo-rbx/rbx-dom/blob/master/docs/attributes.md
    // Los valores quedan como los guarda SetAttribute (ver gl_atributos.h).
    //
    // 0x06 es Float64: se leia como Int64 y TODO atributo numerico llegaba con
    // un valor absurdo (un ProductId 3570980000 salia 4749779289356894208;
    // 2.826 atributos en el place de prueba). Y 0x04 (Int32) no existia, asi
    // que la instancia que lo tuviera perdia TODOS sus atributos.
    void _apply_attributes(Node *n, const PackedByteArray &raw) {
        if (raw.size() < 4) return;
        rbx::Reader r(raw.ptr(), raw.size());
        uint32_t count = r.u32le();
        if (!r.ok() || count > 4096) { n->set_meta("__rbx_attributes_raw", raw); return; }

        for (uint32_t k = 0; k < count; k++) {
            String name = r.str();
            uint8_t t = r.u8();
            if (!r.ok() || name.is_empty()) { n->set_meta("__rbx_attributes_raw", raw); return; }

            Variant v;
            bool known = true;
            switch (t) {
                case 0x02: v = r.str(); break;                              // String
                case 0x03: v = (r.u8() != 0); break;                        // Bool
                case 0x04: v = (double)r.i32le(); break;                    // Int32
                case 0x05: v = (double)r.f32le(); break;                    // Float32
                case 0x06: v = r.f64le(); break;                            // Float64
                case 0x09: { float a = r.f32le(); int32_t b = r.i32le();    // UDim
                             v = gl_attr_udim(a, b); } break;
                case 0x0A: { float a = r.f32le(); int32_t b = r.i32le();    // UDim2
                             float c = r.f32le(); int32_t d = r.i32le();
                             v = Vector4(a, (float)b, c, (float)d); } break;
                case 0x0E: v = gl_attr_brickcolor((int)r.u32le()); break;   // BrickColor
                case 0x0F: { float cr = r.f32le(), cg = r.f32le(),          // Color3
                                   cb = r.f32le();
                             v = Color(cr, cg, cb); } break;
                case 0x10: { float a = r.f32le(), b = r.f32le();            // Vector2
                             v = Vector2(a, b); } break;
                case 0x11: { float a = r.f32le(), b = r.f32le(),            // Vector3
                                   c = r.f32le();
                             v = Vector3(a, b, c); } break;
                case 0x14: {                                                // CFrame
                    // Posicion, un byte de orientacion y, solo si es 0, la
                    // matriz por filas (XVector, YVector, ZVector de Roblox).
                    float m[9];
                    const float px = r.f32le(), py = r.f32le(), pz = r.f32le();
                    const uint8_t rid = r.u8();
                    if (rid == 0) { for (int j = 0; j < 9; j++) m[j] = r.f32le(); }
                    else if (!rbx::rot_id_matrix(rid, m)) { known = false; break; }
                    v = rbx::cframe_to_transform(m, Vector3(px, py, pz));
                } break;
                case 0x15: { String e = r.str(); uint32_t val = r.u32le();  // EnumItem
                             v = gl_attr_enum(e, (int)val); } break;
                case 0x17: {                                                // NumberSequence
                    const uint32_t np = r.u32le();
                    if (np > 64) { known = false; break; }
                    Array puntos;
                    for (uint32_t i = 0; i < np; i++) {
                        const float env = r.f32le(), tiempo = r.f32le(), valor = r.f32le();
                        Dictionary p; p["Time"] = tiempo; p["Value"] = valor; p["Envelope"] = env;
                        puntos.push_back(p);
                    }
                    v = gl_attr_secuencia("NumberSequence", puntos);
                } break;
                case 0x19: {                                                // ColorSequence
                    const uint32_t np = r.u32le();
                    if (np > 64) { known = false; break; }
                    Array puntos;
                    for (uint32_t i = 0; i < np; i++) {
                        const float env = r.f32le(), tiempo = r.f32le();
                        const float cr = r.f32le(), cg = r.f32le(), cb = r.f32le();
                        Dictionary p; p["Time"] = tiempo; p["Color"] = Color(cr, cg, cb); p["Envelope"] = env;
                        puntos.push_back(p);
                    }
                    v = gl_attr_secuencia("ColorSequence", puntos);
                } break;
                case 0x1B: { float a = r.f32le(), b = r.f32le();            // NumberRange
                             v = gl_attr_rango(a, b); } break;
                case 0x1C: { float a = r.f32le(), b = r.f32le(),            // Rect
                                   c = r.f32le(), d = r.f32le();
                             v = Rect2(a, b, c - a, d - b); } break;
                case 0x21: { const int peso = r.u16le();                    // Font
                             const int estilo = r.u8();
                             String familia = r.str();
                             r.str();   // CachedFaceId: el TTF que Studio tenia a mano
                             v = gl_attr_fuente(familia, peso, estilo); } break;
                default: known = false; break;
            }
            if (!known || !r.ok()) { n->set_meta("__rbx_attributes_raw", raw); return; }
            _set_meta_safe(n, name, v);   // asi lo lee GetAttribute de luau_api.h
            attrs_applied++;
        }
    }

    void _set_owner_recursive(Node *n, Node *owner) {
        for (int k = 0; k < n->get_child_count(); k++) {
            Node *c = n->get_child(k);
            c->set_owner(owner);
            _set_owner_recursive(c, owner);
        }
    }

    void _fail(const String &msg) {
        last_error = msg;
        phase = PH_ERROR;
        // Liberar lo que no llegara a colgarse de ningun sitio.
        for (HashMap<int32_t, Inst>::Iterator it = by_ref.begin(); it; ++it) {
            Node *n = it->value.node;
            if (n && !n->get_parent()) memdelete(n);
        }
        by_ref.clear();
        if (root && !root->get_parent()) { memdelete(root); root = nullptr; }
    }

    // ── Fases ───────────────────────────────────────────────────────
    bool _do_parse() {
        PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
        if (bytes.size() < 32) { _fail("No se pudo leer el archivo o esta vacio: " + path); return false; }
        if (bytes[0] == '<' && bytes[7] != '!') {
            _fail("Este .rbxlx esta en XML. El lector de XML llega en la siguiente "
                  "version; por ahora guardalo en Studio como .rbxl binario.");
            return false;
        }
        String err;
        if (!rbx::parse_container(bytes, file, err)) { _fail(err); return false; }

        for (int k = 0; k < file.chunks.size(); k++) {
            const String &n = file.chunks[k].name;
            if (n == "INST") inst_chunks.push_back(k);
            else if (n == "PROP") prop_chunks.push_back(k);
            else if (n == "PRNT") prnt_chunks.push_back(k);
        }

        // Cabeceras de los INST: clases, referents y lista aplanada.
        for (int i = 0; i < inst_chunks.size(); i++) {
            const rbx::Chunk &c = file.chunks[inst_chunks[i]];
            rbx::Reader r(c.data.ptr(), c.data.size());
            uint32_t idx = r.u32le();
            String cname = r.str();
            uint8_t fmt = r.u8();
            uint32_t count = r.u32le();
            if (!r.ok() || count > file.instance_count + 1024) continue;

            LocalVector<int32_t> refs; refs.resize(count);
            r.read_ref_array(count, refs.ptr());
            if (!r.ok()) continue;

            class_names[idx] = cname;
            Vector<int32_t> vec;
            for (uint32_t k = 0; k < count; k++) {
                vec.push_back(refs[k]);
                flat_class.push_back(idx);
                flat_ref.push_back(refs[k]);
            }
            class_refs[idx] = vec;
            class_counts[cname] = (int)count;
        }

        // Cuantos scripts hay, para poder decir "script 12 de 202".
        for (int i = 0; i < prop_chunks.size(); i++) {
            const rbx::Chunk &c = file.chunks[prop_chunks[i]];
            rbx::Reader r(c.data.ptr(), c.data.size());
            uint32_t idx = r.u32le();
            String pname = r.str();
            if (pname != "Source") continue;
            HashMap<uint32_t, Vector<int32_t>>::Iterator it = class_refs.find(idx);
            if (it) scripts_total += it->value.size();
        }

        // Los servicios cuelgan de un DataModel que NO existe como instancia.
        root = memnew(Node);
        root->set_name("Game");
        root->set_meta("__rbx_class", "DataModel");

        cursor = 0;
        phase = PH_INSTANCES;
        return true;
    }

    bool _do_instances() {
        const int total = flat_ref.size();
        int end = MIN(cursor + _lote(BATCH_INSTANCES), total);
        for (; cursor < end; cursor++) {
            const String cname = class_names.has(flat_class[cursor])
                                 ? class_names[flat_class[cursor]] : String("Instance");
            String godot_class = rbx::map_class(cname);
            Node *n = nullptr;
            if (!godot_class.is_empty())
                n = gl_cast<Node>(ClassDB::instantiate(StringName(godot_class)));
            if (!n) {
                RBXInstance *gi = memnew(RBXInstance);
                gi->set_rbx_class_name(cname);
                n = gi;
                if (godot_class.is_empty()) _bump(missing_classes, cname);
            }
            n->set_name(cname);
            n->set_meta("__rbx_class", cname);
            if (cname == "WedgePart" || cname == "CornerWedgePart" || cname == "TrussPart") {
                if (RobloxPart *pz = gl_cast<RobloxPart>(n))
                    pz->set_roblox_shape(cname == "WedgePart" ? 3 : cname == "CornerWedgePart" ? 4 : 5);
            }
            Inst inst; inst.rbx_class = cname; inst.node = n;
            by_ref.insert(flat_ref[cursor], inst);
        }
        current_item = String::num_int64(cursor) + " / " + String::num_int64(total) + " instancias";
        if (cursor >= total) { cursor = 0; sub_cursor = 0; prop_loaded = false; phase = PH_PROPERTIES; }
        return true;
    }

    // Carga la siguiente columna de propiedades. Devuelve false si ya no quedan.
    bool _load_next_prop() {
        while (cursor < prop_chunks.size()) {
            const rbx::Chunk &c = file.chunks[prop_chunks[cursor]];
            rbx::Reader r(c.data.ptr(), c.data.size());
            uint32_t idx = r.u32le();
            cur_prop = r.str();
            cur_type = r.u8();
            if (!r.ok()) { cursor++; continue; }

            HashMap<uint32_t, Vector<int32_t>>::Iterator rit = class_refs.find(idx);
            if (!rit || rit->value.size() == 0) { cursor++; continue; }
            cur_refs = rit->value;
            cur_class = class_names.has(idx) ? class_names[idx] : String();
            const uint32_t count = (uint32_t)cur_refs.size();

            // Tags y AttributesSerialize son blobs binarios: se aplican enteros
            // aqui porque necesitan los bytes crudos, no el valor decodificado.
            if (cur_prop == "Tags" && cur_type == rbx::PT_SHAREDSTRING) {
                LocalVector<uint32_t> ids;
                if (rbx::decode_sharedstring_indices(r, count, ids)) {
                    for (uint32_t k = 0; k < count; k++) {
                        HashMap<int32_t, Inst>::Iterator ii = by_ref.find(cur_refs[k]);
                        if (ii && (int)ids[k] < file.shared_raw.size())
                            _apply_tags(ii->value.node, file.shared_raw[ids[k]]);
                    }
                }
                cursor++; continue;
            }
            // El terreno de voxeles: crudo, para guardarlo en su archivo (_guardar_terreno).
            if (cur_class == "Terrain" && cur_type == rbx::PT_STRING &&
                (cur_prop == "SmoothGrid" || cur_prop == "MaterialColors")) {
                for (uint32_t k = 0; k < count; k++) {
                    PackedByteArray b = r.blob();
                    if (!r.ok()) break;
                    terreno_ref = cur_refs[k];
                    if (cur_prop == "SmoothGrid") terreno_grid = b; else terreno_colores = b;
                }
                cursor++; continue;
            }
            // Las piezas con las que se hizo una union: se guardan crudas para
            // hornear su malla al acabar las propiedades (rbx_uniones.h).
            if ((cur_prop == "ChildData2" && cur_type == rbx::PT_SHAREDSTRING) ||
                (cur_prop == "ChildData" && cur_type == rbx::PT_STRING)) {
                if (cur_class == "UnionOperation" || cur_class == "IntersectOperation") {
                    if (cur_type == rbx::PT_SHAREDSTRING) {
                        LocalVector<uint32_t> ids;
                        if (rbx::decode_sharedstring_indices(r, count, ids))
                            for (uint32_t k = 0; k < count; k++)
                                if ((int)ids[k] < file.shared_raw.size() && file.shared_raw[ids[k]].size() > 0)
                                    datos_union[cur_refs[k]] = file.shared_raw[ids[k]];
                    } else {
                        for (uint32_t k = 0; k < count; k++) {
                            PackedByteArray b = r.blob();
                            if (!r.ok()) break;
                            if (b.size() > 0 && !datos_union.has(cur_refs[k])) datos_union[cur_refs[k]] = b;
                        }
                    }
                }
                cursor++; continue;
            }
            // Mismo blob separado por \0, pero como String: lo guardan asi los
            // places mas viejos y otras herramientas. Antes se perdian.
            if (cur_prop == "Tags" && cur_type == rbx::PT_STRING) {
                for (uint32_t k = 0; k < count; k++) {
                    PackedByteArray raw = r.blob();
                    if (!r.ok()) break;
                    if (raw.size() == 0) continue;
                    HashMap<int32_t, Inst>::Iterator ii = by_ref.find(cur_refs[k]);
                    if (ii) _apply_tags(ii->value.node, raw);
                }
                cursor++; continue;
            }
            if (cur_prop == "AttributesSerialize" && cur_type == rbx::PT_STRING) {
                for (uint32_t k = 0; k < count; k++) {
                    PackedByteArray raw = r.blob();
                    if (!r.ok()) break;
                    if (raw.size() == 0) continue;
                    HashMap<int32_t, Inst>::Iterator ii = by_ref.find(cur_refs[k]);
                    if (ii) _apply_attributes(ii->value.node, raw);
                }
                cursor++; continue;
            }

            if (!rbx::decode_column(r, cur_type, count, file.shared_strings, cur_vals)) {
                _bump(ignored_props, cur_class + String(".") + cur_prop +
                                     " (" + rbx::prop_type_name(cur_type) + ")");
                cursor++; continue;
            }
            sub_cursor = 0;
            prop_loaded = true;
            return true;
        }
        return false;
    }

    // Traduce las propiedades de GUI de Roblox a los METODOS de las clases Roblox*
    // (RobloxFrame/TextLabel/...): en Godot no se llaman "Position"/"Size"/etc.
    // como propiedades sino set_udim2_pos()/set_bg_color()/... Sin esto, NINGUNA
    // propiedad de UI se aplicaba: todo Frame quedaba con su default (blanco,
    // ~100x50, pegado arriba-izquierda) — de ahi los recuadros blancos raros.
    // Devuelve true si la manejo (para no seguir con la ruta generica).
    bool _apply_gui_prop(Node *n, const String &prop, const Variant &val) {
        const int t = val.get_type();
        // Posicion / tamano (UDim2 -> Vector4(sx,ox,sy,oy))
        if (prop == "Position" && t == Variant::VECTOR4 && n->has_method("set_udim2_pos")) {
            Vector4 u = val; n->call("set_udim2_pos", u.x, u.y, u.z, u.w); return true;
        }
        if (prop == "Size" && t == Variant::VECTOR4 && n->has_method("set_udim2_size")) {
            Vector4 u = val; n->call("set_udim2_size", u.x, u.y, u.z, u.w); return true;
        }
        if (prop == "AnchorPoint" && t == Variant::VECTOR2 && n->has_method("set_anchor_point")) {
            Vector2 a = val; n->call("set_anchor_point", a.x, a.y); return true;
        }
        // Fondo
        if (prop == "BackgroundColor3" && t == Variant::COLOR && n->has_method("set_bg_color")) {
            Color c = val; n->call("set_bg_color", c.r, c.g, c.b); return true;
        }
        if (prop == "BackgroundTransparency" && n->has_method("set_bg_alpha")) {
            n->call("set_bg_alpha", (float)val); return true;
        }
        if (prop == "BorderColor3" && t == Variant::COLOR && n->has_method("set_border_color")) {
            Color c = val; n->call("set_border_color", c.r, c.g, c.b); return true;
        }
        if (prop == "BorderSizePixel" && n->has_method("set_border_px")) {
            n->call("set_border_px", (int)(int64_t)val); return true;
        }
        // ── UDim2 -> metodos con 4 floats ────────────────────────────
        // TileSize/CanvasSize/CellSize/CellPadding se guardan como UDim2 y
        // necesitan su setter propio (no son una propiedad simple).
        if (t == Variant::VECTOR4) {
            Vector4 u = val;
            struct { const char *prop; const char *method; } kU2[] = {
                { "TileSize",    "set_gl_tile_size" },
                { "CanvasSize",  "set_gl_canvas_size" },
                { "CellSize",    "set_gl_cell_size" },
                { "CellPadding", "set_gl_cell_padding" },
            };
            for (int i = 0; i < 4; i++)
                if (prop == kU2[i].prop && n->has_method(kU2[i].method)) {
                    n->call(kU2[i].method, u.x, u.y, u.z, u.w); return true;
                }
        }
        // ── UDim (escala + offset) -> metodos ────────────────────────
        // Roblox guarda UICorner.CornerRadius como los 4 radios, y el
        // UIPadding / UIListLayout.Padding como UDim. Sin traducirlos, el
        // redondeo y los margenes de TODA la interfaz se perdian.
        if (t == Variant::VECTOR2) {
            Vector2 d = val;   // (scale, offset)
            if ((prop == "CornerRadius" || prop == "TopLeftRadius" ||
                 prop == "TopRightRadius" || prop == "BottomLeftRadius" ||
                 prop == "BottomRightRadius") && n->has_method("set_gl_udim")) {
                n->call("set_gl_udim", d.x, (int)d.y); return true;
            }
            if (n->has_method("set_gl_pad")) {
                int side = -1;
                if (prop == "PaddingLeft")        side = 0;
                else if (prop == "PaddingTop")    side = 1;
                else if (prop == "PaddingRight")  side = 2;
                else if (prop == "PaddingBottom") side = 3;
                if (side >= 0) { n->call("set_gl_pad", side, d.x, (int)d.y); return true; }
            }
            if (prop == "Padding" && n->has_method("set_gl_padding_udim")) {
                n->call("set_gl_padding_udim", d.x, (int)d.y); return true;
            }
        }
        // FontFace llega como Dictionary {Family, Weight, Style}
        if (prop == "FontFace" && t == Variant::DICTIONARY && n->has_method("set_FontFace")) {
            Dictionary d = val;
            n->call("set_FontFace", String(d.get("Family", "")));
            // El peso y el estilo tambien: sin ellos todo Gotham Black salia normal.
            n->set_meta("_gl_font_weight", (int)(int64_t)d.get("Weight", 400));
            n->set_meta("_gl_font_style", (int)(int64_t)d.get("Style", 0));
            return true;
        }
        // Texto: set_rbx_text guarda el original para que RichText pueda
        // reprocesarlo cuando llegue esa propiedad (el orden no importa).
        if (prop == "Text" && n->has_method("set_rbx_text")) {
            n->call("set_rbx_text", String(val)); return true;
        }
        if (prop == "Text" && n->has_method("set_text")) {
            n->call("set_text", String(val)); return true;
        }
        if (prop == "TextColor3" && t == Variant::COLOR && n->has_method("set_text_color")) {
            Color c = val; n->call("set_text_color", c.r, c.g, c.b); return true;
        }
        if (prop == "TextSize" && n->has_method("set_text_size")) {
            n->call("set_text_size", (int)(float)val); return true;
        }
        if (prop == "TextScaled" && n->has_method("set_text_scaled")) {
            n->call("set_text_scaled", (bool)val); return true;
        }
        // Alineacion / contorno / rotacion / recorte: por el SETTER de la
        // propiedad si la clase la tiene (guarda el valor Y lo pinta); si no, el
        // helper de roblox_gui.h. Pintandolo solo, el campo se quedaba en el de
        // serie y un Clone lo perdia: las cabeceras y tarjetas que Build a
        // Mountain clona de plantillas salian centradas en vez de a la izquierda.
        if (gl_rbx_es_extra_gui(prop) && n->get(StringName(prop)).get_type() != Variant::NIL) {
            n->set(StringName(prop), val);
            return true;
        }
        if (gl_apply_rbx_gui_extra(n, prop, val)) return true;
        // Imagen (rbxassetid:// no se puede bajar; set_image marca el asset faltante)
        if ((prop == "Image" || prop == "Texture") && n->has_method("set_image")) {
            n->call("set_image", String(val)); return true;
        }
        if (prop == "ImageColor3" && t == Variant::COLOR && n->has_method("set_image_color")) {
            Color c = val; n->call("set_image_color", c.r, c.g, c.b); return true;
        }
        if (prop == "ImageTransparency" && n->has_method("set_image_transparency")) {
            n->call("set_image_transparency", (float)val); return true;
        }
        return false;
    }

    void _apply_one_prop(int k) {
        HashMap<int32_t, Inst>::Iterator ii = by_ref.find(cur_refs[k]);
        if (!ii) return;
        Node *n = ii->value.node;
        const Variant &v = cur_vals[k];

        if (cur_prop == "Name") {
            String raw = v;
            String safe = _safe_node_name(raw, cur_class);
            n->set_name(safe);
            if (safe != raw) n->set_meta("__rbx_name", raw);
            return;
        }
        if (cur_prop == "Source") {
            String src = v;
            Ref<LuauScript> ls; ls.instantiate();
            ls->_set_source_code(src);
            n->set("codigo_luau", ls);
            scripts_seen++;
            if (!src.is_empty()) scripts_imported++;
            current_item = "Script " + String::num_int64(scripts_seen) + " de " +
                           String::num_int64(scripts_total) + ": " + n->get_name();
            return;
        }
        // Roblox serializa los scripts con "Disabled"; GodotLuau expone la
        // propiedad contraria, "Enabled" (solo Script/LocalScript, como hace
        // Studio). Sin esta traduccion el Disabled acababa en un metadato y un
        // script desactivado en Roblox arrancaba igualmente aqui.
        // Doc: https://create.roblox.com/docs/reference/engine/classes/BaseScript
        // (BaseScript.Enabled: "the opposite of Disabled").
        if (cur_prop == "Disabled" && v.get_type() == Variant::BOOL &&
            _props_of(n).has("Enabled")) {
            n->set(StringName("Enabled"), !(bool)v);
            return;
        }
        if (cur_type == rbx::PT_REF) {
            int32_t tgt = (int32_t)(int64_t)v;
            if (tgt >= 0) { PendingRef pr; pr.node = n; pr.prop = cur_prop; pr.target = tgt;
                            pending_refs.push_back(pr); }
            return;
        }

        String target = rbx::map_property(cur_class, cur_prop);
        if (target.is_empty()) return;
        // Casi ninguna pieza tiene MaterialVariant: con el texto vacio no se apunta
        // nada (si no, cada pieza del mapa cargaria una meta que no dice nada).
        if (target == "MaterialVariant" && v.get_type() == Variant::STRING && String(v).is_empty()) return;

        Variant value = v;
        if (target == "Material")       value = rbx::map_material((int)(int64_t)v);
        else if (target == "Shape")     value = rbx::map_part_type((int)(int64_t)v);
        else if (cur_prop == "BrickColor") { target = "Color"; value = rbx::brick_color((int)(int64_t)v); }
        else if (target == "CFrame") {
            Node3D *n3 = gl_cast<Node3D>(n);
            if (n3) { n3->set_transform(v); return; }
        }

        if (value.get_type() == Variant::STRING) {
            String s = value;
            if (s.begins_with("rbxassetid://") || s.begins_with("rbxasset://") ||
                s.begins_with("http://www.roblox.com/asset")) {
                Dictionary d;
                d["Instancia"] = n->get_name();
                d["Propiedad"] = target;
                d["AssetId"] = s;
                missing_assets.push_back(d);
            }
        }

        // GUI: traducir Position/Size/BackgroundColor3/Text/... a los metodos de
        // las clases Roblox* (se usa el nombre ORIGINAL de Roblox, no el mapeado).
        if (_apply_gui_prop(n, cur_prop, value)) return;

        if (_props_of(n).has(target)) {
            n->set(StringName(target), value);
            return;
        }
        // La propiedad puede existir en Godot con otro nombre ("Visible" ->
        // "visible"). Sin esta pasada, un place importado enseña TODOS los
        // frames aunque en Roblox estuvieran ocultos.
        const String native = rbx::map_native_property(target);
        if (!native.is_empty() && _props_of(n).has(native)) {
            // Godot acota z_index a +-4096 y Roblox usa valores enormes
            // (999999999 para "siempre encima"): sin recortar suelta un error
            // por cada nodo.
            if (native == "z_index")
                value = CLAMP((int64_t)value, (int64_t)-4096, (int64_t)4096);
            n->set(StringName(native), value);
            return;
        }
        _set_meta_safe(n, target, value);
        _bump(ignored_props, cur_class + String(".") + target);
    }

    bool _do_properties() {
        int budget = _lote(BATCH_VALUES);
        while (budget > 0) {
            if (!prop_loaded && !_load_next_prop()) {
                _hornear_uniones();
                _guardar_terreno();
                cursor = 0; phase = PH_HIERARCHY; return true;
            }
            const int total = cur_vals.size();
            int end = MIN(sub_cursor + budget, total);
            budget -= (end - sub_cursor);
            for (; sub_cursor < end; sub_cursor++) _apply_one_prop(sub_cursor);
            if (sub_cursor >= total) {
                if (cur_prop != "Source")
                    current_item = cur_class + "." + cur_prop;
                prop_loaded = false;
                cursor++;
            }
        }
        return true;
    }

    // Uniones: su malla sale del CSG de sus piezas (rbx_uniones.h), una por
    // contenido. Se guarda en assets/rbx/csg/ para que la escena guardada la
    // encuentre; si no se puede escribir (un juego exportado), vale en memoria.
    void _hornear_uniones() {
        HashMap<String, String> hechas;   // huella -> ruta ("" = no salio)
        for (KeyValue<int32_t, PackedByteArray> &e : datos_union) {
            HashMap<int32_t, Inst>::Iterator ii = by_ref.find(e.key);
            RBXMeshPart *mp = ii ? gl_cast<RBXMeshPart>(ii->value.node) : nullptr;
            if (!mp) continue;
            const bool colores = !(mp->has_meta("UsePartColor") && (bool)mp->get_meta("UsePartColor"));
            const String huella = rbx::huella_csg(e.value, colores);
            String ruta;
            if (const String *hay = hechas.getptr(huella)) {
                ruta = *hay;
            } else {
                const Ref<ArrayMesh> m = rbx::malla_de_union(e.value, colores);
                if (m.is_valid()) {
                    ruta = String("res://GodotLuau/assets/rbx/csg/") + huella + ".res";
                    DirAccess::make_dir_recursive_absolute("res://GodotLuau/assets/rbx/csg");
                    ResourceSaver::get_singleton()->save(m, ruta);
                    gl_mallas_locales()[ruta] = m;   // gl_malla_local la ve sin cargarla
                }
                hechas[huella] = ruta;
            }
            if (ruta.is_empty()) { uniones_sin_piezas++; continue; }
            mp->set_gl_malla_csg(ruta);
            uniones_horneadas++;
        }
        datos_union.clear();
    }

    // El SmoothGrid va a su archivo (1,2 MB no caben con gracia en un .tscn) y el
    // Terrain lo convierte en malla al entrar en el arbol (gl_terreno.h).
    void _guardar_terreno() {
        HashMap<int32_t, Inst>::Iterator ii = by_ref.find(terreno_ref);
        Node *t = ii ? ii->value.node : nullptr;
        if (!t || terreno_grid.size() <= 2) return;
        GLTerreno prueba;
        if (!gl_terreno_decodificar(terreno_grid, prueba, terreno_error)) return;
        terreno_chunks = (int)prueba.chunks.size();
        Ref<HashingContext> h;
        h.instantiate();
        h->start(HashingContext::HASH_MD5);
        h->update(terreno_grid);
        const String ruta = String("res://GodotLuau/assets/rbx/terreno/") + h->finish().hex_encode() + ".bin";
        DirAccess::make_dir_recursive_absolute("res://GodotLuau/assets/rbx/terreno");
        Ref<FileAccess> f = FileAccess::open(ruta, FileAccess::WRITE);
        if (f.is_valid()) { f->store_buffer(terreno_grid); f->close(); }
        PackedColorArray colores;
        for (int k = 0; k + 2 < terreno_colores.size() && colores.size() < 23; k += 3)
            colores.push_back(Color(terreno_colores[k] / 255.0f, terreno_colores[k + 1] / 255.0f, terreno_colores[k + 2] / 255.0f));
        t->set("GLColoresTerreno", colores);
        t->set("GLDatosTerreno", ruta);
        terreno_grid = PackedByteArray();
    }

    bool _do_hierarchy() {
        if (link_child.is_empty()) {
            if (prnt_chunks.is_empty()) { cursor = 0; phase = PH_REFS; return true; }
            const rbx::Chunk &c = file.chunks[prnt_chunks[0]];
            rbx::Reader r(c.data.ptr(), c.data.size());
            r.u8();
            uint32_t count = r.u32le();
            if (!r.ok() || count > file.instance_count + 1024) { cursor = 0; phase = PH_REFS; return true; }
            LocalVector<int32_t> ch, pa; ch.resize(count); pa.resize(count);
            r.read_ref_array(count, ch.ptr());
            r.read_ref_array(count, pa.ptr());
            if (!r.ok()) { cursor = 0; phase = PH_REFS; return true; }
            for (uint32_t k = 0; k < count; k++) { link_child.push_back(ch[k]); link_parent.push_back(pa[k]); }
            cursor = 0;
        }

        const int total = link_child.size();
        int end = MIN(cursor + _lote(BATCH_LINKS), total);
        for (; cursor < end; cursor++) {
            HashMap<int32_t, Inst>::Iterator ci = by_ref.find(link_child[cursor]);
            if (!ci) continue;
            Node *child = ci->value.node;
            if (child->get_parent()) continue;
            // parent == -1 es el DataModel: servicios de primer nivel, legitimo.
            if (link_parent[cursor] < 0) { _attach(root, child); continue; }
            // Padre referenciado pero NO definido en el archivo: irrecuperable.
            // Va al contenedor RecoveredInstances, no suelto en Game.
            HashMap<int32_t, Inst>::Iterator pi = by_ref.find(link_parent[cursor]);
            if (!pi) { _attach(_orphan_container(), child); orphans++; continue; }
            _attach(pi->value.node, child);
        }
        current_item = String::num_int64(cursor) + " / " + String::num_int64(total) + " enlaces";
        if (cursor >= total) {
            // Nada puede quedar suelto: seria una fuga y ademas se perderia.
            // (Instancia definida pero ausente del PRNT: tampoco es del juego,
            // asi que al contenedor de recuperados con las demas.)
            for (HashMap<int32_t, Inst>::Iterator it = by_ref.begin(); it; ++it) {
                Node *n = it->value.node;
                if (n && n != root && !n->get_parent()) { _attach(_orphan_container(), n); orphans++; }
            }
            cursor = 0; phase = PH_REFS;
        }
        return true;
    }

    bool _do_refs() {
        const int total = pending_refs.size();
        int end = MIN(cursor + _lote(BATCH_REFS), total);
        for (; cursor < end; cursor++) {
            const PendingRef &pr = pending_refs[cursor];
            HashMap<int32_t, Inst>::Iterator ti = by_ref.find(pr.target);
            if (!ti || !ti->value.node) continue;
            Node *tgt = ti->value.node;
            String prop = rbx::map_property(String(), pr.prop);
            if (prop.is_empty()) continue;
            if (_props_of(pr.node).has(prop)) {
                Variant cur = pr.node->get(StringName(prop));
                if (cur.get_type() == Variant::NODE_PATH)
                    pr.node->set(StringName(prop), pr.node->get_path_to(tgt));
                else if (cur.get_type() == Variant::STRING)   // Motor6D.Part0/Part1: la ruta
                    pr.node->set(StringName(prop), String(pr.node->get_path_to(tgt)));
                else
                    pr.node->set(StringName(prop), tgt);
            } else {
                _set_meta_safe(pr.node, prop, pr.node->get_path_to(tgt));
            }
            refs_ok++;
        }
        current_item = String::num_int64(cursor) + " / " + String::num_int64(total) + " referencias";
        if (cursor >= total) { cursor = 0; phase = PH_VERIFY; }
        return true;
    }

    // Comprobado: cada instancia del archivo debe tener nodo y estar colgada
    // dentro del arbol, por si algo se saltó en las fases anteriores.
    bool _do_verify() {
        const int total = flat_ref.size();
        int end = MIN(cursor + BATCH_VERIFY, total);
        for (; cursor < end; cursor++) {
            HashMap<int32_t, Inst>::Iterator it = by_ref.find(flat_ref[cursor]);
            const String cname = class_names.has(flat_class[cursor])
                                 ? class_names[flat_class[cursor]] : String("?");
            if (!it || !it->value.node) {
                if (verify_errors.size() < 200)
                    verify_errors.push_back(String("Sin nodo: ") + cname);
                continue;
            }
            Node *n = it->value.node;
            if (n != root && !n->get_parent()) {
                _attach(_orphan_container(), n);
                orphans++;
                if (verify_errors.size() < 200)
                    verify_errors.push_back(String("Recuperado suelto (en RecoveredInstances): ") + cname + " / " + n->get_name());
                continue;
            }
            verified_ok++;
        }
        current_item = String::num_int64(cursor) + " / " + String::num_int64(total) + " comprobadas";
        if (cursor >= total) { cursor = 0; phase = PH_PLACE; }
        return true;
    }

public:
    // ── Recolocado ──────────────────────────────────────────────────
    // Busca un Game (DataModel) ya montado en la escena para reutilizarlo.
    static Node *find_existing_game(Node *scene_root) {
        if (!scene_root) return nullptr;
        if (scene_root->is_class("RobloxTemplate") || scene_root->is_class("RobloxGame3D"))
            return scene_root;
        // Un Game valido es el que tiene un Workspace dentro.
        if (scene_root->get_node_or_null(NodePath("Workspace"))) return scene_root;
        for (int k = 0; k < scene_root->get_child_count(); k++) {
            Node *c = scene_root->get_child(k);
            if (c->is_class("RobloxTemplate") || c->is_class("RobloxGame3D")) return c;
            if (c->get_node_or_null(NodePath("Workspace"))) return c;
        }
        return nullptr;
    }

private:
    // Punto de aparicion "sobre el mapa": centroide XZ de las Parts ancladas
    // importadas y por encima de todo (tope Y). Asi el jugador cae en el centro
    // del mapa en vez de quedarse en la placa blanca del origen. Devuelve false
    // si no hay geometria anclada (nada que medir).
    bool _map_spawn_point(Node *ws, Vector3 &out) {
        if (!ws || !ws->is_inside_tree()) return false;
        bool found = false;
        double sx = 0, sz = 0;
        int n = 0;
        float maxY = 0, minY = 0;
        Vector<Node *> stack;
        stack.push_back(ws);
        while (!stack.is_empty()) {
            Node *nd = stack[stack.size() - 1];
            stack.remove_at(stack.size() - 1);
            if (nd->is_class("RobloxPart") && nd->has_meta("__rbx_class")) {
                Variant anc = nd->get("Anchored");
                if (anc.get_type() == Variant::BOOL && (bool)anc) {
                    if (Node3D *p = gl_cast<Node3D>(nd)) {
                        // El arbol importado aun NO esta dentro del SceneTree:
                        // pedir la posicion global aqui solo imprime un error y
                        // devuelve cero. La local es la buena en este punto.
                        Vector3 gp = p->is_inside_tree() ? p->get_global_position() : p->get_position();
                        sx += gp.x; sz += gp.z; n++;
                        if (!found) { maxY = minY = gp.y; found = true; }
                        else { if (gp.y > maxY) maxY = gp.y; if (gp.y < minY) minY = gp.y; }
                    }
                }
            }
            for (int k = 0; k < nd->get_child_count(); k++) stack.push_back(nd->get_child(k));
        }
        if (!found || n == 0) return false;
        float topY = maxY + 5.0f;
        // Evitar caidas absurdas cuando hay un skybox altisimo: si el mapa es muy
        // alto, aparecer a media altura y no desde el techo.
        if (maxY - minY > 300.0f) topY = minY + (maxY - minY) * 0.5f;
        out = Vector3((float)(sx / n), topY, (float)(sz / n));
        return true;
    }

    // Los nodos que vienen del .rbxl llevan la marca __rbx_class; los de
    // relleno de la plantilla llevan _gl_template_generated. Si el place aporta
    // puntos de aparicion propios, se quitan los de relleno. Si NO trae ninguno
    // (spawn por script, comun en muchos juegos), la placa de relleno se REUBICA
    // sobre el mapa en vez de dejarla en el origen (0,0,0) lejos del mapa, que
    // es por lo que el jugador aparecia en una placa blanca solitaria.
    // v1.17: "de relleno" ahora exige la marca de plantilla. Antes bastaba con
    // no venir del archivo, y eso podia borrar un SpawnLocation que el usuario
    // hubiera creado A MANO en Godot (prohibido). En proyectos viejos sin marca
    // no se toca ninguno: conservador.
    void _drop_default_spawns(Node *ws) {
        Vector<Node *> imported, defaults;
        Vector<Node *> stack;
        stack.push_back(ws);
        while (!stack.is_empty()) {
            Node *n = stack[stack.size() - 1];
            stack.remove_at(stack.size() - 1);
            if (n->is_class("SpawnLocation")) {
                if (n->has_meta("__rbx_class"))                     imported.push_back(n);
                else if (_is_marked(n) && _subtree_all_marked(n))   defaults.push_back(n);
                // sin marca y sin venir del archivo: es del usuario, ni tocarlo
            }
            for (int k = 0; k < n->get_child_count(); k++) stack.push_back(n->get_child(k));
        }
        if (!imported.is_empty()) {
            // El place trae sus propios spawns: los de relleno sobran.
            for (int k = 0; k < defaults.size(); k++) {
                Node *d = defaults[k];
                if (d->get_parent()) d->get_parent()->remove_child(d);
                memdelete(d);
            }
            report["spawns_de_relleno_quitados"] = defaults.size();
            return;
        }
        // Sin spawn propio: reubicar UNA placa de relleno sobre el mapa.
        if (defaults.is_empty()) return;
        Vector3 spawn;
        if (!_map_spawn_point(ws, spawn)) return;   // no hay mapa que medir
        if (Node3D *keep = gl_cast<Node3D>(defaults[0]))
            keep->set_position(spawn);
        for (int k = 1; k < defaults.size(); k++) {   // sobran las demas
            Node *d = defaults[k];
            if (d->get_parent()) d->get_parent()->remove_child(d);
            memdelete(d);
        }
        report["spawn_de_relleno_reubicado"] = spawn;
    }

    // Pasa la CONFIGURACION del nodo importado al nodo que se reutiliza en su
    // lugar (misma clase). Antes el nodo importado se descartaba entero y con
    // el se perdian Workspace.Gravity, Lighting.ClockTime, StarterPlayer.
    // CharacterWalkSpeed... aunque el place las trajera cambiadas. Solo se
    // copian propiedades estilo Roblox (PascalCase): las nativas de Godot
    // (position, visible, ...) del nodo existente no se tocan.
    static void _copy_rbx_props(Node *from, Node *to) {
        if (!from || !to || from->get_class() != to->get_class()) return;
        TypedArray<Dictionary> pl = from->get_property_list();
        for (int k = 0; k < pl.size(); k++) {
            Dictionary d = pl[k];
            const String pname = d.get("name", String());
            if (pname.is_empty()) continue;
            const char32_t f = pname[0];
            if (!(f >= 'A' && f <= 'Z')) continue;
            const int usage = (int)d.get("usage", 0);
            if (!(usage & PROPERTY_USAGE_STORAGE)) continue;
            if (usage & PROPERTY_USAGE_READ_ONLY) continue;
            to->set(StringName(pname), from->get(StringName(pname)));
        }
        // El codigo del script (propiedad interna "codigo_luau", en minuscula:
        // el filtro PascalCase no la ve) es el Source de Roblox y tambien es
        // configuracion del place: sin esto un script fusionado se quedaba con
        // el codigo viejo.
        {
            Variant code = from->get(StringName("codigo_luau"));
            if (code.get_type() == Variant::OBJECT && (Object *)code != nullptr)
                to->set(StringName("codigo_luau"), code);
        }
        // Los metadatos (atributos, tags, clase Roblox) tambien viajan, para
        // que GetAttribute/GetTags funcionen igual sobre el nodo reutilizado.
        TypedArray<StringName> metas = from->get_meta_list();
        for (int k = 0; k < metas.size(); k++) {
            const StringName mn = metas[k];
            if (mn == StringName("_gl_template_generated")) continue;
            to->set_meta(mn, from->get_meta(mn));
        }
    }

    // Fusiona los hijos de src dentro de dst SIN duplicar (el bug de "creas
    // cosas que ya existen": StarterPlayerScripts, Modules, Baseplate... salian
    // repetidos como @Nombre@2). Reglas, por hijo con el mismo nombre en dst:
    //   · dst SIN marca de plantilla  -> es del usuario: NO se toca; el
    //     importado entra como hermano (Godot lo renombra) y se cuenta.
    //   · dst plantilla INTACTA       -> la version del place lo SUSTITUYE
    //     entera (asi un Baseplate encogido del place queda encogido aqui).
    //   · dst plantilla CON contenido del usuario dentro -> se conserva el
    //     nodo, se le copia la configuracion del place y se fusiona registro
    //     a registro hacia dentro.
    void _merge_children(Node *dst, Node *src) {
        Vector<Node *> kids;
        for (int j = 0; j < src->get_child_count(); j++) kids.push_back(src->get_child(j));
        for (int j = 0; j < kids.size(); j++) {
            Node *kid = kids[j];
            Node *match = dst->get_node_or_null(NodePath(kid->get_name()));
            if (!match) {
                src->remove_child(kid);
                _attach(dst, kid);
                continue;
            }
            if (!_is_marked(match)) {
                src->remove_child(kid);
                _attach(dst, kid);
                dup_conflicts++;
                continue;
            }
            if (_subtree_all_marked(match)) {
                dst->remove_child(match);
                memdelete(match);
                src->remove_child(kid);
                _attach(dst, kid);
                defaults_replaced++;
                continue;
            }
            _copy_rbx_props(kid, match);
            _merge_children(match, kid);
            src->remove_child(kid);
            memdelete(kid);
        }
    }

    // Roblox Studio OCULTA en el Explorer la mayoria de los servicios que el
    // archivo serializa (TimerService, StudioData, PlayerEmulatorService...):
    // en el place de prueba son 38 de 52. Colgarlos de Game a pelo era lo que
    // llenaba la raiz de nodos "de mas" que no existen en el arbol de Roblox.
    // Se agrupan en Game/HiddenServices: GetService los sigue encontrando
    // (la busqueda es recursiva por nombre, ver luau_api.h).
    void _group_hidden_services() {
        Vector<Node *> hide;
        for (int k = 0; k < root->get_child_count(); k++) {
            Node *c = root->get_child(k);
            if (!c->has_meta("__rbx_class")) continue;      // solo lo que vino del archivo
            const String rc = c->get_meta("__rbx_class");
            if (rbx::is_explorer_service(rc)) continue;     // los 14 visibles se quedan
            // Solo clases claramente de servicio: un Model/Part suelto de un
            // .rbxm NO es un servicio y debe quedarse a la vista.
            if (!rbx::is_service(rc) && rc != "StudioData" && rc != "AvatarSettings") continue;
            hide.push_back(c);
        }
        if (hide.is_empty()) return;
        Node *box = root->get_node_or_null(NodePath("HiddenServices"));
        if (!box) {
            box = gl_cast<Node>(ClassDB::instantiate(StringName("Folder")));
            if (!box) box = memnew(Node);
            box->set_name("HiddenServices");
            root->add_child(box);
        }
        for (int k = 0; k < hide.size(); k++) {
            Node *c = hide[k];
            root->remove_child(c);
            _attach(box, c);
        }
        hidden_grouped += hide.size();
    }

    // El place define el mundo entero: si su Workspace NO traia "Baseplate",
    // en Roblox ese suelo no existe y aqui tampoco debe quedarse. Solo se
    // quita el MARCADO como plantilla e intacto (si el place trajo el suyo, la
    // fusion ya lo sustituyo y este chequeo no encuentra nada que quitar); un
    // Baseplate hecho o tocado por el usuario no se toca jamas.
    void _drop_default_baseplate(Node *ws) {
        Node *bp = ws->get_node_or_null(NodePath("Baseplate"));
        if (!bp || bp->has_meta("__rbx_class")) return;     // no hay, o es el del place
        if (!_is_marked(bp) || !_subtree_all_marked(bp)) return;
        // Solo si el place aporto geometria de verdad: sin mapa importado la
        // placa de la plantilla sigue haciendo de suelo.
        bool imported_geom = false;
        Vector<Node *> stack;
        stack.push_back(ws);
        while (!stack.is_empty() && !imported_geom) {
            Node *n = stack[stack.size() - 1];
            stack.remove_at(stack.size() - 1);
            if (n != ws && n->has_meta("__rbx_class") && gl_cast<Node3D>(n))
                imported_geom = true;
            for (int k = 0; k < n->get_child_count(); k++) stack.push_back(n->get_child(k));
        }
        if (!imported_geom) return;
        ws->remove_child(bp);
        memdelete(bp);
        report["baseplate_de_plantilla_quitado"] = true;
    }

    // Vuelca el contenido del arbol importado en la estructura destino:
    // los servicios que ya existan se REUTILIZAN (contenido fusionado sin
    // duplicar y configuracion copiada) y los que falten se crean.
    // ════════════════════════════════════════════════════════════════
    //  PAQUETE .gdlu (GodotLuau Exporter)
    //
    //  Un .gdlu es el MISMO formato binario de Roblox (Studio lo guarda con
    //  extension .rbxm), pero con el juego entero dentro de una carpeta
    //  "GDLU" y — esto es lo que no puede el .rbxl — las IMAGENES del juego
    //  embebidas, leidas en Studio con la sesion del usuario.
    //
    //      GDLU/
    //        GDLU_Manifest   StringValue con JSON {formato, juego, placeId}
    //        SVC_Workspace   Folder con los hijos del servicio
    //        SVC_...         (uno por servicio exportado)
    //        GDLU_Lighting   StringValue con JSON de las props de Lighting
    //        GDLU_Servicios  StringValue con JSON {servicio: {prop: valor}}
    //                        (StarterPlayer, Players, Workspace)
    //        GDLU_Assets/
    //          <id>_<ancho>_<alto>/   "1","2",... StringValue con base64
    //        GDLU_Mallas/
    //          <id>/                  "1","2",... base64 de la malla (GLM1, abajo)
    //        GDLU_Animaciones/
    //          <id>                   la KeyframeSequence/CurveAnimation tal cual
    //
    //  Aqui se deshace ese envoltorio para que el resto del importador vea
    //  exactamente lo mismo que ve con un .rbxl: servicios colgando de root.
    // ════════════════════════════════════════════════════════════════
    static String _gdlu_dir_assets() { return "res://GodotLuau/assets/rbx/"; }

    bool _gdlu_imagen(Node *caja);
    bool _gdlu_malla(Node *caja);

    Node *luz_destino = nullptr;   // el servicio Lighting recien creado

    static Variant _gdlu_valor(const String &s);
    void _gdlu_lighting(Node *sv);
    void _gdlu_aplicar_lighting();
    Dictionary luz_props;
    void _gdlu_aplicar_servicios(const Dictionary &por_servicio);

    bool _gdlu_desenvolver();

    bool _do_place() {
        // .gdlu: quitar el envoltorio ANTES de nada (deja servicios en root).
        _gdlu_desenvolver();
        // Primero, con o sin destino: los servicios ocultos fuera de la raiz.
        _group_hidden_services();

        // Sin escena destino (importacion suelta: RBXImporter usado desde un
        // script, o el place abierto tal cual): igual hay que asegurar los
        // scripts de jugador de fabrica. Si no, el place trae su propio
        // StarterPlayerScripts y el juego se queda SIN menu de Escape en Luau y
        // sin los modulos de control y camara.
        if (!target_game || target_game == root) {
            gl_ensure_default_player_scripts(root);
            phase = PH_DONE; _finish(); return true;
        }

        // Solo 3D: un Workspace 2D no puede recibir un place de Roblox.
        Node *tws = target_game->get_node_or_null(NodePath("Workspace"));
        if (tws && tws->is_class("RobloxWorkspace2D")) {
            _fail("La escena destino usa un Workspace 2D. El importador de .rbxl "
                  "solo funciona en proyectos 3D.");
            return false;
        }

        Vector<Node *> services;
        for (int k = 0; k < root->get_child_count(); k++) services.push_back(root->get_child(k));

        for (int k = 0; k < services.size(); k++) {
            Node *svc = services[k];
            const String svc_name = svc->get_name();
            Node *dst = target_game->get_node_or_null(NodePath(svc_name));

            if (dst && dst != svc) {
                // Ya existe: se copia su configuracion (solo si es de
                // plantilla; uno del usuario no se pisa) y se fusiona el
                // contenido sin duplicar hijos que ya esten.
                if (_is_marked(dst)) _copy_rbx_props(svc, dst);
                _merge_children(dst, svc);
                root->remove_child(svc);
                memdelete(svc);
                reused_services++;
            } else {
                // No existe: el servicio importado pasa tal cual al destino.
                root->remove_child(svc);
                _attach(target_game, svc);
                created_services++;
            }
        }

        // El Game importado ya esta vacio.
        if (root && !root->get_parent()) { memdelete(root); }
        root = target_game;

        // La fusion puede SUSTITUIR un nodo de plantilla intacto por la version
        // del place (regla de _merge_children): si el place trae su propio
        // StarterPlayerScripts, el PlayerModule/Modules/Menu de fabrica se iban
        // con el nodo sustituido y el juego quedaba sin controles ni menu. Se
        // re-asegura la estructura por defecto (idempotente: solo crea lo que
        // falte, no toca nada del place). Va ANTES de la limpieza de spawns
        // para que un SpawnLocation de relleno recreado aqui se retire igual.
        if (target_game->has_method("_gl_update_structure"))
            target_game->call("_gl_update_structure");
        else
            gl_ensure_default_player_scripts(target_game);   // raiz que no es RobloxTemplate

        if (RobloxWorkspace *ws = gl_cast<RobloxWorkspace>(
                target_game->get_node_or_null(NodePath("Workspace")))) {
            // Si el place trae sus propios puntos de aparicion, el SpawnLocation
            // de relleno del template (en el origen) sobra: el mapa importado
            // suele estar lejisimos del 0,0,0 y el jugador aparecia en el vacio.
            _drop_default_spawns(ws);
            // Y lo mismo con el Baseplate de plantilla si el place no trae uno.
            _drop_default_baseplate(ws);
            // El Workspace llego lleno de golpe y pudo quedarse sin cielo ni
            // sol: se lo pedimos ahora que ya tiene todo dentro.
            ws->gl_ensure_environment();
        }

        phase = PH_DONE;
        _finish();
        return true;
    }

    void _finish() {
        const uint64_t ms = Time::get_singleton()->get_ticks_msec() - t_start;
        report["instancias_totales"]     = (int)file.instance_count;
        report["nodos_creados"]          = (int)by_ref.size();
        report["clases_en_archivo"]      = (int)file.class_count;
        report["por_clase"]              = class_counts;
        report["clases_sin_equivalente"] = missing_classes;
        report["propiedades_ignoradas"]  = ignored_props;
        report["assets_faltantes"]       = missing_assets;
        report["scripts_importados"]     = scripts_imported;
        report["tags_aplicados"]         = tags_applied;
        report["atributos_aplicados"]    = attrs_applied;
        report["uniones_horneadas"]      = uniones_horneadas;   // su malla salio del CSG de sus piezas
        report["uniones_sin_piezas"]     = uniones_sin_piezas;
        report["terreno_chunks"]         = terreno_chunks;      // chunks de 32^3 voxeles del SmoothGrid
        if (!terreno_error.is_empty()) report["terreno_error"] = terreno_error;
        report["referencias_resueltas"]  = refs_ok;
        report["huerfanos"]              = orphans;
        report["comprobadas_ok"]         = verified_ok;
        report["errores_comprobacion"]   = verify_errors;
        report["servicios_reutilizados"] = reused_services;
        report["servicios_creados"]      = created_services;
        // v1.17: trazabilidad de la fusion 1:1
        report["hermanos_renombrados"]   = renamed_siblings;   // duplicados que Godot renombro (@X@2)
        report["servicios_ocultos"]      = hidden_grouped;     // agrupados en Game/HiddenServices
        report["plantilla_sustituida"]   = defaults_replaced;  // nodos de plantilla sustituidos por los del place
        report["conflictos_con_usuario"] = dup_conflicts;      // nombres que chocaron con nodos del usuario
        report["tiempo_ms"]              = (int)ms;
        current_item = "Listo";
        if (verbose)
            UtilityFunctions::print("[RBX] ", (int)by_ref.size(), " nodos en ", (int)ms,
                                    " ms | reutilizados ", reused_services,
                                    " servicios, creados ", created_services);
    }

    // ════════════════════════════════════════════════════════════════
    //  MONTAJE REPARTIDO EN VARIOS FRAMES
    //
    //  Meter el place en el arbol de una sentada son 46.727 ENTER_TREE en UN
    //  solo frame: 934 ms clavados nada mas entrar (medido en el place de
    //  prueba). Ahi dentro va el montaje de cada Part y el arranque de los 264
    //  scripts, asi que no hay forma de hacerlo mas barato — pero si de
    //  repartirlo, que es lo que hace Roblox.
    //
    //  Como: con el arbol TODAVIA FUERA de la escena (o sea, sin que salte un
    //  solo ENTER_TREE), se desenganchan los hijos de los nodos gordos y se
    //  apuntan en una cola en anchura — padres antes que hijos. Luego se
    //  reenganchan con presupuesto de milisegundos por frame.
    //
    //  El ORDEN de hermanos se respeta: cuando un nodo se pela, se le quitan
    //  TODOS los hijos y se encolan por indice, asi que vuelven en el mismo
    //  orden. Un subarbol pequeño no se pela: viaja entero con su padre.
    //
    //  ⚠ SIN TERMINAR — NO USAR TODAVIA. Nadie lo llama por defecto.
    //  La parte de Luau ya esta resuelta (gl_montando_mundo hace que los scripts
    //  esperen al mundo completo), pero el C++ tambien da por hecho que el
    //  DataModel esta entero en cuanto entra al arbol. Ya se aplazo el sellado de
    //  la replicacion (_stamp_static_ids), y aun asi casca con SIGSEGV: falta
    //  auditar el resto de caminos que reaccionan al ENTER_TREE del DataModel
    //  (spawn del jugador, clonado de StarterGui, scripts por defecto...).
    //  Medido, lo que esto ahorraria: el add_child de una sentada son 934 ms en
    //  UN frame con 46.727 nodos; pelar el arbol cuesta 14,8 ms y deja 4.817
    //  trozos, que a 6 ms por frame son ~2 s de carga suave.
    // ════════════════════════════════════════════════════════════════
    struct GLTrozo { uint64_t padre; uint64_t hijo; };
    std::vector<GLTrozo> montaje_cola;
    size_t montaje_cursor = 0;
    bool   montaje_activo = false;

    // Cuenta descendientes, pero se planta en `tope`: solo hace falta saber si
    // el subarbol es grande, no cuanto.
    int _contar_hasta(Node* n, int tope) {
        if (!n) return 0;
        int c = 1;
        for (int i = 0; i < n->get_child_count() && c < tope; i++)
            c += _contar_hasta(n->get_child(i), tope - c);
        return c;
    }

    void _pelar_en_anchura(Node* raiz, int umbral) {
        std::vector<Node*> frontera;
        frontera.push_back(raiz);
        int guarda = 0;
        while (!frontera.empty() && guarda++ < 100000) {
            std::vector<Node*> siguiente;
            for (Node* n : frontera) {
                if (!n || n->get_child_count() == 0) continue;
                if (_contar_hasta(n, umbral + 1) <= umbral) continue;   // pequeño: entero
                std::vector<Node*> hijos;
                for (int i = 0; i < n->get_child_count(); i++) hijos.push_back(n->get_child(i));
                for (Node* h : hijos) {
                    if (!h) continue;
                    n->remove_child(h);
                    montaje_cola.push_back({ (uint64_t)n->get_instance_id(),
                                             (uint64_t)h->get_instance_id() });
                    siguiente.push_back(h);
                }
            }
            frontera.swap(siguiente);
        }
    }

public:
    // Prepara el montaje repartido y mete la raiz (ya ligera) en `destino`.
    void mount_begin(Node* destino) {
        Node* raiz = get_result();
        if (!raiz || !destino) return;
        UtilityFunctions::push_warning(
            "[GodotLuau] MountBegin: el montaje repartido AUN NO ESTA TERMINADO "
            "(casca con places que traen scripts). Usa add_child() normal.");
        montaje_cola.clear();
        montaje_cursor = 0;
        _pelar_en_anchura(raiz, 48);
        gl_montando_mundo() = true;   // los scripts esperan a tener el mundo entero
        destino->add_child(raiz);
        montaje_activo = true;
    }

    // Reengancha lo que quepa en el presupuesto. Devuelve true cuando termina.
    bool mount_step(int presupuesto_ms) {
        if (!montaje_activo) return true;
        if (presupuesto_ms < 1) presupuesto_ms = 1;
        const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
        while (montaje_cursor < montaje_cola.size()) {
            const GLTrozo t = montaje_cola[montaje_cursor++];
            Node* p = gl_cast<Node>(ObjectDB::get_instance(ObjectID(t.padre)));
            Node* h = gl_cast<Node>(ObjectDB::get_instance(ObjectID(t.hijo)));
            if (p && h && !h->get_parent()) p->add_child(h);
            if ((Time::get_singleton()->get_ticks_usec() - t0) >= (uint64_t)presupuesto_ms * 1000)
                return false;
        }
        montaje_cola.clear();
        montaje_cursor = 0;
        montaje_activo = false;
        gl_montando_mundo() = false;   // mundo completo: que arranquen los scripts
        return true;
    }

    bool mount_done() const { return !montaje_activo; }
    int  mount_left() const { return (int)(montaje_cola.size() - montaje_cursor); }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("MountBegin", "destino"), &RBXImporter::mount_begin);
        ClassDB::bind_method(D_METHOD("MountStep", "ms"),       &RBXImporter::mount_step);
        ClassDB::bind_method(D_METHOD("MountDone"),             &RBXImporter::mount_done);
        ClassDB::bind_method(D_METHOD("MountLeft"),             &RBXImporter::mount_left);
        ClassDB::bind_method(D_METHOD("Begin", "path"),        &RBXImporter::begin_import);
        // Analisis previo con pausa (1.17)
        ClassDB::bind_method(D_METHOD("SetPauseAfterAnalysis","v"), &RBXImporter::set_pausar_tras_analisis);
        ClassDB::bind_method(D_METHOD("IsAnalyzed"),           &RBXImporter::is_analyzed);
        ClassDB::bind_method(D_METHOD("GetAnalysis"),          &RBXImporter::get_analysis);
        ClassDB::bind_method(D_METHOD("Continue"),             &RBXImporter::continuar);
        ClassDB::bind_method(D_METHOD("Reject"),               &RBXImporter::rechazar);
        ClassDB::bind_method(D_METHOD("Step"),                 &RBXImporter::step);
        ClassDB::bind_method(D_METHOD("GetProgress"),          &RBXImporter::get_progress);
        ClassDB::bind_method(D_METHOD("GetPhaseName"),         &RBXImporter::get_phase_name);
        ClassDB::bind_method(D_METHOD("GetCurrentItem"),       &RBXImporter::get_current_item);
        ClassDB::bind_method(D_METHOD("GetResult"),            &RBXImporter::get_result);
        ClassDB::bind_method(D_METHOD("SetTarget", "game"),    &RBXImporter::set_target);
        ClassDB::bind_method(D_METHOD("ImportFile", "path"),   &RBXImporter::import_file);
        ClassDB::bind_method(D_METHOD("GetLastError"),         &RBXImporter::get_last_error);
        ClassDB::bind_method(D_METHOD("GetReport"),            &RBXImporter::get_report);
        ClassDB::bind_method(D_METHOD("SetVerbose", "v"),      &RBXImporter::set_verbose);
        ClassDB::bind_method(D_METHOD("IsDone"),               &RBXImporter::is_done);
        ClassDB::bind_method(D_METHOD("HasFailed"),            &RBXImporter::has_failed);
        // Alias snake_case para GDScript
        ClassDB::bind_method(D_METHOD("import_file", "path"),  &RBXImporter::import_file);
        ClassDB::bind_method(D_METHOD("get_last_error"),       &RBXImporter::get_last_error);
        ClassDB::bind_method(D_METHOD("get_report"),           &RBXImporter::get_report);
        ClassDB::bind_method(D_METHOD("set_verbose", "v"),     &RBXImporter::set_verbose);
    }

public:
    void set_verbose(bool v) { verbose = v; }
    String get_last_error() const { return last_error; }
    Dictionary get_report() const { return report; }
    Node *get_result() { return root; }
    bool is_done() const { return phase == PH_DONE; }
    bool has_failed() const { return phase == PH_ERROR; }
    String get_current_item() const { return current_item; }

    // Game existente con el que fusionar. Si es null, el resultado se queda como
    // un arbol suelto que el llamante colgara donde quiera.
    void set_target(Node *game) { target_game = game; }

    // EN INGLES: el importador lo usa gente de todas partes y es la parte del
    // producto que mas se enseña (la ventana de progreso por fases).
    String get_phase_name() const {
        switch (phase) {
            case PH_IDLE:       return "Preparing";
            case PH_PARSE:      return "Reading file";
            case PH_ANALYZED:   return "Waiting for confirmation";
            case PH_INSTANCES:  return "1/4  Building structure";
            case PH_PROPERTIES: return "2/4  Applying properties";
            case PH_HIERARCHY:  return "3/4  Linking hierarchy";
            case PH_REFS:       return "3/4  Resolving references";
            case PH_VERIFY:     return "4/4  Verifying";
            case PH_PLACE:      return "4/4  Placing";
            case PH_DONE:       return "Done";
            default:            return "Error";
        }
    }

    // Progreso global 0..1, ponderado por lo que cuesta cada fase.
    float get_progress() const {
        struct W { float base, span; };
        auto frac = [](int a, int b) -> float { return b > 0 ? (float)a / (float)b : 1.0f; };
        switch (phase) {
            case PH_IDLE:   return 0.0f;
            case PH_PARSE:  return 0.02f;
            case PH_INSTANCES:  return 0.05f + 0.20f * frac(cursor, flat_ref.size());
            case PH_PROPERTIES: return 0.25f + 0.45f * frac(cursor, prop_chunks.size());
            case PH_HIERARCHY:  return 0.70f + 0.10f * frac(cursor, link_child.size());
            case PH_REFS:       return 0.80f + 0.05f * frac(cursor, pending_refs.size());
            case PH_VERIFY:     return 0.85f + 0.10f * frac(cursor, flat_ref.size());
            case PH_PLACE:      return 0.95f;
            case PH_DONE:       return 1.0f;
            default:            return 1.0f;
        }
    }

    // Prepara la importacion. Devuelve false si el archivo no sirve.
    bool begin_import(const String &p_path) {
        path = p_path;
        last_error = String();
        report = Dictionary(); class_counts = Dictionary(); missing_classes = Dictionary();
        ignored_props = Dictionary(); missing_assets = Array(); verify_errors = Array();
        by_ref.clear(); prop_cache.clear(); pending_refs.clear();
        inst_chunks.clear(); prop_chunks.clear(); prnt_chunks.clear();
        flat_class.clear(); flat_ref.clear();
        link_child.clear(); link_parent.clear();
        class_names.clear(); class_refs.clear();
        file = rbx::RBXFile();
        cursor = sub_cursor = 0;
        prop_loaded = false;
        scripts_imported = scripts_total = scripts_seen = 0;
        tags_applied = orphans = attrs_applied = refs_ok = 0;
        reused_services = created_services = verified_ok = 0;
        renamed_siblings = hidden_grouped = defaults_replaced = 0;
        dup_conflicts = 0;
        root = nullptr;
        current_item = "";
        t_start = Time::get_singleton()->get_ticks_msec();
        ritmo = 1.0f;
        phase = PH_PARSE;
        const bool ok = _do_parse();
        // Con pausa: se para NADA MAS leer el archivo. Ya se sabe todo lo que
        // trae (clases y cuentas) y aun no existe ni un nodo, asi que
        // rechazar aqui no deja nada a medias.
        if (ok && pausar_tras_analisis && phase == PH_INSTANCES) phase = PH_ANALYZED;
        return ok;
    }

    // ── Analisis previo (1.17) ──────────────────────────────────────────
    //  Antes se importaba de golpe en cuanto elegias el archivo: si te
    //  equivocabas de .rbxl, te lo comias entero. Ahora se lee, se enseña lo
    //  que hay y no se toca el proyecto hasta que el usuario dice que si.
    void set_pausar_tras_analisis(bool v) { pausar_tras_analisis = v; }
    bool is_analyzed() const { return phase == PH_ANALYZED; }
    void continuar() { if (phase == PH_ANALYZED) phase = PH_INSTANCES; }
    void rechazar() {
        if (phase != PH_ANALYZED) return;
        phase = PH_ERROR;
        last_error = "Cancelled by user";
        // El nodo raiz SI se crea al leer (es el contenedor donde ira todo).
        // Si el usuario rechaza, hay que soltarlo o se queda huerfano en la
        // ObjectDB hasta cerrar ("ObjectDB instances were leaked at exit").
        if (root) { memdelete(root); root = nullptr; }
    }

    // Lo que se enseña en la ventana, ya agrupado por secciones y EN INGLES
    // (el importador lo ve gente de todas partes).
    Dictionary get_analysis() const {
        Dictionary d;
        d["place"]  = path.get_file().get_basename();
        d["path"]   = path;
        d["total"]  = (int)file.instance_count;
        d["classes"] = (int)class_counts.size();

        // Secciones: cada clase cae en la primera que la reconoce. El orden es
        // el de importancia para quien mira: primero el mundo, luego el codigo.
        static const char *SEC[] = {
            "World & Parts", "Models & Meshes", "Scripts", "User Interface",
            "Effects & Sound", "Constraints & Physics", "Services & Data", "Other"
        };
        Array secs;
        for (int s = 0; s < 8; s++) {
            Dictionary sd; sd["name"] = String(SEC[s]); sd["items"] = Array(); sd["count"] = 0;
            secs.push_back(sd);
        }
        Array claves = class_counts.keys();
        for (int i = 0; i < claves.size(); i++) {
            const String cn = claves[i];
            const int n = (int)class_counts[claves[i]];
            Dictionary item; item["class"] = cn; item["count"] = n;
            Dictionary sd = secs[_seccion_de(cn)];
            Array it = sd["items"]; it.push_back(item); sd["items"] = it;
            sd["count"] = (int)sd["count"] + n;
        }
        // Dentro de cada seccion, de mas a menos (insercion: son pocas clases).
        for (int s = 0; s < secs.size(); s++) {
            Dictionary sd = secs[s];
            Array it = sd["items"];
            for (int a = 0; a < it.size(); a++)
                for (int b = a + 1; b < it.size(); b++) {
                    Dictionary da = it[a], db = it[b];
                    if ((int)db["count"] > (int)da["count"]) { it[a] = db; it[b] = da; }
                }
            sd["items"] = it;
        }
        d["sections"] = secs;
        return d;
    }

    // Procesa un lote. Devuelve true mientras quede trabajo.
    bool step() {
        // PH_ANALYZED devuelve false: no hay trabajo hasta que alguien llame a
        // Continue(). Asi el bucle del llamante no gira en vacio.
        if (phase == PH_ANALYZED) return false;
        _ritmo_empezar();
        bool r;
        switch (phase) {
            case PH_PARSE:      r = _do_parse(); break;
            case PH_INSTANCES:  r = _do_instances(); break;
            case PH_PROPERTIES: r = _do_properties(); break;
            case PH_HIERARCHY:  r = _do_hierarchy(); break;
            case PH_REFS:       r = _do_refs(); break;
            case PH_VERIFY:     r = _do_verify(); break;
            case PH_PLACE:      r = _do_place(); break;
            default:            return false;   // DONE o ERROR
        }
        _ritmo_ajustar();
        return r;
    }

    // Importacion de una sola llamada (bloquea). Util desde codigo y para tests.
    Node *import_file(const String &p_path) {
        if (!begin_import(p_path)) return nullptr;
        int guard = 0;
        while (phase != PH_DONE && phase != PH_ERROR) {
            if (!step()) break;
            if (++guard > 2000000) { _fail("El importador no termina (bucle infinito)"); break; }
        }
        if (phase == PH_ERROR) return nullptr;
        if (phase != PH_DONE) { phase = PH_DONE; _finish(); }
        return root;
    }
};

// Los cuerpos de los metodos van por zonas:
#include "rbx_gdlu.h"

VARIANT_ENUM_CAST(RBXImporter::Phase);

#endif // RBX_IMPORTER_H
