# Third-party software in POMAN

POMAN is free software, released under the GNU Affero General Public License v3.0 (see `LICENSE`).
Its own code, DSP, presets, drum sounds, cabinet IRs and logo are original to this project.
It builds on the following third-party software:

| Component | Use | Licence |
|---|---|---|
| [JUCE 8](https://juce.com) | framework, plugin wrappers, DSP building blocks | AGPLv3 or a commercial JUCE licence |
| Steinberg VST3 SDK (via JUCE) | VST3 plugin format | used under its GPLv3 option |
| Steinberg ASIO SDK headers (via JUCE) | ASIO audio in the standalone | used under its GPLv3 option |
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) v0.6.0 | loading and running `.nam` captures | MIT, © 2023 Steven Atkinson |
| [Eigen](https://eigen.tuxfamily.org) (via NeuralAmpModelerCore) | linear algebra for NAM | MPL-2.0 |
| [nlohmann/json](https://github.com/nlohmann/json) (via NeuralAmpModelerCore) | reading `.nam` files | MIT, © 2013-2025 Niels Lohmann |

POMAN uses JUCE under the AGPLv3 and the Steinberg SDKs under the GPLv3, whose terms allow combining
them with AGPLv3 code. Anyone distributing POMAN or a modified version must provide its source under
the AGPLv3 too.

NAM captures (`.nam` files) are made and licensed by their authors; POMAN does not include any.
