# Visor y comprimidos de la interfaz gráfica

Este incremento sigue el manual §3.8 y conserva la fuente DOS 8×16, fondo azul, etiquetas cian, teclas amarillas y cursor de bloque. Los menús VIEW COMMANDS cambian al mantener Ctrl o Alt. F10 permite elegir el menú Ctrl/Alt para una sola orden sin interferir con F4 del portapapeles ni los atajos de ventanas de GNOME; Esc o una tecla ajena cancela ese menú. El prototipo ncurses conserva su alcance anterior.

## Visor

| Tecla | Acción |
|---|---|
| A / D / H / J / W | Alpha, Dump, Hex, Junk y Wrap; repetir alterna con el modo anterior |
| C / Shift+C | Auto, OEM CP437, ANSI Windows-1252, UTF-8, UTF-16LE/BE y EBCDIC IBM037 |
| L / M / R / Tab | Línea resaltada, máscara de presentación, regla y tabulaciones 2/3/4/6/8 |
| Cursores, PgUp/PgDn, Home/End | Navegar; L mueve la línea antes de desplazar la página |
| Ctrl+cursores / Ctrl+Home/End | Desplazar conservando la línea; 20 columnas; extremos horizontales |
| O / 0..9 | Línea original o offset hexadecimal |
| Ctrl+0..9 / Alt+0..9 | Crear/recuperar bookmark del archivo actual |
| F / B / S / F9 / `/` / `\` | Buscar desde inicio/final/página, adelante o atrás |
| Space / + / − / F8 | Repetir, cambiar dirección y detenerse por hit/página |
| Ctrl+S | Alternar las dos consultas recientes |
| G / F4 / F5 / Ctrl+C / Ctrl+Insert | Gather, copiar o agregar líneas al portapapeles |
| F2 / F6 | Vaciar portapapeles; cambiar la separación al agregar líneas |
| F3 / Ctrl+F3 | Recargar una vez o cada segundo |
| Shift+F2..F6 / Shift+F7 | Desplazamiento automático a cinco velocidades; ciclo |
| E / Alt+E | Sublime/editor habitual o LTREE_ALT_EDITOR; en Hex ambos abren edición de bytes |
| X / Alt+X | Shell integrado o shell independiente en Kitty |
| F10, F10, F5 / Alt+F6 | Cambiar primer plano/fondo del visor durante esa sesión |
| Alt+F7 / Alt+F8 / Alt+F9 | Maximizar/restaurar; ciclar ancho/alto |
| F1 / Esc / Q | Ayuda contextual; cerrar el visor |

En el prompt de búsqueda, F2 cambia case, F3 recupera la consulta, F4 alterna Text/Hex/Unicode, F8 cambia hit/página y Up/Down abre el historial. Enter vacío limpia la búsqueda. Hex desde Alpha cambia a Hex para mostrar offsets en bytes; Text y Unicode pueden buscarse desde Hex. Unicode en Hex busca UTF-16LE o BE según BOM/charset. Las búsquedas de texto y los asteriscos internos no atraviesan saltos de línea; una consulta con asterisco inicial se trata literalmente, como la búsqueda por lotes existente.

Ctrl+V desde el listado abre los marcados de la vista actual. N/P cambia de archivo, U desmarca, Alt+Home/End va al primero/último. Una búsqueda explícita o Space puede continuar en otro marcado. Se conservan el modo del visor y el contexto del listado. Alt+V utiliza LTREE_ALT_VIEWER si existe, o el visor interno. Las variables de editor/visor alternativo se separan en ejecutable y argumentos con QProcess; el nombre del archivo se agrega como argumento, sin interpolar un shell.

Los historiales del visor y los nombres de salida de Gather persisten en `~/.config/ltreec/viewer-history.json` (o XDG_CONFIG_HOME). Se mantienen separados de search-history.json y filespec-history.json. El override LTREE_HISTORY_FILE aísla las pruebas. Bookmarks, colores, modo y velocidades permanecen en memoria.

## Gather

G inicia la selección. Cursores/Home/End eligen el comienzo; Enter lo fija. Luego eligen el final y Enter abre el destino. F4 copia las líneas seleccionadas y F5 las agrega al portapapeles. En la primera etapa copian solamente la línea actual. F6 introduce separación antes de agregar contenido.

El destino puede ser un archivo, `CLIP:` o `PRN`. Un archivo recibe texto UTF-8 al final y se crea si no existe. F3/Up/Down recupera nombres anteriores. PRN abre el diálogo de impresión Qt/Linux, que permite elegir impresora; no se emulan dispositivos DOS LPTn ni parámetros Batch. La salida física a una impresora queda por probar.

## Editar Hex

1. V, H, E.
2. Escribir dos dígitos por byte. Tab cambia al área de caracteres de un byte.
3. Cursores/Home/End mueven el bloque; F8 deshace todos los cambios sin guardar de la página.
4. Enter solicita **Save changes to this file? Y/N**. PgUp/PgDn también confirma antes de guardar y avanzar. N conserva la edición; Esc descarta.

Solo sobrescribe bytes existentes de la página inicial; conserva longitud, inodo y enlaces duros. Rechaza enlaces simbólicos, rutas con padres simbólicos, archivos especiales, cambios externos detectados y rangos inválidos. Antes de escribir verifica identidad, tamaño, ctime y contenido original. Ante error de escritura intenta restaurar los bytes originales e informa si la restauración quedó incompleta. No usa privilegios. El límite del visor sigue siendo 32 MiB por archivo.

## Comprimidos

| Tecla desde el listado normal | Acción |
|---|---|
| V | Listado textual del comprimido; otro modo muestra sus bytes originales |
| Enter | Abrir el navegador del comprimido seleccionado; Esc regresa al listado |
| F5 | Crear comprimido del archivo actual |
| Ctrl+F5 | Crear comprimido con los marcados de la vista actual, incluido Branch |

El navegador conserva la estética del listado y ocupa la ventana hasta salir con Esc. Enter abre una carpeta o un miembro; Backspace sube. B muestra toda la rama, F filtra por nombre y T/U o Ctrl+T/U marca/desmarca. C/E extrae el elemento o carpeta actual, Ctrl+C/E los marcados y Alt+C/E los marcados con estructura. El destino inicial es el directorio del otro panel F8 cuando existe. F2 cambia la conservación de rutas. F4 permite reemplazar; reemplazar requiere Y/N antes de iniciar. Una colisión sin reemplazo informa el error y conserva el destino.

Ctrl+S busca entre los miembros marcados y conserva las marcas con coincidencias. Reutiliza los modos Text/Hex/Unicode del motor de búsqueda. Descomprime un miembro a la vez en un directorio temporal privado y lo borra al terminar, permitiendo buscar miembros mayores de 32 MiB sin cargarlos enteros en RAM. Los miembros abiertos con V sí tienen ese límite. F9 permite indicar contraseña para lectura de comprimidos cifrados.

El prompt de creación permite editar la ruta con Left/Right/Home/End, Backspace/Delete, selección con Shift, Ctrl+A y portapapeles Ctrl+C/X/V. El cursor es de bloque y la ruta se desplaza para mantenerlo visible. Esto también se aplica a los prompts de extracción, Filespec, búsqueda y contraseña.

**F3 Format** abre un selector con ZIP, TAR, TAR.GZ, TAR.BZ2, TAR.XZ, 7Z, GZ, BZ2 y XZ. Up/Down/Home/End eligen; Enter aplica y ajusta la extensión conservando el nombre; Esc cancela. También se reconoce la extensión escrita al aceptar, incluidos TGZ/TBZ/TBZ2/TXZ. Una ruta sin extensión recibe la del formato elegido; una extensión desconocida se rechaza. GZ/BZ2/XZ comprimen un solo archivo; para varios se elige ZIP/7Z/TAR o una variante TAR comprimida. Al elegir un stream desde el nombre sugerido se conserva la extensión del archivo original, por ejemplo `notes.txt.gz`.

ZIP se escribe con deflate, nombres UTF-8 y rutas relativas, compatible con lectores de Windows y Linux. El comprimido se publica al terminar; errores o cancelación conservan un destino anterior. F2 permite aplanar rutas; nombres repetidos se rechazan antes de comprimir. TAR usa pax restringido para rutas largas y UTF-8; 7Z utiliza el writer nativo de libarchive. Los padres del comprimido deben existir. Las fuentes deben ser archivos regulares dentro de la base seleccionada.

Lectura con libarchive: ZIP, TAR y sus filtros gzip/bzip2/xz/zstd, archivos individuales comprimidos, 7z, RAR/RAR5 y otros formatos activados del motor. ZIP cifrado con contraseña, TAR, TAR.gz, gzip y 7z se prueban con fixtures; RAR y todas sus variantes siguen pendientes de fixtures específicos. El reconocimiento inicial de V usa extensiones; Enter solicita al motor validar los comprimidos reconocidos. No se utiliza ARCHIVER.BB2 ni detección completa por firmas.

La extracción rechaza rutas absolutas, `..`, unidades Windows, enlaces simbólicos/duros y miembros especiales. No sigue enlaces en los directorios de destino ni reemplaza un enlace/archivo especial. Crea archivos temporales y publica solamente miembros leídos sin error. Conserva permisos ordinarios y mtime de archivos; no restaura propietarios, ACL/xattrs ni metadatos completos de directorios. Los archivos ya extraídos permanecen al cancelar; el archivo temporal en curso se elimina. La detección de cambios del comprimido obliga a recargar el catálogo antes de otra operación.

Los miembros se ven como lectura sola: **extraer → editar → crear un nuevo ZIP**. Aún no se editan ni se reemplazan miembros dentro del comprimido, ni se crean ZIP cifrados. Los comprimidos anidados pueden verse como listado textual; para navegar/extractar otro nivel se extraen primero. Catálogos limitados a 100.000 entradas. La cancelación se comprueba entre bloques/miembros; una lectura del kernel o una operación interna del descompresor puede demorar su respuesta.

El navegador usa Enter; Alt+F5 se conserva para GNOME y no se ofrece como atajo de apertura. Enter sobre archivos normales conserva Tree/File; Esc permite volver al árbol desde cualquier archivo. No se modifican los atajos globales del escritorio. Alt+F4 sigue disponible, y **Alt+K o F4/F4/K** ofrece la alternativa que no interfiere con el cierre de ventanas de GNOME.

## Referencias de implementación

- Manual privado §3.8 y comandos de archivo comprimido, fuera de la distribución pública.
- [Formatos de escritura](https://github.com/libarchive/libarchive/blob/master/libarchive/archive_write_format.3) y [filtros de compresión](https://github.com/libarchive/libarchive/blob/master/libarchive/archive_write_filter.3).
- [Ejemplos de libarchive](https://github.com/libarchive/libarchive/wiki/Examples) y [API pública](https://github.com/libarchive/libarchive/blob/master/libarchive/archive.h).
- Pruebas en tests/test_view_archive.cpp; controles anteriores en tests/test_ltree.cpp.
- Este incremento no acredita comparación diferencial completa contra ZTreeWin.

## Búsqueda opcional con expresiones regulares

F4 en el prompt de búsqueda alterna Text, Hex, Unicode y Regex. Funciona en Ctrl+S sobre archivos marcados, en F del visor y en Ctrl+S de miembros de comprimidos. F2 alterna mayúsculas. Regex usa Qt/PCRE2 sobre Unicode con detección UTF-8/UTF-16/UTF-32, por línea, sin coincidencias entre líneas; ^/$ anclan cada línea. Se validan los patrones antes de ejecutar y de guardar la consulta. Un error de sintaxis mantiene el prompt y las marcas.

V conserva consulta, modo y sensibilidad; Space y +/- navegan y resaltan coincidencias de longitud variable. Las coincidencias vacías avanzan por caracteres completos, sin bucles. Elegir Regex desde Hex/Dump pasa a Alpha. El historial guarda la cadena en sus archivos actuales; F4 selecciona cómo interpretarla al recuperarla.

Se limita cada línea a 8 Mi unidades UTF-16 y se acotan recursos PCRE2: un límite excedido produce error en lugar de interpretarlo como ausencia de coincidencia. Se conservan progreso, spinner, cancelación y políticas de errores existentes. No incluye búsqueda multilínea ni búsqueda de contenido en el prototipo de terminal. Ejemplo: `^ORDER_[0-9]+$`. Sintaxis: https://doc.qt.io/qt-6/qregularexpression.html y límites: https://www.pcre.org/current/doc/html/pcre2pattern.html.
