# Capas y elementos

## Capas

El panel **Capas** lista las capas de la imagen, la superior primero, cada una con una miniatura de lo
que dibuja esa capa por sí sola. Haz clic derecho en una fila de capa para ver todo lo que sigue.

- **Añadir** crea una capa; **Dup.** (`Ctrl+J`) la copia; **Eliminar** la quita.
- **Reordenar**: arrastra una fila, o usa `Ctrl+]` y `Ctrl+[`.
- **Visibilidad**: el ojo a la izquierda de la fila. Una capa oculta se ve tenue en su miniatura.
- **Opacidad** y **modo de fusión** (once, de Multiplicar a Diferencia) están sobre la lista y
  funcionan en una capa o en todo un grupo.
- **Bloquear** mantiene las herramientas fuera de una capa.
- **Recortar a la capa inferior** dibuja una capa solo donde dibuja la capa de debajo: un brillo que se
  queda dentro del cuerpo en el que está.
- **Combinar hacia abajo** (`Ctrl+E`) pasa los elementos de la capa superior a la de debajo, cada uno
  tal como era, con la opacidad y la fusión incorporadas. Donde eso cambiaría la imagen (una
  transformación, un contorno, una capa oculta, dos modos de fusión) se rechaza y se dice el motivo.
- **Propiedades de la capa** (en el menú de la fila) da a una capa una etiqueta de color que raya su
  fila, y notas que se guardan con ella.
- **Capa de referencia** muestra una capa en el lienzo pero la deja fuera de toda exportación y del
  contorno alrededor de toda la figura: un boceto sobre el que dibujar.

## Grupos

Selecciona varias capas con `Ctrl`+clic y pulsa `Ctrl+G` para agruparlas. Un grupo compone sus capas
en una imagen y *después* la fusiona, que no es lo mismo que fusionar cada una, y es para lo que sirve
un grupo. Suelta una capa sobre la fila de un grupo para meterla; `Ctrl`+soltar sobre una capa agrupa
las dos; `Ctrl+Shift+G` disuelve un grupo en su sitio.

## Elementos

Una capa contiene varios **elementos**: uno por cada color con el que están pintados sus píxeles, más
las formas, textos, rellenos y degradados dibujados en ella. El panel **Elementos** los lista, el
superior primero. Elige uno para cambiarlo en **Propiedades**: qué es, sus controles y después la
sombra y el contorno de la capa (cada uno se pliega).

Una capa es una **pila**: quitar el elemento superior devuelve lo que había debajo, exactamente. Pintar
sobre un color no recorta el anterior; borrar se registra como lo que se borró de qué elemento, así
que nunca toca nada dibujado después.

## Capas a lo largo de los fotogramas

En una animación, una capa recorre todos los fotogramas: añade, elimina, mueve, renombra, oculta,
agrupa o etiqueta una en cualquier fotograma y queda así en todos, conservando cada fotograma sus
propios píxeles de ella. Consulta [Animación](topic:animation). Los archivos antiguos cuyos
fotogramas tenían capas separadas las conservan hasta que **Sprite > Mismas capas en todos los
fotogramas** las une por nombre, sin perder nada.

## Deshacer

Cada operación es un paso de deshacer, y **Editar > Historial...** los lista: haz clic en uno para
volver a él. La profundidad de deshacer se fija en las Preferencias.
