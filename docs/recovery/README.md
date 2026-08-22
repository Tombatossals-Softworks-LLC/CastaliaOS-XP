# Guía de recuperación

*Verificado en la versión 0.2.0.*

Qué hacer cuando el sistema se rompe, ordenado de menos a más grave.

| Síntoma | Empieza por |
|---|---|
| Va raro desde una actualización | [Puntos de restauración](#puntos-de-restauración) |
| Arranca pero la pantalla se ve mal | [Modo seguro](#modo-seguro) |
| No llega al escritorio | [Modo seguro](#modo-seguro) |
| No arranca en absoluto | [Entorno de recuperación](#entorno-de-recuperación) |
| El menú de arranque desapareció | [Entorno de recuperación](#entorno-de-recuperación) → reparar el arranque |

## Puntos de restauración

Un punto de restauración es **una foto del sistema**: los programas, la
configuración, los archivos del sistema. Se toma automáticamente antes de cada
actualización, y a mano cuando quieras.

**No incluye `/home`.** Esto es a propósito y es lo más importante de esta
página: restaurar arregla el sistema y **no deshace tu trabajo**. Un punto de
restauración que te devolviera los documentos a como estaban el martes sería un
error mucho peor que la actualización que venías a arreglar.

### Desde el escritorio

Centro de control → **Recuperación**, o el **Centro de recuperación** en el
menú. Ahí se ven los puntos que hay, con su fecha y por qué se tomaron.

### Desde la terminal

```sh
castalia-restore list                     # qué puntos hay
castalia-restore create --label "antes de tocar X"
castalia-restore restore 20260822-101500 --confirm
castalia-restore prune                    # borrar los más viejos
```

`--dry-run` en cualquiera de ellos enseña exactamente lo que haría sin
hacerlo.

### Restaurar es reversible

Antes de restaurar, Castalia toma **otro punto** automáticamente, marcado como
`pre-restore`. Si la restauración te deja peor, puedes volver.

### Cuánto ocupan

Poco. Los archivos que no cambiaron entre dos puntos **son el mismo archivo en
el disco** (enlaces duros), no dos copias. Funciona en ext4 normal; no hace
falta btrfs.

## Modo seguro

En el menú de arranque: **Castalia OS — Modo seguro (Safe Mode)**.

Arranca el mismo sistema con:

- sin aceleración gráfica (`nomodeset`) — para cuando el controlador de vídeo
  es el problema;
- un solo núcleo (`maxcpus=1`);
- sin animaciones y sin sonidos;
- el tema de alto contraste, que es el que se ve en cualquier pantalla;
- sin los servicios opcionales.

Desde ahí puedes usar el Centro de control para cambiar lo que rompió el
arranque normal, o tomar un punto de restauración.

## Entorno de recuperación

En el menú de arranque: **Castalia OS — Recuperación (Recovery)**.

Esto **no arranca el sistema**. Se queda antes, en el arranque temprano, monta
tu disco desde fuera y te da un menú de cinco opciones:

| Opción | Cuándo |
|---|---|
| **1. Restaurar un punto** | una actualización rompió el sistema |
| **2. Comprobar y reparar el disco** | el sistema no monta, o da errores de disco |
| **3. Reparar el menú de arranque** | Windows se llevó el arranque por delante, o GRUB desapareció |
| **4. Abrir una consola** | sabes lo que haces |
| **5. Reiniciar** | ya está |

Funciona **aunque el sistema no monte**: por eso se ejecuta antes de que el
arranque intente montarlo, y no después. Si tu disco no monta, la opción 2
sigue estando disponible, y es la que hay que probar.

La opción 1 no es una segunda implementación de los puntos de restauración:
entra en tu sistema y ejecuta el mismo `castalia-restore` de siempre. Si
fueran dos programas distintos, uno de los dos estaría mal y nadie sabría
cuál hasta el día que importara.

### Comprobar el disco desmonta primero

La opción 2 desmonta el sistema de archivos antes de comprobarlo, y si no
puede desmontarlo, **se niega**. Comprobar un sistema de archivos montado es
la forma de convertir un error reparable en varios que no lo son.

## Si nada de esto funciona

Arranca desde el USB de instalación, elige el escritorio en vivo, y desde ahí
tus archivos siguen siendo accesibles: el Explorer puede abrir el disco duro y
copiar lo que necesites a otro USB. Primero salvar los datos, después arreglar
el sistema.
