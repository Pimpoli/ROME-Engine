#ifndef ROBLOX_WORKSPACE_MUNDO_H
#define ROBLOX_WORKSPACE_MUNDO_H

// Conjuntos rigidos (assemblies) y streaming por distancia del Workspace.
// Salio de roblox_workspace.h (que lo incluye al final, tras la clase) para que
// no pase de 2.000 lineas; el codigo es el mismo.

#include "roblox_workspace.h"   // la clase entera: este archivo se incluye al final de ella

using namespace godot;

// ══════════════════════════════════════════════════════════════════
//  CONSTRUCTOR DE CONJUNTOS (assemblies) — ver gl_assembly.h
//
//  Agrupa por union-find las piezas unidas por juntas RIGIDAS, elige la
//  raiz con las reglas de Roblox y le pega a esa raiz las formas de todos
//  sus miembros. El resultado es UN cuerpo con la masa, el centro de masas
//  y la forma del conjunto entero, como en Roblox.
//
//  Solo se reconstruye cuando algo lo ensucia (una junta que aparece,
//  desaparece o se recablea, o un Anchored que cambia). NO lo ensucian las
//  altas normales del arbol: durante una tanda de brainrots entran cientos
//  de piezas y reconstruir en cada una seria absurdo.
// ══════════════════════════════════════════════════════════════════
inline uint64_t RobloxWorkspace::_uf_raiz(HashMap<uint64_t, uint64_t>& padre, uint64_t x) {
    while (true) {
        uint64_t* p = padre.getptr(x);
        if (!p || *p == x) return x;
        uint64_t abuelo = x;
        if (uint64_t* pp = padre.getptr(*p)) abuelo = *pp;
        padre[x] = abuelo;      // aplastado del camino
        x = abuelo;
    }
}

inline void RobloxWorkspace::_uf_unir(HashMap<uint64_t, uint64_t>& padre, uint64_t a, uint64_t b) {
    if (!padre.has(a)) padre[a] = a;
    if (!padre.has(b)) padre[b] = b;
    const uint64_t ra = _uf_raiz(padre, a);
    const uint64_t rb = _uf_raiz(padre, b);
    if (ra != rb) padre[rb] = ra;
}

inline uint64_t RobloxWorkspace::_huella_de(const LocalVector<RobloxPart*>& piezas) {
    // Mezcla que no depende del orden: suma + xor + cuantos son.
    uint64_t suma = 0, x = 0;
    for (uint32_t i = 0; i < piezas.size(); i++) {
        const uint64_t id = (uint64_t)piezas[i]->get_instance_id();
        suma += id;
        x ^= id * 0x9E3779B97F4A7C15ULL;
    }
    return suma ^ (x + (uint64_t)piezas.size() * 0x100000001B3ULL);
}

// Piezas vivas de un grupo, quien manda y si alguna esta anclada. Solo LEE:
// se puede llamar dos veces sin que pase nada.
inline bool RobloxWorkspace::_resolver_grupo(const LocalVector<uint64_t>& ids, LocalVector<RobloxPart*>& piezas,
                                             RobloxPart*& raiz, bool& alguna_anclada) {
    piezas.clear();
    alguna_anclada = false;
    for (uint32_t i = 0; i < ids.size(); i++) {
        RobloxPart* p = gl_cast<RobloxPart>(ObjectDB::get_instance(ObjectID(ids[i])));
        // Viva, en el arbol y colgando del Workspace (no de un almacen).
        if (!p || !p->is_inside_tree() || !p->gl_en_workspace()) continue;
        if (p->get_anchored()) alguna_anclada = true;
        piezas.push_back(p);
    }
    if (piezas.size() < 2) return false;
    // ── La RAIZ, con las reglas de Roblox: manda RootPriority, y a igualdad
    //    la pieza mas grande. El id desempata para que la eleccion sea
    //    estable entre reconstrucciones.
    raiz = piezas[0];
    for (uint32_t i = 1; i < piezas.size(); i++) {
        RobloxPart* c = piezas[i];
        const int pr_c = c->get_root_priority(), pr_r = raiz->get_root_priority();
        if (pr_c != pr_r) { if (pr_c > pr_r) raiz = c; continue; }
        const Vector3 sc = c->get_size(), sr = raiz->get_size();
        const float vc = sc.x * sc.y * sc.z, vr = sr.x * sr.y * sr.z;
        if (vc != vr) { if (vc > vr) raiz = c; continue; }
        if ((uint64_t)c->get_instance_id() < (uint64_t)raiz->get_instance_id()) raiz = c;
    }
    return true;
}

