# Using LTree Commander

Guía detallada de las funciones de la interfaz gráfica y del prototipo de terminal. La interfaz del programa está en inglés; esta guía conserva la documentación de trabajo en español.

## Tamaño y estado de la ventana

Al cerrar normalmente, el modo gráfico guarda tamaño normal, estado maximizado y pantalla completa en `~/.config/ltreec/window.ini` (respeta `XDG_CONFIG_HOME`). Los recupera en el siguiente arranque; `--fullscreen` tiene prioridad. Una ventana cerrada mientras está minimizada vuelve visible, conservando su tamaño y los otros estados.

En X11 también se guarda la geometría para solicitar la posición anterior. En Wayland se restauran tamaño y estados; el escritorio decide la posición. La geometría se ajusta al espacio disponible si cambió la pantalla. Si faltan datos válidos, se mantiene el tamaño predeterminado. El modo terminal y los lanzamientos que activan una instancia existente no cambian esta configuración. Con `--new-instance`, queda guardada la geometría de la última instancia gráfica que cierre normalmente.

## Ayuda F1

F1 abre un manual navegable en inglés, con esquemas de la pantalla principal y los paneles, capítulos, teclas destacadas y ejemplos. El texto se ajusta al ancho disponible; todas las líneas se pueden leer desplazándose. La ayuda abre el capítulo del contexto actual en el visor, comprimidos, comparación, Prune, Graft, New date y estadísticas.

