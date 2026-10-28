#ifndef GL_CARGA_DEFECTO_H
#define GL_CARGA_DEFECTO_H

// ════════════════════════════════════════════════════════════════════
//  PANTALLA DE CARGA POR DEFECTO, como la de Roblox.
//
//  Roblox pone la suya al entrar a un juego y la quita:
//    · en el acto, si un LocalScript de ReplicatedFirst llama a
//      ReplicatedFirst:RemoveDefaultLoadingScreen() (para poner la suya);
//    · sola, a los pocos segundos, si hay algo en ReplicatedFirst;
//    · al acabar la carga, si ReplicatedFirst esta vacio.
//  Aqui igual. Lo que se ve es la intro de Just Press it (el splash de Click
//  the Button, del usuario) con "Rocket Meteor × GodotLuau":
//    · del negro al morado; caen el logo de Rocket Meteor, la × (en el centro)
//      y el de GodotLuau, cada uno desde cerca de la camara, con su sombra, su
//      anillo de arcoiris, destello, halo que gira, humo, rebote y golpe;
//    · debajo se escribe el nombre del juego con la letra de la intro de
//      Roblox: Arcade (Press Start 2P) a TextSize 30, con sombra, anillo gris
//      con la parte de arriba en arcoiris y borde negro, a la resolucion real
//      de la ventana. Cada letra entra rebotando, con su nota cada vez mas
//      aguda, al ritmo de la carga: el nombre hace de barra. La × es la X de
//      la misma fuente, con el mismo estilo;
//    · al acabar, estrellas; las letras se parten en 2 y los logos se agrietan
//      y saltan en pedazos; y la pantalla baja con su borde ondulado y deja
//      ver el juego.
//  Las cifras (tiempos, curvas, colores, tamaños) son las del original, en px
//  de su lienzo de 640x360.
//
//  La animacion la hace la GPU (gl_carga_shaders.h): cada capa se dibuja una
//  vez y cada frame la CPU solo pasa el reloj y avisa cuando entra una letra.
//  Las letras y la × salen de un atlas pintado una vez por tamaño de ventana,
//  con un MultiMesh (una celda por letra) en tres pasadas; los trozos de la
//  rotura van en una malla con sus datos en una textura.
//
//  La animacion avanza como mucho 1/30 s por frame: un frame gordo de la carga
//  la para en vez de hacerla saltar; y no empieza hasta que los frames van
//  fluidos (hasta ahi, negro; mientras, se pinta el atlas y se compilan los
//  shaders). Se apaga con godotluau/carga/pantalla_por_defecto = false o
//  GL_CARGA_DEFECTO=0. En headless no sale.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/audio_stream_ogg_vorbis.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/classes/canvas_item_material.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/font_file.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/text_line.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include "gl_carga_estado.h"
#include "gl_carga_piezas.h"
#include "gl_carga_shaders.h"

// En px del lienzo de 640x360 del original.
#define GLC_ALTO 130.0f          // alto de los logos
#define GLC_HUECO 28.0f
#define GLC_FILA_Y 151.0f        // centro de la fila (el del logo del original)
#define GLC_TEXTO_Y 298.0f       // centro del nombre (el del texto del original)
// El texto de la intro de Roblox: TextSize 30 de Arcade es un em de 30 / 1,763
// (ver gl_fuentes.h), UIStroke de 2 (borde negro) y de 5 (anillo y sombra), y
// la sombra baja 4. El em de Roblox empieza 0,3645 em por encima del centro de
// su caja (medido en la intro grabada).
#define GLC_TEXTO_EM (30.0f / 1.763f)
#define GLC_BORDE 2.0f
#define GLC_ANILLO 5.0f
#define GLC_SOMBRA_DY 4.0f
#define GLC_EM_ARRIBA 0.3645f
// La × es la X de la fuente (7x7 pixeles de su em de 8): 7,5 px del lienzo por
// pixel, y sus trazos 1,5 veces los del texto.
#define GLC_ASPA_PIX 7.5f
#define GLC_ASPA_TRAZO 1.5f
// Tiempos, en s desde que empieza (el 30,5 del original).
#define GLC_TOQUE0 0.73333f      // el primer logo toca (31,233 del original)
#define GLC_ESCALON 0.25f
#define GLC_TEXTO_TRAS 0.9f      // el nombre empieza tras tocar el ultimo
#define GLC_LETRA_DUR 0.3f
#define GLC_ROMPER_TRAS 0.45f    // BREAK_DELAY
#define GLC_BAJAR_TRAS 0.4f      // DROP_AFTER
#define GLC_BAJAR_DUR 0.73333f
#define GLC_PIEZA_VIDA 1.3f
#define GLC_AUTO_RF 5.0          // con algo en ReplicatedFirst, se va sola a los 5 s
#define GLC_TOPE 15.0            // nunca se queda colgada
#define GLC_VOCES 16             // reproductores de sonido

inline Color gl_carga_morado() { return Color(0.10980392f, 0.015686275f, 0.08627451f); }

// GL_CARGA_TRAZA=1: fases, frames lentos y lo que cuesta (ver _process).
inline bool gl_carga_traza() {
    static const bool t = OS::get_singleton()->get_environment("GL_CARGA_TRAZA") == "1";
    return t;
}

class GLCargaDefecto;

// Lo que dibuja cada capa lo pide a su dueno (gl_carga_capas.h). Casi todas
// dibujan una vez: lo que se mueve lo mueven sus shaders.
#define GLC_CAPA(NOMBRE)                                     \
    class NOMBRE : public Control {                          \
        GDCLASS(NOMBRE, Control);                            \
    protected:                                               \
        static void _bind_methods() {}                       \
    public:                                                  \
        GLCargaDefecto* dueno = nullptr;                     \
        void _draw() override;                               \
    };
GLC_CAPA(GLCargaFondo)     // el fondo y, al final, su bajada con borde ondulado
GLC_CAPA(GLCargaAtras)     // las sombras de los logos
GLC_CAPA(GLCargaHalos)     // halo, anillo y destello de cada logo
GLC_CAPA(GLCargaDibujo)    // los logos (dentro del SubViewport que luego se rompe)
GLC_CAPA(GLCargaAtlas)     // las letras para el atlas (su propio SubViewport)
GLC_CAPA(GLCargaPantalla)  // el dibujo, o sus trozos al romperse
GLC_CAPA(GLCargaFrente)    // humo y estrellas
#undef GLC_CAPA

// Las letras y la ×, por capas como los TextLabel de la intro de Roblox (todas
// las sombras, luego todos los anillos, luego las letras): 0 sombra, 1 anillo,
// 2 letra con su borde. Cada capa es una pasada del mismo MultiMesh.
class GLCargaTexto : public Control {
    GDCLASS(GLCargaTexto, Control);
protected:
    static void _bind_methods() {}
public:
    GLCargaDefecto* dueno = nullptr;
    int capa = 0;
    void _draw() override;
};

// Una celda del atlas: una letra (o la ×, letra -1) con sus tres capas.
struct GLCargaCelda {
    int letra = -1;
    Rect2 en_atlas;      // px del atlas
    Vector2 casa;        // esquina en reposo, px de la ventana
};

