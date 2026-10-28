#ifndef GL_CUNAS_H
#define GL_CUNAS_H

// Geometria de las cunas de Roblox (Wedge y CornerWedge) y de la celosia de
// TrussPart. Salio de roblox_part.h para que no pase de 2.000 lineas.

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/shape3d.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

using namespace godot;

// ── CUÑAS DE ROBLOX ──────────────────────────────────────────────────────────
// Wedge y CornerWedge se dibujaban Y chocaban como cajas. Aqui se construye la
// geometria de verdad: la malla con normales planas y la forma de colision como
// casco convexo con los MISMOS vertices, para que lo que se ve y lo que choca
// sean lo mismo.
//
// El sentido de giro de cada triangulo no se deja al azar: se le pasa la normal
// que DEBE tener la cara y se ordena en consecuencia. Asi no hay caras del reves
// por haberme equivocado al escribir los vertices.
inline void _gl_tri(PackedVector3Array& v, const Vector3& a, const Vector3& b,
                    const Vector3& c, const Vector3& fuera) {
    const Vector3 n = (b - a).cross(c - a);
    if (n.dot(fuera) >= 0.0f) { v.push_back(a); v.push_back(b); v.push_back(c); }
    else                      { v.push_back(a); v.push_back(c); v.push_back(b); }
}
inline void _gl_quad(PackedVector3Array& v, const Vector3& a, const Vector3& b,
                     const Vector3& c, const Vector3& d, const Vector3& fuera) {
    _gl_tri(v, a, b, c, fuera);
    _gl_tri(v, a, c, d, fuera);
}

// Malla con normales PLANAS a partir de triangulos sueltos (una cuña tiene
// aristas vivas: interpolar normales la dejaria con aspecto de globo).
//
// _gl_tri deja cada triangulo en sentido ANTIHORARIO visto desde fuera (su
// (b-a)x(c-a) apunta afuera), pero Godot dibuja de frente los HORARIOS (doc. de
// ArrayMesh y SurfaceTool: "clockwise winding order for front faces"). Con el
// orden tal cual, la GPU tiraba las caras de fuera y pintaba las de dentro,
// con la normal mirando al otro lado: los taludes de la cantera de Build a
// Mountain salian NEGROS (azul marino en la foto de la camara 3) y se veia la
// cara de atras de cada cuña. Aqui se da la vuelta al triangulo (a, c, b) y la
// normal sigue siendo la de fuera.
inline Ref<ArrayMesh> gl_malla_de_triangulos(const PackedVector3Array& tris) {
    PackedVector3Array verts, normales;
    verts.resize(tris.size());
    normales.resize(tris.size());
    for (int i = 0; i + 2 < tris.size(); i += 3) {
        const Vector3 a = tris[i], b = tris[i + 1], c = tris[i + 2];
        const Vector3 n = (b - a).cross(c - a).normalized();
        verts[i] = a; verts[i + 1] = c; verts[i + 2] = b;
        normales[i] = n; normales[i + 1] = n; normales[i + 2] = n;
    }
    Array arr;
    arr.resize(ArrayMesh::ARRAY_MAX);
    arr[ArrayMesh::ARRAY_VERTEX] = verts;
    arr[ArrayMesh::ARRAY_NORMAL] = normales;
    Ref<ArrayMesh> m;
    m.instantiate();
    m->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
    return m;
}

