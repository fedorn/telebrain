/*
This file is part of Telebrain, a fork of Telegram Desktop with AI Copilot.

For license and copyright information please follow this link:
https://github.com/fedorn/telebrain/blob/dev/LEGAL
*/
#include "ai/openai_client.h"
#include "ai/ai_contstants.h"

#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "base/unixtime.h"
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QDateTime>
#include <QtCore/QByteArray>

namespace AI {

OpenAIClient::OpenAIClient(QObject *parent)
: QObject(parent)
, _networkManager(std::make_unique<QNetworkAccessManager>()) {
}

OpenAIClient::~OpenAIClient() = default;

void OpenAIClient::setSessionController(not_null<Window::SessionController*> controller) {
	_sessionController = controller.get();
}

void OpenAIClient::sendChatCompletion(
		const std::vector<MessageData> &messages,
		std::function<void(const QString &)> onSuccess,
		std::function<void(const QString &)> onError) {
	
	if (_isWaitingForResponse) {
		onError("Already waiting for a response. Please wait for the current request to complete.");
		return;
	}
	// Store callbacks
	_onSuccess = std::move(onSuccess);
	_onError = std::move(onError);
	_isWaitingForResponse = true;

	// Get base URL from settings
	QString baseUrl = Core::App().settings().aiChatBaseUrl();
	if (baseUrl.isEmpty()) {
		baseUrl = "https://api.openai.com/v1";
	}
	// Get API key from settings
	QString openaiApiKey = Core::App().settings().aiChatApiKey();
	
	// Prepare the request
	QNetworkRequest request(QUrl(baseUrl + "/chat/completions"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	if (!openaiApiKey.isEmpty()) {
		request.setRawHeader("Authorization", QString("Bearer %1").arg(openaiApiKey).toUtf8());
	}
	request.setTransferTimeout(60000); // 60 second timeout

	// Prepare messages with system message and conversation
	QJsonArray messagesArray;
	
	// Add system message for context
	messagesArray.append(QJsonObject{
		{"role", "system"},
		{"content", prepareSystemMessage()}
	});

	// Add conversation messages (last 100 to avoid token limits)
	const int maxHistory = 100;
	const int startIndex = std::max(0, static_cast<int>(messages.size()) - maxHistory);
	for (int i = startIndex; i < static_cast<int>(messages.size()); ++i) {
		const auto &msg = messages[i];
		// Skip the "thinking" message and the initial assistant greeting
		if (msg.text == kThinkingMessage || msg.text == kWelcomeMessage) {
			continue;
		}
		messagesArray.append(QJsonObject{
			{"role", msg.isFromUser ? "user" : "assistant"},
			{"content", msg.text}
		});
	}

	// Create request body
	QJsonObject requestBody = createRequestBody(messagesArray);
	QJsonDocument doc(requestBody);
	QByteArray data = doc.toJson();

	// Send the request
	QNetworkReply *reply = _networkManager->post(request, data);
	
	// Connect signals
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		handleResponse(reply);
		reply->deleteLater();
	});

	connect(reply, &QNetworkReply::errorOccurred,
		this, [this, reply](QNetworkReply::NetworkError error) {
			handleNetworkError(error, reply->errorString());
			reply->deleteLater();
		});
}

bool OpenAIClient::isWaitingForResponse() const {
	return _isWaitingForResponse;
}

void OpenAIClient::handleResponse(QNetworkReply *reply) {
	_isWaitingForResponse = false;

	if (reply->error() != QNetworkReply::NoError) {
		return;
	}

	QByteArray responseData = reply->readAll();
	QJsonParseError parseError;
	QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData, &parseError);

	if (parseError.error != QJsonParseError::NoError) {
		if (_onError) {
			_onError(QString("JSON parse error: %1").arg(parseError.errorString()));
		}
		return;
	}

	QJsonObject responseObj = jsonResponse.object();
	
	// Check for API errors
	if (responseObj.contains("error")) {
		QJsonObject errorObj = responseObj["error"].toObject();
		QString errorMessage = errorObj["message"].toString();
		if (_onError) {
			_onError(QString("OpenAI API error: %1").arg(errorMessage));
		}
		return;
	}

	// Extract the response text
	QJsonArray choices = responseObj["choices"].toArray();
	if (choices.isEmpty()) {
		if (_onError) {
			_onError("No response from OpenAI API");
		}
		return;
	}

