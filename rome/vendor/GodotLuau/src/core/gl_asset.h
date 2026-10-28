#ifndef GL_ASSET_H
#define GL_ASSET_H

// Ids de assets que viven en la nube de Roblox (rbxassetid:// y las URLs de
// roblox.com). NO son rutas de Godot: pasarlos por ResourceLoader suelta dos
// errores rojos por cada uno y, al abrir un place importado con cientos de
// sonidos o imagenes, la consola queda inservible.
//
// El id se conserva igual en el nodo (metadato __rbx_missing_asset) y el
// importador de .rbxl los lista todos en su reporte, para poder exportar el
// asset desde Studio y enchufarlo a mano.

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/templates/local_vector.hpp>

using namespace godot;

inline bool gl_is_roblox_asset(const String& id) {
    return id.begins_with("rbxassetid://")               ||
           id.begins_with("rbxasset://")                 ||
           id.begins_with("http://www.roblox.com/asset") ||
           id.begins_with("https://www.roblox.com/asset")||
           id.begins_with("http://roblox.com/asset")     ||
           id.begins_with("https://roblox.com/asset");
}

// ── Sustitucion LOCAL de assets de la nube ───────────────────────────
// Las imagenes y sonidos de un place viven en la nube de Roblox y no se pueden
// descargar, asi que las UIs importadas salian con huecos en blanco. Con esto se
// pueden "enchufar" a mano: basta guardar el archivo con el NUMERO del id como
// nombre en
//        res://GodotLuau/assets/rbx/<id>.<ext>
// y cualquier ImageLabel, ImageButton, Decal, Sound o Animation que use ese id lo
// encontrara solo. Ejemplo: rbxassetid://1234567 -> assets/rbx/1234567.png
// Asi se exporta el asset desde Studio (o se pone otro propio) y la UI del place
// aparece con su aspecto real, sin tocar ni un script del juego.
inline String gl_asset_id_number(const String& id) {
    // Se queda con el ULTIMO tramo numerico (sirve para rbxassetid://123 y para
    // las URLs tipo .../asset/?id=123).
    String digits;
    for (int i = 0; i < id.length(); i++) {
        const char32_t c = id[i];
        if (c >= '0' && c <= '9') digits += String::chr(c);
        else if (!digits.is_empty()) digits = "";
    }
    return digits;
}

// ── Indice de la carpeta de assets ───────────────────────────────────
// Buscar un id probaba hasta 14 extensiones con ResourceLoader::exists, y cada
// prueba son varias consultas al disco (el archivo, su .import, su .remap...).
// MEDIDO al darle a Play con Build a Mountain: 313 ms de los 954 que tarda en
// instanciarse la escena, casi todo de las 1.609 MeshPart (cada una probaba 10
// rutas hasta dar con su .res). Ahora la carpeta se lista UNA vez y cada
// busqueda es mirar un HashMap. Lo que escribe el importador se anota aparte.
static const char* const GL_ASSET_EXTS[] = { ".png", ".jpg", ".jpeg", ".webp", ".svg",
                                             ".ogg", ".wav", ".mp3", ".tres", ".res",
                                             ".mesh", ".obj", ".glb", ".gltf" };
static const int GL_ASSET_N_EXTS = 14;

struct GLAssetIndice {
    bool hecho = false;
    HashMap<String, uint32_t> crudo;       // numero -> bits de las extensiones con archivo
    HashMap<String, uint32_t> importado;   // numero -> bits de las que tienen .import/.remap
};

inline GLAssetIndice& gl_asset_indice_ref() { static GLAssetIndice ix; return ix; }

// "<numero><ext>", "<numero><ext>.import" o ".remap" (lo que queda en un juego
// exportado, donde el PNG original ya no va en el paquete).
inline void gl_asset_indice_nombre(GLAssetIndice& ix, String n) {
    bool imp = false;
    if (n.ends_with(".import")) { imp = true; n = n.substr(0, n.length() - 7); }
    else if (n.ends_with(".remap")) { imp = true; n = n.substr(0, n.length() - 6); }
    const int punto = n.rfind(".");
    if (punto <= 0) return;
    const String num = n.substr(0, punto);
    for (int i = 0; i < num.length(); i++)
        if (num[i] < '0' || num[i] > '9') return;
    const String ext = n.substr(punto);
    for (int i = 0; i < GL_ASSET_N_EXTS; i++) {
        if (ext != GL_ASSET_EXTS[i]) continue;
        HashMap<String, uint32_t>& m = imp ? ix.importado : ix.crudo;
        if (uint32_t* b = m.getptr(num)) *b |= 1u << i;
        else m.insert(num, 1u << i);
        return;
    }
}

inline GLAssetIndice& gl_asset_indice() {
    GLAssetIndice& ix = gl_asset_indice_ref();
    if (!ix.hecho) {
        ix.hecho = true;
        const PackedStringArray nombres = DirAccess::get_files_at("res://GodotLuau/assets/rbx/");
        for (int i = 0; i < nombres.size(); i++) gl_asset_indice_nombre(ix, nombres[i]);
    }
    return ix;
}

