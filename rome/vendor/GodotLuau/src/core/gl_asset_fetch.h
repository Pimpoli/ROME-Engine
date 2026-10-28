#ifndef GL_ASSET_FETCH_H
#define GL_ASSET_FETCH_H

// ── Descarga automatica de assets de la nube de Roblox (1.17) ────────────────
// Antes la postura era "no se pueden descargar" y las UIs importadas salian con
// huecos blancos: medido en la tienda del place, TODOS los ImageLabel tenian
// Image=rbxassetid://... y textura NULA (una TextureRect sin textura no dibuja
// nada, y el UIGradient de encima no tiene sobre que multiplicar — por eso las
// tarjetas se veian planas y sin colores).
//
// Los assets publicos SI se sirven sin credenciales:
//     https://assetdelivery.roblox.com/v1/asset?id=<numero>
// Aqui se bajan UNA vez, en serie y con dedupe, a un cache persistente:
//     user://gl_rbx_cache/<id>.<png|jpg|webp>
// gl_gui_load_image ya sabe cargar imagenes crudas de user://, asi que el resto
// del motor no cambia: al terminar la descarga se re-llama set_image en los
// nodos que esperaban y la textura aparece. La sustitucion manual en
// GodotLuau/assets/rbx/ sigue mandando (se comprueba antes que el cache).
//
// Gotcha de Roblox: muchos builders pegan ids de DECAL en Image. Ese asset no
// es el PNG: es un XML que apunta al id de la imagen real. Se sigue la
// indireccion UNA vez y el resultado se guarda con el id ORIGINAL (que es el
// que esta escrito en los nodos y el que busca el cache).

#include <godot_cpp/classes/node.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/http_request.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include "gl_asset.h"

using namespace godot;

// Indice del cache de descargas (user://gl_rbx_cache/): se lista UNA vez. Antes
// cada ImageLabel con un id que no estaba en assets/rbx/ sondeaba el disco tres
// veces, en cada llamada y sin recordar nada.
inline HashMap<String, uint32_t>& gl_cache_indice() {
    static HashMap<String, uint32_t> m;
    static bool hecho = false;
    if (!hecho) {
        hecho = true;
        static const char* kExts[] = { ".png", ".jpg", ".webp" };
        const PackedStringArray nombres = DirAccess::get_files_at("user://gl_rbx_cache/");
        for (int i = 0; i < nombres.size(); i++) {
            const String n = nombres[i];
            for (int e = 0; e < 3; e++) {
                if (!n.ends_with(kExts[e])) continue;
                const String num = n.substr(0, n.length() - String(kExts[e]).length());
                if (uint32_t* b = m.getptr(num)) *b |= 1u << e;
                else m.insert(num, 1u << e);
            }
        }
    }
    return m;
}
inline void gl_cache_indice_anotar(const String& num, const String& ext) {
    static const char* kExts[] = { ".png", ".jpg", ".webp" };
    for (int e = 0; e < 3; e++) {
        if (ext != kExts[e]) continue;
        HashMap<String, uint32_t>& m = gl_cache_indice();
        if (uint32_t* b = m.getptr(num)) *b |= 1u << e;
        else m.insert(num, 1u << e);
    }
}

class GLAssetFetcher : public Node {
    GDCLASS(GLAssetFetcher, Node);

    struct Pedido {
        String num;                      // id con el que se GUARDA en el cache
        String descargar;                // id que se PIDE (cambia si era un decal)
        String original;                 // el Image tal cual lo escribio el nodo
        LocalVector<uint64_t> duenos;    // ObjectID de los nodos que esperan
        bool via_decal = false;
        int intentos = 0;                // fallos de transporte ya sufridos
    };

    LocalVector<Pedido> cola;
    HashMap<String, bool> fallidos;      // no reintentar en esta sesion
    HashMap<String, bool> en_cola;
    HTTPRequest* http = nullptr;
    bool ocupado = false;

    static String _dir_cache() { return "user://gl_rbx_cache/"; }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_gl_done", "result", "code", "headers", "body"),
                             &GLAssetFetcher::_gl_done);
        ClassDB::bind_method(D_METHOD("_gl_lanzar"), &GLAssetFetcher::_gl_lanzar);
    }

    // Los pedidos encolados ANTES de que el alta diferida aterrice se quedaban
    // sin lanzar (_gl_lanzar corta si aun no esta en el arbol): al entrar, drena.
    void _notification(int p_what) {
        if (p_what == NOTIFICATION_READY) _gl_lanzar();
    }