	QJsonObject choice = choices[0].toObject();
	QJsonObject message = choice["message"].toObject();
	QString aiResponse = message["content"].toString();

	if (aiResponse.isEmpty()) {
		if (_onError) {
			_onError("Empty response from OpenAI API");
		}
		return;
	}

	// Call success callback
	if (_onSuccess) {
		_onSuccess(aiResponse);
	}
}

void OpenAIClient::handleNetworkError(QNetworkReply::NetworkError error, const QString &errorString) {
	_isWaitingForResponse = false;
	
	if (_onError) {
		_onError(QString("Network error: %1").arg(errorString));
	}
}

QString OpenAIClient::prepareSystemMessage() const {
	QString systemMessage = "You are Telebrain — the AI Copilot integrated into Telegram Desktop. Be concise, helpful, and context-aware. Use the conversation context when relevant, ask clarifying questions when information is missing, and avoid fabricating Telegram data you cannot access.";
	
	// Add current date
	QString currentDate = QDateTime::currentDateTime().toString("MMMM d, yyyy");
	systemMessage += QString("\n\nToday's date is: %1").arg(currentDate);
	
	// Add user information if session controller is available
	if (_sessionController) {
		const auto user = _sessionController->session().user();
		const auto userName = user->name();
		const auto userUsername = user->username();
		
		QString userInfo = "\n\nUser Information:\n";
		userInfo += QString("- Name: %1\n").arg(userName);
		if (!userUsername.isEmpty()) {
			userInfo += QString("- Username: @%1\n").arg(userUsername);
		}
		userInfo += "- You are chatting in Telebrain (AI Copilot for Telegram Desktop).\n";
		userInfo += "- The user is asking you questions and seeking assistance.\n";
		
		systemMessage += userInfo;
		
		// Add chat context
		QString chatContext = getChatContext();
		if (!chatContext.isEmpty()) {
			systemMessage += chatContext;
		}
	}
	
	return systemMessage;
}

QString OpenAIClient::getChatContext() const {
	if (!_sessionController) {
		return QString();
	}

	// Get the last 30 (equals to kMessagesPerPageFirst) messages from the current active chat if it's open
	const auto activeChat = _sessionController->activeChatCurrent();
	if (const auto history = activeChat.history()) {
		const int maxChatMessages = 30;
		int messageCount = 0;
		QString chatContext = QString("\n\nCurrent chat context (last %1 messages):\n").arg(maxChatMessages);
		
		// Collect the latest messages in reverse order (newest first)
		std::vector<QString> messageLines;
		
		// Iterate through blocks in reverse order to get the most recent messages
		for (auto blockIt = history->blocks.rbegin(); 
			 blockIt != history->blocks.rend() && messageCount < maxChatMessages; 
			 ++blockIt) {
			const auto &block = *blockIt;
			
			// Iterate through messages in reverse order within each block
			for (auto msgIt = block->messages.rbegin(); 
				 msgIt != block->messages.rend() && messageCount < maxChatMessages; 
				 ++msgIt) {
				const auto &element = *msgIt;
				const auto item = element->data();
				
				// Only include regular text messages (skip service messages, etc.)
				if (item->isRegular() && !item->isEmpty()) {
					QString messageText = item->originalText().text;
					if (!messageText.isEmpty()) {
						QString senderName = item->displayFrom()->name();
						QString timestamp = QDateTime::fromSecsSinceEpoch(item->date()).toString("HH:mm");
						
						messageLines.push_back(QString("[%1] %2: %3")
							.arg(timestamp)
							.arg(senderName)
							.arg(messageText));
						
						messageCount++;
					}
				}
			}
		}
		
		// Add messages to context in chronological order (oldest first)
		// Since we collected them newest first, we need to reverse the order
		for (auto it = messageLines.rbegin(); it != messageLines.rend(); ++it) {
			chatContext += *it + "\n";
		}
		
		if (messageCount > 0) {
			chatContext += "\nYou can reference this chat context when responding to the user.";
			return chatContext;
		}
	}
	
	return QString();
}

QJsonObject OpenAIClient::createRequestBody(const QJsonArray &messages) const {
	QJsonObject requestBody;
	
	// Get model from settings, default to gpt-5.2 if not set
	QString model = Core::App().settings().aiChatModel();
	if (model.isEmpty()) {
		model = "gpt-5.2";
	}
	
	requestBody["model"] = model;
	requestBody["messages"] = messages;
	requestBody["store"] = true;
	
	return requestBody;
}

} // namespace AI 