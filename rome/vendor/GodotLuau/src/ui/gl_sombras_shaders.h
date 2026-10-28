#ifndef GL_SOMBRAS_SHADERS_H
#define GL_SOMBRAS_SHADERS_H

// Shaders de las sombras propias (gl_sombras.h). Ver alli el porque.

// Copia la profundidad de una camara ortogonal del sol como D = dot(P, -sol):
// la distancia al plano de la camara + `desfase` (el D de ese plano), igual
// para todas las baldosas. Ortogonal con Z invertida: d = 1 en el plano
// cercano y 0 en el lejano, lineal -> dist = cerca + (1 - d) * (lejos - cerca).
// Las filas van al reves: en el mapa, v crece hacia +Y del sol.
static const char* GL_SOMBRA_COPIA = R"(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D prof;
layout(r32f, set = 0, binding = 1) uniform restrict writeonly image2D dst;
layout(push_constant, std430) uniform P { vec2 tam; float cerca; float lejos; float desfase; float ox; float oy; float r; } p;
void main() {
	ivec2 id = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(id, ivec2(p.tam)))) return;
	float d = texelFetch(prof, id, 0).r;
	float D = p.cerca + (1.0 - d) * (p.lejos - p.cerca) + p.desfase;
	imageStore(dst, ivec2(p.ox, p.oy) + ivec2(id.x, int(p.tam.y) - 1 - id.y), vec4(D));
}
)";

// Lo comun a la mascara y a la pasada final: los datos y la geometria.
static const char* GL_SOMBRA_COMUN = R"(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 4, std140) uniform P {
	vec4 cam_x; vec4 cam_y; vec4 cam_z; vec4 cam_o;
	vec4 lente;   // tanX, tanY, cerca, lejos
	vec4 tam;     // ancho, alto, modo, opacidad
	vec4 sol;     // hacia el sol, suavidad (studs)
	vec4 S;       // w = 1 si hay mapa dinamico
	vec4 e_x;     // eje X del sol, lado de la baldosa (studs)
	vec4 e_y;     // eje Y del sol, baldosas por lado
	vec4 e_par;   // px por baldosa, px del atlas, sesgo (texeles), desplazamiento por la normal (texeles)
	vec4 ventana; // primera baldosa (i, j), tan del tamaño del sol, penumbra maxima (studs)
	vec4 flags[7];
	vec4 d_x;     // eje X del sol, esquina x del mapa dinamico
	vec4 d_y;     // eje Y del sol, esquina y
	vec4 d_par;   // lado (studs), px
	vec4 niebla;  // modo (0 no, 1 exponencial, 2 profundidad), densidad, inicio, fin
	vec4 niebla2; // curva, altura, densidad en altura
} p;
layout(set = 0, binding = 1) uniform sampler2D prof;

vec3 vista(ivec2 id) {
	float d = texelFetch(prof, id, 0).r;
	float n = p.lente.z, f = p.lente.w;
	float L = n * f / (n + max(d, 1e-7) * (f - n));
	vec2 uv = (vec2(id) + 0.5) / p.tam.xy;
	return vec3((uv.x * 2.0 - 1.0) * p.lente.x * L, (1.0 - uv.y * 2.0) * p.lente.y * L, -L);
}

// Normal (espacio de la camara) desde la profundidad: con cada vecino el lado
// de menos salto, asi un borde no mezcla la cara de delante con la de detras.
vec3 normal_vista(ivec2 id, vec3 vpos) {
	ivec2 lim = ivec2(p.tam.xy) - 1;
	vec3 pr = vista(min(id + ivec2(1, 0), lim)), pl = vista(max(id - ivec2(1, 0), ivec2(0)));
	vec3 pd = vista(min(id + ivec2(0, 1), lim)), pu = vista(max(id - ivec2(0, 1), ivec2(0)));
	vec3 dx = abs(pr.z - vpos.z) < abs(vpos.z - pl.z) ? pr - vpos : vpos - pl;
	vec3 dy = abs(pd.z - vpos.z) < abs(vpos.z - pu.z) ? pd - vpos : vpos - pu;
	vec3 nv = normalize(cross(dy, dx));
	return dot(nv, vpos) > 0.0 ? -nv : nv;
}

vec3 a_mundo(vec3 v) { return p.cam_x.xyz * v.x + p.cam_y.xyz * v.y + p.cam_z.xyz * v.z; }
)";

// Pasada 1, a media resolucion: cuanta luz del sol le llega a cada pixel,
// mirando el atlas de baldosas (guardado) y el mapa dinamico (cada frame).
// Guarda (luz, distancia) para elegir luego la muestra de profundidad parecida.
static const char* GL_SOMBRA_MASCARA = R"(
layout(rg16f, set = 0, binding = 0) uniform restrict writeonly image2D masc;
layout(set = 0, binding = 3) uniform sampler2D atlas;
layout(set = 0, binding = 5) uniform sampler2D mapa_din;

