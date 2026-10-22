# POMAN — Guitar Amp Suite

*Free guitar amp suite (VST3 + standalone) for Windows: modelled tube amps, pedals, cabinets, studio, drums, looper
and a practice player. Interface in Spanish and English.*

**Descargas:** en [Releases](../../releases) están `POMAN-Portable-win64.zip` (sin instalación) y `POMAN-win64.zip`
(la app y el plugin VST3). Dentro, `LEEME.txt` explica cómo empezar.

Plugin de amplificador de guitarra original (VST3 + Standalone) hecho con JUCE 8.
Su concepto es el de un "rig completo" en un solo plugin, con DSP, nombre e interfaz propios.

## Cadena de señal

```
Input → [Afinador] → Gate (detección) → Comp → Wah → Octavador → Drive (5 tipos) → POMAN Amplifier (4x) → Gate (corte)
      → Cabina (modelo 4x12 o IR) → Graphic EQ → Volumen (pedal) → Harmonizer → Mod (chorus/flanger/tremolo) → Delay ping-pong → Reverb → Output
```

- **Gate**: detecta en la DI limpia (con filtro paso alto para que el zumbido de red no la mantenga abierta) y atenúa
  después del amplificador, alineada con su latencia y abriendo 1 ms antes, así que elimina también el ruido del amplificador
  sin comerse el ataque. Un detector de parada (envolvente rápida frente a lenta) cierra en ~45 ms cuando se apagan las cuerdas
  y deja decaer las notas largas (~190 ms).
- **Wah**: filtro de estado variable TPT, barrido logarítmico 400 Hz-2,2 kHz, resonancia, modo Auto (envolvente). La posición
  se suaviza (sin zipper con un pedal de expresión MIDI a 100 Hz); con PEDAL ON el pedal enciende el wah y el talón lo apaga.
  **Volumen**: pedal de volumen con curva de audio antes de los efectos de tiempo.
- **Submerge**: octava abajo con divisor flip-flop y octava arriba con rectificador de onda completa.
- **Comp**: compresor feed-forward con rodilla suave de 6 dB, sustain (umbral + ratio), ataque, nivel y mezcla en paralelo.
- **Drive** (8 tipos, 16x oversampling; los recortes fuertes con ADAA de primer orden): Mid Boost (joroba de medios, aprieta graves), Blues Drive (transparente con mezcla limpia),
  Distortion (recorte duro de op-amp), Fuzz (dos etapas, tono con medios recortados), Treble Boost, Transparent OD (limpio + germanio
  asimétrico con foco en 1 kHz y control de agudos activo), Mosfet Drive (recorte MOSFET asimétrico) y Octave Fuzz (rectificador
  de onda completa con ADAA antes del fuzz, con paso bajo entre etapas).
- **Whammy**: transpositor de doble lectura con ventana sincronizada a periodos enteros de la nota (YIN, como el
  armonizador), así que no desafina (±1 cent medido); de 0 a ±12/24 semitonos según la posición del pedal de expresión,
  paso directo en el talón. **Detune** (tipo de MOD): ±15 cents estéreo con cruce trapezoidal, exacto entre empalmes.
- **POMAN Amplifier** (17 modelos): Glass Clean, Modern Rhythm, Modern Lead, US Clean, Chime Class A, Brit Crunch,
  Hot Rod Lead, Hi-Gain Rect, Tight Djent, Lead 51 (alta ganancia estilo 5150, 5 etapas) y Cali Lead (lead californiano
  tipo Mark, 5 etapas, pensado para el EQ en V) y Liquid Lead (lead virtuoso: 4 etapas, medios vocales a 1,2 kHz,
  agudos suaves), y los clásicos Tweed Deluxe (6V6, clase A sin realimentación), Plexi 50 (EL34), Bassman 4x10 (6L6),
  Smooth Overdrive (boutique, 6L6) y Brit 80s Lead (EL34). NAM Capture ocupa siempre el índice 10 de la lista; los modelos nuevos van detrás. Cada modelo define etapas de preamp (2-4), filtros de acoplo,
  stack de tonos pasivo modelado del circuito (ecuación de Yeh y Smith, DAFx 2006, con valores Fender o Marshall según el
  modelo), voz de medios propia y sag. Las válvulas siguen las ecuaciones de Koren (`Source/dsp/Tubes.h`): cada etapa
  de preamplificador es un 12AX7 con carga de 100 kΩ a 300 V, punto de trabajo propio de cada modelo y corriente de
  rejilla, resuelto una vez en una tabla e interpolado; las etapas invierten la fase. La potencia es un par push-pull de
  EL34, 6L6, EL84 o 6V6 (clase AB cerca del corte o clase A), normalizado a pendiente máxima 1 para que el lazo sea estable. La etapa de potencia tiene realimentación negativa global con PRESENCE y DEPTH dentro
  del lazo (cantidad propia de cada modelo; el Chime no tiene), interacción con el altavoz cuando la realimentación es baja
  y una ligera asimetría push-pull, y un transformador de salida cuyo núcleo satura antes los graves; el preamplificador
  simula la conducción de rejilla en los ataques fuertes. Las etapas no lineales van a 4x oversampling.
