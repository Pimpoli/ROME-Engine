#ifndef GL_CARGA_CAPAS_H
#define GL_CARGA_CAPAS_H

// Lo que dibuja cada capa de la pantalla de carga por defecto
// (gl_carga_defecto.h, que lo incluye al final de la clase). Halos, logos,
// letras y trozos se dibujan una vez, en reposo: los mueven sus shaders
// (gl_carga_shaders.h).

// Fondo: lleno hasta que baja; entonces, la imagen del borde ondulado
// (splash_transition del original, 824x704 en x=-101, y=-108, repetida a lo
// ancho) y lleno por debajo.
inline void GLCargaFondo::_draw() {
    GLCargaDefecto* d = dueno;
    if (!d) return;
    GLCargaCrono crono(d->tr_cpu_parte[1]);
    const Vector2 L = get_size();
    const Color c = d->_color_fondo();
    if (d->fase < 3 || d->tex_borde.is_null()) {
        if (d->fase < 3) draw_rect(Rect2(Vector2(), L), c);
        return;
    }
    const float y_v = -108.0f - d->_subida() + d->_bajada();
    const float x_min = -d->o.x / d->u, x_max = (d->tam.x - d->o.x) / d->u;
    const int k0 = (int)Math::floor((x_min + 101.0f) / 824.0f), k1 = (int)Math::floor((x_max + 101.0f) / 824.0f);
    for (int k = k0; k <= k1; k++) {
        const Vector2 p = d->P(Vector2(-101.0f + k * 824.0f, y_v)) * d->f;
        draw_texture_rect(d->tex_borde, Rect2(p, Vector2(824.0f, 704.0f) * d->u * d->f), false, c);
    }
    const float y_fin = d->P(Vector2(0.0f, y_v + 704.0f)).y * d->f.y - 1.0f;
    if (y_fin < L.y) draw_rect(Rect2(0.0f, y_fin, L.x, L.y - y_fin), c);
}

// Sombras: el logo en negro en su sitio, desde que toca hasta que se rompe.
inline void GLCargaAtras::_draw() {
    GLCargaDefecto* d = dueno;
    if (!d) return;
    GLCargaCrono crono(d->tr_cpu_parte[1]);
    if (d->fase != 1) return;
    for (int k = 0; k < 3; k++) {
        if (d->t < d->toque(k)) continue;
        const Vector2 c = d->P(d->centro[k]) * d->f;
        const Vector2 m = d->medida[k] * d->u * d->f;
        if (k == 1) {
            if (d->linea_aspa.is_null()) continue;
            draw_set_transform(gl_carga_aspa_origen(d->P(d->centro[1]), (float)d->aspa_px, 1.0f) * d->f, 0.0f, d->f);
            d->linea_aspa->draw_outline(get_canvas_item(), Vector2(), d->aspa_anillo_n, Color(0, 0, 0));
            d->linea_aspa->draw(get_canvas_item(), Vector2(), Color(0, 0, 0));
            draw_set_transform(Vector2(), 0.0f, Vector2(1, 1));
        } else {
            const Ref<ImageTexture>& tx = k == 0 ? d->tex_rm : d->tex_gl;
            if (tx.is_valid()) draw_texture_rect(tx, Rect2(c - m * 0.5f, m), false, Color(0, 0, 0));
        }
    }
}

// Halo, anillo y destello de cada logo: un cuadro por logo, en reposo y en px
// de la ventana, con su indice en el rojo (0, 0,5, 1). El tamaño es el del
// original (2,9 veces el logo); el shader lo recorta a lo que pinta.
inline void GLCargaHalos::_draw() {
    GLCargaDefecto* d = dueno;
    if (!d) return;
    GLCargaCrono crono(d->tr_cpu_parte[2]);
    draw_set_transform(Vector2(), 0.0f, d->f);
    for (int k = 0; k < 3; k++) {
        const float lado = 2.893f * (d->medida[k].x + d->medida[k].y) * 0.5f * d->u;
        draw_rect(Rect2(d->P(d->centro[k]) - Vector2(lado, lado) * 0.5f, Vector2(lado, lado)), Color(k * 0.5f, 1, 1, 1));
    }
    draw_set_transform(Vector2(), 0.0f, Vector2(1, 1));
}

