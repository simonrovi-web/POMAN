# Third-party software in POMAN

POMAN's own code, DSP, presets, drum sounds and bundled cabinet IRs are original to this project.
It builds on the following third-party software:

| Component | Use | Licence |
|---|---|---|
| [JUCE 8](https://juce.com) | framework, plugin wrappers, DSP building blocks | AGPLv3 or a commercial JUCE licence |
| Steinberg VST3 SDK (via JUCE) | VST3 plugin format | see the SDK's licence (bundled with JUCE) |
| Steinberg ASIO SDK headers (via JUCE) | ASIO audio in the standalone | Steinberg proprietary licence or GPLv3 |
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) v0.6.0 | loading and running `.nam` captures | MIT, © 2023 Steven Atkinson |
| [Eigen](https://eigen.tuxfamily.org) (via NeuralAmpModelerCore) | linear algebra for NAM | MPL-2.0 |
| [nlohmann/json](https://github.com/nlohmann/json) (via NeuralAmpModelerCore) | reading `.nam` files | MIT, © 2013-2025 Niels Lohmann |

Before distributing or selling POMAN, the JUCE, VST3 and ASIO terms above must be satisfied
(for example a commercial JUCE licence, or releasing the source under the AGPLv3).

NAM captures (`.nam` files) are made and licensed by their authors; POMAN does not include any.