// Wedge: prisma triangular. Como en Roblox, la cara rectangular vertical va
// DETRAS (+Z, la cara Back) y la pendiente baja hacia DELANTE (-Z, hacia donde
// mira la LookVector). Antes estaba al reves (alta en -Z).
// Comprobado de dos formas, no supuesto:
//  - la normal de la pendiente que sale de la formula del devforum de Roblox
//    ("Finding the angle of a wedgepart": bv = -(Z*Look - Y*Up), n = bv x Right)
//    es (0, Size.Z, -Size.Y) en local: sube y mira a -Z; el triangulo de
//    EgoMoose (draw3dTriangle) tambien deja el borde alto en +Z;
//  - en Build a Mountain las 7 cuñas de la cantera tienen su +Z hacia DENTRO
//    del foso, y en Roblox (rbp_3) se ven como rampas de tierra que SUBEN desde
//    la hierba hasta el borde de piedra.
inline void gl_geom_cuna(const Vector3& s, Ref<Mesh>& malla, Ref<Shape3D>& forma) {
    const float hx = s.x * 0.5f, hy = s.y * 0.5f, hz = s.z * 0.5f;
    const Vector3 A(-hx, -hy, -hz), B(hx, -hy, -hz);   // arista baja, delante
    const Vector3 E(-hx, -hy,  hz), F(hx, -hy,  hz);   // suelo, detras
    const Vector3 D(-hx,  hy,  hz), C(hx,  hy,  hz);   // arista alta, detras
    PackedVector3Array t;
    _gl_quad(t, E, F, C, D, Vector3(0, 0, 1));    // cara vertical (Back)
    _gl_quad(t, A, E, F, B, Vector3(0, -1, 0));   // suelo
    _gl_quad(t, A, B, C, D, Vector3(0, s.z, -s.y));   // la pendiente
    _gl_tri (t, A, D, E, Vector3(-1, 0, 0));      // costado izquierdo
    _gl_tri (t, B, F, C, Vector3( 1, 0, 0));      // costado derecho
    malla = gl_malla_de_triangulos(t);
    PackedVector3Array puntos;
    puntos.push_back(A); puntos.push_back(B); puntos.push_back(C);
    puntos.push_back(D); puntos.push_back(E); puntos.push_back(F);
    Ref<ConvexPolygonShape3D> cv;
    cv.instantiate();
    cv->set_points(puntos);
    forma = cv;
}

// CornerWedge: piramide con la punta sobre la esquina (+X, +Y, -Z), como en
// Roblox. Suelo entero, dos caras verticales triangulares (la de +X y la de -Z,
// que se juntan bajo la punta) y dos pendientes que bajan hacia -X y hacia +Z.
// Antes la punta iba sobre (-X, -Z). Comprobado en Build a Mountain: las 4
// esquinas de la cantera, cada una con su giro (0, 90, -90, 180), tienen esa
// esquina (+X, -Z) hacia DENTRO del foso, justo donde las cuñas vecinas tienen
// su borde alto, y en Roblox (rbp_3, rb_2) sus dos pendientes enlazan sin
// escalon con las rampas.
inline void gl_geom_cuna_esquina(const Vector3& s, Ref<Mesh>& malla, Ref<Shape3D>& forma) {
    const float hx = s.x * 0.5f, hy = s.y * 0.5f, hz = s.z * 0.5f;
    const Vector3 A(-hx, -hy, -hz), B(hx, -hy, -hz), C(hx, -hy, hz), D(-hx, -hy, hz);
    const Vector3 P(hx,  hy, -hz);    // la punta, sobre la esquina B
    PackedVector3Array t;
    _gl_quad(t, A, B, C, D, Vector3(0, -1, 0));   // suelo
    _gl_tri (t, A, B, P, Vector3(0, 0, -1));      // cara vertical en -Z
    _gl_tri (t, B, C, P, Vector3(1, 0, 0));       // cara vertical en +X
    _gl_tri (t, P, A, D, Vector3(-s.y, s.x, 0));  // pendiente hacia -X
    _gl_tri (t, P, D, C, Vector3(0, s.z, s.y));   // pendiente hacia +Z
    malla = gl_malla_de_triangulos(t);
    PackedVector3Array puntos;
    puntos.push_back(A); puntos.push_back(B); puntos.push_back(C);
    puntos.push_back(D); puntos.push_back(P);
    Ref<ConvexPolygonShape3D> cv;
    cv.instantiate();
    cv->set_points(puntos);
    forma = cv;
}

// Caja orientada (para las barras de la celosia): centro y tres semiejes.
inline void _gl_caja_orientada(PackedVector3Array& v, const Vector3& c, const Vector3& eu,
                               const Vector3& ev, const Vector3& ew) {
    Vector3 p[8];
    for (int i = 0; i < 8; i++)
        p[i] = c + ((i & 1) ? eu : -eu) + ((i & 2) ? ev : -ev) + ((i & 4) ? ew : -ew);
    _gl_quad(v, p[1], p[3], p[7], p[5], eu);
    _gl_quad(v, p[0], p[4], p[6], p[2], -eu);
    _gl_quad(v, p[2], p[6], p[7], p[3], ev);
    _gl_quad(v, p[0], p[1], p[5], p[4], -ev);
    _gl_quad(v, p[4], p[5], p[7], p[6], ew);
    _gl_quad(v, p[0], p[2], p[3], p[1], -ew);
}

