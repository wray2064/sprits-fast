# Dibujar

## Las herramientas

| Herramienta | Tecla | Qué hace |
|---|---|---|
| Lápiz | `B` | Trazos a mano alzada con el pincel actual |
| Aerógrafo | `Shift+B` | Puntos dispersos dentro de un radio |
| Borrador | `E` | Borra; un trazo se corta donde se borró, una forma conserva el hueco |
| Relleno | `G` | Rellena el área conectada en la que haces clic |
| Degradado | `Shift+G` | Rellena el área que encontraría un relleno (o la selección) con un degradado tramado del primer al segundo color, a lo largo de tu arrastre |
| Cuentagotas | `I` | Toma el color que hay bajo el puntero |
| Rectángulo | `R` | Arrastra un rectángulo; sigue siendo editable |
| Elipse | `U` | Arrastra una elipse; sigue siendo editable |
| Línea | `L` | Arrastra una línea; `Shift` ajusta a 15 grados |
| Contorno | `D` | Rellena dentro de un contorno cerrado que dibujas |
| Polígono | `Shift+D` | Haz clic esquina por esquina |
| Curva | `Shift+L` | Haz clic para colocar puntos, arrastra al colocar uno para sacar tiradores |
| Texto | `Shift+T` | Haz clic y escribe |
| Corte | `C` | Dibuja un rectángulo con nombre para las exportaciones |
| Mover | `V` | Mueve la capa o la selección; muestra el control de giro y escala |
| Mano | `H` | Desplaza la vista (o mantén `Espacio`) |
| Zoom | `Z` | Clic para acercar, `Alt`+clic para alejar |

Las herramientas de selección están en [Seleccionar y mover](topic:selecting).

## El pincel

El panel **Herramienta**, a la izquierda, muestra las opciones de la herramienta activa. Para el lápiz,
el aerógrafo y el borrador:

- **Tamaño**, y **Redondo** o **Cuadrado**. `Shift+]` y `Shift+[` agrandan o reducen el pincel.
- **Píxel perfecto** mantiene una diagonal de un píxel de ancho, sin esquinas dobles.
- El **estabilizador** suaviza una mano temblorosa siguiendo al puntero un poco por detrás.
- Un **pincel propio**: selecciona píxeles y pulsa `Ctrl+B`.
- Una **tableta gráfica** funciona sin más: la presión controla el tamaño y el extremo borrador borra.

Mantén `Shift` y haz clic para dibujar una línea recta desde el último trazo. El clic derecho dibuja
con el segundo color; `X` intercambia los dos.

## Tintas

El lápiz y el aerógrafo tienen cuatro **tintas**: lo que un trazo hace con lo que hay debajo.

- **Simple** pinta cada píxel que cubre el pincel.
- **Bloquear alfa** pinta solo donde ya hay algo dibujado.
- **Reemplazar color** pinta solo sobre el segundo color.
- **Sombreado** pasa cada píxel a la ranura vecina de la paleta, de modo que una zona sombreada
  sigue a la paleta.

Cada tinta forma parte del trazo, así que gira y escala con el dibujo.

## Simetría

**Ver > Simetría horizontal** y **Simetría vertical** reflejan lo que dibujas respecto a ejes que
empiezan en el centro del lienzo. Arrastra un eje para moverlo.

## Formas que siguen siendo formas

Un rectángulo, una elipse, una línea, un polígono o una curva conservan sus parámetros. Elígelo en
el panel **Elementos** y sus tiradores aparecen en el lienzo: arrastra una esquina para cambiar el
tamaño, arrastra un punto para remodelarlo. **Propiedades** muestra sus números, el redondeo, el
contorno y la sombra, y los cambia sin redibujar nada. `Shift` mantiene un rectángulo cuadrado y
una elipse redonda.

> Dibujar una forma en una capa no toca lo que había. Pintar sobre un color coloca el color nuevo
> encima, y quitar el elemento superior en el panel Elementos devuelve el anterior tal como estaba.

## Texto

Elige la herramienta Texto, haz clic donde deba ir y escribe. El resultado es un elemento hecho de
palabras, un lugar y un tamaño: selecciónalo en el panel Elementos y edita el texto en
**Propiedades**, y los píxeles se reconstruyen. La tipografía es la propia de Fast, de cinco por siete píxeles.

## Contornos, sombras y tramado

En **Propiedades**, **Contorno** dibuja una línea alrededor de lo que dibuja una capa, o alrededor de
toda la figura a través de las capas, sin costura donde se unen las piezas; **Sombra** proyecta una
sombra desde la capa o desde toda la figura. Ambos se generan a partir del dibujo tal como está
ahora, así que siguen cualquier cambio.

El **relleno tramado** convierte el color de una capa en un tramado entre dos colores: densidad
constante, o un degradado lineal, radial o angular, con doce patrones. El **ancla** decide qué hace
el patrón cuando la obra gira: Local gira con ella, Global se mantiene nivelado con el lienzo y Fijo
lo clava al lienzo mientras la obra se mueve.

## Después

[Color y paletas](topic:colour), [Capas y elementos](topic:layers).
