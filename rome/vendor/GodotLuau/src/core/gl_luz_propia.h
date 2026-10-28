#ifndef GL_LUZ_PROPIA_H
#define GL_LUZ_PROPIA_H

// ════════════════════════════════════════════════════════════════════
//  Luz propia de las piezas (Forward+, fuera del editor).
//
//  MEDIDO (UHD 770, Build a Mountain a 1457x711, calidad 7): la pasada opaca
//  costaba 3,15 ms y 2,4 de ellos eran el sombreado con luz de Godot. Con todo
//  el 3D en un material comun sin luz bajaba a 0,52 ms (143 -> 203 FPS); con
//  un material comun CON luz, 2,90. En una escena minima (un plano que llena
//  la pantalla) el shader con luz de Godot cuesta 2,55 ms por millon de
//  pixeles en esta GPU, y la misma cuenta hecha a mano en un shader
//  "unshaded", 0,81.
//
//  Asi: las piezas van con un shader unshaded que hace la cuenta de Godot
//  (Lambert + GGX + reflejo del cielo) con lo que MIDE la sonda (gl_sonda.h):
//  el ambiente en 6 direcciones, el sol, el cielo para el reflejo y hasta 8
//  luces puntuales o focos. Todo va en uniforms GLOBALES: un cambio de luz no
//  toca ningun material. La niebla la sigue poniendo Godot (tambien a lo
//  unshaded) y las sombras del sol, las propias (gl_sombras.h).
//  Lo demas (avatares, terreno, efectos) sigue con la luz de Godot.
//  GL_LUZ_GODOT=1 vuelve a la luz de Godot (A/B).
// ════════════════════════════════════════════════════════════════════

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include "gl_servicios.h"   // gl_purgar_cache

using namespace godot;

// Luces puntuales y focos de Roblox vivos en el arbol (roblox_luces.h), con
// si alumbran (Enabled y en el Workspace; lo apunta la propia luz al cambiar):
// de aqui elige la sonda las que pasa al shader sin subir por el arbol.
inline HashMap<uint64_t, bool>& gl_luces_registro() {
    static HashMap<uint64_t, bool> s;
    return s;
}

// Variantes del shader.
enum : uint32_t {
    GL_LUZ_ALFA   = 1u << 0,   // mezcla (cola de transparentes)
    GL_LUZ_HASH   = 1u << 1,   // transparencia por borrado de pixeles
    GL_LUZ_TEX_UV = 1u << 2,   // textura con las UV de la malla (Baseplate, TextureID)
    GL_LUZ_TEX_TRI = 1u << 3,  // textura en triplanar (Material de Roblox, variantes)
    GL_LUZ_NORMAL = 1u << 4,   // relieve del Material (triplanar)
    GL_LUZ_VCOLOR = 1u << 5,   // color por vertice (uniones con UsePartColor = false)
    GL_LUZ_CARAS  = 1u << 6,   // studs / variantes por cara (malla unitaria, gl_superficies.h)
};

// Para medir A/B en el mismo frame (Workspace._gl_luz_propia_ab): 0 la apaga.
inline int& gl_luz_propia_forzada() {
    static int v = -1;
    return v;
}

// Forward+ fuera del editor y con lectura asincrona de texturas (Godot 4.4+),
// que es como la sonda le pasa sus medidas a la CPU sin parar la GPU.
inline bool gl_luz_propia_activa() {
    if (gl_luz_propia_forzada() == 0) return false;
    static int v = -1;
    if (v < 0) {
        v = 0;
        RenderingServer* rs = RenderingServer::get_singleton();
        Engine* e = Engine::get_singleton();
        OS* os = OS::get_singleton();
        RenderingDevice* rd = rs ? rs->get_rendering_device() : nullptr;
        if (rd && e && os && !e->is_editor_hint() && os->get_environment("GL_LUZ_GODOT") != "1" &&
            rs->has_method("get_current_rendering_method") &&
            String(rs->call("get_current_rendering_method")) == "forward_plus" &&
            rd->has_method("texture_get_data_async"))
            v = 1;
    }
    return v == 1;
}

// ── Los uniforms globales ─────────────────────────────────────────────
// gl_amb_*: ambiente (pieza blanca, Lambert) con la normal en +x -x +y -y +z -z.
// gl_ibl_*: el cielo para el reflejo (su nivel mas borroso por la energia del
// ambiente), en las mismas 6 direcciones.
// gl_sol_col: rgb = sol en una pieza blanca con N.L = 1; w = su especular.
// gl_sol_dir: hacia el sol.
// gl_lpN: posicion, 1/alcance. gl_lcN: color*energia (lineal), tamaño (envoltura).
// gl_ldN: direccion del foco, coseno del corte (-2 = omni).
// gl_laN: caida (decay), exponente del borde del foco, especular, 0.
static const int GL_LUZ_MAX = 8;
static const char* const GL_LUZ_EJES[6] = {"px", "nx", "py", "ny", "pz", "nz"};

