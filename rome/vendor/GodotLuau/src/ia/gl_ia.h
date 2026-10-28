#ifndef GL_IA_H
#define GL_IA_H

// ════════════════════════════════════════════════════════════════════
//  GodotLuau IA — Capa 1: el chat local (llama.cpp embebido)
//
//  POR QUE ES UNA EXTENSION APARTE
//  llama.cpp añade ~10 MB al binario. El motor pesa 9, asi que meterlo
//  dentro lo dejaria en 19 para TODO el mundo, incluido quien no piense
//  usar la IA nunca. Va en su propia GDExtension: si el usuario no la
//  descarga, no existe y no cuesta nada. Es la misma idea que los modelos
//  de autocompletado — opcional de verdad, no "opcional pero lo pagas".
//
//  COMO CORRE
//  Un hilo aparte hace la inferencia y va dejando los trozos de texto en
//  una cola; el _process (hilo principal) la vacia y emite la señal. Asi
//  el editor NUNCA se congela, ni siquiera con el modelo grande, y el
//  texto aparece palabra a palabra como en cualquier chat.
//
//  NIVELES DE PENSAMIENTO (lo que pidio el usuario)
//    0 Rapido   — pocas fichas, temperatura baja: responde ya.
//    1 Normal   — el termino medio.
//    2 Profundo — mas fichas y deja sitio para que el modelo razone.
//  La reescritura de codigo SIEMPRE usa el modelo mas potente que haya
//  descargado, sea cual sea el nivel (lo decide la capa de arriba).
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "llama.h"
#include "ggml-backend.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace godot;

class GLLocalAI : public Node {
    GDCLASS(GLLocalAI, Node);

    llama_model*   modelo = nullptr;
    llama_context* ctx    = nullptr;
    const llama_vocab* vocab = nullptr;

    std::thread        hilo;
    std::atomic<bool>  generando{false};
    std::atomic<bool>  cancelar_flag{false};
    std::mutex         cola_mtx;
    std::deque<std::string> cola;      // trozos listos para emitir
    std::atomic<bool>  termino{false};
    std::string        respuesta_completa;
    std::string        ultimo_error;
    String             ruta_modelo;
    int                n_ctx_actual = 4096;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("cargar_modelo", "ruta", "n_ctx", "hilos", "capas_gpu"), &GLLocalAI::cargar_modelo,
                             DEFVAL(4096), DEFVAL(0), DEFVAL(0));
        ClassDB::bind_method(D_METHOD("backends"), &GLLocalAI::backends);
        ClassDB::bind_method(D_METHOD("soltar_modelo"),   &GLLocalAI::soltar_modelo);
        ClassDB::bind_method(D_METHOD("esta_listo"),      &GLLocalAI::esta_listo);
        ClassDB::bind_method(D_METHOD("esta_generando"),  &GLLocalAI::esta_generando);
        ClassDB::bind_method(D_METHOD("preguntar", "sistema", "mensaje", "nivel"), &GLLocalAI::preguntar,
                             DEFVAL(1));
        ClassDB::bind_method(D_METHOD("cancelar"),        &GLLocalAI::cancelar);
        ClassDB::bind_method(D_METHOD("ultimo_error_txt"),&GLLocalAI::ultimo_error_txt);
        ClassDB::bind_method(D_METHOD("info_modelo"),     &GLLocalAI::info_modelo);

        ADD_SIGNAL(MethodInfo("trozo",     PropertyInfo(Variant::STRING, "texto")));
        ADD_SIGNAL(MethodInfo("terminado", PropertyInfo(Variant::STRING, "texto_completo")));
        ADD_SIGNAL(MethodInfo("fallo",     PropertyInfo(Variant::STRING, "mensaje")));
    }

public:
    // El hilo principal vacia la cola y emite: las señales de Godot NO son
    // seguras desde otro hilo. PUBLICO porque godot-cpp lo invoca desde el
    // registro de metodos virtuales.
    void _process(double) override {
        std::deque<std::string> lote;
        {
            std::lock_guard<std::mutex> lk(cola_mtx);
            lote.swap(cola);
        }
        for (const std::string& t : lote) emit_signal("trozo", String::utf8(t.c_str()));

        if (termino.load()) {
            termino.store(false);
            set_process(false);
            if (hilo.joinable()) hilo.join();
            generando.store(false);
            if (!ultimo_error.empty()) {
                emit_signal("fallo", String::utf8(ultimo_error.c_str()));
                ultimo_error.clear();
            } else {
                emit_signal("terminado", String::utf8(respuesta_completa.c_str()));
            }
        }
    }

