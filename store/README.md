# Ficha de Google Play

Todo lo que se carga en Play Console para **Play Again**, en 7 idiomas: en-US, es-419, es-ES, pt-BR, fr-FR, de-DE, it-IT.

Títulos (máx. 30): la marca más «GBA Emulator» en cada idioma, que es lo que la gente busca, y «retro» donde entra:

| Idioma | Título |
|---|---|
| en-US | Play Again: GBA Retro Emulator |
| es-419 / es-ES | Play Again: Emulador GBA retro |
| pt-BR | Play Again: Emulador GBA retrô |
| fr-FR | Play Again : Émulateur GBA |
| de-DE | Play Again: Retro-GBA-Emulator |
| it-IT | Play Again: Emulatore GBA |

applicationId: `com.nestordelrio.playagain`. Política de privacidad: ver «Política de privacidad» más abajo.

## Dónde está cada cosa

```
fastlane/metadata/android/<idioma>/
    title.txt                  título (máx. 30)
    short_description.txt      descripción breve (máx. 80)
    full_description.txt       descripción completa (máx. 4000)
    changelogs/1.txt           novedades de la versión 1
    images/icon.png            ícono 512×512
    images/featureGraphic.png  gráfico destacado 1024×500
    images/phoneScreenshots/   6 capturas 1080×1920, en orden
```

Es el formato de *fastlane supply*: se puede subir todo con `fastlane supply` o copiar a mano en Play Console → Presencia en la tienda → Ficha principal (y «Administrar traducciones» para cada idioma). Idioma predeterminado sugerido: **es-419**.

Las capturas muestran la interfaz real de la app (mismos textos y medidas) con cuadros reales de Cometa, el juego incluido. No muestran nombres de otras apps ni marcas de terceros. Para regenerarlas: `python3 store/tools/render_screenshots.py` (necesita Playwright, Pillow y las fuentes Inter y Noto Color Emoji).

## Productos de compra (Monetizar → Productos → Productos únicos)

IDs exactos: no se pueden cambiar después. Precio base sugerido en USD; Play calcula el resto (revisá ARS y BRL a mano).

Cómo crear cada uno (Monetize with Play → Products → One-time products → Create one-time product):
1. Product ID, nombre y descripción de la tabla de abajo (el ícono es opcional). Impuestos y cumplimiento: dejar lo que viene.
2. Next → opción de compra: ID `buy`, tipo **Buy** (Comprar). Sin «Quantities and limits»: se compra de a uno. La primera opción Buy queda como compatible con versiones anteriores; la app lee esa misma.
3. Set prices → Bulk edit pricing → todas las regiones → precio en USD → Apply. Revisar ARS y BRL.
4. Activate. Después, en el producto, Manage translations para los otros idiomas.
Para probar compras hay que instalar la app desde la pista de prueba interna con una cuenta de License testing (Setup → License testing): el build debug tiene otro applicationId (`.debug`) y Play no le encuentra los productos.

| ID | Precio | Tipo |
|---|---|---|
| `supporter_pack` | 2,99 | se compra una vez |
| `tip_small` | 0,99 | la app lo consume (se puede repetir) |
| `tip_medium` | 2,99 | la app lo consume |
| `tip_large` | 4,99 | la app lo consume |

### Nombre y descripción por idioma

**supporter_pack**

| Idioma | Nombre | Descripción |
|---|---|---|
| es-419 / es-ES | Pack Colaborador | 6 temas para los controles, 2 íconos para la app e insignia de colaborador. Solo decoración: no desbloquea funciones. |
| en-US | Supporter Pack | 6 control themes, 2 app icons and a supporter badge. Decoration only: it doesn't unlock features. |
| pt-BR | Pacote Apoiador | 6 temas para os controles, 2 ícones para o app e selo de apoiador. Só decoração: não libera funções. |
| fr-FR | Pack Soutien | 6 thèmes pour les commandes, 2 icônes pour l'app et un badge de soutien. Purement décoratif : ne débloque aucune fonction. |
| de-DE | Unterstützer-Paket | 6 Designs für die Steuerung, 2 App-Icons und ein Unterstützer-Abzeichen. Reine Deko: schaltet keine Funktionen frei. |
| it-IT | Pacchetto Sostenitore | 6 temi per i comandi, 2 icone per l'app e un badge da sostenitore. Solo decorazione: non sblocca funzioni. |

**Propinas** (misma descripción para las tres)

| Idioma | tip_small | tip_medium | tip_large | Descripción |
|---|---|---|---|---|
| es-419 / es-ES | Propina: un café | Propina: una pizza | Propina grande | Una propina para el proyecto. No desbloquea nada. |
| en-US | Tip: a coffee | Tip: a pizza | Big tip | A tip for the project. It doesn't unlock anything. |
| pt-BR | Gorjeta: um café | Gorjeta: uma pizza | Gorjeta grande | Uma gorjeta para o projeto. Não libera nada. |
| fr-FR | Pourboire : un café | Pourboire : une pizza | Gros pourboire | Un pourboire pour le projet. Ne débloque rien. |
| de-DE | Trinkgeld: ein Kaffee | Trinkgeld: eine Pizza | Großes Trinkgeld | Ein Trinkgeld für das Projekt. Schaltet nichts frei. |
| it-IT | Mancia: un caffè | Mancia: una pizza | Mancia grande | Una mancia per il progetto. Non sblocca nulla. |

## Política de privacidad

