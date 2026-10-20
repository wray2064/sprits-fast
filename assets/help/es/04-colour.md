# Color y paletas

## Ranuras de la paleta

Todo documento tiene una paleta, y los colores que contiene son **ranuras** y no valores. Un píxel
pintado a través de una ranura la recuerda. Cambia la ranura y cambia todo lo pintado a través de
ella, en todos los fotogramas, redibujado a partir del trabajo, no repintado encima. Cambiar los
colores de un personaje por una versión nocturna o por el color de un segundo equipo es una sola edición.

## El panel Paleta

- **Clic** en una muestra para pintar con ella. **Clic derecho** para ponerla como segundo color.
- **Doble clic** en una muestra para cambiar lo que significa la ranura, ponerle nombre o quitarla
  (antes avisa si algo pinta a través de ella).
- Arrastra una muestra sobre otra para moverla; **Ordenar** ordena por tono, saturación o luminosidad.
  Ninguna ranura cambia de número, así que nada pintado a través de una lo nota.
- **Añadir el color actual** crea una ranura nueva con el color con el que pintas.
- **Hacer de cada color una ranura** convierte en ranura cada color pintado como valor propio, en
  todos los fotogramas.
- **Rampa** rellena ranuras a intervalos regulares entre un color izquierdo y uno derecho.
- **Ajustar** gira el tono y empuja la saturación y la luminosidad de todas las ranuras a la vez, y
  puede devolverse exactamente a donde estaba.
- Los **modelos** son paletas generadas para Fast: grises, un bit, rampas de tono sombreadas hacia el
  azul y el amarillo. Las paletas famosas se cargan desde sus propios archivos.

Bajo las muestras, **Tonos** ofrece la rampa del color actual, con el tono desplazado como sombrean
los artistas de píxeles (las sombras hacia el azul, las luces hacia el amarillo). Haz clic en uno
para pintar con él, o pulsa **Guardar estos tonos como ranuras**. **Recientes** lista los colores con
los que pintaste que no son ranuras, el más nuevo primero.

## Varias paletas y el intercambio

Un documento puede tener las paletas que quieras. El desplegable de la parte superior del panel dice
cuál usa cada fotograma, y `<>` o `Ctrl+P` pasa a la siguiente: toda la animación se recolorea de
una vez. En **Paletas** (cerrado hasta que lo necesites) cada paleta puede renombrarse, copiarse y
borrarse, y un fotograma concreto puede tener una paleta propia. Una paleta por fotograma a lo largo
de la línea de tiempo es el ciclo de color.

## Cargar y guardar paletas

**Cargar** y **Guardar** usan estos formatos:

| Formato | De dónde viene |
|---|---|
| `.gpl` | GIMP, y lo que Aseprite lee y escribe; conserva los nombres de las ranuras |
| `.hex` | un color por línea; lo que sirve Lospec |
| `.pal` | JASC |
| `.act` | tabla de colores de Photoshop |

Cargar también lee una paleta de cualquier imagen. Cargar reemplaza la paleta que usa el fotograma
actual y recolorea un sprite dibujado a través de ranuras, que es para lo que sirven. Una línea
suelta en un archivo de paleta se omite en lugar de hacer fallar el archivo.

## El color con el que pintas

El panel **Herramienta** muestra los dos colores (botón izquierdo y derecho) con un selector. `X` los
intercambia. Un color que no es una ranura es un valor simple; se queda exactamente como se eligió.

## Abrir una imagen

Cuando abres un PNG, GIF, JPEG o BMP de 256 colores o menos (pixel art), esos colores **se convierten
en la paleta**, en el orden en que aparecen por primera vez, y cada píxel pinta a través de su ranura.
Recolorear un sprite que viene de otro sitio es entonces una edición de una muestra. Una imagen con
miles de colores es una fotografía y se rechaza, con la sugerencia de importarla como
[referencia](topic:references).

## Ajustar colores

**Sprite > Ajustar colores** cambia tono, saturación, luminosidad, brillo y contraste, o invierte,
para una capa, un fotograma o todos los fotogramas. Cambia los colores propios de los elementos y, si
lo pides, las ranuras de paleta a través de las que pintan. Nada se convierte en píxeles, y cancelar
devuelve exactamente lo que había.
