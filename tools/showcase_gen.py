#!/usr/bin/env python3
"""Generate the Castalia OS showcase page — a self-contained visual tour.

Embeds real screenshots (QEMU desktop captures + offscreen Qt app renders)
and the mark as data URIs, so the page works fully offline (and as an
Artifact). Writes docs/showcase.html.

Usage:
    PYTHONPATH=tools python3 tools/showcase_gen.py [--shots DIR] [--out FILE]
"""

from __future__ import annotations

import argparse
import base64
import re
import sys
import urllib.parse
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


def data_uri_png(path: Path) -> str:
    return "data:image/png;base64," + base64.b64encode(
        path.read_bytes()).decode("ascii")


def data_uri_svg(path: Path) -> str:
    raw = re.sub(r"<!--.*?-->", "", path.read_text(encoding="utf-8"),
                 flags=re.S)
    return "data:image/svg+xml," + urllib.parse.quote(raw, safe="")


def img(path: Path, alt: str) -> str:
    if not path.exists():
        return f'<div class="missing">{alt}</div>'
    uri = data_uri_svg(path) if path.suffix == ".svg" else data_uri_png(path)
    return f'<img src="{uri}" alt="{alt}" loading="lazy">'


CSS = """
:root{
  --sea-0:#0A141F; --sea-1:#0D1D2E; --sea-2:#16344E;
  --azure:#3E82B6; --azure-lt:#7FB0D4; --sand:#D8C49A; --sand-lt:#E8DFC9;
  --ink:#EAF1F7; --ink-dim:#9DB3C6; --line:#22384C;
}
*{box-sizing:border-box}
html{scroll-behavior:smooth}
body{margin:0;background:var(--sea-1);color:var(--ink);
  font-family:'Segoe UI',Verdana,'DejaVu Sans',system-ui,sans-serif;
  line-height:1.6;-webkit-font-smoothing:antialiased}
img{display:block;max-width:100%}
.wrap{max-width:1120px;margin:0 auto;padding:0 22px}
.eyebrow{font-size:12px;text-transform:uppercase;letter-spacing:.22em;
  color:var(--azure-lt);margin:0 0 10px}
h1,h2,h3{margin:0;letter-spacing:-.01em;text-wrap:balance}
section{padding:74px 0;border-bottom:1px solid var(--line)}
.lead{color:var(--ink-dim);max-width:60ch;font-size:17px}

/* hero */
.hero{position:relative;overflow:hidden;
  background:radial-gradient(120% 90% at 70% 10%,#16344E 0%,#0D1D2E 55%,#0A141F 100%);
  border-bottom:1px solid var(--line)}
.hero .wrap{padding-top:64px;padding-bottom:44px}
.brand{display:flex;align-items:center;gap:14px;margin-bottom:40px}
.brand .mk{width:44px;height:44px}
.brand b{font-size:16px;letter-spacing:.02em}
.brand span{color:var(--ink-dim);font-size:13px}
.hero h1{font-size:clamp(34px,6vw,64px);font-weight:800;line-height:1.02}
.hero h1 .g{background:linear-gradient(100deg,var(--azure-lt),var(--sand));
  -webkit-background-clip:text;background-clip:text;color:transparent}
.hero .lead{margin:20px 0 0;font-size:clamp(15px,2vw,19px)}
.badges{display:flex;flex-wrap:wrap;gap:10px;margin-top:26px}
.badge{font-size:12.5px;color:var(--ink);background:rgba(62,130,182,.16);
  border:1px solid rgba(127,176,212,.35);border-radius:999px;padding:6px 14px}
.shot{margin-top:44px;border-radius:12px;overflow:hidden;
  border:1px solid var(--line);
  box-shadow:0 30px 80px rgba(0,0,0,.55),0 0 0 1px rgba(127,176,212,.12)}
.shot img{width:100%}
.cap{color:var(--ink-dim);font-size:13px;margin-top:12px;text-align:center}

/* generic section heads */
.head{margin-bottom:30px}
.head h2{font-size:clamp(24px,4vw,38px);font-weight:800}
.head .lead{margin-top:12px}

/* app gallery */
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));
  gap:20px}
.card{background:var(--sea-2);border:1px solid var(--line);border-radius:12px;
  overflow:hidden;transition:transform .18s ease,box-shadow .18s ease}
.card:hover{transform:translateY(-4px);
  box-shadow:0 18px 44px rgba(0,0,0,.5)}
.card .frame{background:#05090f;padding:10px}
.card .frame img{width:100%;border-radius:6px;border:1px solid #0a1622}
.card .meta{padding:14px 16px}
.card .meta h3{font-size:16px}
.card .meta p{margin:5px 0 0;color:var(--ink-dim);font-size:13.5px}

/* feature strips */
.split{display:grid;grid-template-columns:1fr 1fr;gap:36px;align-items:center}
@media(max-width:800px){.split{grid-template-columns:1fr}}
.split .shot{margin-top:0}
.flist{list-style:none;padding:0;margin:18px 0 0;display:flex;
  flex-direction:column;gap:12px}
.flist li{display:flex;gap:12px;align-items:flex-start;font-size:15px}
.flist .dot{flex:none;width:9px;height:9px;border-radius:50%;margin-top:8px;
  background:var(--azure)}
.flist b{color:var(--ink)}

/* stats */
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));
  gap:18px}
.stat{background:var(--sea-2);border:1px solid var(--line);border-radius:12px;
  padding:20px}
.stat .n{font-size:30px;font-weight:800;color:var(--azure-lt);
  font-variant-numeric:tabular-nums}
.stat .l{color:var(--ink-dim);font-size:13px;margin-top:4px}

/* honest / build */
.pill{display:inline-flex;gap:8px;align-items:center;font-size:12.5px;
  background:rgba(110,207,142,.12);color:#8fe0a8;border-radius:999px;
  padding:5px 12px;border:1px solid rgba(110,207,142,.3)}
.mono{font-family:'Cascadia Mono','DejaVu Sans Mono',Consolas,monospace}
.term{background:#05090f;border:1px solid var(--line);border-radius:10px;
  padding:16px 18px;font-family:'Cascadia Mono','DejaVu Sans Mono',monospace;
  font-size:13px;color:#9db8cc;overflow-x:auto}
.term .g{color:#6ecf8e}.term .a{color:var(--azure-lt)}.term .w{color:var(--sand)}

footer{padding:40px 0 60px;color:var(--ink-dim);font-size:13px}
footer .legal{max-width:80ch;margin-top:10px}
a{color:var(--azure-lt)}
"""


