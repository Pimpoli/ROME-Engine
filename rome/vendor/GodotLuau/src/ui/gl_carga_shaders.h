#ifndef GL_CARGA_SHADERS_H
#define GL_CARGA_SHADERS_H

// Los shaders de la pantalla de carga por defecto (gl_carga_defecto.h). La
// animacion entera la calculan ellos: cada capa se dibuja UNA vez y cada frame
// la CPU solo pasa el reloj. Las curvas son las de gl_carga_piezas.h, pasadas
// a GLSL (las cifras del splash original, en px de su lienzo de 640x360).

// Curvas comunes: el ease() de Godot, OUT_BACK, la caida de cada logo (aparece
// 0,267 s antes de tocar, baja 100 px viniendo de mas cerca y rebota 16 y 7
// px), el anillo (ease 0,2176 en 0,7 s) y el color HSV de Godot.
static const char* GL_CARGA_GLSL_COMUN = R"(
float gl_ease(float x, float c) {
	x = clamp(x, 0.0, 1.0);
	if (c < 1.0) return 1.0 - pow(1.0 - x, 1.0 / c);
	return pow(x, c);
}
float gl_back(float t) {
	t = clamp(t, 0.0, 1.0) - 1.0;
	return 1.0 + 2.70158 * t * t * t + 1.70158 * t * t;
}
float gl_logo_alfa(float tau) {
	return clamp((tau + 0.26667) / 0.26667, 0.0, 1.0);
}
float gl_logo_escala(float tau) {
	if (tau >= 0.0) return 1.0;
	return 5.7 - 4.7 * gl_ease(max(0.0, (31.233334 + tau) / 31.233334), 8.574176);
}
float gl_logo_dy(float tau) {
	float e = 0.0;
	if (tau <= -0.26667) e = -100.0;
	else if (tau < 0.0) e = mix(-100.0, 0.0, gl_ease((tau + 0.26667) / 0.26667, 2.2973967));
	float r = 0.0;
	if (tau > 0.0 && tau < 0.13333) r = mix(0.0, -16.0, gl_ease(tau / 0.13333, 0.3186402));
	else if (tau >= 0.13333 && tau < 0.26667) r = mix(-16.0, 0.0, gl_ease((tau - 0.13333) / 0.13334, 2.6390157));
	else if (tau >= 0.26667 && tau < 0.36667) r = mix(0.0, -7.035, (tau - 0.26667) / 0.1);
	else if (tau >= 0.36667 && tau < 0.46667) r = mix(-7.035, 0.0, gl_ease((tau - 0.36667) / 0.1, 2.6390157));
	return e + r;
}
float gl_onda(float tau) {
	return gl_ease((tau + 0.033333) / 0.7, 0.2176377);
}
vec3 gl_hsv(float h, float s, float v) {
	vec3 c = clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
	return v * mix(vec3(1.0), c, s);
}
)";

// Los logos cayendo. Cada uno se dibuja en reposo con su indice en el rojo
// del color (0 Rocket Meteor, 1 GodotLuau); el shader lo escala alrededor de
// su centro, lo baja y lo funde.
static const char* GL_CARGA_SHADER_LOGOS = R"(
global uniform float gl_carga_reloj;
uniform float escala = 1.0;
uniform vec2 toques = vec2(0.0);
uniform vec2 centro0 = vec2(0.0);
uniform vec2 centro1 = vec2(0.0);
varying float v_alfa;
void vertex() {
	bool segundo = COLOR.r > 0.5;
	COLOR = vec4(1.0);
	float tau = gl_carga_reloj - (segundo ? toques.y : toques.x);
	vec2 c = segundo ? centro1 : centro0;
	v_alfa = gl_logo_alfa(tau);
	VERTEX = c + (VERTEX - c) * gl_logo_escala(tau) + vec2(0.0, gl_logo_dy(tau) * escala);
	if (v_alfa <= 0.0) VERTEX = c;
}
void fragment() {
	COLOR = texture(TEXTURE, UV) * vec4(1.0, 1.0, 1.0, v_alfa);
}
)";

