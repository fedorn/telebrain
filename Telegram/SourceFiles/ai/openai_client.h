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

	void ensureContextLoaded(std::function<void()> done);

private Q_SLOTS:
	void handleNetworkError(QNetworkReply::NetworkError error, const QString &errorString);

private:
	QString prepareSystemMessage() const;
	void getChatContext(std::function<void(QString)> done);
	QJsonObject createRequestBody(const QJsonArray &messages) const;
	QJsonArray buildToolsDefinition() const;

	void sendCompletionRequest(QJsonArray messages);
	void handleResponse(QNetworkReply *reply);

	QString executeToolListChats(const QJsonObject &args) const;
	QString executeToolGetUserInfo(const QJsonObject &args) const;
	void executeToolReadChatMessages(
		uint64 chatId,
		int count,
		std::function<void(const QString &)> done);
	void processToolCalls(
		const QJsonArray &toolCalls,
		std::function<void(const QJsonArray &)> done);

	std::unique_ptr<QNetworkAccessManager> _networkManager;
	bool _isWaitingForResponse = false;
	std::function<void(const QString &)> _onSuccess;
	std::function<void(const QString &)> _onError;
	Window::SessionController* _sessionController = nullptr;
	QJsonArray _lastSentMessages;
};

} // namespace AI 