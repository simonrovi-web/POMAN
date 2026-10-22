#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PortablePaths.h"
#include <atomic>
#include <map>

// Interface language (English / Spanish). The UI is written in English; tr() maps a string to the
// current language through one table, and retranslate() walks a component tree and updates labels,
// buttons and tooltips. It keeps no per-component state: the shown text is mapped back to its English
// key with the reverse table, so switching repeatedly, or code updating a text, never mixes languages.
// Model, drive and effect type names stay as they are in both languages.
namespace poman::i18n
{
enum class Language { english, spanish };

namespace detail
{
// English -> Spanish (UTF-8). Fragments of composed texts are listed as they appear in the code.
inline const std::vector<std::pair<const char*, const char*>>& table()
{
    static const std::vector<std::pair<const char*, const char*>> entries {
        // Header, pages, presets
        { "INPUT", "ENTRADA" }, { "OUTPUT", "SALIDA" }, { "RIG", "RIG" }, { "STUDIO", "ESTUDIO" }, { "DRUMS", "BATER\xc3\x8d" "A" },
        { "LIVE", "DIRECTO" }, { "SAVE", "GUARDAR" }, { "TUNER", "AFINADOR" }, { "CAL", "CAL" },
        { "Preset", "Preset" }, { "FACTORY", "F\xc3\x81" "BRICA" }, { "USER", "USUARIO" }, { "GUITAR AMP SUITE", "SUITE DE AMPLIFICADORES" },
        { "Save preset", "Guardar preset" }, { "Name for the new preset:", "Nombre del nuevo preset:" }, { "Save", "Guardar" },
        { "Cancel", "Cancelar" }, { "Could not save to ", "No se pudo guardar en " },
        { "Language / Idioma", "Idioma / Language" },
        { "Input calibration: press, then play hard for 5 seconds", "Calibrar entrada: pulsa y toca fuerte durante 5 segundos" },
        { "Guitar input: the interface input the guitar is plugged into (the other inputs are ignored)",
          "Entrada de guitarra: la entrada de la interfaz donde est\xc3\xa1 conectada la guitarra (las dem\xc3\xa1s se ignoran)" },
        { "NO SIG", "SIN SE\xc3\x91" "AL" },

        // Sections and knobs
        { "GATE", "PUERTA" }, { "THRESHOLD", "UMBRAL" }, { "COMP", "COMP" }, { "SUSTAIN", "SUSTAIN" }, { "ATTACK", "ATAQUE" },
        { "LEVEL", "NIVEL" }, { "BLEND", "MEZCLA" }, { "OCTAVE", "OCTAVADOR" }, { "SUB", "SUB" }, { "UP", "ARRIBA" }, { "DRY", "SECO" },
        { "WAH", "WAH" }, { "POSITION", "POSICI\xc3\x93N" }, { "RESO", "RESO" }, { "VOLUME", "VOLUMEN" }, { "PEDAL ON", "PEDAL ON" },
        { "DRIVE", "DRIVE" }, { "TONE", "TONO" }, { "GRAPHIC EQ", "EQ GR\xc3\x81" "FICO" }, { "LOW CUT", "CORTE BAJO" },
        { "HIGH CUT", "CORTE ALTO" }, { "AMPLIFIER", "AMPLIFICADOR" }, { "GAIN", "GANANCIA" }, { "BASS", "GRAVES" }, { "MID", "MEDIOS" },
        { "TREBLE", "AGUDOS" }, { "PRESENCE", "PRESENCIA" }, { "DEPTH", "PROFUND." }, { "MASTER", "MASTER" }, { "CABINET", "CAJA" },
        { "MIC POS", "POS. MICRO" }, { "A/B MIX", "MEZCLA A/B" }, { "TIME", "TIEMPO" }, { "DELAY", "DELAY" }, { "FEEDBACK", "REALIM." },
        { "MIX", "MEZCLA" }, { "REVERB", "REVERB" }, { "SIZE", "TAMA\xc3\x91O" }, { "VOICE 1", "VOZ 1" }, { "VOICE 2", "VOZ 2" },
        { "HARMONIZER", "ARMONIZADOR" }, { "V1 LEVEL", "NIVEL V1" }, { "V2 LEVEL", "NIVEL V2" }, { "FDBK", "REALIM." }, { "MOD", "MOD" },
        { "RATE", "VELOC." }, { "SHAPE", "FORMA" }, { "STEREO", "EST\xc3\x89REO" }, { "TAP", "TAP" }, { "BYPASSED", "DESACTIVADO" },
        { "LOAD NAM", "CARGAR NAM" }, { "IR A", "IR A" }, { "IR B", "IR B" }, { "BUILT-IN", "INTEGRADA" },

        // Tooltips and messages (RIG)
        { "Right-click POSITION to MIDI-learn an expression pedal. With PEDAL ON, moving the pedal "
          "switches the wah on, and resting at the heel for half a second switches it off",
          "Clic derecho en POSICI\xc3\x93N para asignar un pedal de expresi\xc3\xb3n por MIDI. Con PEDAL ON, al mover el pedal "
          "el wah se enciende, y al dejarlo en el tal\xc3\xb3n medio segundo se apaga" },
        { "Sync the delay to the tempo (DAW tempo in a plugin, studio/drums tempo standalone)",
          "Sincroniza el delay con el tempo (el del DAW en el plugin, el del estudio/bater\xc3\xad" "a en la app)" },
        { "Tap the tempo (shared with STUDIO and DRUMS)", "Marca el tempo con pulsaciones (compartido con ESTUDIO y BATER\xc3\x8d" "A)" },
        { "Load an impulse response into slot A", "Carga una respuesta al impulso en la ranura A" },
        { "Load a second IR into slot B (blended with A/B MIX, phase-aligned automatically)",
          "Carga una segunda IR en la ranura B (se mezcla con MEZCLA A/B y se alinea en fase autom\xc3\xa1ticamente)" },
        { "Remove both IRs and use the built-in 4x12 model", "Quita las dos IR y usa la caja 4x12 integrada" },
        { "Load a Neural Amp Modeler capture (.nam) and select NAM Capture", "Carga una captura de Neural Amp Modeler (.nam) y selecciona NAM Capture" },
        { "Previous IR in the folder (slot A)", "IR anterior de la carpeta (ranura A)" },
        { "Next IR in the folder (slot A)", "IR siguiente de la carpeta (ranura A)" },
        { "Load a NAM capture", "Cargar una captura NAM" }, { "Load a cabinet impulse response", "Cargar una respuesta al impulso de caja" },
        { "NAM capture", "Captura NAM" }, { "Impulse response", "Respuesta al impulso" },
        { "Built-in 4x12 model", "Caja 4x12 integrada" }, { " (missing)", " (no encontrado)" }, { "   (aligned ", "   (alineadas " },
        { " smp)", " muestras)" }, { " kHz model, +", " kHz el modelo, +" }, { " ms: set your interface to ", " ms: pon tu interfaz a " },
        { "No capture loaded: press LOAD NAM (Glass Clean plays meanwhile)", "Sin captura: pulsa CARGAR NAM (mientras suena Glass Clean)" },
        { "Play a string - output muted", "Toca una cuerda - salida silenciada" }, { "Play a note...", "Toca una nota..." },
        { "Unison", "Un\xc3\xadsono" }, { "Oct", "Oct" }, { "2 Oct", "2 Oct" },

        // MIDI
        { "Cancel MIDI learn", "Cancelar aprendizaje MIDI" },
        { "MIDI learn: move a knob, pedal or footswitch", "Aprender MIDI: mueve un mando, pedal o pulsador" },
        { "Forget CC ", "Olvidar CC " }, { "Forget all MIDI mappings", "Olvidar todas las asignaciones MIDI" },
        { "Program Change n loads preset n (always on)", "Program Change n carga el preset n (siempre activo)" },
        { "LEARN...", "APRENDE..." }, { "Right-click for MIDI learn", "Clic derecho para aprender MIDI" },
        { " (right-click for MIDI)", " (clic derecho para MIDI)" }, { "Waiting for a MIDI controller...", "Esperando un controlador MIDI..." },

        // STUDIO
        { "COUNT-IN", "CUENTA" }, { "RECORDING", "GRABANDO" }, { "PLAY", "PLAY" }, { "STOP", "STOP" }, { "REC", "REC" },
        { "CLICK", "CLICK" }, { "NEW SESSION", "NUEVA SESI\xc3\x93N" }, { "OPEN SESSION", "ABRIR SESI\xc3\x93N" },
        { "EXPORT MIX", "EXPORTAR MEZCLA" }, { "OPEN FOLDER", "ABRIR CARPETA" }, { "BPM", "BPM" },
        { "One bar of click (with the other tracks playing) before recording starts",
          "Un comp\xc3\xa1s de click (con las otras pistas sonando) antes de empezar a grabar" },
        { "Warning: the disk was too slow, ", "Aviso: el disco fue demasiado lento, " },
        { " ms of the take are missing. Record to a faster drive.", " ms de la toma se han perdido. Graba en un disco m\xc3\xa1s r\xc3\xa1pido." },
        { "Take saved in ", "Toma guardada en " }, { "Recording track ", "Grabando pista " },
        { "Open a POMAN session folder", "Abrir una carpeta de sesi\xc3\xb3n de POMAN" }, { "Opened ", "Abierta " },
        { "New session", "Nueva sesi\xc3\xb3n" },
        { "Clear all tracks and start a new session?\nRecorded WAV files stay on disk.",
          "\xc2\xbf" "Vaciar todas las pistas y empezar una sesi\xc3\xb3n nueva?\nLos WAV grabados se quedan en el disco." },
        { "New session: ", "Nueva sesi\xc3\xb3n: " }, { "Exported ", "Exportado " },
        { "Arm a track (R), press REC to record, STOP to keep the take.", "Arma una pista (R), pulsa REC para grabar y STOP para guardar la toma." },
        { "TRACK ", "PISTA " }, { "Arm for recording", "Armar para grabar" }, { "Mute", "Silenciar" }, { "Solo", "Solo" },
        { "Clear this track (the WAV file stays on disk)", "Vaciar esta pista (el WAV se queda en el disco)" },
        { "COUNT-IN  ", "CUENTA  " }, { "empty", "vac\xc3\xad" "a" }, { "REAMP", "REAMP" },
        { "Re-amp: play this take's clean DI through the current rig and record the result as the new take",
          "Re-amplificar: pasa la DI limpia de esta toma por el rig actual y graba el resultado como toma nueva" },
        { "Re-amping track ", "Re-amplificando pista " }, { "Re-amp done: ", "Re-amplificado listo: " },

        // DRUMS
        { "I want drums", "Quiero bater\xc3\xad" "a" }, { "Custom", "Personalizado" }, { "Room ambience (Studio kit)", "Ambiente de sala (kit Studio)" },
        { "SWING", "SWING" }, { "ROOM", "SALA" }, { "KIT", "KIT" }, { "DRUMS ON", "BATER\xc3\x8d" "A ON" }, { "CLEAR", "BORRAR" },
        { "LOAD KIT", "CARGAR KIT" }, { "BUILT-IN KIT", "KIT INTEGRADO" },
        { "Click: hit on/off   Right-click: accent   Drag: paint.   Drums play with the studio transport "
          "and are not recorded into your tracks; EXPORT MIX includes them.",
          "Clic: golpe s\xc3\xad/no   Clic derecho: acento   Arrastrar: pintar.   La bater\xc3\xad" "a suena con el transporte del estudio "
          "y no se graba en tus pistas; EXPORTAR MEZCLA la incluye." },
        { "Load a folder of WAV samples as the drum kit (kick, snare, hats, toms, cymbals named in the file names)",
          "Carga una carpeta de muestras WAV como kit (bombo, caja, charles, toms y platos seg\xc3\xban el nombre del archivo)" },
        { "Back to the selected built-in kit", "Volver al kit integrado seleccionado" },
        { "Drum kit", "Kit de bater\xc3\xad" "a" }, { "Choose a folder with drum samples (WAV)", "Elige una carpeta con muestras de bater\xc3\xad" "a (WAV)" },

        // LOOP
        { "EMPTY", "VAC\xc3\x8d" "O" }, { "PLAYING", "SONANDO" }, { "OVERDUB", "OVERDUB" }, { "STOPPED", "PARADO" }, { "WAITING", "ESPERANDO" },
        { "DUB", "DUB" }, { "CANCEL", "CANCELAR" }, { "REDO", "REHACER" }, { "UNDO", "DESHACER" }, { "SYNC", "SYNC" },
        { "LOOP LEVEL", "NIVEL LOOP" }, { "REC/PLAY/DUB", "REC/PLAY/DUB" }, { " = CC ", " = CC " },
        { "No MIDI footswitch assigned", "Ning\xc3\xban pulsador MIDI asignado" }, { " bar)", " comp\xc3\xa1s)" }, { " bars)", " compases)" },
        { "Tempo sync: loops are whole 4/4 bars at the studio/drums tempo; while the drums run, "
          "recording starts on the next beat and the loop stays locked to them",
          "Sincron\xc3\xad" "a: los loops son compases 4/4 enteros al tempo del estudio/bater\xc3\xad" "a; con la bater\xc3\xad" "a en marcha, "
          "la grabaci\xc3\xb3n empieza en el siguiente tiempo y el loop queda enganchado a ella" },
        { "REC starts the loop, PLAY closes it, DUB layers on top. UNDO removes the last layer (press again to bring it back). "
          "Up to 60 s. SYNC locks loops to the drums' tempo in whole bars. Loops play into STUDIO recordings. "
          "Right-click a button to assign a MIDI footswitch.",
          "REC empieza el loop, PLAY lo cierra, DUB a\xc3\xb1" "ade capas. DESHACER quita la \xc3\xbaltima capa (pulsa otra vez para recuperarla). "
          "Hasta 60 s. SYNC ajusta los loops al tempo de la bater\xc3\xad" "a en compases enteros. Los loops suenan en las grabaciones de ESTUDIO. "
          "Clic derecho en un bot\xc3\xb3n para asignar un pulsador MIDI." },

        // LIVE
        { "WHAMMY", "WHAMMY" }, { "DETUNE", "DESAFINAR" },
        { "Right-click POSITION to MIDI-learn an expression pedal. With PEDAL ON, moving the pedal "
          "switches the whammy on, and resting at the heel for half a second switches it off",
          "Clic derecho en POSICI\xc3\x93N para asignar un pedal de expresi\xc3\xb3n por MIDI. Con PEDAL ON, al mover el pedal "
          "el whammy se enciende, y al dejarlo en el tal\xc3\xb3n medio segundo se apaga" },
        { "JAM", "JAM" }, { "LOAD SONG", "CARGAR CANCI\xc3\x93N" }, { "PAUSE", "PAUSA" }, { "SPEED", "VELOCIDAD" },
        { "PITCH", "TONO" }, { "LOOP", "BUCLE" }, { "No song loaded", "Ninguna canci\xc3\xb3n cargada" },
        { "Choose a song", "Elige una canci\xc3\xb3n" }, { "Backing track", "Pista de acompa\xc3\xb1" "amiento" },
        { "Set the loop start here", "Marca aqu\xc3\xad el inicio del bucle" },
        { "Set the loop end here and loop", "Marca aqu\xc3\xad el final del bucle y repite" },
        { "Repeat the A-B section", "Repite el tramo A-B" },
        { "Load a song (WAV, MP3, FLAC, AIFF, OGG). SPEED slows it down without changing the pitch; PITCH changes "
          "the key without changing the speed. Click the waveform to jump; A and B mark a section to loop. "
          "The song is heard but never recorded into your takes.",
          "Carga una canci\xc3\xb3n (WAV, MP3, FLAC, AIFF, OGG). VELOCIDAD la ralentiza sin cambiar el tono; TONO cambia "
          "la tonalidad sin cambiar la velocidad. Haz clic en la onda para saltar; A y B marcan un tramo para repetir. "
          "La canci\xc3\xb3n se oye pero nunca se graba en tus tomas." },
        { "PAGE", "P\xc3\x81GINA" }, { "LOOPER", "LOOPER" },
        { "Click a preset to load it. Right-click a switch, < or > to assign a MIDI footswitch.",
          "Haz clic en un preset para cargarlo. Clic derecho en un pulsador, < o > para asignarle un pulsador MIDI." },
    };
    return entries;
}

struct Maps
{
    std::map<juce::String, juce::String> toSpanish, toEnglish;
    Maps()
    {
        for (const auto& [en, es] : table())
        {
            const auto spanish = juce::String::fromUTF8 (es);
            toSpanish[en] = spanish;
            toEnglish[spanish] = en;
        }
    }
};

inline const Maps& maps()
{
    static const Maps m;
    return m;
}

inline std::atomic<int>& current()
{
    static std::atomic<int> language { -1 }; // -1: not loaded yet
    return language;
}

// Standalone: next to POMAN.exe (portable); plugin or read-only folder: the user's app data.
inline juce::File settingsFile()
{
    const auto portable = poman::portableRoot();
    if (juce::JUCEApplicationBase::isStandaloneApp() && portable.hasWriteAccess())
        return portable.getChildFile ("POMAN.language");
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("POMAN").getChildFile ("language.txt");
}
} // namespace detail

inline Language language()
{
    auto& c = detail::current();
    if (c.load() < 0)
    {
        const auto saved = detail::settingsFile().loadFileAsString().trim();
        const bool spanish = saved.isNotEmpty() ? saved == "es" : juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("es");
        c.store (spanish ? 1 : 0);
    }
    return c.load() == 1 ? Language::spanish : Language::english;
}

inline void setLanguage (Language l)
{
    detail::current().store (l == Language::spanish ? 1 : 0);
    const auto file = detail::settingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (l == Language::spanish ? "es" : "en");
}

inline juce::String tr (const juce::String& english)
{
    if (language() != Language::spanish)
        return english;
    const auto& m = detail::maps().toSpanish;
    const auto it = m.find (english);
    return it != m.end() ? it->second : english;
}

// English key of a shown text (the text itself when it is not a translation).
inline juce::String englishOf (const juce::String& shown)
{
    const auto& m = detail::maps().toEnglish;
    const auto it = m.find (shown);
    return it != m.end() ? it->second : shown;
}

// Updates every label, button and tooltip under root to the current language.
inline void retranslate (juce::Component& root)
{
    if (auto* label = dynamic_cast<juce::Label*> (&root))
        label->setText (tr (englishOf (label->getText())), juce::dontSendNotification);
    if (auto* button = dynamic_cast<juce::Button*> (&root))
        button->setButtonText (tr (englishOf (button->getButtonText())));
    if (auto* tip = dynamic_cast<juce::SettableTooltipClient*> (&root); tip != nullptr && tip->getTooltip().isNotEmpty())
        tip->setTooltip (tr (englishOf (tip->getTooltip())));
    for (auto* child : root.getChildren())
        retranslate (*child);
}
} // namespace poman::i18n
