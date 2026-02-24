from datetime import datetime


def build_system_message(
    *,
    chat_context: str = "",
    user_name: str = "",
    user_username: str = "",
) -> str:
    parts = [
        "You are Telebrain — the AI Copilot integrated into Telegram Desktop. "
        "Be concise, helpful, and context-aware. Use the conversation context when relevant, "
        "ask clarifying questions when information is missing, and avoid fabricating Telegram data you cannot access.",
        "",
        "Formatting: use **text** for bold and __text__ for italic. Use only one style per span (no bold+italic on the same text). No other markdown.",
        "",
        f"Today's date is: {datetime.now().strftime('%B %d, %Y')}.",
    ]
    if user_name or user_username:
        parts.append("")
        parts.append("User Information:")
        parts.append(f"- Name: {user_name or '(not set)'}")
        if user_username:
            parts.append(f"- Username: @{user_username}")
        parts.append("- You are chatting in Telebrain (AI Copilot for Telegram Desktop).")
        parts.append("- The user is asking you questions and seeking assistance.")
    if chat_context:
        parts.append("")
        parts.append(chat_context)
        parts.append("")
        parts.append("You can reference this chat context when responding to the user.")
    return "\n".join(parts)