// Un pixel blanco: el valor de un sampler global antes de tener el suyo.
inline Ref<ImageTexture>& gl_luz_blanca_ref() {
    static Ref<ImageTexture> t;
    return t;
}
inline Ref<ImageTexture> gl_luz_textura_blanca() {
    Ref<ImageTexture>& t = gl_luz_blanca_ref();
    if (t.is_null()) {
        Ref<Image> img = Image::create_empty(1, 1, false, Image::FORMAT_RGBAF);
        img->fill(Color(1, 1, 1, 1));
        t = ImageTexture::create_from_image(img);
    }
    return t;
}

inline void gl_luz_globales() {
    static bool hecho = false;
    if (hecho) return;
    hecho = true;
    RenderingServer* rs = RenderingServer::get_singleton();
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (!rs || !ps) return;
    // global_shader_parameter_get_list() es solo para el editor (fuera, avisa
    // de que es lento): los del proyecto estan en shader_globals/.
    auto anadir = [&](const String& n, RenderingServer::GlobalShaderParameterType t, const Variant& v) {
        if (!ps->has_setting("shader_globals/" + n)) rs->global_shader_parameter_add(StringName(n), t, v);
    };
    for (int i = 0; i < 6; i++) {
        anadir(String("gl_amb_") + GL_LUZ_EJES[i], RenderingServer::GLOBAL_VAR_TYPE_VEC3, Vector3(0.3f, 0.3f, 0.3f));
        anadir(String("gl_ibl_") + GL_LUZ_EJES[i], RenderingServer::GLOBAL_VAR_TYPE_VEC3, Vector3(0.3f, 0.3f, 0.3f));
    }
    anadir("gl_sol_col", RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4(1, 1, 1, 1));
    anadir("gl_sol_dir", RenderingServer::GLOBAL_VAR_TYPE_VEC3, Vector3(0.3f, 0.8f, 0.5f).normalized());
    anadir("gl_luz_n", RenderingServer::GLOBAL_VAR_TYPE_INT, 0);
    // La sombra del sol (gl_sombras.h): la mascara a media resolucion que se
    // calcula antes de la pasada opaca, y x = encendida, y = opacidad.
    anadir("gl_sombra", RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4());
    anadir("gl_sombra_masc", RenderingServer::GLOBAL_VAR_TYPE_SAMPLER2D, gl_luz_textura_blanca());
    for (int i = 0; i < GL_LUZ_MAX; i++) {
        const String k = String::num_int64(i);
        anadir("gl_lp" + k, RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4(0, -100000, 0, 1));
        anadir("gl_lc" + k, RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4());
        anadir("gl_ld" + k, RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4(0, 0, -1, -2));
        anadir("gl_la" + k, RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4(0, 1, 0, 0));
    }
}

// ── La cuenta de la luz (la de Godot 4.7, Forward+) ───────────────────
// Lambert del sol y las luces, ambiente por cubo de 6 direcciones, especular
// GGX (D y V de Godot, Fresnel de Schlick con su f90) y reflejo del cielo con
// la aproximacion del BRDF de entorno. Las luces puntuales: ventana de Godot
// (1 - (d/r)^4)^2, caida d^-decay, borde de foco y la envoltura de luz
// esferica (tamaño > 0), que es la que hace que alumbren casi por volumen como
// las de Roblox (roblox_luces.h).
static const char* GL_LUZ_COMUN = R"(
global uniform vec3 gl_amb_px; global uniform vec3 gl_amb_nx; global uniform vec3 gl_amb_py;
global uniform vec3 gl_amb_ny; global uniform vec3 gl_amb_pz; global uniform vec3 gl_amb_nz;
global uniform vec3 gl_ibl_px; global uniform vec3 gl_ibl_nx; global uniform vec3 gl_ibl_py;
global uniform vec3 gl_ibl_ny; global uniform vec3 gl_ibl_pz; global uniform vec3 gl_ibl_nz;
global uniform vec4 gl_sol_col;
global uniform vec3 gl_sol_dir;
global uniform int gl_luz_n;
global uniform vec4 gl_lp0; global uniform vec4 gl_lp1; global uniform vec4 gl_lp2; global uniform vec4 gl_lp3;
global uniform vec4 gl_lp4; global uniform vec4 gl_lp5; global uniform vec4 gl_lp6; global uniform vec4 gl_lp7;
global uniform vec4 gl_lc0; global uniform vec4 gl_lc1; global uniform vec4 gl_lc2; global uniform vec4 gl_lc3;
global uniform vec4 gl_lc4; global uniform vec4 gl_lc5; global uniform vec4 gl_lc6; global uniform vec4 gl_lc7;
global uniform vec4 gl_ld0; global uniform vec4 gl_ld1; global uniform vec4 gl_ld2; global uniform vec4 gl_ld3;
global uniform vec4 gl_ld4; global uniform vec4 gl_ld5; global uniform vec4 gl_ld6; global uniform vec4 gl_ld7;
global uniform vec4 gl_la0; global uniform vec4 gl_la1; global uniform vec4 gl_la2; global uniform vec4 gl_la3;
global uniform vec4 gl_la4; global uniform vec4 gl_la5; global uniform vec4 gl_la6; global uniform vec4 gl_la7;
global uniform vec4 gl_sombra;
global uniform sampler2D gl_sombra_masc;