class GLCargaDefecto : public CanvasLayer {
    GDCLASS(GLCargaDefecto, CanvasLayer);
    friend class GLCargaFondo;
    friend class GLCargaAtras;
    friend class GLCargaHalos;
    friend class GLCargaDibujo;
    friend class GLCargaAtlas;
    friend class GLCargaTexto;
    friend class GLCargaPantalla;
    friend class GLCargaFrente;

    // Capas (por orden de dibujo), lienzos y materiales.
    GLCargaFondo* fondo = nullptr;
    GLCargaAtras* atras = nullptr;
    GLCargaHalos* halos = nullptr;
    GLCargaPantalla* pantalla = nullptr;
    GLCargaFrente* frente = nullptr;
    SubViewport* vp = nullptr;          // logos, × y nombre: lo que se rompe
    GLCargaDibujo* dibujo = nullptr;
    GLCargaTexto* texto[3] = { nullptr, nullptr, nullptr };
    SubViewport* vp_atlas = nullptr;    // las letras, pintadas una vez
    GLCargaAtlas* atlas = nullptr;
    Ref<ShaderMaterial> m_halos, m_logos, m_trozos, m_glifos[3];
    Ref<ArrayMesh> cuadro;              // 0..1: la malla de cada celda
    Ref<MultiMesh> mm_glifos;
    Ref<ArrayMesh> malla_trozos;        // al romperse
    Ref<ArrayMesh> malla_vacia;         // un triangulo sin area: compila el shader de los trozos
    Ref<ImageTexture> tex_trozos;

    // Estado
    bool rf_mirado = false, rf_con_algo = false;
    double t_real = 0.0, t = 0.0;       // t: tiempo de animacion
    int fluidos = 0;
    int fase = 0;                        // 0 negro, 1 intro, 2 rotura, 3 bajada
    double prog = 0.0;
    uint32_t pend_max = 0;
    int tex_max = 0;
    int golpes = 0;                      // impactos ya lanzados (sonido + humo)
    int ondas = 0;
    double t_rotura = 0.0;               // desde que se rompio
    bool listo_texto = false;
    double t_texto_listo = -1.0;
    uint32_t semilla = 0x9E3779B9u;

    // El nombre y la ×, en px de la ventana (los del SubViewport). Los em van
    // en multiplos de 8: cada pixel de la fuente cae en px enteros.
    String nombre;
    LocalVector<double> t_letra;          // cuando entro cada letra (-1 = aun no)
    LocalVector<float> x_letra;           // borde izquierdo de cada letra, y el final
    int letras = 0;
    int letra_px = 48, aspa_px = 176;     // em
    float x0_texto = 0.0f, y_texto = 0.0f;   // esquina del em de la primera letra
    int borde_n = 8, anillo_n = 20;       // outline_size de Godot (sobresale n/4 px)
    int aspa_borde_n = 12, aspa_anillo_n = 30;
    float sombra_dy = 4.0f, aspa_sombra_dy = 6.0f;
    Ref<Font> fuente;
    // Cada letra (y la ×) ya formada: para el atlas y la sombra de la ×.
    LocalVector<Ref<TextLine>> linea_letra;
    Ref<TextLine> linea_aspa;
    // El atlas: sus celdas, la instancia de cada letra (-1 los espacios) y lo
    // que le sobra a cada celda alrededor del em (anillo; abajo, mas la sombra).
    LocalVector<GLCargaCelda> celdas;
    LocalVector<int> inst_letra;
    Vector2i atlas_tam = Vector2i(1, 1);
    int margen_letra = 0, margen_aspa = 0, alto_letra = 0, alto_aspa = 0;

    // Medidas: el lienzo de 640x360 encajado en los px reales de la ventana.
    Vector2i tam;
    float u = 1.0f;
    Vector2 o;                            // esquina del lienzo
    Vector2 f = Vector2(1, 1);            // de px de la ventana a la capa
    Vector2 centro[3];                    // de cada pieza, en px del lienzo
    Vector2 medida[3];
    Ref<ImageTexture> tex_rm, tex_gl, tex_borde, tex_humo, tex_estrella;
    Ref<AudioStream> son[4];              // impacto, letra, arcoiris, rotura
    uint64_t voces[GLC_VOCES] = {};       // los reproductores (ObjectID)
    int voz_sig = 0;

    LocalVector<GLCargaPieza> piezas;
    LocalVector<GLCargaParticula> particulas;
    double reloj_part = 0.0;              // las particulas van a 15 por segundo
    bool part_cambio = false;
    // Lo ultimo que se dibujo de las capas que casi no cambian.
    Color fondo_dibujado;
    int firma_atras = -1;
    bool redibujar_todo = true;

    // Para la traza: los frames mientras se ve, con reloj de pared (el delta de
    // Godot se recorta a 50 ms y esconde los tirones); la CPU de lo nuestro
    // (_process y los _draw de las capas) y la GPU de la ventana y del lienzo
    // durante la intro.
    uint64_t tr_antes = 0;
    int tr_frames = 0, tr_lentos = 0;
    double tr_suma = 0.0, tr_peor = 0.0;
    uint64_t tr_cpu_parte[6] = { 0, 0, 0, 0, 0, 0 };   // proceso, fondo+sombras, logos+halos, texto+atlas, pantalla, frente
    uint64_t tr_cpu_medidor = 0, tr_cpu_sonido = 0;     // dentro de proceso: medir (se resta) y sonidos
    int tr_frames_cpu = 0, tr_gpu_n = 0;
    double tr_gpu_raiz = 0.0, tr_gpu_vp = 0.0;

    float _azar(float a, float b) {
        semilla ^= semilla << 13;
        semilla ^= semilla >> 17;
        semilla ^= semilla << 5;
        return a + (b - a) * (float)(semilla & 0xFFFFFF) / (float)0xFFFFFF;
    }
    Vector2 P(const Vector2& v) const { return o + v * u; }      // lienzo -> px de la ventana
    float toque(int k) const { return GLC_TOQUE0 + k * GLC_ESCALON; }

    static PackedByteArray _bytes(int rec) {
        size_t n = 0;
        const unsigned char* d = gl_carga_recurso(rec, n);
        PackedByteArray b;
        if (!d) return b;
        b.resize((int64_t)n);
        memcpy(b.ptrw(), d, n);
        return b;
    }
    static Ref<ImageTexture> _png(int rec) {
        Ref<Image> img;
        img.instantiate();
        if (img->load_png_from_buffer(_bytes(rec)) != OK) return Ref<ImageTexture>();
        return ImageTexture::create_from_image(img);
    }
    // Un logo SVG rasterizado a `alto` px (se mide a escala 1 y se repite a la
    // buena): nitido a cualquier resolucion.
    static Ref<ImageTexture> _svg_a_alto(int rec, float alto) {
        const PackedByteArray svg = _bytes(rec);
        Ref<Image> img;
        img.instantiate();
        if (img->load_svg_from_buffer(svg, 1.0f) != OK || img->get_height() <= 0) return Ref<ImageTexture>();
        const float esc = alto / (float)img->get_height();
        img.instantiate();
        if (img->load_svg_from_buffer(svg, esc) != OK) return Ref<ImageTexture>();
        return ImageTexture::create_from_image(img);
    }

