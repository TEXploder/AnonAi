# AnonAI for Notepad++

AnonAI is a native Notepad++ plugin that sends selected text or document context to an AI provider and writes the answer back into the editor.

Created by **texploder / asashin.com**.

## Features

- OpenAI via the Responses API.
- Claude via the Anthropic Messages API.
- DeepSeek via its OpenAI-compatible Chat Completions API.
- Custom OpenAI-compatible endpoint for local or hosted providers.
- Per-provider API key, base URL, and model fields. Model IDs are free text, so new provider models work without a plugin update.
- API keys are stored with Windows DPAPI for the current Windows user.
- Instant answer mode for selected text.
- Prompt mode for adding an extra instruction in a separate window.
- Secret typeout mode: select/write a question, start the mode, then every printable key reveals the next AI output character.
- Secret typeout auto-wraps with real line breaks when the current line reaches the visible editor width.
- Configurable background chat history. Set `Background messages` to the number of user/assistant messages that should stay persistent across requests. `0` disables it.
- About dialog with creator credits.

## Shortcuts

- `Ctrl+Alt+Shift+K`: Settings
- `Ctrl+Alt+Shift+A`: Instant answer from selection
- `Ctrl+Alt+Shift+P`: Ask with extra prompt
- `Ctrl+Alt+Shift+H`: Secret typeout from selection
- `Ctrl+Alt+Shift+X`: Stop current request or secret typeout

Notepad++ lets users remap plugin shortcuts under `Settings > Shortcut Mapper > Plugin commands`.

Secret typeout uses `.` as the default cover character while the answer is still loading. Change it in AnonAI settings if you want another single-character cover.
Secret typeout line breaks are based on the current caret position, existing text on the line, Notepad++ font/zoom, and the current editor window width.

Background chat history is stored locally in Notepad++'s plugin config folder as `AnonAI.history.jsonl`. Use `Plugins > AnonAI > Clear background chat history` or set `Background messages` to `0` and save settings to remove/disable it.

## Build

```powershell
cmake -S . -B build -G Ninja
cmake --build build
```

The DLL is written to:

```text
build\bin\AnonAI.dll
```

## Manual Install

Create this folder under your Notepad++ installation:

```text
%ProgramFiles%\Notepad++\plugins\AnonAI
```

Copy `build\bin\AnonAI.dll` into that folder, then restart Notepad++.

For portable Notepad++, use the `plugins\AnonAI` folder inside the portable Notepad++ directory.

If Notepad++ is installed in `C:\Program Files\Notepad++`, close Notepad++ and run:

```powershell
.\scripts\Install-AnonAI.ps1
```

## Provider Defaults

The plugin ships with editable defaults:

- OpenAI: `https://api.openai.com/v1`, model `gpt-5`
- Claude: `https://api.anthropic.com`, model `claude-sonnet-4-6`
- DeepSeek: `https://api.deepseek.com`, model `deepseek-v4-flash`
- Custom OpenAI-compatible: `https://api.example.com/v1`, model `model-name`

Change the model field to any model ID your provider account supports.

## Credits And License

Created by **texploder / asashin.com**.

You may use, modify, and redistribute AnonAI under the included MIT License.
Keep the original credits and license notices in copies, forks, and modified versions.

This project is not affiliated with Notepad++, OpenAI, Anthropic, Claude, or DeepSeek.
