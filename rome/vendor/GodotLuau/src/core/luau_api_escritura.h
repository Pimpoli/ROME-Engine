#ifndef LUAU_API_ESCRITURA_H
#define LUAU_API_ESCRITURA_H

// Escritura de propiedades desde Luau (__newindex de las instancias), la
// replicacion de lo escrito y el aviso a GetPropertyChangedSignal/Changed.
// Va aparte de luau_api.h (pasaba de 2.000 lineas) y se incluye desde alli,
// despues de la lectura de propiedades.

// Transparencia que se ve en una pieza del rig de avatar, con la regla de Roblox:
// 1 - (1 - Transparency) * (1 - LocalTransparencyModifier). Se pinta con la
// transparencia de la malla (no con visible), para no pelearse con quien la
// oculta por su cuenta (el fundido al reaparecer, la camara).
inline void gl_rig_aplicar_transparencia(Node* n) {
    static const StringName k_t("_glp_Transparency"), k_l("_glp_LocalTransparencyModifier"), k_base("_gl_rig_transp");
    const float t = n->has_meta(k_t) ? (float)n->get_meta(k_t) : (n->has_meta(k_base) ? (float)n->get_meta(k_base) : 0.0f);
    const float l = n->has_meta(k_l) ? (float)n->get_meta(k_l) : 0.0f;
    const float e = CLAMP(1.0f - (1.0f - t) * (1.0f - l), 0.0f, 1.0f);
    for (int i = 0; i < n->get_child_count(true); i++)
        if (GeometryInstance3D* g = gl_cast<GeometryInstance3D>(n->get_child(i, true)))
            if (g->get_transparency() != e) g->set_transparency(e);
}