    void _preparar(const Vector2i& nuevo) {
        tam = nuevo;
        redibujar_todo = true;
        vp->set_size(tam);
        u = MIN(tam.x / 640.0f, tam.y / 360.0f);
        o = Vector2((tam.x - 640.0f * u) * 0.5f, (tam.y - 360.0f * u) * 0.5f);
        // La fila: la × en el centro, Rocket Meteor (logo entero) a su
        // izquierda y GodotLuau a su derecha, al mismo hueco.
        const float w_rm = GLC_ALTO * 2048.0f / 1321.0f;
        const float lado_x = 7.0f * GLC_ASPA_PIX;
        medida[0] = Vector2(w_rm, GLC_ALTO);
        medida[1] = Vector2(lado_x, lado_x);
        medida[2] = Vector2(GLC_ALTO, GLC_ALTO);
        centro[1] = Vector2(320.0f, GLC_FILA_Y);
        centro[0] = Vector2(320.0f - lado_x * 0.5f - GLC_HUECO - w_rm * 0.5f, GLC_FILA_Y);
        centro[2] = Vector2(320.0f + lado_x * 0.5f + GLC_HUECO + GLC_ALTO * 0.5f, GLC_FILA_Y);
        // Logos nitidos al tamaño real: cada SVG se rasteriza a su alto en px.
        tex_rm = _svg_a_alto(GLC_REC_LOGO_RM, GLC_ALTO * u);
        tex_gl = _svg_a_alto(GLC_REC_LOGO_GL, GLC_ALTO * u);
        // El nombre, a la resolucion real: el em de TextSize 30 (17 px del
        // lienzo) en px de la ventana, redondeado a multiplo de 8; un nombre
        // largo baja hasta caber en 600 px del lienzo.
        const int nl = nombre.length();
        const float em_v = nl > 0 ? MIN(GLC_TEXTO_EM, 600.0f / (float)nl) : GLC_TEXTO_EM;
        letra_px = MAX(1, (int)Math::round(em_v * u / 8.0f)) * 8;
        while (letra_px > 8 && (float)(nl * letra_px) > 600.0f * u) letra_px -= 8;
        x_letra.resize(nl + 1);
        for (int i = 0; i <= nl; i++)
            x_letra[i] = (i == 0 || fuente.is_null()) ? (float)(i * letra_px)
                : fuente->get_string_size(nombre.substr(0, i), HORIZONTAL_ALIGNMENT_LEFT, -1, letra_px).x;
        x0_texto = Math::round(P(Vector2(320.0f, 0.0f)).x - x_letra[nl] * 0.5f);
        y_texto = Math::round(P(Vector2(0.0f, GLC_TEXTO_Y)).y - GLC_EM_ARRIBA * letra_px);
        aspa_px = MAX(1, (int)Math::round(GLC_ASPA_PIX * u)) * 8;
        // Trazos: Godot hace sobresalir el contorno outline_size / 4 px.
        borde_n = MAX(1, (int)Math::round(4.0f * GLC_BORDE * u));
        anillo_n = MAX(1, (int)Math::round(4.0f * GLC_ANILLO * u));
        aspa_borde_n = MAX(1, (int)Math::round(4.0f * GLC_BORDE * GLC_ASPA_TRAZO * u));
        aspa_anillo_n = MAX(1, (int)Math::round(4.0f * GLC_ANILLO * GLC_ASPA_TRAZO * u));
        sombra_dy = Math::round(GLC_SOMBRA_DY * u);
        aspa_sombra_dy = Math::round(GLC_SOMBRA_DY * GLC_ASPA_TRAZO * u);
        linea_letra.resize(nl);
        for (int i = 0; i < nl; i++) {
            Ref<TextLine> l;
            if (fuente.is_valid() && nombre[i] != ' ') {
                l.instantiate();
                l->add_string(nombre.substr(i, 1), fuente, letra_px);
            }
            linea_letra[i] = l;
        }
        linea_aspa.unref();
        if (fuente.is_valid()) {
            linea_aspa.instantiate();
            linea_aspa->add_string(GLC_ASPA_LETRA, fuente, aspa_px);
        }
        _montar_atlas();
        _uniformes_fijos();
        dibujo->queue_redraw();
        halos->queue_redraw();
        pantalla->queue_redraw();
        for (GLCargaTexto* c : texto) c->queue_redraw();
    }

    // El atlas de las letras: cada una (y la ×) en su celda, con sus tres capas
    // en tres canales (rojo la letra, verde la letra con su borde, azul la
    // letra con su anillo). Se pinta una vez (vp_atlas, UPDATE_ONCE); cada
    // frame las letras salen de aqui en tres pasadas. La celda deja alrededor
    // del em lo que sobresale el anillo y, abajo, ademas la sombra.
    void _montar_atlas() {
        celdas.clear();
        const int nl = nombre.length();
        inst_letra.resize(nl);
        margen_letra = (int)Math::ceil(anillo_n / 4.0f) + 2;
        margen_aspa = (int)Math::ceil(aspa_anillo_n / 4.0f) + 2;
        alto_letra = letra_px + 2 * margen_letra + (int)sombra_dy;
        alto_aspa = aspa_px + 2 * margen_aspa + (int)aspa_sombra_dy;
        const int ancho_max = 2048;
        int x = 1, y = 1, fila = 0, ancho = 1;
        for (int i = 0; i < nl; i++) {
            inst_letra[i] = -1;
            if (linea_letra[i].is_null()) continue;
            const int w = (int)Math::ceil(x_letra[i + 1] - x_letra[i]) + 2 * margen_letra;
            if (x + w + 1 > ancho_max && x > 1) {
                x = 1;
                y += fila + 2;
                fila = 0;
            }
            GLCargaCelda c;
            c.letra = i;
            c.en_atlas = Rect2(x, y, w, alto_letra);
            c.casa = Vector2(x0_texto + x_letra[i] - margen_letra, y_texto - margen_letra);
            inst_letra[i] = (int)celdas.size();
            celdas.push_back(c);
            x += w + 2;
            fila = MAX(fila, alto_letra);
            ancho = MAX(ancho, x);
        }
        if (fila > 0) y += fila + 2;
        GLCargaCelda cx;
        cx.en_atlas = Rect2(1, y, aspa_px + 2 * margen_aspa, alto_aspa);
        cx.casa = gl_carga_aspa_origen(P(centro[1]), (float)aspa_px, 1.0f) - Vector2(margen_aspa, margen_aspa);
        celdas.push_back(cx);
        ancho = MAX(ancho, 2 + aspa_px + 2 * margen_aspa);
        atlas_tam = Vector2i(ancho + 1, y + alto_aspa + 1);
        vp_atlas->set_size(atlas_tam);
        vp_atlas->set_update_mode(SubViewport::UPDATE_ONCE);
        atlas->queue_redraw();
        // Una instancia por celda, en su sitio de reposo; su color dice donde
        // esta en el atlas.
        const Vector2 inv(1.0f / atlas_tam.x, 1.0f / atlas_tam.y);
        mm_glifos->set_instance_count((int)celdas.size());
        for (uint32_t k = 0; k < celdas.size(); k++) {
            const GLCargaCelda& c = celdas[k];
            const Rect2& r = c.en_atlas;
            mm_glifos->set_instance_transform_2d((int)k, Transform2D(Vector2(r.size.x, 0.0f), Vector2(0.0f, r.size.y), c.casa));
            mm_glifos->set_instance_color((int)k, Color(r.position.x * inv.x, r.position.y * inv.y,
                                                        r.get_end().x * inv.x, r.get_end().y * inv.y));
            mm_glifos->set_instance_custom_data((int)k, _dato_glifo(c.letra));
        }
    }

