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
#include "data/data_forum_topic.h"
#include "data/data_histories.h"
#include "data/data_replies_list.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
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

constexpr auto kContextMessageCount = 30;

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
	_onSuccess = std::move(onSuccess);
	_onError = std::move(onError);
	_isWaitingForResponse = true;

	auto messagesCopy = messages;
	const auto useLocal = Core::App().settings().useLocalAiBackend();
	getChatContext([this, messagesCopy = std::move(messagesCopy), useLocal](QString chatContext) {
		QString baseUrl = Core::App().settings().aiChatBaseUrl();
		if (baseUrl.isEmpty()) {
			baseUrl = useLocal ? "http://127.0.0.1:8000" : "https://openrouter.ai/api/v1";
		}
		QString path = useLocal ? "/v1/chat" : "/chat/completions";
		QString urlStr = baseUrl.endsWith('/') ? baseUrl.left(baseUrl.size() - 1) + path : baseUrl + path;

		QNetworkRequest request{QUrl(urlStr)};
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		request.setTransferTimeout(60000);

		QJsonObject requestBody;
		QByteArray data;
		if (useLocal) {
			_isLocalBackendRequest = true;
			request.setRawHeader("HTTP-Referer", QByteArray("https://github.com/fedorn/telebrain"));
			request.setRawHeader("X-Title", QByteArray("Telebrain"));
			QJsonArray messagesArray;
			const int maxHistory = 100;
			const int startIndex = std::max(0, static_cast<int>(messagesCopy.size()) - maxHistory);
			for (int i = startIndex; i < static_cast<int>(messagesCopy.size()); ++i) {
				const auto &msg = messagesCopy[i];
				if (msg.text == kThinkingMessage || msg.text == kWelcomeMessage) {
					continue;
				}
				messagesArray.append(QJsonObject{
					{"role", msg.isFromUser ? "user" : "assistant"},
					{"content", msg.text}
				});
			}
			requestBody["messages"] = messagesArray;
			requestBody["chat_context"] = chatContext;
			QString userName;
			QString userUsername;
			if (_sessionController) {
				const auto user = _sessionController->session().user();
				userName = user->name();
				userUsername = user->username();
			}
			requestBody["user_name"] = userName;
			requestBody["user_username"] = userUsername;
			QJsonDocument doc(requestBody);
			data = doc.toJson();
		} else {
			request.setRawHeader("HTTP-Referer", QByteArray("https://github.com/fedorn/telebrain"));
			request.setRawHeader("X-Title", QByteArray("Telebrain"));
			QString openaiApiKey = Core::App().settings().aiChatApiKey();
			if (!openaiApiKey.isEmpty()) {
				request.setRawHeader("Authorization", QString("Bearer %1").arg(openaiApiKey).toUtf8());
			}
			QJsonArray messagesArray;
			messagesArray.append(QJsonObject{
				{"role", "system"},
				{"content", prepareSystemMessage(chatContext)}
			});
			const int maxHistory = 100;
			const int startIndex = std::max(0, static_cast<int>(messagesCopy.size()) - maxHistory);
			for (int i = startIndex; i < static_cast<int>(messagesCopy.size()); ++i) {
				const auto &msg = messagesCopy[i];
				if (msg.text == kThinkingMessage || msg.text == kWelcomeMessage) {
					continue;
				}
				messagesArray.append(QJsonObject{
					{"role", msg.isFromUser ? "user" : "assistant"},
					{"content", msg.text}
				});
			}
			requestBody = createRequestBody(messagesArray);
			QJsonDocument doc(requestBody);
			data = doc.toJson();
		}

		QNetworkReply *reply = _networkManager->post(request, data);

		connect(reply, &QNetworkReply::finished, this, [this, reply]() {
			handleResponse(reply);
			reply->deleteLater();
		});

		connect(reply, &QNetworkReply::errorOccurred,
			this, [this, reply](QNetworkReply::NetworkError error) {
				handleNetworkError(error, reply->errorString());
				reply->deleteLater();
			});
	});
}

bool OpenAIClient::isWaitingForResponse() const {
	return _isWaitingForResponse;
}

