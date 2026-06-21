# TunerAccess

**An accessible guitar & bass tuner for blind and low-vision musicians.**
**Un accordeur de guitare et basse accessible pour les musiciens aveugles et malvoyants.**

VST3 · Audio Unit · Standalone — built with JUCE 8.
NVDA on Windows · VoiceOver on macOS.

🌐 [reaperaccessible.com](https://reaperaccessible.com) (EN) · [reaperaccessible.fr](https://reaperaccessible.fr) (FR) · 📦 [Download / Télécharger](https://github.com/reaperaccessible/TunerAccess/releases/latest)

**Languages:** [English](#english) · [Français](#français)

---

## English

### What is TunerAccess?

TunerAccess is a chromatic instrument tuner designed **first** for screen-reader users. Most tuners — including popular pedals like the Boss TU-3 — are entirely visual: a note name, a needle, a green light. None of that helps a blind musician. TunerAccess delivers the exact same information — the detected note, the deviation in cents, the direction to turn, the target string — but through **speech, braille, and sound** instead of light.

It runs as a standalone application and as a VST3 / Audio Unit plugin, and it is fully operable from the keyboard.

### Features

- **Instruments & tunings** — 6- and 7-string guitar, 4-, 5- and 6-string bass, each with several tunings (Standard, Drop, Open, DADGAD, half/full step down…) plus a Free Chromatic mode.
- **Pluck-to-hear announcements** — the tuner stays silent while a string rings, then announces the reading **once** on the next pluck. No chatter, no need to fight a constant stream of speech.
- **In-tune lock tone** — press `T` for a steady 880 Hz tone that sounds while you are in tune (the audio equivalent of a strobe tuner's lock or a pedal's green light). Tune by ear until the tone holds.
- **YIN pitch detection** — accurate down to ~25 Hz, so 5- and 6-string basses (low B at 31 Hz) are fully covered.
- **Two named inputs** — one for your electric, one for a microphone, each with its own device channel and gain (−12 to +24 dB). Rename with `F2`. Switch instantly.
- **Fully accessible audio settings** — choose your sound card, output, sample rate and buffer (ASIO on Windows, CoreAudio on macOS). The inaccessible stock dialog is never shown.
- **Built-in manual** — press `F1` for the manual, `H` for spoken help.
- **Self-contained** — no dependency on other software at run time. Settings are saved automatically.

### Keyboard quick reference

| Key | Action |
|---|---|
| `Tab` / `Shift+Tab` | Move between Tuner, Input, and Audio Settings |
| `Left` / `Right` | Switch field (Instrument / Tuning / String) |
| `Up` / `Down` | Change the value of the current field |
| `Enter` | Start or stop the tuner |
| `T` | Toggle the in-tune lock tone |
| `F1` | Open the user manual |
| `F10` | Open the audio settings |
| `H` | Speak the help |

(On macOS laptops, `F1` / `F10` may require the `Fn` key.)

### Download & install

Grab the latest [release](https://github.com/reaperaccessible/TunerAccess/releases/latest):

- **Windows** — run `ReaperAccessible_TunerAccess_X.YY_Setup.exe` (bilingual installer; installs Standalone + VST3 + the NVDA client).
- **macOS** — download `TunerAccess-macOS.zip`, then follow `INSTALL-macOS.txt` inside it. The build is currently **unsigned**, so after copying the bundles you must run, for example:
  ```bash
  xattr -dr com.apple.quarantine /Applications/TunerAccess.app
  ```

### Building from source

JUCE 8 is fetched automatically — no manual setup needed.

**Windows** (Visual Studio 2022):
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

**macOS** (Xcode 14+):
```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Artefacts land in `build/TunerAccess_artefacts/Release/` (`Standalone`, `VST3`, and `AU` on macOS). The macOS porting notes are in [`MACOS_PORTING_GUIDE.md`](MACOS_PORTING_GUIDE.md).

### Credits & license

By **ReaperAccessible** — [reaperaccessible.com](https://reaperaccessible.com). Windows (NVDA) build by the project owner; macOS (VoiceOver) port by **math65**. See [`installer/data/EULA.txt`](installer/data/EULA.txt) for licensing.

---

## Français

### Qu'est-ce que TunerAccess ?

TunerAccess est un accordeur chromatique conçu **d'abord** pour les utilisateurs de lecteurs d'écran. La plupart des accordeurs — y compris les pédales populaires comme la Boss TU-3 — sont entièrement visuels : un nom de note, une aiguille, une LED verte. Rien de tout cela n'aide un musicien aveugle. TunerAccess fournit exactement la même information — la note détectée, l'écart en cents, le sens à tourner, la corde cible — mais par la **parole, le braille et le son** plutôt que par la lumière.

Il fonctionne en application autonome et en greffon VST3 / Audio Unit, et tout se pilote au clavier.

### Fonctionnalités

- **Instruments et accordages** — guitare 6 et 7 cordes, basse 4, 5 et 6 cordes, chacun avec plusieurs accordages (Standard, Drop, Open, DADGAD, demi-ton ou ton en dessous…) ainsi qu'un mode Chromatique libre.
- **Annonces « jouer pour entendre »** — l'accordeur reste silencieux pendant que la corde résonne, puis annonce la lecture **une seule fois** au coup de corde suivant. Pas de bavardage, pas de flux de parole continu à combattre.
- **Tonalité de verrouillage** — appuyez sur `T` pour une tonalité continue de 880 Hz qui sonne tant que vous êtes accordé (l'équivalent audio du verrouillage d'un accordeur à stroboscope ou de la LED verte d'une pédale). Accordez à l'oreille jusqu'à ce que la tonalité tienne.
- **Détection de hauteur YIN** — précise jusqu'à environ 25 Hz, donc les basses 5 et 6 cordes (si grave à 31 Hz) sont entièrement couvertes.
- **Deux entrées nommées** — une pour votre électrique, une pour un microphone, chacune avec son canal de périphérique et son gain (−12 à +24 dB). Renommez avec `F2`. Basculez instantanément.
- **Réglages audio entièrement accessibles** — choisissez votre carte son, la sortie, le taux d'échantillonnage et la mémoire tampon (ASIO sous Windows, CoreAudio sous macOS). La fenêtre standard inaccessible n'est jamais affichée.
- **Manuel intégré** — appuyez sur `F1` pour le manuel, `H` pour l'aide vocale.
- **Autonome** — aucune dépendance à un autre logiciel à l'exécution. Les réglages sont sauvegardés automatiquement.

### Aide-mémoire clavier

| Touche | Action |
|---|---|
| `Tab` / `Maj+Tab` | Se déplacer entre Accordeur, Entrée et Audio Settings |
| `Gauche` / `Droite` | Changer de champ (Instrument / Accordage / Corde) |
| `Haut` / `Bas` | Changer la valeur du champ courant |
| `Entrée` | Démarrer ou arrêter l'accordeur |
| `T` | Activer / désactiver la tonalité de verrouillage |
| `F1` | Ouvrir le manuel |
| `F10` | Ouvrir les réglages audio |
| `H` | Énoncer l'aide |

(Sur les portables macOS, `F1` / `F10` peuvent nécessiter la touche `Fn`.)

### Téléchargement et installation

Récupérez la dernière [version](https://github.com/reaperaccessible/TunerAccess/releases/latest) :

- **Windows** — lancez `ReaperAccessible_TunerAccess_X.YY_Setup.exe` (installeur bilingue ; installe l'autonome + le VST3 + le client NVDA).
- **macOS** — téléchargez `TunerAccess-macOS.zip`, puis suivez le `INSTALL-macOS.txt` à l'intérieur. Le build est actuellement **non signé** : après avoir copié les bundles, vous devez par exemple exécuter :
  ```bash
  xattr -dr com.apple.quarantine /Applications/TunerAccess.app
  ```

### Compiler depuis les sources

JUCE 8 est téléchargé automatiquement — aucune configuration manuelle nécessaire.

**Windows** (Visual Studio 2022) :
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

**macOS** (Xcode 14+) :
```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Les artefacts se trouvent dans `build/TunerAccess_artefacts/Release/` (`Standalone`, `VST3`, et `AU` sur macOS). Les notes de portage macOS sont dans [`MACOS_PORTING_GUIDE.md`](MACOS_PORTING_GUIDE.md).

### Crédits et licence

Par **ReaperAccessible** — [reaperaccessible.fr](https://reaperaccessible.fr). Build Windows (NVDA) par le propriétaire du projet ; portage macOS (VoiceOver) par **math65**. Voir [`installer/data/EULA.txt`](installer/data/EULA.txt) pour la licence.
