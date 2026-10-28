#ifndef GL_RICHTEXT_H
#define GL_RICHTEXT_H

// ════════════════════════════════════════════════════════════════════
//  RichText de Roblox (TextLabel / TextButton con RichText = true).
//  Antes se quitaban las etiquetas y el texto salia plano. Ahora se traducen a
//  BBCode y lo dibuja un RichTextLabel INTERNO encima, con la fuente, el
//  tamaño, el color, el contorno y la alineacion del propio control. El Label
//  conserva el texto plano (para medidas, TextScaled y los carteles 3D) pero
//  no lo pinta: visible_characters = 0.
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/control.hpp>
#include "gl_cast.h"
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/font_variation.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include "gl_fuentes.h"   // el size de <font> es alto de linea (gl_tam_godot_de)

using namespace godot;

// "#FF7800" o "rgb(255,120,0)" -> Color. `ok` = se entendio.
inline Color gl_rich_color(const String& s, bool& ok) {
    ok = false;
    const String v = s.strip_edges().to_lower();
    if (v.begins_with("#") && (v.length() == 7 || v.length() == 4)) {
        ok = true;
        return Color::html(v);
    }
    if (v.begins_with("rgb(") && v.ends_with(")")) {
        const PackedStringArray p = v.substr(4, v.length() - 5).split(",");
        if (p.size() == 3) {
            ok = true;
            return Color(p[0].strip_edges().to_float() / 255.0f, p[1].strip_edges().to_float() / 255.0f,
                         p[2].strip_edges().to_float() / 255.0f);
        }
    }
    return Color();
}

// Los atributos de una etiqueta: nombre="valor" o nombre='valor'.
inline void gl_rich_atributos(const String& tag, Dictionary& out) {
    int i = 0;
    const int n = tag.length();
    while (i < n) {
        while (i < n && tag[i] == ' ') i++;
        int ini = i;
        while (i < n && tag[i] != '=' && tag[i] != ' ') i++;
        const String nombre = tag.substr(ini, i - ini).strip_edges().to_lower();
        while (i < n && (tag[i] == ' ' || tag[i] == '=')) i++;
        if (i >= n) break;
        const char32_t comilla = tag[i];
        if (comilla != '"' && comilla != '\'') { i++; continue; }
        i++;
        ini = i;
        while (i < n && tag[i] != comilla) i++;
        if (!nombre.is_empty()) out[nombre] = tag.substr(ini, i - ini);
        i++;
    }
}

inline String gl_rich_entidades(const String& s) {
    return s.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"")
            .replace("&apos;", "'").replace("&amp;", "&");
}

