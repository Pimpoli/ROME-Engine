#ifndef LUAU_API_INDICE_GUI_H
#define LUAU_API_INDICE_GUI_H
#include "gl_cast.h"

// Lectura de propiedades desde Luau (__index de las instancias), la GUI: PlayerGui, GuiObject, tweens de GUI, las clases de GUI,
// ScrollingFrame y el respaldo de lectura de las GUI.
//
// Va aparte de luau_api.h (pasaba de 2.000 lineas) y se incluye desde alli,
// justo antes de godot_object_index_real, que llama a las zonas en orden.
// Devuelve cuantos valores deja en la pila, o -1 si la clave no es de
// esta zona y hay que seguir buscando.

// Propiedades de la seleccion con mando de un GuiObject. Aqui no hay
// navegacion con mando, pero se guardan y se leen: la UI de mando de los
// juegos las usa. 0 no es suya, 1 booleano, 2 numero, 3 SelectionBehavior,
// 4 instancia (NextSelection*, SelectionImageObject).
static inline int gl_gui_prop_seleccion(const char* key) {
    auto lado = [](const char* d) {
        return strcmp(d, "Up") == 0 || strcmp(d, "Down") == 0 || strcmp(d, "Left") == 0 || strcmp(d, "Right") == 0;
    };
    if (key[0] == 'S' && strncmp(key, "Selection", 9) == 0) {
        const char* r = key + 9;
        if (strcmp(r, "Group") == 0) return 1;
        if (strcmp(r, "Order") == 0) return 2;
        if (strcmp(r, "ImageObject") == 0) return 4;
        if (strncmp(r, "Behavior", 8) == 0 && lado(r + 8)) return 3;
        return 0;
    }
    if (key[0] == 'N' && strncmp(key, "NextSelection", 13) == 0 && lado(key + 13)) return 4;
    return 0;
}

