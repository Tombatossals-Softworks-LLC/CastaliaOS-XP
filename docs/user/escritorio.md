# El escritorio

*Verificado en la versión 0.2.0.*

## Las dos mitades

Castalia dibuja el escritorio en **dos programas separados**, y conviene
saberlo porque explica algún comportamiento:

- `castalia-desktop` pinta el fondo y los iconos.
- `castalia-panel` pinta la barra de tareas de abajo: el botón de Inicio, los
  botones de las ventanas abiertas, el área de notificación y el reloj.

Si uno de los dos se cierra, el otro sigue funcionando. No es lo habitual,
pero si alguna vez ves el fondo sin barra de tareas, eso es lo que pasó:
cierra la sesión (Súper+Escape) y vuelve a entrar.

## Los iconos del escritorio

Los iconos son los archivos que hay en tu carpeta `Escritorio`. Poner un
archivo ahí lo hace aparecer; borrarlo de ahí lo hace desaparecer. No hay una
base de datos oculta.

Doble clic abre. Un clic selecciona. El menú del botón derecho ofrece abrir,
renombrar y enviar a la papelera.

## La barra de tareas

De izquierda a derecha:

| Zona | Qué hace |
|---|---|
| **Inicio** | abre el menú de aplicaciones (también con la tecla Súper) |
| Botones de ventana | uno por ventana abierta; clic para traerla al frente |
| Área de notificación | volumen, red, y los avisos del sistema |
| Reloj | la hora; clic para el calendario |

Los botones de ventana los sabe la barra por **EWMH**, el mismo protocolo que
usa cualquier gestor de ventanas de X11. Eso significa que una ventana de una
aplicación que no sea de Castalia — una de Wine, por ejemplo — también sale
ahí.

## Cambiar el fondo y el tema

Centro de control → **Apariencia**. Castalia trae siete temas:

| Tema | Qué es |
|---|---|
| **Castalia Human** | el tema por defecto: cálido, tostado, amanecer |
| Castalia Classic | azul y gris, el más parecido a la época |
| Castalia Azul | azul más saturado |
| Castalia Plata | gris plateado, sobrio |
| Castalia Oliva | verdes apagados |
| Castalia Medianoche | oscuro |
| Castalia High Contrast | alto contraste, para ver mal o pantallas muy malas |

Un tema cambia **todo a la vez**: los colores de las ventanas, los bordes, los
iconos, el puntero, la pantalla de acceso y los sonidos. No hay que
configurarlos por separado.
