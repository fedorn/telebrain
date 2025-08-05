/*
This file is part of Telebrain, a fork of Telegram Desktop with AI Copilot.

For license and copyright information please follow this link:
https://github.com/fedorn/telebrain/blob/dev/LEGAL
*/
#pragma once

#include "window/section_widget.h"
#include "window/section_memento.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/text/text.h"
#include "ai/openai_client.h"
#include <QtGui/QMouseEvent>
#include <QtCore/QTimer>

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {
class ComposeControls;
} // namespace HistoryView

namespace Ui {
class ChatStyle;
class RpWidget;
} // namespace Ui

namespace AI {

// Forward declaration of MessagesWidget
class MessagesWidget;

class AIChatWidget final : public Window::SectionWidget {
public:
	AIChatWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

	// Pure virtual methods from SectionWidget
	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;

	// Pure virtual methods from FloatSectionDelegate
	QRect floatPlayerAvailableRect() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;

private:
	void updateControlsGeometry();
	void setupComposeControls();
	void handleSendMessage(const QString &text);
	void addUserMessage(const QString &text);
	void addAIMessage(const QString &text);
	void sendToOpenAI();
	void clearChat();

	std::vector<MessageData> _messages;
	std::unique_ptr<Ui::ScrollArea> _scroll;
	std::unique_ptr<MessagesWidget> _messagesWidget;
	std::unique_ptr<HistoryView::ComposeControls> _composeControls;
	std::unique_ptr<Ui::ChatStyle> _chatStyle;
	std::unique_ptr<Ui::RoundButton> _clearButton;
	
	// OpenAI API integration
	std::unique_ptr<OpenAIClient> _openaiClient;

};

} // namespace AI 