https://claude.ai/artifact/G2R7ujvKJjMBHq5M2yNJRD (español e inglés, vigente desde el 6/10/2026). Va en Play Console → Contenido de la app → Política de privacidad, y en la ficha. Antes de cargarla hay que compartirla como «cualquiera con el link» desde el menú Compartir de la página: Play rechaza un link que pida iniciar sesión. Si la app cambia lo que guarda o empieza a usar Internet, anuncios o analíticas, hay que actualizarla (y el formulario de Seguridad de los datos).

## Formularios de Play Console

Respuestas para Política → Contenido de la app y Presencia en la tienda. Salen de lo que hace el código hoy: si la app cambia (Internet, anuncios, analíticas, cuentas), hay que revisarlas.

**Política de privacidad**: el link de arriba.

**Acceso a la app**: «Todas las funciones están disponibles sin acceso especial». No hay cuentas; el Pack Colaborador es una compra, no un acceso restringido.

**Anuncios**: «No, mi app no contiene anuncios».

**Clasificación de contenido** (cuestionario IARC):
- Categoría: Juego (la app trae Cometa y va en Juegos).
- Violencia: **sí**. Cometa es un shooter espacial: se dispara a naves, asteroides y un jefe; sin personas ni animales, sin sangre, gráficos pixelados de fantasía. Contestar las preguntas de detalle con eso.
- Miedo, sexualidad o desnudez, lenguaje, drogas/alcohol/tabaco, apuestas o azar simulado, humor grosero: no.
- Interacción entre usuarios, chat o contenido de otros usuarios: no. Comparte la ubicación: no. Navegador o acceso libre a Internet: no.
- Compras digitales: **sí** (Pack Colaborador y propinas).
- La clasificación cubre lo que trae la app (Cometa), no los juegos que abre cada persona desde sus archivos. Lo esperable es la clasificación más baja o la siguiente por los disparos (My Boy!, que no trae juego, tiene PEGI 3).

**Público objetivo y contenido**: solo «18 años o más». A «¿Podría atraer a niños sin querer?»: no (la ficha habla de volver a jugar juegos de GBA, para adultos). Elegir menores de 13 activa la política de Familias.

**Seguridad de los datos**: «¿Tu app recopila o comparte alguno de los tipos de datos del usuario obligatorios?» → **No**. Con eso el formulario termina. Por qué:
- La app no tiene servidores, cuentas, anuncios ni analíticas: nada sale del teléfono hacia el desarrollador ni hacia terceros.
- Juegos, partidas, trucos y preferencias se procesan en el teléfono («procesamiento en el dispositivo» no se declara). La carpeta de partidas es una carpeta del propio teléfono.
- La copia de seguridad de Android la hace el sistema, no la app.
- Pagos: la ayuda de Play dice que no hace falta declarar lo que recopila el sistema de facturación de Google Play para procesar pagos si la app no accede a esos datos y Google los pide directo al usuario. Es el caso.
- Ojo: la biblioteca de pagos de Google trae una dependencia de registros de Google (`datatransport`). Google no publica una guía de Seguridad de los datos para esa biblioteca (el SDK Index no muestra ninguna), así que no se declara; si Google la publica, se ajusta el formulario y la política.

**ID de publicidad**: «No» (la app no lo usa). Antes de subir el AAB, revisar en Android Studio el manifiesto combinado (AndroidManifest.xml → pestaña Merged Manifest): no tiene que aparecer `com.google.android.gms.permission.AD_ID` ni permisos de ubicación. Los de red que agregan las bibliotecas de Google son esperables.

**Apps gubernamentales**: no. **Funciones financieras**: «Mi app no ofrece funciones financieras». **Salud**: ninguna. **Noticias**: no.

**Configuración de la tienda**:
- Tipo: Juego. Categoría: **Arcade**. Es donde compiten los emuladores de GBA: My Boy! (#4 en pagos de Arcade), Pizza Boy A, John GBA y Nostalgia.GBA están ahí; Lemuroid está en Entretenimiento y RetroArch en Acción (revisado en Play el 6/10/2026).
- Etiquetas (máx. 5): Grow users → Store presence → Store settings → App category → **Manage tags** (aparece después de elegir tipo y categoría). Play no publica la lista; con la consola en inglés se llaman **Offline**, **Single player** y **Pixelated** (los mismos que muestran en la ficha Minecraft, Dead Cells o Retro Bowl). «Retro» no existe como etiqueta (Retro Bowl no la tiene). No poner géneros como «Shoot 'em up»: describen a Cometa, no a la app, y Play pide etiquetas obvias.
- Las apps gratis en la categoría Juegos aparecen también en Google Play Games para PC; se puede desactivar en Play Console si no se quiere.
- Correo de contacto: nestordelrio1989@gmail.com (sale público en la ficha). Sitio web: opcional.

## Reglas que respetan estos textos

- Sin «Nintendo», «Game Boy», «Pokémon», nombres de juegos comerciales ni de otras apps (en la ficha y en las capturas). «GBA» aparece solo como descripción del formato.
- Sin «gratis», «sin anuncios» ni rankings en el título ni en el ícono (Play lo prohíbe ahí), y tampoco en las capturas ni en el gráfico destacado, por las dudas. Sí están en las descripciones, donde están permitidos.
- La descripción aclara que no hay juegos comerciales ni BIOS, y que se abren archivos que el usuario tenga legalmente.
- Si algún día se agregan anuncios, hay que sacar «sin anuncios» de las descripciones.
