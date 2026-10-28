#ifndef GL_COMPAT_H
#define GL_COMPAT_H

// ════════════════════════════════════════════════════════════════════
//  ANALIZADOR DE COMPATIBILIDAD — Capa 0 de "GodotLuau IA"
//
//  Dice, SIN EJECUTAR NADA y sin equivocarse, que lineas de un script de
//  Roblox no van a funcionar en GodotLuau. No es un modelo ni una
//  heuristica: parsea el codigo con el MISMO Luau que corre el juego y
//  cruza cada simbolo contra lo que el motor implementa de verdad
//  (gl_compat_gen.h, generado del propio codigo del puente).
//
//  Por que esto y no una IA: para el 90% del trabajo un modelo es la
//  herramienta equivocada. Sabemos con certeza que soporta el motor, asi
//  que "Tool.Enabled no existe aqui" es un HECHO comprobable en 1 ms, no
//  algo que haya que adivinar. El modelo hace falta despues, para la
//  parte difusa ("y esto como lo reescribo").
//
//  Los tres veredictos que sabe dar:
//    · lo usas y lo tenemos            -> callado
//    · lo usas, Roblox lo tiene, aqui no -> ESTO es lo que rompe el juego
//    · lo usas y no existe ni en Roblox -> es tuyo (atributo, campo)
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "Luau/Parser.h"
#include "Luau/Ast.h"
#include "Luau/Allocator.h"
#include "Luau/ParseOptions.h"
#include "Luau/Compiler.h"

#include "gl_compat_gen.h"

#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

using namespace godot;

// ── Conjuntos de consulta ────────────────────────────────────────────────
// Las tablas generadas son arrays de cadenas; se pasan a un set una sola vez.
struct GLCompatTablas {
    std::unordered_set<std::string> gl_miembros, gl_clases, gl_servicios, gl_enums;
    std::unordered_set<std::string> rbx_clases, rbx_miembros, rbx_enums;

    static void cargar(std::unordered_set<std::string>& dst, const char* const* src) {
        for (int i = 0; src[i]; i++) dst.insert(src[i]);
    }
    GLCompatTablas() {
        cargar(gl_miembros,   GL_COMPAT_MIEMBROS);
        cargar(gl_clases,     GL_COMPAT_CLASES);
        cargar(gl_servicios,  GL_COMPAT_SERVICIOS);
        cargar(gl_enums,      GL_COMPAT_ENUMS);
        cargar(rbx_clases,    GL_RBX_CLASES);
        cargar(rbx_miembros,  GL_RBX_MIEMBROS);
        cargar(rbx_enums,     GL_RBX_ENUMS);
    }
};

inline const GLCompatTablas& gl_compat_tablas() {
    static GLCompatTablas t;
    return t;
}

// Bases que NO son instancias de Roblox: sus miembros no se comprueban.
inline bool gl_compat_base_ignorada(const char* n) {
    static const char* kIgnorar[] = {
        "string", "table", "math", "os", "task", "coroutine", "bit32", "utf8",
        "debug", "buffer", "vector", "_G", "shared", "script", "self",
        // Constructores del stdlib: sus miembros son .new/.fromRGB/etc.
        "Vector3", "Vector2", "CFrame", "UDim", "UDim2", "Color3", "BrickColor",
        "Rect", "Region3", "Ray", "NumberRange", "NumberSequence", "ColorSequence",
        "TweenInfo", "Instance", "Random", "Faces", "Axes", "PhysicalProperties",
        "DateTime", "Font", "RaycastParams", "OverlapParams", "Enum", nullptr
    };
    for (int i = 0; kIgnorar[i]; i++) if (strcmp(n, kIgnorar[i]) == 0) return true;
    return false;
}

// Compila SOLO para comprobar sintaxis: sin optimizar y sin info de depuracion,
// que es lo mas rapido y lo unico que hace falta para decir "esto compila".
inline std::string gl_compat_compilar(const std::string& src) {
    Luau::CompileOptions o;
    o.optimizationLevel = 0;
    o.debugLevel        = 0;
    return Luau::compile(src, o);
}

