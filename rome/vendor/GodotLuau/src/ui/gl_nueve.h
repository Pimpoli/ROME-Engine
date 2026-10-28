#ifndef GL_NUEVE_H
#define GL_NUEVE_H

// ════════════════════════════════════════════════════════════════════
//  ScaleType.Slice de Roblox (9-slice) para ImageLabel e ImageButton.
//  SliceCenter es el rectangulo central EN PIXELES de la imagen: las esquinas
//  se pintan a su tamaño (por SliceScale), los bordes se estiran en un eje y
//  el centro en los dos. Antes Slice caia en Stretch: los paneles y botones
//  redondeados de casi todas las UIs salian con las esquinas deformadas.
//  NinePatchRect de Godot no vale: no tiene escala de los bordes.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/canvas_item.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/rect2.hpp>

using namespace godot;

inline void gl_dibujar_nueve(CanvasItem* ci, const Ref<Texture2D>& tex, const Rect2& dst,
                             const Rect2& centro, float escala, const Color& tinte) {
    if (tex.is_null()) return;
    const Vector2 ts = tex->get_size();
    if (ts.x <= 0.0f || ts.y <= 0.0f || dst.size.x <= 0.0f || dst.size.y <= 0.0f) return;
    // Sin SliceCenter (todo a cero) Roblox estira la imagen entera.
    if (centro.position == Vector2() && centro.size == Vector2()) {
        ci->draw_texture_rect(tex, dst, false, tinte);
        return;
    }
    const float x0 = Math::clamp(centro.position.x, 0.0f, ts.x);
    const float y0 = Math::clamp(centro.position.y, 0.0f, ts.y);
    const float x1 = Math::clamp(centro.position.x + centro.size.x, x0, ts.x);
    const float y1 = Math::clamp(centro.position.y + centro.size.y, y0, ts.y);
    const float s = escala > 0.0f ? escala : 1.0f;
    float l = x0 * s, t = y0 * s, r = (ts.x - x1) * s, b = (ts.y - y1) * s;
    // Si los bordes no caben en la caja, se encogen en proporcion.
    if (l + r > dst.size.x && l + r > 0.0f) { const float k = dst.size.x / (l + r); l *= k; r *= k; }
    if (t + b > dst.size.y && t + b > 0.0f) { const float k = dst.size.y / (t + b); t *= k; b *= k; }
    const float sx[4] = { 0.0f, x0, x1, ts.x };
    const float sy[4] = { 0.0f, y0, y1, ts.y };
    const float dx[4] = { dst.position.x, dst.position.x + l, dst.position.x + dst.size.x - r, dst.position.x + dst.size.x };
    const float dy[4] = { dst.position.y, dst.position.y + t, dst.position.y + dst.size.y - b, dst.position.y + dst.size.y };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            const Rect2 orig(sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]);
            const Rect2 dest(dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]);
            if (orig.size.x <= 0.0f || orig.size.y <= 0.0f || dest.size.x <= 0.0f || dest.size.y <= 0.0f) continue;
            ci->draw_texture_rect_region(tex, dest, orig, tinte);
        }
    }
}

#endif // GL_NUEVE_H