// Las letras y la ×, desde el atlas (un canal por capa: rojo la letra, verde
// la letra con su borde de 2 px, azul la letra con su anillo de 5 px). Cada
// instancia es una celda; su color dice donde esta en el atlas y su dato
// propio, (cuando entro, indice o -1 la ×, centro en el lienzo). `capa`: 0
// sombra (el azul en negro, bajado), 1 anillo (arriba el color de la letra,
// abajo gris, sobre la caja de su linea de Roblox: 1,763 em que empiezan 0,517
// em por encima del em), 2 la letra blanca con su borde negro. Una capa por
// pasada, como los tres TextLabel de la intro de Roblox. En un MultiMesh 2D,
// VERTEX llega en el espacio de la celda (0..1) y Godot pone la instancia
// DESPUES: por eso skip_vertex_transform, y el shader lo pasa a px del lienzo
// con MODEL_MATRIX (que ya la incluye) antes de moverlo.
static const char* GL_CARGA_MODO_GLIFOS = "render_mode skip_vertex_transform;\n";
static const char* GL_CARGA_SHADER_GLIFOS = R"(
uniform int capa = 0;
global uniform float gl_carga_reloj;
global uniform float gl_carga_reloj_real;
uniform float escala = 1.0;
uniform vec2 x_centro = vec2(0.0);
uniform float x_toque = 0.0;
uniform vec4 medida_letra = vec4(1.0);
uniform vec4 medida_aspa = vec4(1.0);
uniform vec2 atlas = vec2(1.0);
varying vec4 v_rect;
varying float v_alfa;
varying float v_tono;
varying float v_aspa;
void vertex() {
	v_rect = COLOR;
	COLOR = vec4(1.0);
	vec4 d = INSTANCE_CUSTOM;
	vec2 p = (MODEL_MATRIX * vec4(VERTEX, 0.0, 1.0)).xy;
	v_tono = fract(d.z / 640.0 * 2.0 + gl_carga_reloj_real);
	if (d.y < 0.0) {
		v_aspa = 1.0;
		float tau = gl_carga_reloj - x_toque;
		v_alfa = gl_logo_alfa(tau);
		p = x_centro + (p - x_centro) * gl_logo_escala(tau) + vec2(0.0, gl_logo_dy(tau) * escala);
	} else {
		v_aspa = 0.0;
		float k = clamp((gl_carga_reloj - d.x) / 0.3, 0.0, 1.0);
		v_alfa = k;
		float dy = ((1.0 - gl_back(k)) * -30.0 + sin(gl_carga_reloj * 7.0 + d.y * 0.8) * 1.5) * escala;
		p.y += floor(dy + 0.5);
	}
	VERTEX = v_alfa > 0.0 ? p : vec2(0.0);
}
void fragment() {
	vec2 cuv = mix(v_rect.xy, v_rect.zw, UV);
	vec4 med = v_aspa > 0.5 ? medida_aspa : medida_letra;
	vec4 m = texture(TEXTURE, cuv);
	vec4 c;
	if (capa == 0) {
		vec2 s = cuv - vec2(0.0, med.w / atlas.y);
		c = vec4(0.0, 0.0, 0.0, s.y >= v_rect.y ? texture(TEXTURE, s).b : 0.0);
	} else if (capa == 1) {
		float k = clamp((UV.y * med.x - med.y + 0.517 * med.z) / (1.763 * med.z), 0.0, 1.0);
		c = vec4(mix(gl_hsv(v_tono, 0.5, 1.0), vec3(0.3403393), smoothstep(0.38, 0.42, k)), m.b);
	} else {
		float a = m.r + m.g * (1.0 - m.r);
		c = vec4(vec3(m.r / max(a, 0.0001)), a);
	}
	COLOR = vec4(c.rgb, c.a * v_alfa);
}
)";