// ════════════════════════════════════════════════════════════════════
//  godot_object_newindex — __newindex of GodotObject
////
//  godot_object_newindex — __newindex de GodotObject
// ════════════════════════════════════════════════════════════════════
inline int godot_object_newindex_impl(lua_State* L) {
    GL_STACK_GUARD(L);
    GodotObjectWrapper* wrapper = (GodotObjectWrapper*)lua_touserdata(L, 1);
    const char* key = gl_str(L, 2);
    if (!wrapper || !gow_node(wrapper)) return 0;

    // OnIncomingMessage y demas callbacks del chat: arriba, por lo mismo que
    // OnInvoke (los caminos genericos guardarian la funcion como un meta).
    if (gl_chat_newindex(L, gow_node(wrapper), key)) return 0;

    // Filtrados por la clave antes de mirar la clase (cast_to no es gratis y
    // por aqui pasa toda escritura).
    {
        Node* nw = gow_node(wrapper);
        // ImageContent, TextureContent... (gl_puente_editables.h).
        if (gl_clave_content(key) && gl_newindex_content(L, nw, key)) return 0;
        // UIPadding: UDim completo por lado (antes se perdia la escala).
        if (key[0] == 'P' && strncmp(key, "Padding", 7) == 0 && nw->is_class("UIPadding")) {
            static const char* lados[] = { "PaddingLeft", "PaddingTop", "PaddingRight", "PaddingBottom" };
            for (int i = 0; i < 4; i++) {
                if (strcmp(key, lados[i]) != 0) continue;
                if (lua_istable(L, 3))
                    nw->call(StringName("set_gl_pad"), i, gl_lua_campo_num(L, 3, "Scale"), (int)gl_lua_campo_num(L, 3, "Offset"));
                else
                    nw->call(StringName("set_gl_pad"), i, 0.0, (int)gl_num(L, 3));
                return 0;
            }
        }
        // MeshPart.TextureID es como se llama en Roblox (TextureId es el de
        // SpecialMesh). Sin la propiedad (MeshPart de Instance.new), a un meta.
        if ((strcmp(key, "TextureID") == 0 || strcmp(key, "TextureId") == 0) &&
            strcmp(gl_roblox_classname(nw), "MeshPart") == 0) {
            const char* s = lua_tostring(L, 3);
            const String v = String::utf8(s ? s : "");
            if (gl_has_property(nw, "TextureId")) nw->set(StringName("TextureId"), v);
            else nw->set_meta(StringName("_glp_TextureId"), v);
            return 0;
        }
        // Seleccion con mando de los GuiObject (ver gl_gui_prop_seleccion).
        if ((key[0] == 'S' || key[0] == 'N') && gl_cast<Control>(nw)) {
            if (const int tipo = gl_gui_prop_seleccion(key)) {
                if (tipo == 4) {
                    const String ok = String("_glo_") + String(key);
                    Node* dest = _gl_has_metatable(L, 3, "GodotObject")
                        ? gow_node((GodotObjectWrapper*)lua_touserdata(L, 3)) : nullptr;
                    if (dest) nw->set_meta(ok, (int64_t)dest->get_instance_id());
                    else if (nw->has_meta(ok)) nw->remove_meta(ok);
                    return 0;
                }
                const String mk = String("_glp_") + String(key);
                if (tipo == 1) nw->set_meta(mk, lua_toboolean(L, 3) != 0);
                else nw->set_meta(mk, (int64_t)gl_num_or(L, 3, 0));
                return 0;
            }
        }
        static const StringName k_starter_player("StarterPlayer");
        if (nw->get_name() == k_starter_player && strcmp(gl_roblox_classname(nw), "StarterPlayer") == 0 &&
            gl_starterplayer_prop(key)) {
            Variant gv;
            if (gl_luau_to_variant(L, 3, gv)) nw->set_meta(String("_glp_") + String(key), gv);
            return 0;
        }
    }

    // BindableFunction.OnInvoke = function(...) ... end
    // Va EN LO MAS ALTO: mas abajo hay caminos genericos que se lo tragaban
    // (guardaban la funcion como un meta cualquiera) y nunca llegaba aqui, asi
    // que OnInvoke se quedaba en nil e Invoke devolvia nil.
    if (strcmp(key, "OnInvoke") == 0) {
        if (BindableEventNode* bfn = gl_cast<BindableEventNode>(gow_node(wrapper))) {
            if (lua_isfunction(L, 3)) {
                lua_pushvalue(L, 3);
                bfn->inv_ref = gl_ref(L, -1);
                // El PRINCIPAL, no la corrutina: gl_state_alive solo conoce los
                // registrados. Mismo fallo que tenia OnServerInvoke.
                bfn->inv_L = gl_main_of(L);
                lua_pop(L, 1);
            } else {
                bfn->inv_ref = -1;
                bfn->inv_L = nullptr;
            }
            return 0;
        }
    }

    // RemoteFunction.OnServerInvoke / OnClientInvoke — MISMO SITIO Y MISMO
    // MOTIVO que OnInvoke: la rama vivia mas abajo y NUNCA se ejecutaba, porque
    // antes de llegar hay un camino generico que guarda la funcion como un meta
    // cualquiera y devuelve. Resultado: el manejador se quedaba sin registrar y
    // TODO InvokeServer devolvia nil, sin un solo error.
    //
    // Lo que eso rompe en un juego de verdad: un tycoon pide al servidor su
    // dinero, su nivel y los precios con InvokeServer. Si todo vuelve nil, la
    // interfaz se queda con los numeros de diseno ($17.17 M, $99.99 M) y la
    // base no se configura. "Todo corre y nada funciona" — medido en el place.
    if (strcmp(key, "OnServerInvoke") == 0 || strcmp(key, "OnClientInvoke") == 0) {
        if (RemoteFunctionNode* rfn = gl_cast<RemoteFunctionNode>(gow_node(wrapper))) {
            const bool srv = strcmp(key, "OnServerInvoke") == 0;
            if (lua_isfunction(L, 3)) {
                lua_pushvalue(L, 3);
                const int ref = gl_ref(L, -1);
                lua_pop(L, 1);
                // AQUI ESTABA EL FALLO: se guardaba `L`, que es la CORRUTINA en
                // la que corre el script. gl_state_alive() solo conoce los
                // estados REGISTRADOS, que son los principales, asi que daba
                // false y tanto leer el manejador como invocarlo se rendian
                // devolviendo nil. Sin ningun error: InvokeServer devolvia nil
                // SIEMPRE, y con el todo lo que un juego le pide al servidor
                // (dinero, nivel, precios) — de ahi "todo corre y nada
                // funciona". El ref vale igual desde el hilo principal: el
                // registro es compartido por toda la familia de estados.
                lua_State* vm = gl_main_of(L);
                if (srv) rfn->set_server_invoke(vm, ref);
                else     rfn->set_client_invoke(vm, ref);
            } else {
                if (srv) rfn->set_server_invoke(nullptr, LUA_NOREF);
                else     rfn->set_client_invoke(nullptr, LUA_NOREF);
            }
            return 0;
        }
    }

    // Efectos (gl_puente_efectos.h): antes de los caminos de Node3D, que se
    // comian emitter.Rotation = NumberRange.new(...).
    if (gl_efectos_newindex(L, gow_node(wrapper), key)) return 0;

    // Los constraints modernos (AlignPosition, AlignOrientation...) son Node3D,
    // pero su Position/CFrame es el OBJETIVO: va a su propiedad. Antes
    // `align.Position = meta` movia el propio nodo y el objetivo no cambiaba.
    if (strcmp(key, "Position") == 0 || strcmp(key, "CFrame") == 0) {
        static const StringName k_mover("_gl_mover");
        Node* nm = gow_node(wrapper);
        if (nm->has_meta(k_mover) && gl_has_property(nm, String(key))) {
            Variant gv;
            if (gl_luau_to_variant(L, 3, gv)) nm->set(StringName(key), gv);
            return 0;
        }
    }

    // UserInputService.MouseBehavior / MouseIconEnabled. Antes la escritura no
    // llegaba al servicio y el raton no se bloqueaba ni se ocultaba nunca.
    if (UserInputService* uis_w = gl_cast<UserInputService>(gow_node(wrapper))) {
        if (strcmp(key, "MouseBehavior") == 0)    { uis_w->set_mouse_behavior((int)gl_num(L, 3)); return 0; }
        if (strcmp(key, "MouseIconEnabled") == 0) { uis_w->set_mouse_icon_enabled(lua_toboolean(L, 3) != 0); return 0; }
    }

    // Camera: CameraType, CameraSubject, CFrame, FieldOfView, Focus
    // (gl_puente_camara.h). Va antes del CFrame generico de los Node3D.
    if (Camera3D* cam_w = gl_cast<Camera3D>(gow_node(wrapper))) {
        if (gl_camara_newindex(L, cam_w, key)) return 0;
    }

    // Anchored sobre un Node3D que no es Part (ver la lectura mas arriba).
    {
        Node* _n = gow_node(wrapper);
        if (strcmp(key, "Anchored") == 0 && gl_cast<Node3D>(_n) && !gl_has_property(_n, "Anchored")) {
            const bool on = lua_toboolean(L, 3) != 0;
            _n->set_meta("_gl_anchored", on);
            if (PhysicsBody3D* pb = gl_cast<PhysicsBody3D>(_n))
                if (RigidBody3D* rb = gl_cast<RigidBody3D>(pb)) rb->set_freeze_enabled(on);
            return 0;
        }
    }
    Node* n = gow_node(wrapper);

    // Mouse.TargetFilter — subárbol que el rayo ignora (1.14.10 Finish).
    if (RobloxMouse* mo = gl_cast<RobloxMouse>(n)) {
        if (strcmp(key, "TargetFilter") == 0) {
            Node* tf = lua_isnil(L, 3) ? nullptr : _gl_node_from_lua(L, 3);
            mo->target_filter_id = tf ? (uint64_t)tf->get_instance_id() : 0;
            return 0;
        }
    }

    // ── PlayerObject: escrituras de miembros de Player (CameraMode, etc.) ──
    // Antes LocalPlayer ERA el personaje y estas escrituras iban al character;
    // ahora LocalPlayer es el Player. Se aceptan aquí para no romper scripts
    // (p.ej. el CameraModule incluido hace LocalPlayer.CameraMode = mode).
    if (PlayerObject* plr = gl_cast<PlayerObject>(n)) {
        // Enum.CameraMode: 0 Classic, 1 LockFirstPerson. Antes esto se reenviaba a
        // RobloxPlayer::set_camera_mode, que NO es lo mismo: ahi camera_mode es el
        // estilo de seguimiento (Fija/Suave/Combinada) y ademas clampa a 1..3, asi
        // que Classic(0) se convertia en 1 y LockFirstPerson no bloqueaba nada.
        if (strcmp(key, "CameraMode") == 0) {
            plr->camera_mode = (int)gl_num(L, 3);
            Node* ch = plr->get_character();
            if (ch && ch->has_method("set_lock_first_person"))
                ch->call("set_lock_first_person", plr->camera_mode == 1);
            return 0;
        }
        if (strcmp(key, "DisplayName") == 0) { plr->display_name = String(gl_str(L, 3)); return 0; }
        // Team (1.14.10): antes se aceptaba y se DESCARTABA en silencio. Ahora
        // asigna de verdad y el servidor lo difunde (cada máquina construye su
        // propio objeto Player, así que el equipo hay que replicarlo aparte).
        if (strcmp(key, "Team") == 0) {
            Node* t = lua_isnil(L, 3) ? nullptr : _gl_node_from_lua(L, 3);
            plr->team_id = t ? (uint64_t)t->get_instance_id() : 0;
            if (Node* ns = _gl_find_network_service(plr))
                ns->call("net_set_team", (Object*)plr, (Object*)t);
            return 0;
        }
        if (strcmp(key, "Neutral") == 0) {
            if (lua_toboolean(L, 3)) {   // Neutral = true → sin equipo
                plr->team_id = 0;
                if (Node* ns = _gl_find_network_service(plr))
                    ns->call("net_set_team", (Object*)plr, (Object*)nullptr);
            }
            return 0;
        }
        // player.TeamColor = BrickColor.new("Bright red") mete al jugador en el
        // Team de ese color, como Roblox (antes solo guardaba un int en un campo
        // que no leía nadie). Si no hay Team con ese color, se queda el color.
        if (strcmp(key, "TeamColor") == 0) {
            String name = read_brickcolor_name(L, 3);
            if (name.is_empty()) return 0;
            plr->team_color = name;
            Node* t = _gl_find_team_by_color(plr, name);
            plr->team_id = t ? (uint64_t)t->get_instance_id() : 0;
            if (Node* ns = _gl_find_network_service(plr))
                ns->call("net_set_team", (Object*)plr, (Object*)t);
            return 0;
        }
        if (strcmp(key, "CameraMinZoomDistance") == 0 || strcmp(key, "CameraMaxZoomDistance") == 0 ||
            strcmp(key, "AutoJumpEnabled") == 0 || strcmp(key, "DevComputerMovementMode") == 0 ||
            strcmp(key, "DevTouchMovementMode") == 0 || strcmp(key, "DevCameraOcclusionMode") == 0 ||
            strcmp(key, "DevComputerCameraMode") == 0)
            return 0;   // aceptar y descartar (no romper)
    }

    if (strcmp(key, "Archivable") == 0) {
        static const StringName k("_gl_no_archivable");
        if (lua_toboolean(L, 3)) { if (n->has_meta(k)) n->remove_meta(k); }
        else n->set_meta(k, true);
        return 0;
    }

    if (strcmp(key, "Name") == 0) {
        // 33 us por asignacion medidos: set_name valida el nombre y lo compara
        // con TODOS los hermanos para garantizar unicidad. Los scripts reasignan
        // el mismo nombre a menudo (al clonar, al configurar), y ahi no hay nada
        // que hacer.
        const char* nn = gl_str(L, 3);
        gl_set_rbx_name(n, String(nn));
        return 0;
    }

    if (strcmp(key, "Parent") == 0) {
        GLAutTiempo _aut_parent(AUT_PARENT);
        // Durante el flush de física (handler de Touched) no se puede tocar el
        // árbol: el cambio entero se difiere al final del frame (mismo orden:
        // remove y luego add, la cola de deferred es FIFO).
        const bool defer_fis = gl_en_callback_fisica();
        if (lua_isnil(L, 3)) {
            if (Node* p = n->get_parent()) {
                if (defer_fis) p->call_deferred("remove_child", n);
                else p->remove_child(n);
            }
        } else {
            GodotObjectWrapper* p_wrap = (GodotObjectWrapper*)lua_touserdata(L, 3);
            if (p_wrap && gow_node(p_wrap) && gow_node(p_wrap) != n) {
                if (Node* p = n->get_parent()) {
                    if (defer_fis) p->call_deferred("remove_child", n);
                    else p->remove_child(n);
                }
                Node* nuevo_padre = gow_node(p_wrap);
                if (defer_fis) {
                    // El add_child llega luego: el nombre de Roblox se apunta ya
                    // por si Godot lo renombra al meterlo.
                    if (!n->has_meta(GL_META_RBX_NAME)) n->set_meta(GL_META_RBX_NAME, String(n->get_name()));
                    nuevo_padre->call_deferred("add_child", n);
                } else {
                    gl_add_child_rbx(nuevo_padre, n);
                    // Set owner so the node is visible/saveable in the editor
                    if (n->is_inside_tree()) {
                        Node* scene_root = n->get_tree()->get_edited_scene_root();
                        if (!scene_root) scene_root = (Node*)n->get_tree()->get_root(); // <-- ADDED (Node*) / AÑADIDO (Node*)
                        if (!n->get_owner()) n->set_owner(scene_root);
                    }
                    // EQUIPAR/DESEQUIPAR POR .Parent: en Roblox mover una Tool
                    // al personaje ES equiparla (suelda el Handle a la mano y
                    // dispara Equipped) y sacarla de ahi, desequiparla. Es el
                    // idioma mas usado, mas incluso que Humanoid:EquipTool.
                    if (RobloxTool* rt = gl_cast<RobloxTool>(n)) {
                        const bool a_personaje = nuevo_padre->get_node_or_null(NodePath("Humanoid")) != nullptr;
                        if (a_personaje) {
                            gl_tool_agarrar(nuevo_padre, n);
                            rt->equip(nullptr);
                        } else {
                            gl_tool_soltar(n);
                            rt->unequip();
                        }
                    }
                }
            }
        }
        return 0;
    }

    // OJO: estos handlers 3D solo deben CONSUMIR la escritura si el nodo es un
    // Node3D. Antes hacían `return 0` incondicional, así que una GUI (Control 2D)
    // que hacía `label.Position = UDim2(...)` entraba aquí, no hacía nada y se
    // TRAGABA la escritura: la GUI se quedaba en (0,0) — el clásico "todo
    // amontonado arriba-izquierda". Si no es Node3D, se cae al bloque de GUI.
    // HumanoidRootPart del jugador: escribir su velocidad empuja al personaje
    // (lo que hace un dash o un knockback). Despues lo frena el propio andar.
    if (strcmp(key, "AssemblyLinearVelocity") == 0 || strcmp(key, "Velocity") == 0) {
        if (Node3D* pers = gl_character_root_of_hrp(n)) {
            CharacterBody3D* cb = gl_cast<CharacterBody3D>(pers);
            if (cb && !gl_cast<RobloxPart>(n)) {
                if (LuauVector3* v = check_vector3(L, 3)) cb->set_velocity(Vector3(v->x, v->y, v->z));
                return 0;
            }
        }
    }

    // Attachment: Position y CFrame son LOCALES (respecto a su pieza), como en
    // Roblox; WorldPosition y WorldCFrame, las del mundo. Antes todo iba al
    // mundo: `att.Position = Vector3.new(0, 2, 0)` lo mandaba al origen.
    if (Attachment* at = gl_cast<Attachment>(n)) {
        const bool mundo = strcmp(key, "WorldPosition") == 0 || strcmp(key, "WorldCFrame") == 0;
        if (strcmp(key, "Position") == 0 || strcmp(key, "WorldPosition") == 0) {
            if (LuauVector3* vec = check_vector3(L, 3)) {
                const Vector3 v(vec->x, vec->y, vec->z);
                if (mundo && at->is_inside_tree()) at->set_global_position(v); else at->set_position(v);
            }
            return 0;
        }
        if ((strcmp(key, "CFrame") == 0 || strcmp(key, "WorldCFrame") == 0) && lua_istable(L, 3)) {
            const Transform3D t = _gl_read_cframe_tbl(L, 3);
            if (mundo && at->is_inside_tree()) at->set_global_transform(t); else at->set_transform(t);
            return 0;
        }
    }

    if (strcmp(key, "Position") == 0 && gl_cast<Node3D>(n)) {
        LuauVector3* vec = check_vector3(L, 3);
        Node3D* n3d = gl_cast<Node3D>(n);
        // Si es el HumanoidRootPart, mueve al personaje entero (como Roblox).
        if (vec && n3d) gl_set_part_global_position(n3d, Vector3(vec->x, vec->y, vec->z));
        return 0;
    }

    if (strcmp(key, "Rotation") == 0 && gl_cast<Node3D>(n)) {
        LuauVector3* vec = check_vector3(L, 3);
        Node3D* n3d = gl_cast<Node3D>(n);
        if (vec && n3d) {
            if (n3d->is_inside_tree()) n3d->set_global_rotation_degrees(Vector3(vec->x, vec->y, vec->z));
            else n3d->set_rotation_degrees(Vector3(vec->x, vec->y, vec->z));
        }
        return 0;
    }

    if (strcmp(key, "CFrame") == 0 && lua_istable(L, 3) && gl_cast<Node3D>(n)) {
        Node3D* n3d_cf = gl_cast<Node3D>(n);
        if (n3d_cf) {
            lua_getfield(L, 3, "X"); float tx = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "Y"); float ty = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "Z"); float tz = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "m00"); float m00 = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "m01"); float m01 = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "m02"); float m02 = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "m10"); float m10 = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "m11"); float m11 = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "m12"); float m12 = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "m20"); float m20 = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "m21"); float m21 = (float)lua_tonumber(L,-1); lua_pop(L,1);
            lua_getfield(L, 3, "m22"); float m22 = (float)lua_tonumber(L,-1); lua_pop(L,1);
            Transform3D t;
            t.origin = Vector3(tx, ty, tz);
            t.basis.rows[0] = Vector3(m00, m01, m02);
            t.basis.rows[1] = Vector3(m10, m11, m12);
            t.basis.rows[2] = Vector3(m20, m21, m22);
            // Si es el HumanoidRootPart, mueve al personaje entero (como Roblox).
            gl_set_part_global_transform(n3d_cf, t);
        }
        return 0;
    }

    if (strcmp(key, "CameraMode") == 0) {
        RobloxPlayer* rp = gl_cast<RobloxPlayer>(n);
        if (rp) rp->set_camera_mode((int)gl_num(L, 3));
        RobloxPlayer2D* rp2d = gl_cast<RobloxPlayer2D>(n);
        if (rp2d) rp2d->set_camera_mode((int)gl_num(L, 3));
        return 0;
    }
    if (strcmp(key, "MouseSensitivity") == 0) {
        RobloxPlayer* rp = gl_cast<RobloxPlayer>(n);
        if (rp) rp->set_mouse_sensitivity((float)gl_num(L, 3));
        return 0;
    }

    if (strcmp(key, "MovementType") == 0) {
        RobloxPlayer2D* rp2d = gl_cast<RobloxPlayer2D>(n);
        if (rp2d) rp2d->set_movement_type((int)gl_num(L, 3));
        return 0;
    }

    Humanoid* hum = gl_cast<Humanoid>(n);
    if (hum) {
        if (strcmp(key, "Health")               == 0) { hum->set_health((float)gl_num(L, 3));               return 0; }
        if (strcmp(key, "MaxHealth")            == 0) { hum->set_max_health((float)gl_num(L, 3));            return 0; }
        if (strcmp(key, "WalkSpeed")            == 0) { hum->set_walk_speed((float)gl_num(L, 3));            return 0; }
        if (strcmp(key, "JumpPower")            == 0) { hum->set_jump_power((float)gl_num(L, 3));            return 0; }
        if (strcmp(key, "JumpHeight")           == 0) { hum->set_jump_height((float)gl_num(L, 3));           return 0; }
        if (strcmp(key, "Jump")                 == 0) { hum->set_jump_request(lua_toboolean(L, 3) != 0);     return 0; }
        if (strcmp(key, "UseJumpPower")         == 0) { hum->set_use_jump_power(lua_toboolean(L, 3) != 0);   return 0; }
        if (strcmp(key, "HipHeight")            == 0) { hum->set_hip_height((float)gl_num(L, 3));            return 0; }
        if (strcmp(key, "Gravity")              == 0) { hum->set_gravity_val((float)gl_num(L, 3));           return 0; }
        if (strcmp(key, "AutoRotate")           == 0) { hum->set_auto_rotate(lua_toboolean(L, 3) != 0);                return 0; }
        if (strcmp(key, "AutoJumpEnabled")      == 0) { hum->set_auto_jump_enabled(lua_toboolean(L, 3) != 0);          return 0; }
        if (strcmp(key, "DisplayName")          == 0) { hum->set_display_name(String(gl_str(L, 3)));         return 0; }
        if (strcmp(key, "NameDisplayDistance")  == 0) { hum->set_name_display_distance((float)gl_num(L, 3)); return 0; }
        if (strcmp(key, "DisplayDistanceType")  == 0) { hum->set_display_distance_type((int)gl_num(L, 3));   return 0; }
        if (strcmp(key, "HealthDisplayType")    == 0) { hum->set_health_display_type((int)gl_num(L, 3));     return 0; }
        if (strcmp(key, "HealthDisplayDistance")== 0) { hum->set_health_display_distance((float)gl_num(L,3));return 0; }
        if (strcmp(key, "RigType")              == 0) { hum->set_rig_type((int)gl_num(L, 3));                return 0; }
        if (strcmp(key, "EvaluateStateMachine") == 0) { hum->set_evaluate_state_machine(lua_toboolean(L, 3) != 0);    return 0; }
        if (strcmp(key, "BreakJointsOnDeath")   == 0) { hum->set_break_joints_on_death(lua_toboolean(L, 3) != 0);     return 0; }
        if (strcmp(key, "RequiresNeck")         == 0) { hum->set_requires_neck(lua_toboolean(L, 3) != 0);              return 0; }
        // Sit: true se sienta (sin asiento, en el sitio); false se levanta y
        // suelta el asiento.
        if (strcmp(key, "Sit") == 0) {
            if (lua_toboolean(L, 3)) { if (!hum->gl_asiento()) hum->change_state(6); }
            else hum->gl_levantar(false);
            return 0;
        }
        if (strcmp(key, "PlatformStand")        == 0) { hum->set_platform_stand(lua_toboolean(L, 3) != 0);  return 0; }
        // WalkToPoint / WalkToPart son el destino del MoveTo, como en Roblox.
        if (strcmp(key, "WalkToPoint") == 0) {
            if (LuauVector3* v = check_vector3(L, 3)) {
                Node3D* pz = hum->gl_moveto_pieza();
                const Vector3 p(v->x, v->y, v->z);
                hum->gl_move_to(pz ? pz->get_global_transform().xform(p) : p, pz);
            }
            return 0;
        }
        if (strcmp(key, "WalkToPart") == 0) {
            Node3D* pz = nullptr;
            if (_gl_has_metatable(L, 3, "GodotObject"))
                pz = gl_cast<Node3D>(gow_node((GodotObjectWrapper*)lua_touserdata(L, 3)));
            if (pz && pz->is_inside_tree()) hum->gl_move_to(pz->get_global_position(), pz);   // hasta la pieza
            else hum->stop_npc_movement();
            return 0;
        }
    }

    Humanoid2D* hum2d = gl_cast<Humanoid2D>(n);
    if (hum2d) {
        if (strcmp(key, "Health")    == 0) { hum2d->set_health((float)gl_num(L, 3));    return 0; }
        if (strcmp(key, "MaxHealth") == 0) { hum2d->set_max_health((float)gl_num(L, 3)); return 0; }
        // WalkSpeed/JumpPower delegate to the parent RobloxPlayer2D (movement lives in the player) / delegan al RobloxPlayer2D padre (movimiento está en el player)
        if (strcmp(key, "WalkSpeed") == 0) {
            RobloxPlayer2D* pp = gl_cast<RobloxPlayer2D>(n->get_parent());
            if (pp) pp->set_walk_speed((float)gl_num(L, 3));
            return 0;
        }
        if (strcmp(key, "JumpPower") == 0) {
            RobloxPlayer2D* pp = gl_cast<RobloxPlayer2D>(n->get_parent());
            // Convertir a negativo internamente (en 2D Y crece hacia abajo)
            if (pp) pp->set_jump_power(-(float)gl_num(L, 3));
            return 0;
        }
    }

    RobloxPart* part = gl_cast<RobloxPart>(n);
    if (part) {
        if (strcmp(key, "Anchored")         == 0) { part->set_anchored(lua_toboolean(L,3)!=0);                   return 0; }
        if (strcmp(key, "CanCollide")       == 0) { part->set_can_collide(lua_toboolean(L,3)!=0);                return 0; }
        if (strcmp(key, "CanTouch")         == 0) { part->set_can_touch(lua_toboolean(L,3)!=0);                  return 0; }
        if (strcmp(key, "CanQuery")         == 0) { part->set_can_query(lua_toboolean(L,3)!=0);                  return 0; }
        if (strcmp(key, "CollisionGroup")   == 0) { part->set_collision_group(String(gl_str(L,3)));   return 0; }
        if (strcmp(key, "CastShadow")       == 0) { part->set_cast_shadow(lua_toboolean(L,3)!=0);                return 0; }
        if (strcmp(key, "Locked")           == 0) { part->set_locked(lua_toboolean(L,3)!=0);                     return 0; }
        if (strcmp(key, "Massless")         == 0) { part->set_massless(lua_toboolean(L,3)!=0);                   return 0; }
        if (strcmp(key, "Transparency")     == 0) { part->set_transparency((float)gl_num(L,3));        return 0; }
        if (strcmp(key, "Reflectance")      == 0) { part->set_reflectance((float)gl_num(L,3));         return 0; }
        if (strcmp(key, "Material")         == 0) { part->set_roblox_material((int)gl_num(L,3));       return 0; }
        if (strcmp(key, "MaterialVariant")  == 0) {
            const char* s = lua_tostring(L, 3);
            const String nuevo = String::utf8(s ? s : "");
            static const StringName k_mv("MaterialVariant");
            const bool cambia = !part->has_meta(k_mv) || String(part->get_meta(k_mv)) != nuevo;
            part->set_meta(k_mv, nuevo);
            // Se dibuja ya con la textura de la variante (gl_variantes.h).
            if (cambia) part->gl_refrescar_material();
            return 0;
        }
        if (strcmp(key, "Shape")            == 0) { part->set_roblox_shape(gl_shape_roblox((int)gl_num(L,3))); return 0; }
        if (strcmp(key, "Density")          == 0) { part->set_density((float)gl_num(L,3));             return 0; }
        if (strcmp(key, "Friction")         == 0) { part->set_friction_val((float)gl_num(L,3));        return 0; }
        if (strcmp(key, "Elasticity")       == 0) { part->set_elasticity((float)gl_num(L,3));          return 0; }
        if (strcmp(key, "FrictionWeight")   == 0) { part->set_friction_weight((float)gl_num(L,3));     return 0; }
        if (strcmp(key, "ElasticityWeight") == 0) { part->set_elasticity_weight((float)gl_num(L,3));   return 0; }
        if (strcmp(key, "TopSurface")       == 0) { part->set_top_surface((int)gl_num(L,3));           return 0; }
        if (strcmp(key, "BottomSurface")    == 0) { part->set_bottom_surface((int)gl_num(L,3));        return 0; }
        if (strcmp(key, "FrontSurface")     == 0) { part->set_front_surface((int)gl_num(L,3));         return 0; }
        if (strcmp(key, "BackSurface")      == 0) { part->set_back_surface((int)gl_num(L,3));          return 0; }
        if (strcmp(key, "LeftSurface")      == 0) { part->set_left_surface((int)gl_num(L,3));          return 0; }
        if (strcmp(key, "RightSurface")     == 0) { part->set_right_surface((int)gl_num(L,3));         return 0; }
        if (strcmp(key, "Color") == 0 || strcmp(key, "BrickColor") == 0) {
            LuauColor3* col = check_color3(L, 3);
            if (col) part->set_color(Color(col->r, col->g, col->b));
            return 0;
        }
        if (strcmp(key, "Size") == 0) {
            LuauVector3* vec = check_vector3(L, 3);
            if (vec) part->set_size(Vector3(vec->x, vec->y, vec->z));
            return 0;
        }
        if (strcmp(key, "Position") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) gl_set_part_global_position(part, Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key, "Orientation") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) part->set_rotation_degrees(Vector3(v->x, v->y, v->z));
            return 0;
        }
        // Escribir la velocidad del conjunto la escribe en la RAIZ, que es el
        // cuerpo de verdad: escribirsela a un miembro congelado no hacia nada.
        if (strcmp(key, "Velocity") == 0 || strcmp(key, "AssemblyLinearVelocity") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) gl_raiz_de(part)->set_linear_velocity(Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key, "AngularVelocity") == 0 || strcmp(key, "AssemblyAngularVelocity") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) gl_raiz_de(part)->set_angular_velocity(Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key, "RootPriority") == 0) {
            part->set_root_priority((int)gl_num(L, 3));
            return 0;
        }
        // Seat / VehicleSeat: su estado va en metas (ver gl_es_asiento).
        if (gl_es_asiento(part)) {
            if (strcmp(key, "Disabled") == 0) {
                const bool d = lua_toboolean(L, 3) != 0;
                part->set_meta("Disabled", d);
                if (d) if (Humanoid* h = gl_cast<Humanoid>(gl_ocupante_de(part))) h->gl_levantar(false);
                return 0;
            }
            if (gl_es_asiento_vehiculo(part)) {
                if (strcmp(key, "Throttle") == 0 || strcmp(key, "ThrottleFloat") == 0) { part->set_meta("_gl_throttle", gl_num(L, 3)); gl_fire_prop_changed(part, "Throttle"); return 0; }
                if (strcmp(key, "Steer") == 0    || strcmp(key, "SteerFloat") == 0)    { part->set_meta("_gl_steer", gl_num(L, 3));    gl_fire_prop_changed(part, "Steer");    return 0; }
                if (strcmp(key, "MaxSpeed") == 0 || strcmp(key, "Torque") == 0 || strcmp(key, "TurnSpeed") == 0) {
                    part->set_meta(StringName(key), gl_num(L, 3));
                    return 0;
                }
                if (strcmp(key, "HeadsUpDisplay") == 0) { part->set_meta("HeadsUpDisplay", lua_toboolean(L, 3) ? 1.0 : 0.0); return 0; }
            }
        }
    }

    // ── RobloxWorkspace write ─────────────────────────────────────
    RobloxWorkspace* ws = gl_cast<RobloxWorkspace>(n);
    if (ws) {
        if (strcmp(key, "Gravity")                   == 0) { ws->set_gravity((float)gl_num(L,3));                      return 0; }
        if (strcmp(key, "FallenPartsDestroyHeight")  == 0) { ws->set_fallen_parts_destroy_height((float)gl_num(L,3));  return 0; }
        if (strcmp(key, "AutoFallHeight")            == 0) { ws->set_auto_fall_height(lua_toboolean(L,3)!=0);                    return 0; }
        if (strcmp(key, "FallHeightMargin")          == 0) { ws->set_fall_height_margin((float)gl_num(L,3));          return 0; }
        if (strcmp(key, "StreamingEnabled")          == 0) { ws->set_streaming_enabled(lua_toboolean(L,3)!=0);                   return 0; }
        if (strcmp(key, "AirDensity")                == 0) { ws->set_air_density((float)gl_num(L,3));                  return 0; }
        if (strcmp(key, "TouchesUseCollisionGroups") == 0) { ws->set_touches_use_collision_groups(lua_toboolean(L,3)!=0);        return 0; }
    }

    // ── RobloxPlayer write ────────────────────────────────────────
    RobloxPlayer* rp_w = gl_cast<RobloxPlayer>(n);
    if (rp_w) {
        if (strcmp(key, "UserId")                  == 0) { return 0; }   // inmutable: lo asigna el servidor (host=1, 2,3,...)
        if (strcmp(key, "DisplayName")             == 0) { rp_w->set_display_name(String(gl_str(L,3)));            return 0; }
        if (strcmp(key, "AccountAge")              == 0) { rp_w->set_account_age((int)gl_num(L,3));                return 0; }
        if (strcmp(key, "AutoJumpEnabled")         == 0) { rp_w->set_auto_jump_enabled(lua_toboolean(L,3)!=0);               return 0; }
        if (strcmp(key, "DevCameraOcclusionMode")  == 0) { rp_w->set_dev_camera_occlusion_mode((int)gl_num(L,3));  return 0; }
        if (strcmp(key, "DevComputerMovementMode") == 0) { rp_w->set_dev_computer_movement_mode((int)gl_num(L,3)); return 0; }
        if (strcmp(key, "DevTouchMovementMode")    == 0) { rp_w->set_dev_touch_movement_mode((int)gl_num(L,3));    return 0; }
        if (strcmp(key, "TeamColor") == 0) {
            LuauColor3* col = check_color3(L, 3);
            if (col) rp_w->set_team_color(Color(col->r, col->g, col->b));
            return 0;
        }
        // Móvil (1.15): el jugador (o el SettingsModule) puede modificar el joystick.
        if (strcmp(key, "JoystickDynamic") == 0) { rp_w->set_joystick_dynamic(lua_toboolean(L,3)!=0);   return 0; }
        if (strcmp(key, "JoystickRadius")  == 0) { rp_w->set_joystick_radius((float)gl_num(L,3)); return 0; }
        if (strcmp(key, "JoystickOpacity") == 0) { rp_w->set_joystick_opacity((float)gl_num(L,3)); return 0; }
    }

    Lighting* light = gl_cast<Lighting>(n);
    if (light) {
        if (strcmp(key, "Brightness")               == 0) { light->set_brightness((float)gl_num(L,3));          return 0; }
        if (strcmp(key, "ClockTime")                == 0) { light->set_clock_time((float)gl_num(L,3));          return 0; }
        if (strcmp(key, "GeographicLatitude")       == 0) { light->set_geographic_latitude((float)gl_num(L,3)); return 0; }
        if (strcmp(key, "GlobalShadows")            == 0) { light->set_global_shadows(lua_toboolean(L,3) != 0);          return 0; }
        if (strcmp(key, "ShadowSoftness")           == 0) { light->set_shadow_softness((float)gl_num(L,3));    return 0; }
        if (strcmp(key, "FogDensity")               == 0) { light->set_fog_density((float)gl_num(L,3));        return 0; }
        if (strcmp(key, "FogStart")                 == 0) { light->set_fog_start((float)gl_num(L,3));          return 0; }
        if (strcmp(key, "FogEnd")                   == 0) { light->set_fog_end((float)gl_num(L,3));            return 0; }
        if (strcmp(key, "ExposureCompensation")     == 0) { light->set_exposure_comp((float)gl_num(L,3));      return 0; }
        if (strcmp(key, "EnvironmentDiffuseScale")  == 0) { light->set_env_diffuse_scale((float)gl_num(L,3));  return 0; }
        if (strcmp(key, "EnvironmentSpecularScale") == 0) { light->set_env_specular_scale((float)gl_num(L,3)); return 0; }
        if (strcmp(key, "Technology")               == 0) { light->set_technology((int)gl_num(L,3)); return 0; }
        if (strcmp(key, "Ambient")                  == 0) { LuauColor3* c = check_color3(L, 3); if(c) light->set_ambient(Color(c->r,c->g,c->b)); return 0; }
        if (strcmp(key, "OutdoorAmbient")           == 0) { LuauColor3* c = check_color3(L, 3); if(c) light->set_outdoor_ambient(Color(c->r,c->g,c->b)); return 0; }
        if (strcmp(key, "FogColor")                 == 0) { LuauColor3* c = check_color3(L, 3); if(c) light->set_fog_color(Color(c->r,c->g,c->b)); return 0; }
        if (strcmp(key, "ColorShift_Top")           == 0) { LuauColor3* c = check_color3(L, 3); if(c) light->set_color_shift_top(Color(c->r,c->g,c->b)); return 0; }
        if (strcmp(key, "ColorShift_Bottom")        == 0) { LuauColor3* c = check_color3(L, 3); if(c) light->set_color_shift_bottom(Color(c->r,c->g,c->b)); return 0; }
        if (strcmp(key, "TimeOfDay")                == 0) { light->set_time_of_day(String(gl_str(L,3)));         return 0; }
        if (strcmp(key, "DayNightCycle")            == 0) { light->set_day_night_cycle(lua_toboolean(L,3) != 0);           return 0; }
        if (strcmp(key, "DayLengthMinutes")         == 0) { light->set_day_length_minutes((float)gl_num(L,3));   return 0; }
    }

    // ── BodyMovers — newindex ──────────────────────────────────────
    BodyVelocity* bv_w = gl_cast<BodyVelocity>(n);
    if (bv_w) {
        if (strcmp(key,"Velocity") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) bv_w->set_velocity(Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key,"MaxForce") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) bv_w->set_max_force(Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key,"P") == 0) { bv_w->set_p((float)gl_num(L,3)); return 0; }
    }
    BodyPosition* bpos_w = gl_cast<BodyPosition>(n);
    if (bpos_w) {
        if (strcmp(key,"Position") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) bpos_w->set_position_val(Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key,"MaxForce") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) bpos_w->set_max_force(Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key,"P") == 0) { bpos_w->set_p((float)gl_num(L,3)); return 0; }
        if (strcmp(key,"D") == 0) { bpos_w->set_d((float)gl_num(L,3)); return 0; }
    }
    BodyForce* bf_w = gl_cast<BodyForce>(n);
    if (bf_w) {
        if (strcmp(key,"Force") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) bf_w->set_force(Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key,"RelativeTo") == 0) { bf_w->set_relative_to(lua_toboolean(L,3)!=0); return 0; }
    }
    BodyAngularVelocity* bav_w = gl_cast<BodyAngularVelocity>(n);
    if (bav_w) {
        if (strcmp(key,"AngularVelocity") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) bav_w->set_angular_velocity(Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key,"MaxTorque") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) bav_w->set_max_torque(Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key,"P") == 0) { bav_w->set_p((float)gl_num(L,3)); return 0; }
    }
    BodyGyro* bgyro_w = gl_cast<BodyGyro>(n);
    if (bgyro_w) {
        if (strcmp(key,"MaxTorque") == 0) {
            LuauVector3* v = check_vector3(L, 3);
            if (v) bgyro_w->set_max_torque(Vector3(v->x, v->y, v->z));
            return 0;
        }
        if (strcmp(key,"P") == 0) { bgyro_w->set_p((float)gl_num(L,3)); return 0; }
        if (strcmp(key,"D") == 0) { bgyro_w->set_d((float)gl_num(L,3)); return 0; }
        if (strcmp(key,"CFrame") == 0) {
            // CFrame es una TABLA en este motor (no userdata). Aceptamos:
            //  - un nodo (GodotObject) → usa su transform global
            //  - una tabla CFrame con componentes m00..m22 + X/Y/Z
            if (_gl_has_metatable(L, 3, "GodotObject")) {
                GodotObjectWrapper* wr = (GodotObjectWrapper*)lua_touserdata(L, 3);
                Node3D* n3d = gl_cast<Node3D>(gow_node(wr));
                if (n3d) bgyro_w->set_cframe(gl_node_xf(n3d));
            } else if (lua_istable(L, 3)) {
                auto gf = [&](const char* k) -> float {
                    lua_getfield(L, 3, k); float r = (float)lua_tonumber(L, -1); lua_pop(L, 1); return r;
                };
                Transform3D t;
                t.origin = Vector3(gf("X"), gf("Y"), gf("Z"));
                t.basis.rows[0] = Vector3(gf("m00"), gf("m01"), gf("m02"));
                t.basis.rows[1] = Vector3(gf("m10"), gf("m11"), gf("m12"));
                t.basis.rows[2] = Vector3(gf("m20"), gf("m21"), gf("m22"));
                bgyro_w->set_cframe(t);
            }
            return 0;
        }
    }

    // ── Constraints — newindex ─────────────────────────────────────
    WeldConstraint* weld_w = gl_cast<WeldConstraint>(n);
    if (weld_w) {
        if (strcmp(key,"Part0") == 0) {
            GodotObjectWrapper* pw = (GodotObjectWrapper*)lua_touserdata(L,3);
            if (pw) weld_w->set_part0(gow_node(pw));
            return 0;
        }
        if (strcmp(key,"Part1") == 0) {
            GodotObjectWrapper* pw = (GodotObjectWrapper*)lua_touserdata(L,3);
            if (pw) weld_w->set_part1(gow_node(pw));
            return 0;
        }
        if (strcmp(key,"Enabled") == 0) { weld_w->set_enabled(lua_toboolean(L,3)!=0); return 0; }
    }
    if (Weld* wd_w = gl_cast<Weld>(n)) {
        if (strcmp(key,"Part0") == 0 || strcmp(key,"Part1") == 0) {
            Node* p = _gl_has_metatable(L, 3, "GodotObject") ? gow_node((GodotObjectWrapper*)lua_touserdata(L, 3)) : nullptr;
            if (key[4] == '0') wd_w->set_part0(p); else wd_w->set_part1(p);
            return 0;
        }
        if ((strcmp(key,"C0") == 0 || strcmp(key,"C1") == 0) && lua_istable(L, 3)) {
            const Transform3D t = _gl_read_cframe_tbl(L, 3);
            if (key[1] == '0') wd_w->set_c0(t); else wd_w->set_c1(t);
            return 0;
        }
        if (strcmp(key,"Enabled") == 0) { wd_w->set_enabled(lua_toboolean(L,3)!=0); return 0; }
    }
    HingeConstraint* hinge_w = gl_cast<HingeConstraint>(n);
    if (hinge_w) {
        if (strcmp(key,"Enabled")             == 0) { hinge_w->set_enabled(lua_toboolean(L,3)!=0);               return 0; }
        if (strcmp(key,"LimitsEnabled")       == 0) { hinge_w->set_limits_enabled(lua_toboolean(L,3)!=0);        return 0; }
        if (strcmp(key,"UpperAngle")          == 0) { hinge_w->set_upper_angle((float)gl_num(L,3));    return 0; }
        if (strcmp(key,"LowerAngle")          == 0) { hinge_w->set_lower_angle((float)gl_num(L,3));    return 0; }
        if (strcmp(key,"MotorEnabled")        == 0) { hinge_w->set_motor_enabled(lua_toboolean(L,3)!=0);         return 0; }
        if (strcmp(key,"MotorMaxTorque")      == 0) { hinge_w->set_motor_max_torque((float)gl_num(L,3)); return 0; }
        if (strcmp(key,"MotorTargetVelocity") == 0) { hinge_w->set_angular_velocity((float)gl_num(L,3)); return 0; }   // nombre viejo
        if (strcmp(key,"Part0") == 0) { GodotObjectWrapper* pw=(GodotObjectWrapper*)lua_touserdata(L,3); if(pw) hinge_w->set_part0(gow_node(pw)); return 0; }
        if (strcmp(key,"Part1") == 0) { GodotObjectWrapper* pw=(GodotObjectWrapper*)lua_touserdata(L,3); if(pw) hinge_w->set_part1(gow_node(pw)); return 0; }
    }
    RodConstraint* rod_w = gl_cast<RodConstraint>(n);
    if (rod_w) {
        if (strcmp(key,"Enabled") == 0) { rod_w->set_enabled(lua_toboolean(L,3)!=0);          return 0; }
        if (strcmp(key,"Length")  == 0) { rod_w->set_length((float)gl_num(L,3));    return 0; }
        if (strcmp(key,"Part0")   == 0) { GodotObjectWrapper* pw=(GodotObjectWrapper*)lua_touserdata(L,3); if(pw) rod_w->set_part0(gow_node(pw)); return 0; }
        if (strcmp(key,"Part1")   == 0) { GodotObjectWrapper* pw=(GodotObjectWrapper*)lua_touserdata(L,3); if(pw) rod_w->set_part1(gow_node(pw)); return 0; }
    }
    SpringConstraint* spring_w = gl_cast<SpringConstraint>(n);
    if (spring_w) {
        if (strcmp(key,"Enabled")    == 0) { spring_w->set_enabled(lua_toboolean(L,3)!=0);            return 0; }
        if (strcmp(key,"FreeLength") == 0) { spring_w->set_free_length((float)gl_num(L,3)); return 0; }
        if (strcmp(key,"Stiffness")  == 0) { spring_w->set_stiffness((float)gl_num(L,3));   return 0; }
        if (strcmp(key,"Damping")    == 0) { spring_w->set_damping((float)gl_num(L,3));     return 0; }
        if (strcmp(key,"Part0") == 0) { GodotObjectWrapper* pw=(GodotObjectWrapper*)lua_touserdata(L,3); if(pw) spring_w->set_part0(gow_node(pw)); return 0; }
        if (strcmp(key,"Part1") == 0) { GodotObjectWrapper* pw=(GodotObjectWrapper*)lua_touserdata(L,3); if(pw) spring_w->set_part1(gow_node(pw)); return 0; }
    }

    // ── GUI — newindex ─────────────────────────────────────────────
    // Helper: leer UDim2 de tabla Lua {X={Scale,Offset}, Y={Scale,Offset}}
    auto read_udim2_lua = [&]() -> GuiUDim2 {
        GuiUDim2 d;
        if (!lua_istable(L, 3)) return d;
        lua_getfield(L, 3, "X");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "Scale");  d.xs = lua_isnumber(L,-1)?(float)lua_tonumber(L,-1):0; lua_pop(L,1);
            lua_getfield(L, -1, "Offset"); d.xo = lua_isnumber(L,-1)?(float)lua_tonumber(L,-1):0; lua_pop(L,1);
        } lua_pop(L,1);
        lua_getfield(L, 3, "Y");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "Scale");  d.ys = lua_isnumber(L,-1)?(float)lua_tonumber(L,-1):0; lua_pop(L,1);
            lua_getfield(L, -1, "Offset"); d.yo = lua_isnumber(L,-1)?(float)lua_tonumber(L,-1):0; lua_pop(L,1);
        } lua_pop(L,1);
        return d;
    };

    ScreenGui* sg_w = gl_cast<ScreenGui>(n);
    if (sg_w) {
        if (strcmp(key,"Enabled")       == 0) { sg_w->set_sg_enabled(lua_toboolean(L,3)!=0);          return 0; }
        if (strcmp(key,"DisplayOrder")  == 0) { sg_w->set_display_order((int)gl_num(L,3));   return 0; }
        if (strcmp(key,"ResetOnSpawn")  == 0) { sg_w->set_reset_on_spawn(lua_toboolean(L,3)!=0);       return 0; }
    }

    // Rotation de GuiObject: una sola rama para TODOS los Control, porque las
    // clases de GUI no la exponen por separado. gl_apply_rbx_gui_extra ya sabe
    // colocar el pivote en el centro, como Roblox.
    if (Control* ctrl_w = gl_cast<Control>(n)) {
        if (strcmp(key, "Rotation") == 0) {
            gl_apply_rbx_gui_extra(ctrl_w, "Rotation", (float)gl_num(L, 3));
            return 0;
        }
    }

    // Aplicar propiedades comunes de GUI para los tipos de GUI
    // ZIndex va por set_ZIndex, NO por el set_z_index crudo de Godot. El crudo
    // rechaza cualquier valor fuera de +-4096 (ERR_FAIL_COND) y no guarda el
    // valor de Roblox, asi que el idioma tipico de Roblox para "siempre encima"
    // (ZIndex = 999999) no aplicaba NADA y al releerlo salia 0.
    #define GUI_NEWINDEX_COMMON(cast_type, var_name) \
        cast_type* var_name = gl_cast<cast_type>(n); \
        if (var_name) { \
            if (strcmp(key,"Visible")   == 0) { var_name->set_visible(lua_toboolean(L,3)!=0); return 0; } \
            if (strcmp(key,"ZIndex")    == 0) { var_name->set_ZIndex((int)gl_num(L,3)); return 0; } \
            if (strcmp(key,"Size")      == 0) { GuiUDim2 d=read_udim2_lua(); var_name->set_udim2_size(d.xs,d.xo,d.ys,d.yo); return 0; } \
            if (strcmp(key,"Position")  == 0) { GuiUDim2 d=read_udim2_lua(); var_name->set_udim2_pos(d.xs,d.xo,d.ys,d.yo);  return 0; } \
            if (strcmp(key,"AnchorPoint")==0 && lua_istable(L,3)) { \
                lua_getfield(L,3,"X"); float ax = lua_isnumber(L,-1)?(float)lua_tonumber(L,-1):0; lua_pop(L,1); \
                lua_getfield(L,3,"Y"); float ay = lua_isnumber(L,-1)?(float)lua_tonumber(L,-1):0; lua_pop(L,1); \
                var_name->set_anchor_point(ax,ay); return 0; \
            } \
            if (strcmp(key,"BackgroundColor3")==0) { LuauColor3* c=check_color3(L, 3); if(c) var_name->set_bg_color(c->r,c->g,c->b); return 0; } \
            if (strcmp(key,"BackgroundTransparency")==0) { var_name->set_bg_alpha((float)gl_num(L,3)); return 0; } \
            if (strcmp(key,"BorderColor3")==0) { LuauColor3* c=check_color3(L, 3); if(c) var_name->set_border_color(c->r,c->g,c->b); return 0; } \
            if (strcmp(key,"BorderSizePixel")==0) { var_name->set_border_px((int)gl_num(L,3)); return 0; } \
            if (strcmp(key,"ClipsDescendants")==0) { var_name->set(StringName("ClipsDescendants"), lua_toboolean(L,3)!=0); return 0; } \
        }
    // ── ViewportFrame (gl_viewport_frame.h): CurrentCamera y el retrato del
    // menu. Lo demas es de un Frame (abajo) o del respaldo de las GUI.
    if (strcmp(key, "CurrentCamera") == 0 || strcmp(key, "Subject") == 0) {
        if (ViewportFrame* vfr = dynamic_cast<ViewportFrame*>(n)) {
            if (key[0] == 'C') vfr->gl_set_camara(_gl_node_from_lua(L, 3));
            else               vfr->set_subject(_gl_node_from_lua(L, 3));
            return 0;
        }
    }
    GUI_NEWINDEX_COMMON(RobloxFrame, rframe_w)
    GUI_NEWINDEX_COMMON(RobloxTextLabel, rtlabel_w)
    if (rtlabel_w) {
        if (strcmp(key,"Text")==0){ rtlabel_w->set_rbx_text(String::utf8(gl_str(L,3))); return 0; }
        if (strcmp(key,"TextColor3")==0)  { LuauColor3* c=check_color3(L, 3); if(c) rtlabel_w->set_text_color(c->r,c->g,c->b); return 0; }
        if (strcmp(key,"TextSize")==0)    { rtlabel_w->set_text_size((int)gl_num(L,3)); return 0; }
        if (strcmp(key,"TextScaled")==0)  { rtlabel_w->set_text_scaled(lua_toboolean(L,3)!=0); return 0; }
        // Estas tres se tragaban en silencio: no hay respaldo generico de
        // escritura para las clases GUI (solo lo tienen las _gl_bridge), asi
        // que `label.TextTransparency = 1` no hacia NADA — aunque leerlo si
        // funcionaba y devolvia el valor viejo. Rompia sombras de texto,
        // atenuados de la hotbar y flujos de dialogo que comparan contra 1.
        if (strcmp(key,"TextTransparency")==0)       { rtlabel_w->set_TextTransparency((float)gl_num(L,3));       return 0; }
        if (strcmp(key,"TextStrokeTransparency")==0) { gl_apply_rbx_gui_extra(rtlabel_w, "TextStrokeTransparency", (double)gl_num(L,3)); return 0; }
        if (strcmp(key,"TextStrokeColor3")==0)       { LuauColor3* c=check_color3(L,3); if(c) gl_apply_rbx_gui_extra(rtlabel_w, "TextStrokeColor3", Color(c->r,c->g,c->b)); return 0; }
        // Por el setter de la clase: guarda _tx/_ty_align (el getter deja de
        // mentir) y aplica el orden REAL de Roblox (Left=0, Right=1, Center=2
        // — aqui estaba mapeado a mano con Right y Center intercambiados).
        if (strcmp(key,"TextXAlignment")==0) { rtlabel_w->set_TextXAlignment((int)gl_num(L,3)); return 0; }
        if (strcmp(key,"TextYAlignment")==0) { rtlabel_w->set_TextYAlignment((int)gl_num(L,3)); return 0; }
    }
    GUI_NEWINDEX_COMMON(RobloxTextButton, rtbutton_w)
    if (rtbutton_w) {
        if (strcmp(key,"Text")==0){ rtbutton_w->set_rbx_text(String::utf8(gl_str(L,3))); return 0; }
        if (strcmp(key,"TextColor3")==0) { LuauColor3* c=check_color3(L, 3); if(c) rtbutton_w->set_text_color(c->r,c->g,c->b); return 0; }
        if (strcmp(key,"TextSize")==0)   { rtbutton_w->set_text_size((int)gl_num(L,3)); return 0; }
        // Estas tres se tragaban en silencio: no hay respaldo generico de
        // escritura para las clases GUI (solo lo tienen las _gl_bridge), asi
        // que `label.TextTransparency = 1` no hacia NADA — aunque leerlo si
        // funcionaba y devolvia el valor viejo. Rompia sombras de texto,
        // atenuados de la hotbar y flujos de dialogo que comparan contra 1.
        if (strcmp(key,"TextTransparency")==0)       { rtbutton_w->set_TextTransparency((float)gl_num(L,3));       return 0; }
        if (strcmp(key,"TextStrokeTransparency")==0) { gl_apply_rbx_gui_extra(rtbutton_w, "TextStrokeTransparency", (double)gl_num(L,3)); return 0; }
        if (strcmp(key,"TextStrokeColor3")==0)       { LuauColor3* c=check_color3(L,3); if(c) gl_apply_rbx_gui_extra(rtbutton_w, "TextStrokeColor3", Color(c->r,c->g,c->b)); return 0; }
    }
    GUI_NEWINDEX_COMMON(RobloxTextBox, rtbox_w)
    if (rtbox_w) {
        if (strcmp(key,"Text")==0)           { rtbox_w->set_text(String::utf8(gl_str(L,3)));             return 0; }
        if (strcmp(key,"PlaceholderText")==0){ rtbox_w->set_placeholder(String(gl_str(L,3))); return 0; }
        if (strcmp(key,"ClearTextOnFocus")==0){ rtbox_w->set_select_all_on_focus(lua_toboolean(L,3)!=0);     return 0; }
    }
    GUI_NEWINDEX_COMMON(RobloxImageLabel, rimage_w)
    if (rimage_w) {
        if (strcmp(key,"Image")==0)            { rimage_w->set_image(String(gl_str(L,3)));        return 0; }
        if (strcmp(key,"ImageColor3")==0)      { LuauColor3* c=check_color3(L, 3); if(c) rimage_w->set_image_color(c->r,c->g,c->b); return 0; }
        if (strcmp(key,"ImageTransparency")==0){ rimage_w->set_image_transparency((float)gl_num(L,3)); return 0; }
    }
    GUI_NEWINDEX_COMMON(RobloxScrollingFrame, rscroll_w)
    if (rscroll_w) {
        if (strcmp(key,"ScrollingEnabled")==0){ rscroll_w->set_ScrollingEnabled(lua_toboolean(L,3)!=0); return 0; }
    }
    // ImageButton faltaba en esta lista. No hereda de ImageLabel (las dos salen
    // de clases DISTINTAS de Godot: Button y TextureRect), asi que ninguna otra
    // rama lo cazaba y sus escrituras caian al camino generico. Ese cubre casi
    // todo, pero no ZIndex — y un HUD de Roblox va lleno de ImageButton.
    GUI_NEWINDEX_COMMON(RobloxImageButton, rimgbtn_w)
    if (rimgbtn_w) {
        if (strcmp(key,"Image")==0)            { rimgbtn_w->set_image(String(gl_str(L,3))); return 0; }
        if (strcmp(key,"ImageColor3")==0)      { LuauColor3* c=check_color3(L, 3); if(c) rimgbtn_w->set_image_color(c->r,c->g,c->b); return 0; }
        if (strcmp(key,"ImageTransparency")==0){ rimgbtn_w->set_image_transparency((float)gl_num(L,3)); return 0; }
        if (strcmp(key,"HoverImage")==0)       { rimgbtn_w->set_HoverImage(String(gl_str(L,3)));   return 0; }
        if (strcmp(key,"PressedImage")==0)     { rimgbtn_w->set_PressedImage(String(gl_str(L,3))); return 0; }
        if (strcmp(key,"AutoButtonColor")==0)  { rimgbtn_w->set_AutoButtonColor(lua_toboolean(L,3)!=0); return 0; }
    }
    #undef GUI_NEWINDEX_COMMON

    // (OnServerInvoke / OnClientInvoke se atienden EN LO MAS ALTO de esta
    //  funcion: aqui abajo ya no llegaban. Ver la nota de alli.)

    // ── Sound (gl_puente_sonido.h) ──
    if (RobloxSound* rsound_w = gl_cast<RobloxSound>(n)) {
        if (gl_sonido_newindex(L, rsound_w, key)) return 0;
    }

    // ── ClickDetector newindex ─────────────────────────────────────────
    ClickDetector* cd_w = gl_cast<ClickDetector>(n);
    if (cd_w) {
        if (strcmp(key,"MaxActivationDistance")==0){ cd_w->set_max_distance((float)gl_num(L,3)); return 0; }
    }

    // ── ProximityPrompt newindex ───────────────────────────────────────
    ProximityPrompt* pp_w = gl_cast<ProximityPrompt>(n);
    if (pp_w) {
        if (strcmp(key,"ActionText")            == 0) { pp_w->set_action_text(String(gl_str(L,3)));  return 0; }
        if (strcmp(key,"KeyboardKeyCode")       == 0) {
            // Enum.KeyCode llega como NUMERO; un texto ("E") tambien vale.
            if (lua_type(L, 3) == LUA_TNUMBER) pp_w->set_key_code_num((int)lua_tonumber(L, 3));
            else                               pp_w->set_key_code(String(gl_str(L, 3)));
            return 0;
        }
        if (strcmp(key,"Style")                 == 0) { pp_w->set_style((int)gl_num(L,3));           return 0; }
        if (strcmp(key,"MaxActivationDistance") == 0) { pp_w->set_max_distance((float)gl_num(L,3));  return 0; }
        if (strcmp(key,"HoldDuration")          == 0) { pp_w->set_hold_duration((float)gl_num(L,3)); return 0; }
        if (strcmp(key,"Enabled")               == 0) { pp_w->set_enabled(lua_toboolean(L,3)!=0);              return 0; }
        if (strcmp(key,"ClickablePrompt")       == 0) { pp_w->set_clickable_prompt(lua_toboolean(L,3)!=0);     return 0; }
        if (strcmp(key,"ObjectText")            == 0) { pp_w->set_object_text(String(gl_str(L,3)));            return 0; }
        if (strcmp(key,"RequiresLineOfSight")   == 0) { pp_w->set_requires_line_of_sight(lua_toboolean(L,3)!=0); return 0; }
        if (strcmp(key,"GamepadKeyCode")        == 0) { pp_w->set_gamepad_key_num((int)gl_num(L,3));           return 0; }
        if (strcmp(key,"Exclusivity")           == 0) { pp_w->set_exclusivity((int)gl_num(L,3));               return 0; }
        if (strcmp(key,"AutoLocalize")          == 0) { pp_w->set_auto_localize(lua_toboolean(L,3)!=0);        return 0; }
        if (strcmp(key,"UIOffset")              == 0) {
            // Vector2 (el stdlib lo hace tabla), o un numero suelto por comodidad.
            if (lua_istable(L,3)) {
                lua_getfield(L,3,"Y");
                pp_w->set_ui_offset_y(lua_isnumber(L,-1) ? (float)lua_tonumber(L,-1) : 0.0f);
                lua_pop(L,1);
            } else if (lua_isnumber(L,3)) {
                pp_w->set_ui_offset_y((float)lua_tonumber(L,3));
            }
            return 0;
        }
    }

    // ── Model newindex (RBXModel importado) ────────────────────────────
    if (n->is_class("RBXModel")) {
        if (strcmp(key, "PrimaryPart") == 0) {
            if (lua_isnil(L, 3)) { n->set("PrimaryPart", NodePath()); return 0; }
            GodotObjectWrapper* w2 = (GodotObjectWrapper*)lua_touserdata(L, 3);
            Node* t = w2 ? gow_node(w2) : nullptr;
            if (t) n->set("PrimaryPart", n->get_path_to(t));
            return 0;
        }
        if (strcmp(key, "WorldPivot") == 0 && lua_istable(L, 3)) {
            n->set("WorldPivot", _gl_read_cframe_tbl(L, 3));
            return 0;
        }
    }

    // ── SpawnLocation newindex ─────────────────────────────────────────
    SpawnLocation* spwn_w = gl_cast<SpawnLocation>(n);
    if (spwn_w) {
        if (strcmp(key,"Enabled")  == 0) { spwn_w->set_enabled(lua_toboolean(L,3)!=0);           return 0; }
        if (strcmp(key,"Duration") == 0) { spwn_w->set_duration((float)gl_num(L,3));    return 0; }
        if (strcmp(key,"Neutral")  == 0) { spwn_w->set_neutral(lua_toboolean(L,3)!=0);            return 0; }
        if (strcmp(key,"AllowTeamChangeOnTouch") == 0) { spwn_w->set_allow_team_change(lua_toboolean(L,3)!=0); return 0; }
        if (strcmp(key,"TeamColor") == 0) {
            String name = read_brickcolor_name(L, 3);
            if (!name.is_empty()) spwn_w->set_team_color(name);
            return 0;
        }
    }

    // ── Team (1.15): acepta BrickColor / nombre / número ───────────────
    if (n->is_class("Team")) {
        if (strcmp(key,"TeamColor") == 0) {
            String name = read_brickcolor_name(L, 3);
            if (!name.is_empty()) n->set("TeamColor", name);
            return 0;
        }
    }

    // ── Players (1.15): ajustes del respawn automático ─────────────────
    if (Players* players_w = gl_cast<Players>(n)) {
        if (strcmp(key,"RespawnTime") == 0) { players_w->set_respawn_time(gl_num(L,3)); return 0; }
        if (strcmp(key,"CharacterAutoLoads") == 0) { players_w->set_character_auto_loads(lua_toboolean(L,3)!=0); return 0; }
        if (strcmp(key,"MaxPlayers") == 0) { players_w->set_max_players((int)gl_num(L,3)); return 0; }
    }

    // ── BillboardGui newindex ─────────────────────────────────────────
    BillboardGui* bbgui_w = gl_cast<BillboardGui>(n);
    if (bbgui_w) {
        if (strcmp(key,"Enabled")     == 0) { bbgui_w->set_enabled(lua_toboolean(L,3)!=0);           return 0; }
        if (strcmp(key,"AlwaysOnTop") == 0) { bbgui_w->set_always_on_top(lua_toboolean(L,3)!=0);     return 0; }
        if (strcmp(key,"MaxDistance") == 0) { bbgui_w->set_max_distance((float)gl_num(L,3)); return 0; }
        if (strcmp(key,"DistanceLowerLimit") == 0) { bbgui_w->set_min_distance((float)gl_num(L,3)); return 0; }
        if (strcmp(key,"StudsOffset") == 0) {
            if (LuauVector3* sv = check_vector3(L, 3))
                bbgui_w->set_studs_offset(Vector3(sv->x, sv->y, sv->z));
            return 0;
        }
        // Adornee: el cartel se pega a ESE objeto y no a su padre. Antes ni
        // existia y escribirlo mataba el script del juego.
        if (strcmp(key,"Adornee") == 0) {
            if (lua_isnil(L,3)) { bbgui_w->set_adornee(nullptr); return 0; }
            GodotObjectWrapper* aw = (GodotObjectWrapper*)lua_touserdata(L,3);
            if (aw) bbgui_w->set_adornee(gow_node(aw));
            return 0;
        }
    }

    // ── Motor6D newindex ──────────────────────────────────────────────
    Motor6D* m6d_w = gl_cast<Motor6D>(n);
    if (m6d_w) {
        if (strcmp(key,"DesiredAngle") == 0) { m6d_w->set_desired_angle((float)gl_num(L,3)); return 0; }
        if (strcmp(key,"MaxVelocity")  == 0) { m6d_w->set_max_velocity((float)gl_num(L,3));  return 0; }
        if (strcmp(key,"Enabled")      == 0) { m6d_w->set_enabled(lua_toboolean(L,3)!=0);               return 0; }
        if (strcmp(key,"CurrentAngle") == 0) { m6d_w->set_current_angle((float)gl_num(L,3)); return 0; }
        // Part0/Part1: una pieza o nil (la quita).
        if (strcmp(key,"Part0") == 0 || strcmp(key,"Part1") == 0) {
            Node* p = _gl_has_metatable(L, 3, "GodotObject") ? gow_node((GodotObjectWrapper*)lua_touserdata(L,3)) : nullptr;
            if (key[4] == '0') m6d_w->set_part0(p); else m6d_w->set_part1(p);
            return 0;
        }
        // C0, C1 y Transform: CFrame. Antes C1 y Transform se perdian en silencio.
        if ((strcmp(key,"C0") == 0 || strcmp(key,"C1") == 0 || strcmp(key,"Transform") == 0) && lua_istable(L,3)) {
            const Transform3D t = _gl_read_cframe_tbl(L, 3);
            if (key[0] == 'T') m6d_w->set_transform(t);
            else if (key[1] == '0') m6d_w->set_c0(t);
            else m6d_w->set_c1(t);
            return 0;
        }
    }

    // ── RobloxTool newindex ───────────────────────────────────────────
    RobloxTool* rtool_w = gl_cast<RobloxTool>(n);
    if (rtool_w) {
        if (strcmp(key,"ToolTip")      == 0) { rtool_w->set_tool_tip(String(gl_str(L,3)));  return 0; }
        if (strcmp(key,"CanBeDropped") == 0) { rtool_w->set_can_be_dropped(lua_toboolean(L,3)!=0);    return 0; }
        // Enabled: lo escriben los inventarios custom de los places reales; el
        // camino generico de escritura solo cubre clases _gl_bridge y esto se
        // descartaba en silencio.
        if (strcmp(key,"Enabled")      == 0) { rtool_w->set_tool_enabled(lua_toboolean(L,3)!=0);      return 0; }
        if (strcmp(key,"RequiresHandle")== 0){ rtool_w->set_requires_handle(lua_toboolean(L,3)!=0);   return 0; }
    }

    // ── AnimationTrack (gl_puente_animacion.h) ─────────────────────────
    if (AnimationTrack* pista_w = gl_cast<AnimationTrack>(n))
        if (gl_animacion_newindex(L, pista_w, key)) return 0;

    // ── AnimationObject newindex ──────────────────────────────────────
    AnimationObject* aobj_w = gl_cast<AnimationObject>(n);
    if (aobj_w) {
        if (strcmp(key,"AnimationId") == 0) { aobj_w->set_animation_id(String(gl_str(L,3))); return 0; }
    }

    // ── UIGradient.Color = ColorSequence / Transparency = NumberSequence ──
    // Todas las paradas, no solo la primera y la ultima: un degradado de tres
    // colores perdia el del medio, y la Transparency (la mascara de fundido de
    // casi toda UI) ni se podia escribir. Un Color3 o un numero solos valen
    // como secuencia constante, igual que en Roblox.
    if (n->is_class("UIGradient") && (strcmp(key, "Color") == 0 || strcmp(key, "Transparency") == 0)) {
        const bool es_color = key[0] == 'C';
        PackedFloat32Array tiempos, valores;
        PackedColorArray colores;
        if (es_color && check_color3(L, 3)) {
            LuauColor3* c = check_color3(L, 3);
            tiempos.push_back(0.0f); tiempos.push_back(1.0f);
            colores.push_back(Color(c->r, c->g, c->b)); colores.push_back(Color(c->r, c->g, c->b));
        } else if (!es_color && lua_isnumber(L, 3)) {
            const float v = (float)lua_tonumber(L, 3);
            tiempos.push_back(0.0f); tiempos.push_back(1.0f);
            valores.push_back(v); valores.push_back(v);
        } else if (lua_istable(L, 3)) {
            lua_getfield(L, 3, "Keypoints");
            if (lua_istable(L, -1)) {
                const int nkp = (int)lua_objlen(L, -1);
                for (int i = 1; i <= nkp; i++) {
                    lua_rawgeti(L, -1, i);
                    if (lua_istable(L, -1)) {
                        lua_getfield(L, -1, "Time");
                        const float t = (float)lua_tonumber(L, -1);
                        lua_pop(L, 1);
                        lua_getfield(L, -1, "Value");
                        if (es_color) {
                            if (LuauColor3* c = check_color3(L, -1)) {
                                tiempos.push_back(t);
                                colores.push_back(Color(c->r, c->g, c->b));
                            }
                        } else if (lua_isnumber(L, -1)) {
                            tiempos.push_back(t);
                            valores.push_back((float)lua_tonumber(L, -1));
                        }
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
        if (es_color && colores.size() >= 2) n->call("set_gl_stops", tiempos, colores);
        else if (!es_color && valores.size() >= 2) n->call("set_gl_alpha", tiempos, valores);
        return 0;
    }

    // ── UIGridLayout.CellSize / CellPadding (UDim2) ─────────────────────
    // En Roblox las dos son UDim2. Aqui la clase las guarda en cuatro floats
    // (set_gl_cell_size / set_gl_cell_padding) y el IMPORTADOR ya las mapeaba,
    // pero desde un SCRIPT no habia camino: `grid.CellSize = UDim2.new(0,80,0,80)`
    // se tragaba en silencio (el puente generico no sabe leer un UDim2) y la
    // rejilla se quedaba con sus 100x100 de fabrica. Medido con probe_layouts.
    if ((strcmp(key, "CellSize") == 0 || strcmp(key, "CellPadding") == 0) && lua_istable(L, 3)) {
        const char* metodo = (strcmp(key, "CellSize") == 0) ? "set_gl_cell_size"
                                                            : "set_gl_cell_padding";
        if (n->has_method(metodo)) {
            // UDim2 = { X = {Scale, Offset}, Y = {Scale, Offset} }
            float xs = 0, xo = 0, ys = 0, yo = 0;
            lua_getfield(L, 3, "X");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "Scale");  xs = (float)lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "Offset"); xo = (float)lua_tonumber(L, -1); lua_pop(L, 1);
            }
            lua_pop(L, 1);
            lua_getfield(L, 3, "Y");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "Scale");  ys = (float)lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "Offset"); yo = (float)lua_tonumber(L, -1); lua_pop(L, 1);
            }
            lua_pop(L, 1);
            n->call(metodo, xs, xo, ys, yo);
            if (n->has_method("_gl_invalidate")) n->call("_gl_invalidate");
            return 0;
        }
    }

    // ── FontFace: la API NUEVA de fuentes de Roblox ─────────────────────
    // Es una tabla Font (ver Font.new en el stdlib), no un string, asi que el
    // camino generico no la sabia guardar y `label.FontFace = Font.new(...)`
    // reventaba. Se guarda la familia, que es lo que identifica la fuente.
    if (strcmp(key, "FontFace") == 0 && lua_istable(L, 3)) {
        lua_getfield(L, 3, "Family");
        if (lua_isstring(L, -1) && gl_has_property(n, "FontFace"))
            n->set("FontFace", String(lua_tostring(L, -1)));
        lua_pop(L, 1);
        // Peso y estilo, para devolver el mismo Font al leerlo.
        n->set_meta(StringName("_gl_font_weight"), gl_lua_campo_enum(L, 3, "Weight", 400));
        n->set_meta(StringName("_gl_font_style"), gl_lua_campo_enum(L, 3, "Style", 0));
        // Y se dibuja con ella (gl_fuentes.h).
        if (Control* c = gl_cast<Control>(n)) if (c->is_inside_tree()) gl_aplicar_fuente(c);
        return 0;
    }

    // ── Font: la API VIEJA de fuentes (Enum.Font) ─────────────────────
    // La usan muchos juegos y se perdia: no habia propiedad, y leerla daba
    // "Font is not a valid member". Se guarda el valor y se pone la FontFace
    // equivalente (la que da Font.fromEnum en Roblox) para las familias
    // principales, asi FontFace y Font cuentan lo mismo.
    if (strcmp(key, "Font") == 0 && gl_cast<Control>(n) && gl_has_property(n, "FontFace")) {
        const int v = (int)gl_num(L, 3);
        n->set_meta(StringName("_glp_Font"), v);
        // La FontFace equivalente, la de Font.fromEnum en Roblox (con sus numeros).
        const GLFuenteEnum& f = gl_fuente_enum(v);
        n->set("FontFace", String("rbxasset://fonts/families/") + f.familia + ".json");
        n->set_meta(StringName("_gl_font_weight"), f.peso);
        n->set_meta(StringName("_gl_font_style"), f.estilo);
        Control* c = gl_cast<Control>(n);
        if (c->is_inside_tree()) gl_aplicar_fuente(c);
        return 0;
    }

    // ── Respaldo de escritura de las GUI (ver la lectura) ──────────────
    // `label.TextWrapped = true`, `label.RichText = true`, `img.ScaleType =
    // Enum.ScaleType.Slice`... se perdian en silencio.
    if (gl_cast<Control>(n) && gl_has_property(n, String(key))) {
        Variant gv;
        if (gl_luau_to_variant(L, 3, gv)) { n->set(StringName(key), gv); return 0; }
    }

    // ── Puente genérico de escritura (solo clases nuevas con meta _gl_bridge) ──
    if (n->has_meta("_gl_bridge")) {
        Variant gv;
        // Referencia a otra instancia: se guarda su ObjectID (ver la lectura).
        const String _ok = String("_glo_") + String(key);
        if (_gl_has_metatable(L, 3, "GodotObject")) {
            GodotObjectWrapper* ow = (GodotObjectWrapper*)lua_touserdata(L, 3);
            Node* dest = ow ? gow_node(ow) : nullptr;
            if (dest) n->set_meta(_ok, (int64_t)dest->get_instance_id());
            else if (n->has_meta(_ok)) n->remove_meta(_ok);
            return 0;
        }
        if (lua_isnil(L, 3) && n->has_meta(_ok)) { n->remove_meta(_ok); return 0; }
        if (gl_luau_to_variant(L, 3, gv)) {
            if (gl_has_property(n, String(key))) n->set(String(key), gv);
            else n->set_meta(String("_glp_") + String(key), gv);
        } else if (lua_istable(L, 3)) {
            // UICorner.CornerRadius y UIListLayout.Padding son UDim enteros: la
            // escala tambien cuenta. Solo con el Offset, UDim.new(0.5, 0) (la
            // pastilla, lo mas comun por script) dejaba las esquinas en cuadrado.
            const char* udim = strcmp(key, "CornerRadius") == 0 ? "set_gl_udim"
                             : strcmp(key, "Padding") == 0 ? "set_gl_padding_udim" : nullptr;
            if (udim && n->has_method(udim)) {
                lua_getfield(L, 3, "Scale");
                lua_getfield(L, 3, "Offset");
                if (lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
                    n->call(udim, (float)lua_tonumber(L, -2), (int)lua_tonumber(L, -1));
                    lua_pop(L, 2);
                    return 0;
                }
                lua_pop(L, 2);
            }
            // Otros UDim / UDim2 → usar Offset
            lua_getfield(L, 3, "Offset");
            if (lua_isnumber(L, -1)) {
                int off = (int)lua_tonumber(L, -1);
                if (gl_has_property(n, String(key))) n->set(String(key), off);
                else n->set_meta(String("_glp_") + String(key), off);
            }
            lua_pop(L, 1);
        }
        return 0;
    }
    // ── Piezas del rig de avatar (ver la lectura en luau_api_indice_personaje.h) ──
    // Lo que no ha cogido ningun manejador de arriba se guarda en una meta _glp_
    // para devolverlo al leer (antes se tiraba en silencio). Transparency y
    // LocalTransparencyModifier ademas se ven: la camara de Roblox en primera
    // persona y los efectos de invisibilidad o fantasma van por ahi.
    if (n->has_meta(StringName("_gl_rig_tamano")) && !gl_cast<RobloxPart>(n) && key[0] >= 'A' && key[0] <= 'Z') {
        Variant gv;
        if (!gl_luau_to_variant(L, 3, gv)) return 0;
        n->set_meta(String("_glp_") + String(key), gv);
        if (strcmp(key, "Transparency") == 0 || strcmp(key, "LocalTransparencyModifier") == 0)
            gl_rig_aplicar_transparencia(n);
        return 0;
    }

    // Propiedad importada que vive en un meta con su nombre (ver la lectura): se
    // actualiza el meta. Y las caras del cielo, que el Sky guarda asi: al
    // cambiarlas se redibuja el skybox (diferido: un script suele poner las seis
    // seguidas y basta con rehacerlo una vez).
    if (key[0] >= 'A' && key[0] <= 'Z') {
        const StringName mk(key);
        const bool cielo = gl_cara_del_cielo(key) && strcmp(gl_roblox_classname(n), "Sky") == 0;
        if (n->has_meta(mk) || cielo) {
            Variant gv;
            if (gl_luau_to_variant(L, 3, gv)) n->set_meta(mk, gv);
            if (cielo && n->has_method("_gl_cielo_cambio")) n->call_deferred("_gl_cielo_cambio");
        }
    }
    return 0;
}

