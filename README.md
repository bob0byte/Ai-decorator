# AI Decorator

A [Geode](https://geode-sdk.org) mod for Geometry Dash 2.2081. Build a structure in the editor, select it, press the **AI** button, describe how you want it to look, and Google Gemini (free tier) generates decoration objects around it.

## How it works

1. `src/main.cpp` hooks `EditorUI::init` and adds a square **AI** button next to the music playtest button.
2. Pressing it reads the current selection (object IDs, positions, rotation, scale) and opens `DecoratorPopup`.
3. The popup sends the selection plus your description to the Gemini generateContent API (`src/Decorator.cpp` builds the prompt).
4. Gemini answers with a JSON list of objects (`id`, `x`, `y`, `rotation`, `scale`, flips, z layer/order, color channels).
5. The mod converts that into a GD object string, creates the objects with `LevelEditorLayer::createObjectsFromString`, adds a single undo step, and selects the result.

## Settings

| Setting | What it does |
|---|---|
| Gemini API Key | Required. Free key from aistudio.google.com/apikey, no card needed. |
| Model | Gemini model ID (default `gemini-3.8-flash`; `gemini-3.5-flash-lite` has higher free limits). |
| Max Objects | Cap on objects per generation (default 150). |
| Object Palette | Optional `id:description` list of objects the AI should prefer. |

## Building

Pushing to GitHub builds the mod for Windows, macOS, iOS and Android via the workflow in `.github/workflows/build.yml`. Download the `.geode` file from the run's **Build Output** artifact and drop it into your Geode mods folder.

To build locally, install the [Geode CLI and SDK](https://docs.geode-sdk.org/getting-started/), then:

```sh
geode build
```

## Known limitations / ideas

- The AI only knows object IDs from its training, so it's most reliable with IDs that are in your selection or your palette. Filling the palette with your favourite deco objects makes a big difference.
- Ideas for later: a preview before placing, a "regenerate" button, using a screenshot of the selection as input, and per-level style memory.
