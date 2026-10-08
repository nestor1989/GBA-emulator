"""Store screenshots and feature graphic for Play Again.

Usage: pip install playwright pillow && python3 render_screenshots.py [locale ...]
Needs the Inter and Noto Color Emoji fonts installed.

App screens are rebuilt in HTML from the app's real layout math (GameActivity,
GamepadView), real strings (res/values*/strings.xml) and real Cometa frames
captured from mGBA. Then each screen goes into a 1080x1920 marketing frame.
"""
import base64
import html
import io
import math
import os
import re
import sys
import xml.dom.minidom as minidom

from PIL import Image
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
FRAMES = os.path.join(HERE, "frames")          # Cometa frames captured with homebrew/cometa/tools/capture.c
OUT = os.path.join(ROOT, "fastlane", "metadata", "android")
RES = os.path.join(ROOT, "app", "src", "main", "res")
WORK = os.path.join(HERE, "build")
os.makedirs(WORK, exist_ok=True)

ONLY = sys.argv[1:]
LOCALES = {"en-US": "values", "es-419": "values-es", "es-ES": "values-es", "pt-BR": "values-pt-rBR",
           "fr-FR": "values-fr", "de-DE": "values-de", "it-IT": "values-it"}


def load_strings(folder):
    out = {}
    d = minidom.parse(os.path.join(RES, folder, "strings.xml"))
    for e in d.getElementsByTagName("string"):
        v = e.firstChild.data if e.firstChild else ""
        out[e.getAttribute("name")] = v.replace("\\'", "'").replace("\\n", "\n")
    return out


EN = load_strings("values")