// ── Un hallazgo ──────────────────────────────────────────────────────────
struct GLCompatHallazgo {
    String archivo;
    int    linea = 0;
    int    columna = 0;
    String simbolo;
    String tipo;        // clase | servicio | enum | miembro | sintaxis
    String severidad;   // grave | aviso | info
    String mensaje;
};

// ── El visitante del AST ─────────────────────────────────────────────────
class GLCompatVisitor : public Luau::AstVisitor {
public:
    std::vector<GLCompatHallazgo>* out = nullptr;
    String archivo;
    std::unordered_set<std::string> ya_visto;   // un aviso por simbolo y archivo
    // Nombres que el PROPIO codigo del usuario define (metodos de sus modulos,
    // claves de sus tablas). Sin esto, un `DataService.Start()` se denunciaba
    // porque "Start" tambien existe en algun sitio de la API de Roblox: el
    // analizador se llenaba de ruido y dejaba de servir.
    const std::unordered_set<std::string>* definidos = nullptr;
    // Solo recolectar definiciones (1a pasada), sin denunciar nada.
    bool solo_recolectar = false;
    std::unordered_set<std::string>* recolector = nullptr;

    void anota(const Luau::Location& loc, const String& simbolo, const char* tipo,
               const char* sev, const String& msg) {
        const std::string clave = std::string(tipo) + "|" + std::string(simbolo.utf8().get_data());
        if (!ya_visto.insert(clave).second) return;   // ya avisado en este archivo
        GLCompatHallazgo h;
        h.archivo   = archivo;
        h.linea     = (int)loc.begin.line + 1;      // Luau cuenta desde 0
        h.columna   = (int)loc.begin.column + 1;
        h.simbolo   = simbolo;
        h.tipo      = tipo;
        h.severidad = sev;
        h.mensaje   = msg;
        out->push_back(h);
    }

    // ── 1a PASADA: que nombres define el usuario ─────────────────────
    void _definir(Luau::AstExpr* e) {
        if (!e || !recolector) return;
        if (Luau::AstExprIndexName* i = e->as<Luau::AstExprIndexName>())
            recolector->insert(i->index.value);
    }
    bool visit(Luau::AstStatFunction* st) override {       // function M.X() / M:X()
        _definir(st->name);
        return true;
    }
    bool visit(Luau::AstStatAssign* st) override {          // M.X = ...
        for (size_t i = 0; i < st->vars.size; i++) _definir(st->vars.data[i]);
        return true;
    }
    bool visit(Luau::AstExprTable* t) override {            // { X = ... }
        if (!recolector) return true;
        for (size_t i = 0; i < t->items.size; i++) {
            if (Luau::AstExprConstantString* k =
                    t->items.data[i].key ? t->items.data[i].key->as<Luau::AstExprConstantString>() : nullptr)
                recolector->insert(std::string(k->value.data, k->value.size));
        }
        return true;
    }

    // `Instance.new("X")` y `game:GetService("X")`
    bool visit(Luau::AstExprCall* call) override {
        if (solo_recolectar) return true;
        const GLCompatTablas& T = gl_compat_tablas();
        if (Luau::AstExprIndexName* idx = call->func->as<Luau::AstExprIndexName>()) {
            const char* metodo = idx->index.value;
            const Luau::AstExprConstantString* arg0 =
                (call->args.size > 0) ? call->args.data[0]->as<Luau::AstExprConstantString>() : nullptr;

            // Instance.new("Clase")
            if (strcmp(metodo, "new") == 0 && arg0) {
                if (Luau::AstExprGlobal* g = idx->expr->as<Luau::AstExprGlobal>()) {
                    if (strcmp(g->name.value, "Instance") == 0) {
                        const std::string cls(arg0->value.data, arg0->value.size);
                        if (!T.gl_clases.count(cls)) {
                            if (T.rbx_clases.count(cls))
                                anota(call->location, String(cls.c_str()), "clase", "grave",
                                      String("Instance.new(\"") + cls.c_str() +
                                      "\"): Roblox tiene esa clase, GodotLuau todavia no. "
                                      "Devuelve nil y la linea siguiente suele morir.");
                            else
                                anota(call->location, String(cls.c_str()), "clase", "aviso",
                                      String("Instance.new(\"") + cls.c_str() +
                                      "\"): esa clase no existe ni en Roblox. ¿Un nombre mal escrito?");
                        }
                    }
                }
            }
            // game:GetService("X")
            if (strcmp(metodo, "GetService") == 0 && arg0) {
                const std::string svc(arg0->value.data, arg0->value.size);
                if (!T.gl_servicios.count(svc)) {
                    if (T.rbx_clases.count(svc))
                        anota(call->location, String(svc.c_str()), "servicio", "grave",
                              String("GetService(\"") + svc.c_str() +
                              "\"): Roblox tiene ese servicio, GodotLuau todavia no.");
                    else
                        anota(call->location, String(svc.c_str()), "servicio", "aviso",
                              String("GetService(\"") + svc.c_str() +
                              "\"): ese servicio no existe ni en Roblox.");
                }
            }
        }
        return true;
    }

