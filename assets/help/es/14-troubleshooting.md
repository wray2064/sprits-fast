# Solución de problemas y límites

## El programa se cerró y perdí trabajo

Arráncalo otra vez. Si la última sesión terminó sin guardar, pregunta **¿Recuperar tu trabajo?** sobre
el trabajo que perdiste en último lugar, con una imagen de él, su nombre (o *Sin título*), cuándo se
escribió por última vez, su tamaño y dónde se estaba guardando. **Recuperar** lo abre como documento sin
guardar: usa Guardar para decir dónde va. **Más tarde** lo deja para la próxima; **Descartar** pregunta
dos veces, porque no se puede deshacer. Las copias se escriben mientras tienes cambios sin guardar (cada
un par de minutos por defecto) y nunca se escriben sobre tu archivo. Las copias que dejaron cierres
anteriores no se vuelven a ofrecer: cuando atiendes la última, se mueven a una carpeta `older` junto a
las copias de recuperación (se conservan las diez más recientes). Consulta
[Abrir, guardar y exportar](topic:files).

## ¿Dónde están mis ajustes?

En una carpeta llamada `SpritsFast` de tu perfil de usuario: preferencias, atajos, tus temas (`themes`),
los diseños guardados (`layouts`), la lista de archivos recientes y las copias de recuperación. La
versión de la Microsoft Store guarda la misma carpeta dentro del almacenamiento propio de la aplicación
y la elimina al desinstalar. Borrar la carpeta te deja un programa como recién instalado.
**Ventana > Restablecer disposición** devuelve solo la disposición de los paneles.

## Todo es demasiado pequeño, demasiado grande o está cortado

**Editar > Preferencias... > Apariencia > Tamaño de la interfaz** escala el texto y los espacios entre
75 % y 150 %, y la **densidad** cambia el aire. La escala de pantalla de Windows se sigue sola. Si un
panel es demasiado estrecho para lo que contiene, arrastra la línea de su lado para ensancharlo, o usa
la disposición **Compacta** (**Ventana > Disposiciones**).

## Una tecla no hace lo que dice

- Otro comando puede compartir la tecla: **Editar > Preferencias... > Teclas** avisa cuando dos lo hacen.
- Una tecla escrita en un cuadro de texto va al cuadro; haz clic primero en el lienzo.
- `Espacio` es la mano, no un comando propio: mantenlo y arrastra.

## No me deja dibujar

- La capa puede estar **bloqueada** (panel Capas), oculta, o ser una **capa de referencia**.
- Mientras una animación se **reproduce**, dibujar se rechaza para que un trazo no caiga en un fotograma
  que no estás mirando. Pulsa `Intro` para detenerla.
- Una selección mantiene el lápiz, el borrador y el relleno **dentro** de ella; **Deseleccionar**
  (`Ctrl+D`) lo levanta.

## Límites

| | |
|---|---|
| Lienzo | 16.384 píxeles por lado, 16 millones de píxeles en total |
| Piezas en un documento de marioneta | 256 |
| Una referencia | 8 MB cada una, 16 MB en un documento |
| Una imagen abierta como obra | una fotografía con miles de colores se rechaza y se sugiere como referencia |
| Un GIF | 256 colores por fotograma, sin transparencia parcial |

## Archivos de Aseprite

Pasa todo lo que el formato describe. Lo que no pasa se dice, no se descarta: un conjunto de mosaicos
guardado en otro archivo, y los pocos modos de fusión que Fast no tiene, que entran como el más cercano
que sí tiene.

## El idioma es incorrecto

**Editar > Preferencias... > General > Idioma**.

## Informar de un problema

**Ayuda > Acerca de** tiene la versión y un enlace para informar de uno. Di qué hiciste, qué esperabas y
qué ocurrió; adjunta el `.lsprite` si se trata de un sprite. Nunca se envía nada automáticamente: el
programa nunca se conecta a internet.