    // El dato propio de cada celda: (cuando entro, indice o -1 la ×, centro en
    // el lienzo). Una letra que aun no ha entrado entra "nunca".
    Color _dato_glifo(int i) const {
        if (i < 0) return Color(0.0f, -1.0f, 320.0f, 0.0f);
        const float x_v = (x0_texto + (x_letra[i] + x_letra[i + 1]) * 0.5f - o.x) / u;
        return Color(i < letras ? (float)t_letra[i] : 1.0e6f, (float)i, x_v, 0.0f);
    }

    // Los cuatro relojes de la animacion, como uniformes globales de shader:
    // cuatro llamadas por frame para todos los materiales (eran trece). Se
    // registran antes de compilar los shaders y se quedan (cuatro floats que no
    // se guardan en el proyecto): quitarlos con materiales aun vivos que los
    // usan daria errores.
    static void _registrar_relojes() {
        static bool hechos = false;   // la lista de globales solo se puede leer en el editor
        if (hechos) return;
        hechos = true;
        static const char* const nombres[4] = { "gl_carga_reloj", "gl_carga_reloj_real", "gl_carga_rotura", "gl_carga_bajada" };
        RenderingServer* rs = RenderingServer::get_singleton();
        for (const char* n : nombres) rs->global_shader_parameter_add(StringName(n), RenderingServer::GLOBAL_VAR_TYPE_FLOAT, 0.0f);
    }
    void _relojes() {
        static const StringName k_reloj("gl_carga_reloj"), k_real("gl_carga_reloj_real"), k_rotura("gl_carga_rotura"),
            k_bajada("gl_carga_bajada");
        RenderingServer* rs = RenderingServer::get_singleton();
        rs->global_shader_parameter_set(k_reloj, (float)t);
        rs->global_shader_parameter_set(k_real, (float)t_real);
        rs->global_shader_parameter_set(k_rotura, fase >= 2 ? (float)t_rotura : -1.0f);
        rs->global_shader_parameter_set(k_bajada, _bajada() * u);
    }

    // Lo que los shaders necesitan de las medidas (cambia con la ventana).
    void _uniformes_fijos() {
        m_logos->set_shader_parameter("escala", u);
        m_logos->set_shader_parameter("toques", Vector2(toque(0), toque(2)));
        m_logos->set_shader_parameter("centro0", P(centro[0]));
        m_logos->set_shader_parameter("centro1", P(centro[2]));
        for (const Ref<ShaderMaterial>& m : m_glifos) {
            m->set_shader_parameter("escala", u);
            m->set_shader_parameter("x_centro", P(centro[1]));
            m->set_shader_parameter("x_toque", toque(1));
            m->set_shader_parameter("medida_letra", Vector4(alto_letra, margen_letra, letra_px, sombra_dy));
            m->set_shader_parameter("medida_aspa", Vector4(alto_aspa, margen_aspa, aspa_px, aspa_sombra_dy));
            m->set_shader_parameter("atlas", Vector2(atlas_tam));
        }
        m_halos->set_shader_parameter("escala", u);
        m_halos->set_shader_parameter("toques", Vector3(toque(0), toque(1), toque(2)));
        m_halos->set_shader_parameter("centro0", P(centro[0]));
        m_halos->set_shader_parameter("centro1", P(centro[1]));
        m_halos->set_shader_parameter("centro2", P(centro[2]));
    }

    // Los sonidos salen de un grupo de reproductores creado al montar, por
    // turnos: crear, colgar y soltar un nodo por nota costaba ~1 ms cada vez.
    // 16 dan para las notas de un nombre largo (una cada 0,04 s, de hasta 0,4 s).
    void _sonar(int i, float db, float tono) {
        if (son[i].is_null()) return;
        GLCargaCrono crono(tr_cpu_sonido);
        AudioStreamPlayer* a = Object::cast_to<AudioStreamPlayer>(ObjectDB::get_instance(ObjectID(voces[voz_sig])));
        voz_sig = (voz_sig + 1) % GLC_VOCES;
        if (!a) return;
        a->set_stream(son[i]);
        a->set_volume_db(db);
        a->set_pitch_scale(tono);
        a->play();
    }

    // Humo del impacto (big_smoke del original: 20 de una vez, un disco de 106
    // px, hacia arriba con 87° de abanico, gravedad 98, 10 fotogramas).
    void _humo(int k) {
        const int n = k == 1 ? 8 : 20;
        const float radio = medida[k].x * 0.45f;
        const Vector2 c = centro[k] + Vector2(0.0f, medida[k].y * 0.28f);
        for (int i = 0; i < n; i++) {
            GLCargaParticula p;
            const float a = _azar(0.0f, Math_TAU), r = radio * Math::sqrt(_azar(0.0f, 1.0f));
            p.pos = P(c + Vector2(Math::cos(a), Math::sin(a)) * r);
            const float dir = Math::deg_to_rad(-90.0f + _azar(-87.127f, 87.127f));
            p.vel = Vector2(Math::cos(dir), Math::sin(dir)) * _azar(103.6f, 235.41f) * u;
            p.ang = Math::deg_to_rad(_azar(-720.0f, 720.0f));
            p.giro = Math::deg_to_rad(_azar(-119.0f, 115.0f));
            p.vida = MAX(0.15f, 2.0f * (1.0f - _azar(0.0f, 1.0f)));
            p.escala = _azar(0.5f, 1.0f);
            particulas.push_back(p);
        }
        part_cambio = true;
    }

    // Estrellas al acabar el nombre (star del original: 10, en todas
    // direcciones, frenando, a velocidad x3).
    void _estrellas() {
        for (int i = 0; i < 10; i++) {
            GLCargaParticula p;
            p.estrella = true;
            p.pos = P(Vector2(320.0f, GLC_TEXTO_Y));
            const float a = _azar(0.0f, Math_TAU);
            p.vel = Vector2(Math::cos(a), Math::sin(a)) * _azar(70.0f, 345.48f) * u;
            p.ang = Math::deg_to_rad(_azar(-720.0f, 720.0f));
            p.giro = Math::deg_to_rad(_azar(-100.0f, 100.0f)) * 3.0f;
            p.vida = 2.0f * (1.0f - _azar(0.0f, 0.54f)) / 3.0f;
            p.escala = _azar(0.1f, 1.2f);
            p.freno = _azar(1.0f, 2.0f) * 3.0f;
            particulas.push_back(p);
        }
        part_cambio = true;
    }