- **Tab**: índice; cursores eligen un capítulo y **Enter** lo abre.
- **Left/Right** o **[/]**: capítulo anterior/siguiente.
- **Up/Down, PgUp/PgDn, Home/End** y rueda: desplazamiento.
- **F** o **/**: búsqueda literal, sin distinguir mayúsculas, en todos los capítulos de la ayuda; **Space** repite. Esc cancela el texto de búsqueda.
- **Esc/F1**: volver a la vista o al prompt, conservando selección, marcas y opciones. Enter dentro de un capítulo no ejecuta comandos sobre archivos.

El prototipo de terminal usa el mismo navegador con tres capítulos específicos de sus funciones disponibles. La ayuda gráfica tiene dieciséis capítulos; incluye búsquedas Regex, historial, transferencias, permisos, fechas, comprimidos y edición Hex. Es un manual de las funciones implementadas, con sus límites, escrito para LTree Commander.

Funciones de la alpha: ventana, árbol/listado, carga, navegación básica, marcado, filtros por nombres/fechas/tamaños, búsqueda de contenido, apertura en editor, visor Alpha/Hex/Wrap/Dump/Junk con comandos Ctrl/Alt, edición Hex, comprimidos ZIP/TAR/7Z y F7 Autoview, paneles F8, puntos de montaje con L, ordenación Alt+S, comparación de texto y binarios con J/H, consola integrada con X, copia y movimiento del archivo seleccionado con C/M, de marcados a una carpeta con Ctrl+C/Ctrl+M y con estructura y máscaras mediante Alt+C/Alt+M, creación de carpetas con M y borrado de archivos y carpetas vacías con D y de marcados con Ctrl+D. No es todavía un clon completo. Véanse las funciones y limitaciones en [estado de implementación](IMPLEMENTACION.md).

## Compilar

Dependencias: compilador C++20, CMake 3.22+, Ninja, Qt 6.11+ (Core, Widgets, Concurrent, Network, PrintSupport y Test), libarchive, QTermWidget 6, Python 3 con fontTools, kbd y gzip. El prototipo de terminal requiere también pkg-config y ncursesw; se puede excluir con `-DLTREE_BUILD_TERMINAL=OFF`. La comparación de texto utiliza GNU diff. Sublime Text con `subl` permite Open/Edit de los archivos de texto.

La fuente DOS 8×16 se genera al compilar desde la instalación local de kbd; el archivo TTF generado queda dentro de build. Para otra ubicación usar `-DLTREE_PSF_FONT=/ruta/default8x16.psfu.gz`; `-DLTREE_FONT_PYTHON=/ruta/python3` selecciona el intérprete con fontTools. La misma entrada determina la cuadrícula y la fuente de la consola.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

## Probar la aplicación

El ejecutable se llama `ltc`, por Linux + Tree + Commander. Los historiales conservan su ubicación en `~/.config/ltreec`; el identificador del escritorio y el ícono siguen siendo `ltreec`.

```bash
./build/ltc "$HOME"
```

`F` permite filtrar, por ejemplo `*.php,-admin*.*`; F seguido de Enter restablece todos los archivos. También admite `*.php,=>s1000,<=s5000` para tamaños en bytes y `*.php,=>TODAY-7,<=TODAY` para los últimos días. Las fechas explícitas usan **mes-día-año**, por ejemplo `=10-01-2026`. La fuente bitmap 8×16 fue aprobada por el usuario y se conserva. El esquema usa el azul oscuro, cian, amarillo y selección gris de su captura de Windows, con colores por extensiones.

`L` abre los puntos de montaje: cursores y Enter para elegir, F5 para actualizar y Esc para cancelar. También podés escribir una ruta absoluta. Cada panel conserva las ubicaciones visitadas durante la sesión.

Por defecto solo se abre una instancia por usuario en su directorio de ejecución. Lanzarlo otra vez solicita activar la ventana existente y conserva su carpeta, selección y operaciones. Para abrir otra ventana independiente, usar `build/ltc --new-instance [directory]`. El bloqueo se recupera después de un cierre abrupto. Las ventanas de versiones anteriores deben cerrarse una vez para comenzar a usar esta regla.

El lanzador de escritorio se encuentra en `resources/ltreec.desktop` y los íconos SVG/PNG en `resources/icons`. El lanzador requiere `ltc` en PATH; al instalarlo se pueden usar rutas absolutas para Exec/TryExec. El ícono se instala con el nombre `ltreec` en el tema hicolor. La ventana incluye el ícono y se identifica con el mismo nombre del lanzador para GNOME.

`--tree-sizes` muestra a la derecha del árbol el tamaño acumulado de los archivos cargados en cada rama, incluidas sus subcarpetas colapsadas. Por ejemplo:

```bash
./build/ltc --tree-sizes "$HOME"
```

La columna está desactivada por defecto y aparece en ambos paneles con F8. `[ … ]` indica una rama completamente cargada; `{ … }` indica un total parcial y `{ ? }` una rama cargada sin archivos conocidos, pero con subcarpetas pendientes o errores. Una carpeta sin cargar no muestra una cifra; una rama completamente vacía tampoco. Los tamaños suman todos los archivos cargados, independientemente de Filespec y las marcas. `k` representa 1024 bytes, redondeado hacia arriba después de sumar la rama; las cifras demasiado largas usan M/G/T/P/E para conservar la alineación. No recorre carpetas por activar la columna: `*` carga la rama y F3 actualiza la carpeta activa. Los paneles conservan sus datos cargados; Tab sincroniza los de una misma raíz según el comportamiento existente. Lanzar de nuevo con `--tree-sizes` también activa la columna de la instancia que ya está abierta.

`F4` alterna los menús normal, Ctrl y Alt. El menú elegido sirve para una sola orden: después vuelve al normal, incluso al entrar en un prompt. Esc cancela el menú sin salir del listado. Una tecla ajena al menú lo cancela y ejecuta su función normal. Mantener Ctrl o Alt permite seguir usando los atajos físicos habituales.

`Alt+I` muestra un recuadro de información en la esquina inferior derecha sin reemplazar la lista ni desplazar su selección: permisos, propietario/grupo, tamaño, espacio asignado, enlaces duros, dispositivo/inodo y fechas. Los enlaces simbólicos se inspeccionan como enlaces y muestran su destino, incluso si está roto. Los cursores, Home/End y PgUp/PgDn permiten navegar mientras se muestran los datos; Tab cambia de panel con F8. Los comandos habituales y el ratón siguen disponibles. F3 refresca la lista y los metadatos; Alt+I, Enter o Esc cierra el recuadro conservando selección y marcas. La fecha Created aparece como Unavailable cuando el sistema de archivos no la proporciona; Changed es la fecha de cambio de metadatos.

`A` desde Directory, Branch o Showall abre **Permissions** para el archivo bajo el cursor, como Attributes del menú normal. También funciona mientras Info muestra ese archivo. Escribir `644`, `u-w` o `u+x`, pulsar Enter para revisar y Y/Enter para aplicar; N/Esc cancela. Mantiene selección, desplazamiento, filtro y marcas. Actúa sobre el archivo actual aunque haya otros marcados; el indicador de solo lectura se actualiza al terminar, también en el otro panel si tiene ese archivo cargado. Los enlaces simbólicos y archivos especiales se rechazan.

`Alt+A` desde el árbol abre **Permissions** para el directorio seleccionado. Introducir `755`, `u+w`, `go-w` o `u=rwx,g=rx,o=` y pulsar Enter para revisar el modo anterior y el nuevo. Y/Enter aplica; N/Esc cancela y Backspace permite editar. El cambio afecta solo a ese directorio. Los modos de 3/4 cifras octales son exactos, incluidos los bits especiales; las expresiones simbólicas exigen indicar `u`, `g`, `o` o `a`, admiten `rwxXst` y cláusulas separadas por comas. `=` sustituye también los bits especiales de las clases indicadas. No es un intérprete completo de la sintaxis de chmod. No sigue enlaces, pide volver a revisar si la carpeta cambió y no eleva privilegios. Ctrl+A sobre archivos marcados se describe debajo; los cambios recursivos de directorios siguen pendientes. También puede abrirse Permissions desde Info con Alt+A, y ambos comandos funcionan desde el menú Alt de F4.

`M` desde el árbol crea una carpeta dentro de la seleccionada: escribir el nombre y pulsar Enter. Admite varios niveles con `/`, como `carpeta/subcarpeta`. Esc cancela el prompt; F4 alterna Jump para entrar al crear y F12 lo desactiva. Inicialmente Jump está desactivado. F3 recupera el último nombre creado; Up/Down recorren el historial de la sesión. Desde archivos, M mueve el archivo seleccionado, según el flujo descrito debajo.

Después de borrar todos los archivos, `Enter`, `Esc` o `←` vuelve al árbol con la carpeta seleccionada; `D` permite borrarla si está vacía. Si la carpeta inicial quedó completamente vacía, `Enter` revela su padre y mantiene la carpeta seleccionada, incluso si ya estás en el árbol. `D` sobre la carpeta inicial también prepara ese contexto y muestra la confirmación. `Backspace` sube al padre y amplía la raíz si hace falta, conservando carga y marcas. La raíz física `/` permanece protegida.

`D` o `Delete` desde el árbol borra la carpeta seleccionada si está vacía. Enter/Y confirma; N/Esc cancela. Es borrado permanente, sin papelera. Se comprueba el contenido real, incluidos archivos ocultos y directorios aún no cargados. Al terminar selecciona la siguiente entrada visible del árbol, o la anterior si era la última, conservando el desplazamiento y actualizando los paneles de la misma raíz. Para borrar la carpeta que era raíz de navegación, el árbol se amplía antes a su padre; `/` no se puede borrar.

Los menús del árbol/listado y los prompts destacan las teclas en amarillo y muestran el resto del comando en celeste, incluido `eXecute`.

Desde la lista de archivos, `D` o `Delete` borra el archivo seleccionado, con la misma confirmación. Funciona en Directory, Branch y Showall, conserva la carpeta, el filtro y las demás marcas, y selecciona el siguiente archivo disponible. Los enlaces se eliminan sin borrar su destino. F4 para papelera sigue pendiente.

`C` desde Directory, Branch o Showall copia únicamente el archivo seleccionado. En **COPY file / as**, Enter conserva el nombre; podés escribir otro nombre o una máscara como `*.bak`. Tab cambia mayúsculas/minúsculas; F3 y Up/Down recuperan el historial. Después **COPY to** pide la carpeta de destino, inicialmente la del panel opuesto con F8 o la del archivo de origen. Si no existe, pregunta antes de crearla. Si el archivo destino ya existe, Y/Enter reemplaza y N omite; Esc cancela. Copia directamente dentro del destino, conserva selección y marcas, y rechaza copiar sobre el mismo archivo. `Ctrl+C` copia los marcados del listado activo directamente a una carpeta de destino, sin duplicar subcarpetas, también desde Branch y Showall. Pregunta por la máscara y la política de reemplazo; las colisiones entre nombres destino se detectan antes de escribir.

`Alt+C` desde Branch o una lista de archivos copia los marcados del listado activo, respetando Filespec y Tags-Only. Primero introduce la máscara: `*.*` conserva nombres, `*.bak` cambia extensión y `<backup_>*.*` agrega un prefijo. Tab alterna mayúsculas/minúsculas y F4 pide confirmación individual. Después elige el destino (el panel opuesto con F8) y la estructura: **Full** conserva la ruta Linux completa debajo del destino, **Current** incluye el nombre de la carpeta de origen y sus subcarpetas, **Relative** conserva solo las subcarpetas. F/C/R elige y Enter acepta. Showall utiliza Full.

Finalmente elige qué hacer con archivos existentes: **Y** reemplaza, **N** pregunta por cada uno, **O** reemplaza solo versiones más antiguas, **V** nunca reemplaza y **R** crea nombres numerados. En la confirmación individual Y/Enter copia, N omite y A acepta los siguientes. Ante errores R reintenta y S omite. Esc cancela los pendientes. La copia conserva las marcas y los originales. F3 recupera máscaras/destinos anteriores; no se crean carpetas sin archivos marcados. Las máscaras avanzadas de secuencias/fecha, find-and-replace y operaciones encadenadas siguen pendientes.

`M` desde archivos mueve únicamente el archivo bajo el cursor, con el mismo flujo de nombres y destino de C. `Ctrl+M` mueve los marcados a una sola carpeta. `Alt+M` mueve los marcados conservando estructura y utiliza las máscaras, Full/Current/Relative, F4 y reemplazos de Alt+C. Respeta Filespec y Tags-Only. Los movimientos completados retiran los archivos y sus marcas del origen; omitidos, fallidos y pendientes permanecen cuando todavía existen. Ambos paneles se actualizan. Se mantienen las carpetas de origen vacías; Mirror sigue pendiente; Alt+P implementa Prune desde el árbol.

Dentro del mismo montaje, Move renombra conservando el objeto y sus metadatos. Entre montajes, completa y sincroniza la copia antes de retirar el origen; conserva permisos rwx y atime/mtime, con las limitaciones de metadatos del motor de copia. Si no logra retirar el origen, informa que quedó conservado; puede quedar la copia terminada en destino. Esc detiene los pendientes y conserva los movimientos ya terminados. Se rechazan enlaces simbólicos, tipos especiales, destinos que sean fuentes y colisiones de máscaras. Un archivo cambiado durante la copia no se elimina. Un cierre abrupto durante la breve retirada del origen puede dejar un archivo `.ltree-move-*` en su carpeta; aún no hay recuperación automática de operaciones interrumpidas.

`Ctrl+D` o `Ctrl+Delete` borra los marcados del listado activo. Primero Enter/Y confirma el lote y N/Esc cancela. Después pregunta **Confirm delete for each file?**: N intenta todos los archivos posibles, continúa ante errores y conserva los que fallaron; Y pide Y/Enter para borrar o N para omitir cada archivo. Esc detiene los pendientes. Con confirmación individual, un error permite R/Enter para reintentar, S para omitir y C/Esc para cancelar. El resumen indica borrados, omitidos y errores. El borrado es permanente y respeta Filespec y Tags-Only.

Desde **Branch**, si se borraron todos los archivos del lote sin errores ni omisiones, relee la rama completa. Si no quedan archivos y la lectura fue correcta, pregunta si querés borrar la carpeta de origen y sus subdirectorios vacíos. Y/Enter confirma esa segunda operación; N/Esc conserva las carpetas. Si quedó contenido sin marcar/filtrado/no cargado, hubo fallos o se canceló, conserva la carpeta. El borrado final vuelve a verificar el contenido real y solo elimina directorios vacíos; un archivo aparecido después permanece intacto.

Los campos de entrada usan cursor de bloque: búsqueda de contenido, Filespec, prefijo, selector L, creación, comparación y búsqueda/posición del visor. El cursor se superpone a la celda sin insertar una raya en el texto.

Después de buscar con **Ctrl+S**, **V** abre el archivo seleccionado con la misma consulta, modo y opción de mayúsculas, situado en la primera coincidencia. **SPACE** continúa por las siguientes; **+ / −** cambia la dirección. La consulta aparece al pie del visor y se conserva al volver al listado y abrir otro archivo, también con F8. Una búsqueda cancelada antes de ejecutarla conserva la anterior. **F** permite reemplazar la búsqueda dentro del visor.

**Expresiones regulares:** en el prompt de **Ctrl+S**, pulsa **F4** hasta seleccionar **regex** (Text → Hex → Unicode → Regex). También está disponible en el prompt de **F** del visor y en **Ctrl+S** dentro de comprimidos. **F2** conserva la opción de mayúsculas. Ejemplos: `error|warning`, `^ORDER_[0-9]+$`, `(?<=ID:)\d+` y `\p{L}+`. Se usa la sintaxis de [Qt/PCRE2](https://doc.qt.io/qt-6/qregularexpression.html), sobre texto Unicode con detección UTF-8/UTF-16/UTF-32. Los patrones se evalúan por línea: `^` y `$` se refieren a esa línea y las coincidencias no cruzan sus límites. Un patrón inválido conserva el prompt y las marcas para corregirlo.

**V** hereda Regex y **SPACE / + / −** recorre las coincidencias, incluidas las de longitud cero. El visor muestra Regex en Alpha/Wrap; elegir Regex desde Hex/Dump pasa a Alpha. Las consultas se guardan en los historiales existentes; al recuperarlas, selecciona el modo apropiado con F4. La memoria por línea está limitada a 8 Mi caracteres UTF-16 y PCRE2 tiene límites de trabajo/memoria; si se exceden, informa un error y permite cancelar u omitir el archivo. Los archivos con codificación inválida también se informan. El prototipo de terminal aún no tiene búsqueda de contenido.

Durante la búsqueda, el selector sigue al archivo que se está leyendo. El pie conserva los paneles y muestra consulta, ruta, **Hits** (archivos con coincidencias), spinner, barra y porcentaje, tiempo transcurrido, tiempo restante estimado y velocidad en MB/s. La lectura actualiza el avance dentro de archivos grandes; Esc cancela conservando las marcas pendientes.

En **F → Filespec**, **↑** abre el historial con la máscara más reciente seleccionada; **↓** empieza por la más antigua. **Enter** recupera la máscara para editarla y otro Enter la aplica. Esc vuelve a la entrada previa. La lista permite PgUp/PgDn/Home/End, marcas con Ins o Ctrl+letra/número, filtros, ordenación, borrado y Append, igual que la lista de búsquedas. F3 desde el prompt recupera la última máscara; en la lista, **F3 guarda** y **F4 recarga**.

Ambos historiales se cargan al iniciar y se guardan al cerrar si cambiaron: `~/.config/ltreec/filespec-history.json` y `~/.config/ltreec/search-history.json` (respetando XDG_CONFIG_HOME). Al abrir por primera vez esta versión, las búsquedas del archivo anterior en `~/.local/share/ltreec/search-history.json` se importan automáticamente si el nuevo aún no existe; el original se conserva.

En el prompt de búsqueda `Ctrl+S`, **↑** abre una lista del historial con la búsqueda más reciente seleccionada abajo; **↓** empieza por la más antigua. Usa cursores, PgUp/PgDn y Home/End. **Enter** recupera la consulta al prompt para editarla; otro Enter busca. **Esc** cierra la lista conservando la entrada previa. F3 desde el prompt recupera la última consulta directamente. El historial se guarda al cerrar y se recupera al iniciar, en `~/.config/ltreec/search-history.json` (o la ubicación XDG correspondiente).

En la lista, Del elimina entradas sin marca e Ins protege/desprotege una entrada. Ctrl+letra/número asigna un acceso directo; esa tecla recupera la entrada, Ctrl+tecla lo hace desde el prompt y Alt+tecla recupera y busca. Ctrl+C/V/X quedan reservados. ←/→ filtra entre todas, marcadas, accesos directos y sin marca; Alt+←/→ alterna orden por marca o texto. `+`/`|` agrega la entrada al texto sin separador; Esc permite editar después. Ctrl+Insert copia al portapapeles. F3 guarda y F4 recarga este historial; Alt+↑ desde el prompt guarda la entrada sin buscar. Se conservan 64 consultas sin marca y las marcadas quedan protegidas.

`F8` divide o une la vista; `Tab` cambia de panel. `Ctrl+F6` copia marcas al otro lado. `Shift+F8` permite intercambiar lados y elegir estadísticas. Al unir se conserva el lado activo.

`?` o `/` desde el árbol o archivos abre las estadísticas extendidas, con la disposición de dos columnas de ZTreeWin: montaje, archivos cargados, sistema, pantalla y sesión. **F3** actualiza; **Ctrl+F3** alterna actualización cada segundo; **< / >** cambia de montaje sin mover los paneles. **Esc/Enter** vuelve conservando la posición y marcas. **F1** explica los valores Linux. A 80×25, cursores/PgUp/PgDn/Home/End o rueda permiten ver todas las secciones. El tamaño de bloque reemplaza al clúster; sectores físicos y compresión NTFS aparecen como N/A.

`V` abre el visor Alpha/Hex/Dump/Junk/Wrap con menús VIEW COMMANDS, Ctrl y Alt, charset, máscara, regla, líneas, Gather, historial, bookmarks y recarga automática. `Ctrl+V` recorre marcados. **H y E** permite sobrescribir bytes con confirmación Y/N; Tab alterna Hex/caracteres y F8 deshace. `Enter` navega el comprimido seleccionado; `F5` y `Ctrl+F5` crean comprimidos del actual o marcados; `F3 Format` elige ZIP, TAR, TAR.GZ/BZ2/XZ, 7Z o GZ/BZ2/XZ de un solo archivo. La ruta admite cursores, selección y portapapeles. El visor conserva su límite de 32 MiB; ver [controles y límites de visor/comprimidos](VISOR_Y_COMPRIMIDOS.md).

`J` desde archivos propone el mismo nombre en la carpeta del otro panel. Enter abre la comparación en caracteres, incluso para binarios; H cambia a Hex y otra H vuelve. Puede escribirse otro nombre en el prompt. Espacio/+ y − recorren diferencias; Esc vuelve a los paneles.

`Ctrl+J` compara directamente dos archivos marcados del listado activo. Con uno marcado, compara el archivo bajo el cursor con ese marcado. Funciona también en Branch/Showall y sin F8; Esc vuelve conservando marcas y selección.

En el comparador, `C` alterna sensibilidad a mayúsculas, `W` comprime espacios/tabulaciones y `E` omite líneas vacías. El pie indica sus estados. Para probarlas, crear dos textos con diferencias de mayúsculas, espacios y líneas vacías, marcar ambos con T y pulsar Ctrl+J. Activar C, W y E deja el texto equivalente; H conserva las diferencias de bytes originales.

`X` abre una consola negra dentro de la ventana, con la fuente aprobada. Se inicia el shell de Linux (`$SHELL`, Bash como alternativa) en la carpeta activa; en Branch/Showall usa la carpeta del archivo seleccionado. Podés escribir cualquier comando y pulsar Enter, usar historial y autocompletado del shell, tuberías y programas interactivos. Ctrl+C interrumpe el programa en primer plano. `exit` o Ctrl+D termina el shell y vuelve; Esc vuelve cuando el shell está en primer plano. Al regresar se actualiza esa carpeta y se conservan filtros, marcas, selección y paneles F8. Cambiar de carpeta con `cd` dentro del shell no cambia la ubicación de LTree Commander. Los comandos ejecutados tienen los permisos normales de tu usuario y pueden modificar archivos.

`F1` muestra las teclas disponibles. `Alt+Enter` alterna pantalla completa. Puede pasarse otra carpeta como argumento; sin argumento se abre la carpeta personal.

Ctrl+S desde archivos busca sobre los marcados. Para probarlo sobre una carpeta propia: Enter, Ctrl+T, Ctrl+S, escribir el texto y confirmar. Enter vuelve al árbol y Ctrl+S desde el árbol muestra los resultados marcados en Showall. Las pruebas automatizadas crean sus propios árboles temporales, incluido el caso de 174 archivos y 17 resultados; no necesitan proyectos reales ni un demo descargado.

## Operaciones incorporadas al cierre de la alpha

- **Ctrl+A** en archivos aplica permisos POSIX a los marcados del listado. Acepta `600`, `go-w` o `a+X`, calculados por archivo. Enter revisa y Y aplica; N/Esc cancela. Conserva las marcas, informa fallos y permite detener los pendientes. No sigue enlaces ni eleva privilegios.
- **R** renombra el archivo o directorio actual; **Ctrl+R** renombra los marcados en sus respectivas carpetas con una máscara, por ejemplo `*.bak`. Enter revisa y Y aplica. F4 en Ctrl+R activa confirmación individual; N omite, A acepta los siguientes y Esc cancela. Conserva carga, selección y marcas en ambos paneles. No reemplaza destinos; colisiones y swaps se rechazan. R usa un nombre literal; secuencias, find-replace, deshacer y recuperación tras cierre abrupto siguen pendientes.
- **C** en DIR COMMANDS compara el directorio; **Alt+C** compara la rama cargada. El destino se lee del disco. El prompt permite Identical por tamaño/fecha, Unique/Newer/Older, Size smaller/larger/different y Binary same/different; F12 restablece. Al éxito sustituye las marcas del ámbito fuente; fallos o cancelación conservan las previas. Comparación de rama por rutas relativas, sensible a mayúsculas por defecto. Fechas de caché en milisegundos; tolerancias pendientes.
- **Alt+F4** filtra la lista por nombres duplicados/únicos, tamaño exacto, contenido binario y fechas iguales/nuevas/antiguas. En GNOME usar **F4, F4, K** o Alt+K para evitar el Alt+F4 del compositor. F2 cambia case y 0 incluye/excluye vacíos. Cada filtro estrecha la lista actual; A restaura. Ctrl+T marca los resultados. Salir al árbol o relogar restaura el orden y el listado. Comparar contenido de grupos grandes puede ser lento y admite Esc.
- **Ctrl+F7/F8/F9** en Tree marca, desmarca o invierte los coincidentes con Filespec en la rama cargada.

## DIR COMMANDS: Avail, Global y enlaces

- **A — Avail** muestra capacidad y espacio disponible de los montajes y ubicaciones. Flechas seleccionan, F5 actualiza y Enter/Esc vuelve; conserva la carpeta activa.
- **G — Global** reúne los archivos ya registrados de todas las ubicaciones abiertas con L. Respeta Filespec y las marcas. **Ctrl+G** muestra únicamente los marcados coincidentes. Enter/Esc restaura el árbol original; las operaciones se reflejan en las ubicaciones guardadas y en F8. No carga discos nuevos.
- **H — sHortcut** crea un enlace simbólico al directorio seleccionado. Escribe la **ruta completa del nuevo enlace** y pulsa Enter; Esc cancela. Una ruta relativa se interpreta desde el directorio seleccionado; `~/` apunta a tu carpeta personal. El padre debe existir y ningún nombre se reemplaza. Con F8 sugiere el directorio opuesto.
- **< / >** recorren las ubicaciones **ya registradas**, como los discos cargados del original. También funcionan coma y punto; conservan la posición y las marcas y solo cambian el panel activo. Usa L para registrar otra ubicación.

Oops con O en DIR COMMANDS se omite por decisión del autor. O en FILE COMMANDS sigue abriendo archivos. Los enlaces creados pueden usarse desde Linux; recorrer directorios enlazados dentro de LTree continúa pendiente.

## Alt+P: Prune

En el árbol, `Alt+P` elimina la carpeta seleccionada, sus subdirectorios cargados y los archivos que contienen. Las marcas y Filespec no limitan esta operación. Escribir `PRUNE` y pulsar Enter confirma; una rama incompleta pide además Y/N para operar solo sobre lo cargado.

- **F5** conserva la carpeta actual y sus propios archivos.
- **F6** elimina únicamente directorios vacíos.
- **F2** permite borrar archivos de solo lectura, definidos en Linux como archivos sin bits de escritura.
- **F4** utiliza la papelera de Linux; si falla, conserva el objeto e informa el error.

Todas las opciones comienzan en no. **F1** explica las opciones y **Esc** cancela o detiene los pendientes. El borrado permanente no tiene deshacer. No recorre destinos de enlaces ni montajes; las carpetas no cargadas se conservan. Véanse los límites de concurrencia y papelera en [IMPLEMENTACION.md](IMPLEMENTACION.md).

## Alt+G: Graft

En el árbol, **Alt+G** mueve la carpeta seleccionada y todo su contenido, incluido lo que aún no está cargado. Las marcas y Filespec no limitan la operación. Escribe una carpeta de destino existente y pulsa **Enter**: trasladar `alpha` a `beta` crea `beta/alpha`. El nombre original se conserva; un destino que ya contenga ese nombre se rechaza sin reemplazar ni mezclar contenido.

- **Tab / Shift+Tab** recorren las rutas disponibles; con F8 se propone inicialmente la carpeta del otro panel.
- **F2** abre el árbol para elegir el destino. Enter recupera la carpeta al prompt; otro Enter ejecuta.
- **F3** recupera el último destino y **↑ / ↓** abren el historial, guardado en `~/.config/ltreec/graft-history.json` (respetando XDG_CONFIG_HOME).
- **F1** muestra ayuda y **Esc** cancela o detiene el traslado.

Dentro del mismo sistema de archivos se usa un renombrado. Entre sistemas de archivos se utiliza GNU `mv`, conservando los atributos que permitan el destino y los permisos del usuario. Una interrupción entre sistemas de archivos puede dejar contenido en ambos lados; el programa informa los errores y las rutas de recuperación. No se trasladan ramas con puntos de montaje ni se siguen enlaces simbólicos dentro de ellas. Más detalles en [IMPLEMENTACION.md](IMPLEMENTACION.md).

## Alt+F: File display

**Alt+F**, desde el árbol o el listado de archivos, recorre cuatro formatos:

1. **Name:** nombre y extensión separados, en varias columnas.
2. **Name, size and attributes:** añade tamaño y atributos, en varias columnas cuando hay espacio.
3. **Name, size, attributes and date:** detalle en una columna, con fecha y hora; la hora se oculta si el panel es estrecho.
4. **Long name:** nombre completo con la extensión contigua, en una columna.

Se conserva inicialmente el formato de detalle. En F8, el formato es común a ambos paneles; sus selecciones, marcas, filtros y orden permanecen independientes. Funciona también en Branch, Showall, Global y Autoview. En los modos de columnas, las flechas izquierda/derecha avanzan una columna y PgUp/PgDn una página completa; el ratón selecciona y abre la entrada bajo el puntero.

**Shift+←/→** ajusta el espacio destinado a la extensión en los formatos con campos separados; **Shift+Home** restaura su posición. **F4, F4, F** permite cambiar de formato sin sostener Alt. La elección se mantiene durante la sesión; todavía no se guarda como preferencia para el siguiente inicio.


## Prueba dentro de la terminal

```bash
ltc --terminal /ruta/de/trabajo
```

El modo se elige automáticamente: con `DISPLAY` o `WAYLAND_DISPLAY` se abre la ventana gráfica; sin ambas variables se usa la terminal. Se respeta una plataforma `QT_QPA_PLATFORM` definida explícitamente. La detección usa el entorno de la sesión; no comprueba conexiones a servidores gráficos indicados por variables obsoletas.

`--terminal` fuerza el prototipo dentro de la terminal aunque exista una sesión gráfica, por ejemplo desde Kitty en GNOME. En una consola de texto o una sesión SSH sin entorno gráfico basta `ltc /ruta/de/trabajo`. Este modo dibuja dentro de la terminal, sin abrir una ventana Qt ni necesitar DISPLAY/Wayland. Sin ruta inicial usa el directorio actual. La instancia única se comparte con la interfaz gráfica; para probar ambas a la vez usa `ltc --terminal --new-instance /ruta/de/trabajo`.

Incluye árbol/listado, carga con `+` y `*`, Branch/Showall, marcas, Filespec, formatos Alt+F, F7 Autoview, paneles F8, selector L y visor texto/hex. **Enter** alterna árbol/archivos, **F8** divide, **Tab** cambia panel, **V** abre el visor y **H** cambia texto/hex. **L** muestra montajes; desde allí **/** o **P** permite escribir una ruta. **Q**, seguido de **Y**, sale y devuelve el control a la terminal. **F1** muestra los controles disponibles.