inline int gl_index_gui(lua_State* L, Node* n, const char* key) {
    // ViewportFrame.CurrentCamera (gl_viewport_frame.h), antes que el de
    // Workspace (zona juego), que devolveria la camara del jugador.
    if (key[0] == 'C' && strcmp(key, "CurrentCamera") == 0)
        if (ViewportFrame* vf = dynamic_cast<ViewportFrame*>(n)) { wrap_node(L, vf->gl_camara()); return 1; }
    // UIGridLayout.CellSize / CellPadding: UDim2 (la escritura ya existia).
    if (key[0] == 'C' && (strcmp(key, "CellSize") == 0 || strcmp(key, "CellPadding") == 0) &&
        n->is_class("UIGridLayout")) {
        const PackedFloat32Array v = n->call(StringName("get_CellUDim2"));
        if (v.size() == 8 && gl_push_constructor(L, "UDim2")) {
            const int o = key[4] == 'S' ? 0 : 4;
            for (int i = 0; i < 4; i++) lua_pushnumber(L, v[o + i]);
            lua_call(L, 4, 1);
            return 1;
        }
    }
    if ((key[0] == 'S' || key[0] == 'N') && gl_cast<Control>(n)) {
        if (const int tipo = gl_gui_prop_seleccion(key)) {
            if (tipo == 4) {
                const String ok = String("_glo_") + String(key);
                Node* d = n->has_meta(ok)
                    ? gl_cast<Node>(ObjectDB::get_instance(ObjectID((uint64_t)(int64_t)n->get_meta(ok)))) : nullptr;
                wrap_node(L, d);
                return 1;
            }
            const String mk = String("_glp_") + String(key);
            const Variant v = n->has_meta(mk) ? n->get_meta(mk) : Variant();
            if (tipo == 1) lua_pushboolean(L, v.booleanize() ? 1 : 0);
            else if (tipo == 2) lua_pushnumber(L, (double)(int64_t)v);
            else uis_push_enum(L, "SelectionBehavior", (int)(int64_t)v);
            return 1;
        }
    }
    // Lo nuevo de aqui va filtrado por la clave ANTES de mirar la clase: casi
    // toda lectura del puente pasa por esta zona y cast_to no es gratis.
    //
    // UIPadding: cada lado es un UDim (escala + offset), como en Roblox. Antes
    // salia el offset pelado y `relleno.PaddingTop.Scale` reventaba.
    if (key[0] == 'P' && strncmp(key, "Padding", 7) == 0 && n->is_class("UIPadding")) {
        static const char* lados[] = { "PaddingLeft", "PaddingTop", "PaddingRight", "PaddingBottom" };
        for (int i = 0; i < 4; i++) {
            if (strcmp(key, lados[i]) != 0) continue;
            const PackedFloat32Array v = n->call(StringName("get_PadUDim"));
            if (v.size() != 8) break;
            if (!gl_push_constructor(L, "UDim")) { lua_pushnumber(L, v[4 + i]); return 1; }
            lua_pushnumber(L, v[i]);
            lua_pushnumber(L, v[4 + i]);
            lua_call(L, 2, 1);
            return 1;
        }
    }
    // UICorner.CornerRadius y UIListLayout.Padding: un UDim, como en Roblox
    // (antes salia un numero y `esquina.CornerRadius.Scale` reventaba).
    if ((key[0] == 'C' && strcmp(key, "CornerRadius") == 0 && n->is_class("UICorner")) ||
        (key[0] == 'P' && strcmp(key, "Padding") == 0 && n->is_class("UIListLayout"))) {
        static const StringName k_radio("get_RadiusUDim"), k_hueco("get_PaddingUDim");
        const Vector2 u = n->call(key[0] == 'C' ? k_radio : k_hueco);
        if (!gl_push_constructor(L, "UDim")) { lua_pushnumber(L, u.y); return 1; }
        lua_pushnumber(L, u.x);
        lua_pushnumber(L, u.y);
        lua_call(L, 2, 1);
        return 1;
    }
    // La seleccion con mando de Roblox (GuiService.SelectedObject) no existe
    // aqui: senales validas que no saltan.
    if (key[0] == 'S' && (strcmp(key, "SelectionGained") == 0 || strcmp(key, "SelectionLost") == 0) &&
        gl_cast<Control>(n)) {
        _gl_push_inert_signal(L);
        return 1;
    }
    if (key[0] == 'T' && (strcmp(key, "TextXAlignment") == 0 || strcmp(key, "TextYAlignment") == 0 ||
                          strcmp(key, "TextBounds") == 0 || strcmp(key, "TextFits") == 0)) {
        RobloxTextLabel* tl = gl_cast<RobloxTextLabel>(n);
        RobloxTextButton* tbn = tl ? nullptr : gl_cast<RobloxTextButton>(n);
        RobloxTextBox* tbx = (tl || tbn) ? nullptr : gl_cast<RobloxTextBox>(n);
        Control* ctl = gl_cast<Control>(n);
        if (ctl && (tl || tbn || tbx)) {
            // EnumItems: `etiqueta.TextXAlignment.Name` reventaba con el numero.
            if (key[4] == 'X' || key[4] == 'Y') {
                uis_push_enum(L, key, (int)(int64_t)n->get(StringName(key)));
                return 1;
            }
            // TextBounds: lo que ocupa el texto que se ve, con la fuente y el
            // tamano con los que se dibuja (con ajuste de linea si lo hay). Si el
            // ajuste de TextScaled quedo pendiente (estaba oculto), se hace ya.
            if (tl) tl->_gl_refit_si_pendiente();
            else if (tbn) tbn->_gl_refit_si_pendiente();
            else tbx->_gl_refit_si_pendiente();
            const String t = tl ? tl->_gl_display_text() : (tbn ? tbn->_gl_display_text() : tbx->get_text());
            Vector2 m;
            const Ref<Font> f = ctl->get_theme_font(StringName("font"));
            if (f.is_valid() && !t.is_empty()) {
                const bool envuelve = (bool)n->get(StringName("TextWrapped")) || (bool)n->get(StringName("TextScaled"));
                m = f->get_multiline_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, envuelve ? ctl->get_size().x : -1.0f,
                                                 ctl->get_theme_font_size(StringName("font_size")));
            }
            // TextFits: si cabe en la caja con el tamano de letra actual.
            if (key[4] == 'F') {
                const Vector2 caja = ctl->get_size();
                lua_pushboolean(L, m.x <= caja.x + 0.5f && m.y <= caja.y + 0.5f);
                return 1;
            }
            push_vector2(L, m.x, m.y);
            return 1;
        }
    }
    // Font (Enum.Font, la API vieja): lo escrito, o lo que trajo el importador,
    // o Legacy (el de un texto nuevo en Roblox). El numero sale como EnumItem
    // por la conversion del final de godot_object_index_real.
    if (key[0] == 'F' && strcmp(key, "Font") == 0 && gl_cast<Control>(n) &&
        gl_has_property(n, "FontFace")) {
        static const StringName k_escrito("_glp_Font");
        static const StringName k_importado("Font");
        const Variant v = n->has_meta(k_escrito) ? n->get_meta(k_escrito)
                        : n->has_meta(k_importado) ? n->get_meta(k_importado) : Variant(0);
        lua_pushnumber(L, (double)(int64_t)v);
        return 1;
    }
    // FontFace: un Font de verdad (familia, peso y estilo). Antes salia el
    // texto de la familia y `etiqueta.FontFace.Weight.Name` reventaba.
    if (key[0] == 'F' && strcmp(key, "FontFace") == 0 && gl_cast<Control>(n) &&
        gl_has_property(n, "FontFace")) {
        static const StringName k_peso("_gl_font_weight");
        static const StringName k_estilo("_gl_font_style");
        const String familia = n->get(StringName("FontFace"));
        if (gl_push_constructor(L, "Font")) {
            // Sin familia es un texto nuevo: en Roblox nace con LegacyArial (Font.Legacy).
            if (familia.is_empty()) lua_pushstring(L, "rbxasset://fonts/families/LegacyArial.json");
            else lua_pushstring(L, familia.utf8().get_data());
            uis_push_enum(L, "FontWeight", n->has_meta(k_peso) ? (int)(int64_t)n->get_meta(k_peso) : 400);
            uis_push_enum(L, "FontStyle", n->has_meta(k_estilo) ? (int)(int64_t)n->get_meta(k_estilo) : 0);
            lua_call(L, 3, 1);
            return 1;
        }
    }
    // ScrollingFrame: el lienzo y la ventana en pixeles.
    if (key[0] == 'A' && (strcmp(key, "AbsoluteCanvasSize") == 0 || strcmp(key, "AbsoluteWindowSize") == 0)) {
        if (RobloxScrollingFrame* sf = gl_cast<RobloxScrollingFrame>(n)) {
            const Vector2 v = key[8] == 'C' ? sf->gl_lienzo_px() : sf->gl_ventana_px();
            push_vector2(L, v.x, v.y);
            return 1;
        }
    }
    // ── PlayerGui:GetGuiObjectsAtPosition(x, y) ─────────────────────
    // Devuelve los GuiObject visibles bajo ese punto, del MAS ANIDADO al menos
    // (el de encima primero, como Roblox). Lo usa el drag&drop de cualquier
    // inventario custom: sin esto la llamada moria con "not a valid member" y
    // se llevaba por delante el handler entero de soltar el raton — el arrastre
    // no equipaba ni intercambiaba nada, en silencio.
    if ((n->is_class("PlayerGui") || n->is_class("StarterGui")) &&
        strcmp(key, "GetGuiObjectsAtPosition") == 0) {
        lua_pushlightuserdata(L, (void*)n);
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            Node* raiz = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
            const Vector2 punto((float)gl_num(pL, 2), (float)gl_num(pL, 3));
            struct Golpe { Control* c; int prof; };
            std::vector<Golpe> golpes;
            if (raiz) {
                std::function<void(Node*, int)> rec = [&](Node* nd, int prof) {
                    for (int i = 0; i < nd->get_child_count(); i++) {
                        Node* h = nd->get_child(i);
                        if (Control* c = gl_cast<Control>(h)) {
                            if (c->is_visible_in_tree() && c->get_global_rect().has_point(punto)) {
                                // Recortado por un ancestro con ClipsDescendants: no se ve.
                                bool tapado = false;
                                for (Node* p = c->get_parent(); p; p = p->get_parent()) {
                                    Control* pc = gl_cast<Control>(p);
                                    if (!pc) break;
                                    if (pc->is_clipping_contents() && !pc->get_global_rect().has_point(punto)) { tapado = true; break; }
                                }
                                if (!tapado) golpes.push_back({ c, prof });
                            }
                        }
                        rec(h, prof + 1);
                    }
                };
                rec(raiz, 0);
            }
            // Mas anidado primero (orden estable entre iguales).
            for (size_t i = 1; i < golpes.size(); i++) {
                Golpe g = golpes[i];
                size_t j = i;
                while (j > 0 && golpes[j - 1].prof < g.prof) { golpes[j] = golpes[j - 1]; j--; }
                golpes[j] = g;
            }
            lua_newtable(pL);
            for (size_t i = 0; i < golpes.size(); i++) {
                wrap_node(pL, golpes[i].c);
                lua_rawseti(pL, -2, (int)i + 1);
            }
            return 1;
        }, "GetGuiObjectsAtPosition", 1);
        return 1;
    }

    // ── GUI ────────────────────────────────────────────────────────
    ScreenGui* sg = gl_cast<ScreenGui>(n);
    if (sg) {
        if (strcmp(key,"Enabled")       == 0) { lua_pushboolean(L, sg->get_sg_enabled());   return 1; }
        if (strcmp(key,"DisplayOrder")  == 0) { lua_pushnumber(L,  sg->get_display_order()); return 1; }
        if (strcmp(key,"ResetOnSpawn")  == 0) { lua_pushboolean(L, sg->get_reset_on_spawn());return 1; }
        // AbsoluteSize de un ScreenGui = tamaño del viewport (como Roblox); pos = 0,0.
        // Varios scripts leen ScreenGui.AbsoluteSize para escalar su UI.
        if (strcmp(key,"AbsoluteSize")     == 0) {
            Vector2 vp(1280, 720);
            if (sg->get_viewport()) vp = sg->get_viewport()->get_visible_rect().size;
            push_vector2(L, vp.x, vp.y); return 1;
        }
        if (strcmp(key,"AbsolutePosition") == 0) { push_vector2(L, 0, 0); return 1; }
    }

    auto push_udim2_table = [&](const GuiUDim2& d) { gl_push_udim2(L, d.xs, d.xo, d.ys, d.yo); };

    // Propiedades comunes de GUI (Frame, Label, Button, TextBox, ImageLabel, ScrollingFrame)
    // AbsolutePosition / AbsoluteSize (1.15): el rect en PÍXELES ya calculado,
    // común a todo GuiObject. Sirve tanto para leerlo desde scripts como para
    // medir el layout de verdad. Va antes de los bloques por clase (genérico).
    // MouseEnter / MouseLeave: en Roblox las tiene TODO GuiObject. Aqui iban
    // clase por clase (solo los botones de verdad), asi que engancharlas en un
    // TextLabel o un ImageLabel reventaba. Este bloque unico las cubre todas.
    // ── TweenSize / TweenPosition / TweenSizeAndPosition ────────────────
    // La forma CLASICA de animar UI en Roblox (anterior a TweenService, y
    // todavia usadisima: menus, popups, barras). No existian, asi que
    // `frame:TweenSize(...)` tumbaba el script. Se implementan sobre el mismo
    // Tween de Godot, respetando la firma de Roblox:
    //   TweenSize(endSize, easingDirection, easingStyle, time, override, callback)
    if (gl_cast<Control>(n) &&
        (strcmp(key,"TweenSize") == 0 || strcmp(key,"TweenPosition") == 0 ||
         strcmp(key,"TweenSizeAndPosition") == 0)) {
        lua_pushlightuserdata(L, (void*)n);
        lua_pushinteger(L, strcmp(key,"TweenSize") == 0 ? 0
                          : (strcmp(key,"TweenPosition") == 0 ? 1 : 2));
        lua_pushcclosure(L, [](lua_State* pL) -> int {
            Node* nd = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
            const int modo = (int)lua_tointeger(pL, lua_upvalueindex(2));
            if (!nd) { lua_pushboolean(pL, 0); return 1; }
            // arg 1 = self. El primer UDim2 va en 2; con AndPosition, el
            // segundo en 3 y el resto se corre uno.
            int i = 2;
            // OJO: nada de "GuiUDim2 a{}, b{};" aqui. lua_pushcclosure es un
            // MACRO y el preprocesador NO respeta las llaves: una coma a nivel
            // de bloque dentro del lambda le parte los argumentos.
            GuiUDim2 destino_sz{};
            GuiUDim2 destino_pos{};
            auto leer_udim2 = [&](int idx, GuiUDim2& out) -> bool {
                if (!lua_istable(pL, idx)) return false;
                lua_getfield(pL, idx, "X");
                if (lua_istable(pL, -1)) {
                    lua_getfield(pL, -1, "Scale");  out.xs = (float)lua_tonumber(pL, -1); lua_pop(pL, 1);
                    lua_getfield(pL, -1, "Offset"); out.xo = (float)lua_tonumber(pL, -1); lua_pop(pL, 1);
                }
                lua_pop(pL, 1);
                lua_getfield(pL, idx, "Y");
                if (lua_istable(pL, -1)) {
                    lua_getfield(pL, -1, "Scale");  out.ys = (float)lua_tonumber(pL, -1); lua_pop(pL, 1);
                    lua_getfield(pL, -1, "Offset"); out.yo = (float)lua_tonumber(pL, -1); lua_pop(pL, 1);
                }
                lua_pop(pL, 1);
                return true;
            };
            if (modo == 0)      { leer_udim2(i, destino_sz);  i++; }
            else if (modo == 1) { leer_udim2(i, destino_pos); i++; }
            else                { leer_udim2(i, destino_sz); i++; leer_udim2(i, destino_pos); i++; }
            // easingDirection, easingStyle, time (los tres opcionales)
            i += 2;   // las dos curvas: Godot las aplica en el Tween de abajo
            const double dur = lua_isnumber(pL, i) ? lua_tonumber(pL, i) : 1.0;

            // Sin dependencias del TweenService: se interpola con un Tween de
            // Godot sobre las propiedades UDim2 ya existentes del nodo.
            SceneTree* st = gl_cast<SceneTree>(Engine::get_singleton()->get_main_loop());
            if (!st) { lua_pushboolean(pL, 0); return 1; }
            Ref<Tween> tw = st->create_tween();
            if (tw.is_null()) { lua_pushboolean(pL, 0); return 1; }
            tw->set_parallel(true);
            Control* c = gl_cast<Control>(nd);
            if (modo == 0 || modo == 2) {
                const Vector2 area = gl_gui_parent_area(c);
                const Vector2 fin(destino_sz.xs * area.x + destino_sz.xo,
                                  destino_sz.ys * area.y + destino_sz.yo);
                tw->tween_property(c, "size", fin, dur);
            }
            if (modo == 1 || modo == 2) {
                const Vector2 area = gl_gui_parent_area(c);
                const Vector2 fin(destino_pos.xs * area.x + destino_pos.xo,
                                  destino_pos.ys * area.y + destino_pos.yo);
                tw->tween_property(c, "position", fin, dur);
            }
            lua_pushboolean(pL, 1);   // Roblox devuelve si el tween arranco
            return 1;
        }, "TweenSize", 2);
        return 1;
    }

    if (gl_cast<Control>(n) &&
        (strcmp(key,"MouseEnter") == 0 || strcmp(key,"MouseLeave") == 0)) {
        lua_newtable(L);
        lua_pushlightuserdata(L, (void*)n);
        lua_pushboolean(L, strcmp(key,"MouseEnter") == 0 ? 1 : 0);
        lua_pushcclosure(L, [](lua_State* pL)->int{
            Node* nd = (Node*)lua_touserdata(pL, lua_upvalueindex(1));
            const bool entra = lua_toboolean(pL, lua_upvalueindex(2)) != 0;
            int fn = -1;
            for (int ai = 1; ai <= lua_gettop(pL); ai++)
                if (lua_isfunction(pL, ai)) { fn = ai; break; }
            if (fn == -1 || !nd) { lua_pushnil(pL); return 1; }
            lua_State* mL = gl_main_of(pL);
            if (!mL) mL = pL;
            lua_pushvalue(pL, fn);
            int ref = gl_ref(pL, -1); lua_pop(pL, 1);
            if (!gl_gui_add_hover_cb(nd, entra, mL, ref)) { lua_pushnil(pL); return 1; }
            _gl_push_connection(pL, nd, ref);
            return 1;
        }, "Connect", 2);
        lua_setfield(L, -2, "Connect");
        gl_signal_lowercase_aliases(L);
        return 1;
    }

    if (Control* ctrl = gl_cast<Control>(n)) {
        if (strcmp(key,"AbsolutePosition") == 0) { Vector2 p = ctrl->get_global_position(); push_vector2(L, p.x, p.y); return 1; }
        if (strcmp(key,"AbsoluteSize")     == 0) { Vector2 s = ctrl->get_size();            push_vector2(L, s.x, s.y); return 1; }
        // Rotation: comun a todo GuiObject. Se podia ESCRIBIR (el importador la
        // aplica) pero no LEER, asi que guardar la rotacion original de un
        // elemento para animarlo — lo primero que hace cualquier libreria de
        // tweens de UI — moria con "Rotation is not a valid member of Frame".
        if (strcmp(key,"Rotation") == 0) {
            if (ctrl->has_method("get_rbx_rotation")) lua_pushnumber(L, (double)ctrl->call("get_rbx_rotation"));
            else lua_pushnumber(L, Math::rad_to_deg((double)ctrl->get_rotation()));
            return 1;
        }
        // El campo de la clase: el recorte de Godot solo se enciende al entrar
        // en el arbol, y un ScrollingFrame o CanvasGroup nuevo ya recorta.
        if (strcmp(key,"ClipsDescendants") == 0) {
            static const StringName k_get("get_ClipsDescendants");
            lua_pushboolean(L, ctrl->has_method(k_get) ? (bool)ctrl->call(k_get) : ctrl->is_clipping_contents());
            return 1;
        }
    }

    // ── UN SOLO cast para las 7 clases de GUI ────────────────────────
    // En godot-cpp `Object::cast_to` NO es un static_cast: copia un StringName
    // (refcount atomico), pide el tag de la clase al ClassDB del motor POR
    // NOMBRE, llama a object_cast_to, pide el instance binding y encima hace un
    // dynamic_cast con RTTI. Son tres cruces de la frontera GDExtension y un
    // RTTI POR CAST.
    //
    // Aqui abajo venian SIETE casts seguidos a clases de GUI, y los pagaba
    // entera cualquier lectura sobre una Part, un Model o un Humanoid — que es
    // la inmensa mayoria del trafico. Las siete heredan de Control (Panel,
    // Label, Button, Button, LineEdit, TextureRect, Panel), asi que con un solo
    // cast a Control se descartan las siete de golpe: para lo que no es GUI,
    // siete RTTI se convierten en una comparacion contra nullptr.
    Control* _es_gui = gl_cast<Control>(n);

    RobloxFrame* rframe = _es_gui ? gl_cast<RobloxFrame>(n) : nullptr;
    if (rframe) {
        if (strcmp(key,"Size")     == 0) { push_udim2_table(rframe->get_udim2_size()); return 1; }
        if (strcmp(key,"Position") == 0) { push_udim2_table(rframe->get_udim2_pos());  return 1; }
        if (strcmp(key,"BackgroundColor3")      == 0) { push_color3(L, rframe->get_bg_r(), rframe->get_bg_g(), rframe->get_bg_b()); return 1; }
        if (strcmp(key,"BackgroundTransparency")== 0) { lua_pushnumber(L, rframe->get_bg_alpha()); return 1; }
        if (strcmp(key,"Visible")  == 0) { lua_pushboolean(L, rframe->is_visible()); return 1; }
        if (strcmp(key,"ZIndex")   == 0) { lua_pushnumber(L, rframe->get_ZIndex()); return 1; }
        if (strcmp(key,"MouseEnter") == 0 || strcmp(key,"MouseLeave") == 0) {
            lua_newtable(L);
            lua_pushstring(L, key);
            lua_pushlightuserdata(L, (void*)rframe);
            lua_pushcclosure(L, [](lua_State* pL)->int{
                const char* sig = lua_tostring(pL, lua_upvalueindex(1));
                RobloxFrame* f  = (RobloxFrame*)lua_touserdata(pL, lua_upvalueindex(2));
                // Antes esto conectaba la señal a un metodo que NO existe
                // ("_emit_mouse_enter"), guardaba el ref de la funcion de Luau
                // y no lo usaba, y encima devolvia 0 valores. Resultado: el
                // hover de los Frame no llamaba nunca al callback y
                // table.insert(janitor, frame.MouseEnter:Connect(fn)) reventaba.
                int fn_pos = -1;
                for (int ai = 1; ai <= lua_gettop(pL); ai++)
                    if (lua_isfunction(pL, ai)) { fn_pos = ai; break; }
                if (fn_pos == -1 || !f) { lua_pushnil(pL); return 1; }
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1); lua_pop(pL, 1);
                if (String(sig) == String("MouseEnter")) f->add_enter_cb(mL, ref);
                else                                     f->add_leave_cb(mL, ref);
                _gl_push_connection(pL, f, ref);
                return 1;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
    }
    RobloxTextLabel* rtlabel = _es_gui ? gl_cast<RobloxTextLabel>(n) : nullptr;
    if (rtlabel) {
        if (strcmp(key,"Size")     == 0) { push_udim2_table(rtlabel->get_udim2_size()); return 1; }
        if (strcmp(key,"Position") == 0) { push_udim2_table(rtlabel->get_udim2_pos());  return 1; }
        if (strcmp(key,"BackgroundColor3")      == 0) { push_color3(L, rtlabel->get_bg_r(), rtlabel->get_bg_g(), rtlabel->get_bg_b()); return 1; }
        if (strcmp(key,"BackgroundTransparency")== 0) { lua_pushnumber(L, rtlabel->get_bg_alpha()); return 1; }
        // Text es el de Roblox, CON las etiquetas de RichText; ContentText, el
        // que se ve. Antes Text devolvia el limpio.
        if (strcmp(key,"Text")     == 0) { lua_pushstring(L, rtlabel->get_rbx_text().utf8().get_data()); return 1; }
        if (strcmp(key,"ContentText") == 0) { lua_pushstring(L, rtlabel->_gl_display_text().utf8().get_data()); return 1; }
        if (strcmp(key,"Visible")  == 0) { lua_pushboolean(L, rtlabel->is_visible()); return 1; }
        if (strcmp(key,"ZIndex")   == 0) { lua_pushnumber(L, rtlabel->get_ZIndex()); return 1; }
    }
    RobloxTextButton* rtbutton = _es_gui ? gl_cast<RobloxTextButton>(n) : nullptr;
    if (rtbutton) {
        if (strcmp(key,"Size")     == 0) { push_udim2_table(rtbutton->get_udim2_size()); return 1; }
        if (strcmp(key,"Position") == 0) { push_udim2_table(rtbutton->get_udim2_pos());  return 1; }
        if (strcmp(key,"BackgroundColor3")      == 0) { push_color3(L, rtbutton->get_bg_r(), rtbutton->get_bg_g(), rtbutton->get_bg_b()); return 1; }
        if (strcmp(key,"BackgroundTransparency")== 0) { lua_pushnumber(L, rtbutton->get_bg_alpha()); return 1; }
        if (strcmp(key,"Text")     == 0) { lua_pushstring(L, rtbutton->get_rbx_text().utf8().get_data()); return 1; }
        if (strcmp(key,"ContentText") == 0) { lua_pushstring(L, rtbutton->_gl_display_text().utf8().get_data()); return 1; }
        if (strcmp(key,"Visible")  == 0) { lua_pushboolean(L, rtbutton->is_visible()); return 1; }
        if (strcmp(key,"ZIndex")   == 0) { lua_pushnumber(L, rtbutton->get_ZIndex()); return 1; }
        if (strcmp(key,"MouseButton1Click") == 0 || strcmp(key,"Activated") == 0 ||
            strcmp(key,"MouseButton1Down")  == 0 || strcmp(key,"MouseButton1Up") == 0 ||
            strcmp(key,"InputBegan")        == 0 || strcmp(key,"InputEnded") == 0) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)rtbutton);
            // 0 = clic completo, 1 = al apretar, 2 = al soltar. Los tres iban a
            // la MISMA lista y se disparaban juntos: mantener pulsado no existia.
            // 3/4 = InputBegan/InputEnded (con InputObject; ProximityPrompt UI).
            lua_pushinteger(L, strcmp(key,"MouseButton1Down") == 0 ? 1
                             : (strcmp(key,"MouseButton1Up") == 0 ? 2
                             : (strcmp(key,"InputBegan") == 0 ? 3
                             : (strcmp(key,"InputEnded") == 0 ? 4 : 0))));
            lua_pushcclosure(L, [](lua_State* pL)->int{
                RobloxTextButton* btn = (RobloxTextButton*)lua_touserdata(pL, lua_upvalueindex(1));
                const int cual = (int)lua_tointeger(pL, lua_upvalueindex(2));
                int fn_pos = -1;
                for (int ai=1; ai<=lua_gettop(pL); ai++) if (lua_isfunction(pL,ai)) { fn_pos=ai; break; }
                if (fn_pos==-1 || !btn) return 0;
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1); lua_pop(pL,1);
                if      (cual == 1) btn->add_down_cb(mL, ref);
                else if (cual == 2) btn->add_up_cb(mL, ref);
                else if (cual == 3) btn->add_input_began_cb(mL, ref);
                else if (cual == 4) btn->add_input_ended_cb(mL, ref);
                else                btn->add_click_cb(mL, ref);
                _gl_push_connection(pL, btn, ref); return 1;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
        // MouseLeave iba junto a MouseEnter: faltaba, y como esta rama TAPA la
        // generica de Control (que si tiene las dos), cualquier hover de Luau
        // moria con "MouseLeave is not a valid member of RobloxTextButton".
        if (strcmp(key,"MouseEnter") == 0 || strcmp(key,"MouseLeave") == 0) {
            const bool _is_enter = (strcmp(key,"MouseEnter") == 0);
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)rtbutton);
            lua_pushboolean(L, _is_enter ? 1 : 0);
            lua_pushcclosure(L, [](lua_State* pL)->int{
                RobloxTextButton* btn = (RobloxTextButton*)lua_touserdata(pL, lua_upvalueindex(1));
                const bool is_enter = lua_toboolean(pL, lua_upvalueindex(2)) != 0;
                int fn_pos = -1;
                for (int ai=1; ai<=lua_gettop(pL); ai++) if (lua_isfunction(pL,ai)) { fn_pos=ai; break; }
                if (fn_pos==-1 || !btn) return 0;
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1); lua_pop(pL,1);
                if (is_enter) btn->add_enter_cb(mL, ref);
                else          btn->add_leave_cb(mL, ref);
                _gl_push_connection(pL, btn, ref); return 1;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
    }
    RobloxImageButton* rimgbtn = _es_gui ? gl_cast<RobloxImageButton>(n) : nullptr;
    if (rimgbtn) {
        if (strcmp(key,"Size")     == 0) { push_udim2_table(rimgbtn->get_udim2_size()); return 1; }
        if (strcmp(key,"Position") == 0) { push_udim2_table(rimgbtn->get_udim2_pos());  return 1; }
        if (strcmp(key,"BackgroundColor3")      == 0) { push_color3(L, rimgbtn->get_bg_r(), rimgbtn->get_bg_g(), rimgbtn->get_bg_b()); return 1; }
        if (strcmp(key,"BackgroundTransparency")== 0) { lua_pushnumber(L, rimgbtn->get_bg_alpha()); return 1; }
        if (strcmp(key,"Visible")  == 0) { lua_pushboolean(L, rimgbtn->is_visible()); return 1; }
        if (strcmp(key,"ZIndex")   == 0) { lua_pushnumber(L, rimgbtn->get_ZIndex()); return 1; }
        if (strcmp(key,"MouseButton1Click") == 0 || strcmp(key,"Activated") == 0 ||
            strcmp(key,"MouseButton1Down")  == 0 || strcmp(key,"MouseButton1Up") == 0 ||
            strcmp(key,"InputBegan")        == 0 || strcmp(key,"InputEnded") == 0) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)rimgbtn);
            // 0 = clic completo, 1 = al apretar, 2 = al soltar (ver TextButton).
            // 3/4 = InputBegan/InputEnded con InputObject.
            lua_pushinteger(L, strcmp(key,"MouseButton1Down") == 0 ? 1
                             : (strcmp(key,"MouseButton1Up") == 0 ? 2
                             : (strcmp(key,"InputBegan") == 0 ? 3
                             : (strcmp(key,"InputEnded") == 0 ? 4 : 0))));
            lua_pushcclosure(L, [](lua_State* pL)->int{
                RobloxImageButton* btn = (RobloxImageButton*)lua_touserdata(pL, lua_upvalueindex(1));
                const int cual = (int)lua_tointeger(pL, lua_upvalueindex(2));
                int fn_pos = -1;
                for (int ai=1; ai<=lua_gettop(pL); ai++) if (lua_isfunction(pL,ai)) { fn_pos=ai; break; }
                if (fn_pos==-1 || !btn) return 0;
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1); lua_pop(pL,1);
                if      (cual == 1) btn->add_down_cb(mL, ref);
                else if (cual == 2) btn->add_up_cb(mL, ref);
                else if (cual == 3) btn->add_input_began_cb(mL, ref);
                else if (cual == 4) btn->add_input_ended_cb(mL, ref);
                else                btn->add_click_cb(mL, ref);
                _gl_push_connection(pL, btn, ref); return 1;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
        // MouseLeave iba junto a MouseEnter: faltaba, y como esta rama TAPA la
        // generica de Control (que si tiene las dos), cualquier hover de Luau
        // moria con "MouseLeave is not a valid member of RobloxImageButton".
        if (strcmp(key,"MouseEnter") == 0 || strcmp(key,"MouseLeave") == 0) {
            const bool _is_enter = (strcmp(key,"MouseEnter") == 0);
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)rimgbtn);
            lua_pushboolean(L, _is_enter ? 1 : 0);
            lua_pushcclosure(L, [](lua_State* pL)->int{
                RobloxImageButton* btn = (RobloxImageButton*)lua_touserdata(pL, lua_upvalueindex(1));
                const bool is_enter = lua_toboolean(pL, lua_upvalueindex(2)) != 0;
                int fn_pos = -1;
                for (int ai=1; ai<=lua_gettop(pL); ai++) if (lua_isfunction(pL,ai)) { fn_pos=ai; break; }
                if (fn_pos==-1 || !btn) return 0;
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1); lua_pop(pL,1);
                if (is_enter) btn->add_enter_cb(mL, ref);
                else          btn->add_leave_cb(mL, ref);
                _gl_push_connection(pL, btn, ref); return 1;
            }, "Connect", 2);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
    }
    RobloxTextBox* rtbox = _es_gui ? gl_cast<RobloxTextBox>(n) : nullptr;
    if (rtbox) {
        if (strcmp(key,"Size")     == 0) { push_udim2_table(rtbox->get_udim2_size()); return 1; }
        if (strcmp(key,"Position") == 0) { push_udim2_table(rtbox->get_udim2_pos());  return 1; }
        if (strcmp(key,"Text")     == 0) { lua_pushstring(L, rtbox->get_text().utf8().get_data()); return 1; }
        if (strcmp(key,"PlaceholderText") == 0) { lua_pushstring(L, rtbox->get_placeholder().utf8().get_data()); return 1; }
        if (strcmp(key,"Visible")  == 0) { lua_pushboolean(L, rtbox->is_visible()); return 1; }
        // IsFocused(): si el cursor esta escribiendo en esta caja. Lo usan los
        // paneles para no tragarse las teclas del juego mientras se escribe.
        if (strcmp(key,"IsFocused") == 0) {
            lua_pushlightuserdata(L, (void*)rtbox);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxTextBox* tb = (RobloxTextBox*)lua_touserdata(pL, lua_upvalueindex(1));
                lua_pushboolean(pL, tb && tb->has_focus());
                return 1;
            }, "IsFocused", 1);
            return 1;
        }
        // CaptureFocus / ReleaseFocus: poner o quitar el cursor de escritura a
        // mano. Es lo que hace cualquier chat o buscador al abrirse ("escribe
        // ya, sin que el jugador tenga que hacer clic"). Faltaban las dos.
        if (strcmp(key,"CaptureFocus") == 0 || strcmp(key,"ReleaseFocus") == 0) {
            lua_pushlightuserdata(L, (void*)rtbox);
            lua_pushboolean(L, strcmp(key,"CaptureFocus") == 0);
            lua_pushcclosure(L, [](lua_State* pL) -> int {
                RobloxTextBox* tb = (RobloxTextBox*)lua_touserdata(pL, lua_upvalueindex(1));
                const bool coger = lua_toboolean(pL, lua_upvalueindex(2)) != 0;
                if (tb) {
                    if (coger) tb->grab_focus();
                    else       tb->release_focus();
                }
                return 0;
            }, "CaptureFocus", 2);
            return 1;
        }
        if (strcmp(key,"FocusLost") == 0) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void*)rtbox);
            lua_pushcclosure(L, [](lua_State* pL)->int{
                RobloxTextBox* tb = (RobloxTextBox*)lua_touserdata(pL, lua_upvalueindex(1));
                int fn_pos = -1;
                for (int ai=1; ai<=lua_gettop(pL); ai++) if (lua_isfunction(pL,ai)) { fn_pos=ai; break; }
                if (fn_pos==-1 || !tb) return 0;
                lua_State* mL = gl_main_of(pL);
                if (!mL) mL = pL;
                lua_pushvalue(pL, fn_pos);
                int ref = gl_ref(pL, -1); lua_pop(pL,1);
                tb->add_focus_lost_cb(mL, ref);
                _gl_push_connection(pL, tb, ref); return 1;
            }, "Connect", 1);
            lua_setfield(L, -2, "Connect");
            gl_signal_lowercase_aliases(L);
            return 1;
        }
    }
    // ── ScrollingFrame: lecturas basicas ─────────────────────────────
    // No existia NINGUNA rama de lectura para esta clase: leer Position/Size
    // desde Luau daba "not a valid member" (escribir si funcionaba). Lo uso el
    // scroll del menu .luau y cualquier script que anime un ScrollingFrame.
    RobloxScrollingFrame* rscrollr = _es_gui ? gl_cast<RobloxScrollingFrame>(n) : nullptr;
    if (rscrollr) {
        if (strcmp(key,"Size")     == 0) { push_udim2_table(rscrollr->get_udim2_size()); return 1; }
        if (strcmp(key,"Position") == 0) { push_udim2_table(rscrollr->get_udim2_pos());  return 1; }
        if (strcmp(key,"ScrollingEnabled")==0){ lua_pushboolean(L, rscrollr->get_ScrollingEnabled() ? 1 : 0); return 1; }
    }
    RobloxImageLabel* rimage = _es_gui ? gl_cast<RobloxImageLabel>(n) : nullptr;
    if (rimage) {
        if (strcmp(key,"Size")     == 0) { push_udim2_table(rimage->get_udim2_size()); return 1; }
        if (strcmp(key,"Position") == 0) { push_udim2_table(rimage->get_udim2_pos());  return 1; }
        if (strcmp(key,"BackgroundColor3")      == 0) { push_color3(L, rimage->get_bg_r(), rimage->get_bg_g(), rimage->get_bg_b()); return 1; }
        if (strcmp(key,"BackgroundTransparency")== 0) { lua_pushnumber(L, rimage->get_bg_alpha()); return 1; }
        if (strcmp(key,"Visible")  == 0) { lua_pushboolean(L, rimage->is_visible()); return 1; }
        // Se podia ESCRIBIR pero no leer: el script de prompts de Roblox guarda
        // el valor antes de animarlo (`local t = frame.ImageTransparency`) y
        // moria ahi, justo despues de que los prompts empezaran a funcionar.
        if (strcmp(key,"ImageTransparency") == 0) { lua_pushnumber(L, rimage->get_image_transparency()); return 1; }
        if (strcmp(key,"ImageColor3") == 0) {
            const Color c = rimage->get_ImageColor3();
            push_color3(L, c.r, c.g, c.b); return 1;
        }
        if (strcmp(key,"Image") == 0) { lua_pushstring(L, rimage->get_Image().utf8().get_data()); return 1; }
    }

    // ── Respaldo de lectura de las GUI ─────────────────────────────────
    // Cualquier propiedad que la clase tenga ligada con su nombre de Roblox
    // (TextWrapped, RichText, MaxVisibleGraphemes, ScaleType, SliceCenter,
    // ImageRectOffset...). Las GUI no llevan _gl_bridge, asi que no tenian
    // respaldo y esas lecturas no existian desde los scripts.
    if (_es_gui && gl_has_property(n, String(key))) {
        if (gl_variant_to_luau(L, n->get(StringName(key)))) return 1;
    }

    // InputBegan/InputChanged/InputEnded de los GuiObject que no son botones
    // (los botones tienen las suyas arriba), MouseMoved y MouseWheel*: senales
    // validas que todavia no saltan. Enrutar la entrada pide cambiarles el mouse_filter, y eso taparia
    // los clics de los botones que tengan debajo.
    if (_es_gui && ((key[0] == 'I' && (strcmp(key, "InputBegan") == 0 || strcmp(key, "InputChanged") == 0 ||
                                       strcmp(key, "InputEnded") == 0)) ||
                    (key[0] == 'M' && (strcmp(key, "MouseMoved") == 0 || strcmp(key, "MouseWheelForward") == 0 ||
                                       strcmp(key, "MouseWheelBackward") == 0)))) {
        _gl_push_inert_signal(L);
        return 1;
    }

    return -1;
}

#endif // LUAU_API_INDICE_GUI_H