    // Van a 15 pasos por segundo, como el original: entre paso y paso no hay
    // nada que volver a dibujar.
    void _mover_particulas(double d) {
        reloj_part += d;
        const float paso = 1.0f / 15.0f;
        while (reloj_part >= paso) {
            reloj_part -= paso;
            if (!particulas.is_empty()) part_cambio = true;
            for (uint32_t i = 0; i < particulas.size();) {
                GLCargaParticula& p = particulas[i];
                p.edad += paso;
                if (p.edad >= p.vida) {
                    particulas.remove_at_unordered(i);
                    continue;
                }
                if (p.estrella) p.vel *= MAX(0.0f, 1.0f - p.freno * MIN(1.0f, p.edad / p.vida / 0.358f) * paso);
                else p.vel.y += 98.0f * u * paso;
                p.pos += p.vel * paso;
                p.ang += p.giro * paso;
                i++;
            }
        }
    }

    // Cuanto va de carga (0..1): scripts que faltan por arrancar, texturas en
    // cola y que los frames ya vayan fluidos. Nunca vuelve atras.
    void _medir_progreso() {
        GLColaArranque& c = gl_cola_arranque();
        uint32_t pend = 0;
        for (int i = 0; i < 3; i++) pend += c.cola[i].size() - c.cabeza[i];
        pend_max = MAX(pend_max, pend);
        const double p_scr = pend_max ? 1.0 - (double)pend / (double)pend_max : 1.0;
        const int tp = gl_tex_pendientes();
        tex_max = MAX(tex_max, tp);
        const double p_tex = tex_max ? 1.0 - (double)tp / (double)tex_max : 1.0;
        const double p_flu = MIN(1.0, fluidos / 12.0);
        double p = 0.45 * p_scr + 0.35 * p_tex + 0.2 * p_flu;
        if (t_real > GLC_TOPE) p = 1.0;
        prog = MAX(prog, p);
    }

    // Cada letra entra 0,1 s despues de la anterior (como mucho 1,3 s el nombre
    // entero) y nunca antes de que la carga llegue a ella. Entrar es apuntar en
    // su instancia cuando: su rebote lo hace el shader.
    void _escribir() {
        const double t0 = toque(2) + GLC_TEXTO_TRAS;
        const int nl = nombre.length();
        if (t < t0) return;
        const double paso = MIN(0.1, nl > 0 ? 1.3 / nl : 0.1);
        while (letras < nl) {
            const double cuando = MAX(t0 + letras * paso, letras > 0 ? t_letra[letras - 1] + paso : t0);
            if (t < cuando || prog + 1e-6 < (double)(letras + 1) / nl) break;
            t_letra[letras] = t;
            letras++;
            const int i = letras - 1;
            if (i < (int)inst_letra.size() && inst_letra[i] >= 0) mm_glifos->set_instance_custom_data(inst_letra[i], _dato_glifo(i));
            // text_audio: tono 0,5 y +0,1 antes de cada letra.
            if (nombre[i] != ' ') _sonar(1, -12.667f, 0.5f + 0.1f * letras);
        }
        if (!listo_texto && letras >= nl && (nl == 0 ? prog >= 1.0 : t >= t_letra[nl - 1] + GLC_LETRA_DUR)) {
            listo_texto = true;
            t_texto_listo = t;
            _sonar(2, -5.867f, 1.0f);   // text_complete (rainbow_open)
            _estrellas();
        }
    }

    // ── La rotura (Motor2D Effects.Shatter, con las opciones del splash) ──
    void _pieza(const LocalVector<Vector2>& poli, const Vector2& empuje, const Vector2& explosion, float grieta, float temblor) {
        if (poli.size() < 3) return;
        GLCargaPieza p;
        Vector2 g;
        for (const Vector2& q : poli) g += q;
        g /= (float)poli.size();
        p.casa = g;
        const Vector2 t_inv(1.0f / MAX(1, tam.x), 1.0f / MAX(1, tam.y));
        for (const Vector2& q : poli) {
            p.p.push_back(q - g);
            p.uv.push_back(q * t_inv);
        }
        const Vector2 lejos = g - explosion;
        const Vector2 dir = lejos.length() > 0.001f ? lejos.normalized() : Vector2(_azar(-0.5f, 0.5f), -1.0f).normalized();
        p.vel = (dir * _azar(40.0f, 140.0f) + empuje + Vector2(0.0f, -170.0f * _azar(0.6f, 1.2f))) * u;
        p.giro = _azar(-360.0f, 360.0f);
        p.grieta = grieta;
        p.temblor = temblor * u;
        piezas.push_back(p);
    }

    static LocalVector<Vector2> _rect(const Rect2& r) {
        LocalVector<Vector2> v;
        v.push_back(r.position);
        v.push_back(Vector2(r.get_end().x, r.position.y));
        v.push_back(r.get_end());
        v.push_back(Vector2(r.position.x, r.get_end().y));
        return v;
    }

    void _romper() {
        fase = 2;
        t_rotura = 0.0;
        vp->set_update_mode(SubViewport::UPDATE_DISABLED);   // se queda con el ultimo dibujo
        piezas.clear();
        LocalVector<Vector2> a, b, c, d;
        // Las letras, en 2 trozos cada una (un corte en cualquier angulo); los
        // dos trozos se separan 70 px/s. Cada trozo lleva la letra con su
        // anillo y su sombra.
        const Vector2 centro_txt = P(Vector2(320.0f, GLC_TEXTO_Y));
        const float margen = (GLC_ANILLO + 1.0f) * u;
        for (int i = 0; i < letras; i++) {
            if (nombre[i] == ' ') continue;
            const float ola = Math::round(Math::sin((float)t * 7.0f + i * 0.8f) * 1.5f * u);
            const Rect2 r(Vector2(x0_texto + x_letra[i] - margen, y_texto + ola - margen),
                          Vector2(x_letra[i + 1] - x_letra[i] + margen * 2.0f, letra_px + margen * 2.0f + sombra_dy));
            const float ang = _azar(0.0f, (float)Math_PI);
            const Vector2 n(Math::cos(ang), Math::sin(ang));
            const Vector2 p0 = r.get_center() + Vector2(_azar(-0.35f, 0.35f) * r.size.x * 0.5f, _azar(-0.35f, 0.35f) * r.size.y * 0.5f);
            gl_carga_cortar(_rect(r), p0, n, a, b);
            _pieza(a, n * 70.0f, centro_txt, 0.14f, 1.5f);
            _pieza(b, -n * 70.0f, centro_txt, 0.14f, 1.5f);
        }
        // Los logos, por zonas, cada zona en 4 trozos con dos cortes que se
        // cruzan; se agrietan 0,22 s (temblando 2 px) y saltan. La zona de la
        // × llega hasta su anillo y su sombra.
        static const int cols[3] = { 4, 2, 3 }, filas[3] = { 3, 2, 3 };
        for (int k = 0; k < 3; k++) {
            const float m = k == 1 ? (GLC_ANILLO + GLC_SOMBRA_DY) * GLC_ASPA_TRAZO + 2.0f : 4.0f;
            const Rect2 r(P(centro[k] - medida[k] * 0.5f - Vector2(m, m)), (medida[k] + Vector2(m, m) * 2.0f) * u);
            const Vector2 celda(r.size.x / cols[k], r.size.y / filas[k]);
            for (int j = 0; j < filas[k]; j++)
                for (int i = 0; i < cols[k]; i++) {
                    const Rect2 zona(r.position + Vector2(i * celda.x, j * celda.y), celda);
                    const Vector2 cz = zona.get_center();
                    const float a1 = _azar(0.0f, (float)Math_PI);
                    const float a2 = a1 + (float)Math_PI * 0.5f + _azar(-0.5f, 0.5f);
                    const Vector2 n1(Math::cos(a1), Math::sin(a1)), n2(Math::cos(a2), Math::sin(a2));
                    const Vector2 corr(_azar(-0.35f, 0.35f) * celda.x * 0.5f, _azar(-0.35f, 0.35f) * celda.y * 0.5f);
                    gl_carga_cortar(_rect(zona), cz + corr, n1, a, b);
                    for (const LocalVector<Vector2>* mitad : { &a, &b }) {
                        gl_carga_cortar(*mitad, cz + corr, n2, c, d);
                        for (const LocalVector<Vector2>* q : { &c, &d }) {
                            if (q->size() < 3) continue;
                            Vector2 g;
                            for (const Vector2& v : *q) g += v;
                            g /= (float)q->size();
                            const Vector2 fuera = g - cz;
                            _pieza(*q, fuera.length() > 0.001f ? fuera.normalized() * 70.0f : Vector2(),
                                   P(centro[k]), 0.22f, 2.0f);
                        }
                    }
                }
        }
        _subir_trozos();
        _sonar(3, -6.0f, 1.1f * _azar(1.0f / 1.05f, 1.05f));   // button_break
    }