#include "gl_replica.h"   // replicación por red: propiedades, Parent, atributos y etiquetas
#include "gl_cast.h"

// ── Avisar de las escrituras de propiedades ───────────────────────────
// Tras escribir una propiedad se avisa a quien la vigile: eso es lo que hace
// funcionar GetPropertyChangedSignal(prop) y Changed. Las escrituras son de lo
// mas frecuente que hace un juego (miles por segundo), asi que el filtro va
// solo con enteros: el id sale del propio userdata, sin ObjectDB ni casts, y
// todo lo demas solo pasa si alguien vigila ESTE objeto, que es el caso raro.
inline bool gl_prop_vigilada(lua_State* L) {
    std::vector<GLAttrWatch>& v = gl_attr_watchers();
    if (v.empty()) return false;                 // nadie vigila nada
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    if (!w || !w->obj_id) return false;
    for (size_t i = 0; i < v.size(); i++)
        if (v[i].active && v[i].node_id == w->obj_id &&
            (v[i].kind == GL_VIGILA_PROP || v[i].kind == GL_VIGILA_CHANGED)) return true;
    return false;
}
// El valor de la propiedad ANTES de escribirla, guardado en el registro (la
// pila no se toca: el impl lee sus argumentos en 1/2/3). LUA_NOREF si no se
// pudo leer.
inline int gl_valor_previo(lua_State* L) {
    lua_pushcfunction(L, godot_object_index, "__index");
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) { lua_pop(L, 1); return LUA_NOREF; }
    const int ref = gl_ref(L, -1);
    lua_pop(L, 1);
    return ref;
}
// ¿Mismo valor? Para Vector3, Color3, CFrame, instancias..., su __eq.
inline bool gl_mismo_valor(lua_State* L, int a, int b) {
    if (lua_rawequal(L, a, b)) return true;
    const int t = lua_type(L, a);
    if (t != lua_type(L, b)) return false;
    if (t == LUA_TUSERDATA || t == LUA_TTABLE) return lua_equal(L, a, b) != 0;
    return false;
}
// Roblox no dispara Changed si se reasigna el mismo valor; sin esto, un
// manejador que reescribe la propiedad que vigila se llamaria sin fin.
inline void gl_avisar_cambio_prop(lua_State* L, int ref_antes) {
    GodotObjectWrapper* w = (GodotObjectWrapper*)lua_touserdata(L, 1);
    Node* nd = w ? gow_node(w) : nullptr;
    const char* k = lua_tostring(L, 2);
    bool igual = false;
    if (ref_antes != LUA_NOREF) {
        lua_getref(L, ref_antes);
        igual = gl_mismo_valor(L, lua_gettop(L), 3);
        lua_pop(L, 1);
        gl_unref(L, ref_antes);
    }
    if (!igual && nd && k) gl_fire_prop_changed(nd, String::utf8(k));
}

inline int godot_object_newindex(lua_State* L) {
    const bool vigilada = gl_prop_vigilada(L);
    const int antes = vigilada ? gl_valor_previo(L) : LUA_NOREF;
    if (!gl_perfil_on()) {
        GLAutTiempo _a(AUT_ESCRIBE);
        int r = godot_object_newindex_impl(L);
        gl_rep_on_newindex(L);
        if (vigilada) gl_avisar_cambio_prop(L, antes);
        return r;
    }
    const char* k = lua_tostring(L, 2);
    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
    int r = godot_object_newindex_impl(L);
    const uint64_t t1 = Time::get_singleton()->get_ticks_usec();
    gl_rep_on_newindex(L);
    if (vigilada) gl_avisar_cambio_prop(L, antes);
    const uint64_t t2 = Time::get_singleton()->get_ticks_usec();
    GLPerfilPuente& p = gl_perfil();
    p.us_escribe += t2 - t0;
    p.us_impl    += t1 - t0;
    p.us_rep     += t2 - t1;
    p.n_escribe++;
    if (k) { p.veces_escribe[String(k)]++; p.us_por_clave[String(k)] += (t2 - t0); }
    return r;
}

#endif // LUAU_API_ESCRITURA_H