- **NAM Capture**: carga capturas de [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) (`.nam`, WaveNet/LSTM/A2)
  como amplificador. Se cargan y precalientan en el hilo de la interfaz y se intercambian con un try-lock; corren a su
  frecuencia de entrenamiento con remuestreo sinc (≈207 muestras de latencia a 44,1 kHz, 0 a 48 kHz) y se normalizan a
  -18 dB cuando publican su sonoridad. La latencia reportada es constante: el camino más corto se retrasa.
- **Cabinet**: dos IR (A/B) con mezcla de potencia constante y alineación de fase automática por correlación cruzada,
  low/high cut, o el modelo 4x12 integrado. `Tools/IrGen.cpp` genera 20 IR sintéticas propias: 4 cajas (altavoz con
  resonancia y rizado de rotura del cono, caja cerrada o abierta) × 5 tomas (dinámico en centro/borde/cono, cinta en el
  borde, condensador a 60 cm con reflexión del suelo). Los botones < > recorren las IR de la carpeta.
- **Reverb**: red de retardos realimentada (FDN) de 8 líneas con matriz de Hadamard, difusión de entrada, pre-delay,
  amortiguación de agudos por línea y modulación lenta; tipos Room (con reflexiones tempranas), Plate y Hall, y TONE.
  Al apagar delay o reverb (o cambiar a un preset sin ellos) sus colas se apagan solas.

- **Harmonizer**: 2 voces panoramizadas. En modo *Diatonic* sigue el tono de la señal DI limpia (YIN)
  y calcula cada intervalo por grados de la escala elegida (9 escalas con el modo lidio, 12 tonalidades); en *Chromatic* usa
  semitonos fijos. Transpone la señal ya procesada (post-cabina) con un pitch shifter de doble lectura cuya
  ventana es un múltiplo exacto del periodo de entrada, para que la armonía no se desafine.

## Estudio (standalone)

Página **STUDIO** con 4 pistas estéreo: grabación de la salida completa del rig mientras suenan las demás pistas,
cuenta atrás de un compás con pre-roll, mute/solo/volumen, metrónomo, sesiones (`session.xml`, se guardan solas y se
reabren con OPEN SESSION), compensación automática de latencia (entrada + salida del dispositivo + latencia
del plugin) y exportación de la mezcla. Cada toma guarda además la DI limpia (`Track N DI.wav`, alineada con la toma);
**REAMP** la reproduce por el rig actual desde el inicio de la toma y graba el resultado como toma nueva compensando
solo la latencia del plugin, y se detiene sola. Las tomas se escriben en disco desde un hilo en segundo plano
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

Página **LOOP**: grabar, cerrar, overdub, stop, undo/redo de la última capa y clear, hasta 60 s estéreo. Con **SYNC**
los loops se redondean a compases 4/4 enteros al tempo del estudio (cerrar tarde recorta manteniendo la fase; cerrar
pronto sigue grabando hasta la barra) y, con el transporte en marcha, la grabación empieza en el siguiente tiempo y un
loop parado vuelve a entrar en su propia rejilla. El overdub
nunca copia el bucle entero en el hilo de audio: cada bloque de 1024 muestras lleva la generación que contiene y se
fusiona con la base cuando una nueva capa lo toca; deshacer solo descarta una generación (instantáneo).

## MIDI

Clic derecho en cualquier control → MIDI learn (CC). Acciones para footswitch: afinador, preset anterior/siguiente
y los botones del looper. Program Change n carga el preset n. El hilo de audio solo encola los mensajes; se aplican
en el hilo de la interfaz (100 Hz), así que ni los parámetros ni los presets se tocan desde el audio.

## Caja de ritmos (standalone)

Página **DRUMS**: 8 voces y secuenciador de 16 pasos con acentos, swing y nivel por instrumento. Kits sintetizados
*Studio*, *Rock*, *Metal* y *Vintage* (un mismo modelo acústico con afinación, amortiguación y ataque propios, sala y
compresión de bus) y *Electronic* (estilo 808), más **Samples**: una carpeta de WAV/AIFF del usuario asignada por nombre de
archivo (kick/bombo/BD, snare/caja/SD, hat/charles/HH, open/OHH, tom/floor/rack, crash/ride, clap). Con dos archivos de un
instrumento, el más fuerte suena en los acentos; lo que falta lo toca la voz de Studio. Las muestras se convierten a la
frecuencia del motor sin retraso y se intercambian con un try-lock (las voces del kit anterior se callan). POMAN no
incluye muestras de terceros. El tiempo se calcula solo a partir de la posición del transporte del estudio y el BPM, nunca de los
bloques de audio: suena idéntico con cualquier tamaño de buffer, tras un salto y en el render offline de EXPORT MIX
(lo verifica una prueba). Se mezcla después del grabador, así que no entra en las tomas de guitarra.

