# Abyssal — Deep-Pressure Guitar Rig

Plugin de amplificador de guitarra original (VST3 + Standalone) hecho con JUCE 8.
Su concepto es el de un "rig completo" en un solo plugin, con DSP, nombre e interfaz propios.

## Cadena de señal

```
Input → Gate → Submerge (octavador) → Riptide (overdrive) → Leviathan (amp, 4x oversampling)
      → Hull (cabina: modelo 4x12 o IR) → Echolocation (delay ping-pong) → Trench (reverb) → Output
```

- **Gate**: puerta con histéresis y hold, pensada para palm mutes cerrados.
- **Submerge**: octava abajo con divisor flip-flop y octava arriba con rectificador de onda completa.
- **Riptide**: boost con joroba de medios; satura solo la banda alta y aprieta los graves.
- **Leviathan**: tres canales (Clean: 2 etapas; Rhythm: 3; Lead: 4) con etapas tipo triodo asimétricas,
  stack de tonos (Bass/Mid/Treble), etapa de potencia con *sag* y Presence/Depth.
- **Hull**: modelo de cabina por filtros, con un control de posición del micrófono, o carga de un IR propio en `.wav`/`.aif`.

## Compilar (Windows)

```bash
git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git external/JUCE
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel
```

Se genera:
- `build/AbyssalAmp_artefacts/Release/VST3/Abyssal.vst3`: cópialo a `C:\Program Files\Common Files\VST3\`
- `build/AbyssalAmp_artefacts/Release/Standalone/Abyssal.exe`

## Estructura

- `Source/dsp/`: un bloque DSP por archivo (sin dependencias de UI)
- `Source/PluginProcessor.*`: parámetros, estado (incluye la ruta del IR) y cadena
- `Source/PluginEditor.*`: LookAndFeel e interfaz redimensionable
