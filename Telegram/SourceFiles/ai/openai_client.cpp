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
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_replies_list.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_row.h"
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

void OpenAIClient::ensureContextLoaded(std::function<void()> done) {
	if (!_sessionController) {
		done();
		return;
	}
	const auto activeChat = _sessionController->activeChatCurrent();
	const auto history = activeChat.owningHistory();
	if (!history) {
		done();
		return;
	}
	const auto topic = activeChat.topic();
	if (topic) {
		topic->replies()->requestRecentForContext(kContextMessageCount, std::move(done));
	} else {
		history->owner().histories().requestRecentForContext(
			history,
			kContextMessageCount,
			std::move(done));
	}
}

bool OpenAIClient::isWaitingForResponse() const {
	return _isWaitingForResponse;
}

void OpenAIClient::handleResponse(QNetworkReply *reply) {
	if (reply->error() != QNetworkReply::NoError) {
		_isWaitingForResponse = false;
		return;
	}

	QByteArray responseData = reply->readAll();
	QJsonParseError parseError;
	QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		_isWaitingForResponse = false;
		if (_onError) {
			_onError(QString("JSON parse error: %1").arg(parseError.errorString()));
		}
		return;
	}

	QJsonObject responseObj = jsonResponse.object();
	if (responseObj.contains("error")) {
		_isWaitingForResponse = false;
		QJsonObject errorObj = responseObj["error"].toObject();
		QString errorMessage = errorObj["message"].toString();
		if (_onError) {
			_onError(QString("OpenAI API error: %1").arg(errorMessage));
		}
		return;
	}

	QJsonArray choices = responseObj["choices"].toArray();
	if (choices.isEmpty()) {
		_isWaitingForResponse = false;
		if (_onError) {
			_onError("No response from OpenAI API");
		}
		return;
	}

	QJsonObject choice = choices[0].toObject();
	QString finishReason = choice["finish_reason"].toString();
	QJsonObject message = choice["message"].toObject();
	QJsonValue contentValue = message["content"];
	QString aiResponse = contentValue.isString() ? contentValue.toString() : QString();
	QJsonArray toolCalls = message["tool_calls"].toArray();

	if (!toolCalls.isEmpty()) {
		QJsonObject assistantMessage;
		assistantMessage["role"] = "assistant";
		assistantMessage["content"] = contentValue.isNull() ? QJsonValue(QString()) : contentValue;
		assistantMessage["tool_calls"] = toolCalls;
		_lastSentMessages.append(assistantMessage);
		processToolCalls(toolCalls, [this](const QJsonArray &toolMessages) {
			for (const auto &m : toolMessages) {
				_lastSentMessages.append(m);
			}
			sendCompletionRequest(_lastSentMessages);
		});
		return;
	}

	_isWaitingForResponse = false;
	if (aiResponse.isEmpty() && finishReason != "stop") {
		if (_onError) {
			_onError("Empty response from OpenAI API");
		}
		return;
	}
	if (_onSuccess) {
		_onSuccess(aiResponse.isEmpty() ? QString() : aiResponse);
	}
}

void OpenAIClient::handleNetworkError(QNetworkReply::NetworkError error, const QString &errorString) {
	_isWaitingForResponse = false;
	
	if (_onError) {
		_onError(QString("Network error: %1").arg(errorString));
	}
}

