#ifndef ROBLOX_PART_MATERIAL_H
#define ROBLOX_PART_MATERIAL_H

// El material de una pieza: el compartido por aspecto, el dedicado de las que
// llevan textura propia, el shader de studs y lo que pone cada Material de
// Roblox. Salio de roblox_part.h (que lo incluye al final, tras la clase)
// para que no pase de 2.000 lineas; el codigo es el mismo.

#include "roblox_part.h"   // la clase entera: este archivo se incluye al final de ella

using namespace godot;

inline void RobloxPart::_apply_material_visual() {
    if (!_mi_vivo()) return;
    var_tex = Ref<Texture2D>();
    var_studs = 10.0f;
    if (!part_texture.is_valid()) gl_variante_de(this, roblox_material, var_tex, var_studs);
    if (part_texture.is_valid() || malla_con_colores) {
        // Textura propia (ej. Baseplate): material DEDICADO de esta part
        if (!material.is_valid() || material_shared) {
            material.instantiate();
            material_shared = false;
        }
        _configure_material();
    } else {
        // Sin textura: material COMPARTIDO por clave visual
        const uint64_t key = _material_key();
        HashMap<uint64_t, Ref<StandardMaterial3D>>& cache = gl_shared_materials();
        Ref<StandardMaterial3D>* found = cache.getptr(key);
        if (found) {
            material = *found;
        } else {
            material.instantiate();
            material_shared = false;   // recien creado: configurarlo
            _configure_material();
            cache[key] = material;
            gl_purgar_cache(cache, GL_TOPE_CACHE_MATERIALES);
        }
        material_shared = true;
    }
    // ── Studs / Inlet ─────────────────────────────────────────────
    // Si alguna cara lleva relieve, la pieza pasa a un ShaderMaterial que
    // dibuja los tetones en el pixel (ver gl_superficies.h). Solo en
    // bloques y solo si no trae textura propia: en lo demas sigue el
    // StandardMaterial3D compartido, que es mas barato.
    // Luz propia (gl_luz_propia.h): la pieza se dibuja con un shader que hace
    // la cuenta de la luz a mano. No en un ViewportFrame: ahi alumbran sus
    // propias luces y su propio ambiente.
    const bool propia = gl_luz_propia_activa() && !en_vf;
    Ref<ShaderMaterial> sup;
    if (roblox_shape == 0 && !part_texture.is_valid() && !malla_propia.is_valid()) {
        // MaterialVariant del juego (gl_variantes.h): su ColorMap en vez
        // de la textura del Material, proyectado por cara desde la esquina.
        sup = gls_material_superficie(material, _material_key(), roblox_material,
                                      top_surface, bottom_surface, front_surface,
                                      back_surface, left_surface, right_surface,
                                      var_tex, var_studs, propia);
    }
    const bool antes = usa_superficies;
    usa_superficies = sup.is_valid();
    Ref<Material> final;
    if (usa_superficies) final = sup;
    else if (propia) final = gl_luz_de(material, material_shared ? nullptr : &material_luz);
    else final = material;
    mesh_instance->set_material_override(final);
    // Entrar o salir del shader de studs cambia la malla (unitaria o del
    // tamaño real), asi que hay que volver a pedirla.
    if (antes != usa_superficies) _refresh_shape(roblox_shape);
}

