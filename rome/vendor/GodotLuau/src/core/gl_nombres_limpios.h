#ifndef GL_NOMBRES_LIMPIOS_H
#define GL_NOMBRES_LIMPIOS_H

// ════════════════════════════════════════════════════════════════════════
//  LOS NOMBRES QUE VE EL USUARIO — sin "Roblox" delante
//
//  Ver un nodo llamado `RobloxTextButton` en el arbol saca del juego: el
//  usuario esta haciendo SU juego, no mirando una copia de otro. Las clases se
//  llaman como en Roblox y punto: Frame, TextButton, Sound, Terrain...
//
//  COMO, sin romper nada:
//  Cada clase antigua se queda tal cual y se le cuelga una subclase FINA con el
//  nombre limpio. Hereda todo el comportamiento y, como `TextButton` ES-UN
//  `RobloxTextButton`, sigue funcionando cada `cast_to<RobloxTextButton>` y cada
//  `is_class("RobloxTextButton")` que haya por el codigo. La vieja se registra
//  como INTERNA: desaparece del "Add Node" del editor pero sigue siendo
//  cargable, asi que una escena guardada con `type="RobloxFrame"` sigue
//  abriendo — y la tabla de renombres de roblox_datamodel.h la migra sola.
//
//  Es la misma pauta que ya se uso para `Part` en la 1.15; aqui se termina el
//  trabajo para el resto.
//
//  LAS QUE NO PUEDEN: `Tween`, `CanvasGroup`, `Decal` y `Texture` son nombres
//  que Godot YA tiene registrados, y en ClassDB no puede haber dos. Esas se
//  quedan con su nombre interno; da igual de cara al usuario, porque lo que
//  contesta `instance.ClassName` en Luau es el nombre de Roblox (lo resuelve
//  gl_roblox_classname), y en el arbol no son nodos que se creen a mano.
//
//  Y `RobloxTemplate` se queda como esta a proposito: ESE si es el nodo que
//  dice "aqui hay un proyecto tipo Roblox", y es util que se note.
// ════════════════════════════════════════════════════════════════════════

#include "../ui/roblox_gui.h"            // Frame, TextLabel, TextButton, TextBox…
#include "../ui/roblox_billboard.h"      // RobloxTool
#include "../ui/roblox_chat.h"           // RobloxChat
#include "../gameplay/roblox_sound.h"    // RobloxSound, RobloxSoundGroup
#include "../gameplay/roblox_input.h"    // RobloxMouse
#include "../characters/roblox_terrain.h"
#include "../characters/roblox_player.h"
#include "../characters/roblox_player2d.h"
#include "../services/roblox_workspace.h"
#include "../services/roblox_workspace2d.h"
#include "../services/roblox_game2d.h"
#include "../services/roblox_game3d.h"

using namespace godot;

// Una subclase fina: nombre limpio, cero comportamiento propio.
#define GL_NOMBRE_LIMPIO(Limpio, Vieja)      \
    class Limpio : public Vieja {            \
        GDCLASS(Limpio, Vieja);              \
    protected:                               \
        static void _bind_methods() {}       \
    public:                                  \
        Limpio() {}                          \
    };

// ── Interfaz ─────────────────────────────────────────────────────────
GL_NOMBRE_LIMPIO(Frame,          RobloxFrame)
GL_NOMBRE_LIMPIO(ScrollingFrame, RobloxScrollingFrame)
GL_NOMBRE_LIMPIO(TextLabel,      RobloxTextLabel)
GL_NOMBRE_LIMPIO(TextButton,     RobloxTextButton)
GL_NOMBRE_LIMPIO(TextBox,        RobloxTextBox)
GL_NOMBRE_LIMPIO(ImageLabel,     RobloxImageLabel)
GL_NOMBRE_LIMPIO(ImageButton,    RobloxImageButton)

// ── Mundo ────────────────────────────────────────────────────────────
GL_NOMBRE_LIMPIO(Tool,           RobloxTool)

// ── Sonido ───────────────────────────────────────────────────────────
GL_NOMBRE_LIMPIO(Sound,          RobloxSound)
GL_NOMBRE_LIMPIO(SoundGroup,     RobloxSoundGroup)

// ── Servicios y escena ───────────────────────────────────────────────
// El nodo del mundo ya se LLAMA "Workspace" en el arbol (como en Roblox); lo
// que se veia con el nombre viejo era su clase, en el inspector.
GL_NOMBRE_LIMPIO(Workspace,      RobloxWorkspace)
GL_NOMBRE_LIMPIO(Workspace2D,    RobloxWorkspace2D)
GL_NOMBRE_LIMPIO(Game2D,         RobloxGame2D)
GL_NOMBRE_LIMPIO(Game3D,         RobloxGame3D)

// Mouse, Chat, Terrain, Character y Character2D viven junto a su clase base y
// no aqui: hay codigo C++ que las construye con memnew, y para eso tienen que
// estar declaradas en el mismo header que la base.
//
// Sobre `Character`: en Roblox esto no es una clase que se cree a mano, es el
// Model del jugador, y todo el mundo lo llama Character (player.Character). Ese
// es el nombre que espera quien viene de Roblox, no "Player" — Player es la
// otra cosa, el objeto del jugador, que aqui es PlayerObject.

#undef GL_NOMBRE_LIMPIO

#endif // GL_NOMBRES_LIMPIOS_H