public:
    // Unico por arbol; se cuelga de la raiz la primera vez que alguien pide.
    // El alta en el arbol es DIFERIDA, asi que get_node_or_null no lo ve hasta
    // el siguiente vaciado de cola: sin el ObjectID recordado, cada imagen del
    // mismo frame fabricaba su propio fetcher (miles en la carga de un place).
    static GLAssetFetcher* singleton() {
        static ObjectID ultimo;
        if (GLAssetFetcher* f = gl_cast<GLAssetFetcher>(ObjectDB::get_instance(ultimo)))
            return f;
        SceneTree* st = gl_cast<SceneTree>(Engine::get_singleton()->get_main_loop());
        if (!st) return nullptr;
        Window* root = st->get_root();
        if (!root) return nullptr;
        if (Node* ya = root->get_node_or_null(NodePath("GLAssetFetcher"))) {
            ultimo = ya->get_instance_id();
            return gl_cast<GLAssetFetcher>(ya);
        }
        GLAssetFetcher* f = memnew(GLAssetFetcher);
        f->set_name("GLAssetFetcher");
        root->call_deferred("add_child", f);
        ultimo = f->get_instance_id();
        return f;
    }

    // ¿Este asset ya se intento y fallo (401/404) en esta sesion? Lo

    // consulta la UI para no dejar un recuadro BLANCO permanente.

    bool ha_fallado(const String& num) const { return !num.is_empty() && fallidos.has(num); }


    void pedir(const String& id_original, Object* dueno) {
        if (Engine::get_singleton()->is_editor_hint()) return;
        const String num = gl_asset_id_number(id_original);
        if (num.is_empty() || fallidos.has(num)) return;
        const uint64_t oid = dueno ? (uint64_t)dueno->get_instance_id() : 0;
        if (en_cola.has(num)) {
            for (uint32_t i = 0; i < cola.size(); i++)
                if (cola[i].num == num) { if (oid) cola[i].duenos.push_back(oid); return; }
            return;   // en vuelo: al terminar se reaplica igual
        }
        Pedido p;
        p.num = num;
        p.descargar = num;
        p.original = id_original;
        if (oid) p.duenos.push_back(oid);
        cola.push_back(p);
        en_cola[num] = true;
        call_deferred("_gl_lanzar");
    }

    void _gl_lanzar() {
        if (ocupado || cola.is_empty() || !is_inside_tree()) return;
        // HTTPRequest NUEVO por peticion: reutilizar el mismo nodo tras un fallo
        // de handshake dejaba la cola muerta (medido: 1 error de TLS y 0 archivos
        // en cache tras toda una sesion). En hilo aparte ademas: con el juego
        // cargando a tope, el handshake por pasos en el hilo principal moria con
        // mbedtls -0x6C00 (internal error) — en un proyecto vacio la MISMA URL
        // funcionaba a la primera.
        if (http) { http->queue_free(); http = nullptr; }
        http = memnew(HTTPRequest);
        http->set_timeout(15.0);
        http->set_use_threads(true);
        add_child(http);
        http->connect("request_completed", Callable(this, "_gl_done"));
        ocupado = true;
        const String url = String("https://assetdelivery.roblox.com/v1/asset?id=") + cola[0].descargar;
        if (_debug())
            UtilityFunctions::print("[FETCH] lanzo id=", cola[0].descargar, " cola=", (int)cola.size());
        ultimo_code = 0;   // sin respuesta HTTP todavia (que un 4xx viejo no capte el retry)
        if (http->request(url) != OK)
            _gl_termina(false, PackedByteArray());
    }

    // Chivato opcional (GL_FETCH_DEBUG=1): imprime cada intento con su resultado.
    static bool _debug() {
        static int v = -1;
        if (v < 0) v = OS::get_singleton()->get_environment("GL_FETCH_DEBUG") == "1" ? 1 : 0;
        return v == 1;
    }

    int ultimo_code = 0;   // el HTTP code de la respuesta que se esta despachando

    void _gl_done(int result, int code, PackedStringArray, PackedByteArray body) {
        if (_debug() && !cola.is_empty())
            UtilityFunctions::print("[FETCH] id=", cola[0].descargar, " result=", result,
                                    " code=", code, " bytes=", body.size(),
                                    " intento=", cola[0].intentos, " en_cola=", (int)cola.size());
        ultimo_code = code;
        _gl_termina(result == HTTPRequest::RESULT_SUCCESS && code == 200 && body.size() > 8, body);
    }