// Cuanto sol le llega al pixel (1 = todo): la mascara de la sombra, a media
// resolucion, guarda (luz, distancia). Las 4 muestras que rodean al pixel se
// mezclan como un bilineal (sin escalones de 2 pixeles en los bordes) pero
// pesando cada una por lo que se parece su distancia a la del pixel, asi un
// borde no mezcla lo de delante con lo de detras. Si ninguna se parece (un
// cristal en el aire), sin sombra.
float gl_sol_vis(vec2 fc, float dist) {
	if (gl_sombra.x < 0.5) {
		return 1.0;
	}
	// gl_sombra.zw = el trozo de la textura que se usa.
	vec2 q = clamp(fc * 0.5, vec2(1.0), gl_sombra.zw - 1.0);
	vec2 uv = q / vec2(textureSize(gl_sombra_masc, 0));
	vec4 l = textureGather(gl_sombra_masc, uv, 0);   // (0,1) (1,1) (1,0) (0,0)
	if (min(min(l.x, l.y), min(l.z, l.w)) >= 0.999) {
		return 1.0;
	}
	vec4 d = abs(textureGather(gl_sombra_masc, uv, 1) - dist);
	vec2 f = fract(q - 0.5);
	vec4 wb = vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
	float tol = max(0.03 * dist, 0.6);
	vec4 wd = wb * clamp(1.0 - d / tol, 0.0, 1.0);
	float sw = wd.x + wd.y + wd.z + wd.w;
	if (sw < 0.0001) {
		return 1.0;
	}
	return 1.0 - gl_sombra.y * (1.0 - dot(l, wd) / sw);
}

vec3 gl_cubo_amb(vec3 n) {
	vec3 q = n * n;
	return q.x * (n.x > 0.0 ? gl_amb_px : gl_amb_nx) + q.y * (n.y > 0.0 ? gl_amb_py : gl_amb_ny)
		+ q.z * (n.z > 0.0 ? gl_amb_pz : gl_amb_nz);
}

vec3 gl_cubo_ibl(vec3 n) {
	vec3 q = n * n;
	return q.x * (n.x > 0.0 ? gl_ibl_px : gl_ibl_nx) + q.y * (n.y > 0.0 ? gl_ibl_py : gl_ibl_ny)
		+ q.z * (n.z > 0.0 ? gl_ibl_pz : gl_ibl_nz);
}

// N.L * D * V * F de Godot, con el PI de la luz ya cancelado con el de D.
vec3 gl_ggx(vec3 n, vec3 l, vec3 v, float nl, float nv, float a, vec3 f0, float f90) {
	vec3 h = normalize(l + v);
	float nh = clamp(dot(n, h), 0.0, 1.0);
	float lh = clamp(dot(l, h), 0.0, 1.0);
	float an = nh * a;
	float k = a / (1.0 - nh * nh + an * an);
	float g = clamp(0.5 / mix(2.0 * nl * nv, nl + nv, a), 0.0, 1.0);
	float m = 1.0 - lh;
	float m2 = m * m;
	return (nl * k * k * g) * (f0 + (f90 - f0) * (m2 * m2 * m));
}