# ---- Themes (Cosmetics.kt) -------------------------------------------------------
def argb(v):
    a, r, g, b = (v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255
    return f"rgba({r},{g},{b},{a / 255:.3f})"


def with_alpha(v, a):
    return (v & 0xFFFFFF) | (a << 24)


CLASSIC = dict(panel=0xFF121418, button=0x26FFFFFF, face=0x26FFFFFF, pressed=0x77FFFFFF, outline=0x55FFFFFF,
               label=0xE6FFFFFF, accent=0xFF4F8CFF, onAccent=0xFFFFFFFF, tint=0xFFFFFFFF)
MIDNIGHT = dict(panel=0xFF0A1024, button=0xFF1A2547, face=0xFF2D52B0, pressed=0xFF5B86F0, outline=0xFF34477E,
                label=0xFFDCE6FF, accent=0xFF4F7BFF, onAccent=0xFFFFFFFF, tint=0xFFBFD0FF)


def theme_colors(t, overlay):
    tinted = t["face"] != t["button"]
    if overlay:
        return dict(idle=with_alpha(t["tint"], 0x33),
                    face=with_alpha(t["accent"], 0x55) if tinted else with_alpha(t["tint"], 0x33),
                    pressed=with_alpha(t["tint"], 0x88), outline=with_alpha(t["tint"], 0x66),
                    label=with_alpha(t["tint"], 0xCC), chipFill=with_alpha(t["tint"], 0x26),
                    chipStroke=with_alpha(t["tint"], 0x40), chipText=with_alpha(t["tint"], 0xF2))
    return dict(idle=t["button"], face=t["face"], pressed=t["pressed"], outline=t["outline"], label=t["label"],
                chipFill=t["button"], chipStroke=t["outline"], chipText=t["label"])


# ---- GamepadView geometry (dp) ------------------------------------------------------------
def gamepad_svg(w, h, t, overlay, pressed=()):
    c = theme_colors(t, overlay)
    scale = min(max(h / 300, 0.8), 1.3) if overlay else min(max(min(w / 360, h / 290), 0.75), 1.5)
    s = lambda v: v * scale
    margin, shW, shH, faceR, pillW, pillH, gap, R = s(16), s(110), s(44), s(34), s(76), s(30), s(20), s(72)
    if overlay:
        top = margin
        pillY = h - margin - pillH / 2
        cy = pillY - pillH / 2 - s(70)
    else:
        group = shH + gap + R * 2 + gap + pillH
        spare = max(0, h - group - margin * 2)
        top = margin + spare * 0.2
        cy = top + shH + gap + R
        pillY = margin + spare * 0.55 + shH + gap + R * 2 + gap + pillH / 2
    dx = margin + R + s(4)
    arm, reach = R * 0.36, R * 0.95
    k = arm * 0.35
    x, y = dx, cy
    p = (f"M{x-arm+k},{y-reach} H{x+arm-k} A{k},{k} 0 0 1 {x+arm},{y-reach+k} V{y-arm} H{x+reach-k} "
         f"A{k},{k} 0 0 1 {x+reach},{y-arm+k} V{y+arm-k} A{k},{k} 0 0 1 {x+reach-k},{y+arm} H{x+arm} "
         f"V{y+reach-k} A{k},{k} 0 0 1 {x+arm-k},{y+reach} H{x-arm+k} A{k},{k} 0 0 1 {x-arm},{y+reach-k} "
         f"V{y+arm} H{x-reach+k} A{k},{k} 0 0 1 {x-reach},{y+arm-k} V{y-arm+k} A{k},{k} 0 0 1 {x-reach+k},{y-arm} "
         f"H{x-arm} V{y-reach+k} A{k},{k} 0 0 1 {x-arm+k},{y-reach} Z")
    sw = 1.5
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" style="position:absolute;left:0;top:0">']
    out.append(f'<path d="{p}" fill="{argb(c["idle"])}" stroke="{argb(c["outline"])}" stroke-width="{sw}"/>')
    if "UP" in pressed:
        out.append(f'<rect x="{x-arm}" y="{y-reach}" width="{2*arm}" height="{reach-arm}" rx="{k}" fill="{argb(c["pressed"])}"/>')
    if "RIGHT" in pressed:
        out.append(f'<rect x="{x+arm}" y="{y-arm}" width="{reach-arm}" height="{2*arm}" rx="{k}" fill="{argb(c["pressed"])}"/>')
    out.append(f'<circle cx="{x}" cy="{y}" r="{arm*0.45}" fill="none" stroke="{argb(c["outline"])}" stroke-width="{sw}"/>')
    font = f'font-family="Inter" font-weight="700" font-size="{s(15):.2f}" text-anchor="middle" dominant-baseline="central"'
    pills = [("L", margin, top, shW, shH), ("R", w - margin - shW, top, shW, shH),
             ("SELECT", w / 2 - s(8) - pillW, pillY - pillH / 2, pillW, pillH),
             ("START", w / 2 + s(8), pillY - pillH / 2, pillW, pillH)]
    for lab, bx, by, bw, bh in pills:
        f = c["pressed"] if lab in pressed else c["idle"]
        out.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="{bh/2}" fill="{argb(f)}" stroke="{argb(c["outline"])}" stroke-width="{sw}"/>')
        out.append(f'<text x="{bx+bw/2}" y="{by+bh/2}" {font} fill="{argb(c["label"])}">{lab}</text>')
    aX = w - margin - faceR - s(6)
    aY = cy - s(22)
    for lab, fx, fy in (("A", aX, aY), ("B", aX - s(80), cy + s(24))):
        f = c["pressed"] if lab in pressed else c["face"]
        out.append(f'<circle cx="{fx}" cy="{fy}" r="{faceR}" fill="{argb(f)}" stroke="{argb(c["outline"])}" stroke-width="{sw}"/>')
        out.append(f'<text x="{fx}" y="{fy}" {font} fill="{argb(c["label"])}">{lab}</text>')
    out.append("</svg>")
    return "".join(out)


def chips(labels, active, t, overlay):
    c = theme_colors(t, overlay)
    out = []
    for i, lab in enumerate(labels):
        if i == active:
            style = f"background:{argb(t['accent'])};color:{argb(t['onAccent'])};border:1px solid transparent"
        else:
            style = f"background:{argb(c['chipFill'])};color:{argb(c['chipText'])};border:1px solid {argb(c['chipStroke'])}"
        out.append(f'<div class="chip" style="{style}">{html.escape(lab)}</div>')
    return "".join(out)


def data_uri(path_or_img, fmt="PNG"):
    if isinstance(path_or_img, str):
        img = Image.open(path_or_img)
    else:
        img = path_or_img
    buf = io.BytesIO()
    img.save(buf, fmt)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def sharp_scale(frame, w, h):
    k = max(1, int(min(w / frame.width, h / frame.height)))
    big = frame.convert("RGB").resize((frame.width * k, frame.height * k), Image.NEAREST)
    return big.resize((w, h), Image.BILINEAR)


BASE_CSS = """
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: 'Inter', 'Noto Color Emoji', sans-serif; -webkit-font-smoothing: antialiased; }
.chip { display:inline-flex; align-items:center; justify-content:center; min-height:40px; padding:8px 14px;
        border-radius:20px; font-size:14px; margin:0 4px; line-height:1.15; white-space:nowrap; }
.dim { position:absolute; inset:0; background:rgba(0,0,0,0.6); }
.dialog { position:absolute; left:24px; right:24px; background:#424242; border-radius:2px;
          box-shadow:0 12px 32px rgba(0,0,0,.5); color:#fff; }
.dialog h2 { font-size:20px; font-weight:500; padding:24px 24px 0; }
.dialog .msg { font-size:16px; line-height:1.4; color:rgba(255,255,255,.70); padding:20px 24px 4px; white-space:pre-line; }
.dialog .btns { display:flex; justify-content:flex-end; align-items:center; padding:8px 8px 8px 8px; }
.dialog .btns span { color:#4F8CFF; font-size:14px; font-weight:600; letter-spacing:.04em; text-transform:uppercase;
                     padding:12px 12px; }
.dialog .btns .grow { flex:1 }
.row { display:flex; align-items:center; padding:6px 0; font-size:16px; color:#fff; }
.cb { width:18px; height:18px; border-radius:2px; border:2px solid rgba(255,255,255,.7); margin:0 16px 0 2px; flex:none;
      display:flex; align-items:center; justify-content:center; }
.cb.on { background:#4F8CFF; border-color:#4F8CFF; }
.cb.on::after { content:''; width:5px; height:10px; border:solid #212121; border-width:0 2.5px 2.5px 0; transform:translate(0,-1px) rotate(45deg); }
.row .x { margin-left:auto; padding:0 4px 0 16px; font-size:18px; color:rgba(255,255,255,.7); }
"""


def portrait_screen(frame_png, t, S, turbo=False, pressed=(), dialog=""):
    W, H = 360, 780
    inset = 28  # camera cutout: the app pads the root by the cutout's safe inset
    surface_h = round(W * 160 / 240)
    bar_h = 10 + 40 + 4
    pad_h = H - inset - surface_h - bar_h
    img = data_uri(sharp_scale(Image.open(frame_png), W * 3, surface_h * 3))
    labels = [S["game_turbo_on"].replace("%1$d", "4") if turbo else S["game_turbo"],
              S["game_save"], S["game_load"], S["game_menu"]]
    return f"""<html><head><style>{BASE_CSS}</style></head>
<body style="width:{W}px;height:{H}px;background:{argb(t['panel'])};position:relative;overflow:hidden">
<div style="height:{inset}px;background:#000"></div>
<img src="{img}" style="display:block;width:{W}px;height:{surface_h}px">
<div style="height:{bar_h}px;padding:10px 8px 4px;display:flex;justify-content:center">{chips(labels, 0 if turbo else -1, t, False)}</div>
<div style="position:relative;width:{W}px;height:{pad_h}px">{gamepad_svg(W, pad_h, t, False, pressed)}</div>
{dialog}
</body></html>"""


def landscape_screen(frame_png, t, S, pressed=()):
    W, H = 780, 360
    gw = round(H * 240 / 160)
    img = data_uri(sharp_scale(Image.open(frame_png), gw * 3, H * 3))
    labels = [S["game_turbo"], S["game_menu"]]
    return f"""<html><head><style>{BASE_CSS}</style></head>
<body style="width:{W}px;height:{H}px;background:#000;position:relative;overflow:hidden">
<img src="{img}" style="position:absolute;left:{(W-gw)//2}px;top:0;width:{gw}px;height:{H}px">
<div style="position:absolute;inset:0">{gamepad_svg(W, H, t, True, pressed)}</div>
<div style="position:absolute;top:8px;left:0;right:0;display:flex;justify-content:center">{chips(labels, -1, t, True)}</div>
</body></html>"""


def dialog_html(title, message, buttons, top, body_html=None):
    btns = "".join('<span class="grow"></span>' if b == "|" else f"<span>{html.escape(b)}</span>" for b in buttons)
    body = body_html if body_html is not None else f'<div class="msg">{html.escape(message)}</div>'
    return f'<div class="dim"></div><div class="dialog" style="top:{top}px"><h2>{html.escape(title)}</h2>{body}<div class="btns">{btns}</div></div>'


CHEAT_NAMES = {
    "values": ["Infinite lives", "Max weapon", "Start at stage 2"],
    "values-es": ["Vidas infinitas", "Arma al máximo", "Empezar en la etapa 2"],
    "values-pt-rBR": ["Vidas infinitas", "Arma no máximo", "Começar na fase 2"],
    "values-fr": ["Vies infinies", "Arme au maximum", "Commencer au niveau 2"],
    "values-de": ["Unendlich Leben", "Maximale Waffe", "Ab Level 2 starten"],
    "values-it": ["Vite infinite", "Arma al massimo", "Inizia dal livello 2"],
}

# ---- Captions -----------------------------------------------------------------------------
CAP = {
    "es": [("Abre tu juego y juega en segundos", "Funciona con .gba, .zip y .7z"),
           ("Turbo ×2, ×4 y ×8", "Pasa rápido por las partes lentas"),
           ("Trae tu partida de otro emulador", "Importa .sav y .srm sin perder avances"),
           ("Horizontal, con controles transparentes", "Imagen nítida en cualquier pantalla"),
           ("Trucos para cada juego", "Pega el código y actívalo con un toque"),
           ("Cometa viene incluido", "Un shooter original para probar en segundos")],
    "en": [("Open your game and play in seconds", "Works with .gba, .zip and .7z"),
           ("Turbo ×2, ×4 and ×8", "Speed through the slow parts"),
           ("Bring your save from another emulator", "Import .sav and .srm files, keep your progress"),
           ("Landscape, with see-through controls", "A sharp image on any screen"),
           ("Cheats for every game", "Paste a code and switch it on with a tap"),
           ("Cometa comes included", "An original shooter to try in seconds")],
    "pt": [("Abra seu jogo e jogue em segundos", "Funciona com .gba, .zip e .7z"),
           ("Turbo ×2, ×4 e ×8", "Passe rápido pelas partes lentas"),
           ("Traga seu save de outro emulador", "Importe .sav e .srm sem perder o progresso"),
           ("Na horizontal, com controles transparentes", "Imagem nítida em qualquer tela"),
           ("Cheats para cada jogo", "Cole o código e ative com um toque"),
           ("Cometa já vem incluso", "Um jogo de nave original para testar em segundos")],
    "fr": [("Ouvrez votre jeu et jouez en quelques secondes", "Compatible .gba, .zip et .7z"),
           ("Turbo ×2, ×4 et ×8", "Accélérez les passages lents"),
           ("Reprenez votre sauvegarde d'un autre émulateur", "Importez vos .sav et .srm sans rien perdre"),
           ("En paysage, avec des commandes transparentes", "Une image nette sur tous les écrans"),
           ("Des codes de triche pour chaque jeu", "Collez un code et activez-le d'un geste"),
           ("Cometa est inclus", "Un shoot'em up original à essayer en quelques secondes")],
    "de": [("Spiel öffnen und in Sekunden loslegen", "Funktioniert mit .gba, .zip und .7z"),
           ("Turbo ×2, ×4 und ×8", "Langsame Passagen im Schnelldurchlauf"),
           ("Spielstand aus anderem Emulator mitnehmen", ".sav und .srm importieren, Fortschritt behalten"),
           ("Querformat mit transparenter Steuerung", "Scharfes Bild auf jedem Display"),
           ("Cheats für jedes Spiel", "Code einfügen und mit einem Tipp aktivieren"),
           ("Cometa ist schon dabei", "Ein eigener Shooter zum sofortigen Ausprobieren")],
    "it": [("Apri il tuo gioco e gioca in pochi secondi", "Funziona con .gba, .zip e .7z"),
           ("Turbo ×2, ×4 e ×8", "Supera in fretta i momenti lenti"),
           ("Porta il tuo salvataggio da un altro emulatore", "Importa .sav e .srm senza perdere i progressi"),
           ("In orizzontale, con comandi trasparenti", "Immagine nitida su qualsiasi schermo"),
           ("Trucchi per ogni gioco", "Incolla il codice e attivalo con un tocco"),
           ("Cometa è incluso", "Uno sparatutto originale da provare in pochi secondi")],
}
TAGLINE = {"es": "Tus juegos de GBA, en segundos", "en": "Your GBA games, in seconds",
           "pt": "Seus jogos de GBA, em segundos", "fr": "Vos jeux GBA, en quelques secondes",
           "de": "Deine GBA-Spiele, in Sekunden", "it": "I tuoi giochi GBA, in pochi secondi"}


def lang_of(locale):
    return locale.split("-")[0]


def french(v):
    for a, b in ((" :", " :"), (" ?", " ?"), (" !", " !"), ("« ", "« "), (" »", " »")):
        v = v.replace(a, b)
    return v


# ---- Marketing frame ---------------------------------------------------------------------------
STARS = None


def star_field():
    global STARS
    if STARS is None:
        import random
        rnd = random.Random(4)
        dots = []
        for _ in range(140):
            x, y = rnd.randrange(1080), rnd.randrange(1920)
            r = rnd.choice([1, 1, 1, 2, 2, 3])
            a = rnd.uniform(0.25, 0.8)
            dots.append(f'<rect x="{x}" y="{y}" width="{r}" height="{r}" fill="rgba(200,220,255,{a:.2f})"/>')
        STARS = '<svg width="1080" height="1920" style="position:absolute;inset:0">' + "".join(dots) + "</svg>"
    return STARS


FRAME_CSS = """
* { box-sizing:border-box; margin:0; padding:0 }
body { width:1080px; height:1920px; overflow:hidden; position:relative; font-family:'Inter Display','Inter','Noto Color Emoji',sans-serif;
       background: radial-gradient(ellipse 900px 700px at 80% 85%, rgba(79,140,255,.35), transparent 70%),
                   radial-gradient(ellipse 800px 600px at 10% 20%, rgba(177,62,83,.28), transparent 70%),
                   linear-gradient(170deg, #0d1026 0%, #141a3c 55%, #0b0d1f 100%); }
.cap { position:absolute; left:80px; right:80px; top:110px; text-align:center; color:#fff; }
.cap h1 { font-size:78px; line-height:1.08; font-weight:800; letter-spacing:-0.01em; text-wrap:balance; }
.cap p { margin-top:26px; font-size:40px; line-height:1.25; font-weight:500; color:#aebbe0; text-wrap:balance; }
.phone { position:absolute; background:#05060a; border-radius:70px; padding:18px;
         box-shadow: 0 0 0 3px #2b3150, 0 40px 120px rgba(0,0,0,.6), 0 0 160px rgba(79,140,255,.25); }
.phone img { display:block; border-radius:54px; }
"""


def frame_html(title, sub, screen_png, landscape=False):
    img = data_uri(screen_png)
    if landscape:
        sw, sh = 1000, round(1000 * 360 / 780)
        phone = f'<div class="phone" style="left:{(1080 - sw - 36)//2}px;top:960px"><img src="{img}" style="width:{sw}px;height:{sh}px"></div>'
        return f"""<html><head><style>{FRAME_CSS}</style></head><body>{star_field()}
<div class="cap" style="top:520px"><h1>{html.escape(title)}</h1><p>{html.escape(sub)}</p></div>{phone}</body></html>"""
    else:
        sh = 1300
        sw = round(sh * 360 / 780)
        phone = f'<div class="phone" style="left:{(1080 - sw - 36)//2}px;top:560px"><img src="{img}" style="width:{sw}px;height:{sh}px"></div>'
    return f"""<html><head><style>{FRAME_CSS}</style></head><body>{star_field()}
<div class="cap"><h1>{html.escape(title)}</h1><p>{html.escape(sub)}</p></div>{phone}</body></html>"""


def feature_html(tagline, icon_png, game_png):
    icon = data_uri(icon_png)
    game = data_uri(sharp_scale(Image.open(game_png), 720, 480))
    return f"""<html><head><style>
* {{ box-sizing:border-box; margin:0; padding:0 }}
body {{ width:1024px; height:500px; overflow:hidden; position:relative; font-family:'Inter Display','Inter',sans-serif;
       background: radial-gradient(ellipse 520px 380px at 78% 60%, rgba(79,140,255,.45), transparent 70%),
                   linear-gradient(120deg, #0d1026 0%, #18204a 60%, #0b0d1f 100%); }}
.left {{ position:absolute; left:64px; top:0; bottom:0; width:430px; display:flex; flex-direction:column; justify-content:center; color:#fff; }}
.brand {{ display:flex; align-items:center; gap:22px; }}
.brand img {{ width:112px; height:112px; border-radius:26px; box-shadow:0 10px 30px rgba(0,0,0,.4); }}
.brand h1 {{ font-size:60px; line-height:0.98; font-weight:800; letter-spacing:-0.02em; }}
.tag {{ margin-top:26px; font-size:36px; line-height:1.2; font-weight:600; color:#cdd7f5; text-wrap:balance; }}
.shot {{ position:absolute; right:-24px; top:84px; padding:14px; background:#05060a; border-radius:36px;
         box-shadow:0 0 0 3px #2b3150, 0 30px 80px rgba(0,0,0,.6); transform:rotate(-5deg); }}
.shot img {{ display:block; width:480px; height:320px; border-radius:22px; image-rendering:auto; }}
</style></head><body>{star_field()}
<div class="shot"><img src="{game}"></div>
<div class="left"><div class="brand"><img src="{icon}"><h1>Play<br>Again</h1></div><div class="tag">{html.escape(tagline)}</div></div>
</body></html>"""


def main():
    icon_png = os.path.join(HERE, "icon512.png")
    with sync_playwright() as pw:
        browser = pw.chromium.launch()
        dev = browser.new_page(viewport={"width": 360, "height": 780}, device_scale_factor=3)
        dev_land = browser.new_page(viewport={"width": 780, "height": 360}, device_scale_factor=3)
        mk = browser.new_page(viewport={"width": 1080, "height": 1920}, device_scale_factor=1)
        fg = browser.new_page(viewport={"width": 1024, "height": 500}, device_scale_factor=1)

        def shoot(page, content, path):
            page.set_content(content)
            page.wait_for_timeout(60)
            page.screenshot(path=path, type="png")
            return path

        done_folders = {}
        for locale, folder in [(l, f) for l, f in LOCALES.items() if not ONLY or l in ONLY]:
            S = load_strings(folder)
            lang = lang_of(locale)
            caps = CAP[lang]
            if lang == "fr":
                caps = [(french(a), french(b)) for a, b in caps]
            if folder not in done_folders:
                screens = []
                f = lambda n: os.path.join(FRAMES, n + ".png")
                screens.append(shoot(dev, portrait_screen(f("action"), CLASSIC, S, pressed=("A",)), f"{WORK}/{folder}_1.png"))
                screens.append(shoot(dev, portrait_screen(f("turbo"), CLASSIC, S, turbo=True, pressed=("A", "UP")), f"{WORK}/{folder}_2.png"))
                # The confirmation step: unlike the guide, it names no other emulator (store assets must not).
                msg = "\n\n".join([S["import_confirm_file"].replace("%1$s", "Cometa.sav"),
                                     S["import_confirm_body"].replace("%1$s", "Cometa")])
                dlg = dialog_html(S["import_confirm_title"], msg, [S["action_cancel"], S["import_button"]], 250)
                screens.append(shoot(dev, portrait_screen(f("import"), CLASSIC, S, dialog=dlg), f"{WORK}/{folder}_3.png"))
                screens.append(shoot(dev_land, landscape_screen(f("boss"), CLASSIC, S, pressed=("A",)), f"{WORK}/{folder}_4.png"))
                names = CHEAT_NAMES[folder]
                rows = "".join(f'<div class="row"><span class="cb{" on" if on else ""}"></span>{html.escape(n)}<span class="x">✕</span></div>'
                               for n, on in zip(names, (True, True, False)))
                dlg = dialog_html(S["cheats_title"], "", [S["cheats_add"], "|", S["cheats_done"]], 190,
                                  body_html=f'<div style="padding:16px 20px 8px">{rows}</div>')
                screens.append(shoot(dev, portrait_screen(f("cheats"), CLASSIC, S, dialog=dlg), f"{WORK}/{folder}_5.png"))
                screens.append(shoot(dev, portrait_screen(f("t146"), MIDNIGHT, S), f"{WORK}/{folder}_6.png"))
                done_folders[folder] = screens
            screens = done_folders[folder]
            outdir = os.path.join(OUT, locale, "images", "phoneScreenshots")
            os.makedirs(outdir, exist_ok=True)
            for i, (scr, (title, sub)) in enumerate(zip(screens, caps), start=1):
                content = frame_html(title, sub, scr, landscape=(i == 4))
                path = os.path.join(outdir, f"{i}.png")
                shoot(mk, content, path)
                Image.open(path).convert("RGB").save(path)  # no alpha
            fpath = os.path.join(OUT, locale, "images", "featureGraphic.png")
            shoot(fg, feature_html(french(TAGLINE[lang]) if lang == "fr" else TAGLINE[lang], icon_png,
                                   os.path.join(FRAMES, "boss.png")), fpath)
            Image.open(fpath).convert("RGB").save(fpath)
            Image.open(icon_png).save(os.path.join(OUT, locale, "images", "icon.png"))
            print(locale, "ok")
        browser.close()


if __name__ == "__main__":
    main()
