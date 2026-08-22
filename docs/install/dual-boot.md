# Instalar junto a Windows

*Verificado en la versión 0.2.0.*

Se puede, y Castalia lo hace sin tocar los datos que ya hay. Esta página
explica exactamente qué hace, para que puedas decidir con información en vez
de con fe.

## Antes de empezar

**Haz una copia de seguridad.** Esto no es una fórmula de cortesía: cambiar el
tamaño de una partición mueve estructuras del sistema de archivos, y aunque
todo esté bien hecho, un corte de luz a mitad es un corte de luz a mitad.

**En Windows, desactiva el Inicio rápido y la hibernación.** Con cualquiera de
los dos activos, Windows no cierra el disco del todo: lo deja marcado como «en
uso». Castalia **se negará a redimensionarlo** en ese estado, que es lo
correcto, pero significa que no podrás instalar hasta arreglarlo. En Windows,
como administrador:

```
powercfg /h off
```

y después reinicia Windows y apágalo del todo (Apagar, no Reiniciar).

## Qué hace Castalia, paso a paso

Si hay hueco libre, no toca nada: pone sus tres particiones ahí.

Si no lo hay, y eliges encoger:

1. comprueba que la partición **no está montada**;
2. comprueba el sistema de archivos, y **se detiene** si está sucio (es decir,
   si Windows lo dejó a medias);
3. **ensaya** el encogido sin escribir nada;
4. **encoge el sistema de archivos** — primero;
5. **encoge la partición** — después;
6. vuelve a comprobar el sistema de archivos.

El orden de los pasos 4 y 5 es toda la seguridad de la operación. Al revés, el
borde nuevo de la partición cae dentro de un sistema de archivos que todavía
cree que le pertenece el espacio de más allá, y lo que hubiera ahí deja de
poder leerse — sin ningún error en el momento.

## Cuánto espacio deja

Nunca menos de lo que la partición está usando **más 4 GiB, o más el 15% de lo
que usa, lo que sea mayor**. Windows deja de instalar actualizaciones, y
después deja de arrancar, bastante antes de llenarse.

Si pides más de lo que se puede, el instalador te dice el número máximo. No
recorta tu petición en silencio.

## Qué sistemas de archivos sabe encoger

NTFS (Windows) y ext2/ext3/ext4 (Linux). Cualquier otro se rechaza **por su
nombre**, no se intenta con esperanza.

## Después

El menú de arranque tendrá las dos entradas. Castalia usa `os-prober` para
encontrar el otro sistema, así que aparece solo.

Si Windows deja de arrancar después (raro, pero ocurre si Windows se
reinstala el arranque a sí mismo), el
[entorno de recuperación](../recovery/README.md) tiene una opción para
reparar el menú de arranque.