    // `obj.Miembro`, `obj:Metodo()` y `Enum.Tipo.Valor`
    bool visit(Luau::AstExprIndexName* idx) override {
        if (solo_recolectar) return true;
        const GLCompatTablas& T = gl_compat_tablas();

        // ¿Es la segunda mitad de un Enum.Tipo.Valor?
        if (Luau::AstExprIndexName* padre = idx->expr->as<Luau::AstExprIndexName>()) {
            if (Luau::AstExprGlobal* g = padre->expr->as<Luau::AstExprGlobal>()) {
                if (strcmp(g->name.value, "Enum") == 0) {
                    // GetEnumItems/FromName/FromValue son METODOS del enum, no
                    // valores suyos: comprobarlos como Enum.X.Y daba un falso
                    // positivo en cualquier codigo que recorre un enum.
                    const char* v = idx->index.value;
                    if (strcmp(v, "GetEnumItems") == 0 || strcmp(v, "FromName") == 0 ||
                        strcmp(v, "FromValue") == 0 || strcmp(v, "Name") == 0 ||
                        strcmp(v, "Value") == 0 || strcmp(v, "EnumType") == 0) return true;
                    const std::string par = std::string(padre->index.value) + "." + idx->index.value;
                    if (!T.gl_enums.count(par)) {
                        if (T.rbx_enums.count(par))
                            anota(idx->indexLocation, String(par.c_str()), "enum", "grave",
                                  String("Enum.") + par.c_str() +
                                  " existe en Roblox pero no aqui: la comparacion sera SIEMPRE falsa "
                                  "(y si se indexa, el script muere).");
                        else
                            anota(idx->indexLocation, String(par.c_str()), "enum", "aviso",
                                  String("Enum.") + par.c_str() + " no existe ni en Roblox.");
                    }
                    return true;
                }
            }
        }
        // Enum.Tipo (sin el valor): que el tipo exista
        if (Luau::AstExprGlobal* g = idx->expr->as<Luau::AstExprGlobal>()) {
            if (strcmp(g->name.value, "Enum") == 0) return true;   // se juzga con el valor
            if (gl_compat_base_ignorada(g->name.value)) return true;
        }

        // Miembro corriente. Solo se avisa si Roblox LO TIENE y aqui no: un
        // nombre que no existe en ninguno de los dos es del usuario (un
        // atributo, un campo de una tabla suya) y avisar de eso seria ruido.
        const char* miembro = idx->index.value;
        if (T.gl_miembros.count(miembro)) return true;
        if (!T.rbx_miembros.count(miembro)) return true;
        // Lo define el propio proyecto: es suyo, no de Roblox.
        if (definidos && definidos->count(miembro)) return true;
        anota(idx->indexLocation, String(miembro), "miembro", "grave",
              String("\"") + miembro + "\" existe en la API de Roblox pero el puente de "
              "GodotLuau no lo implementa: leerlo tumba el script y escribirlo se "
              "descarta en silencio.");
        return true;
    }
};

