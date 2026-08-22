# Guía de instalación

*Verificado en la versión 0.2.0.*

## 1. Elegir la imagen

| Imagen | Para qué máquina |
|---|---|
| `castalia-live-desktop-amd64-X.Y.Z.iso` | un PC de 64 bits (Core 2 Duo o posterior) |
| `castalia-live-i386-X.Y.Z.iso` | un PC de 32 bits (Pentium 4 con SSE2, o cualquier cosa posterior) |
| `castalia-live-amd64-X.Y.Z.iso` | imagen base de 64 bits, sin escritorio |

Si no estás seguro, prueba la de 64 bits: si el equipo no puede arrancarla, no
se rompe nada, simplemente no arranca, y entonces usas la de 32.

**Comprueba la descarga** antes de grabarla:

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

## 2. Hacer el USB (o el CD)

En Linux, con el USB desconectado, mira qué discos hay (`lsblk`), conéctalo,
mira otra vez, y **el que apareció** es el tuyo. Después:

```sh
sudo dd if=castalia-live-i386-X.Y.Z.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

`/dev/sdX` es el disco entero, no una partición (`/dev/sdb`, no `/dev/sdb1`).
Esto **borra el USB**.

En Windows: Rufus o balenaEtcher, en modo «imagen DD».

La imagen es híbrida: el mismo archivo sirve para USB y para CD.

## 3. Arrancar desde él

Enciende y pulsa la tecla de menú de arranque — suele ser `F12`, `F11`, `Esc`
o `F8` según el fabricante; en un equipo de esta época a veces hay que
entrar en la BIOS (`Supr` o `F2`) y cambiar el orden de arranque.

Verás el menú de Castalia. La primera opción es **el escritorio en vivo**: se
carga entero en memoria y no toca el disco duro. Ahí puedes mirar si te
funcionan la pantalla, el sonido y la red **antes de instalar nada**.

Si la pantalla se queda negra o se ve mal, vuelve al menú y elige la entrada
que dice **modo seguro**: arranca sin aceleración gráfica.

## 4. Instalar

Desde el escritorio en vivo, el icono **Instalar Castalia**. O desde el menú
de arranque, la entrada de instalación.

Si el equipo no puede con el instalador gráfico, hay uno **de texto** que hace
exactamente lo mismo — no es una versión recortada, usa el mismo motor.

### Las tres formas de instalar

El instalador mira el disco y ofrece solo lo que **puede hacer en ese disco**,
ordenado de menos destructivo a más:

| Modo | Qué le pasa a lo que ya hay |
|---|---|
| **Junto al sistema actual** | nada. Usa el espacio libre que ya hay |
| **Hacer sitio encogiendo una partición** | se reduce un Windows (o un Linux) existente; sus datos se conservan |
| **Usar el disco entero** | **se borra todo** |

Sobre el segundo: el instalador **nunca** deja al sistema vecino sin espacio
para funcionar. Reserva 4 GiB, o el 15% de lo que tenga dentro, lo que sea más
— porque Windows deja de funcionar mucho antes de llenarse del todo.

Y aun así: **haz una copia de seguridad antes de redimensionar.** Ninguna
operación sobre particiones es gratis.

### Lo que te va a preguntar

- en qué disco;
- cómo (los modos de arriba);
- si encoges, cuánto espacio liberar — te enseña cuánto hay en uso y cuánto
  puede ceder como máximo;
- nombre del equipo, tu nombre de usuario y tu nombre;
- una contraseña (dos veces, y no se ve mientras la escribes).

Y antes del último paso te enseña **la lista completa de lo que va a hacer** y
qué particiones se conservan y cuáles se pierden. Para confirmar hay que
escribir el nombre del disco entero. No hay un botón de «Sí» que se pueda
pulsar sin querer.

## 5. El primer arranque

Quita el USB y enciende. Verás el menú de arranque de Castalia con:

- **Castalia OS** — el arranque normal;
- **Modo seguro** — sin aceleración, un solo núcleo, sin animaciones ni
  sonidos, tema de alto contraste;
- **Recuperación** — el [entorno de recuperación](../recovery/README.md);
- y **el otro sistema operativo**, si había uno.

El menú recuerda lo último que arrancaste y espera 4 segundos.

## Si algo va mal

Ve al [manual de problemas](../troubleshooting/README.md). Empieza por el
síntoma, no por la causa.