void OpenAIClient::handleResponse(QNetworkReply *reply) {
	_isWaitingForResponse = false;
	const bool localBackend = _isLocalBackendRequest;
	if (localBackend) {
		_isLocalBackendRequest = false;
	}

	QByteArray responseData = reply->readAll();

	if (localBackend) {
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status != 200) {
			QString errMsg;
			QJsonParseError parseError;
			QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
			if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
				QString detail = doc.object()["detail"].toString();
				if (!detail.isEmpty()) {
					errMsg = detail;
				}
			}
			if (errMsg.isEmpty()) {
				errMsg = QString("Local AI backend error (HTTP %1)").arg(status);
			}
			if (_onError) {
				_onError(errMsg);
			}
			return;
		}
		QJsonParseError parseError;
		QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData, &parseError);
		if (parseError.error != QJsonParseError::NoError) {
			if (_onError) {
				_onError(QString("JSON parse error: %1").arg(parseError.errorString()));
			}
			return;
		}
		QJsonObject responseObj = jsonResponse.object();
		if (responseObj.contains("error")) {
			if (_onError) {
				_onError(responseObj["error"].toString());
			}
			return;
		}
		QString content = responseObj["content"].toString();
		if (content.isEmpty()) {
			if (_onError) {
				_onError("Empty response from local AI backend");
			}
			return;
		}
		if (_onSuccess) {
			_onSuccess(content);
		}
		return;
	}

	if (reply->error() != QNetworkReply::NoError) {
		return;
	}

	QJsonParseError parseError;
	QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData, &parseError);

	if (parseError.error != QJsonParseError::NoError) {
		if (_onError) {
			_onError(QString("JSON parse error: %1").arg(parseError.errorString()));
		}
		return;
	}

	QJsonObject responseObj = jsonResponse.object();

	if (responseObj.contains("error")) {
		QJsonObject errorObj = responseObj["error"].toObject();
		QString errorMessage = errorObj["message"].toString();
		if (_onError) {
			_onError(QString("OpenAI API error: %1").arg(errorMessage));
		}
		return;
	}

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

	if (_onSuccess) {
		_onSuccess(aiResponse);
	}
}

void OpenAIClient::handleNetworkError(QNetworkReply::NetworkError error, const QString &errorString) {
	_isWaitingForResponse = false;
	_isLocalBackendRequest = false;

	if (_onError) {
		_onError(QString("Network error: %1").arg(errorString));
	}
}

QString OpenAIClient::prepareSystemMessage(const QString &chatContext) const {
	QString systemMessage = "You are Telebrain — the AI Copilot integrated into Telegram Desktop. Be concise, helpful, and context-aware. Use the conversation context when relevant, ask clarifying questions when information is missing, and avoid fabricating Telegram data you cannot access.\n\nFormatting: use **text** for bold and __text__ for italic. Use only one style per span (no bold+italic on the same text). No other markdown.";
	QString currentDate = QDateTime::currentDateTime().toString("MMMM d, yyyy");
	systemMessage += QString("\n\nToday's date is: %1").arg(currentDate);

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

		if (!chatContext.isEmpty()) {
			systemMessage += chatContext;
		}
	}

	return systemMessage;
}

void OpenAIClient::getChatContext(std::function<void(QString)> done) {
	if (!_sessionController) {
		done(QString());
		return;
	}
	const auto activeChat = _sessionController->activeChatCurrent();
	const auto history = activeChat.owningHistory();
	if (!history) {
		done(QString());
		return;
	}
	const auto topic = activeChat.topic();
	auto afterLoad = [activeChat, history, done = std::move(done)]() {
		const auto thread = activeChat.thread();
		const auto topicRootId = thread ? thread->topicRootId() : MsgId(0);
		const auto items = history->recentMessagesForContext(topicRootId, kContextMessageCount);
		if (items.empty()) {
			done(QString());
			return;
		}
		QString chatContext = QString("\n\nCurrent chat context (last %1 messages):\n").arg(items.size());
		for (const auto &item : items) {
			QString senderName = item->displayFrom()->name();
			QString timestamp = QDateTime::fromSecsSinceEpoch(item->date()).toString("yyyy-MM-dd HH:mm:ss");
			auto content = item->originalText().text;
			if (content.isEmpty()) {
				content = item->notificationText().text;
			}
			if (content.isEmpty()) {
				content = item->isService()
					? QString("[service]")
					: (item->media() ? QString("[media]") : QString("[message]"));
			}
			chatContext += QString("[%1] %2: %3\n")
				.arg(timestamp)
				.arg(senderName)
				.arg(content);
		}
		chatContext += "\nYou can reference this chat context when responding to the user.";
		done(chatContext);
	};
	if (topic) {
		topic->replies()->requestRecentForContext(kContextMessageCount, std::move(afterLoad));
	} else {
		history->owner().histories().requestRecentForContext(
			history,
			kContextMessageCount,
			std::move(afterLoad));
	}
}

QJsonObject OpenAIClient::createRequestBody(const QJsonArray &messages) const {
	QJsonObject requestBody;
	
	// Get model from settings, default to stepfun/step-3.5-flash:free if not set
	QString model = Core::App().settings().aiChatModel();
	if (model.isEmpty()) {
		model = "stepfun/step-3.5-flash:free";
	}
	
	requestBody["model"] = model;
	requestBody["messages"] = messages;
	requestBody["store"] = true;
	
	return requestBody;
}

} // namespace AI 