void gl_punto(vec4 lp, vec4 lc, vec4 ld, vec4 la, vec3 n, vec3 pw, vec3 v, float nv, float a, vec3 f0,
		float f90, inout vec3 dif, inout vec3 esp) {
	vec3 d = lp.xyz - pw;
	float d2 = dot(d, d);
	float nd = d2 * lp.w * lp.w;
	if (nd >= 1.0) {
		return;
	}
	float dist = sqrt(d2);
	vec3 l = d / max(dist, 0.0001);
	float at = 1.0 - nd * nd;
	at *= at;
	if (la.x != 0.0) {
		at *= pow(max(dist, 0.0001), -la.x);
	}
	if (ld.w > -1.5) {
		float c = max(dot(-l, ld.xyz), ld.w);
		float borde = max(0.0001, (1.0 - c) / (1.0 - ld.w));
		at *= 1.0 - pow(borde, la.y);
	}
	float env = 0.0;
	if (lc.w > 0.0) {
		float t = lc.w / max(dist, 0.001);
		env = max(0.0, 1.0 - inversesqrt(1.0 + t * t));
	}
	float nl = min(env + dot(n, l), 1.0);
	if (nl <= 0.0) {
		return;
	}
	dif += lc.rgb * (nl * at);
	if (la.z > 0.0) {
		esp += lc.rgb * (at * la.z) * gl_ggx(n, l, v, nl, nv, a, f0, f90);
	}
}

// sv: cuanto sol le llega (gl_sol_vis): la sombra quita solo la luz del sol.
vec3 gl_iluminar(vec3 alb, vec3 n, vec3 pw, vec3 v, float rug, float met, float espec, float sv) {
	float nv = max(dot(n, v), 0.0001);
	vec3 f0 = mix(vec3(0.16 * espec * espec), alb, met);
	float f90 = clamp(dot(f0, vec3(16.5)), met, 1.0);
	float a = rug * rug;
	vec3 dif = gl_cubo_amb(n);
	vec3 esp = vec3(0.0);
	float nl = dot(n, gl_sol_dir);
	if (nl > 0.0) {
		dif += gl_sol_col.rgb * (nl * sv);
		if (gl_sol_col.w > 0.0) {
			esp += gl_sol_col.rgb * (gl_sol_col.w * sv) * gl_ggx(n, gl_sol_dir, v, min(nl, 1.0), nv, a, f0, f90);
		}
	}
	if (gl_luz_n > 0) {
		gl_punto(gl_lp0, gl_lc0, gl_ld0, gl_la0, n, pw, v, nv, a, f0, f90, dif, esp);
		if (gl_luz_n > 1) {
			gl_punto(gl_lp1, gl_lc1, gl_ld1, gl_la1, n, pw, v, nv, a, f0, f90, dif, esp);
			if (gl_luz_n > 2) {
				gl_punto(gl_lp2, gl_lc2, gl_ld2, gl_la2, n, pw, v, nv, a, f0, f90, dif, esp);
				if (gl_luz_n > 3) {
					gl_punto(gl_lp3, gl_lc3, gl_ld3, gl_la3, n, pw, v, nv, a, f0, f90, dif, esp);
					if (gl_luz_n > 4) {
						gl_punto(gl_lp4, gl_lc4, gl_ld4, gl_la4, n, pw, v, nv, a, f0, f90, dif, esp);
						if (gl_luz_n > 5) {
							gl_punto(gl_lp5, gl_lc5, gl_ld5, gl_la5, n, pw, v, nv, a, f0, f90, dif, esp);
							if (gl_luz_n > 6) {
								gl_punto(gl_lp6, gl_lc6, gl_ld6, gl_la6, n, pw, v, nv, a, f0, f90, dif, esp);
								if (gl_luz_n > 7) {
									gl_punto(gl_lp7, gl_lc7, gl_ld7, gl_la7, n, pw, v, nv, a, f0, f90, dif, esp);
								}
							}
						}
					}
				}
			}
		}
	}
	// Reflejo del cielo: la direccion se acerca a la normal con la rugosidad,
	// como en Godot, y el horizonte apaga lo que mira hacia dentro.
	vec3 r = normalize(mix(reflect(-v, n), n, a));
	float hor = min(1.0 + dot(r, n), 1.0);
	vec4 rr = rug * vec4(-1.0, -0.0275, -0.572, 0.022) + vec4(1.0, 0.0425, 1.04, -0.04);
	float a004 = min(rr.x * rr.x, exp2(-9.28 * clamp(dot(n, v), 0.0, 1.0))) * rr.x + rr.y;
	vec2 eb = vec2(-1.04, 1.04) * a004 + rr.zw;
	esp += gl_cubo_ibl(r) * (hor * hor) * (eb.x * f0 + eb.y * clamp(50.0 * f0.g, met, 1.0));
	return alb * dif * (1.0 - met) + esp;
}
)";

