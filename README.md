# POMAN — Guitar Amp Suite

Plugin de amplificador de guitarra original (VST3 + Standalone) hecho con JUCE 8.
Su concepto es el de un "rig completo" en un solo plugin, con DSP, nombre e interfaz propios.

## Cadena de señal

```
Input → [Afinador] → Gate → Comp → Submerge (octavador) → Drive (5 tipos) → POMAN Amplifier (4x)
      → Hull (cabina: modelo 4x12 o IR) → Graphic EQ → Harmonizer → Mod (chorus/flanger/tremolo) → Echolocation (delay ping-pong) → Trench (reverb) → Output
```

- **Gate**: puerta con histéresis y hold, pensada para palm mutes cerrados.
- **Submerge**: octava abajo con divisor flip-flop y octava arriba con rectificador de onda completa.
- **Comp**: compresor feed-forward con rodilla suave de 6 dB, sustain (umbral + ratio), ataque, nivel y mezcla en paralelo.
- **Drive** (5 tipos, 16x oversampling; Distortion y Fuzz con ADAA de primer orden): Mid Boost (joroba de medios, aprieta graves), Blues Drive (transparente con mezcla limpia),
  Distortion (recorte duro de op-amp), Fuzz (dos etapas, tono con medios recortados) y Treble Boost.
- **POMAN Amplifier** (11 modelos): Glass Clean, Modern Rhythm, Modern Lead, US Clean, Chime Class A, Brit Crunch,
  Hot Rod Lead, Hi-Gain Rect, Tight Djent, Lead 51 (alta ganancia estilo 5150, 5 etapas) y Cali Lead (lead californiano
  tipo Mark, 5 etapas, pensado para el EQ en V). NAM Capture ocupa siempre el índice 10 de la lista; los modelos nuevos van detrás. Cada modelo define etapas de preamp (2-4), filtros de acoplo,
  stack de tonos pasivo modelado del circuito (ecuación de Yeh y Smith, DAFx 2006, con valores Fender o Marshall según el
  modelo), voz de medios propia, sag y presencia. Las etapas no lineales van a 4x oversampling.
- **NAM Capture**: carga capturas de [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) (`.nam`, WaveNet/LSTM/A2)
  como amplificador. Se cargan y precalientan en el hilo de la interfaz y se intercambian con un try-lock; corren a su
  frecuencia de entrenamiento con remuestreo sinc (≈207 muestras de latencia a 44,1 kHz, 0 a 48 kHz) y se normalizan a
  -18 dB cuando publican su sonoridad. La latencia reportada es constante: el camino más corto se retrasa.
- **Cabinet**: dos IR (A/B) con mezcla de potencia constante y alineación de fase automática por correlación cruzada,
  low/high cut, o el modelo 4x12 integrado. Incluye 4 IR sintéticos generados por `Tools/IrGen.cpp`.

- **Harmonizer**: 2 voces panoramizadas. En modo *Diatonic* sigue el tono de la señal DI limpia (YIN)
  y calcula cada intervalo por grados de la escala elegida (8 escalas, 12 tonalidades); en *Chromatic* usa
  semitonos fijos. Transpone la señal ya procesada (post-cabina) con un pitch shifter de doble lectura cuya
  ventana es un múltiplo exacto del periodo de entrada, para que la armonía no se desafine.

## Estudio (standalone)

Página **STUDIO** con 4 pistas estéreo: grabación de la salida completa del rig mientras suenan las demás pistas,
cuenta atrás de un compás con pre-roll, mute/solo/volumen, metrónomo, sesiones (`session.xml`, se guardan solas y se
reabren con OPEN SESSION), compensación automática de latencia (entrada + salida del dispositivo + latencia
del plugin) y exportación de la mezcla. Las tomas se escriben en disco desde un hilo en segundo plano
(`ThreadedWriter`) como WAV de 24 bits en `Recordings/Session <fecha>/`; el hilo de audio no se bloquea nunca.
En el VST3 la página no aparece: ahí graba el DAW.

- **Graphic EQ**: 10 bandas de octava (31 Hz-16 kHz, ±12 dB, picos Q 1,41), low cut y high cut de 12 dB/oct
  y nivel. `GraphicEQ::designFilters()` es la única fuente de los filtros: la usan el audio y la curva de la
  interfaz, y una prueba verifica que la curva coincide con la respuesta medida (error < 0,2 dB).
- **Mod**: chorus (7-21 ms, seno, estéreo en cuadratura), flanger (0,3-4,3 ms, triángulo, realimentación
  limitada con tanh para que sea estable) y tremolo (forma seno-cuadrada y desfase estéreo hasta auto-pan).

