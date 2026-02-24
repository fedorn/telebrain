import httpx
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from config import OPENROUTER_BASE_URL, OPENROUTER_API_KEY, OPENROUTER_MODEL
from prompts import build_system_message

app = FastAPI(title="Telebrain AI", version="0.1.0")


class ChatMessage(BaseModel):
    role: str
    content: str


class ChatRequest(BaseModel):
    messages: list[ChatMessage]
    chat_context: str = ""
    user_name: str = ""
    user_username: str = ""


class ChatResponse(BaseModel):
    content: str


class ErrorResponse(BaseModel):
    error: str


@app.post("/v1/chat", response_model=ChatResponse)
async def chat(request: ChatRequest) -> ChatResponse:
    system_content = build_system_message(
        chat_context=request.chat_context,
        user_name=request.user_name,
        user_username=request.user_username,
    )
    openrouter_messages = [
        {"role": "system", "content": system_content},
        *[{"role": m.role, "content": m.content} for m in request.messages],
    ]
    payload = {
        "model": OPENROUTER_MODEL,
        "messages": openrouter_messages,
        "store": True,
    }
    headers = {
        "Content-Type": "application/json",
        "HTTP-Referer": "https://github.com/fedorn/telebrain",
        "X-Title": "Telebrain",
    }
    if OPENROUTER_API_KEY:
        headers["Authorization"] = f"Bearer {OPENROUTER_API_KEY}"
    async with httpx.AsyncClient(timeout=60.0) as client:
        response = await client.post(
            f"{OPENROUTER_BASE_URL.rstrip('/')}/chat/completions",
            json=payload,
            headers=headers,
        )
    if response.status_code != 200:
        try:
            err = response.json()
            msg = err.get("error", {}).get("message", response.text)
        except Exception:
            msg = response.text
        raise HTTPException(status_code=502, detail=msg)
    data = response.json()
    if "error" in data:
        raise HTTPException(
            status_code=502,
            detail=data["error"].get("message", "Unknown API error"),
        )
    choices = data.get("choices", [])
    if not choices:
        raise HTTPException(status_code=502, detail="No response from API")
    content = (choices[0].get("message") or {}).get("content", "")
    if not content:
        raise HTTPException(status_code=502, detail="Empty response from API")
    return ChatResponse(content=content)


@app.get("/health")
async def health() -> dict:
    return {"status": "ok"}