// TrussPart: la escalera de barras de Roblox. Cuatro largueros en las aristas
// del eje mas largo, un marco cada 2 studs y una diagonal por cara en cada
// tramo, alternando el sentido (el estilo AlternatingSupports, el de serie).
// Choca como su caja entera, igual que en Roblox. Antes se importaba y se
// creaba como un bloque macizo.
inline void gl_geom_truss(const Vector3& s, Ref<Mesh>& malla, Ref<Shape3D>& forma) {
    static const Vector3 EJE[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
    int la = 1;                       // eje largo
    if (s.x > s[la]) la = 0;
    if (s.z > s[la]) la = 2;
    const int a1 = (la + 1) % 3, a2 = (la + 2) % 3;
    const float h[3] = { s.x * 0.5f, s.y * 0.5f, s.z * 0.5f };
    const float g = Math::min(0.3f, Math::min(h[a1], h[a2]) * 0.4f);   // grosor de barra
    const float b1 = h[a1] - g * 0.5f, b2 = h[a2] - g * 0.5f;          // donde van las barras
    auto P = [&](float l, float u, float w) { return EJE[la] * l + EJE[a1] * u + EJE[a2] * w; };
    PackedVector3Array t;
    // Largueros
    for (int i = 0; i < 4; i++) {
        const float u = (i & 1) ? b1 : -b1, w = (i & 2) ? b2 : -b2;
        _gl_caja_orientada(t, P(0, u, w), EJE[la] * h[la], EJE[a1] * (g * 0.5f), EJE[a2] * (g * 0.5f));
    }
    // Marcos cada 2 studs
    const int tramos = MAX(1, (int)Math::round(s[la] / 2.0f));
    const float paso = s[la] / (float)tramos;
    for (int i = 0; i <= tramos; i++) {
        const float l = CLAMP(-h[la] + paso * i, -h[la] + g * 0.5f, h[la] - g * 0.5f);
        for (int lado = -1; lado <= 1; lado += 2) {
            _gl_caja_orientada(t, P(l, 0, lado * b2), EJE[a1] * h[a1], EJE[la] * (g * 0.5f), EJE[a2] * (g * 0.5f));
            _gl_caja_orientada(t, P(l, lado * b1, 0), EJE[a2] * h[a2], EJE[la] * (g * 0.5f), EJE[a1] * (g * 0.5f));
        }
    }
    // Diagonales: en cada cara, de una esquina del tramo a la contraria
    const float gd = g * 0.8f;
    for (int i = 0; i < tramos; i++) {
        const float l0 = -h[la] + paso * i, l1 = l0 + paso;
        const float sg = (i % 2 == 0) ? 1.0f : -1.0f;
        for (int cara = 0; cara < 4; cara++) {
            const bool en_u = cara < 2;                         // cara de u fija o de w fija
            const float fijo = (cara % 2 == 0) ? 1.0f : -1.0f;
            Vector3 a, b, normal;
            if (en_u) {
                a = P(l0, fijo * b1, -sg * b2); b = P(l1, fijo * b1, sg * b2); normal = EJE[a1];
            } else {
                a = P(l0, -sg * b1, fijo * b2); b = P(l1, sg * b1, fijo * b2); normal = EJE[a2];
            }
            const Vector3 d = b - a;
            const Vector3 lado_v = d.cross(normal).normalized();
            _gl_caja_orientada(t, (a + b) * 0.5f, d * 0.5f, normal * (gd * 0.5f), lado_v * (gd * 0.5f));
        }
    }
    malla = gl_malla_de_triangulos(t);
    Ref<BoxShape3D> caja;
    caja.instantiate();
    caja->set_size(s);
    forma = caja;
}

#endif // GL_CUNAS_H
