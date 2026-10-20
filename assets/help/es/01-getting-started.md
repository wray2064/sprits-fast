# Primeros pasos

Cinco minutos desde un lienzo vacío hasta un sprite que puedes usar.

## 1. Empieza

Cuando se abre el programa muestra la **pantalla de inicio**.

- A la izquierda, las formas de entrar: **Abrir...**, **Importar hoja de sprites...**, la
  **Biblioteca** y esta **Guía de usuario**.
- En el centro, una tarjeta por cada tamaño preestablecido y, debajo, los archivos que abriste
  hace poco, cada uno con su propia imagen (la primera vez hay tres accesos a esta guía en su lugar).
- A la derecha, los datos del documento nuevo (nombre, tamaño, fondo y paleta inicial) y un consejo debajo.

Elige una tarjeta (**Tile 32 x 32** es un buen primer ejemplo) y pulsa **Crear**, o elige
**Empezar con un lienzo en blanco**. Para un lienzo de tu propio tamaño, fija el ancho y el alto a
la derecha. **Archivo > Nuevo** (`Ctrl+N`) vuelve a mostrar la pantalla de inicio con las mismas preguntas.

> Puedes volver a la pantalla de inicio cuando quieras con **Archivo > Pantalla de inicio**.

## 2. Dibuja

1. El lápiz ya está elegido. Pulsa `B` para volver a él.
2. Haz clic en un color del panel **Paleta**, a la izquierda, y luego haz clic y arrastra sobre el lienzo.
3. Pulsa `E` para el borrador, `G` para rellenar un área, `I` para tomar un color del lienzo.
4. Pulsa `R` para un rectángulo, `U` para una elipse, `L` para una línea. Arrástralos y luego
   arrastra sus tiradores: una forma sigue siendo una forma.
5. La rueda del ratón hace zoom, `Espacio` y arrastrar desplaza la vista, `Ctrl+0` ajusta el
   lienzo a la ventana.

¿Te has equivocado? `Ctrl+Z` deshace y `Ctrl+Shift+Z` (o `Ctrl+Y`) rehace, hasta donde llegue la
profundidad de deshacer de las Preferencias.

## 3. Usa capas

El panel **Capas**, a la derecha, lista las capas. **Añadir** crea una nueva encima de la elegida.
Una capa contiene elementos, uno por cada color con el que pintaste más las formas; el panel
**Elementos** los lista. Todo se explica en [Capas y elementos](topic:layers).

## 4. Dale movimiento (opcional)

Pulsa `T` para abrir la línea de tiempo. **+ Fotograma** añade un fotograma; dibuja en él; pulsa
`Intro` para reproducir. La línea de tiempo se abre sobre el lienzo cuando la quieres y se
esconde cuando no; **Fijar** la mantiene acoplada. Consulta [Animación](topic:animation).

## 5. Guarda

**Archivo > Guardar** (`Ctrl+S`) pregunta dónde poner un archivo `.lsprite`, que lo conserva todo:
capas, fotogramas, paletas, formas y el historial de lo que es cada elemento.

## 6. Sácalo

Para usarlo en un juego o en la web, **Archivo > Exportar PNG** escribe una imagen de 1x a 16x,
**Exportar animación** escribe un GIF, un PNG animado o un WebP, y **Exportar hoja** coloca los
fotogramas en una sola imagen con una descripción que un motor de juegos puede leer. Tras la
primera exportación, **Archivo > Exportar de nuevo** (`Ctrl+Shift+E`) la repite en el mismo archivo
con los ajustes actuales. Consulta [Abrir, guardar y exportar](topic:files).

## Después

- [Dibujar](topic:drawing) para ver todas las herramientas.
- [Color y paletas](topic:colour) para el sistema de ranuras.
- [Paneles, temas y preferencias](topic:appearance) para dejar el programa a tu gusto.
