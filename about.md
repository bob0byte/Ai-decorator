# AI Decorator

Build a structure, select it, and let AI decorate it.

## How to use
1. (Optional, only for typed prompts) paste a free Gemini API key into the <cy>mod settings</c> (aistudio.google.com/apikey).
2. In the level editor, build your gameplay structure and **select it**.
3. Press the <cy>AI</c> button next to the playtest buttons on the left.
4. Tap a style (Neon, Lava, Ruins, Ice, Nature, Minimal) to decorate instantly and offline, or type a description and press send to use Gemini.
5. Compare Before / After, then press **Apply**.

The decoration is placed as one undo step and left selected, so you can move it, recolor it, or undo it if you don't like it.

## Notes
- Each generation is a request to Google's Gemini API. The free tier has daily limits; if you hit one, wait or switch the model to gemini-3.5-flash-lite.
- Results vary. Try a more specific description, or add your favourite deco object IDs to the <cy>Object Palette</c> setting to steer the AI.