// Texto de Roblox -> BBCode de Godot. `base` es el color del texto (para
// <font transparency="..."> sin color).
// escala_tam: font_size de Godot por cada punto de TextSize de Roblox (el size de
// <font> es alto de LINEA, ver gl_tam_godot_de); 1 = sin convertir (el chat).
inline String gl_rich_a_bbcode(const String& src, const Color& base, float escala_tam = 1.0f) {
    struct Abierta { String nombre; String cierre; };
    LocalVector<Abierta> pila;
    String out;
    int mayus = 0;
    const int n = src.length();
    int i = 0;
    auto texto = [&](const String& t) {
        String x = gl_rich_entidades(t);
        if (mayus > 0) x = x.to_upper();
        // Con un marcador en medio: reemplazar "[" por "[lb]" y despues "]"
        // por "[rb]" rompia el propio "[lb]".
        out += x.replace("[", String::chr(1)).replace("]", "[rb]").replace(String::chr(1), "[lb]");
    };
    int desde = 0;
    while (i < n) {
        if (src[i] != '<') { i++; continue; }
        // Comentario <!-- ... -->
        if (src.substr(i, 4) == "<!--") {
            texto(src.substr(desde, i - desde));
            const int fin = src.find("-->", i + 4);
            i = fin < 0 ? n : fin + 3;
            desde = i;
            continue;
        }
        const int fin = src.find(">", i + 1);
        if (fin < 0) break;   // un '<' suelto: es texto
        texto(src.substr(desde, i - desde));
        String tag = src.substr(i + 1, fin - i - 1).strip_edges();
        i = fin + 1;
        desde = i;
        const bool cierra = tag.begins_with("/");
        if (cierra) tag = tag.substr(1).strip_edges();
        bool sola = tag.ends_with("/");
        if (sola) tag = tag.substr(0, tag.length() - 1).strip_edges();
        const int sp = tag.find(" ");
        const String nombre = (sp < 0 ? tag : tag.substr(0, sp)).to_lower();
        if (nombre == "br") { out += "\n"; continue; }
        if (cierra) {
            // Cierra hasta la ultima abierta con ese nombre.
            for (int k = (int)pila.size() - 1; k >= 0; k--) {
                if (pila[k].nombre != nombre) continue;
                for (int j = (int)pila.size() - 1; j >= k; j--) {
                    out += pila[j].cierre;
                    if (pila[j].nombre == "uppercase" || pila[j].nombre == "uc" ||
                        pila[j].nombre == "smallcaps" || pila[j].nombre == "sc") mayus--;
                }
                pila.resize(k);
                break;
            }
            continue;
        }
        Dictionary at;
        if (sp >= 0) gl_rich_atributos(tag.substr(sp + 1), at);
        String abre, cierre;
        if (nombre == "b" || nombre == "i" || nombre == "u" || nombre == "s") {
            abre = "[" + nombre + "]"; cierre = "[/" + nombre + "]";
        } else if (nombre == "uppercase" || nombre == "uc" || nombre == "smallcaps" || nombre == "sc") {
            mayus++;
        } else if (nombre == "font") {
            bool ok = false;
            Color c = at.has("color") ? gl_rich_color(at["color"], ok) : base;
            const bool trans = at.has("transparency");
            if (trans) c.a = Math::clamp(1.0f - (float)String(at["transparency"]).to_float(), 0.0f, 1.0f);
            if (ok || trans) { abre += "[color=#" + c.to_html(trans) + "]"; cierre = "[/color]" + cierre; }
            if (at.has("size")) {
                abre += "[font_size=" + String::num_int64(MAX(1, (int)Math::round(String(at["size"]).to_int() * escala_tam))) + "]";
                cierre = "[/font_size]" + cierre;
            }
            if (at.has("weight")) {
                const String w = String(at["weight"]).to_lower();
                if (w == "bold" || w == "semibold" || w == "heavy" || w.to_int() >= 600) {
                    abre += "[b]"; cierre = "[/b]" + cierre;
                }
            }
        } else if (nombre == "stroke") {
            bool ok = false;
            Color c = at.has("color") ? gl_rich_color(at["color"], ok) : Color(0, 0, 0);
            if (at.has("transparency")) c.a = Math::clamp(1.0f - (float)String(at["transparency"]).to_float(), 0.0f, 1.0f);
            const int grosor = at.has("thickness") ? MAX(1, String(at["thickness"]).to_int()) : 1;
            // x4: el contorno de Godot sale outline_size/4 px por fuera (ver UIStroke).
            abre = "[outline_size=" + String::num_int64(grosor * 4) + "][outline_color=#" + c.to_html(true) + "]";
            cierre = "[/outline_color][/outline_size]";
        } else if (nombre == "mark") {
            bool ok = false;
            Color c = at.has("color") ? gl_rich_color(at["color"], ok) : Color(1, 1, 0);
            if (at.has("transparency")) c.a = Math::clamp(1.0f - (float)String(at["transparency"]).to_float(), 0.0f, 1.0f);
            abre = "[bgcolor=#" + c.to_html(true) + "]";
            cierre = "[/bgcolor]";
        } else {
            continue;   // etiqueta desconocida: Roblox tampoco la pinta
        }
        if (sola) continue;
        out += abre;
        pila.push_back({ nombre, cierre });
    }
    texto(src.substr(desde, n - desde));
    for (int k = (int)pila.size() - 1; k >= 0; k--) out += pila[k].cierre;
    return out;
}

inline bool gl_rich_tiene_etiquetas(const String& s) {
    return s.find("<") >= 0 && s.find(">") >= 0;
}

// El texto que el control pone en su propio texto. Con RichText y etiquetas, un
// Button no puede ocultar el suyo: se queda vacio (lo dibuja la capa de encima).
inline String gl_rich_texto_base(Control* host, bool rich, const String& raw) {
    if (!rich) return raw;
    if (gl_cast<Button>(host) && gl_rich_tiene_etiquetas(raw)) return String();
    return gl_rich_entidades(_gl_strip_rich_tags(raw));
}