// Aplica TODO el aspecto al material actual (solo materiales no compartidos
// o recien creados para el cache).
inline void RobloxPart::_configure_material() {
    if (!material.is_valid()) return;
    // Reset to neutral
    material->set_metallic(0.0f);
    material->set_roughness(0.55f);
    // Difuso Lambert, como Roblox: su sol da Brightness/pi * N.L justo
    // (medido). El Burley por defecto de Godot aclaraba las caras que el
    // sol toca de refilon (+10 niveles en una pared con N.L = 0.27).
    material->set_diffuse_mode(BaseMaterial3D::DIFFUSE_LAMBERT);
    // FIX 1: Use set_feature to disable emission in Godot 4
    //// CORRECCIÓN 1: Usar set_feature para deshabilitar la emisión en Godot 4
    material->set_feature(BaseMaterial3D::FEATURE_EMISSION, false);
    // Opaco por defecto: solo las piezas realmente translucidas usan alpha.
    // Antes TODA pieza usaba TRANSPARENCY_ALPHA, lo que rompe el orden de
    // dibujado y desactiva efectos (SSAO/sombras) en piezas opacas.
    material->set_transparency(BaseMaterial3D::TRANSPARENCY_DISABLED);
    // Y sin dibujo de material: lo pone abajo el que le toque.
    material->set_texture(StandardMaterial3D::TEXTURE_ALBEDO, Ref<Texture2D>());
    material->set_texture(StandardMaterial3D::TEXTURE_NORMAL, Ref<Texture2D>());
    material->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, false);
    material->set_flag(BaseMaterial3D::FLAG_UV1_USE_TRIPLANAR, false);
    material->set_uv1_scale(Vector3(1, 1, 1));

    switch (roblox_material) {
        case 0:  material->set_roughness(0.55f); break; // Plastic (Roblox: ligero brillo, no super mate)
        // SmoothPlastic: 0.60, MEDIDO (2026-10-08, carta de Play y fotos de
        // Build a Mountain). Con 0.30 el Fresnel de Godot a ras doblaba el
        // reflejo del cielo de Roblox: cesped y sombras teñidos de cian (cesped
        // a 70 studs B 123 contra 102; sombra B 130 contra 75) y la hierba
        // lejana lavada. Bajar el especular no vale: quita reflejo de frente y
        // deja el de canto. Con 0.60: cesped (138,214,100) contra (134,218,102)
        // y la carta de 8,5 a 4,7 niveles de error medio.
        case 1:  material->set_roughness(0.60f); break; // SmoothPlastic
        case 2:  // Neon: luz propia que no depende de la escena
            // MEDIDO con el halo de las farolas de Build a Mountain (BloomEffect
            // 0.35/24/1.6): Roblox pinta el neon a ~5 veces su color (en lineal
            // gamma 2, como el color de pieza) y el sol no lo cambia. Con x2 y
            // el albedo iluminado no llegaba al umbral del bloom: sin halo.
            material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
            material->set_emission(gl_color_pieza(Color(color.r, color.g, color.b)));
            material->set_emission_energy_multiplier(5.5f);
            material->set_roughness(0.5f);
            break;
        case 3:  material->set_roughness(0.85f); break; // Wood
        case 4:  material->set_roughness(0.80f); break; // WoodPlanks
        case 5:  material->set_roughness(0.20f); break; // Marble
        case 6:  material->set_roughness(0.90f); break; // Slate
        case 7:  material->set_roughness(0.95f); break; // Concrete
        case 8:  material->set_roughness(0.90f); break; // Granite
        case 9:  material->set_roughness(0.95f); break; // Brick
        case 10: // Metal
            material->set_metallic(0.9f);
            material->set_roughness(0.35f);
            break;
        case 11: // CorrodedMetal
            material->set_metallic(0.4f);
            material->set_roughness(0.90f);
            break;
        // Metal PURO (metallic 1) sin nada que reflejar sale NEGRO: en PBR
        // un metal no tiene color propio, solo el reflejo. En Roblox la
        // chapa y el papel de aluminio son grises brillantes, no espejos.
        case 12: // DiamondPlate
            material->set_metallic(0.60f);
            material->set_roughness(0.40f);
            break;
        case 13: // Foil
            material->set_metallic(0.85f);
            material->set_roughness(0.20f);
            break;
        case 14: material->set_roughness(0.95f); break; // Grass
        case 15: // Ice
            material->set_roughness(0.05f);
            material->set_metallic(0.05f);
            // FIX 3: Replace TRANSPARENCY_GLASS with TRANSPARENCY_ALPHA
            //// CORRECCIÓN 3: Reemplazar TRANSPARENCY_GLASS por TRANSPARENCY_ALPHA
            material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
            break;
        case 16: // Glass
            material->set_roughness(0.0f);
            material->set_metallic(0.0f);
            // FIX 4: Replace TRANSPARENCY_GLASS with TRANSPARENCY_ALPHA
            //// CORRECCIÓN 4: Reemplazar TRANSPARENCY_GLASS por TRANSPARENCY_ALPHA
            material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
            break;
        case 17: material->set_roughness(1.00f); break; // Sand
        case 18: material->set_roughness(0.95f); break; // Fabric
        case 19: material->set_roughness(0.95f); break; // Rock
        case 20: material->set_roughness(0.90f); break; // Snow
        case 21: material->set_roughness(0.95f); break; // Cobblestone
        case 22: material->set_roughness(0.85f); break; // Pebble
        case 23: material->set_roughness(1.00f); break; // Ground
        case 24: material->set_roughness(0.85f); break; // Mud (algo húmedo)
        case 25: material->set_roughness(0.95f); break; // Sandstone
        case 26: material->set_roughness(0.90f); break; // Basalt
        case 27: // CrackedLava — roca con brasas incandescentes
            material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
            material->set_emission(Color(1.0f, 0.35f, 0.05f));
            material->set_emission_energy_multiplier(1.6f);
            material->set_roughness(0.80f);
            break;
        case 28: // Glacier — hielo compacto ligeramente translúcido
            material->set_roughness(0.10f);
            material->set_metallic(0.05f);
            break;
        case 29: material->set_roughness(0.98f); break; // LeafyGrass
        case 30: material->set_roughness(0.85f); break; // Salt
        case 31: material->set_roughness(0.90f); break; // Limestone
        case 32: material->set_roughness(0.92f); break; // Pavement
        case 33: material->set_roughness(0.95f); break; // Asphalt
        case 34: // ForceField — campo de energía translúcido y brillante
            material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
            material->set_emission(Color(0.3f, 0.7f, 1.0f));
            material->set_emission_energy_multiplier(1.4f);
            material->set_roughness(0.20f);
            material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
            break;
        case 35: // Water — agua translúcida
            material->set_roughness(0.05f);
            material->set_metallic(0.10f);
            material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
            break;
        case 36: // Air — invisible (bloque vacío, como en Terrain)
            material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
            break;
        default: material->set_roughness(0.55f); break;
    }
    // Specular base ~0.5 (brillo tipo plastico de Roblox); reflectance lo
    // sube. Antes era set_specular(reflectance) => 0 por defecto => TODO se
    // veia mate y plano, sin reflejos especulares. Este es el cambio que
    // mas acerca las piezas al look de Roblox.
    material->set_specular(Math::clamp(0.5f + reflectance * 0.5f, 0.0f, 1.0f));
    // Reflectance 1 en Roblox es un espejo del cielo: tambien la alisa (si no,
    // con la rugosidad de SmoothPlastic el reflejo saldria emborronado).
    if (reflectance > 0.001f)
        material->set_roughness(Math::lerp(material->get_roughness(), 0.05f, Math::clamp(reflectance, 0.0f, 1.0f)));
    // Activar alpha solo si la pieza es translucida (las de vidrio/hielo ya
    // lo ponen en su case). Con "transparencia por pixeles" se usa ALPHA_HASH:
    // la GPU descarta pixeles en vez de mezclar, asi la pieza se dibuja como
    // si fuera opaca (sin ordenar ni mezclar) y cuesta mucho menos.
    if (transparency > 0.001f) {
        material->set_transparency(_gl_use_pixel_transparency()
            ? BaseMaterial3D::TRANSPARENCY_ALPHA_HASH
            : BaseMaterial3D::TRANSPARENCY_ALPHA);
    }
    _update_albedo();
    // Materiales con transparencia inherente: conservan el Color de la pieza
    // pero fuerzan un alfa característico si el usuario no definió Transparency.
    if (roblox_material == 36) {                                  // Air: invisible
        material->set_albedo(gl_color_pieza(Color(color.r, color.g, color.b, 0.0f)));
    } else if (roblox_material == 35 && transparency < 0.01f) {   // Water
        material->set_albedo(gl_color_pieza(Color(color.r, color.g, color.b, 0.55f)));
    } else if (roblox_material == 34 && transparency < 0.01f) {   // ForceField
        material->set_albedo(gl_color_pieza(Color(color.r, color.g, color.b, 0.35f)));
    } else if (roblox_material == 2) {                            // Neon: solo su luz
        material->set_albedo(Color(0, 0, 0, material->get_albedo().a));
    }
    // Dibujo del Material de Roblox (madera, ladrillo, cesped...). Hasta
    // ahora Material solo cambiaba la rugosidad: un muro de ladrillo se
    // veia como plastico naranja. Se salta si la pieza trae su propia
    // textura (Baseplate, Decal), que manda sobre la del material.
    if (!part_texture.is_valid()) {
        if (var_tex.is_valid()) {
            // MaterialVariant en una forma que no es caja (cilindro, cuña,
            // malla propia): su ColorMap x Color en triplanar, una baldosa
            // cada StudsPerTile. Las cajas van por el shader de caras.
            const float e = 1.0f / Math::max(var_studs, 0.05f);
            material->set_texture(StandardMaterial3D::TEXTURE_ALBEDO, var_tex);
            material->set_uv1_scale(Vector3(e, e, e));
            material->set_flag(BaseMaterial3D::FLAG_UV1_USE_TRIPLANAR, true);
            material->set_uv1_triplanar_blend_sharpness(8.0f);   // poca mezcla: pixel art nitido
            // Anisotropo: vista de canto, sin el la cara coge un mipmap tan
            // pequeño que el pixel art se deshace en una mancha (en Roblox no).
            material->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC);
        } else {
            gls_vestir_material(material, roblox_material);
        }
    }
    _apply_part_texture();   // la textura persistida sobrevive re-aplicados
}

#endif // ROBLOX_PART_MATERIAL_H
