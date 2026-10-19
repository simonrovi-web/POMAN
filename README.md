# POMAN — Guitar Amp Suite

Plugin de amplificador de guitarra original (VST3 + Standalone) hecho con JUCE 8.
Su concepto es el de un "rig completo" en un solo plugin, con DSP, nombre e interfaz propios.

## Cadena de señal

```
Input → [Afinador] → Gate → Submerge (octavador) → Drive (5 tipos) → POMAN Amplifier (4x oversampling)
      → Hull (cabina: modelo 4x12 o IR) → Harmonizer → Echolocation (delay ping-pong) → Trench (reverb) → Output
```

- **Gate**: puerta con histéresis y hold, pensada para palm mutes cerrados.
- **Submerge**: octava abajo con divisor flip-flop y octava arriba con rectificador de onda completa.
- **Drive** (5 tipos): Mid Boost (joroba de medios, aprieta graves), Blues Drive (transparente con mezcla limpia),
  Distortion (recorte duro de op-amp), Fuzz (dos etapas, tono con medios recortados) y Treble Boost.
- **POMAN Amplifier** (9 modelos): Glass Clean, Modern Rhythm, Modern Lead, US Clean, Chime Class A, Brit Crunch,
  Hot Rod Lead, Hi-Gain Rect y Tight Djent. Cada modelo define etapas de preamp (2-4), filtros de acoplo,
  stack de tonos propio (frecuencias y voz de medios), sag y presencia. Las etapas no lineales van a 4x oversampling.
- **Hull**: modelo de cabina por filtros, con un control de posición del micrófono, o carga de un IR propio en `.wav`/`.aif`.

- **Harmonizer**: 2 voces panoramizadas. En modo *Diatonic* sigue el tono de la señal DI limpia (YIN)
  y calcula cada intervalo por grados de la escala elegida (8 escalas, 12 tonalidades); en *Chromatic* usa
  semitonos fijos. Transpone la señal ya procesada (post-cabina) con un pitch shifter de doble lectura cuya
  ventana es un múltiplo exacto del periodo de entrada, para que la armonía no se desafine.

## Extras

- **Presets**: 18 de fábrica más presets de usuario (`.poman`, XML). En el standalone se guardan en `Presets/` junto al exe; en el VST3, en `Documentos/POMAN/Presets`.
- **Afinador** YIN (31 Hz a 1,3 kHz) que silencia la salida mientras está abierto.
- **Medidores** de entrada y salida con indicador de saturación.
- **Audio**: ASIO y WASAPI de baja latencia; en el primer arranque elige la interfaz ASIO de hardware con el buffer más pequeño estable.

## Compilar (Windows)

```bash
git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git external/JUCE
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel
```

Se genera:
- `build/POMAN_artefacts/Release/VST3/POMAN.vst3`: cópialo a `C:\Program Files\Common Files\VST3\`
- `build/POMAN_artefacts/Release/Standalone/POMAN.exe` (portable: guarda ajustes, presets e IRs junto al .exe)

## Pruebas

```bash
cmake --build build --config Release --target POMANTests POMANDspTests
build/POMANTests_artefacts/Release/POMANTests.exe        # precisión del afinador
build/POMANDspTests_artefacts/Release/POMANDspTests.exe  # render de amps/drives, intervalos y afinación del harmonizer
```

## Estructura

- `Source/dsp/`: un bloque DSP por archivo (sin dependencias de UI)
- `Source/PluginProcessor.*`: parámetros, estado (incluye la ruta del IR) y cadena
- `Source/PluginEditor.*`: LookAndFeel, interfaz redimensionable, medidores y afinador
- `Source/PresetManager.*`, `Source/Tuner.h`, `Source/StandaloneApp.cpp` (app portable)