    // Los trozos a la GPU: una malla con todos (cada vertice con el indice de
    // su trozo en el rojo y el verde, a 8 bits) y sus datos en una textura de
    // dos filas; el shader los mueve y la CPU solo le pasa el reloj.
    void _subir_trozos() {
        const int n = (int)piezas.size();
        if (n == 0) return;
        PackedVector2Array v, uv;
        PackedColorArray col;
        PackedInt32Array idx;
        PackedByteArray datos;
        datos.resize((int64_t)n * 2 * 4 * (int64_t)sizeof(float));
        float* w = (float*)datos.ptrw();
        for (int i = 0; i < n; i++) {
            const GLCargaPieza& p = piezas[i];
            const Color marca((float)(i % 256) / 255.0f, (float)(i / 256) / 255.0f, 0.0f, 1.0f);
            const int base = (int)v.size();
            for (uint32_t k = 0; k < p.p.size(); k++) {
                v.push_back(p.p[k]);
                uv.push_back(p.uv[k]);
                col.push_back(marca);
            }
            for (int k = 1; k + 1 < (int)p.p.size(); k++) {
                idx.push_back(base);
                idx.push_back(base + k);
                idx.push_back(base + k + 1);
            }
            float* f0 = w + (int64_t)i * 4;
            f0[0] = p.casa.x;
            f0[1] = p.casa.y;
            f0[2] = p.vel.x;
            f0[3] = p.vel.y;
            float* f1 = w + ((int64_t)n + i) * 4;
            f1[0] = p.giro;
            f1[1] = p.grieta;
            f1[2] = p.temblor;
            f1[3] = GLC_PIEZA_VIDA;
        }
        Array arr;
        arr.resize(Mesh::ARRAY_MAX);
        arr[Mesh::ARRAY_VERTEX] = v;
        arr[Mesh::ARRAY_TEX_UV] = uv;
        arr[Mesh::ARRAY_COLOR] = col;
        arr[Mesh::ARRAY_INDEX] = idx;
        malla_trozos.instantiate();
        malla_trozos->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
        tex_trozos = ImageTexture::create_from_image(Image::create_from_data(n, 2, false, Image::FORMAT_RGBAF, datos));
        m_trozos->set_shader_parameter("datos", tex_trozos);
        m_trozos->set_shader_parameter("gravedad", 900.0f * u);
        m_trozos->set_shader_parameter("modo", 1);
        pantalla->queue_redraw();
    }

