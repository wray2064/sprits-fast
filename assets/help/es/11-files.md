# Abrir, guardar y exportar

## Tus archivos

El archivo propio de Fast es `.lsprite`. Lo conserva todo: capas, elementos, fotogramas, paletas,
ciclos, cortes, referencias, marionetas y la vista en la que lo dejaste. Un archivo escrito por Fast
se abre en versiones posteriores, y todo lo que contenga que esta versión no entienda se vuelve a
escribir sin cambios.

## Abrir

**Archivo > Abrir...** (`Ctrl+O`), arrastrar y soltar sobre la ventana, o doble clic en el Explorador.
Lee:

| Archivo | Lo que obtienes |
|---|---|
| `.lsprite` | la obra, exactamente |
| `.aseprite`, `.ase` | la obra: capas (nombres, opacidad, visibilidad, fusión, grupos), fotogramas y sus duraciones, etiquetas como ciclos, la paleta, mosaicos |
| PNG, JPEG, BMP | un dibujo separado por colores; con 256 colores o menos los colores pasan a ser la paleta |
| GIF | todos los fotogramas, con la duración que indicaba el GIF |

Guardar una imagen abierta pregunta dónde va el `.lsprite`: la imagen de la que vino nunca se
sobrescribe. Una fotografía con miles de colores se rechaza con la sugerencia de importarla como
[referencia](topic:references).

**Pestañas**: cada archivo abierto o creado va a una pestaña junto a la obra en vez de sobre ella (un
documento en blanco y sin tocar simplemente se reemplaza). `Ctrl+Tab` pasa de una a otra, `Ctrl+W`
cierra una. Un archivo ya abierto se muestra, no se abre dos veces. **Archivo > Abrir reciente** lista
los últimos.

## Guardar

- **Guardar** (`Ctrl+S`), **Guardar como...** (`Ctrl+Shift+S`), **Guardar una copia...** (escribe en
  otro sitio sin cambiar qué archivo estás editando).
- Los guardados son atómicos: un archivo se escribe entero o no se escribe, así que un fallo nunca
  deja medio sprite.
- Cerrar una pestaña o salir pregunta por cada documento con trabajo sin guardar.

## Recuperación

Mientras tienes cambios sin guardar, se escribe una copia del documento cada un par de minutos en la
carpeta de ajustes del programa. **Nunca se escribe sobre el archivo que estás editando.** La copia se
borra cuando guardas correctamente o cierras con normalidad, así que una copia encontrada al arrancar
significa que la última sesión terminó mal. Fast la ofrece: **Abrir** la carga como documento sin
guardar (tú eliges dónde va de verdad), **Descartar** la tira, **Más tarde** la deja para la próxima.

**Archivo > Autoguardado** lo desactiva, o **Escribir una copia ahora**. El intervalo está en las
Preferencias (*Autoguardar una copia cada...*).

## Exportar

Exportar compila con calidad de exportación, deja tu documento intacto, y exportar no es guardar.

- **Archivo > Exportar PNG** escribe una imagen de 1x a 16x. Escalar duplica píxeles enteros, nunca
  interpola. **Indexado** escribe una tabla de colores y un byte por píxel, con las ranuras de la paleta
  en su orden como primeras entradas de la tabla: la ranura 3 es el índice 3. Un juego que cambia de
  paleta en ejecución cambia la tabla.
- **Archivo > Exportar animación...** escribe el ciclo seleccionado con su modo de bucle, o todos los
  fotogramas, como **GIF animado** (se reproduce en todas partes; 256 colores por fotograma y sin
  transparencia parcial; la línea de estado avisa si se redujo un fotograma), **PNG animado** (todos los
  colores y todos los niveles de transparencia exactos), **WebP animado** sin pérdida o una **secuencia
  de PNG** numerada.
- **Archivo > Exportar hoja...** coloca los fotogramas en una sola imagen: una cuadrícula, una fila o una
  columna, a una escala entera, del ciclo seleccionado o de todos los fotogramas. **Borde** y
  **separación** evitan que un filtro de textura contamine un fotograma con el siguiente; **Recortar**
  corta cada celda al menor rectángulo que contiene lo que dibuja cualquier fotograma (un solo rectángulo
  para todos, de modo que las celdas encajan). Al lado va un pequeño `.json` con la posición y la
  duración de cada celda, los nombres de los fotogramas, los ciclos y las cortes, con el formato
  propio de Fast o con el JSON que escribe Aseprite (por clave o en lista), que ya leen los importadores
  de motores de juego hechos para Aseprite.
- **Archivo > Exportar de nuevo** (`Ctrl+Shift+E`) repite la última exportación en el mismo archivo con
  los ajustes tal como están ahora.

Cada celda de una hoja es, byte por byte, lo que da exportar ese fotograma solo.

## Paletas

**Cargar** y **Guardar** en el panel Paleta leen y escriben `.gpl`, `.hex`, `.pal` y `.act`. Consulta
[Color y paletas](topic:colour).

## Desde la línea de comandos

La descarga gratuita (no la versión de la Store, que no tiene alias de línea de comandos) exporta sin
ventana, para un script de compilación:

```
sprits_fast --export hero.aseprite --to hero.gif --scale 4 --cycle walk
```

La extensión de `--to` dice qué se escribe: `.png` es un fotograma (`--frame N`), o con `--sheet` una
hoja de sprites y su descripción (`--border`, `--spacing`, `--trim`, `--json hash` o `--json array`),
`--sequence` un archivo por paso, `--animated` un PNG animado; `.gif` es la animación; `.webp` es un
fotograma, o con `--animated` la animación; `.lsprite` convierte un `.aseprite`, PNG o GIF en un
documento; `.gpl`, `.hex`, `.pal` o `.act` escribe la paleta. El código de salida es 0 si todo fue
bien, y un fallo dice por qué.