// Los logos (en px de la ventana, dentro del SubViewport), en reposo y con su
// indice en el rojo: el shader los hace caer. La × va con las letras.
inline void GLCargaDibujo::_draw() {
    GLCargaDefecto* d = dueno;
    if (!d) return;
    GLCargaCrono crono(d->tr_cpu_parte[2]);
    for (int k = 0; k < 3; k += 2) {
        const Ref<ImageTexture>& tx = k == 0 ? d->tex_rm : d->tex_gl;
        if (tx.is_null()) continue;
        const Vector2 m = d->medida[k] * d->u;
        draw_texture_rect(tx, Rect2(d->P(d->centro[k]) - m * 0.5f, m), false, Color(k == 0 ? 0.0f : 1.0f, 1, 1, 1));
    }
}

// El atlas: cada letra (y la ×) en su celda, sumando cada capa en su canal
// (rojo la letra; verde la letra y su borde; azul la letra y su anillo).
inline void GLCargaAtlas::_draw() {
    GLCargaDefecto* d = dueno;
    if (!d) return;
    GLCargaCrono crono(d->tr_cpu_parte[3]);
    const RID ri = get_canvas_item();
    for (const GLCargaCelda& c : d->celdas) {
        const bool aspa = c.letra < 0;
        const Ref<TextLine>& l = aspa ? d->linea_aspa : d->linea_letra[c.letra];
        if (l.is_null()) continue;
        const int m = aspa ? d->margen_aspa : d->margen_letra;
        const Vector2 p = c.en_atlas.position + Vector2(m, m);
        l->draw(ri, p, Color(1, 0, 0));
        l->draw_outline(ri, p, aspa ? d->aspa_borde_n : d->borde_n, Color(0, 1, 0));
        l->draw(ri, p, Color(0, 1, 0));
        l->draw_outline(ri, p, aspa ? d->aspa_anillo_n : d->anillo_n, Color(0, 0, 1));
        l->draw(ri, p, Color(0, 0, 1));
    }
}

// Una pasada de las letras y la × (sombras, anillos o letras): el MultiMesh
// entero con el atlas. Las mueve el shader.
inline void GLCargaTexto::_draw() {
    GLCargaDefecto* d = dueno;
    if (!d) return;
    GLCargaCrono crono(d->tr_cpu_parte[3]);
    if (d->mm_glifos.is_null() || !d->vp_atlas) return;
    draw_multimesh(d->mm_glifos, d->vp_atlas->get_texture());
}

// La composicion sobre la pantalla o, al romperse, sus trozos (una malla que
// mueve el shader). Mientras no se rompe tambien se pinta un triangulo sin
// area con la misma malla: el shader de los trozos ya esta compilado cuando
// hace falta.
inline void GLCargaPantalla::_draw() {
    GLCargaDefecto* d = dueno;
    if (!d) return;
    GLCargaCrono crono(d->tr_cpu_parte[4]);
    if (!d->vp) return;
    const Ref<Texture2D> tx = d->vp->get_texture();
    if (tx.is_null()) return;
    if (d->fase < 2 || d->malla_trozos.is_null()) {
        draw_texture_rect(tx, Rect2(Vector2(), get_size()), false);
        if (d->malla_vacia.is_valid()) draw_mesh(d->malla_vacia, tx);
        return;
    }
    draw_set_transform(Vector2(), 0.0f, d->f);
    draw_mesh(d->malla_trozos, tx);
    draw_set_transform(Vector2(), 0.0f, Vector2(1, 1));
}

// Humo (10 fotogramas de 64x64, se apaga a lo largo de su vida) y estrellas
// (crecen y menguan). Solo se vuelve a dibujar en cada paso suyo (15 por s).
inline void GLCargaFrente::_draw() {
    GLCargaDefecto* d = dueno;
    if (!d) return;
    GLCargaCrono crono(d->tr_cpu_parte[5]);
    for (const GLCargaParticula& p : d->particulas) {
        const float x = p.edad / p.vida;
        draw_set_transform(p.pos * d->f, p.ang, d->f);
        if (!p.estrella) {
            if (d->tex_humo.is_null()) continue;
            const int cuadro = CLAMP((int)(x * 10.0f), 0, 9);
            const float lado = 64.0f * p.escala * d->u;
            draw_texture_rect_region(d->tex_humo, Rect2(Vector2(-lado, -lado) * 0.5f, Vector2(lado, lado)),
                                     Rect2(cuadro * 64.0f, 0.0f, 64.0f, 64.0f), Color(1, 1, 1, 1.0f - x));
        } else {
            if (d->tex_estrella.is_null()) continue;
            const Vector2 m = Vector2(22.0f, 20.0f) * p.escala * 4.0f * x * (1.0f - x) * d->u;
            draw_texture_rect(d->tex_estrella, Rect2(-m * 0.5f, m), false);
        }
    }
    draw_set_transform(Vector2(), 0.0f, Vector2(1, 1));
}

#endif // GL_CARGA_CAPAS_H