**F7 Autoview** mantiene el listado a la izquierda y muestra el archivo seleccionado a la derecha, también dentro del panel activo de F8. Las flechas normales cambian de archivo; **Shift+H** alterna texto/hex y **Shift+A** vuelve a texto. **Shift+↑/↓/PgUp/PgDn/Home/End** desplaza la vista previa; **Alt+←/→** ajusta su ancho y **Alt+Home** restaura la proporción inicial. **F7**, **Enter** o **Esc** cierra la vista previa y recupera la vista de origen. Tab/F8 la cierran antes de cambiar de panel. En paneles demasiado estrechos se oculta hasta ampliar la ventana. La lectura se hace en segundo plano, con el límite de 32 MiB del visor.

La fuente la proporciona el emulador de terminal. En terminales de 256 colores, el programa usa amarillo/cian definidos y azul oscuro/gris cercanos a la paleta gráfica; no depende de los primeros 16 tonos del tema ni los redefine. En terminales con color directo usa los RGB del esquema gráfico; con 8/16 colores depende de los tonos disponibles. Los historiales de Filespec de este prototipo viven solo durante la sesión. Todavía no incluye búsqueda de contenido, copiar/mover/borrar, JFC, consola X, Global ni todos los menús del programa gráfico. Los glifos que ocupen varias celdas se muestran como `?` para conservar la alineación; el visor recorta las líneas al ancho disponible. La versión gráfica conserva sus funciones y continúa siendo la interfaz completa de la alpha.