    bool _replicated_first_con_algo() {
        Node* dm = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(dm_id)));
        Node* rf = dm ? dm->get_node_or_null(NodePath("ReplicatedFirst")) : nullptr;
        return rf && rf->get_child_count() > 0;
    }

    // Un shader: lo de delante (render_mode), las curvas comunes y lo suyo.
    Ref<ShaderMaterial> _material(const char* cuerpo, const char* delante = "") {
        Ref<Shader> s;
        s.instantiate();
        s->set_code(String("shader_type canvas_item;\n") + String(delante) + String(GL_CARGA_GLSL_COMUN) + String(cuerpo));
        Ref<ShaderMaterial> m;
        m.instantiate();
        m->set_shader(s);
        return m;
    }

    static Ref<ArrayMesh> _malla(const PackedVector2Array& v, const PackedVector2Array& uv, const PackedInt32Array& idx) {
        Array arr;
        arr.resize(Mesh::ARRAY_MAX);
        arr[Mesh::ARRAY_VERTEX] = v;
        arr[Mesh::ARRAY_TEX_UV] = uv;
        arr[Mesh::ARRAY_INDEX] = idx;
        Ref<ArrayMesh> m;
        m.instantiate();
        m->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
        return m;
    }

    // Capa a pantalla completa (Control) colgada de la capa.
    template <typename T>
    T* _capa(Node* padre, Control::MouseFilter raton) {
        T* c = memnew(T);
        c->dueno = this;
        c->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
        c->set_mouse_filter(raton);
        padre->add_child(c, false, Node::INTERNAL_MODE_BACK);
        return c;
    }

    static SubViewport* _lienzo(Node* padre, SubViewport::UpdateMode modo) {
        SubViewport* s = memnew(SubViewport);
        s->set_transparent_background(true);
        s->set_disable_3d(true);
        s->set_update_mode(modo);
        s->set_disable_input(true);
        padre->add_child(s, false, Node::INTERNAL_MODE_BACK);
        return s;
    }

    void _montar() {
        set_layer(1024);
        set_process_mode(Node::PROCESS_MODE_ALWAYS);
        nombre = String(ProjectSettings::get_singleton()->get_setting("application/config/name", ""));
        t_letra.resize(nombre.length());
        // Press Start 2P (OFL, en GodotLuau/assets/fonts), la Arcade de Roblox.
        // Sin hinting ni subpixel: con el em en multiplos de 8 cada pixel de la
        // letra cae en px enteros y sale nitido; el suavizado queda para las
        // esquinas redondas de los trazos (como el UIStroke de Roblox).
        Ref<FontFile> base = gl_fuente_archivo("pressstart2p-regular.ttf");
        if (base.is_valid() && !base->get_data().is_empty()) {
            Ref<FontFile> fp;
            fp.instantiate();
            fp->set_data(base->get_data());
            fp->set_hinting(TextServer::HINTING_NONE);
            fp->set_subpixel_positioning(TextServer::SUBPIXEL_POSITIONING_DISABLED);
            fp->set_generate_mipmaps(false);
            fuente = fp;
        }
        if (fuente.is_null()) fuente = ThemeDB::get_singleton()->get_fallback_font();
        tex_borde = _png(GLC_REC_BORDE);
        tex_humo = _png(GLC_REC_HUMO);
        tex_estrella = _png(GLC_REC_ESTRELLA);
        for (int i = 0; i < 4; i++) {
            if (GLC_REC_SON_IMPACTO + i == GLC_REC_SON_LETRA) {
                // La nota de las letras va en PCM (16 bits, estereo, 44100 Hz).
                Ref<AudioStreamWAV> w;
                w.instantiate();
                w->set_format(AudioStreamWAV::FORMAT_16_BITS);
                w->set_stereo(true);
                w->set_mix_rate(44100);
                w->set_data(_bytes(GLC_REC_SON_LETRA));
                son[i] = w;
            } else {
                son[i] = AudioStreamOggVorbis::load_from_buffer(_bytes(GLC_REC_SON_IMPACTO + i));
            }
        }
        for (uint64_t& v : voces) {
            AudioStreamPlayer* a = memnew(AudioStreamPlayer);
            add_child(a, false, Node::INTERNAL_MODE_BACK);
            v = (uint64_t)a->get_instance_id();
        }

        _registrar_relojes();
        _relojes();

        // En la ventana: fondo, sombras de los logos, halos, la composicion (o
        // sus trozos) y, encima, humo y estrellas.
        fondo = _capa<GLCargaFondo>(this, Control::MOUSE_FILTER_STOP);   // el juego no recibe clics mientras
        atras = _capa<GLCargaAtras>(this, Control::MOUSE_FILTER_IGNORE);
        halos = _capa<GLCargaHalos>(this, Control::MOUSE_FILTER_IGNORE);
        m_halos = _material(GL_CARGA_SHADER_HALOS);
        halos->set_material(m_halos);
        pantalla = _capa<GLCargaPantalla>(this, Control::MOUSE_FILTER_IGNORE);
        m_trozos = _material(GL_CARGA_SHADER_TROZOS, GL_CARGA_MODO_TROZOS);
        pantalla->set_material(m_trozos);
        frente = _capa<GLCargaFrente>(this, Control::MOUSE_FILTER_IGNORE);

        // El atlas de las letras: sumando, cada capa a su canal.
        vp_atlas = _lienzo(this, SubViewport::UPDATE_DISABLED);
        atlas = _capa<GLCargaAtlas>(vp_atlas, Control::MOUSE_FILTER_IGNORE);
        Ref<CanvasItemMaterial> suma;
        suma.instantiate();
        suma->set_blend_mode(CanvasItemMaterial::BLEND_MODE_ADD);
        atlas->set_material(suma);

        // El lienzo de la composicion (lo que luego se rompe), a los px de la
        // ventana: los logos y, encima, las tres pasadas de las letras.
        vp = _lienzo(this, SubViewport::UPDATE_ALWAYS);
        dibujo = _capa<GLCargaDibujo>(vp, Control::MOUSE_FILTER_IGNORE);
        m_logos = _material(GL_CARGA_SHADER_LOGOS);
        dibujo->set_material(m_logos);
        PackedVector2Array qv;
        qv.push_back(Vector2(0, 0));
        qv.push_back(Vector2(1, 0));
        qv.push_back(Vector2(1, 1));
        qv.push_back(Vector2(0, 1));
        PackedInt32Array qi;
        for (int k : { 0, 1, 2, 0, 2, 3 }) qi.push_back(k);
        cuadro = _malla(qv, qv, qi);
        PackedVector2Array tv;
        for (int k = 0; k < 3; k++) tv.push_back(Vector2());
        PackedInt32Array ti;
        for (int k : { 0, 1, 2 }) ti.push_back(k);
        malla_vacia = _malla(tv, tv, ti);
        mm_glifos.instantiate();
        mm_glifos->set_transform_format(MultiMesh::TRANSFORM_2D);
        mm_glifos->set_use_colors(true);
        mm_glifos->set_use_custom_data(true);
        mm_glifos->set_mesh(cuadro);
        for (int i = 0; i < 3; i++) {
            texto[i] = _capa<GLCargaTexto>(vp, Control::MOUSE_FILTER_IGNORE);
            texto[i]->capa = i;
            m_glifos[i] = _material(GL_CARGA_SHADER_GLIFOS, GL_CARGA_MODO_GLIFOS);
            m_glifos[i]->set_shader_parameter("capa", i);
            texto[i]->set_material(m_glifos[i]);
        }

        if (gl_carga_traza()) {
            RenderingServer* rs = RenderingServer::get_singleton();
            rs->viewport_set_measure_render_time(get_viewport()->get_viewport_rid(), true);
            rs->viewport_set_measure_render_time(vp->get_viewport_rid(), true);
            // Control para medir: la logica entera, sin dibujar nada en la ventana.
            if (OS::get_singleton()->get_environment("GL_CARGA_OCULTA") == "1") set_visible(false);
        }

        const Vector2i ws = get_tree()->get_root()->get_size();
        _preparar(Vector2i(MAX(ws.x, 64), MAX(ws.y, 64)));
        set_process(true);
    }

    // La pantalla baja: en px del lienzo, con el borde ondulado de la imagen
    // (filas 57 a 84) siempre fuera de la ventana al empezar.
    float _subida() const { return MAX(0.0f, Math::ceil(-24.0f - (-o.y / u))); }
    float _bajada() const {
        if (fase < 3) return 0.0f;
        const float vis_max = (tam.y - o.y) / u;
        const float estirar = MAX(1.0f, (vis_max - (-51.0f - _subida())) / 513.0f);
        const float x = (float)((t_rotura - GLC_BAJAR_TRAS) / GLC_BAJAR_DUR);
        return 513.0f * estirar * gl_carga_ease(x, 4.2870927f);
    }
    Color _color_fondo() const {
        if (fase >= 3) {
            const float x = (float)CLAMP((t_rotura - GLC_BAJAR_TRAS + 0.033) / 0.8, 0.0, 1.0);
            return gl_carga_morado().lerp(Color(0, 0, 0), x);
        }
        if (fase == 0) return Color(0, 0, 0);
        return Color(0, 0, 0).lerp(gl_carga_morado(), (float)CLAMP((t - 0.3) / 0.96667, 0.0, 1.0));
    }