protected:
    void _empujar(const std::string& s) {
        std::lock_guard<std::mutex> lk(cola_mtx);
        cola.push_back(s);
    }

    // ── El bucle de generacion (corre en el hilo aparte) ──────────────
    void _generar(std::string prompt, int max_fichas, float temp, float top_p, uint32_t semilla) {
        respuesta_completa.clear();

        // 1. Tokenizar. Se pide primero el tamaño (numero negativo = cuantos
        //    hacen falta) y luego se llena: la forma que documenta llama.h.
        const int n_prompt = -llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(),
                                             nullptr, 0, true, true);
        if (n_prompt <= 0) { ultimo_error = "no se pudo tokenizar la peticion"; termino.store(true); return; }
        std::vector<llama_token> fichas((size_t)n_prompt);
        if (llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(),
                           fichas.data(), n_prompt, true, true) < 0) {
            ultimo_error = "fallo al tokenizar"; termino.store(true); return;
        }
        if (n_prompt >= n_ctx_actual - 16) {
            ultimo_error = "la peticion no cabe en la ventana de contexto del modelo";
            termino.store(true); return;
        }

        // 2. Cadena de muestreo. Con temperatura 0 se usa greedy (determinista),
        //    que es lo que interesa para transcribir codigo.
        llama_sampler_chain_params sp = llama_sampler_chain_default_params();
        sp.no_perf = true;
        llama_sampler* smpl = llama_sampler_chain_init(sp);
        if (temp <= 0.001f) {
            llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
        } else {
            llama_sampler_chain_add(smpl, llama_sampler_init_top_p(top_p, 1));
            llama_sampler_chain_add(smpl, llama_sampler_init_temp(temp));
            llama_sampler_chain_add(smpl, llama_sampler_init_dist(semilla));
        }

        // 3. Bucle: se evalua el lote y se van sacando fichas hasta el fin.
        llama_batch lote = llama_batch_get_one(fichas.data(), (int32_t)fichas.size());
        int generadas = 0;
        char pieza[512];
        while (!cancelar_flag.load()) {
            if (llama_decode(ctx, lote) != 0) { ultimo_error = "fallo de inferencia (decode)"; break; }
            const llama_token id = llama_sampler_sample(smpl, ctx, -1);
            if (llama_vocab_is_eog(vocab, id)) break;

            const int n = llama_token_to_piece(vocab, id, pieza, (int32_t)sizeof(pieza), 0, true);
            if (n > 0) {
                const std::string trozo(pieza, (size_t)n);
                respuesta_completa += trozo;
                _empujar(trozo);
            }
            llama_sampler_accept(smpl, id);
            if (++generadas >= max_fichas) break;
            static thread_local llama_token siguiente;
            siguiente = id;
            lote = llama_batch_get_one(&siguiente, 1);
        }
        llama_sampler_free(smpl);
        termino.store(true);
    }

