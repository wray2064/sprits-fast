# Animación

## La línea de tiempo

Un fotograma es un sprite, y los fotogramas del documento en orden son la animación. Pulsa `T` para
abrir la **línea de tiempo**. Tiene dos formas:

- **Un cajón** (por defecto): una pestaña pequeña en el borde inferior del lienzo dice en qué
  fotograma estás. Apoya el puntero en ella, haz clic o pulsa `T`, y la tira se abre sobre el lienzo;
  aparta el puntero y se esconde otra vez (un clic o `T` la mantiene abierta hasta que pulses `T`,
  `Esc` u **Ocultar**). El lienzo conserva toda la ventana y nada cambia de tamaño cuando la tira va y viene.
- **Fijada**: el botón **Fijar** (o **Ventana > Fijar la línea de tiempo**) la acopla bajo el lienzo
  como un panel normal, que es lo que quiere animar todo el día. **Soltar** la deja volver. La disposición
  **Animación** (Ventana > Disposiciones) la fija por ti.

## Fotogramas

- **+ Fotograma** añade un fotograma después del actual, copiando su dibujo y compartiendo su paleta;
  **+ Enlazado** añade uno que comparte sus celdas con el original (un trazo en cualquiera cae en ambos);
  **+ Vacío** añade uno en blanco.
- Haz clic en la miniatura de un fotograma para editarlo. `,` y `.` retroceden y avanzan.
- **Arrastra** un fotograma para reordenarlo. **Eliminar** lo quita. `Ctrl+Shift+D` duplica el fotograma actual.
- **Duración** fija cuánto tiempo permanece el fotograma en pantalla, en milisegundos; `Shift`+clic
  selecciona un tramo de fotogramas, y los botones actúan entonces sobre todo el tramo: duplicar,
  eliminar, **Invertir**, o darles una misma duración.
- Haz clic derecho en un fotograma para su menú: añadir un fotograma vacío después, duplicarlo,
  moverlo a la izquierda o a la derecha, invertir el tramo seleccionado, eliminarlo.

Las capas recorren toda la animación: añade, elimina, mueve, renombra, oculta o agrupa una capa en
cualquier fotograma y queda así en todos, conservando cada uno sus propios píxeles de ella. Consulta
[Capas y elementos](topic:layers).

## Celdas enlazadas

Una **celda enlazada** hace que la celda de una capa en varios fotogramas sea una sola: un trazo en
cualquiera de ellos cae en todos, mientras se dibuja. **+ Enlazado** duplica un fotograma de esa
manera, y el menú de la fila de una capa enlaza o desenlaza una sola capa.

## Reproducción

`Intro` reproduce y detiene. **Velocidad** previsualiza a media velocidad para juzgar el tiempo, o al
doble para ver el movimiento. La reproducción es una función del tiempo transcurrido, así que perder
un fotograma no cuesta nada y desplazarse por la línea de tiempo cae exactamente en lo que mostró la
reproducción. Mientras reproduce, el lienzo muestra el fotograma que dice el reloj; detener es lo que
cambia sobre qué fotograma actúan las herramientas.

## Papel cebolla

`O` activa el **papel cebolla**: los fotogramas de uno y otro lado se dibujan tenues bajo el actual,
con sus propios tintes. Sus ajustes (bajo la **v** a su lado) eligen cuántos fotogramas antes y
después, y si los vecinos vienen de la lista de fotogramas o del ciclo seleccionado, de modo que sean
los que se reproducirán a cada lado de este. La **vista previa** de la esquina (`P`) muestra el sprite
de 1x a 4x sobre ocho fondos distintos.

## Ciclos

Un **ciclo** es un tramo de fotogramas con nombre, "caminar" o "herido", con su propio modo de bucle,
guardado con el archivo. Es una *secuencia*, no un subconjunto: un paso nombra un fotograma, y dos
pasos pueden nombrar el mismo, de modo que un paseo de cuatro fotogramas dibujado una vez reproduce
`0 1 2 1`.

1. Pulsa **Nuevo** junto a *Ciclo* y ponle nombre.
2. Añade pasos: elige un fotograma y pulsa el botón de añadir, en el orden en que deben reproducirse.
3. Elige **cuándo termina**: bucle, una vez o ida y vuelta (de ida y vuelta sin repetir ninguno de los extremos).

La reproducción, el papel cebolla y las exportaciones usan el ciclo seleccionado, o todos los
fotogramas cuando no hay ninguno. Un fotograma lleva un punto por cada vez que un ciclo lo reproduce, y
un fotograma que ningún paso nombra aparece atenuado, no marcado.

## Interpolaciones

Consulta [Girar, escalar y el lienzo](topic:transforms): fija una transformación en dos fotogramas,
selecciona el tramo y pulsa **Interpolar**.

## Paletas en el tiempo

Un fotograma puede tener su propia paleta, de modo que un fotograma de destello, o una paleta por
fotograma a lo largo de la línea de tiempo, es ciclo de color sin esfuerzo. Consulta
[Color y paletas](topic:colour).

## Sacarlo

**Archivo > Exportar animación...** escribe el ciclo seleccionado (o todos los fotogramas) como GIF
animado, PNG animado, WebP animado o una secuencia numerada de PNG; **Exportar hoja...** coloca los
fotogramas en una sola imagen. Consulta [Abrir, guardar y exportar](topic:files).