// Lo que es del material (los mismos nombres que el shader de caras de
// gl_superficies.h, asi el codigo que los pone vale para los dos).
static const char* GL_LUZ_MATERIAL = R"(
uniform vec4 gl_col : source_color = vec4(1.0);
uniform float gl_rug : hint_range(0.0, 1.0) = 0.55;
uniform float gl_met : hint_range(0.0, 1.0) = 0.0;
uniform float gl_esp : hint_range(0.0, 1.0) = 0.5;
uniform vec3 gl_emi : source_color = vec3(0.0);
uniform float gl_emi_f = 0.0;
varying vec3 v_nw;
varying vec3 v_pw;
)";

// El shader de una variante. Las texturas, la triplanar y el relieve hacen lo
// mismo que el StandardMaterial3D de Godot, salvo una cosa: la triplanar va en
// el espacio de la pieza CON su tamaño (VERTEX por la escala de la instancia),
// asi una pieza del agrupado estatico (malla unitaria escalada) repite la
// textura igual que la misma pieza suelta.
inline String gl_luz_codigo(uint32_t f) {
    const bool alfa = (f & (GL_LUZ_ALFA | GL_LUZ_HASH)) != 0;
    const bool hash = (f & GL_LUZ_HASH) != 0;
    String s = "shader_type spatial;\nrender_mode unshaded, blend_mix, depth_draw_opaque, cull_back;\n";
    s += GL_LUZ_COMUN;
    s += GL_LUZ_MATERIAL;
    if (f & GL_LUZ_CARAS) {
        // Studs, inlets y variantes por cara (ver gls_shader en gl_superficies.h).
        s += R"(
uniform vec3 gl_sa = vec3(0.0);
uniform vec3 gl_sb = vec3(0.0);
uniform sampler2D gl_tex : source_color, hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D gl_studs : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D gl_studs_n : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float gl_tex_esc = 0.0;
varying vec2 v_p;
varying flat vec3 v_t1;
varying flat vec3 v_t2;
varying flat float v_tipo;
void vertex() {
	v_nw = MODEL_NORMAL_MATRIX * NORMAL;
	v_pw = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 ex = MODEL_MATRIX[0].xyz; vec3 ey = MODEL_MATRIX[1].xyz; vec3 ez = MODEL_MATRIX[2].xyz;
	vec3 tam = vec3(length(ex), length(ey), length(ez));
	vec3 ax = ex / max(tam.x, 0.0001); vec3 ay = ey / max(tam.y, 0.0001); vec3 az = ez / max(tam.z, 0.0001);
	vec3 n = NORMAL;
	vec3 an = abs(n);
	if (an.y >= an.x && an.y >= an.z) {
		v_p = vec2((VERTEX.x + 0.5) * tam.x, (VERTEX.z + 0.5) * tam.z);
		v_t1 = ax; v_t2 = az;
		v_tipo = n.y > 0.0 ? gl_sa.x : gl_sa.y;
	} else if (an.z >= an.x) {
		v_p = vec2((VERTEX.x + 0.5) * tam.x, (VERTEX.y + 0.5) * tam.y);
		v_t1 = ax; v_t2 = ay;
		v_tipo = n.z > 0.0 ? gl_sb.x : gl_sa.z;
	} else {
		v_p = vec2((VERTEX.z + 0.5) * tam.z, (VERTEX.y + 0.5) * tam.y);
		v_t1 = az; v_t2 = ay;
		v_tipo = n.x > 0.0 ? gl_sb.z : gl_sb.y;
	}
}
void fragment() {
	vec2 p = v_p;
	vec3 col = gl_col.rgb;
	if (gl_tex_esc > 0.0) { col *= texture(gl_tex, p * gl_tex_esc).rgb; }
	vec3 nn = normalize(v_nw);
	if (v_tipo > 0.5) {
		float signo = v_tipo > 1.5 ? -1.0 : 1.0;
		float mul = texture(gl_studs, p).r / 0.784;
		vec3 nt = texture(gl_studs_n, p).rgb * 2.0 - 1.0;
		if (signo < 0.0) { nt.xy = -nt.xy; mul = 2.0 - mul; }
		nn = normalize(nn * nt.z + v_t1 * nt.x + v_t2 * nt.y);
		col *= mul;
	}
	vec3 v = normalize(CAMERA_POSITION_WORLD - v_pw);
	ALBEDO = gl_iluminar(col, nn, v_pw, v, gl_rug, gl_met, gl_esp, gl_sol_vis(FRAGCOORD.xy, -VERTEX.z)) + gl_emi * gl_emi_f;
)";
        if (alfa) s += "\tALPHA = gl_col.a;\n";
        s += "}\n";
        return s;
    }
    const bool uv = (f & GL_LUZ_TEX_UV) != 0;
    const bool tri = (f & GL_LUZ_TEX_TRI) != 0;
    const bool nor = tri && (f & GL_LUZ_NORMAL) != 0;
    const bool vcol = (f & GL_LUZ_VCOLOR) != 0;
    if (uv || tri) {
        s += "uniform sampler2D gl_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;\n";
        s += "uniform vec3 gl_uv_esc = vec3(1.0);\nuniform vec3 gl_uv_desp = vec3(0.0);\n";
    }
    if (tri) s += "uniform float gl_tri_nit = 1.0;\nvarying vec3 v_tp;\nvarying vec3 v_tw;\n";
    if (nor) {
        s += "uniform sampler2D gl_tex_n : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;\n";
        s += "uniform float gl_n_prof = 1.0;\nvarying vec3 v_tan;\nvarying vec3 v_bin;\n";
    }
    if (hash) s += "uniform float gl_hash = 1.0;\n";
    if (tri) {
        // Solo se lee la proyeccion que pesa: en la cara plana de un bloque (casi
        // todo un place) dos de los tres pesos son 0 exactos. Las derivadas van
        // fuera de los if, asi el filtro (mipmaps, anisotropico) es el mismo.
        // MEDIDO en Build a Mountain, calidad 7: las texturas eran 0,28 ms.
        s += R"(
vec4 gl_tri(sampler2D t, vec3 w, vec3 p) {
	vec3 dx = dFdx(p);
	vec3 dy = dFdy(p);
	vec4 c = vec4(0.0);
	if (w.z > 0.0) {
		c += textureGrad(t, p.xy, dx.xy, dy.xy) * w.z;
	}
	if (w.y > 0.0) {
		c += textureGrad(t, p.xz, dx.xz, dy.xz) * w.y;
	}
	if (w.x > 0.0) {
		vec2 sx = vec2(-1.0, 1.0);
		c += textureGrad(t, p.zy * sx, dx.zy * sx, dy.zy * sx) * w.x;
	}
	return c;
}
)";
    }
    s += "void vertex() {\n\tv_nw = MODEL_NORMAL_MATRIX * NORMAL;\n\tv_pw = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;\n";
    if (uv) s += "\tUV = UV * gl_uv_esc.xy + gl_uv_desp.xy;\n";
    if (tri) {
        s += R"(	vec3 esc = vec3(length(MODEL_MATRIX[0].xyz), length(MODEL_MATRIX[1].xyz), length(MODEL_MATRIX[2].xyz));
	vec3 w = pow(abs(NORMAL), vec3(gl_tri_nit));
	v_tw = w / dot(w, vec3(1.0));
	v_tp = (VERTEX * esc * gl_uv_esc + gl_uv_desp) * vec3(1.0, -1.0, 1.0);
)";
    }
    if (nor) {
        s += R"(	vec3 t = vec3(0.0, 0.0, -1.0) * abs(NORMAL.x) + vec3(1.0, 0.0, 0.0) * abs(NORMAL.y) + vec3(1.0, 0.0, 0.0) * abs(NORMAL.z);
	vec3 b = vec3(0.0, 1.0, 0.0) * abs(NORMAL.x) + vec3(0.0, 0.0, -1.0) * abs(NORMAL.y) + vec3(0.0, 1.0, 0.0) * abs(NORMAL.z);
	v_tan = (MODEL_MATRIX * vec4(normalize(t), 0.0)).xyz;
	v_bin = (MODEL_MATRIX * vec4(normalize(b), 0.0)).xyz;
)";
    }
    if (vcol) {
        s += "\tCOLOR.rgb = mix(pow((COLOR.rgb + vec3(0.055)) * (1.0 / (1.0 + 0.055)), vec3(2.4)), "
             "COLOR.rgb * (1.0 / 12.92), lessThan(COLOR.rgb, vec3(0.04045)));\n";
    }
    s += "}\nvoid fragment() {\n\tvec4 alb = gl_col;\n";
    if (uv) s += "\talb *= texture(gl_tex, UV);\n";
    if (tri) s += "\talb *= gl_tri(gl_tex, v_tw, v_tp);\n";
    if (vcol) s += "\talb *= COLOR;\n";
    s += "\tvec3 n = normalize(v_nw);\n";
    if (nor) {
        s += R"(	vec3 nm = gl_tri(gl_tex_n, v_tw, v_tp).rgb;
	nm.xy = nm.xy * 2.0 - 1.0;
	nm.z = sqrt(max(0.0, 1.0 - dot(nm.xy, nm.xy)));
	n = normalize(mix(n, normalize(v_tan) * nm.x + normalize(v_bin) * nm.y + n * nm.z, gl_n_prof));
)";
    }
    s += "\tvec3 v = normalize(CAMERA_POSITION_WORLD - v_pw);\n";
    s += "\tALBEDO = gl_iluminar(alb.rgb, n, v_pw, v, gl_rug, gl_met, gl_esp, gl_sol_vis(FRAGCOORD.xy, -VERTEX.z))"
         " + gl_emi * gl_emi_f;\n";
    if (alfa) s += "\tALPHA = alb.a;\n";
    if (hash) s += "\tALPHA_HASH_SCALE = gl_hash;\n";
    s += "}\n";
    return s;
}

