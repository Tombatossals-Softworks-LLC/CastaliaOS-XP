#include "AppRoster.h"

#include <QCoreApplication>
#include <QHash>

namespace castalia {
namespace apps {

const QVector<Group> &roster()
{
    static const QVector<Group> groups = {
        {QT_TRANSLATE_NOOP("AppRoster", "Accesorios"), {
            {QT_TRANSLATE_NOOP("AppRoster", "Notas"),
             "castalia-notas", "text-editor"},
            {QT_TRANSLATE_NOOP("AppRoster", "Notas adhesivas"),
             "castalia-adhesivas", "sticky-note"},
            {QT_TRANSLATE_NOOP("AppRoster", "Escritor"),
             "castalia-escritor", "richtext"},
            {QT_TRANSLATE_NOOP("AppRoster", "Pintura"),
             "castalia-pintura", "paint"},
            {QT_TRANSLATE_NOOP("AppRoster", "Calculadora"),
             "castalia-calc", "calculator"},
            {QT_TRANSLATE_NOOP("AppRoster", "Mapa de caracteres"),
             "castalia-caracteres", "charmap"},
            {QT_TRANSLATE_NOOP("AppRoster", "Reloj"),
             "castalia-reloj", "clock"},
            {QT_TRANSLATE_NOOP("AppRoster", "Calendario"),
             "castalia-calendario", "calendar"},
            {QT_TRANSLATE_NOOP("AppRoster", "Lupa"),
             "castalia-lupa", "magnifier"},
            {QT_TRANSLATE_NOOP("AppRoster", "Visor de imágenes"),
             "castalia-visor", "image-viewer"},
            {QT_TRANSLATE_NOOP("AppRoster", "Reproductor multimedia"),
             "castalia-multimedia", "media-player"},
            {QT_TRANSLATE_NOOP("AppRoster", "Archivos comprimidos"),
             "castalia-archivador", "archive"},
            {QT_TRANSLATE_NOOP("AppRoster", "Captura de pantalla"),
             "castalia-captura", "camera"},
        }},
        {QT_TRANSLATE_NOOP("AppRoster", "Juegos"), {
            {QT_TRANSLATE_NOOP("AppRoster", "Buscaminas"),
             "castalia-buscaminas", "mine"},
            {QT_TRANSLATE_NOOP("AppRoster", "Solitario"),
             "castalia-solitario", "cards"},
        }},
        {QT_TRANSLATE_NOOP("AppRoster", "Sistema"), {
            {QT_TRANSLATE_NOOP("AppRoster", "Castalia Explorer"),
             "castalia-explorer", "folder"},
            {QT_TRANSLATE_NOOP("AppRoster", "Papelera de reciclaje"),
             "castalia-papelera", "trash"},
            {QT_TRANSLATE_NOOP("AppRoster", "Buscar archivos"),
             "castalia-buscar", "search"},
            {QT_TRANSLATE_NOOP("AppRoster", "Terminal"),
             "castalia-terminal", "terminal"},
            {QT_TRANSLATE_NOOP("AppRoster", "Ejecutar…"),
             "castalia-ejecutar", "terminal"},
            {QT_TRANSLATE_NOOP("AppRoster", "Monitor del sistema"),
             "castalia-monitor", "chart"},
            {QT_TRANSLATE_NOOP("AppRoster", "Visor de registros"),
             "castalia-registros", "documents"},
            {QT_TRANSLATE_NOOP("AppRoster", "Historial de notificaciones"),
             "castalia-notificaciones",
             "help", "--historial"},
            {QT_TRANSLATE_NOOP("AppRoster", "Servicios del sistema"),
             "castalia-servicios", "services"},
            {QT_TRANSLATE_NOOP("AppRoster", "Centro de hardware"),
             "castalia-hardware", "computer"},
            {QT_TRANSLATE_NOOP("AppRoster", "Administrador de discos"),
             "castalia-discos", "disk"},
            {QT_TRANSLATE_NOOP("AppRoster", "Asistente de migración"),
             "castalia-migrar", "home"},
            {QT_TRANSLATE_NOOP("AppRoster", "Control de volumen"),
             "castalia-volumen", "speaker"},
            {QT_TRANSLATE_NOOP("AppRoster", "Fecha y hora"),
             "castalia-fechahora", "clock"},
            {QT_TRANSLATE_NOOP("AppRoster", "Cuentas de usuario"),
             "castalia-usuarios", "users"},
            {QT_TRANSLATE_NOOP("AppRoster", "Impresoras"),
             "castalia-impresoras", "printer"},
            {QT_TRANSLATE_NOOP("AppRoster", "Centro de redes"),
             "castalia-redes", "network", nullptr,
             "conexiones red wifi internet ip dns"},
            {QT_TRANSLATE_NOOP("AppRoster", "Programas predeterminados"),
             "castalia-predeterminados",
             "package"},
            {QT_TRANSLATE_NOOP("AppRoster", "Diagnóstico del sistema"),
             "castalia-diagnostico", "gauge"},
            {QT_TRANSLATE_NOOP("AppRoster", "Centro de software"),
             "castalia-software", "package"},
            {QT_TRANSLATE_NOOP("AppRoster", "Centro de actualizaciones"),
             "castalia-actualizaciones",
             "update"},
            {QT_TRANSLATE_NOOP("AppRoster", "Centro de recuperación"),
             "castalia-recuperacion", "shield"},
            {QT_TRANSLATE_NOOP("AppRoster", "Salir de Castalia"),
             "castalia-salir", "power"},
        }},
        {QT_TRANSLATE_NOOP("AppRoster", "Compatibilidad"), {
            {QT_TRANSLATE_NOOP("AppRoster", "Aplicaciones de Windows"),
             "castalia-wine", "compat"},
            {QT_TRANSLATE_NOOP("AppRoster", "Juegos clásicos (DOS/ScummVM)"),
             "castalia-clasicos",
             "joystick"},
        }},
        {QT_TRANSLATE_NOOP("AppRoster", "Accesibilidad"), {
            {QT_TRANSLATE_NOOP("AppRoster", "Teclado en pantalla"),
             "castalia-teclado", "keyboard"},
            {QT_TRANSLATE_NOOP("AppRoster", "Lupa (magnificador)"),
             "castalia-lupa", "magnifier"},
        }},
    };
    return groups;
}

QString label(const Entry &entry)
{
    return QCoreApplication::translate("AppRoster", entry.label);
}

QString title(const Group &group)
{
    return QCoreApplication::translate("AppRoster", group.title);
}

QString iconForBinary(const QString &bin)
{
    // Built once: the switcher asks for every window it lists, and it is
    // rebuilt on every Alt+Tab.
    static const QHash<QString, QString> byBinary = []() {
        QHash<QString, QString> map;
        for (const Group &g : roster())
            for (const Entry &e : g.entries)
                map.insert(QString::fromLatin1(e.bin),
                           QString::fromLatin1(e.icon));
        return map;
    }();
    return byBinary.value(bin);
}

} // namespace apps
} // namespace castalia