public:
    GLLocalAI() { set_process(false); }
    ~GLLocalAI() { soltar_modelo(); }

    bool cargar_modelo(const String& ruta, int n_ctx, int hilos, int capas_gpu) {
        soltar_modelo();
        ultimo_error.clear();

        static bool backend_puesto = false;
        if (!backend_puesto) { llama_backend_init(); backend_puesto = true; }
        // El log de llama.cpp escupe cientos de lineas por carga: se calla.
        llama_log_set([](ggml_log_level, const char*, void*) {}, nullptr);

        // El .gguf se lee del disco REAL: llama.cpp no sabe de res://.
        const String abs = ProjectSettings::get_singleton()
                         ? ProjectSettings::get_singleton()->globalize_path(ruta) : ruta;
        const CharString cs = abs.utf8();

        llama_model_params mp = llama_model_default_params();
        // 0 = todo en CPU. Con el backend Vulkan compilado, un numero alto
        // (99) descarga TODAS las capas a la GPU. Se deja en 0 por defecto: en
        // una GPU integrada la memoria es compartida y no siempre gana — hay
        // que MEDIRLO en cada maquina antes de darlo por bueno.
        mp.n_gpu_layers = capas_gpu;
        modelo = llama_model_load_from_file(cs.get_data(), mp);
        if (!modelo) {
            ultimo_error = "no se pudo abrir el modelo (¿ruta o archivo .gguf mal?)";
            return false;
        }
        vocab = llama_model_get_vocab(modelo);

        llama_context_params cp = llama_context_default_params();
        cp.n_ctx   = (uint32_t)(n_ctx > 256 ? n_ctx : 4096);
        cp.n_batch = 512;
        const int nh = hilos > 0 ? hilos
                     : (int)MAX(1, (int)OS::get_singleton()->get_processor_count() - 2);
        cp.n_threads       = nh;
        cp.n_threads_batch = nh;
        ctx = llama_init_from_model(modelo, cp);
        if (!ctx) {
            ultimo_error = "no se pudo crear el contexto (¿poca memoria?)";
            llama_model_free(modelo); modelo = nullptr; vocab = nullptr;
            return false;
        }
        n_ctx_actual = (int)cp.n_ctx;
        ruta_modelo  = ruta;
        return true;
    }

    void soltar_modelo() {
        cancelar();
        if (hilo.joinable()) hilo.join();
        if (ctx)    { llama_free(ctx);          ctx = nullptr; }
        if (modelo) { llama_model_free(modelo); modelo = nullptr; }
        vocab = nullptr;
        ruta_modelo = String();
    }

    bool esta_listo() const     { return ctx != nullptr && modelo != nullptr; }
    bool esta_generando() const { return generando.load(); }
    String ultimo_error_txt() const { return String::utf8(ultimo_error.c_str()); }

    // Que backends lleva compilados este binario (para saber si hay Vulkan).
    Array backends() const {
        Array a;
        for (size_t i = 0; i < ggml_backend_reg_count(); i++) {
            ggml_backend_reg_t r = ggml_backend_reg_get(i);
            if (r) a.push_back(String::utf8(ggml_backend_reg_name(r)));
        }
        return a;
    }

    Dictionary info_modelo() const {
        Dictionary d;
        d["cargado"] = esta_listo();
        d["ruta"]    = ruta_modelo;
        d["n_ctx"]   = n_ctx_actual;
        if (modelo) {
            char buf[256] = {0};
            llama_model_desc(modelo, buf, sizeof(buf));
            d["descripcion"] = String::utf8(buf);
            d["parametros"]  = (int64_t)llama_model_n_params(modelo);
        }
        return d;
    }

    // `sistema` es el contexto que le damos nosotros (lo que sabe del motor y
    // del proyecto); `mensaje` es lo que escribio el usuario. Se arma con la
    // plantilla de chat DEL PROPIO MODELO, que es como sabe distinguir los
    // turnos — meterlo a pelo da respuestas mucho peores.
    bool preguntar(const String& sistema, const String& mensaje, int nivel) {
        if (!esta_listo())  { ultimo_error = "no hay modelo cargado"; return false; }
        if (generando.load()) return false;

        const CharString s_sis = sistema.utf8();
        const CharString s_msg = mensaje.utf8();
        llama_chat_message msgs[2] = {
            { "system", s_sis.get_data() },
            { "user",   s_msg.get_data() },
        };
        const char* plantilla = llama_model_chat_template(modelo, nullptr);
        std::vector<char> buf(32768);
        int n = llama_chat_apply_template(plantilla, msgs, 2, true, buf.data(), (int32_t)buf.size());
        if (n > (int)buf.size()) { buf.resize((size_t)n + 1); n = llama_chat_apply_template(plantilla, msgs, 2, true, buf.data(), (int32_t)buf.size()); }
        std::string prompt;
        if (n > 0) prompt.assign(buf.data(), (size_t)n);
        else {
            // Sin plantilla (modelo base): se cae a un formato sencillo.
            prompt = std::string(s_sis.get_data()) + "\n\n" + std::string(s_msg.get_data()) + "\n";
        }

        // Los tres niveles de pensamiento.
        int   fichas = 320;  float temp = 0.2f; float top_p = 0.9f;
        if (nivel <= 0)      { fichas = 160; temp = 0.1f; }
        else if (nivel >= 2) { fichas = 1024; temp = 0.35f; }

        cancelar_flag.store(false);
        termino.store(false);
        generando.store(true);
        { std::lock_guard<std::mutex> lk(cola_mtx); cola.clear(); }
        set_process(true);
        hilo = std::thread(&GLLocalAI::_generar, this, prompt, fichas, temp, top_p,
                           (uint32_t)Time::get_singleton()->get_ticks_usec());
        return true;
    }

    void cancelar() { cancelar_flag.store(true); }
};

#endif // GL_IA_H