float luz_2x2(sampler2D m, vec2 uv, float ref, float res) {
	vec4 g = textureGather(m, uv, 0);   // (i0,j1) (i1,j1) (i1,j0) (i0,j0)
	vec4 l = step(vec4(ref), g);
	vec2 f = fract(uv * res - 0.5);
	return mix(mix(l.w, l.z, f.x), mix(l.x, l.y, f.x), f.y);
}

float nucleo(sampler2D m, vec2 uv, float ref, float res, float r) {
	float s = 0.0;
	for (int j = -1; j <= 1; j++)
		for (int i = -1; i <= 1; i++)
			s += luz_2x2(m, uv + vec2(i, j) * (r / res), ref, res);
	return s / 9.0;
}

// Sombra como la de verdad: nitida donde el objeto toca el suelo y mas difusa
// cuanto mas lejos esta lo que tapa. Primero se buscan bloqueadores alrededor
// (16 texeles en 4 gathers, en el radio de la penumbra maxima): si no hay
// ninguno, luz sin mas (lo normal: sale barato); si tapan todos, sombra. Si
// no, la penumbra = base + distancia al bloqueador x tan(tamaño del sol).
float sombra_mapa(sampler2D m, vec2 uv, float ref, float res, float texel) {
	float rs = clamp(0.5 * p.ventana.w / texel, 1.0, 8.0);
	float suma = 0.0;
	float n = 0.0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			vec4 g = textureGather(m, uv + (vec2(i, j) - 0.5) * (rs / res), 0);
			vec4 b = vec4(lessThan(g, vec4(ref)));
			suma += dot(g, b);
			n += b.x + b.y + b.z + b.w;
		}
	if (n < 0.5) return 1.0;
	float w = min(p.sol.w + max(ref - suma / n, 0.0) * p.ventana.z, p.ventana.w);
	float r = max(0.5 * w / texel, 0.5);
	if (n > 15.5 && r <= rs) return 0.0;
	return nucleo(m, uv, ref, res, r);
}

// 1 = le da el sol, 0 = sombra, -1 = no hay dato.
float luz_atlas(vec3 wpos, vec3 nw) {
	float T = p.e_x.w, n = p.e_y.w;
	float texel = T / p.e_par.x;
	vec3 P = wpos + nw * (p.e_par.w * texel);
	vec2 xy = vec2(dot(p.e_x.xyz, P), dot(p.e_y.xyz, P));
	vec2 rel = xy / T - p.ventana.xy;
	if (any(lessThan(rel, vec2(0.0))) || any(greaterThanEqual(rel, vec2(n)))) return -1.0;
	ivec2 s = ivec2(mod(floor(xy / T), vec2(n)));
	int k = s.y * int(n) + s.x;
	if (p.flags[k >> 2][k & 3] < 0.5) return -1.0;
	// El sol con el que se dibujo el atlas (puede ir un poco por detras del de
	// ahora mientras se rehace el otro): su eje Z = X x Y.
	float ref = dot(P, -cross(p.e_x.xyz, p.e_y.xyz)) - p.e_par.z * texel;
	float l = sombra_mapa(atlas, xy / (T * n), ref, p.e_par.y, texel);
	float borde = min(min(rel.x, n - rel.x), min(rel.y, n - rel.y));
	return mix(1.0, l, clamp(borde * 2.0, 0.0, 1.0));
}

float luz_din(vec3 wpos, vec3 nw) {
	float texel = p.d_par.x / p.d_par.y;
	vec3 P = wpos + nw * (p.e_par.w * texel);
	vec2 uv = vec2(dot(p.d_x.xyz, P) - p.d_x.w, dot(p.d_y.xyz, P) - p.d_y.w) / p.d_par.x;
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return -1.0;
	float ref = dot(P, -cross(p.d_x.xyz, p.d_y.xyz)) - p.e_par.z * texel;
	float l = sombra_mapa(mapa_din, uv, ref, p.d_par.y, texel);
	vec2 b = min(uv, 1.0 - uv);
	return mix(1.0, l, clamp(min(b.x, b.y) * 10.0, 0.0, 1.0));
}

void main() {
	ivec2 h = ivec2(gl_GlobalInvocationID.xy);
	ivec2 hs = (ivec2(p.tam.xy) + 1) / 2;
	if (any(greaterThanEqual(h, hs))) return;
	ivec2 id = min(h * 2, ivec2(p.tam.xy) - 1);
	float d = texelFetch(prof, id, 0).r;
	if (d <= 0.0) { imageStore(masc, h, vec4(1.0, 60000.0, 0.0, 0.0)); return; }
	vec3 vpos = vista(id);
	vec3 nw = normalize(a_mundo(normal_vista(id, vpos)));
	if (dot(nw, p.sol.xyz) <= 0.0) { imageStore(masc, h, vec4(1.0, -vpos.z, 0.0, 0.0)); return; }
	vec3 wpos = p.cam_o.xyz + a_mundo(vpos);
	float luz = luz_atlas(wpos, nw);
	// Sombra entera en el atlas: el mapa dinamico ya no cambia nada (es el minimo).
	if (p.S.w > 0.5 && luz != 0.0) {
		float ld = luz_din(wpos, nw);
		if (ld >= 0.0) luz = luz < 0.0 ? ld : min(luz, ld);
	}
	imageStore(masc, h, vec4(luz < 0.0 ? 1.0 : luz, -vpos.z, 0.0, 0.0));
}
)";

