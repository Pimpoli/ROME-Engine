#ifndef GL_EDITABLE_IMAGEN_H
#define GL_EDITABLE_IMAGEN_H

// ════════════════════════════════════════════════════════════════════
//  EditableImage de Roblox: una imagen RGBA8 que los scripts pintan en
//  tiempo de ejecucion y que se ve EN VIVO donde se use
//  (ImageLabel.ImageContent, MeshPart.TextureContent, Decal...).
//  https://create.roblox.com/docs/reference/engine/classes/EditableImage
//
//  Los pixeles viven en un vector propio: es lo que leen y escriben los
//  scripts, sin cruzar a Godot. Quien la muestra recibe UNA ImageTexture
//  compartida; pintar solo marca la imagen como sucia y la subida a la GPU
//  se hace una vez por frame como mucho (gl_editables_vaciar, en diferido).
//  Asi un WritePixelsBuffer de 1024x1024 es una copia de memoria y no
//  recrea ninguna textura.
//
//  Coordenadas como en Roblox: (0, 0) es el pixel de arriba a la izquierda y
//  (Size.X - 1, Size.Y - 1) el de abajo a la derecha. Los metodos de dibujo
//  hablan de TRANSPARENCIA (0 = opaco); el buffer, de ALFA (255 = opaco).
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <vector>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <algorithm>

using namespace godot;

// Enum.ImageCombineType (valores de Roblox).
enum GLCombinar {
    GL_COMB_SOURCE_OVER = 1,
    GL_COMB_OVERWRITE   = 2,
    GL_COMB_ADD         = 3,
    GL_COMB_MULTIPLY    = 4,
    GL_COMB_ALPHA_BLEND = 5,
    GL_COMB_NORMAL_MAP  = 6,
    GL_COMB_SUBTRACT    = 7,
};

// Lado maximo de Roblox (y el que se da por defecto al crear sin Size).
inline constexpr int GL_EI_LADO_MAX = 1024;
inline constexpr int GL_EI_LADO_DEFECTO = 512;

// Cuantas hay vivas y cuanta memoria de pixeles usan: las sondas comprueban
// con esto que Destroy y el recolector no dejan nada atras.
inline int64_t& gl_ei_vivas() { static int64_t n = 0; return n; }
inline int64_t& gl_ei_bytes() { static int64_t n = 0; return n; }

// Las subidas pendientes las hace el gestor (gl_editables.h); aqui solo se
// avisa de que esta imagen tiene cambios.
class GLEditableImage;
inline void gl_editables_apuntar_imagen(GLEditableImage* ei);

