# Telebrain AI — Local Python API

A separate service that handles all AI logic (prompts, model config, tools). Use it so you can change the system prompt, tools, and provider settings without recompiling the C++ app.

## 1. What it is

Telebrain AI is an HTTP API that the Telebrain desktop app can call instead of talking to OpenRouter (or another provider) directly. The app sends chat messages and context; this service builds the system prompt, calls the LLM, and returns the reply. All prompt and model configuration lives here (`.env`, `prompts.py`, `main.py`), so you can iterate without rebuilding the C++ project.

## 2. How to run the local API

```bash
cd telebrain_ai
python -m venv .venv
source .venv/bin/activate   # Linux/macOS
# .venv\Scripts\activate   # Windows
pip install -r requirements.txt
cp .env.example .env
# Edit .env: set OPENROUTER_API_KEY (and OPENROUTER_MODEL / OPENROUTER_BASE_URL if needed)
uvicorn main:app --reload --host 127.0.0.1 --port 8000
```

With `--reload`, the server restarts when you change code (e.g. `prompts.py`, `main.py`).

## 3. Using the local API in the app

In Telebrain: open **Settings → Telebrain** and enable **“Use local AI backend (Python API)”**. The app will use `http://127.0.0.1:8000` by default for this mode.

## Editing without recompiling

- **System prompt** — `prompts.py`, function `build_system_message()`
- **Model and API** — `.env` or `config.py`
- **Tools** — add in `main.py` (e.g. OpenRouter tools) and wire from the prompt

## API

- **POST /v1/chat** — Request body: `{ "messages": [{"role","content"}], "chat_context": "", "user_name": "", "user_username": "" }`. Response: `{ "content": "..." }`.
- **GET /health** — Returns `{"status":"ok"}` when the service is up.
