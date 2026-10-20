# Paneles, temas y preferencias

## Paneles

Todos los paneles se acoplan, se agrupan en pestañas, flotan y cambian de tamaño. Arrastra uno por su
pestaña hasta el lado de otro para acoplarlo al lado, encima de él para compartir su sitio como
pestaña, o fuera, sobre el lienzo, para que flote; arrastra la línea entre dos para cambiar su tamaño.
El menú **Ventana** abre y cierra cada uno. El lienzo conserva el centro y no puede taparse ni moverse.

La disposición se conserva entre sesiones, y el menú **Ventana > Disposiciones** trae tres disposiciones
incorporadas:

- **Dibujo**: opciones de herramienta y paleta a la izquierda; capas, elementos, propiedades y
  transformar a la derecha.
- **Animación**: la línea de tiempo fijada bajo el lienzo y los demás paneles agrupados en pestañas.
- **Compacta**: para una pantalla pequeña; un solo grupo de pestañas a la derecha.

**Guardar la disposición actual...** conserva la disposición con un nombre propio, y elegirlo la devuelve.
**Restablecer disposición** pone el predeterminado.

## Temas

**Editar > Preferencias... > Apariencia** elige cómo se ve el programa. Se incluyen nueve temas:

| Tema | Carácter |
|---|---|
| Slate | gris azulado frío con ámbar, el predeterminado |
| Graphite | gris neutro con acento azul |
| Midnight | negro azulado profundo que hace brillar el lienzo |
| OLED | negro puro, de modo que un píxel que no se enciende está apagado |
| Alto contraste oscuro | negro, blanco y un solo amarillo; todos los pares por encima de 7:1 |
| Paper | la misma disposición vuelta del revés para una sala luminosa |
| Warm paper | un sepia suave, amable en sesiones largas |
| Famicom | plástico crema y rojo intenso |
| Alto contraste claro | blanco, negro y un solo azul |

- **Acento**: nueve ajustes y un selector. El acento se mueve solo lo imprescindible para verse y
  poder llevar texto.
- **Tamaño de la interfaz** (75 % a 150 %) escala el texto y los espacios, nunca el lienzo; la
  **densidad** (Compacta, Cómoda, Amplia) cambia cuánto aire hay.
- **Legibilidad** dice qué pares de colores son difíciles de leer y ofrece **Corregirlos**.
- **Colores** lista cada parte de la interfaz con su propio selector, su valor hexadecimal y un
  restablecer. Las ediciones se ven al instante; **Guardar como tema nuevo** las conserva como un tema
  propio (un archivo de texto `.lstheme` que puedes copiar, editar y enviar). **Importar...** y
  **Exportar...** intercambian archivos de tema.

## Preferencias

**General**:

- *Un documento nuevo*: el tamaño, el fondo y la paleta con los que empieza un documento nuevo.
- *Trabajo*: cada cuánto escribe el autoguardado y la profundidad de deshacer (*pasos de deshacer*).
- *El lienzo*: el color y la opacidad de la cuadrícula de píxeles, y los colores y el tamaño de los
  cuadros del tablero.
- *Idioma*: inglés o español.

**Teclas**: cada atajo es un comando con nombre. Elige una y pulsa las teclas nuevas (hasta cuatro por
comando); se avisa cuando dos comandos comparten una tecla; se restablece una o todas. Los menús muestran
las teclas que tengas. **Ayuda > Atajos de teclado** las lista todas.

Las preferencias y las teclas son archivos de texto sencillo en la carpeta de ajustes
(`preferences.txt`, `keys.txt`) que el programa lee a la defensiva: un archivo editado a mano puede
hacer que Fast se comporte raro, nunca que se rompa.

## Búsqueda de comandos

`Ctrl+Shift+P` (o **Ayuda > Buscar comandos...**) encuentra cualquier comando con unas pocas letras y la
ejecuta con Intro.

## Idiomas

Los menús, los títulos y los nombres de herramientas y comandos pasan por un catálogo de texto sencillo
por idioma; el inglés y el español se incluyen con el programa. El formato del catálogo está documentado
en `docs/translating.md` del proyecto.