// ── La clase que ve GDScript ─────────────────────────────────────────────
class GLCompat : public RefCounted {
    GDCLASS(GLCompat, RefCounted);

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("analizar_texto", "codigo", "nombre"), &GLCompat::analizar_texto);
        ClassDB::bind_method(D_METHOD("analizar_archivo", "ruta"),           &GLCompat::analizar_archivo);
        ClassDB::bind_method(D_METHOD("analizar_carpeta", "ruta"),           &GLCompat::analizar_carpeta);
        ClassDB::bind_method(D_METHOD("analizar_nodo", "raiz"),              &GLCompat::analizar_nodo);
        ClassDB::bind_method(D_METHOD("resumen", "hallazgos"),               &GLCompat::resumen);
        ClassDB::bind_method(D_METHOD("consultar", "simbolo"),               &GLCompat::consultar);
        ClassDB::bind_method(D_METHOD("compilar", "codigo"),                 &GLCompat::compilar);
        ClassDB::bind_method(D_METHOD("sugerir", "simbolo"),                 &GLCompat::sugerir);
    }

    static Dictionary _a_dict(const GLCompatHallazgo& h) {
        Dictionary d;
        d["archivo"]   = h.archivo;
        d["linea"]     = h.linea;
        d["columna"]   = h.columna;
        d["simbolo"]   = h.simbolo;
        d["tipo"]      = h.tipo;
        d["severidad"] = h.severidad;
        d["mensaje"]   = h.mensaje;
        return d;
    }

    // El trabajo de verdad: parsear y recorrer.
    static void _analizar(const String& codigo, const String& nombre,
                          std::vector<GLCompatHallazgo>& out,
                          const std::unordered_set<std::string>* definidos = nullptr,
                          std::unordered_set<std::string>* recolector = nullptr) {
        const CharString cs = codigo.utf8();
        Luau::Allocator allocator;
        Luau::AstNameTable names(allocator);
        Luau::ParseOptions opts;
        opts.captureComments = false;

        Luau::ParseResult res = Luau::Parser::parse(cs.get_data(), (size_t)cs.length(),
                                                    names, allocator, opts);
        if (!res.errors.empty()) {
            if (recolector) return;   // en la pasada de recoleccion no se reporta
            // Un error de sintaxis se reporta y se para: sin AST no hay nada
            // que recorrer. (Luau parsea con recuperacion, pero un arbol roto
            // daria hallazgos falsos.)
            for (const Luau::ParseError& e : res.errors) {
                GLCompatHallazgo h;
                h.archivo   = nombre;
                h.linea     = (int)e.getLocation().begin.line + 1;
                h.columna   = (int)e.getLocation().begin.column + 1;
                h.simbolo   = "";
                h.tipo      = "sintaxis";
                h.severidad = "grave";
                h.mensaje   = String("Error de sintaxis: ") + String(e.getMessage().c_str());
                out.push_back(h);
            }
            return;
        }
        if (!res.root) return;

        GLCompatVisitor v;
        v.out = &out;
        v.archivo = nombre;
        v.definidos = definidos;
        v.recolector = recolector;
        v.solo_recolectar = (recolector != nullptr);
        res.root->visit(&v);
    }

    static void _listar_scripts(const String& ruta, std::vector<String>& rutas) {
        Ref<DirAccess> dir = DirAccess::open(ruta);
        if (dir.is_null()) return;
        dir->list_dir_begin();
        String n = dir->get_next();
        while (!n.is_empty()) {
            if (n != "." && n != "..") {
                const String hijo = ruta.path_join(n);
                if (dir->current_is_dir()) {
                    // .godot y las carpetas ocultas no llevan scripts del juego.
                    if (!n.begins_with(".")) _listar_scripts(hijo, rutas);
                } else if (n.ends_with(".lua") || n.ends_with(".luau")) {
                    rutas.push_back(hijo);
                }
            }
            n = dir->get_next();
        }
        dir->list_dir_end();
    }

    // DOS PASADAS sobre todo el proyecto: la primera aprende que nombres define
    // el propio codigo del usuario (los metodos de sus modulos, las claves de
    // sus tablas), la segunda ya puede distinguir "esto es de Roblox y falta"
    // de "esto es tuyo". Sin la primera, un DataService.Start() se denunciaba.
    static void _recoger_carpeta(const String& ruta, std::vector<GLCompatHallazgo>& out) {
        std::vector<String> rutas;
        _listar_scripts(ruta, rutas);
        std::unordered_set<std::string> definidos;
        std::vector<String> fuentes;
        fuentes.reserve(rutas.size());
        for (const String& r : rutas) {
            Ref<FileAccess> f = FileAccess::open(r, FileAccess::READ);
            fuentes.push_back(f.is_valid() ? f->get_as_text() : String());
        }
        std::vector<GLCompatHallazgo> tirar;
        for (size_t i = 0; i < rutas.size(); i++)
            if (!fuentes[i].is_empty()) _analizar(fuentes[i], rutas[i], tirar, nullptr, &definidos);
        for (size_t i = 0; i < rutas.size(); i++)
            if (!fuentes[i].is_empty()) _analizar(fuentes[i], rutas[i], out, &definidos, nullptr);
    }

    // Los scripts de un arbol de escena: el codigo vive en el recurso
    // "codigo_luau" (LuauScript) de cada nodo de script.
    static void _recoger_scripts_nodo(Node* n, std::vector<String>& nombres, std::vector<String>& fuentes) {
        if (!n) return;
        const String cls = n->get_class();
        if (cls == "ServerScript" || cls == "LocalScript" || cls == "ModuleScript") {
            String src;
            const Variant rec = n->get(StringName("codigo_luau"));
            if (Object* o = rec) {
                const Variant fu = o->get(StringName("gl_fuente"));
                if (fu.get_type() == Variant::STRING) src = fu;
            }
            if (src.is_empty()) {
                const Variant fu = n->get(StringName("gl_fuente"));
                if (fu.get_type() == Variant::STRING) src = fu;
            }
            if (!src.is_empty()) { nombres.push_back(n->get_name()); fuentes.push_back(src); }
        }
        for (int i = 0; i < n->get_child_count(); i++) _recoger_scripts_nodo(n->get_child(i), nombres, fuentes);
    }

    static void _recoger_nodo_viejo(Node* n, std::vector<GLCompatHallazgo>& out) {
        if (!n) return;
        const String cls = n->get_class();
        if (cls == "ServerScript" || cls == "LocalScript" || cls == "ModuleScript") {
            String src;
            const Variant rec = n->get(StringName("codigo_luau"));
            if (Object* o = rec) {
                const Variant fu = o->get(StringName("gl_fuente"));
                if (fu.get_type() == Variant::STRING) src = fu;
            }
            if (src.is_empty()) {
                const Variant fu = n->get(StringName("gl_fuente"));
                if (fu.get_type() == Variant::STRING) src = fu;
            }
            if (!src.is_empty()) _analizar(src, n->get_name(), out);
        }
        for (int i = 0; i < n->get_child_count(); i++) _recoger_nodo_viejo(n->get_child(i), out);
    }

    static void _recoger_nodo(Node* raiz, std::vector<GLCompatHallazgo>& out) {
        std::vector<String> nombres, fuentes;
        _recoger_scripts_nodo(raiz, nombres, fuentes);
        std::unordered_set<std::string> definidos;
        std::vector<GLCompatHallazgo> tirar;
        for (size_t i = 0; i < nombres.size(); i++) _analizar(fuentes[i], nombres[i], tirar, nullptr, &definidos);
        for (size_t i = 0; i < nombres.size(); i++) _analizar(fuentes[i], nombres[i], out, &definidos, nullptr);
    }

    static Array _a_array(const std::vector<GLCompatHallazgo>& v) {
        Array a;
        for (const GLCompatHallazgo& h : v) a.push_back(_a_dict(h));
        return a;
    }

