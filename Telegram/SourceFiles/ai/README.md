# AI Chat Widget

This module provides AI chat functionality for Telebrain using OpenAI's Completions API-compatible LLM API.

## Features

- Real-time chat with OpenAI's GPT-4.1 model or any other Completions API-compatible model.
- Conversation history management with context awareness
- Error handling and user feedback
- Loading indicators ("🤔 Thinking..." state)
- Text selection and message selection support
- Integration with Telegram user and chat context

## Architecture

### Key Components

- **`AIChatWidget`**: Main widget class that handles the chat interface and UI
- **`MessagesWidget`**: Handles message display, text selection, and user interactions
- **`OpenAIClient`**: Dedicated class for OpenAI API interactions
- **`MessageData`**: Simple structure for storing chat messages

### Design Principles

- **Separation of Concerns**: UI logic is separated from API logic
- **Reusability**: The `OpenAIClient` can be used by other parts of the application
- **Error Handling**: Comprehensive error handling at multiple levels
- **Context Awareness**: Automatically includes user information and chat context

## Setup

### Building

The AI chat widget is automatically included in the Telegram Desktop build when you compile the project. The module includes:

- `ai_chat_widget.cpp` / `ai_chat_widget.h` - Main UI components
- `openai_client.cpp` / `openai_client.h` - OpenAI API integration
- `ai_constants.h` - constants used

## Usage

1. Launch Telegram Desktop
2. Navigate to the AI chat section
3. Type your message and press Enter
4. The AI will respond using LLM model with context from your current chat

## Implementation Details

### OpenAIClient Class

The `OpenAIClient` class handles all OpenAI's Completions API interactions:

```cpp
class OpenAIClient : public QObject {
    Q_OBJECT

public:
    void sendChatCompletion(
        const std::vector<MessageData> &messages,
        std::function<void(const QString &)> onSuccess,
        std::function<void(const QString &)> onError);
    
    bool isWaitingForResponse() const;
    void setSessionController(not_null<Window::SessionController*> controller);
};
```

**Key Features:**
- **Callback-based API**: Uses std::function for success/error callbacks
- **Context Integration**: Automatically includes user info and chat context
- **Error Handling**: Comprehensive error handling for network, API, and JSON issues
- **Rate Limiting**: Prevents multiple simultaneous requests

### API Integration

The widget uses OpenAI's Chat Completions API (`/v1/chat/completions`) with the following features:

- **Model**: By default GPT-4.1
- **Conversation History**: Last 100 messages to manage token limits
- **System Message**: Includes user information and chat context
- **Timeout**: 30-second request timeout

### Context Awareness

The AI assistant receives context about:

1. **User Information**: Name and username from Telegram
2. **Chat Context**: Last 30 messages from the current active chat
3. **Conversation History**: Previous AI chat messages

### Error Handling

The widget handles various error scenarios:

- Missing API key
- Network errors
- API errors (rate limits, invalid requests, etc.)
- JSON parsing errors
- Empty responses
- Timeout errors

### User Experience

- Shows "🤔 Thinking..." indicator while processing
- Replaces thinking indicator with actual response
- Displays error messages with ❌ icon
- Maintains conversation context
- Supports text selection and message selection
- Real-time cursor changes based on interaction mode

## Dependencies

- **Qt6::Network**: For HTTP requests to OpenAI API
- **Qt6::Core**: For JSON parsing and environment variables
- **Qt6::Widgets**: For UI components
- **RPL Library**: For reactive programming patterns
- **Telegram Core**: For session management and chat context

## Security Notes

- API key is read from environment variables only
- No API key is stored in the application
- Network requests use HTTPS
- Conversation history is kept in memory only
- **Note**: OpenAI API requests include `store: true`, meaning OpenAI may store conversation data on their servers
- No sensitive data is logged or persisted locally in the application

## Limitations

- Requires internet connection
- Subject to OpenAI API rate limits
- Conversation history limited to last 100 messages for token management
- Maximum response length depends on OpenAI model limits

## Testing

The module can be tested by:
1. Compiling and running Telebrain
2. Configuring LLM API endpoint, key, and model in Settings → Telebrain
3. Navigating to the AI chat section
4. Sending test messages