// Pone, actualiza o quita la capa RichTextLabel del control.
inline void gl_rich_sincronizar(Control* host, bool rich, const String& raw) {
    static const StringName k_capa("__gl_rich__");
    RichTextLabel* capa = gl_cast<RichTextLabel>(host->get_node_or_null(NodePath(String(k_capa))));
    Label* lbl = gl_cast<Label>(host);
    const bool quiere = rich && gl_rich_tiene_etiquetas(raw);
    if (!quiere) {
        if (capa) { host->remove_child(capa); capa->queue_free(); }
        if (lbl && host->has_meta("_gl_rich_activo")) {
            host->remove_meta("_gl_rich_activo");
            const int g = host->has_meta("_gl_max_graphemes") ? (int)host->get_meta("_gl_max_graphemes") : -1;
            lbl->set_visible_characters(g);
        }
        return;
    }
    if (!capa) {
        capa = memnew(RichTextLabel);
        capa->set_name(String(k_capa));
        capa->set_meta("_gl_interno", true);
        capa->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        capa->set_focus_mode(Control::FOCUS_NONE);
        capa->set_use_bbcode(true);
        capa->set_scroll_active(false);
        capa->set_fit_content(true);
        capa->set_clip_contents(false);
        host->add_child(capa, false, Node::INTERNAL_MODE_BACK);
    }
    host->set_meta("_gl_rich_activo", true);

    // Estilo del control.
    const Ref<Font> f = host->get_theme_font("font");
    const int tam = host->get_theme_font_size("font_size");
    const Color col = host->get_theme_color("font_color");
    if (f.is_valid()) {
        Ref<FontVariation> negrita, cursiva, ambas;
        negrita.instantiate(); negrita->set_base_font(f); negrita->set_variation_embolden(0.8f);
        cursiva.instantiate(); cursiva->set_base_font(f); cursiva->set_variation_transform(Transform2D(Vector2(1, 0), Vector2(0.2f, 1), Vector2()));
        ambas.instantiate(); ambas->set_base_font(f); ambas->set_variation_embolden(0.8f);
        ambas->set_variation_transform(Transform2D(Vector2(1, 0), Vector2(0.2f, 1), Vector2()));
        capa->add_theme_font_override("normal_font", f);
        capa->add_theme_font_override("bold_font", negrita);
        capa->add_theme_font_override("italics_font", cursiva);
        capa->add_theme_font_override("bold_italics_font", ambas);
    }
    for (const char* k : { "normal_font_size", "bold_font_size", "italics_font_size", "bold_italics_font_size" })
        capa->add_theme_font_size_override(k, tam);
    capa->add_theme_color_override("default_color", col);
    capa->add_theme_constant_override("outline_size", host->get_theme_constant("outline_size"));
    capa->add_theme_color_override("font_outline_color", host->get_theme_color("font_outline_color"));

    // Alineacion horizontal (las etiquetas de parrafo de BBCode) y ajuste.
    HorizontalAlignment h = HORIZONTAL_ALIGNMENT_LEFT;
    VerticalAlignment v = VERTICAL_ALIGNMENT_CENTER;
    if (lbl) {
        h = lbl->get_horizontal_alignment();
        v = lbl->get_vertical_alignment();
        capa->set_autowrap_mode(lbl->get_autowrap_mode());
        lbl->set_visible_characters(0);   // el Label mide, la capa pinta
        const int g = host->has_meta("_gl_max_graphemes") ? (int)host->get_meta("_gl_max_graphemes") : -1;
        capa->set_visible_characters(g);
    } else if (Button* b = gl_cast<Button>(host)) {
        h = b->get_text_alignment();
        capa->set_autowrap_mode(TextServer::AUTOWRAP_OFF);
    }
    String familia;
    int peso = 400, estilo = 0;
    gl_fuente_de(host, familia, peso, estilo);
    String bb = gl_rich_a_bbcode(raw, col, gl_linea_roblox(1.0f, gl_familia_legacy(familia)) / gl_fuente_razon(f));
    if (h == HORIZONTAL_ALIGNMENT_CENTER) bb = "[center]" + bb + "[/center]";
    else if (h == HORIZONTAL_ALIGNMENT_RIGHT) bb = "[right]" + bb + "[/right]";
    static const StringName k_bb("_gl_bb");
    if (!capa->has_meta(k_bb) || String(capa->get_meta(k_bb)) != bb) {
        capa->set_meta(k_bb, bb);
        capa->set_text(bb);
    }

    // Vertical: la capa mide lo que ocupa y se coloca dentro de la caja.
    const Vector2 caja = host->get_size();
    capa->set_size(Vector2(caja.x, 0.0f));
    const float alto = (float)capa->get_content_height();
    float y = 0.0f;
    if (v == VERTICAL_ALIGNMENT_CENTER) y = (caja.y - alto) * 0.5f;
    else if (v == VERTICAL_ALIGNMENT_BOTTOM) y = caja.y - alto;
    capa->set_position(Vector2(0.0f, y));
    capa->set_size(Vector2(caja.x, alto));
}

#endif // GL_RICHTEXT_H