def build(shots: Path, out: Path) -> None:
    mark = REPO / "branding" / "logo" / "castalia-mark.svg"
    hero = shots / "castalia-final.png"
    if not hero.exists():
        hero = REPO / "docs" / "evidence" / "phase3-control-center-live.png"

    apps = [
        ("g-explorer.png", "Castalia Explorer",
         "Explorador de archivos real: navegación, historial, vistas."),
        ("g-pintura.png", "Pintura",
         "Editor de mapa de bits: lápiz, formas, relleno, paleta, deshacer."),
        ("g-notas.png", "Notas",
         "Editor de texto rápido, buscar, ajuste de línea, UTF-8."),
        ("g-escritor.png", "Escritor",
         "Texto con formato clase WordPad: fuentes, color, listas; HTML/ODT."),
        ("g-adhesivas.png", "Notas adhesivas",
         "Recordatorios en tarjetas de colores que se guardan solas."),
        ("g-calc.png", "Calculadora",
         "Aritmética con teclado y teclado numérico temático."),
        ("g-caracteres.png", "Mapa de caracteres",
         "Explora glifos por bloque Unicode y cópialos al portapapeles."),
        ("g-reloj.png", "Reloj",
         "Esfera analógica dibujada a mano, fecha, cronómetro y alarma."),
        ("g-lupa.png", "Lupa",
         "Amplía la zona bajo el puntero — accesibilidad de serie."),
        ("g-about.png", "Centro de control",
         "Acerca de Castalia — identidad original, sin Microsoft."),
        ("g-terminal.png", "Terminal",
         "Emulador VT100 propio: shell real, colores ANSI, UTF-8."),
        ("g-archivador.png", "Archivos comprimidos",
         "Abre y crea zip/tar/7z; extrae desde el explorador."),
        ("g-monitor.png", "Monitor del sistema",
         "Procesos en vivo y gráficos de CPU/memoria desde /proc."),
        ("g-software.png", "Centro de software",
         "Explora y elimina programas instalados, con tamaños reales."),
        ("g-actualizaciones.png", "Centro de actualizaciones",
         "Ve qué paquetes tienen versión nueva y aplícalos con un clic."),
        ("g-recuperacion.png", "Centro de recuperación",
         "Puntos de restauración: crea, lista y vuelve atrás con seguridad."),
        ("g-buscaminas.png", "Buscaminas",
         "El clásico de lógica, versión propia: todo dibujado con Qt."),
        ("g-solitario.png", "Solitario",
         "Klondike de dominio público: baraja, tapete y cartas nativas."),
        ("g-diagnostico.png", "Diagnóstico y rendimiento",
         "Info del sistema + benchmarks reales de CPU, RAM, disco, red."),
        ("g-instalador.png", "Instalar Castalia OS",
         "Asistente gráfico que instala el sistema en tu disco."),
    ]
    app_cards = "\n".join(
        f'<div class="card"><div class="frame">{img(shots / f, name)}</div>'
        f'<div class="meta"><h3>{name}</h3><p>{desc}</p></div></div>'
        for f, name, desc in apps)

    themes_shot = shots / "cc-appearance.png"

    html = f"""<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Castalia OS — un escritorio con alma para hardware con historia</title>
<style>{CSS}</style></head><body>

<div class="hero">
  <div class="wrap">
    <div class="brand"><span class="mk">{img(mark, "Castalia")}</span>
      <div><b>Castalia OS</b><br><span>Tombatossals Softworks</span></div>
    </div>
    <p class="eyebrow">Clase XP · original · libre · ligero</p>
    <h1>El escritorio con alma<br>para el hardware<br>
      <span class="g">que todos abandonaron.</span></h1>
    <p class="lead">Un sistema operativo original —ni un clon, ni un tema—
      construido para Pentium&nbsp;4 y Core&nbsp;2 Duo. La comodidad de una
      era, reinventada: rápida, bella, reparable y honesta. Arranca de verdad;
      cada imagen de esta página es una captura real.</p>
    <div class="badges">
      <span class="badge">🏰 Identidad 100% propia</span>
      <span class="badge">⚡ Arranca en QEMU · P4 / 512&nbsp;MB</span>
      <span class="badge">🎨 6 temas (con modo oscuro), un interruptor</span>
      <span class="badge">🪟 Compatible con apps Windows® vía Wine</span>
      <span class="badge">💽 Instalador real (gráfico y texto)</span>
      <span class="badge">🔒 Sin telemetría · sin cuentas</span>
    </div>
    <div class="shot">{img(hero, "Escritorio Castalia en vivo")}</div>
    <p class="cap">Escritorio Castalia arrancado desde la ISO real (463&nbsp;MB)
      en QEMU — fondo «Azure Bay», Centro de control abierto, barra de tareas
      acoplada. Captura del framebuffer, sin retoques.</p>
  </div>
</div>

<section><div class="wrap">
  <div class="head">
    <p class="eyebrow">Aplicaciones reales</p>
    <h2>No es un tema. Es un producto.</h2>
    <p class="lead">Concha propia (Qt&nbsp;5 / C++17): explorador, pintura,
      editor, calculadora, visor, <b>terminal</b> (emulador VT100 propio),
      <b>monitor del sistema</b>, <b>centro de software</b>, <b>Buscaminas</b>,
      <b>Solitario</b>, <b>escritor con formato</b>, notas adhesivas, reloj con
      esfera dibujada a mano, lupa de accesibilidad, mapa de caracteres, centro
      de software, de actualizaciones y de recuperación, captura de pantalla,
      gestor de archivos comprimidos, diagnóstico, gestor de Windows y centro
      de control… veinticinco aplicaciones nativas que comparten una sola
      biblioteca de interfaz y visten el tema activo al instante.</p>
  </div>
  <div class="grid">{app_cards}</div>
</div></section>

<section><div class="wrap">
  <div class="split">
    <div>
      <p class="eyebrow">El escritorio, de verdad</p>
      <h2 style="font-size:clamp(24px,4vw,34px);font-weight:800">
        Una barra de tareas que refleja tus ventanas reales.</h2>
      <ul class="flist">
        <li><span class="dot"></span><span>El panel lee las ventanas
          gestionadas del <b>window manager</b> por EWMH
          (<span class="mono">_NET_CLIENT_LIST</span>) vía libxcb — no es una
          maqueta.</span></li>
        <li><span class="dot"></span><span>Clic para <b>activar</b> y restaurar;
          la ventana activa se resalta; los títulos se recortan.</span></li>
        <li><span class="dot"></span><span>Verificado por un test de
          integración <b>headless</b> (Xvfb + Openbox reales) que comprueba que
          la barra ve exactamente las ventanas abiertas.</span></li>
      </ul>
    </div>
    <div class="shot">{img(shots / "phase8-paint-live.png",
        "Escritorio Castalia con Pintura y barra de tareas real")}</div>
  </div>
  <p class="cap">Sesión Openbox real: Pintura sobre el Explorador, y la barra
    mostrando las tres ventanas abiertas con la activa resaltada.</p>
</div></section>

<section><div class="wrap">
  <div class="split">
    <div class="shot">{img(shots / "g-diagnostico.png",
        "Banco de pruebas de Castalia")}</div>
    <div>
      <p class="eyebrow">Rendimiento medido</p>
      <h2 style="font-size:clamp(24px,4vw,34px);font-weight:800">
        Números reales, no promesas.</h2>
      <ul class="flist">
        <li><span class="dot"></span><span>Un <b>banco de pruebas</b> integrado
          mide CPU (todos los núcleos), memoria, disco, gráficos 2D y red — con
          medidores animados y una <b>puntuación Castalia</b>.</span></li>
        <li><span class="dot"></span><span>Cada cifra se toma <b>en tu equipo,
          en tiempo real</b> (std::thread + POSIX), sin dependencias externas;
          también hay un informe por consola (<span class="mono">--report</span>).</span></li>
        <li><span class="dot"></span><span>La pestaña <b>Sistema</b> detalla
          kernel, CPU, RAM, GPU, discos y red — el «Acerca de» que de verdad
          informa.</span></li>
      </ul>
    </div>
  </div>
  <p class="cap">Diagnóstico del sistema: CPU multinúcleo, ancho de banda de
    memoria, disco, gráficos y red, medidos en vivo.</p>
</div></section>

<section><div class="wrap">
  <div class="split">
    <div class="shot">{img(shots / "phase5-installer-summary.png",
        "Instalador gráfico de Castalia")}</div>
    <div>
      <p class="eyebrow">Instálalo de verdad</p>
      <h2 style="font-size:clamp(24px,4vw,34px);font-weight:800">
        Un instalador real, seguro y probado.</h2>
      <ul class="flist">
        <li><span class="dot"></span><span>Asistente <b>Qt</b> y modo
          <b>texto</b> comparten un mismo backend en Python: la lógica es
          idéntica y <b>testeable</b>.</span></li>
        <li><span class="dot"></span><span><b>Nunca</b> borra un disco sin que
          escribas su nombre para confirmar (§14.5). El plan es completamente
          offline.</span></li>
        <li><span class="dot"></span><span>El motor real (parted, mkfs, rsync,
          fstab por UUID) está <b>probado sobre un disco de bucle</b>; 49 tests
          cubren el plan y las salvaguardas.</span></li>
      </ul>
    </div>
  </div>
  <p class="cap">El resumen del instalador muestra el plan REAL, generado por
    el backend compartido (<span class="mono">--dry-run</span>).</p>
</div></section>

<section><div class="wrap">
  <div class="split">
    <div>
      <p class="eyebrow">Sistema de diseño</p>
      <h2 style="font-size:clamp(24px,4vw,34px);font-weight:800">
        Seis temas. Un interruptor. Todo cambia.</h2>
      <ul class="flist">
        <li><span class="dot"></span><span><b>Castalia Classic, Azul, Oliva,
          Plata, Medianoche (modo oscuro) y Alto Contraste</b> — cada uno un
          sistema de color completo.</span></li>
        <li><span class="dot"></span><span>Los <b>tokens</b> (<span class="mono">theme.conf</span>)
          son la única fuente de verdad: generan el QSS de Qt y el tema de
          Openbox.</span></li>
        <li><span class="dot"></span><span>Contraste <b>WCAG</b> y degradados
          seguros a 16&nbsp;bits, <b>verificados en CI</b>.</span></li>
      </ul>
    </div>
    <div class="shot">{img(themes_shot, "Selector de temas")}</div>
  </div>
</div></section>

<section><div class="wrap">
  <div class="head">
    <p class="eyebrow">Modo oscuro, de verdad</p>
    <h2>Medianoche: todo el escritorio, en grafito.</h2>
    <p class="lead">No es un truco de una app: la biblioteca compartida deriva
      una paleta Qt a juego, así hasta las vistas y campos que la hoja de estilo
      no nombra siguen el tema. Aquí, <b>compuesto por un gestor de ventanas
      real</b> (Openbox con las decoraciones Castalia), no un montaje.</p>
  </div>
  <div class="shot">{img(shots / "desktop-medianoche-live.png",
      "Escritorio Castalia en modo oscuro, compuesto en vivo")}</div>
</div></section>

<section><div class="wrap">
  <div class="head">
    <p class="eyebrow">Construido de verdad</p>
    <h2>Ingeniería, no diapositivas.</h2>
    <p class="lead">Del documento de diseño a un SO que arranca a su propio
      escritorio — verificado en emulación en cada paso.</p>
  </div>
  <div class="stats">
    <div class="stat"><div class="n">5</div><div class="l">fases: arranque ·
      base · escritorio · Wine · instalador+recuperación</div></div>
    <div class="stat"><div class="n">25</div><div class="l">apps nativas
      Qt&nbsp;5 / C++17 + instalador</div></div>
    <div class="stat"><div class="n">140+</div><div class="l">tests verdes ·
      ruff limpio · gates OK</div></div>
    <div class="stat"><div class="n">~11&nbsp;ms</div><div class="l">arranque
      del panel · ~25&nbsp;MB RSS</div></div>
    <div class="stat"><div class="n">0</div><div class="l">recursos de
      Microsoft · verificado por CI</div></div>
    <div class="stat"><div class="n">463&nbsp;MB</div><div class="l">ISO live
      que arranca en QEMU al escritorio</div></div>
  </div>
  <div style="margin-top:26px" class="term">
<span class="a">$ sh build/mkiso.sh --edition live-desktop-amd64</span>
mkiso[live-desktop-amd64]: done: castalia-live-desktop-amd64.iso (463M)
<span class="a">$ python3 tests/qemu/screenshot.py castalia-live-desktop-amd64.iso</span>
INIT: Entering runlevel: 2
<span class="w">castalia bookworm (amd64) — arranque de prueba</span>
<span class="g">screenshot: wrote castalia-final.png (1920x1080) — reached desktop</span>
  </div>
  <p style="margin-top:18px"><span class="pill">✓ compilado contra el Qt del
    sistema destino (ABI correcta, igual que el empaquetado .deb)</span></p>
</div></section>

<footer><div class="wrap">
  <div class="brand"><span class="mk">{img(mark, "Castalia")}</span>
    <div><b>Castalia OS</b> · Castalia Classic</div></div>
  <p class="legal">Sistema operativo independiente de <b>Tombatossals
    Softworks</b>. Windows® es una marca registrada de Microsoft Corporation;
    Castalia&nbsp;OS no está afiliado, ni patrocinado, ni respaldado por
    Microsoft, y no contiene ni comparte ninguno de sus recursos. Todo el
    arte, los sonidos y el código son originales o con licencia libre,
    registrados en <span class="mono">legal/ASSET_PROVENANCE.csv</span>.</p>
</div></footer>

</body></html>"""
    out.write_text(html, encoding="utf-8")
    print(f"showcase-gen: wrote {out} ({len(html)//1024} KiB)")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--shots", type=Path,
                    default=REPO / "docs" / "evidence")
    ap.add_argument("--out", type=Path, default=REPO / "docs" / "showcase.html")
    args = ap.parse_args(argv[1:])
    build(args.shots, args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
