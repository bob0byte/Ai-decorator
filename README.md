# AI Decorator

A [Geode](https://geode-sdk.org) mod for Geometry Dash 2.2081. Build a structure in the editor, select it, press the **AI** button, describe how you want it to look, and Claude generates decoration objects around it.

## How it works

1. `src/main.cpp` hooks `EditorUI::init` and adds an **AI** button to the editor's button menu (`editor-buttons-menu`).
2. Pressing it reads the current selection (object IDs, positions, rotation, scale) and opens `DecoratorPopup`.
3. The popup sends the selection plus your description to the Claude Messages API (`src/Decorator.cpp` builds the prompt).
4. Claude answers with a JSON list of objects (`id`, `x`, `y`, `rotation`, `scale`, flips, z layer/order, color channels).
5. The mod converts that into a GD object string, creates the objects with `LevelEditorLayer::createObjectsFromString`, adds a single undo step, and selects the result.

## Settings

| Setting | What it does |
|---|---|
| Anthropic API Key | Required. Your key from console.anthropic.com. |
| Model | Claude model ID (default `claude-sonnet-5-5`). |
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