// ── SOLO SE REHACE LO QUE CAMBIO ─────────────────────────────────────
// Esto se hacia entero cada vez: soltar las formas prestadas de TODAS las
// raices y volver a ponerlas. Medido en el place de prueba (2.077
// soldaduras): 4,27 ms de tiron, 31 veces en 40 s. Sobre una mediana de 7 ms
// eso es casi doblar el frame, casi una vez por segundo — un bajon pequeño y
// mas o menos regular, justo lo que se veia.
//
// Y era trabajo tirado: cuando aparece un brainrot con sus tres soldaduras,
// los otros 2.000 conjuntos salen EXACTAMENTE IGUAL que estaban. Ahora se
// agrupa primero (union-find, que es solo tabla hash y no toca la fisica),
// se le saca una huella a cada grupo y solo se desmonta y se vuelve a montar
// el que de verdad cambio de miembros. Quitar y poner formas de colision en
// Jolt es lo caro; agrupar no.
inline void RobloxWorkspace::_reconstruir_conjuntos() {
    GLAsmStats& st = gl_asm_stats();
    Time* T = Time::get_singleton();
    uint64_t t = T->get_ticks_usec();
    // El espejo lo rellena gl_constraints_tick en el paso de fisica. Si
    // todavia no ha pasado por ahi desde el ultimo cambio, se espera: son
    // como mucho un par de frames y la reconstruccion ya iba diferida.
    if (!gl_juntas_al_dia()) { gl_ensamblajes_sucios() = true; return; }

    // 1. Union-find sobre las juntas rigidas. Enteros y nada mas: ni un
    //    cast_to, ni una consulta a ObjectDB, ni una subida del arbol.
    //    Filtrar piezas muertas o de fuera del Workspace se hace despues, en
    //    _resolver_grupo, que ya resuelve cada pieza de todos modos.
    HashMap<uint64_t, uint64_t> padre;
    const LocalVector<GLJuntaRigida>& jr = gl_juntas_rigidas();
    const HashMap<uint64_t, bool>& ws = gl_partes_workspace();
    st.n_juntas = (uint64_t)jr.size();
    for (uint32_t i = 0; i < jr.size(); i++) {
        if (!ws.has(jr[i].p0) || !ws.has(jr[i].p1)) continue;
        _uf_unir(padre, jr[i].p0, jr[i].p1);
    }

    st.f1_union = T->get_ticks_usec() - t; t = T->get_ticks_usec();

    // 2. Juntar los miembros de cada grupo.
    HashMap<uint64_t, LocalVector<uint64_t>> grupos;
    for (const KeyValue<uint64_t, uint64_t>& kv : padre) {
        const uint64_t r = _uf_raiz(padre, kv.key);
        grupos[r].push_back(kv.key);
    }
    st.n_grupos = (uint64_t)grupos.size();
    st.f2_grupos = T->get_ticks_usec() - t; t = T->get_ticks_usec();

    // 3. Resolver piezas y raiz de cada grupo, y sacarle la huella. Todavia
    //    sin tocar nada: aqui solo se decide QUE hay que rehacer.
    // Resolver un grupo: piezas vivas, raiz elegida y huella. No toca nada,
    // solo lee — se puede llamar dos veces sin efectos.
    LocalVector<RobloxPart*> piezas;
    HashMap<uint64_t, uint64_t> huellas;   // id de la raiz -> huella nueva
    HashMap<uint64_t, uint64_t> raiz_de_grupo;  // clave del grupo -> raiz
    for (const KeyValue<uint64_t, LocalVector<uint64_t>>& g : grupos) {
        if (g.value.size() < 2) continue;
        RobloxPart* raiz = nullptr;
        bool dummy = false;
        if (!_resolver_grupo(g.value, piezas, raiz, dummy)) continue;
        const uint64_t rid = (uint64_t)raiz->get_instance_id();
        huellas[rid] = _huella_de(piezas);
        raiz_de_grupo[g.key] = rid;
    }

    // 4. Desmontar SOLO lo que ya no vale: conjuntos que desaparecieron o
    //    que cambiaron de miembros. Los que salen con la misma huella se
    //    quedan tal cual, sin tocarles ni una forma de colision.
    st.f3_huellas = T->get_ticks_usec() - t; t = T->get_ticks_usec();
    st.n_intactos = 0; st.n_rehechos = 0;

    LocalVector<uint64_t> a_borrar;
    for (const KeyValue<uint64_t, LocalVector<uint64_t>>& kv : gl_miembros_de_raiz()) {
        const uint64_t* hn = huellas.getptr(kv.key);
        const uint64_t* hv = _huella_grupo.getptr(kv.key);
        if (hn && hv && *hn == *hv) { st.n_intactos++; continue; }   // identico: no tocar
        for (uint32_t i = 0; i < kv.value.size(); i++) {
            if (RobloxPart* p = gl_cast<RobloxPart>(
                    ObjectDB::get_instance(ObjectID(kv.value[i])))) {
                p->gl_conjunto_deshacer();
                gl_raiz_de_pieza().erase(kv.value[i]);
            }
        }
        a_borrar.push_back(kv.key);
    }
    for (uint32_t i = 0; i < a_borrar.size(); i++) {
        gl_miembros_de_raiz().erase(a_borrar[i]);
        _huella_grupo.erase(a_borrar[i]);
    }

    st.f4_quitar = T->get_ticks_usec() - t; t = T->get_ticks_usec();

    // 5. Montar los que hagan falta (los nuevos y los que cambiaron).
    for (const KeyValue<uint64_t, uint64_t>& rg : raiz_de_grupo) {
        if (gl_miembros_de_raiz().has(rg.value)) continue;   // intacto
        st.n_rehechos++;
        const LocalVector<uint64_t>* miembros = grupos.getptr(rg.key);
        if (!miembros) continue;
        RobloxPart* raiz = nullptr;
        bool alguna_anclada = false;
        if (!_resolver_grupo(*miembros, piezas, raiz, alguna_anclada)) continue;
        _huella_grupo[rg.value] = huellas[rg.value];

        // ── Formas y masa al cuerpo raiz ──────────────────────────
        const Transform3D inv_raiz = raiz->get_global_transform().affine_inverse();
        float masa = raiz->gl_masa_propia();
        Vector3 momento = raiz->get_position() * 0.0f;   // acumulador en local
        float masa_c = raiz->gl_masa_propia();           // para el centro de masas
        LocalVector<uint64_t> ids;
        ids.push_back((uint64_t)raiz->get_instance_id());

        for (uint32_t i = 0; i < piezas.size(); i++) {
            RobloxPart* m = piezas[i];
            if (m == raiz) continue;
            Transform3D rel = inv_raiz * m->get_global_transform();
            // El giro del cilindro va en la forma, no en el nodo.
            if (m->gl_forma_girada()) {
                Transform3D giro;
                giro.basis = Basis(Vector3(0, 0, 1), 1.5707964f);
                rel = rel * giro;
            }
            // Solo aportan colision los miembros que colisionan (CanCollide).
            if (m->get_can_collide())
                raiz->gl_conjunto_anadir_forma(m->gl_forma_propia(), rel, (uint64_t)m->get_instance_id());
            const float mm = m->gl_masa_propia();
            masa += mm;
            momento += rel.origin * mm;
            masa_c += mm;
            m->gl_conjunto_ser_miembro();
            ids.push_back((uint64_t)m->get_instance_id());
        }
        raiz->gl_conjunto_masa(masa, masa_c > 0.0001f ? (momento / masa_c) : Vector3());

        // ── Anclado: en Roblox, si UNA pieza esta anclada, TODO el
        //    conjunto lo esta. La raiz manda sobre el cuerpo de verdad.
        // Comparado antes de escribir, como en el resto de escrituras al
        // cuerpo: los dos setters acaban en body_set_mode SIEMPRE, y en
        // Jolt eso mueve al cuerpo de capa de broadphase. Este bloque corre
        // por cada conjunto que se rehace, o sea una vez por modelo clonado.
        if (raiz->get_freeze_mode() != RigidBody3D::FREEZE_MODE_STATIC)
            raiz->set_freeze_mode(RigidBody3D::FREEZE_MODE_STATIC);
        if (raiz->is_freeze_enabled() != alguna_anclada)
            raiz->set_freeze_enabled(alguna_anclada);

        const uint64_t rid = (uint64_t)raiz->get_instance_id();
        for (uint32_t i = 0; i < ids.size(); i++) gl_raiz_de_pieza()[ids[i]] = rid;
        gl_miembros_de_raiz()[rid] = ids;
    }
    st.f5_montar = T->get_ticks_usec() - t;
}

