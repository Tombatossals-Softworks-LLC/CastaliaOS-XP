# Manual de problemas

*Verificado en la versión 0.1.1.*

Organizado por **síntoma**, no por causa, porque cuando algo falla lo que sabes
es lo que ves.

---

## No arranca

### Se queda en el logotipo del fabricante

No llega ni al menú de Castalia. El problema es anterior a Castalia: el orden
de arranque de la BIOS, o el USB. Entra en la BIOS (`Supr` o `F2` al
encender) y comprueba que el disco (o el USB) está antes en el orden.

### Sale el menú de Castalia pero después nada

Elige **Modo seguro** en el menú. Si con modo seguro arranca, el problema es
el controlador de vídeo: Centro de control → Pantalla, y baja la resolución o
cambia el modo.

Si tampoco arranca en modo seguro, usa **Recuperación** → opción 2 (comprobar
el disco).

### «No bootable device» / desapareció el menú

El arranque se perdió — lo más típico es que Windows se reinstalase encima al
actualizarse. Arranca el USB de Castalia, elige **Recuperación** en su menú, y
usa la opción 3, *Reparar el menú de arranque*.

---

## Arranca pero se ve mal

### La pantalla está negra desde que cambié la resolución

No debería poder pasar: el Centro de control aplica el modo nuevo, cuenta
atrás y vuelve solo si no confirmas. Si aun así pasó, arranca en **Modo
seguro**, que ignora la configuración de vídeo, y ponlo bien desde ahí.

### Todo está enorme, o diminuto

Centro de control → Pantalla. Si la resolución nativa de tu monitor no
aparece, tu tarjeta no la está detectando: mira el **Centro de hardware** para
ver qué controlador está usando.

### Los colores están mal / se ve granulado

Es una pantalla a 16 bits en vez de 24. Centro de control → Pantalla.

---

## No hay sonido

1. **Control de volumen**: ¿está silenciado? ¿está al mínimo?
2. ¿Está seleccionada la salida correcta? Un equipo con HDMI y altavoces tiene
   dos, y por defecto no siempre coge la que quieres.
3. **Centro de hardware**: busca la tarjeta de sonido. Si dice **SIN
   CONTROLADOR**, el kernel no la reconoció y el volumen no es el problema.

---

## No hay red

### Por cable

**Centro de hardware** → busca la controladora de red. Si aparece con un
controlador cargado, el cable o el router. Si dice **SIN CONTROLADOR**, es la
tarjeta.

### Wi-Fi

Muchas tarjetas Wi-Fi de la época necesitan *firmware* que no se puede
distribuir con el sistema. El **Centro de hardware** te dirá cuál es la tuya
(por ejemplo `14e4:4315`); con ese número se puede buscar qué paquete de
firmware hace falta.

Mientras tanto: por cable, o un adaptador USB.

---

## Se quedó sin espacio

1. **Vacía la papelera.** Lo que hay ahí sigue ocupando disco.
2. **Administrador de discos** para ver cuánto queda y dónde.
3. **Centro de recuperación** → borra los puntos de restauración viejos
   (`castalia-restore prune`). Ocupan poco, pero muchos ocupan.

---

## Va lento

- **Monitor del sistema** (`Ctrl+Alt+Supr`): mira qué proceso está consumiendo.
- Si es lento **desde el arranque** y tienes poca memoria (512 MB), comprueba
  en el Centro de control → Apariencia que el compositor está desactivado.
  En equipos con menos de 2 GB debería estarlo solo.
- Si va lento **desde una actualización**: [punto de
  restauración](../recovery/README.md).

---

## Una aplicación de Windows no funciona

Eso tiene su propia guía: [Aplicaciones de Windows](../wine/README.md). La
respuesta corta es que el Administrador te dice **con qué nota** funciona cada
aplicación antes de que la instales, y esa nota es honesta.

---

## Una actualización rompió algo

[Puntos de restauración](../recovery/README.md). Castalia toma uno
**automáticamente antes de cada actualización**, así que el punto que
necesitas ya existe.

---

## Cómo pedir ayuda

El **Diagnóstico del sistema** genera un informe con lo que hay en el equipo,
qué controladores usa y los últimos errores. Se puede guardar en un archivo y
adjuntarlo. **Míralo antes de enviarlo**: es información sobre tu equipo.

No hay telemetría. Nada sale de la máquina si no lo mandas tú.