// Lo que ya se busco, encontrado o no (el NEGATIVO se guarda como cadena
// vacia): un place cuyas imagenes viven en los servidores de Roblox pregunta
// mil veces por ids que no estan.
inline HashMap<String, String>& gl_asset_ruta_cache() {
    static HashMap<String, String> m; return m;
}
inline HashMap<String, String>& gl_asset_fichero_cache() {
    static HashMap<String, String> m; return m;
}

// Recurso que Godot sabe cargar: importado (.import/.remap) o nativo (.tres,
// .res, .mesh), en el orden de GL_ASSET_EXTS. Es lo mismo que contestaba
// ResourceLoader::exists, sin tocar el disco.
inline String gl_local_asset_path(const String& id) {
    const String num = gl_asset_id_number(id);
    if (num.is_empty()) return String();
    HashMap<String, String>& cache = gl_asset_ruta_cache();
    if (const String* hay = cache.getptr(num)) return *hay;
    if (!ResourceLoader::get_singleton()) return String();   // sin cachear: aun no hay servidor
    const GLAssetIndice& ix = gl_asset_indice();
    const uint32_t* cr = ix.crudo.getptr(num);
    const uint32_t* im = ix.importado.getptr(num);
    String ruta;
    for (int i = 0; i < GL_ASSET_N_EXTS && ruta.is_empty(); i++) {
        const uint32_t b = 1u << i;
        const bool nativo = i >= 8 && i <= 10;
        if ((im && (*im & b)) || (nativo && cr && (*cr & b)))
            ruta = String("res://GodotLuau/assets/rbx/") + num + GL_ASSET_EXTS[i];
    }
    cache.insert(num, ruta);
    return ruta;
}

// Igual pero mirando el ARCHIVO, no el recurso importado. Hace falta para las
// imagenes que acaba de escribir el importador de .gdlu: existen en disco pero
// Godot todavia no las ha importado, asi que no son recurso y la textura salia
// vacia hasta reabrir el proyecto. Se cargan crudas (ver gl_gui_load_image).
inline String gl_local_asset_file(const String& id) {
    const String num = gl_asset_id_number(id);
    if (num.is_empty()) return String();
    HashMap<String, String>& cache = gl_asset_fichero_cache();
    if (const String* hay = cache.getptr(num)) return *hay;
    const uint32_t* cr = gl_asset_indice().crudo.getptr(num);
    String ruta;
    for (int i = 0; i < 4 && ruta.is_empty(); i++)
        if (cr && (*cr & (1u << i))) ruta = String("res://GodotLuau/assets/rbx/") + num + GL_ASSET_EXTS[i];
    cache.insert(num, ruta);
    return ruta;
}

// El importador de .gdlu acaba de escribir <id><ext> en assets/rbx/: se anota
// en el indice y se olvida lo que se busco antes de ese id (un id que fallo
// seguiria dando fallo para siempre).
inline void gl_asset_indice_anotar(const String& id, const String& ext) {
    const String num = gl_asset_id_number(id);
    if (num.is_empty()) return;
    gl_asset_indice_nombre(gl_asset_indice(), num + ext);
    gl_asset_ruta_cache().erase(num);
    gl_asset_fichero_cache().erase(num);
}

// Textura de un archivo de imagen SUELTO (sin importar), leida una sola vez.
// Cada `Image =` de un ImageLabel y cada TextureID volvia a leer y descomprimir
// el archivo y a subir la textura a la GPU: 415 us por escritura (379 en la
// pantalla de carga de Build a Mountain). Tope de 1024: se sueltan las que ya
// no usa nadie. Se vacia al descargar la extension (register_types).
inline HashMap<String, Ref<Texture2D>>& gl_texturas_crudas() {
    static HashMap<String, Ref<Texture2D>> m;
    return m;
}
inline void gl_texturas_guardar(const String& ruta, const Ref<Texture2D>& t) {
    HashMap<String, Ref<Texture2D>>& c = gl_texturas_crudas();
    if (c.size() >= 1024) {
        LocalVector<String> sobran;
        for (const KeyValue<String, Ref<Texture2D>>& kv : c)
            if (kv.value.is_null() || kv.value->get_reference_count() <= 1) sobran.push_back(kv.key);
        for (uint32_t i = 0; i < sobran.size(); i++) c.erase(sobran[i]);
    }
    c.insert(ruta, t);
}

inline Ref<Texture2D> gl_textura_cruda(const String& ruta) {
    if (ruta.is_empty()) return Ref<Texture2D>();
    HashMap<String, Ref<Texture2D>>& c = gl_texturas_crudas();
    if (Ref<Texture2D>* hay = c.getptr(ruta)) return *hay;
    if (!FileAccess::file_exists(ruta)) return Ref<Texture2D>();
    Ref<Image> img;
    img.instantiate();
    if (img->load(ruta) != OK || img->is_empty()) return Ref<Texture2D>();
    const Ref<Texture2D> t = ImageTexture::create_from_image(img);
    gl_texturas_guardar(ruta, t);
    return t;
}

#endif // GL_ASSET_H
