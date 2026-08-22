# Aplicaciones de Windows (Wine)

*Verificado en la versión 0.1.1.*

Castalia puede ejecutar muchos programas de Windows mediante **Wine**. Muchos,
no todos, y esta guía existe para que sepas de antemano en cuál de los dos
grupos está el tuyo.

## Lo que Wine es y no es

Wine **no es una máquina virtual** y no lleva Windows dentro. Traduce lo que
un programa de Windows le pide al sistema operativo a lo que Linux entiende.
Cuando el programa pide algo que Wine no traduce, falla — a veces al abrirse,
a veces media hora después, al ir a imprimir.

Por eso Castalia no dice «compatible» a secas.

## Las notas

Cada aplicación en el **Administrador de aplicaciones de Windows** lleva una
nota:

| Nota | Qué significa en la práctica |
|---|---|
| **Platino** | funciona tal cual, sin tocar nada |
| **Oro** | funciona, con algún ajuste que el Administrador ya aplica |
| **Plata** | funciona, con fallos menores que se notan pero no estorban |
| **Bronce** | arranca, pero hay partes que no van |
| **No funciona** | no arranca, o arranca y no sirve para nada |

La nota se muestra **antes** de instalar, no después. Un programa en Bronce se
puede instalar igual — pero sabiendo lo que hay.

## Prefijos: una carpeta de Windows por aplicación

Cada aplicación tiene su propio **prefijo**: una carpeta que hace de disco C:
solo para ella. Eso significa que:

- una aplicación no puede romper a otra;
- se puede tener la misma aplicación en dos versiones distintas;
- **desinstalar es borrar la carpeta**, sin residuos.

Cuesta algo de disco (cada prefijo son unos cientos de megas). A cambio,
ninguna instalación puede estropear las demás, que es exactamente el problema
que tenía hacerlo de la otra forma.

## Lo que no va a funcionar

Merece la pena decirlo claro para no perder una tarde:

- **Programas que necesitan un controlador de Windows.** Un antivirus, una
  utilidad de disco, cualquier cosa que instale algo «a bajo nivel». Wine no
  ejecuta controladores de Windows y no va a hacerlo.
- **Juegos con anti-trampas.** Los anticheat modernos comprueban precisamente
  que están en un Windows de verdad.
- **Programas muy nuevos con .NET reciente o DirectX 12.** Castalia apunta a
  software de la época; ahí es donde funciona bien.
- **Lo que dependa de un servicio de Windows** que Wine no implementa.

Si tu programa está en alguno de estos grupos, la respuesta honesta es que no,
y es mejor saberlo ahora.

## Juegos de DOS y aventuras gráficas

Esos no van por Wine. **Juegos clásicos** en el menú usa **DOSBox-X** para DOS
y **ScummVM** para las aventuras gráficas clásicas, que son emuladores
específicos y funcionan mucho mejor que Wine para eso.

## Cuando algo falla

1. Mira la nota. Si es Bronce o No funciona, ya sabes.
2. En el Administrador, la aplicación tiene un botón de **winetricks** para
   añadir componentes concretos (una fuente, una biblioteca).
3. Borra el prefijo y vuelve a instalar. Como cada aplicación tiene el suyo,
   no pierdes nada más.

## Legalidad

Wine es software libre y no contiene código de Microsoft. Los programas de
Windows que ejecutes son tuyos y su licencia es cosa entre tú y quien te la
dio; Castalia no distribuye ninguno.