## JAM (standalone)

Reproductor de acompañamiento: WSOLA (tramas Hann de 2048 con búsqueda por correlación de ±384 muestras) para la velocidad
y remuestreo Lagrange para el tono, bucle A-B, MP3/FLAC/WAV/AIFF/OGG en memoria a 16 bits (hasta 15 min). Se mezcla
después del grabador, así que no entra en las tomas. ~0,8 % de un núcleo con velocidad y tono cambiados.

## Aspecto

Estilo de amplificador rockero tomado del logo: tolex negro, paneles de cuero con bisel de bronce y tornillos, mandos de
latón con arco ámbar, pilotos tipo joya y placas metálicas que se iluminan. Los colores están en `Source/Palette.h` y el
dibujo compartido de fondo, paneles y títulos en `Source/RockStyle.h`; el logo (`Resources/Branding/`) va embebido con
`juce_add_binary_data` y la insignia es también el icono de la aplicación.

## Directo e idioma (standalone)

Página **LIVE/DIRECTO**: preset en grande con < > grandes, pulsadores DRIVE/WAH/MOD/DELAY/REVERB/TAP/AFINADOR (MIDI learn
con clic derecho), rejilla de presets por páginas y afinador grande. La interfaz está en español o inglés (botón ES/EN;
la primera vez, el idioma de Windows): `Source/Localisation.h` tiene una sola tabla y traduce el árbol de componentes sin
guardar estado por componente.

## Extras

- **Delay** sincronizable (figuras de 1/2 a 1/16, con puntillo y tresillo) al tempo del DAW o del estudio, con tap tempo.
- **CAL**: calibración de entrada que lleva el pico de la DI a -9 dBFS.
- **IN**: entrada de la guitarra (IN 1, IN 2… o IN 1+2); las demás entradas de la interfaz se ignoran. La app acepta
  hasta 8 entradas (`InputSelect.h`); en un DAW el plugin es estéreo.
- **Presets**: 43 de fábrica más presets de usuario (`.poman`, XML). En el standalone se guardan en `Presets/` junto al exe; en el VST3, en `Documentos/POMAN/Presets`.
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
build/POMANAmpFeel_artefacts/Release/POMANAmpFeel.exe --check  # dinámica: limpieza al tocar suave, ataque, presence/depth, estabilidad
powershell -ExecutionPolicy Bypass -File Tools/run_tests.ps1   # todas las suites de una vez (lo usa la CI)
powershell -ExecutionPolicy Bypass -File Tools/pluginval.ps1   # valida el VST3 con pluginval (lo descarga la primera vez)
powershell -ExecutionPolicy Bypass -File Tools/sign.ps1 -Thumbprint <SHA1>  # firma exe y VST3 con tu certificado
build/POMANNamTests_artefacts/Release/POMANNamTests.exe external/NeuralAmpModelerCore/example_models  # NAM
python Tests/check_presets.py                                 # IDs de los presets de fábrica
build/POMANMidiTests_artefacts/Release/POMANMidiTests.exe    # MIDI learn, footswitch, PC, ráfagas
build/POMANRecorderTests_artefacts/Release/POMANRecorderTests.exe  # grabación, latencia, mute/solo, overdub, export, metrónomo, cuenta atrás, sesiones
```

`.github/workflows/build.yml` hace lo mismo en GitHub Actions (Windows, VS 2022): dependencias fijadas, compilación,
todas las pruebas, pluginval con rigor 5 y empaquetado como artefactos.

Licencias de terceros: ver [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Estructura

- `Source/dsp/`: un bloque DSP por archivo (sin dependencias de UI)
- `Source/PluginProcessor.*`: parámetros, estado (incluye la ruta del IR) y cadena
- `Source/PluginEditor.*`: LookAndFeel, interfaz redimensionable, medidores y afinador
- `Source/PresetManager.*`, `Source/Tuner.h`, `Source/StandaloneApp.cpp` (app portable)

## Licencia

POMAN es software libre bajo la **GNU Affero General Public License v3.0** (`LICENSE`): puedes usarlo, estudiarlo,
modificarlo y redistribuirlo, y quien distribuya una versión modificada debe publicar su código con la misma licencia.
Las licencias de las bibliotecas de terceros están en `THIRD_PARTY_NOTICES.md`.