private:
    void _gl_termina(bool ok, const PackedByteArray& body) {
        ocupado = false;
        if (cola.is_empty()) return;
        Pedido p = cola[0];

        if (ok) {
            const String ext = _ext_por_magia(body);
            if (!ext.is_empty()) {
                DirAccess::make_dir_recursive_absolute(_dir_cache());
                Ref<FileAccess> f = FileAccess::open(_dir_cache() + p.num + ext, FileAccess::WRITE);
                bool guardado = f.is_valid();
                if (guardado) { f->store_buffer(body); f->close(); gl_cache_indice_anotar(p.num, ext); }
                _saca_actual(guardado ? nullptr : &p);
                if (guardado) _reaplicar(p);
                call_deferred("_gl_lanzar");
                return;
            }
            // No es imagen: ¿XML de decal apuntando a la imagen real?
            if (!p.via_decal) {
                const String interior = _id_dentro_de_xml(body);
                if (!interior.is_empty() && interior != p.descargar) {
                    cola[0].descargar = interior;   // mismo pedido, otra URL
                    cola[0].via_decal = true;
                    call_deferred("_gl_lanzar");
                    return;
                }
            }
            // Respuesta rara con exito HTTP: no reintentar, marcar y seguir.
            _saca_actual(&p);
            call_deferred("_gl_lanzar");
            return;
        }
        // Un error HTTP del servidor (401 asset privado, 404, 403...) es
        // DEFINITIVO: reintentarlo solo repite la misma respuesta 3 veces.
        // Solo se reintenta el fallo de TRANSPORTE (TLS, timeout, corte).
        if (ultimo_code >= 400) {
            _saca_actual(&p);
            call_deferred("_gl_lanzar");
            return;
        }
        // Fallo de transporte (TLS, timeout, corte): reintentar el MISMO pedido
        // hasta 3 veces antes de rendirse — el primer arranque del juego satura
        // la maquina y un handshake puede caerse de forma transitoria.
        if (cola[0].intentos < 2) {
            cola[0].intentos++;
            call_deferred("_gl_lanzar");
            return;
        }
        _saca_actual(&p);
        call_deferred("_gl_lanzar");
    }

    // Saca el pedido 0 de la cola; si `fallo` no es null, lo apunta como fallido.
    void _saca_actual(const Pedido* fallo) {
        if (cola.is_empty()) return;
        if (fallo) fallidos[fallo->num] = true;
        en_cola.erase(cola[0].num);
        cola.remove_at(0);
    }

    void _reaplicar(const Pedido& p) {
        for (uint32_t i = 0; i < p.duenos.size(); i++) {
            Object* o = ObjectDB::get_instance(ObjectID(p.duenos[i]));
            if (!o) continue;
            // set_image vuelve a resolver la ruta y ahora encuentra el cache.
            if (o->has_method("set_image")) o->call("set_image", p.original);
        }
    }

    static String _ext_por_magia(const PackedByteArray& b) {
        if (b.size() < 12) return String();
        const uint8_t* d = b.ptr();
        if (d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return ".png";
        if (d[0] == 0xFF && d[1] == 0xD8)                              return ".jpg";
        if (d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' &&
            d[8] == 'W' && d[9] == 'E' && d[10] == 'B' && d[11] == 'P') return ".webp";
        return String();
    }

    // Busca "?id=NUMERO" o "rbxassetid://NUMERO" dentro del XML de un decal.
    static String _id_dentro_de_xml(const PackedByteArray& b) {
        String s;
        const int n = MIN((int)b.size(), 4096);
        for (int i = 0; i < n; i++) {
            const char c = (char)b[i];
            if (c == 0) break;
            s += String::chr((char32_t)c);
        }
        int at = s.find("?id=");
        if (at < 0) at = s.find("rbxassetid://");
        if (at < 0) return String();
        String num;
        for (int i = at; i < s.length(); i++) {
            const char32_t c = s[i];
            if (c >= '0' && c <= '9') num += String::chr(c);
            else if (!num.is_empty()) break;
        }
        return num;
    }
};

// El cache persistente en user://. La copia manual del usuario en
// GodotLuau/assets/rbx/ (gl_local_asset_path) se comprueba ANTES que esto.
inline String gl_cache_asset_path(const String& id) {
    const String num = gl_asset_id_number(id);
    if (num.is_empty()) return String();
    const uint32_t* b = gl_cache_indice().getptr(num);
    if (!b) return String();
    static const char* kExts[] = { ".png", ".jpg", ".webp" };
    for (int i = 0; i < 3; i++)
        if (*b & (1u << i)) return String("user://gl_rbx_cache/") + num + kExts[i];
    return String();
}

#include "gl_streaming_texturas.h"   // carga de texturas sin bloquear (lo usa gl_gui_load_image)

#endif // GL_ASSET_FETCH_H
