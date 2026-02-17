# 🧠 Telebrain - Telegram Desktop with AI Copilot

Telebrain is a fork of Telegram Desktop with an integrated AI Copilot that brings powerful AI assistance directly into your Telegram experience. Chat with AI in the context of your conversation to get contextually-aware responses. The Completions API endpoint is fully configurable, allowing you to connect to most cloud LLM providers (OpenRouter, OpenAI, Anthropic, DeepSeek) or run against a local model (via LM Studio or similar). This repository contains the complete source code and build instructions, based on the official [Telegram][telegram] messenger desktop client.

![aznavour](https://github.com/user-attachments/assets/cd143ba4-9f68-49b4-a5cc-22bc8249a710)

The source code is published under GPLv3 with OpenSSL exception, the license is available [here][license].

## Support for custom Completions API endpoint, API key, and model

![config](https://github.com/user-attachments/assets/4e165bba-bab1-4d1e-a8b1-f3d560bed7b8)

### Tested endpoints (Base URL) and models

- OpenRouter - default: `https://openrouter.ai/api/v1`, model `stepfun/step-3.5-flash:free` (default), `openrouter/aurora-alpha`, `minimax/minimax-m2.5`, [`openrouter/free`](https://openrouter.ai/openrouter/free) - requires API key
- OpenAI: `https://api.openai.com/v1`, models `gpt-5.2`, `gpt-4.1`, `gpt-4o` - requires API key
- Anthropic: `https://api.anthropic.com/v1`, models `claude-sonnet-4-20250514`, `claude-opus-4-1-20250805` - requires API key
- DeepSeek: `https://api.deepseek.com`, models `deepseek-chat`, `deepseek-reasoner` - requires API key
- LM Studio: `http://localhost:1234/v1`, model `google/gemma-3-12b`, `openai/gpt-oss-20b` (prints reasoning tokens without proper formatting) - doesn't require API key

## Third-party

* Qt 6 ([LGPL](http://doc.qt.io/qt-6/lgpl.html)) and Qt 5.15 ([LGPL](http://doc.qt.io/qt-5/lgpl.html)) slightly patched
* OpenSSL 3.2.1 ([Apache License 2.0](https://www.openssl.org/source/apache-license-2.0.txt))
* WebRTC ([New BSD License](https://github.com/desktop-app/tg_owt/blob/master/LICENSE))
* zlib ([zlib License](http://www.zlib.net/zlib_license.html))
* LZMA SDK 9.20 ([public domain](http://www.7-zip.org/sdk.html))
* liblzma ([public domain](http://tukaani.org/xz/))
* Google Breakpad ([License](https://chromium.googlesource.com/breakpad/breakpad/+/master/LICENSE))
* Google Crashpad ([Apache License 2.0](https://chromium.googlesource.com/crashpad/crashpad/+/master/LICENSE))
* GYP ([BSD License](https://github.com/bnoordhuis/gyp/blob/master/LICENSE))
* Ninja ([Apache License 2.0](https://github.com/ninja-build/ninja/blob/master/COPYING))
* OpenAL Soft ([LGPL](https://github.com/kcat/openal-soft/blob/master/COPYING))
* Opus codec ([BSD License](http://www.opus-codec.org/license/))
* FFmpeg ([LGPL](https://www.ffmpeg.org/legal.html))
* Guideline Support Library ([MIT License](https://github.com/Microsoft/GSL/blob/master/LICENSE))
* Range-v3 ([Boost License](https://github.com/ericniebler/range-v3/blob/master/LICENSE.txt))
* Open Sans font ([Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0.html))
* Vazirmatn font ([SIL Open Font License 1.1](https://github.com/rastikerdar/vazirmatn/blob/master/OFL.txt))
* Emoji alpha codes ([MIT License](https://github.com/emojione/emojione/blob/master/extras/alpha-codes/LICENSE.md))
* xxHash ([BSD License](https://github.com/Cyan4973/xxHash/blob/dev/LICENSE))
* QR Code generator ([MIT License](https://github.com/nayuki/QR-Code-generator#license))
* CMake ([New BSD License](https://github.com/Kitware/CMake/blob/master/Copyright.txt))
* Hunspell ([LGPL](https://github.com/hunspell/hunspell/blob/master/COPYING.LESSER))
* Ada ([Apache License 2.0](https://github.com/ada-url/ada/blob/main/LICENSE-APACHE))

## Build instructions

* Windows [(32-bit)][win32] [(64-bit)][win64]
* [macOS][mac]
* [GNU/Linux using Docker][linux]

[//]: # (LINKS)
[telegram]: https://telegram.org
[license]: LICENSE
[win32]: docs/building-win.md
[win64]: docs/building-win-x64.md
[mac]: docs/building-mac.md
[linux]: docs/building-linux.md