inline uint8_t gl_ei_byte(float v) {
    if (!(v > 0.0f)) return 0;          // tambien NaN
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

// Un pixel de origen (color en 0..1 y alfa) combinado con el de destino d
// (RGBA8 en memoria), segun Enum.ImageCombineType. `cobertura` es la parte
// del pixel que tapa la figura (bordes suavizados): el resultado se mezcla
// con lo que habia en esa proporcion.
inline void gl_ei_combinar(uint8_t* d, float sr, float sg, float sb, float sa, int modo, float cobertura = 1.0f) {
    if (cobertura <= 0.0f) return;
    const float k = 1.0f / 255.0f;
    const float dr = d[0] * k, dg = d[1] * k, db = d[2] * k, da = d[3] * k;
    float r, g, b, a;
    switch (modo) {
        case GL_COMB_OVERWRITE:
            r = sr; g = sg; b = sb; a = sa;
            break;
        case GL_COMB_ADD:
            // El color de origen pesa por su alfa; el alfa se suma tal cual.
            r = dr + sr * sa; g = dg + sg * sa; b = db + sb * sa; a = da + sa;
            break;
        case GL_COMB_SUBTRACT:
            r = dr - sr * sa; g = dg - sg * sa; b = db - sb * sa; a = da - sa;
            break;
        case GL_COMB_MULTIPLY:
            r = dr * sr; g = dg * sg; b = db * sb; a = da * sa;
            break;
        case GL_COMB_ALPHA_BLEND:
        case GL_COMB_NORMAL_MAP:
            // El color de destino cuenta aunque su alfa sea 0 (la diferencia
            // con BlendSourceOver).
            r = sr * sa + dr * (1.0f - sa);
            g = sg * sa + dg * (1.0f - sa);
            b = sb * sa + db * (1.0f - sa);
            a = sa + da * (1.0f - sa);
            if (modo == GL_COMB_NORMAL_MAP) {
                // Se vuelve a dejar el RGB como normal de espacio tangente.
                float nx = r * 2.0f - 1.0f, ny = g * 2.0f - 1.0f, nz = b * 2.0f - 1.0f;
                const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (l > 1e-6f) { nx /= l; ny /= l; nz /= l; }
                r = nx * 0.5f + 0.5f; g = ny * 0.5f + 0.5f; b = nz * 0.5f + 0.5f;
            }
            break;
        case GL_COMB_SOURCE_OVER:
        default: {
            // "Source over" de Porter-Duff con pixeles sin premultiplicar.
            a = sa + da * (1.0f - sa);
            if (a <= 1e-6f) { r = g = b = 0.0f; a = 0.0f; break; }
            const float w = da * (1.0f - sa);
            r = (sr * sa + dr * w) / a;
            g = (sg * sa + dg * w) / a;
            b = (sb * sa + db * w) / a;
            break;
        }
    }
    if (cobertura < 1.0f) {
        r = dr + (r - dr) * cobertura;
        g = dg + (g - dg) * cobertura;
        b = db + (b - db) * cobertura;
        a = da + (a - da) * cobertura;
    }
    d[0] = gl_ei_byte(r); d[1] = gl_ei_byte(g); d[2] = gl_ei_byte(b); d[3] = gl_ei_byte(a);
}

class GLEditableImage : public RefCounted {
    GDCLASS(GLEditableImage, RefCounted);

protected:
    // Para las sondas de GDScript: el tamano, una copia de los pixeles y la
    // textura que reciben los que la muestran.
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("gl_tamano"), &GLEditableImage::gl_tamano);
        ClassDB::bind_method(D_METHOD("gl_imagen"), &GLEditableImage::gl_imagen);
        ClassDB::bind_method(D_METHOD("gl_textura"), &GLEditableImage::gl_textura);
        ClassDB::bind_method(D_METHOD("gl_destruida"), &GLEditableImage::gl_destruida);
    }