QString OpenAIClient::prepareSystemMessage() const {
	QString systemMessage = "You are Telebrain — the AI Copilot integrated into Telegram Desktop. Be concise, helpful, and context-aware. Use the conversation context when relevant, ask clarifying questions when information is missing, and avoid fabricating Telegram data you cannot access.\n\nFormatting: use **text** for bold and __text__ for italic. Do not use other markdown; only these two patterns will be rendered.";
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
	auto afterLoad = [this, activeChat, history, done = std::move(done)]() {
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

QJsonArray OpenAIClient::buildToolsDefinition() const {
	QJsonArray tools;
	QJsonObject listChatsParams;
	listChatsParams["type"] = "object";
	QJsonObject listChatsProps;
	listChatsProps["limit"] = QJsonObject{
		{"type", "integer"},
		{"description", "Max number of chats to return (default 50)"}
	};
	listChatsParams["properties"] = listChatsProps;
	tools.append(QJsonObject{
		{"type", "function"},
		{"function", QJsonObject{
			{"name", "list_chats"},
			{"description", "Get the list of the user's chats (dialogs). Returns chat id, name, and type for each chat."},
			{"parameters", listChatsParams}
		}}
	});
	QJsonObject getUserInfoParams;
	getUserInfoParams["type"] = "object";
	getUserInfoParams["properties"] = QJsonObject{
		{"user_id", QJsonObject{{"type", "integer"}, {"description", "Telegram user id (numeric)"}}},
		{"username", QJsonObject{{"type", "string"}, {"description", "Username without @ (e.g. telegram)"}}}
	};
	tools.append(QJsonObject{
		{"type", "function"},
		{"function", QJsonObject{
			{"name", "get_user_info"},
			{"description", "Get information about a Telegram user by user id or username."},
			{"parameters", getUserInfoParams}
		}}
	});
	QJsonObject readMessagesParams;
	readMessagesParams["type"] = "object";
	readMessagesParams["required"] = QJsonArray{"chat_id"};
	readMessagesParams["properties"] = QJsonObject{
		{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat/peer id from list_chats"}}},
		{"count", QJsonObject{{"type", "integer"}, {"description", "Number of messages to read (1-100, default 20)"}}}
	};
	tools.append(QJsonObject{
		{"type", "function"},
		{"function", QJsonObject{
			{"name", "read_chat_messages"},
			{"description", "Read the last N messages in a chat. Use chat_id from list_chats."},
			{"parameters", readMessagesParams}
		}}
	});
	return tools;
}

QJsonObject OpenAIClient::createRequestBody(const QJsonArray &messages) const {
	QJsonObject requestBody;
	QString model = Core::App().settings().aiChatModel();
	if (model.isEmpty()) {
		model = "stepfun/step-3.5-flash:free";
	}
	requestBody["model"] = model;
	requestBody["messages"] = messages;
	requestBody["store"] = true;
	requestBody["tools"] = buildToolsDefinition();
	return requestBody;
}

void OpenAIClient::sendCompletionRequest(QJsonArray messages) {
	_lastSentMessages = std::move(messages);
	QString baseUrl = Core::App().settings().aiChatBaseUrl();
	if (baseUrl.isEmpty()) {
		baseUrl = "https://openrouter.ai/api/v1";
	}
	QString openaiApiKey = Core::App().settings().aiChatApiKey();
	QNetworkRequest request(QUrl(baseUrl + "/chat/completions"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	if (!openaiApiKey.isEmpty()) {
		request.setRawHeader("Authorization", QString("Bearer %1").arg(openaiApiKey).toUtf8());
	}
	request.setTransferTimeout(60000);
	QJsonObject requestBody = createRequestBody(_lastSentMessages);
	QJsonDocument doc(requestBody);
	QByteArray data = doc.toJson();
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

	QJsonArray messagesArray;
	messagesArray.append(QJsonObject{
		{"role", "system"},
		{"content", prepareSystemMessage()}
	});
	const int maxHistory = 100;
	const int startIndex = std::max(0, static_cast<int>(messages.size()) - maxHistory);
	for (int i = startIndex; i < static_cast<int>(messages.size()); ++i) {
		const auto &msg = messages[i];
		if (msg.text == kThinkingMessage || msg.text == kWelcomeMessage) {
			continue;
		}
		messagesArray.append(QJsonObject{
			{"role", msg.isFromUser ? "user" : "assistant"},
			{"content", msg.text}
		});
	}
	sendCompletionRequest(std::move(messagesArray));
}

QString OpenAIClient::executeToolListChats(const QJsonObject &args) const {
	if (!_sessionController) {
		return QStringLiteral("{\"error\": \"No session\"}");
	}
	const auto limit = std::min(50, std::max(1, args["limit"].toInt(50)));
	QJsonArray chats;
	const auto list = _sessionController->session().data().chatsList()->indexed();
	int count = 0;
	for (const auto &row : *list) {
		if (count >= limit) {
			break;
		}
		const auto history = row->history();
		if (!history) {
			continue;
		}
		const auto peer = history->peer;
		QString type = peer->isUser() ? "user" : (peer->isChat() ? "chat" : "channel");
		QJsonObject obj;
		obj["chat_id"] = QJsonValue(static_cast<qint64>(peer->id.value));
		obj["name"] = peer->name();
		obj["type"] = type;
		if (const auto user = peer->asUser()) {
			const auto u = user->username();
			if (!u.isEmpty()) {
				obj["username"] = u;
			}
		}
		chats.append(obj);
		++count;
	}
	QJsonObject out;
	out["chats"] = chats;
	return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString OpenAIClient::executeToolGetUserInfo(const QJsonObject &args) const {
	if (!_sessionController) {
		return QStringLiteral("{\"error\": \"No session\"}");
	}
	auto &data = _sessionController->session().data();
	UserData *user = nullptr;
	if (args.contains("user_id")) {
		const auto id = args["user_id"].toInteger(0);
		user = data.user(UserId(static_cast<BareId>(id)));
	}
	if (!user && args.contains("username")) {
		auto username = args["username"].toString().trimmed();
		if (username.startsWith('@')) {
			username = username.mid(1);
		}
		if (!username.isEmpty()) {
			const auto peer = data.peerByUsername(username);
			if (peer && peer->isUser()) {
				user = peer->asUser();
			}
		}
	}
	if (!user) {
		return QStringLiteral("{\"error\": \"User not found\"}");
	}
	QJsonObject out;
	out["user_id"] = QJsonValue(static_cast<qint64>(user->id.value));
	out["name"] = user->name();
	const auto u = user->username();
	if (!u.isEmpty()) {
		out["username"] = u;
	}
	return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

void OpenAIClient::executeToolReadChatMessages(
		uint64 chatId,
		int count,
		std::function<void(const QString &)> done) {
	if (!_sessionController) {
		done(QStringLiteral("{\"error\": \"No session\"}"));
		return;
	}
	count = std::min(100, std::max(1, count));
	const auto peerId = PeerId(PeerIdHelper(chatId));
	auto &data = _sessionController->session().data();
	const auto history = data.history(peerId);
	history->owner().histories().requestRecentForContext(
		history,
		count,
		[history, count, done]() {
			const auto items = history->recentMessagesForContext(MsgId(0), count);
			QJsonArray arr;
			for (const auto &item : items) {
				QJsonObject m;
				m["sender"] = item->displayFrom()->name();
				m["date"] = QDateTime::fromSecsSinceEpoch(item->date()).toString(Qt::ISODate);
				m["text"] = item->originalText().text;
				arr.append(m);
			}
			QJsonObject out;
			out["messages"] = arr;
			done(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
		});
}

void OpenAIClient::processToolCalls(
		const QJsonArray &toolCalls,
		std::function<void(const QJsonArray &)> done) {
	struct State {
		QJsonArray results;
		int pending = 0;
		std::function<void(const QJsonArray &)> done;
	};
	const auto state = std::make_shared<State>();
	for (int i = 0; i < toolCalls.size(); ++i) {
		state->results.append(QJsonValue());
	}
	state->done = std::move(done);
	for (int i = 0; i < toolCalls.size(); ++i) {
		const auto tc = toolCalls[i].toObject();
		const auto id = tc["id"].toString();
		const auto func = tc["function"].toObject();
		const auto name = func["name"].toString();
		QJsonObject args;
		const auto argsStr = func["arguments"].toString();
		if (!argsStr.isEmpty()) {
			QJsonParseError err;
			const auto doc = QJsonDocument::fromJson(argsStr.toUtf8(), &err);
			if (err.error == QJsonParseError::NoError && doc.isObject()) {
				args = doc.object();
			}
		}
		if (name == "list_chats") {
			state->results[i] = QJsonObject{
				{"role", "tool"},
				{"tool_call_id", id},
				{"content", executeToolListChats(args)}
			};
		} else if (name == "get_user_info") {
			state->results[i] = QJsonObject{
				{"role", "tool"},
				{"tool_call_id", id},
				{"content", executeToolGetUserInfo(args)}
			};
		} else if (name == "read_chat_messages") {
			state->pending++;
			const auto chatId = static_cast<uint64>(args["chat_id"].toInteger(0));
			const auto count = args["count"].toInt(20);
			const auto index = i;
			executeToolReadChatMessages(chatId, count, [state, index, id](const QString &content) {
				state->results[index] = QJsonObject{
					{"role", "tool"},
					{"tool_call_id", id},
					{"content", content}
				};
				state->pending--;
				if (state->pending == 0) {
					state->done(state->results);
				}
			});
		} else {
			state->results[i] = QJsonObject{
				{"role", "tool"},
				{"tool_call_id", id},
				{"content", QStringLiteral("{\"error\": \"Unknown tool\"}")}
			};
		}
	}
	if (state->pending == 0) {
		state->done(state->results);
	}
}

} // namespace AI 