public:
    Array analizar_texto(const String& codigo, const String& nombre) {
        std::vector<GLCompatHallazgo> out;
        const String nom = nombre.is_empty() ? String("(texto)") : nombre;
        std::unordered_set<std::string> definidos;
        std::vector<GLCompatHallazgo> tirar;
        _analizar(codigo, nom, tirar, nullptr, &definidos);
        _analizar(codigo, nom, out, &definidos, nullptr);
        return _a_array(out);
    }

    Array analizar_archivo(const String& ruta) {
        std::vector<GLCompatHallazgo> out;
        Ref<FileAccess> f = FileAccess::open(ruta, FileAccess::READ);
        if (f.is_valid()) {
            const String src = f->get_as_text();
            std::unordered_set<std::string> definidos;
            std::vector<GLCompatHallazgo> tirar;
            _analizar(src, ruta, tirar, nullptr, &definidos);
            _analizar(src, ruta, out, &definidos, nullptr);
        }
        return _a_array(out);
    }

    Array analizar_carpeta(const String& ruta) {
        std::vector<GLCompatHallazgo> out;
        _recoger_carpeta(ruta.is_empty() ? String("res://") : ruta, out);
        return _a_array(out);
    }

    Array analizar_nodo(Node* raiz) {
        std::vector<GLCompatHallazgo> out;
        _recoger_nodo(raiz, out);
        return _a_array(out);
    }

    // ── LO QUE HACE QUE EL CHAT NO SE INVENTE NADA ───────────────────
    // Dado un simbolo ("Tool.Enabled", "WorldModel", "Technology.Future" o un
    // nombre suelto), dice si ESTE motor lo soporta y si Roblox lo tiene. La
    // capa de chat mete esta respuesta en el contexto del modelo, asi que el
    // modelo no OPINA sobre compatibilidad: se lo decimos nosotros, medido.
    Dictionary consultar(const String& simbolo) {
        const GLCompatTablas& T = gl_compat_tablas();
        const std::string s(simbolo.utf8().get_data());
        Dictionary d;
        d["simbolo"] = simbolo;

        const bool tiene_punto = (s.find('.') != std::string::npos);
        bool gl = false, rbx = false;
        String clase = "miembro";
        if (tiene_punto) {
            gl  = T.gl_enums.count(s)  > 0;
            rbx = T.rbx_enums.count(s) > 0;
            clase = "enum";
        } else {
            if (T.gl_clases.count(s) || T.rbx_clases.count(s)) {
                gl  = T.gl_clases.count(s) > 0;
                rbx = T.rbx_clases.count(s) > 0;
                clase = T.gl_servicios.count(s) ? "servicio" : "clase";
                if (T.gl_servicios.count(s)) gl = true;
            } else {
                gl  = T.gl_miembros.count(s)  > 0;
                rbx = T.rbx_miembros.count(s) > 0;
            }
        }
        d["tipo"]              = clase;
        d["soportado"]         = gl;
        d["existe_en_roblox"]  = rbx;
        d["veredicto"] = gl  ? String("GodotLuau lo soporta.")
                       : rbx ? String("Roblox lo tiene pero GodotLuau todavia NO lo implementa.")
                             : String("No existe ni en Roblox: probablemente sea un nombre propio del proyecto.");
        return d;
    }

    // ── LO QUE HACE QUE LA REESCRITURA ACIERTE ───────────────────────
    // Dado un simbolo que NO soportamos, devuelve los nombres PARECIDOS que si
    // soportamos. Suena menor y es lo que decide si la Capa 2 sirve o no:
    //
    // Medido — sin esto, ni el modelo de 7B acertaba con `.unit`,
    // `:inverse()` o `:vectorToWorldSpace()`. Son ALIAS EN MINUSCULA que
    // Roblox arrastra desde hace anios, y la version PascalCase (`.Unit`,
    // `:Inverse()`, `:VectorToWorldSpace()`) SI existe en el motor. El modelo
    // no lo sabia y se dedicaba a comentar las lineas.
    //
    // Primero se busca ignorando mayusculas (que resuelve justo ese caso), y
    // si no, por parecido de prefijo.
    Array sugerir(const String& simbolo) {
        const GLCompatTablas& T = gl_compat_tablas();
        const std::string s(simbolo.utf8().get_data());
        Array out;
        if (s.empty()) return out;

        std::string bajo = s;
        for (char& c : bajo) c = (char)tolower((unsigned char)c);

        // 1) Mismo nombre con otras mayusculas: el caso de los alias legacy.
        for (const std::string& m : T.gl_miembros) {
            if (m.size() != s.size()) continue;
            std::string mb = m;
            for (char& c : mb) c = (char)tolower((unsigned char)c);
            if (mb == bajo && m != s) out.push_back(String::utf8(m.c_str()));
        }
        if (out.size() > 0) return out;

        // 2) Mismo principio (al menos 4 letras) y longitud parecida.
        if (bajo.size() >= 4) {
            const std::string ini = bajo.substr(0, 4);
            for (const std::string& m : T.gl_miembros) {
                if (m.size() + 4 < s.size() || s.size() + 4 < m.size()) continue;
                std::string mb = m;
                for (char& c : mb) c = (char)tolower((unsigned char)c);
                if (mb.compare(0, ini.size() < mb.size() ? ini.size() : mb.size(), ini) == 0) {
                    out.push_back(String::utf8(m.c_str()));
                    if (out.size() >= 6) break;
                }
            }
        }
        return out;
    }

    // ── LA RED DE SEGURIDAD DE LA CAPA 2 ─────────────────────────────
    // Compila el codigo con el MISMO Luau que corre el juego. No ejecuta
    // nada: solo contesta si compila y, si no, en que linea falla.
    //
    // Es lo que hace que la reescritura con IA sea fiable: lo que un modelo
    // propone no se le enseña al usuario hasta que ESTO dice que compila. Un
    // modelo pequeño se deja comas y cierra mal los bloques constantemente;
    // asi ese fallo se detecta en 1 ms y se le devuelve el error para que lo
    // corrija, en vez de acabar en el proyecto del usuario.
    Dictionary compilar(const String& codigo) {
        const CharString cs = codigo.utf8();
        const std::string src(cs.get_data(), (size_t)cs.length());
        const std::string bc = gl_compat_compilar(src);
        Dictionary d;
        // Luau devuelve bytecode; si el primer byte es 0, lo que sigue es el
        // mensaje de error (mismo criterio que usa el motor al validar).
        if (!bc.empty() && bc[0] == '\0') {
            const String msg = String::utf8(bc.c_str() + 1);
            d["ok"] = false;
            d["mensaje"] = msg;
            // El mensaje viene como ":<linea>: <texto>"
            int linea = 0;
            if (msg.begins_with(":")) {
                const int corte = msg.find(":", 1);
                if (corte > 1) linea = msg.substr(1, corte - 1).to_int();
            }
            d["linea"] = linea;
        } else {
            d["ok"] = true;
            d["mensaje"] = String();
            d["linea"] = 0;
            d["bytecode"] = (int64_t)bc.size();
        }
        return d;
    }

    // Cuenta por severidad y por tipo, para la cabecera del informe.
    Dictionary resumen(const Array& hallazgos) {
        int graves = 0, avisos = 0;
        Dictionary por_tipo, por_simbolo;
        for (int i = 0; i < hallazgos.size(); i++) {
            const Dictionary d = hallazgos[i];
            const String sev = d.get("severidad", "aviso");
            if (sev == "grave") graves++; else avisos++;
            const String t = d.get("tipo", "?");
            por_tipo[t] = (int)por_tipo.get(t, 0) + 1;
            const String s = d.get("simbolo", "");
            if (!s.is_empty()) por_simbolo[s] = (int)por_simbolo.get(s, 0) + 1;
        }
        Dictionary r;
        r["total"]      = hallazgos.size();
        r["graves"]     = graves;
        r["avisos"]     = avisos;
        r["por_tipo"]   = por_tipo;
        r["por_simbolo"] = por_simbolo;
        return r;
    }
};

#endif // GL_COMPAT_H