public:
    int ancho = 0, alto = 0;
    std::vector<uint8_t> px;     // RGBA8, ancho * alto * 4
    Ref<Image> img;              // la que se sube (solo si alguien la muestra)
    Ref<ImageTexture> tex;       // UNA para todos los que la muestran
    bool sucia = false;          // hay cambios sin subir
    bool apuntada = false;       // ya esta en la lista del gestor
    bool destruida = false;

    GLEditableImage() { gl_ei_vivas()++; }
    ~GLEditableImage() {
        gl_ei_vivas()--;
        gl_ei_bytes() -= (int64_t)px.size();
    }

    void crear(int w, int h) {
        gl_ei_bytes() -= (int64_t)px.size();
        ancho = w; alto = h;
        px.assign((size_t)w * (size_t)h * 4, 0);   // transparente
        gl_ei_bytes() += (int64_t)px.size();
    }
    // Desde una imagen de Godot (CreateEditableImageAsync).
    void crear_desde(const Ref<Image>& fuente) {
        Ref<Image> c = fuente->duplicate();
        if (c->is_compressed()) c->decompress();
        if (c->has_mipmaps()) c->clear_mipmaps();
        if (c->get_format() != Image::FORMAT_RGBA8) c->convert(Image::FORMAT_RGBA8);
        crear(c->get_width(), c->get_height());
        const PackedByteArray datos = c->get_data();
        if ((size_t)datos.size() == px.size()) memcpy(px.data(), datos.ptr(), px.size());
    }

    Vector2i gl_tamano() const { return Vector2i(ancho, alto); }
    bool gl_destruida() const { return destruida; }
    Ref<Image> gl_imagen() const {
        if (destruida || px.empty()) return Ref<Image>();
        PackedByteArray datos;
        datos.resize((int64_t)px.size());
        memcpy(datos.ptrw(), px.data(), px.size());
        return Image::create_from_data(ancho, alto, false, Image::FORMAT_RGBA8, datos);
    }

    // La textura compartida. Se crea al primer uso (una imagen que solo se
    // usa de pincel nunca llega a la GPU).
    Ref<Texture2D> gl_textura() {
        if (tex.is_valid()) return tex;
        if (destruida || px.empty()) return Ref<Texture2D>();
        img = Image::create_empty(ancho, alto, false, Image::FORMAT_RGBA8);
        memcpy(img->ptrw(), px.data(), px.size());
        tex = ImageTexture::create_from_image(img);
        sucia = false;
        return tex;
    }

    // Lo llama el gestor una vez por frame.
    void subir() {
        if (!sucia) return;
        sucia = false;
        if (destruida || tex.is_null() || img.is_null()) return;
        memcpy(img->ptrw(), px.data(), px.size());
        tex->update(img);
    }

    void marcar() {
        if (tex.is_null() || destruida) return;   // nadie la muestra: nada que subir
        sucia = true;
        if (!apuntada) gl_editables_apuntar_imagen(this);
    }

    // Destroy: los pixeles se sueltan YA. Quien la mostraba se queda con un
    // pixel transparente (la textura es la misma, asi que no hay que buscarlos).
    void destruir() {
        if (destruida) return;
        destruida = true;
        gl_ei_bytes() -= (int64_t)px.size();
        std::vector<uint8_t>().swap(px);
        if (tex.is_valid()) {
            Ref<Image> vacia = Image::create_empty(1, 1, false, Image::FORMAT_RGBA8);
            vacia->fill(Color(0, 0, 0, 0));
            tex->set_image(vacia);
        }
        img.unref();
    }

    uint8_t* pixel(int x, int y) { return px.data() + ((size_t)y * (size_t)ancho + (size_t)x) * 4; }
    bool dentro(int x, int y) const { return x >= 0 && y >= 0 && x < ancho && y < alto; }

    // ── Buffer ──────────────────────────────────────────────────────
    void escribir(int x, int y, int w, int h, const uint8_t* src) {
        const size_t fila = (size_t)w * 4;
        for (int j = 0; j < h; j++) memcpy(pixel(x, y + j), src + fila * (size_t)j, fila);
        marcar();
    }
    void leer(int x, int y, int w, int h, uint8_t* dst) {
        const size_t fila = (size_t)w * 4;
        for (int j = 0; j < h; j++) memcpy(dst + fila * (size_t)j, pixel(x, y + j), fila);
    }

    // ── Figuras ─────────────────────────────────────────────────────
    void rectangulo(int x, int y, int w, int h, const Color& c, float transp, int modo) {
        const int x1 = std::min(x + w, ancho), y1 = std::min(y + h, alto);
        const float a = 1.0f - std::clamp(transp, 0.0f, 1.0f);
        for (int j = std::max(y, 0); j < y1; j++)
            for (int i = std::max(x, 0); i < x1; i++)
                gl_ei_combinar(pixel(i, j), c.r, c.g, c.b, a, modo);
        marcar();
    }

    void circulo(float cx, float cy, float radio, const Color& c, float transp, int modo, bool suave) {
        if (radio < 0.0f) return;
        const float a = 1.0f - std::clamp(transp, 0.0f, 1.0f);
        const int y0 = std::max((int)std::floor(cy - radio - 1.0f), 0);
        const int y1 = std::min((int)std::ceil(cy + radio + 1.0f), alto - 1);
        const int x0 = std::max((int)std::floor(cx - radio - 1.0f), 0);
        const int x1 = std::min((int)std::ceil(cx + radio + 1.0f), ancho - 1);
        for (int j = y0; j <= y1; j++) {
            for (int i = x0; i <= x1; i++) {
                const float dx = (float)i - cx, dy = (float)j - cy;
                const float d = std::sqrt(dx * dx + dy * dy);
                float cob;
                if (suave) cob = std::clamp(radio + 0.5f - d, 0.0f, 1.0f);
                else cob = d <= radio ? 1.0f : 0.0f;
                if (cob > 0.0f) gl_ei_combinar(pixel(i, j), c.r, c.g, c.b, a, modo, cob);
            }
        }
        marcar();
    }

    // Linea de un pixel. Sin suavizado, Bresenham entre los extremos
    // redondeados (los dos incluidos); con suavizado, Xiaolin Wu.
    void linea(float ax, float ay, float bx, float by, const Color& c, float transp, int modo, bool suave) {
        const float a = 1.0f - std::clamp(transp, 0.0f, 1.0f);
        auto poner = [&](int i, int j, float cob) {
            if (cob > 0.0f && dentro(i, j)) gl_ei_combinar(pixel(i, j), c.r, c.g, c.b, a, modo, std::min(cob, 1.0f));
        };
        if (!suave) {
            int x0 = (int)std::lround(ax), y0 = (int)std::lround(ay);
            const int x1 = (int)std::lround(bx), y1 = (int)std::lround(by);
            const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
            const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
            int err = dx + dy;
            for (int guarda = 0; guarda < 1 << 16; guarda++) {
                poner(x0, y0, 1.0f);
                if (x0 == x1 && y0 == y1) break;
                const int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
            marcar();
            return;
        }
        const bool empinada = std::fabs(by - ay) > std::fabs(bx - ax);
        if (empinada) { std::swap(ax, ay); std::swap(bx, by); }
        if (ax > bx) { std::swap(ax, bx); std::swap(ay, by); }
        const float ddx = bx - ax, ddy = by - ay;
        const float pend = std::fabs(ddx) < 1e-6f ? 1.0f : ddy / ddx;
        auto plot = [&](int u, int v, float cob) { if (empinada) poner(v, u, cob); else poner(u, v, cob); };
        auto fpart = [](float v) { return v - std::floor(v); };
        // Extremos: la parte del pixel que cubre la linea en cada punta.
        int xs = (int)std::lround(ax);
        float ys = ay + pend * ((float)xs - ax);
        float hueco = 1.0f - fpart(ax + 0.5f);
        plot(xs, (int)std::floor(ys), (1.0f - fpart(ys)) * hueco);
        plot(xs, (int)std::floor(ys) + 1, fpart(ys) * hueco);
        float inter = ys + pend;
        const int xe = (int)std::lround(bx);
        const float ye = by + pend * ((float)xe - bx);
        hueco = fpart(bx + 0.5f);
        if (xe != xs) {
            plot(xe, (int)std::floor(ye), (1.0f - fpart(ye)) * hueco);
            plot(xe, (int)std::floor(ye) + 1, fpart(ye) * hueco);
        }
        for (int x = xs + 1; x < xe; x++) {
            plot(x, (int)std::floor(inter), 1.0f - fpart(inter));
            plot(x, (int)std::floor(inter) + 1, fpart(inter));
            inter += pend;
        }
        marcar();
    }

    // Triangulo relleno (DrawTriangle): los pixeles cuyo centro cae dentro,
    // mezclados con BlendSourceOver.
    void triangulo(Vector2 p0, Vector2 p1, Vector2 p2, const Color& c, float transp) {
        const float a = 1.0f - std::clamp(transp, 0.0f, 1.0f);
        const float area = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
        if (std::fabs(area) < 1e-9f) return;
        const int x0 = std::max((int)std::floor(std::min({ p0.x, p1.x, p2.x })), 0);
        const int x1 = std::min((int)std::ceil(std::max({ p0.x, p1.x, p2.x })), ancho - 1);
        const int y0 = std::max((int)std::floor(std::min({ p0.y, p1.y, p2.y })), 0);
        const int y1 = std::min((int)std::ceil(std::max({ p0.y, p1.y, p2.y })), alto - 1);
        auto lado = [](const Vector2& a1, const Vector2& b1, float x, float y) {
            return (b1.x - a1.x) * (y - a1.y) - (b1.y - a1.y) * (x - a1.x);
        };
        for (int j = y0; j <= y1; j++) {
            for (int i = x0; i <= x1; i++) {
                const float x = (float)i, y = (float)j;
                float e0 = lado(p0, p1, x, y), e1 = lado(p1, p2, x, y), e2 = lado(p2, p0, x, y);
                if (area < 0) { e0 = -e0; e1 = -e1; e2 = -e2; }
                if (e0 >= 0 && e1 >= 0 && e2 >= 0)
                    gl_ei_combinar(pixel(i, j), c.r, c.g, c.b, a, GL_COMB_SOURCE_OVER);
            }
        }
        marcar();
    }

    // ── Otra imagen encima ──────────────────────────────────────────
    void imagen(int x, int y, GLEditableImage* src, int modo) {
        if (!src || src->px.empty()) return;
        // Pintarse a si misma: se copia antes el origen.
        std::vector<uint8_t> copia;
        const uint8_t* s = src->px.data();
        if (src == this) { copia = px; s = copia.data(); }
        const float k = 1.0f / 255.0f;
        for (int j = 0; j < src->alto; j++) {
            const int dj = y + j;
            if (dj < 0 || dj >= alto) continue;
            for (int i = 0; i < src->ancho; i++) {
                const int di = x + i;
                if (di < 0 || di >= ancho) continue;
                const uint8_t* sp = s + ((size_t)j * (size_t)src->ancho + (size_t)i) * 4;
                gl_ei_combinar(pixel(di, dj), sp[0] * k, sp[1] * k, sp[2] * k, sp[3] * k, modo);
            }
        }
        marcar();
    }

    // DrawImageTransformed: el pivote del origen va a `pos`, escalado y girado
    // (grados, sentido horario como en la UI de Roblox). Cada pixel de destino
    // se busca hacia atras en el origen: bilineal (ResamplerMode.Default) o el
    // mas cercano (Pixelated). Lo que cae fuera del origen no se pinta.
    void transformada(Vector2 pos, Vector2 escala, float grados, GLEditableImage* src, int modo,
                      bool pixelado, Vector2 pivote) {
        if (!src || src->px.empty()) return;
        if (std::fabs(escala.x) < 1e-6f || std::fabs(escala.y) < 1e-6f) return;
        std::vector<uint8_t> copia;
        const uint8_t* s = src->px.data();
        if (src == this) { copia = px; s = copia.data(); }
        const int sw = src->ancho, sh = src->alto;
        const float rad = grados * 0.017453292519943295f;
        const float co = std::cos(rad), si = std::sin(rad);
        // Caja del origen transformado, para recorrer solo lo necesario.
        const Vector2 esq[4] = { Vector2(0, 0), Vector2((float)sw, 0), Vector2(0, (float)sh), Vector2((float)sw, (float)sh) };
        float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
        for (const Vector2& e : esq) {
            const float lx = (e.x - pivote.x) * escala.x, ly = (e.y - pivote.y) * escala.y;
            const float wx = pos.x + co * lx - si * ly, wy = pos.y + si * lx + co * ly;
            mnx = std::min(mnx, wx); mxx = std::max(mxx, wx);
            mny = std::min(mny, wy); mxy = std::max(mxy, wy);
        }
        const int x0 = std::max((int)std::floor(mnx) - 1, 0), x1 = std::min((int)std::ceil(mxx) + 1, ancho - 1);
        const int y0 = std::max((int)std::floor(mny) - 1, 0), y1 = std::min((int)std::ceil(mxy) + 1, alto - 1);
        const float k = 1.0f / 255.0f;
        auto texel = [&](int i, int j) {
            i = std::clamp(i, 0, sw - 1); j = std::clamp(j, 0, sh - 1);
            return s + ((size_t)j * (size_t)sw + (size_t)i) * 4;
        };
        for (int j = y0; j <= y1; j++) {
            for (int i = x0; i <= x1; i++) {
                const float dx = (float)i + 0.5f - pos.x, dy = (float)j + 0.5f - pos.y;
                // Giro inverso y luego la escala.
                const float u = pivote.x + (co * dx + si * dy) / escala.x;
                const float v = pivote.y + (-si * dx + co * dy) / escala.y;
                if (u < 0.0f || v < 0.0f || u >= (float)sw || v >= (float)sh) continue;
                float r, g, b, a;
                if (pixelado) {
                    const uint8_t* t = texel((int)u, (int)v);
                    r = t[0] * k; g = t[1] * k; b = t[2] * k; a = t[3] * k;
                } else {
                    const float fu = u - 0.5f, fv = v - 0.5f;
                    const int iu = (int)std::floor(fu), iv = (int)std::floor(fv);
                    const float tu = fu - (float)iu, tv = fv - (float)iv;
                    const uint8_t* t00 = texel(iu, iv);
                    const uint8_t* t10 = texel(iu + 1, iv);
                    const uint8_t* t01 = texel(iu, iv + 1);
                    const uint8_t* t11 = texel(iu + 1, iv + 1);
                    float o[4];
                    for (int q = 0; q < 4; q++) {
                        const float arriba = t00[q] + (t10[q] - t00[q]) * tu;
                        const float abajo = t01[q] + (t11[q] - t01[q]) * tu;
                        o[q] = (arriba + (abajo - arriba) * tv) * k;
                    }
                    r = o[0]; g = o[1]; b = o[2]; a = o[3];
                }
                gl_ei_combinar(pixel(i, j), r, g, b, a, modo);
            }
        }
        marcar();
    }
};

#endif // GL_EDITABLE_IMAGEN_H