### N / Ctrl+N — New date

En FILE COMMANDS, `N` cambia la fecha del archivo actual; `Ctrl+N` cambia los archivos marcados del listado visible, respetando Filespec y el alcance Directory/Branch/Showall/Global. `F4` y luego `N` permite ejecutar la variante Ctrl desde el menú.

El prompt **STAMP** conserva el listado. `Enter` aplica y `Esc` cancela; durante un lote, detiene los archivos restantes y conserva los cambios completados. `F2` inserta la fecha/hora actual, `Tab` recupera la fecha del archivo resaltado y `F3` el último valor utilizado. `F4` alterna modificación, acceso o ambas. La creación y ctime no se ofrecen como campos editables en Linux.

`F5` alterna **set to**, **adjust** y, para marcados, **increment**. Se puede escribir `yyyy-MM-dd HH:mm:ss`, solo la fecha o solo la hora: el componente omitido se conserva en cada archivo. Ajustes: `+1y-5n`, `-3d+10s`; y/años, m/meses, d/días, h/horas, n/minutos, s/segundos. `e` redondea hacia arriba al siguiente segundo par. Adjust parte de la fecha de cada archivo; Increment aplica el ajuste al primero y continúa a partir del valor generado anterior, en el orden del listado.

`Up/Down` abre el mismo tipo de historial que Search y Filespec; se guarda en `~/.config/ltreec/stamp-history.json` (o bajo `XDG_CONFIG_HOME`). F1 muestra ayuda. Se rechazan enlaces, archivos especiales y archivos cambiados desde que se abrió el prompt. Los paneles y sus ubicaciones guardadas se actualizan al finalizar, conservando las marcas de archivos presentes. Esta función corresponde a la interfaz gráfica.

La precisión y el rango final dependen del sistema de archivos. Se comprueba la fecha almacenada y se informa si difiere por el rango soportado o por un cambio concurrente; los cambios ya aplicados no se revierten automáticamente. Los enlaces duros comparten sus timestamps.