inline void RobloxWorkspace::_stream_tick() {
    Viewport* vp = get_viewport();
    Camera3D* cam = vp ? vp->get_camera_3d() : nullptr;
    if (!cam) return;
    // El plano FAR de la camara tiene que acompanar a la distancia de
    // vista. Godot recorta en 4000 por defecto y NADIE lo tocaba: "ver
    // 16384 studs" era mentira a partir de 4000 — el streaming mantenia
    // las piezas activas pero la proyeccion las cortaba igual. Con el
    // Z invertido de Godot 4 subir el far no cuesta precision cerca.
    // OJO: esto va ANTES del corte por lista vacia — en una escena sin
    // piezas streameables el far tiene que seguir la vista igual.
    const float lejos = MAX(gl_view_studs_actual(), 4000.0f);
    if (Math::abs((float)cam->get_far() - lejos) > 1.0f) cam->set_far(lejos);
    LocalVector<uint64_t>& lista = gl_stream_lista();
    if (lista.is_empty()) return;
    const Vector3 ojo = cam->get_global_transform().origin;

    // El radio de sombra sigue saliendo del alcance real de la luz.
    float alcance = 800.0f;
    for (int i = 0; i < get_child_count(); i++)
        if (DirectionalLight3D* dl = gl_cast<DirectionalLight3D>(get_child(i)))
            alcance = (float)dl->get_param(Light3D::PARAM_SHADOW_MAX_DISTANCE);
    const float r_som = Math::max(1500.0f, alcance * 2.0f);
    // ── Radio de vision: manda el NIVEL DE CALIDAD ────────────────
    // La tabla de gl_graphics.h dice cuantos studs se ven en cada nivel
    // 1..10 (el jugador los puede cambiar uno a uno). StreamingTargetRadius
    // sigue existiendo como TOPE del place, igual que en Roblox: si el
    // desarrollador lo baja a mano, se respeta; si no lo toca, manda la
    // calidad. Del nivel 8 para arriba la distancia es "infinita" (16.384) y
    // ahi ni se compara: no se oculta nada nunca.
    const bool  vista_infinita = gl_view_studs_actual() >= GL_VIEW_INFINITA - 0.5f
                                 && !stream_radius_explicito;
    float r_vis = gl_view_studs_actual();
    if (stream_radius_explicito) r_vis = Math::min(r_vis, stream_radius);
    // Si el jugador cambio la calidad, las piezas tienen que rehacer su LOD
    // de dibujado. Se aprovecha este mismo barrido: en unos pocos frames han
    // pasado todas.
    const uint32_t vgen = gl_view_gen();
    const float r_vis2 = r_vis * r_vis;
    const float r_som2 = r_som * r_som;
    const float r_part2 = 220.0f * 220.0f;

    // 400 por frame a 60 FPS, o sea 24.000 por segundo. A mas FPS se reparten
    // (a 200 FPS, 120 por frame) y la vuelta al mapa tarda los mismos segundos;
    // a menos de 60 se quedan en 400 por frame, como siempre.
    _stream_saldo = MIN(_stream_saldo + get_process_delta_time() * (GL_STREAM_POR_FRAME * 60.0),
                        (double)GL_STREAM_POR_FRAME);
    uint32_t quedan = (uint32_t)_stream_saldo;
    _stream_saldo -= (double)quedan;
    while (quedan-- > 0 && !lista.is_empty()) {
        if (_stream_cursor >= lista.size()) _stream_cursor = 0;
        const uint64_t id = lista[_stream_cursor];
        Object* o = ObjectDB::get_instance(ObjectID(id));
        Node* n = gl_cast<Node>(o);
        // Muerta o fuera del arbol: fuera de la lista (swap-and-pop, O(1)).
        if (!n || !n->is_inside_tree()) {
            gl_stream_olvidar(id);
            lista[_stream_cursor] = lista[lista.size() - 1];
            lista.resize(lista.size() - 1);
            continue;
        }
        _stream_cursor++;

        if (RobloxPart* p = gl_cast<RobloxPart>(n)) {
            const float d2 = (float)p->get_global_position().distance_squared_to(ojo);
            // Vista infinita (calidad 8+): no hay nada que ocultar, asi que
            // se pasa un radio imposible de superar en vez de comparar.
            p->gl_stream_estado(d2, vista_infinita ? 3.4e38f : r_vis2, r_som2);
            p->gl_lod_refrescar(vgen);
            // Si la matriz de grupos de colision cambio, la pieza se entera
            // aqui. Es una comparacion de dos enteros cuando no ha cambiado.
            if (!p->gl_grupo_al_dia()) p->_aplicar_grupo_colision();
            // Physics LOD: la distancia ya esta calculada, clasificar es
            // gratis. Ver gl_physlod.h para por que "Hz" significa aqui
            // congelar/CCD y no una frecuencia del motor.
            p->gl_lod_evaluar(d2);
            switch (p->gl_lod_clase()) {
                case GL_LOD_ESTATICO:  _lod_est++;  break;
                case GL_LOD_MECANISMO: _lod_mec++;  break;
                default:               _lod_din++;  break;
            }
            if (p->gl_lod_congelada()) _lod_frz++;
            if (p->gl_lod_ccd())       _lod_ccd++;
            // Al cerrar una vuelta completa a la lista se publican los
            // contadores (asi son del mapa entero, no de las 400 de este
            // frame) y se empieza a contar de nuevo.
            if (_stream_cursor >= lista.size()) {
                GLPhysLodCfg& lc = gl_physlod();
                lc.n_estatico = _lod_est;  lc.n_dinamico = _lod_din;
                lc.n_mecanismo = _lod_mec; lc.n_congeladas = _lod_frz;
                lc.n_ccd = _lod_ccd;
                _lod_est = _lod_din = _lod_mec = _lod_frz = _lod_ccd = 0;
                lc.p_lejos = lc.n_lejos;  lc.p_ajenas = lc.n_ajenas;
                lc.p_moviendo = lc.n_moviendo;
                lc.n_lejos = lc.n_ajenas = lc.n_moviendo = 0;
            }
            continue;
        }
        if (GPUParticles3D* pe = gl_cast<GPUParticles3D>(n)) {
            // PROCESS_MODE_DISABLED es lo que de verdad para la simulacion:
            // con emitting=false medido no cambiaba nada.
            const bool lejos = pe->get_global_position().distance_squared_to(ojo) > r_part2;
            const Node::ProcessMode quiero = lejos ? Node::PROCESS_MODE_DISABLED
                                                   : Node::PROCESS_MODE_INHERIT;
            if (pe->get_process_mode() != quiero) {
                pe->set_process_mode(quiero);
                pe->set_visible(!lejos);
            }
        }
    }
}

