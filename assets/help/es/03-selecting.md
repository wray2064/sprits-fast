# Seleccionar y mover

## Hacer una selección

| Herramienta | Tecla | |
|---|---|---|
| Marco rectangular | `M` | Arrastra un rectángulo |
| Marco elíptico | `Shift+M` | Arrastra una elipse |
| Lazo | `Q` | Dibuja alrededor de lo que quieres |
| Lazo poligonal | `Shift+Q` | Haz clic en las esquinas |
| Varita mágica | `W` | Clic para seleccionar el área conectada de un color |

Mantén `Shift` para añadir a la selección, `Alt` para restar y ambas para quedarte solo con lo que
se solapa. La selección se dibuja con hormigas marchantes.

Menú **Selección**: **Todo** (`Ctrl+A`), **Deseleccionar** (`Ctrl+D`), **Volver a seleccionar**, **Invertir**
(`Ctrl+Shift+I`) y **Modificar**, que agranda, reduce o bordea la selección en cualquier número de
píxeles, con esquinas cuadradas o redondas.

Mientras hay una selección, el lápiz, el borrador y el relleno se quedan dentro de ella.

## Mover y cambiar lo seleccionado

- Arrastra los píxeles seleccionados con la herramienta Mover (`V`), o empújalos con las flechas.
- **Voltear** horizontalmente (`Shift+H`) o verticalmente (`Shift+V`).
- **Editar > Girar libremente** pone los píxeles seleccionados en una capa propia y muestra un anillo
  para girarlos el ángulo que quieras, las veces que quieras; **Editar > Escalar libremente** muestra
  ocho tiradores para cambiar su tamaño. Ninguno remuestrea el dibujo: cualquier tamaño o ángulo
  vuelve exactamente al original.
- **Cortar**, **Copiar** y **Pegar** (`Ctrl+X`, `Ctrl+C`, `Ctrl+V`). **Pegar como capa nueva** es
  `Ctrl+Shift+V`. **Eliminar** quita la parte seleccionada y conserva el hueco como parte de aquello
  de lo que se cortó.
- **Editar > Rellenar la selección** la pinta con el color actual; **Contornear la selección** pinta una banda
  del ancho del pincel justo dentro de su borde. Ambos son pintura normal, un paso de deshacer cada uno.
- **Pincel desde la selección** (`Ctrl+B`) convierte los píxeles seleccionados en tu pincel.

## El portapapeles funciona con otros programas

Las copias también van al portapapeles del sistema como imagen, así que se pegan en un navegador, un
chat u otro editor. Una imagen copiada desde cualquier otro sitio se pega en Fast como una pieza
flotante, un elemento por color; un color que la paleta ya tiene entra como esa ranura, de modo que
se recolorea con la paleta enseguida. Pegar también puede convertir la imagen en una
[referencia](topic:references).

## Ajuste

**Ajustar a la cuadrícula** (`Shift+S`) hace que las selecciones cubran mosaicos enteros, que las
formas pongan sus esquinas en las líneas de mosaico y que una selección movida avance de mosaico en
mosaico. Fija el tamaño del mosaico en **Ver > Cuadrícula de mosaicos**. Consulta
[Mosaicos, cuadrículas y cortes](topic:tiles).
