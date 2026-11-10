# Tu propio servidor GodotLuau

Este servicio guarda versiones de tus proyectos y ejecuta sus ServerScripts en un
proceso dedicado. Funciona en una PC encendida o en una máquina virtual Windows o
Linux. No requiere una cuenta ni aloja nada en servidores de Rocket Meteor.

Necesitas Python 3.10 o posterior y un ejecutable de ROME/Godot compatible con la
versión de GodotLuau del proyecto. En Linux el proyecto debe incluir la biblioteca
`.so` de GodotLuau de esa misma revisión; una DLL de Windows no sirve en Linux.

Desde esta carpeta:

```powershell
python server.py --engine "C:\ROME\ROME Studio.exe" --data "C:\MisServidores\datos"
```

En Linux:

```sh
python3 server.py --engine /opt/rome/rome --data /srv/rome-data
```

En el editor, abre **Servidores**, añade `http://127.0.0.1:8088`, dirección del juego
`127.0.0.1` y la clave de `owner-token.txt`, creada al iniciar el servicio. Guarda
los ajustes y comprueba la conexión. **Preparar y probar** guarda una copia,
la envía, importa los recursos en el servidor, inicia el juego y abre un cliente
real. **Publicar / iniciar servidor** deja el juego corriendo sin abrir un cliente.

**Local** conserva la ejecución y los jugadores de prueba locales de GodotLuau.
**Server Real** conecta Play con la última versión publicada; usa **Preparar y
probar** para publicar los cambios antes de la prueba. El Play normal y el reloj
de cinco minutos guardan una copia si hubo cambios. Una copia remota no cambia
automáticamente el juego publicado. La copia local queda en `.godot/luau-server`.
Si la conexión falla se conserva y se comunica el error; no se finge un servidor.

Para una PC remota o una VM, configura un nombre/IP accesible, TLS para la API
(`--bind 0.0.0.0 --cert certificado.pem --key clave.pem`, o un proxy HTTPS delante
del puerto local) y permite el puerto UDP del juego, inicialmente 25565. La API
usa TCP 8088. La API local puede funcionar a través de un túnel privado. NAT/CGNAT
puede impedir conexiones entrantes: el servicio no abre puertos del router por ti.
La clave del propietario permite publicar código y debe mantenerse privada;
los jugadores **no** necesitan ni deben recibirla.

El servidor conserva ZIP inmutables por hash y releases importadas; revisa el
espacio disponible y archiva copias antiguas cuando quieras. No borra versiones
automáticamente. Al cerrar el servicio se detienen sus juegos. Para funcionamiento
continuo, ejecútalo con un servicio del sistema o supervisor y vuelve a iniciar
la publicación después de reiniciar. No hay persistencia de procesos por sí sola.

API v1: `GET /health`, `PUT /v1/projects/{id}/snapshot` (ZIP),
`POST /v1/projects/{id}/publish`, `GET /v1/projects/{id}/status`,
`POST /v1/projects/{id}/stop`. Todos requieren `Authorization: Bearer CLAVE`.
El servidor valida rutas, tamaño expandido, nombres repetidos y checksums antes
de aceptar un archivo. El cliente guarda la clave en los ajustes del editor,
fuera de los archivos de proyecto que se envían.

Las copias pueden incluir scripts privados del servidor. El servicio de gestión
es solo para el propietario; no es un descargador público ni una tienda de juegos.
El multijugador 2D sigue dependiendo de lo que soporte GodotLuau; este servicio
no transforma automáticamente un juego local 2D en un juego con replicación.

Pruebas: `python -m unittest -v test_server.py`.