// Version vieja, de un tiron. Ya no la llama el bucle del frame (la sustituye
// _stream_tick); se conserva porque hace el mismo trabajo de una sentada y
// sirve para forzar un repaso completo si alguna vez hace falta.
inline void RobloxWorkspace::gl_recortar_sombras_lejanas() {
    Viewport* vp = get_viewport();
    Camera3D* cam = vp ? vp->get_camera_3d() : nullptr;
    if (!cam) return;
    const Vector3 ojo = cam->get_global_transform().origin;
    // Radio generoso: el doble del alcance de la sombra, con un minimo. Lo que
    // queda fuera esta tan lejos que su sombra no se veria de todos modos.
    float alcance = 800.0f;
    for (int i = 0; i < get_child_count(); i++)
        if (DirectionalLight3D* dl = gl_cast<DirectionalLight3D>(get_child(i)))
            alcance = (float)dl->get_param(Light3D::PARAM_SHADOW_MAX_DISTANCE);
    const float radio = Math::max(1500.0f, alcance * 2.0f);
    // ── PARTICULAS LEJANAS: el mayor coste medido del frame ──────────
    // El place trae 3.166 emisores y TODOS se simulaban siempre, estuvieran
    // o no delante del jugador. En Roblox un emisor lejos no cuesta nada.
    // Medido apagando por grupos (FPS, mismo sitio):
    //     tal cual .................... 42,8
    //     sin Label3D/BillboardGui .... 47,6
    //     sin ParticleEmitter ......... 56,4   <- el salto grande
    //     sin RobloxPart .............. 58,5
    // Una particula de Roblox mide poco: mas alla de ~220 unidades no se
    // distingue. Al volver a acercarse se reactiva sola.
    const float radio_part = 220.0f;
    _recorrer_recortes(this, ojo, radio * radio, radio_part * radio_part);
}

inline void RobloxWorkspace::_recorrer_recortes(Node* n, const Vector3& ojo, float radio2, float radio_part2) {
    if (RobloxPart* p = gl_cast<RobloxPart>(n)) {
        p->gl_sombra_lejana(p->get_global_position().distance_squared_to(ojo) > radio2);
    } else if (GPUParticles3D* pe = gl_cast<GPUParticles3D>(n)) {
        // PROCESS_MODE_DISABLED es lo que de verdad para la simulacion:
        // con emitting=false medido no cambiaba nada.
        const bool lejos = pe->get_global_position().distance_squared_to(ojo) > radio_part2;
        const Node::ProcessMode quiero = lejos ? Node::PROCESS_MODE_DISABLED
                                               : Node::PROCESS_MODE_INHERIT;
        if (pe->get_process_mode() != quiero) {
            pe->set_process_mode(quiero);
            pe->set_visible(!lejos);
        }
    }
    for (int i = 0; i < n->get_child_count(); i++)
        _recorrer_recortes(n->get_child(i), ojo, radio2, radio_part2);
}

#endif // ROBLOX_WORKSPACE_MUNDO_H
