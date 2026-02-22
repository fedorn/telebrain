/*
This file is part of Telebrain, a fork of Telegram Desktop with AI Copilot.

For license and copyright information please follow this link:
https://github.com/fedorn/telebrain/blob/dev/LEGAL
*/
#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <memory>
#include <functional>
#include "base/basic_types.h"

namespace Window {
class SessionController;
} // namespace Window

namespace AI {

// Simple message structure for AI chat
struct MessageData {
	QString text;
	bool isFromUser;
	TimeId timestamp;
};

class OpenAIClient : public QObject {
	Q_OBJECT

public:
	explicit OpenAIClient(QObject *parent = nullptr);
	~OpenAIClient();
	
	// Set the session controller for context
	void setSessionController(not_null<Window::SessionController*> controller);
	
	// Send a chat completion request
	void sendChatCompletion(
		const std::vector<MessageData> &messages,
		std::function<void(const QString &)> onSuccess,
		std::function<void(const QString &)> onError);

	// Check if currently waiting for a response
	bool isWaitingForResponse() const;

private Q_SLOTS:
	void handleResponse(QNetworkReply *reply);
	void handleNetworkError(QNetworkReply::NetworkError error, const QString &errorString);

private:
	QString prepareSystemMessage(const QString &chatContext) const;
	void getChatContext(std::function<void(QString)> done);
		
	// Create the request body
	QJsonObject createRequestBody(const QJsonArray &messages) const;

	std::unique_ptr<QNetworkAccessManager> _networkManager;
	bool _isWaitingForResponse = false;
	
	// Callbacks for the current request
	std::function<void(const QString &)> _onSuccess;
	std::function<void(const QString &)> _onError;
	
	// Session controller for getting user and chat context
	Window::SessionController* _sessionController = nullptr;
};

} // namespace AI 