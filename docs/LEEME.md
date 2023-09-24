# ADP: ADventure Player

<p align="center">
    <img src="/docs/Logo.png" width="414.5" height="95.5" />
</p>
<p align="center">
https://github.com/jlcebrian/ADP
</p>

El **DAAD** es un sistema de autoría de juegos diseñado para escribir
aventuras de texto portables a una amplia variedad de ordenadores
de los años 80 y 90. Fue desarrollado por Aventuras AD y escrito
por Tim Gilberts.

Este intérprete permite ejecutar juegos escritos en **DAAD** en
ordenadores modernos, contando con versiones para Windows, Mac
y Linux. Se ha buscado un elevado grado de compatibilidad, con
especial empeño en ejecutar fielmente las aventuras comerciales
de **Aventuras AD**. En este sentido, se soportan todos los condactos
originales, incluyendo GFX y SFX, y características tales como
sonido, animación, doble búfer, o cambios de paleta de color.

Notablemente, el intérprete cuenta también con versiones para
Amiga, Atari ST y MS-DOS, por lo que puede sustituir directamente
a los intérpretes originales para máquinas de 16 bits.

El intérprete puede ejecutarse también en un entorno web, gracias
a la herramienta Emscripten.

### ¿Por qué otro intérprete?

El objetivo inicial de ADP era poder obtener versiones PC/VGA de
todas las aventuras de AD. Originalmente, solo Chichen Itzá contaba 
con una versión para MS-DOS comparable a las de Amiga o Atari. El
resto de juegos contaba, en el mejor de los casos, con versión EGA
(y a veces, ¡sólo CGA!). La versión MS-DOS del intérprete cumple
esta función, soportando incluso las características que el
intérprete original VGA, usado en Chichen, no incluía, como sonido
digitalizado (si se dispone de Sound Blaster en el equipo).

Un segundo objetivo es ofrecer una forma de ejecutar aventuras
**DAAD** en ordenadores modernos, de forma que un juego original pueda ser
distribuido en forma de ejecutable, sin necesidad de emuladores.

Por último, un tercer objetivo es disponer de un intérprete de 
código fuente libre del DAAD, que cuente con versiones nativas en
ordenadores 'retro' y pueda utilizarse para sustituir  los 
intérpretes originales en ordenadores de 16 bits o superiores.


## Forma de uso

El intérprete se distribuye en dos formas: como utilidad de línea
de comandos para ordenadores modernos (DDB), y como ejecutable
diseñado para ser incluido junto con los juegos (PLAYER).

La utilidad de línea de comandos requiere como parámetro el nombre
de un fichero de base de datos .DDB, y ejecutará el juego en
nuestro escritorio:

	C:\> DDB PART1.DDB

Además, esta utilidad cuenta con funcionalidad adicional para
decompilar el contenido del fichero DDB, mostrar información
sobre el mismo, y algunas otras. Ejecutar la utilidad sin ningún
parámetro ofrecerá una breve ayuda con las opciones disponibles.

El ejecutable PLAYER está diseñado para acompañar a un juego.
Cuando se ejecuta, examina el directorio donde se encuentra y
muestra la primera pantalla de presentación (.SCR) que vea.
Si hay 2 o más ficheros .DDB en el directorio, mostrará un
mensaje permitiendo al usuario escoger la parte a cargar.

Para ejecutar a una de las versiones de las aventuras de AD,
basta pues con copiar el ejecutable PLAYER a un directorio
donde se haya extraido una de las mismas y ejecutarlo allí.

En ocasiones, obtener los ficheros de un aventura no es tarea
sencilla, pues pueden estar dentro de una imagen de disco tipo
.ADF, .ST o .IMG y es preciso usar alguna herramienta externa.
En general, una aventura completa necesita los siguientes ficheros,
todos ellos con el mismo nombre:

```
	*.DDB       Fichero principal de la aventura
	*.DAT       Fichero con los gráficos de la aventura.
				Según la versión, puede ser también .EGA o .CGA.
	*.CHR       Fichero con el juego de caracteres de la aventura.
				Para ST, habrá también (o en su lugar) un .CH0.
	*.SCR       Pantalla de presentación (normalmente,
				solo de la primera parte de la aventura).
```

## Notas sobre las distintas versiones

### MS-DOS

Esta versión se ejecuta en modo protegido y requiere un
ordenador 386 o superior con, al menos, 2MB de RAM, y
una tarjeta gráfica VGA. 

Cuando se usa para ejecutar un juego con múltiples versiones 
de gráficos incluidas, se puede ejecutar como PLAYER CGA o 
PLAYER EGA para forzar la versión del juego que se muestra. 
Por defecto se mostrará la versión VGA.

Incluso ejecutando una versión CGA o EGA de un juego,
por ahora el intérprete sigue requiriendo tarjeta VGA,
simplemente mostrará gráficos con menos colores.

### AMIGA / ATARI ST

Ambos intérpretes funciona sobre modelos base con 512K de RAM,
pero en caso de contar con RAM adicional, la usarán durante
el proceso de carga como caché para los gráficos del juego
(a costa de hacer la carga inicial más lenta). Al igual que 
los intérpretes originales, están diseñados para ejecutarse
directamente sobre disquette.

El arranque inicial es más lento que en los intérpetes
originales, porque examinan el contenido del disquette para
reconocer el número de partes del juego. Originalmente,
los intérpretes venían pre-compilados para un número de
partes exacto y no necesitaban esta operación, que resulta
especialmente lenta en Amiga.

## Compatibilidad

El intérprete soporta las versiones 1 y 2 del DAAD, en
cualquiera de sus versiones de 16 bits (PC, Atari o Amiga).
Esto se aplica a todas las versiones del intérprete, por lo que
es posible jugar a la versión de PC/CGA en Atari, a la versión
de Amiga en PC, o cualquier otra combinación que queramos.