inline HashMap<uint32_t, Ref<Shader>>& gl_luz_shaders() {
    static HashMap<uint32_t, Ref<Shader>> c;
    return c;
}

// Solo para medir que cuesta cada parte (GL_LUZ_QUITAR=ibl,esp,puntos,tex).
inline String gl_luz_quitar(String s) {
    static const String q = OS::get_singleton() ? OS::get_singleton()->get_environment("GL_LUZ_QUITAR") : String();
    if (q.is_empty()) return s;
    if (q.contains("ibl"))
        s = s.replace("esp += gl_cubo_ibl(r) * (hor * hor) * (eb.x * f0 + eb.y * clamp(50.0 * f0.g, met, 1.0));", "");
    if (q.contains("esp")) s = s.replace("if (gl_sol_col.w > 0.0) {", "if (false) {");
    if (q.contains("puntos")) s = s.replace("if (gl_luz_n > 0) {", "if (false) {");
    if (q.contains("tex")) {
        s = s.replace("texture(gl_tex, p * gl_tex_esc).rgb", "vec3(1.0)");
        s = s.replace("alb *= texture(gl_tex, UV);", "");
        s = s.replace("alb *= gl_tri(gl_tex, v_tw, v_tp);", "");
        s = s.replace("texture(gl_studs, p).r", "0.784");
        s = s.replace("texture(gl_studs_n, p).rgb", "vec3(0.5, 0.5, 1.0)");
    }
    return s;
}