Las tomas no se cargan en RAM: se reproducen desde disco con `BufferingAudioReader` (4 s de lectura anticipada en un
hilo propio), la forma de onda se calcula durante la grabación y se guarda como `.peaks`, y EXPORT MIX procesa por
bloques. Una prueba graba 10 minutos sin perder muestras y con +0,4 MB de memoria; si el disco no da abasto, las
muestras perdidas se cuentan y se avisa.

## Looper (standalone)

Página **LOOP**: grabar, cerrar, overdub, stop, undo/redo de la última capa y clear, hasta 60 s estéreo. El overdub
nunca copia el bucle entero en el hilo de audio: cada bloque de 1024 muestras lleva la generación que contiene y se
fusiona con la base cuando una nueva capa lo toca; deshacer solo descarta una generación (instantáneo).

## MIDI

Clic derecho en cualquier control → MIDI learn (CC). Acciones para footswitch: afinador, preset anterior/siguiente
y los botones del looper. Program Change n carga el preset n. El hilo de audio solo encola los mensajes; se aplican
en el hilo de la interfaz (100 Hz), así que ni los parámetros ni los presets se tocan desde el audio.

## Caja de ritmos (standalone)

Página **DRUMS**: 8 voces sintetizadas (sin samples) en dos kits —*Studio* (modelado acústico con
sala y compresión de bus) y *Electronic* (estilo 808)— y secuenciador de 16 pasos con acentos, swing y nivel por
instrumento. El tiempo se calcula solo a partir de la posición del transporte del estudio y el BPM, nunca de los
bloques de audio: suena idéntico con cualquier tamaño de buffer, tras un salto y en el render offline de EXPORT MIX
(lo verifica una prueba). Se mezcla después del grabador, así que no entra en las tomas de guitarra.

## Extras

- **Delay** sincronizable (figuras de 1/2 a 1/16, con puntillo y tresillo) al tempo del DAW o del estudio, con tap tempo.
- **CAL**: calibración de entrada que lleva el pico de la DI a -9 dBFS.
- **Presets**: 29 de fábrica más presets de usuario (`.poman`, XML). En el standalone se guardan en `Presets/` junto al exe; en el VST3, en `Documentos/POMAN/Presets`.
- **Afinador** YIN (31 Hz a 1,3 kHz) que silencia la salida mientras está abierto.
- **Medidores** de entrada y salida con indicador de saturación.
- **Audio**: ASIO y WASAPI de baja latencia; en el primer arranque elige la interfaz ASIO de hardware con el buffer más pequeño estable.

## Compilar (Windows)

```bash
git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git external/JUCE
git clone --depth 1 --branch v0.6.0 --recurse-submodules --shallow-submodules https://github.com/sdatkinson/NeuralAmpModelerCore.git external/NeuralAmpModelerCore
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel
```

Se genera:
- `build/POMAN_artefacts/Release/VST3/POMAN.vst3`: cópialo a `C:\Program Files\Common Files\VST3\`
- `build/POMAN_artefacts/Release/Standalone/POMAN.exe` (portable: guarda ajustes, presets e IRs junto al .exe)

## Pruebas

```bash
cmake --build build --config Release --target POMANTests POMANDspTests POMANRecorderTests
build/POMANTests_artefacts/Release/POMANTests.exe        # precisión del afinador
build/POMANDspTests_artefacts/Release/POMANDspTests.exe  # amps/drives, harmonizer, EQ, mod y caja de ritmos
build/POMANDrumAnalysis_artefacts/Release/POMANDrumAnalysis.exe previews/drums  # mide cada voz y genera WAV
build/POMANBench_artefacts/Release/POMANBench.exe            # CPU y aliasing por módulo
build/POMANNamTests_artefacts/Release/POMANNamTests.exe external/NeuralAmpModelerCore/example_models  # NAM
python Tests/check_presets.py                                 # IDs de los presets de fábrica
build/POMANMidiTests_artefacts/Release/POMANMidiTests.exe    # MIDI learn, footswitch, PC, ráfagas
build/POMANRecorderTests_artefacts/Release/POMANRecorderTests.exe  # grabación, latencia, mute/solo, overdub, export, metrónomo, cuenta atrás, sesiones
```

Licencias de terceros: ver [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Estructura

- `Source/dsp/`: un bloque DSP por archivo (sin dependencias de UI)
- `Source/PluginProcessor.*`: parámetros, estado (incluye la ruta del IR) y cadena
- `Source/PluginEditor.*`: LookAndFeel, interfaz redimensionable, medidores y afinador
- `Source/PresetManager.*`, `Source/Tuner.h`, `Source/StandaloneApp.cpp` (app portable)