El soporte de la versión 1 en particular es, y seguirá siendo,
incompleto. No se ha conservado esta versión del DAAD y el
actual soporte se ha obtenido mediante ingenería inversa de
las versiones Atari ST de la Aventura Original y Jabato.

Esencialmente, el intérprete debería poder ejecutar cualquier
aventura de 16 bits de Aventuras AD tal como fue distribuido
originalmente, y también cualquier .DDB de 16 bits generado
con las herramientas disponibles actualmente.

Hay un punto de incompatibilidad entre versiones, y es que los
ficheros .SCR de Amiga y ST son incompatibles entre sí, sin
una forma clara de distinguir entre ellos. Si el fichero .DDB
está marcado con la versión correcta (Amiga o Atari), el
intérprete podría mostrarla bien, pero en ocasiones la versión
de Amiga se distribuye con el mismo .DDB que la de Atari, lo
puede provocar que la pantalla de presentación salga corrupta.

## Cambios respecto al DAAD original

El intérprete no pretende extender el DAAD original con nueva
funcionalidad. Sin embargo, sí se han hecho algunas mejoras con
intención de añadir algunas conveniencias al usuario.


### EDICIÓN LÍNEA DE COMANDOS

La línea de comandos cuenta con un histórico. Usando las
teclas de cursor (ARRIBA/ABAJO) es posible navegar por
las últimas acciones realizadas, para repetir una acción
de forma conveniente o editar un error.

Es posible hacer búsquedas en el histórico usando la
tecla F8. Por ejemplo, para recordar un comando escrito
anteriormente que empezaba por "baja", podemos escribir
"baja" y pulsar F8, y se completará la línea con la última
acción escrita que empezaba así. Pulsar F8 de nuevo sirve
para navegar por el resto de resultados coincidentes.

La línea de comandos cuenta también con opción de
deshacer (Ctrl+Z), con Ctrl+Y para re-hacer el cambio.

Por último, existen algunas teclas de edición convenientes
adicionales: Ctrl+Cursor sirve para moverse de palabra
en palabra, y Ctrl+Borrar (tanto suprimir como la tecla
de borrar a la izquierda) borran una palabra completa.
Y finalmente, la tecla ESC borra la línea entera.

### SONIDO DE PULSACIÓN DE TECLA

Originalmente, solo la versión de Atari ST contaba con esta
funcionalidad. El intérprete actual lo incluye, aunque por
defecto usa un sonido diferente. F10 durante la entrada
de texto permite desactivarlo o cambiarlo.

### SAVE/LOAD

En algunas plataformas, SAVE/LOAD no mostrarán el mensaje
original para grabar partida, sino un cuadro de diálogo
(quizá propio del sistema de escritorio) para escoger.

### WHATO

Cuando el usuario escribe una acción donde hay presente
un adjetivo, pero no un nombre, y además en la frase no
hay ninguna palabra desconocida para el parser, WHATO
dará por válido un objeto que encaje con el adjetivo.

Este cambio es experimental y ha sido incluído para
hacer más jugable Templos Sagrados, que cuenta con un
número de objetos con el mismo nombre pero varios
adjetivos. El DAAD no cuenta con un sistema de
desambiguación como Inform u otros, por lo que obliga
en general a escribir "COGER TABLETA INIX" de forma más
verbosa. Este cambio permite hacer un "COGER INIX".

### PICTURE

El condact PICTURE original carga una imagen en un
buffer interno (un proceso lento, que potencialmente
carga la imagen de disquette). El condacto DISPLAY
muestra luego esta imagen rápidamente, lo cual facilita
la creación de animaciones. Una limitación de este
sistema es que 

En este intérprete, el buffer tiene un tamaño superior
y puede almacenar múltiples imágenes. Un condacto
PICTURE con una imagen ya cargada en este búfer
no tendrá ningún efecto (excepto marcarla para ser
usada por el próximo DISPLAY).

## PAUSAS FORZADAS

Las aventuras de AD contienen un número sorprendente de
animaciones. En algunos casos, estas animaciones usan el
condacto PAUSE para temporizar el movimiento, pero en
otros no, contando con ejecutarse en ordenadores lentos.
Por ejemplo, mostrando (DISPLAY) una misma imágen en
diferentes configuraciones de posición y tamaño para
hacer el efecto de que aparece de forma animada, pero
sin pausa alguna entre cada vez que se muestra.

En ordenadores modernos, estas prácticas resultarían en
animaciones que no pueden llegar ni a verse, porque la
máquina es tan rápida que todas esas operaciones serían
instantáneas. Para mejorar la situación, el intérprete
insertará pausas breves automáticamente cuando se
den alguna de las siguientes condiciones:

* Se va a pintar una imagen en una ventana que ya se
ha usado en este frame para pintar

* Se está cambiando un color que ya ha sido
modificado en este frame

* Se ejecuta un comando GFX que intercambia el
buffer principal y el secundario

* Se hace scroll en una ventana que ha sido detenida
previamente con un "[More...]" o pausa de teclado.

Esta lista es susceptible de cambiar en el futuro,
pero su principal utilidad es hacer los juegos de
Aventuras AD más jugables.

## Cómo colaborar con ADP

La versión en inglés de este fichero, [README.md](README.md),
contiene instrucciones detalladas para compilar el programa
a partir de su código fuente.

El repositorio principal de **ADP** se encuentra en GitHub,
donde estará siempre disponible la última versión y puede
reportarse cualquier bug o sugerencia.

## Licencia

**ADP** se distribuye bajo los términos de la licencia MIT.
Para más información, lea el archivo LICENSE.