inline Ref<Shader> gl_luz_shader(uint32_t f) {
    HashMap<uint32_t, Ref<Shader>>& c = gl_luz_shaders();
    if (Ref<Shader>* e = c.getptr(f)) return *e;
    gl_luz_globales();   // antes de compilar: un global que no existe no compila
    Ref<Shader> sh;
    sh.instantiate();
    sh->set_code(gl_luz_quitar(gl_luz_codigo(f)));
    c[f] = sh;
    return sh;
}

// Que variante necesita un StandardMaterial3D de pieza.
inline uint32_t gl_luz_flags(const Ref<StandardMaterial3D>& m) {
    uint32_t f = 0;
    switch (m->get_transparency()) {
        case BaseMaterial3D::TRANSPARENCY_DISABLED: break;
        case BaseMaterial3D::TRANSPARENCY_ALPHA_HASH: f |= GL_LUZ_HASH; break;
        default: f |= GL_LUZ_ALFA; break;
    }
    if (m->get_texture(BaseMaterial3D::TEXTURE_ALBEDO).is_valid())
        f |= m->get_flag(BaseMaterial3D::FLAG_UV1_USE_TRIPLANAR) ? GL_LUZ_TEX_TRI : GL_LUZ_TEX_UV;
    if ((f & GL_LUZ_TEX_TRI) && m->get_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING) &&
        m->get_texture(BaseMaterial3D::TEXTURE_NORMAL).is_valid())
        f |= GL_LUZ_NORMAL;
    if (m->get_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR)) f |= GL_LUZ_VCOLOR;
    return f;
}