protected:
    static void _bind_methods() {}

    void _notification(int p_what) {
        if (p_what == NOTIFICATION_READY) {
            _montar();
        } else if (p_what == NOTIFICATION_PREDELETE) {
            if (gl_carga_defecto_id() == (uint64_t)get_instance_id()) gl_carga_defecto_id() = 0;
            if (gl_carga_traza()) {
                UtilityFunctions::print("[CARGA] fuera  t_real=", t_real, "  fase=", fase, "  quitada_por_el_juego=", gl_carga_quitada(),
                                        "  frames=", tr_frames, "  media_ms=", tr_frames ? tr_suma / tr_frames : 0.0,
                                        "  peor_ms=", tr_peor, "  de_mas_de_33ms=", tr_lentos);
                const double n = MAX(1, tr_frames_cpu);
                tr_cpu_parte[0] -= MIN(tr_cpu_parte[0], tr_cpu_medidor);
                uint64_t total = 0;
                for (uint64_t p : tr_cpu_parte) total += p;
                UtilityFunctions::print("[CARGA] coste  cpu_us_por_frame=", (double)total / n,
                                        "  gpu_ventana_ms=", tr_gpu_n ? tr_gpu_raiz / tr_gpu_n : 0.0,
                                        "  gpu_lienzo_ms=", tr_gpu_n ? tr_gpu_vp / tr_gpu_n : 0.0, "  (intro, ", tr_gpu_n, " frames)");
                UtilityFunctions::print("[CARGA] cpu_us  proceso=", tr_cpu_parte[0] / n, "  fondo_sombras=", tr_cpu_parte[1] / n,
                                        "  logos_halos=", tr_cpu_parte[2] / n, "  texto_atlas=", tr_cpu_parte[3] / n,
                                        "  pantalla=", tr_cpu_parte[4] / n, "  frente=", tr_cpu_parte[5] / n,
                                        "  (de proceso, sonidos=", tr_cpu_sonido / n, "; medir, aparte=", tr_cpu_medidor / n, ")");
            }
        }
    }

public:
    uint64_t dm_id = 0;   // el DataModel, para mirar su ReplicatedFirst

    void _process(double delta) override {
        if (gl_carga_quitada()) {
            queue_free();
            return;
        }
        GLCargaCrono crono(tr_cpu_parte[0]);
        tr_frames_cpu++;
        t_real += delta;
        const bool traza = gl_carga_traza();
        const uint64_t ahora = Time::get_singleton()->get_ticks_usec();
        if (tr_antes && fase >= 1) {
            const double ms = (double)(ahora - tr_antes) / 1000.0;
            tr_frames++;
            tr_suma += ms;
            tr_peor = MAX(tr_peor, ms);
            if (ms > 1000.0 / 30.0) {
                tr_lentos++;
                if (traza)
                    UtilityFunctions::print("[CARGA] frame lento ", ms, " ms  t_real=", t_real, "  t=", t, "  fase=", fase,
                                            "  golpes=", golpes, "  letras=", letras, "  t_rotura=", t_rotura);
            }
        }
        tr_antes = ahora;
        if (!rf_mirado) {
            rf_mirado = true;
            rf_con_algo = _replicated_first_con_algo();
        }
        const Vector2i ws = get_tree()->get_root()->get_size();
        if (ws.x > 0 && ws.y > 0 && ws != tam && fase < 2) _preparar(ws);
        f = Vector2(pantalla->get_size().x / (float)MAX(1, tam.x), pantalla->get_size().y / (float)MAX(1, tam.y));
        fluidos = delta < 0.05 ? fluidos + 1 : 0;
        const double d = MIN(delta, 1.0 / 30.0);
        _medir_progreso();
        const bool auto_rf = rf_con_algo && t_real >= GLC_AUTO_RF;
        const int fase_antes = fase;

        if (fase == 0 && (fluidos >= 3 || t_real > 3.0)) fase = 1;
        if (fase >= 1) t += d;
        if (fase == 1) {
            // Golpe de cada logo: suena 0,233 s antes de tocar; al tocar, humo.
            while (golpes < 3 && t >= toque(golpes) - 0.23333) {
                _sonar(0, -8.565f, 1.94f);
                golpes++;
            }
            while (ondas < 3 && t >= toque(ondas)) _humo(ondas++);
            _escribir();
            if ((listo_texto && t >= t_texto_listo + GLC_ROMPER_TRAS) || auto_rf) _romper();
        } else if (fase >= 2) {
            const double antes = t_rotura;
            t_rotura += d;
            // Los logos saltan a los 0,22 s, con su golpe.
            if (antes < 0.22 && t_rotura >= 0.22) _sonar(3, -4.0f, 0.8f * _azar(1.0f / 1.05f, 1.05f));
            if (fase == 2 && t_rotura >= GLC_BAJAR_TRAS) {
                fase = 3;
                fondo->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
            }
            if (fase == 3 && t_rotura >= MAX(GLC_BAJAR_TRAS + GLC_BAJAR_DUR, 0.22 + GLC_PIEZA_VIDA) && particulas.is_empty()) {
                queue_free();
                return;
            }
        }
        _mover_particulas(d);

        // Los relojes de los shaders (uniformes globales, los mismos para todos):
        // es todo lo que la animacion pide cada frame.
        _relojes();

        if (traza) {
            // Lo que cuesta medir no cuenta como proceso (se apunta aparte).
            GLCargaCrono medidor(tr_cpu_medidor);
            if (fase == 1) {
                RenderingServer* rs = RenderingServer::get_singleton();
                tr_gpu_raiz += rs->viewport_get_measured_render_time_gpu(get_viewport()->get_viewport_rid());
                tr_gpu_vp += rs->viewport_get_measured_render_time_gpu(vp->get_viewport_rid());
                tr_gpu_n++;
            }
            if (fase != fase_antes)
                UtilityFunctions::print("[CARGA] fase ", fase_antes, " -> ", fase, "  t_real=", t_real, "  t=", t, "  prog=", prog,
                                        "  letras=", letras, "  frame_ms=", delta * 1000.0, "  rf_con_algo=", rf_con_algo);
        }
        // Solo se vuelve a dibujar lo que cambia: el fondo mientras se funde o
        // baja, las sombras al tocar cada logo o cambiar de fase, y el humo y
        // las estrellas en cada paso suyo. Lo demas lo mueven los shaders.
        const Color c_fondo = _color_fondo();
        if (redibujar_todo || fase >= 3 || c_fondo != fondo_dibujado) {
            fondo->queue_redraw();
            fondo_dibujado = c_fondo;
        }
        if (redibujar_todo || fase * 4 + ondas != firma_atras) {
            atras->queue_redraw();
            firma_atras = fase * 4 + ondas;
        }
        if (redibujar_todo || part_cambio) frente->queue_redraw();
        part_cambio = false;
        redibujar_todo = false;
    }
};

#include "gl_carga_capas.h"   // lo que dibuja cada capa

// La crea el Workspace al entrar en juego (una vez por partida). La capa va a la
// raiz de la ventana en diferido: el arbol aun se esta montando.
inline void gl_carga_defecto_crear(Node* workspace) {
    static bool hecha = false;
    if (hecha || !workspace || gl_carga_quitada()) return;
    hecha = true;
    if (Engine::get_singleton()->is_editor_hint()) return;
    if (DisplayServer::get_singleton()->get_name() == "headless") return;
    if (OS::get_singleton()->get_environment("GL_CARGA_DEFECTO") == "0") return;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    static const String k_ajuste("godotluau/carga/pantalla_por_defecto");
    if (ps->has_setting(k_ajuste) && !(bool)ps->get_setting(k_ajuste)) return;
    SceneTree* st = workspace->get_tree();
    if (!st || !st->get_root()) return;
    GLCargaDefecto* c = memnew(GLCargaDefecto);
    c->set_name("GLCargaDefecto");
    if (Node* dm = workspace->get_parent()) c->dm_id = (uint64_t)dm->get_instance_id();
    gl_carga_defecto_id() = (uint64_t)c->get_instance_id();
    st->get_root()->call_deferred("add_child", c);
}

#endif // GL_CARGA_DEFECTO_H
