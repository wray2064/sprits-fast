# Bienvenida

Sprit's'fast es un editor de sprites y pixel art con una promesa poco habitual: **nada de lo
que dibujas se convierte en píxeles hasta que se muestra.** Un trazo de lápiz se guarda como el
camino que siguió el lápiz y el pincel con el que lo siguió. Un relleno se guarda como el área
que encontró. Un rectángulo es un rectángulo. Los píxeles solo existen en pantalla y en los
archivos que exportas.

Por eso puedes girar una capa 23 grados, escalarla, cambiar un color de la paleta o cambiar de
opinión diez pasos atrás, y la imagen simplemente se dibuja de nuevo a partir de lo que hiciste.
Nada se remuestrea, así que nada se suaviza.

> Esta guía está dentro del programa, funciona sin conexión y describe esta versión. Pulsa
> **F1** en cualquier momento para abrirla y usa el cuadro de búsqueda de la izquierda para
> encontrar una palabra.

## Por dónde empezar

- ¿Es tu primera vez? Lee [Primeros pasos](topic:getting-started): cinco minutos desde un lienzo
  vacío hasta un sprite guardado y exportado.
- ¿Vienes de otro editor de píxeles? Lee [Dibujar](topic:drawing) y luego
  [Color y paletas](topic:colour): la paleta funciona de otra manera, y es lo mejor.
- ¿Haces una animación? [Animación](topic:animation) y después
  [Marionetas de recortes](topic:puppets) para personajes hechos de piezas.
- ¿Quieres saber qué hace cada tecla? [Atajos de teclado](topic:shortcuts), o
  **Ayuda > Atajos de teclado**, que además te deja cambiarlos.
- ¿Algo no funciona? [Solución de problemas y límites](topic:troubleshooting).

## Qué sigue siendo editable

Como el trabajo se guarda como lo que hiciste, queda editable mucho de lo que normalmente se
pierde al soltar el ratón:

- **Formas**: un rectángulo dibujado hace una hora conserva su tamaño, sus esquinas y sus tiradores.
- **Texto**: un texto son palabras, un lugar y un tamaño; una errata se reescribe, no se borra.
- **Colores**: un color pintado a través de una ranura de la paleta sigue a la ranura. Cambia la
  ranura y cambia todo lo que la usaba, en todos los fotogramas.
- **Transformaciones**: giro, escala y desplazamiento son números de la capa. Ponlos a cero y el
  original vuelve exactamente.
- **Contornos y sombras**: se generan a partir de lo dibujado, así que lo siguen.

## Lo que conserva por ti

Tu trabajo se escribe en archivos `.lsprite`. Mientras trabajas, el programa también guarda copias
de recuperación, de modo que un cierre inesperado o un apagón no te cuesta nada: consulta
[Abrir, guardar y exportar](topic:files).

Sprit's'fast es gratuito y de código abierto (licencia Apache 2.0). No tiene cuentas, no recoge
nada y nunca se conecta a internet.