// Halo, anillo y destello de arcoiris de cada logo (rainbow_shockwave del
// original), detras de el y moviendose con el. Los tres cuadros se dibujan en
// reposo (indice en el rojo: 0, 0,5, 1); el shader los coloca, los recorta a
// lo que pinta (mientras vive el anillo, hasta su borde; despues, el halo) y
// apaga el halo al romperse. Lo que no pinta, ni se mezcla.
static const char* GL_CARGA_SHADER_HALOS = R"(
global uniform float gl_carga_reloj;
global uniform float gl_carga_reloj_real;
global uniform float gl_carga_rotura;
global uniform float gl_carga_bajada;
uniform float escala = 1.0;
uniform vec3 toques = vec3(0.0);
uniform vec2 centro0 = vec2(0.0);
uniform vec2 centro1 = vec2(0.0);
uniform vec2 centro2 = vec2(0.0);
varying float v_prog;
varying float v_alfa;
varying float v_ext;
varying float v_halo;
varying float v_destello;
vec3 gl_hsv_suave(float h, float s, float v) {
	vec3 c = clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
	c = c * c * (3.0 - 2.0 * c);
	return v * mix(vec3(1.0), c, s);
}
void vertex() {
	int i = int(COLOR.r * 2.0 + 0.5);
	COLOR = vec4(1.0);
	float toque = i == 0 ? toques.x : (i == 1 ? toques.y : toques.z);
	vec2 c = i == 0 ? centro0 : (i == 1 ? centro1 : centro2);
	float tau = gl_carga_reloj - toque;
	float prog = gl_onda(tau);
	float ext = 0.336;
	if (prog < 1.0) ext = max(ext, prog * 0.466 + 0.052);
	ext = min(ext, 0.5);
	float dy = (gl_carga_rotura >= 0.0 ? 0.0 : gl_logo_dy(tau) * escala) + gl_carga_bajada;
	bool ver = tau >= -0.033333 && gl_carga_rotura <= 0.6;
	VERTEX = ver ? c + (VERTEX - c) * gl_logo_escala(tau) * (ext / 0.5) + vec2(0.0, dy) : c;
	v_prog = prog;
	v_alfa = gl_logo_alfa(tau);
	v_ext = ext;
	v_halo = 1.0;
	v_destello = 0.0;
	if (gl_carga_rotura >= 0.0) {
		float desde = gl_carga_rotura - 0.22;
		float apaga = desde < 0.0 ? 1.0 : 1.0 - min(1.0, desde / 0.35);
		v_halo = apaga;
		v_destello = desde < 0.0 ? 0.0 : apaga;
	}
}
void fragment() {
	vec2 d = (UV - vec2(0.5)) * (v_ext * 2.0);
	float dist = length(d);
	float mask = (1.0 - smoothstep(0.336 - 0.789, 0.336, dist)) * v_halo;
	mask = max(mask, (1.0 - smoothstep(0.0, 0.052, abs(dist - v_prog * 0.466))) * (1.0 - v_prog));
	mask = max(mask, (1.0 - smoothstep(0.0, 0.278, dist)) * max(sin(v_prog * PI), v_destello));
	float a = mask * 0.778 * 2.0 * v_alfa;
	if (a < 0.002) discard;
	vec3 col = gl_hsv_suave(fract(atan(d.y, d.x) / (2.0 * PI) + gl_carga_reloj_real * 0.25), 0.5, 1.0);
	COLOR = vec4(col, a);
}
)";

// El lienzo de la composicion sobre la pantalla (modo 0) o, al romperse, sus
// trozos (modo 1): una malla con todos, cada vertice con el indice de su trozo
// (rojo + verde * 256, en 8 bits) y los datos de cada trozo en una textura
// (fila 0: casa y velocidad; fila 1: giro, grieta, temblor, vida). Tiemblan
// mientras se agrietan, saltan con gravedad girando, se apagan desde el 55 %
// de su vida y bajan con la pantalla. Alfa premultiplicado, como el lienzo
// (GL_CARGA_MODO_TROZOS va delante de las curvas comunes).
static const char* GL_CARGA_MODO_TROZOS = "render_mode blend_premul_alpha;\n";
static const char* GL_CARGA_SHADER_TROZOS = R"(
uniform int modo = 0;
global uniform float gl_carga_rotura;
global uniform float gl_carga_bajada;
uniform float gravedad = 900.0;
uniform sampler2D datos : filter_nearest;
varying float v_alfa;
float gl_azar(vec2 p) {
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}
void vertex() {
	v_alfa = 1.0;
	if (modo == 1) {
		int i = int(COLOR.r * 255.0 + 0.5) + int(COLOR.g * 255.0 + 0.5) * 256;
		COLOR = vec4(1.0);
		vec4 a = texelFetch(datos, ivec2(i, 0), 0);
		vec4 b = texelFetch(datos, ivec2(i, 1), 0);
		float vuelo = gl_carga_rotura - b.y;
		vec2 pos = a.xy;
		float rot = 0.0;
		if (vuelo < 0.0) {
			float paso = floor(gl_carga_rotura * 60.0);
			pos += (vec2(gl_azar(vec2(float(i), paso)), gl_azar(vec2(paso, float(i) + 7.0))) * 2.0 - 1.0) * b.z;
		} else {
			pos += a.zw * vuelo + vec2(0.0, 0.5 * gravedad * vuelo * vuelo);
			rot = radians(b.x * vuelo);
		}
		pos.y += gl_carga_bajada;
		float cr = cos(rot);
		float sr = sin(rot);
		VERTEX = pos + vec2(VERTEX.x * cr - VERTEX.y * sr, VERTEX.x * sr + VERTEX.y * cr);
		v_alfa = 1.0 - clamp((vuelo - 0.55 * b.w) / (0.45 * b.w), 0.0, 1.0);
		if (vuelo >= b.w) VERTEX = vec2(0.0);
	}
}
void fragment() {
	COLOR = texture(TEXTURE, UV) * v_alfa;
}
)";

#endif // GL_CARGA_SHADERS_H