// Pasada 2, a resolucion completa: donde hay sombra, quita la parte del SOL.
//   pixel = albedo * (A(N) + S * N.L) + resto
//   en sombra = albedo * (A(N) + S * N.L * (1 - opacidad)) + resto
// A(N) y S salen de la sonda (8 cuadros blancos que dibuja el propio Godot:
// ambiente en +x -x +y -y +z -z, y en la direccion del sol sin y con sol).
// La niebla de Godot se mezcla despues de la luz: lo que es niebla no se oscurece.
static const char* GL_SOMBRA_APLICAR = R"(
layout(rgba16f, set = 0, binding = 0) uniform restrict image2D color;
layout(set = 0, binding = 2) uniform sampler2D masc;
layout(set = 0, binding = 6) uniform sampler2D sonda;

vec3 sonda_px(int i) { return texelFetch(sonda, ivec2(i * 16 + 8, 8), 0).rgb; }

vec3 ambiente(vec3 n) {
	vec3 q = n * n;
	return q.x * sonda_px(n.x > 0.0 ? 0 : 1) + q.y * sonda_px(n.y > 0.0 ? 2 : 3) + q.z * sonda_px(n.z > 0.0 ? 4 : 5);
}

float niebla(float dist, float y) {
	float f = 0.0;
	if (p.niebla.x > 1.5) f = pow(smoothstep(p.niebla.z, p.niebla.w, dist), p.niebla2.x) * p.niebla.y;
	else if (p.niebla.x > 0.5) f = 1.0 - exp(min(0.0, -dist * p.niebla.y));
	if (p.niebla.x > 0.5 && abs(p.niebla2.z) >= 0.0001) f = max(f, 1.0 - exp(min(0.0, (y - p.niebla2.y) * p.niebla2.z)));
	return clamp(f, 0.0, 1.0);
}

void main() {
	ivec2 id = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(id, ivec2(p.tam.xy)))) return;
	// De las cuatro muestras de la mascara alrededor: si todas dan sol, nada
	// que hacer (lo normal); si no, la de distancia mas parecida.
	ivec2 h = id / 2;
	ivec2 hmax = (ivec2(p.tam.xy) + 1) / 2 - 1;
	ivec2 o = ivec2((id.x & 1) == 1 ? 1 : -1, (id.y & 1) == 1 ? 1 : -1);
	vec2 m0 = texelFetch(masc, h, 0).xy;
	vec2 m1 = texelFetch(masc, clamp(h + ivec2(o.x, 0), ivec2(0), hmax), 0).xy;
	vec2 m2 = texelFetch(masc, clamp(h + ivec2(0, o.y), ivec2(0), hmax), 0).xy;
	vec2 m3 = texelFetch(masc, clamp(h + o, ivec2(0), hmax), 0).xy;
	int modo = int(p.tam.z);
	if (min(min(m0.x, m1.x), min(m2.x, m3.x)) >= 0.999 && modo != 1) return;
	float d = texelFetch(prof, id, 0).r;
	if (d <= 0.0) return;
	vec3 vpos = vista(id);
	float L = -vpos.z;
	vec2 m = m0;
	if (abs(m1.y - L) < abs(m.y - L)) m = m1;
	if (abs(m2.y - L) < abs(m.y - L)) m = m2;
	if (abs(m3.y - L) < abs(m.y - L)) m = m3;
	float luz = m.x;
	if (modo == 1) { imageStore(color, id, vec4(vec3(luz), 1.0)); return; }
	if (luz >= 0.999) return;
	vec3 nw = normalize(a_mundo(normal_vista(id, vpos)));
	float ndl = dot(nw, p.sol.xyz);
	if (ndl <= 0.0) return;
	vec3 A = ambiente(nw);
	vec3 S = max(sonda_px(7) - sonda_px(6), vec3(0.0)) * ndl;
	vec3 k = (A + S * (1.0 - p.tam.w)) / max(A + S, vec3(1e-4));
	vec3 wpos = p.cam_o.xyz + a_mundo(vpos);
	k = mix(k, vec3(1.0), niebla(length(vpos), wpos.y));
	vec4 c = imageLoad(color, id);
	c.rgb *= mix(k, vec3(1.0), luz);
	imageStore(color, id, c);
}
)";

#endif // GL_SOMBRAS_SHADERS_H
