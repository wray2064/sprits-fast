# Mosaicos, cuadrículas y cortes

## Modo mosaico

**Ver > Modo mosaico** dibuja el lienzo de nuevo a su lado, a lo ancho, a lo alto o en ambos sentidos,
para que se vean las costuras de un mosaico repetido mientras lo dibujas. Un trazo que sale por un
borde vuelve a entrar por el borde opuesto.

## La cuadrícula de mosaicos y el ajuste

**Ver > Cuadrícula de mosaicos** coloca una cuadrícula de cualquier tamaño y desplazamiento sobre la
cuadrícula de píxeles. **Isométrica** la convierte en una cuadrícula de rombos 2:1. **Ajustar a la
cuadrícula** (`Shift+S`) hace entonces que las selecciones cubran mosaicos enteros, que las formas
pongan sus esquinas en las líneas de mosaico y que una selección movida avance de mosaico en mosaico.

**Ver > Cuadrícula de píxeles** muestra la propia cuadrícula de píxeles; su color y su opacidad están en
las Preferencias. Las **reglas** recorren el lienzo, y las **guías** arrastradas desde ellas se quedan
con el documento, giran con el lienzo y también atraen el ajuste (**Ver > Quitar las guías** las quita).

## Capas de mosaicos

**Sprite > Nueva capa de mosaicos...** añade una cuadrícula de mosaicos sobre el lienzo. Cada mosaico
se dibuja una vez y se muestra dondequiera que se coloque.

- **Dibuja píxeles en una celda** y el mosaico que hay debajo cambia en todos los sitios donde se usa.
  Una celda vacía en la que dibujas recibe un mosaico propio.
- O cambia a **colocar mosaicos**: pon el mosaico elegido, volteado o girado, borra celdas, rellena con
  el cubo o toma un mosaico con el cuentagotas.
- Cada fotograma tiene sus propias celdas; los mosaicos se comparten.

Los archivos de Aseprite con capas de mosaicos se abren como capas de mosaicos, con sus volteos y todo.

## Cortes

La herramienta **Corte** (`C`) dibuja un rectángulo con nombre: un botón, una caja de impacto, un
panel que se estira por el centro. Dale a un corte un **centro de nueve cortes** y un **pivote**
opcionales. Los cortes se mueven con el lienzo cuando se recorta, se gira o se redimensiona, y salen
con cada descripción de hoja (incluido `meta.slices` de Aseprite). **Ver > Cortes...** los lista y
los edita.
