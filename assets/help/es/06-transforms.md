# Girar, escalar y el lienzo

## Las transformaciones son números de una capa

Elige una capa y abre el panel **Transformar**. **Rotar**, **Escalar**, **Reflejar** y **Mover**
añaden una entrada a la lista de operaciones de la capa: un ángulo, un factor, un número de píxeles
enteros. La imagen no se gira; el dibujo se recorre de nuevo con el ángulo nuevo cada vez que se
muestra. Así, cinco giros y un regreso a cero devuelven el original, byte por byte, y borrar la
entrada también lo devuelve.

En el lienzo, la herramienta **Mover** (`V`) muestra un control en el pivote de una capa que tiene un
Giro o una Escala:

- Arrastra el **anillo** para girarla. Mantén `Shift` para pasos de 15 grados: 90 grados es una parada,
  y un cuarto de vuelta pone cada píxel exactamente donde lo envía un cuarto de vuelta.
- Arrastra la **flecha roja** para estirar a lo ancho y la **flecha verde** para estirar a lo alto.
  `Shift` para pasos de cuarto.
- Arrastra el **cuadrado** entre ellas para mover la capa en píxeles enteros. `Shift` la mantiene en un eje.
- `Esc` en mitad de un arrastre lo deshace.

El panel Transformar admite los mismos números escritos a mano.

## Girar al estilo pixel art

Una capa girada no es una imagen girada. Sus trazos se recorren de nuevo con el ángulo nuevo, de un
píxel de ancho y unidos; sus rellenos se vuelven a encontrar dentro de sus contornos, sin huecos ni
fugas; un pincel conserva su tamaño; un tramado se mantiene nivelado y nítido mientras su degradado
gira con la forma. No aparece ningún color que el dibujo no tuviera. (Los píxeles que vienen de fuera,
una imagen importada antes de dibujar sobre ella, se giran con el algoritmo RotSprite.)

> Una nota honesta: una diagonal de un solo píxel se rompe en guiones en ángulos arbitrarios. El
> cuerpo sólido gira con nitidez; los detalles finos no. Eso es lo que significa dibujar sin
> suavizado, y es el compromiso correcto para el pixel art, pero míralo antes de fiarte.

## Interpolaciones

Da a una capa las mismas transformaciones (giro, escala, desplazamiento) en dos fotogramas, selecciona
con `Shift`+clic el tramo entre ellos en la línea de tiempo y pulsa **Interpolar**. Cada fotograma
intermedio recibe los valores intermedios, lineales o suavizados al entrar y salir. Cada intermedio
sigue siendo una transformación viva del dibujo, así que un fotograma puede retocarse a mano después y
nada se remuestrea dos veces.

## El lienzo

Menú **Sprite**, cada uno un paso de deshacer, en todos los fotogramas a la vez:

- **Tamaño del lienzo...** agranda o reduce el lienzo respecto a cualquiera de nueve anclas.
- **Recortar a la selección** y **Ajustar al contenido** (al menor rectángulo en el que hay algo dibujado).
- **Tamaño del sprite...**, **Ampliar** y **Reducir**: cambian a cualquier tamaño o porcentaje por
  vecino más cercano, escalando las formas como formas.
- **Girar el lienzo** 90 a la derecha, 90 a la izquierda o 180, y **Voltear el lienzo** horizontal o verticalmente. Las
  guías, los cortes y las marionetas giran con él.

El lienzo más grande tiene 16.384 píxeles por lado y 16 millones de píxeles en total (4096 x 4096).