// Copia al ShaderMaterial lo que dibuja el StandardMaterial3D.
inline void gl_luz_copiar(const Ref<StandardMaterial3D>& m, const Ref<ShaderMaterial>& sm, uint32_t f) {
    static const StringName k_col("gl_col"), k_rug("gl_rug"), k_met("gl_met"), k_esp("gl_esp");
    static const StringName k_emi("gl_emi"), k_emi_f("gl_emi_f"), k_tex("gl_tex"), k_uv("gl_uv_esc");
    static const StringName k_tri("gl_tri_nit"), k_tex_n("gl_tex_n"), k_n("gl_n_prof"), k_hash("gl_hash");
    sm->set_shader_parameter(k_col, m->get_albedo());
    sm->set_shader_parameter(k_rug, m->get_roughness());
    sm->set_shader_parameter(k_met, m->get_metallic());
    sm->set_shader_parameter(k_esp, m->get_specular());
    const bool emi = m->get_feature(BaseMaterial3D::FEATURE_EMISSION);
    sm->set_shader_parameter(k_emi, emi ? m->get_emission() : Color(0, 0, 0));
    sm->set_shader_parameter(k_emi_f, emi ? m->get_emission_energy_multiplier() : 0.0f);
    if (f & (GL_LUZ_TEX_UV | GL_LUZ_TEX_TRI)) {
        static const StringName k_desp("gl_uv_desp");
        sm->set_shader_parameter(k_tex, m->get_texture(BaseMaterial3D::TEXTURE_ALBEDO));
        sm->set_shader_parameter(k_uv, m->get_uv1_scale());
        sm->set_shader_parameter(k_desp, m->get_uv1_offset());
    }
    // Las calcomanias se ordenan entre si con la prioridad (su ZIndex).
    if (sm->get_render_priority() != m->get_render_priority()) sm->set_render_priority(m->get_render_priority());
    if (f & GL_LUZ_TEX_TRI) sm->set_shader_parameter(k_tri, m->get_uv1_triplanar_blend_sharpness());
    if (f & GL_LUZ_NORMAL) {
        sm->set_shader_parameter(k_tex_n, m->get_texture(BaseMaterial3D::TEXTURE_NORMAL));
        sm->set_shader_parameter(k_n, m->get_normal_scale());
    }
    if (f & GL_LUZ_HASH) sm->set_shader_parameter(k_hash, m->get_alpha_hash_scale());
}

// Los compartidos (que no cambian) por su id; cada pieza con material propio
// guarda el suyo y aqui se pone al dia.
inline HashMap<uint64_t, Ref<ShaderMaterial>>& gl_luz_cache() {
    static HashMap<uint64_t, Ref<ShaderMaterial>> c;
    return c;
}

static inline const StringName& gl_luz_k_base() {
    static const StringName k("_gl_luz_base");
    return k;
}

inline Ref<ShaderMaterial> gl_luz_de(const Ref<StandardMaterial3D>& m, Ref<ShaderMaterial>* propio = nullptr) {
    if (m.is_null()) return Ref<ShaderMaterial>();
    const uint32_t f = gl_luz_flags(m);
    if (propio) {
        Ref<ShaderMaterial>& sm = *propio;
        if (sm.is_null()) sm.instantiate();
        Ref<Shader> sh = gl_luz_shader(f);
        if (sm->get_shader() != sh) sm->set_shader(sh);
        gl_luz_copiar(m, sm, f);
        sm->set_meta(gl_luz_k_base(), m);
        return sm;
    }
    HashMap<uint64_t, Ref<ShaderMaterial>>& c = gl_luz_cache();
    const uint64_t k = (uint64_t)m->get_instance_id();
    if (Ref<ShaderMaterial>* e = c.getptr(k)) return *e;
    Ref<ShaderMaterial> sm;
    sm.instantiate();
    sm->set_shader(gl_luz_shader(f));
    gl_luz_copiar(m, sm, f);
    sm->set_meta(gl_luz_k_base(), m);
    c[k] = sm;
    gl_purgar_cache(c, 8192);
    return sm;
}

// Con la luz propia activa, el de la luz propia; si no, el mismo.
inline Ref<Material> gl_luz_si_toca(const Ref<StandardMaterial3D>& m) {
    if (m.is_valid() && gl_luz_propia_activa()) return gl_luz_de(m);
    return m;
}

// El material con la luz de Godot del que salio uno de la luz propia (lo que
// dibuja un ViewportFrame, que tiene sus propias luces). Si no es nuestro, el
// mismo.
inline Ref<Material> gl_luz_godot(const Ref<Material>& m) {
    if (m.is_valid() && m->has_meta(gl_luz_k_base())) {
        Ref<Material> b = m->get_meta(gl_luz_k_base());
        if (b.is_valid()) return b;
    }
    return m;
}

// Al cerrar, con el servidor de render vivo (como gls_liberar).
inline void gl_luz_liberar() {
    gl_luz_cache().clear();
    gl_luz_shaders().clear();
    gl_luz_blanca_ref().unref();
}

#endif // GL_LUZ_PROPIA_H
