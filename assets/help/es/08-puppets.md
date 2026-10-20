# Marionetas de recortes

Animación de recortes tal como la hace una mano: un personaje construido con piezas, cada una girando
sobre su propia articulación, colocado a mano y capturado en un fotograma.

## Haz las piezas

1. Dibuja cada miembro o pieza en una **capa propia**.
2. En el panel **Marioneta**, convierte cada capa en una **pieza**. Sale del fotograma y pasa a ser
   una pieza independiente, dibujada exactamente donde estaba.
3. Cuelga cada pieza de aquella con la que se mueve: un brazo del torso, un antebrazo del brazo. Cuelga
   de donde está dibujada, así que el personaje no cambia, píxel por píxel.

## Dibujar pieza

En **Dibujar pieza** el lienzo muestra una pieza, con todas las herramientas. Su **articulación**
(donde gira) y sus **enchufes** (donde cuelgan otras piezas) son marcas que colocas con un clic o
arrastrando. Una articulación se pone en el centro de un píxel, de modo que un cuarto de vuelta lleva
cada píxel a un píxel y un miembro colocado a 90 grados es el miembro girado, exactamente.

## Pose

En **Posar** el lienzo muestra la marioneta entera.

- Haz clic en una pieza para elegirla. El **anillo** alrededor de su articulación la gira; `Shift` da
  pasos de 15 grados, así que un miembro se detiene en 90.
- El **cuadrado** de la raíz mueve toda la marioneta en píxeles enteros.
- **Capturar pose** crea un fotograma con ella después del que se muestra, y la siguiente captura va
  después de esa: pose, captura, pose, captura.

Un fotograma capturado son las formas propias de las piezas colocadas donde la pose las puso, no una
imagen de ellas, y después es un fotograma como cualquier otro, para retocar a mano. Cada pieza es una
fila de la línea de tiempo, la misma en cada captura.

## Conviene saber

- Girar o voltear el lienzo gira la marioneta y todas sus capturas con él.
- Una marioneta se construye con los pivotes, enchufes y anclajes propios del motor, y la pose desde la
  que se capturó cada fotograma queda escrita en ella. Un programa que anima marionetas por sí mismo
  (Sprit's'pract) abre una marioneta de Fast tal como es.
- Un documento puede tener hasta 256 piezas.

Consulta también [Animación](topic:animation) para saber qué hacer con los fotogramas que capturas.
