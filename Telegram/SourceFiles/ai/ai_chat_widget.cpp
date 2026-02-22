/*
This file is part of Telebrain, a fork of Telegram Desktop with AI Copilot.

For license and copyright information please follow this link:
https://github.com/fedorn/telebrain/blob/dev/LEGAL
*/
#include "ai/ai_chat_widget.h"
#include "ai/ai_contstants.h"

#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "base/unixtime.h"
#include "menu/menu_send.h"
#include "styles/style_window.h"
#include "styles/style_chat.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text.h"
#include "ui/text/text_entity.h"
#include "base/timer.h"
#include "history/view/history_view_element.h"
#include <QtGui/QPainterPath>
#include <QtGui/QTextLayout>
#include <QtGui/QClipboard>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QObject>

namespace AI {

// AI Chat background color
const QColor kAIChatBackgroundColor(241, 241, 241);

[[nodiscard]] TextWithEntities GetTextForMessage(const MessageData &message) {
	if (message.isFromUser) {
		return TextWithEntities::Simple(message.text);
	}
	auto rich = Ui::Text::RichLangValue(message.text);
	TextUtilities::ParseEntities(rich, TextParseLinks
		| TextParseMentions
		| TextParseHashtags
		| TextParseMultiline
		| TextParseMarkdown);
	return rich;
}

// Layout constants for message list (shared by sizing, hit-test, paint)
constexpr int kMessagesMargin = 20;
constexpr int kMessageSpacing = 8;
constexpr int kBottomMargin = 40;

class MessagesWidget : public Ui::RpWidget {
public:
	MessagesWidget(
		QWidget *parent,
		not_null<Ui::ChatStyle*> chatStyle,
		const std::vector<MessageData> &messages)
	: RpWidget(parent)
	, _chatStyle(chatStyle)
	, _messages(messages) {
		setAttribute(Qt::WA_OpaquePaintEvent);
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
		
		updateSize();
	}

	void setMessages(const std::vector<MessageData> &messages) {
		_messages = messages;
		clearTextSelection();
		clearMessageSelection();
		updateSize();
		update();
	}

	void resizeToWidth(int width) {
		if (this->width() != width) {
			resize(width, height());
			updateSize();
		}
	}

	void clearTextSelection() {
		_selectedMessageIndex = -1;
		_textSelection = TextSelection();
		// Only reset _mouseAction if we're not in message selection mode
		if (!_inMessageSelectionMode) {
			_mouseAction = MouseAction::None;
		}
		// Don't reset _mouseSelectType here to preserve triple-click state
		// Don't reset _inMessageSelectionMode here, it should be reset explicitly
		// Don't reset click counter here to preserve triple-click detection
		setCursor(Qt::ArrowCursor);
		update();
	}

	void clearMessageSelection() {
		if (!_selectedMessages.empty()) {
			_selectedMessages.clear();
			_inMessageSelectionMode = false;
			update();
		}
		// Note: This method doesn't cancel the triple-click timer
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		p.fillRect(rect(), kAIChatBackgroundColor);

		const auto messagesRect = getMessagesRect();
		const auto msgPadding = st::msgPadding;
		auto y = messagesRect.top();

		for (size_t i = 0; i < _messages.size(); ++i) {
			const auto &message = _messages[i];
			const auto layout = computeLayoutForMessage(messagesRect, y, message);
			const auto bubbleRect = layout.bubbleRect;
			y = layout.nextY;

			const auto isSelected = _selectedMessages.find(static_cast<int>(i)) != _selectedMessages.end();
			Ui::PaintBubble(p, Ui::SimpleBubble{
				.st = _chatStyle.get(),
				.geometry = bubbleRect,
				.outerWidth = width(),
				.selected = isSelected,
				.shadowed = true,
				.outbg = message.isFromUser,
				.rounding = Ui::BubbleRounding{
					Ui::BubbleCornerRounding::Large,
					Ui::BubbleCornerRounding::Large,
					Ui::BubbleCornerRounding::Large,
					Ui::BubbleCornerRounding::Large,
				},
			});

			const auto &messageStyle = _chatStyle->messageStyle(message.isFromUser, isSelected);
			const auto textRect = bubbleRect.marginsRemoved(msgPadding);
			const auto hasSelection = (_selectedMessageIndex == static_cast<int>(i)) && !_textSelection.empty();
			drawTextWithSelection(p, textRect, message, hasSelection ? _textSelection : TextSelection(), messageStyle);
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			const auto messageIndex = findMessageAtPoint(e->pos());
			if (messageIndex >= 0) {
				const auto &message = _messages[messageIndex];
				const auto textRect = getMessageTextRect(messageIndex);
				const auto localPos = e->pos() - textRect.topLeft();

				// --- Triple-click detection ---
				const qint64 now = QDateTime::currentMSecsSinceEpoch();
				const int interval = QApplication::doubleClickInterval();
				
				if (_clickCount > 0 && (now - _lastClickTime) < interval) {
					if (_clickCount < 3) {
						++_clickCount;
					}
				} else {
					_clickCount = 1;
				}
				_lastClickTime = now;

				const auto textPos = getTextPositionAtPoint(textRect, localPos, message);
				clearMessageSelection();
				_selectedMessageIndex = messageIndex;
				_mouseAction = MouseAction::Selecting;
				_mouseTextSymbol = static_cast<uint16>(textPos);
				if (_clickCount == 3) {
					// Triple-click: select paragraph
					_pressMessageIndex = messageIndex;
					auto selection = TextSelection(textPos, textPos);
					_textSelection = adjustSelectionToParagraphs(GetTextForMessage(message).text, selection);
					_mouseSelectType = TextSelectType::Paragraphs;
				} else {
					// Single click: start letter selection	
					_textSelection = TextSelection(textPos, textPos);
					_mouseSelectType = TextSelectType::Letters; // Reset to letters for new selection
					_pressMessageIndex = messageIndex;
					_inMessageSelectionMode = false; // Ensure we start in text selection mode
				}
				setFocus();
				setCursorForTextSelection();
				update();
			}
			else {
				clearTextSelection();
				clearMessageSelection();
			}
		}
		else {
			clearTextSelection();
			clearMessageSelection();
		}
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		if (_mouseAction == MouseAction::Selecting) {
			if (_inMessageSelectionMode) {
				// We're in message selection mode, update the selection range
				const auto mouseY = e->pos().y();
				const auto currentMessageIndex = findMessageAtY(mouseY);
				if (currentMessageIndex >= 0) {
					updateMessageSelection(_pressMessageIndex, currentMessageIndex);
				}
				setCursor(Qt::ArrowCursor);
				update();
			} else if (_selectedMessageIndex >= 0) {
				// We're in text selection mode
				const auto textRect = getMessageTextRect(_selectedMessageIndex);
				const auto localPos = e->pos() - textRect.topLeft();
				
				// Check if we're still within the same message's text area
				const auto isWithinTextArea = textRect.contains(e->pos());
				
				if (isWithinTextArea) {
					// Continue text selection within the same message
					const auto &message = _messages[_selectedMessageIndex];
					const auto textPos = getTextPositionAtPoint(textRect, localPos, message);
					const auto displayText = GetTextForMessage(message).text;
					auto selection = TextSelection(
						std::min(_mouseTextSymbol, static_cast<uint16>(textPos)),
						std::max(_mouseTextSymbol, static_cast<uint16>(textPos))
					);
					if (_mouseSelectType == TextSelectType::Words) {
						selection = adjustSelectionToWords(displayText, selection);
					} else if (_mouseSelectType == TextSelectType::Paragraphs) {
						selection = adjustSelectionToParagraphs(displayText, selection);
					}
					
					_textSelection = selection;
					clearMessageSelection();
					setCursorForTextSelection();
					update();
				} else {
					// Switch to message selection mode
					_inMessageSelectionMode = true; // Enter message selection mode first
					_mouseSelectType = TextSelectType::Letters; // Reset paragraph selection mode when switching modes
					clearTextSelection(); // Now clear text selection (won't reset _mouseAction)
					_selectedMessageIndex = -1; // Clear text selection state
					// Keep _mouseAction as Selecting to continue processing mouse moves
					
					// Find the message at the current Y position, even if outside message bounds
					const auto currentMessageIndex = findMessageAtY(e->pos().y());
					if (currentMessageIndex >= 0) {
						updateMessageSelection(_pressMessageIndex, currentMessageIndex);
					}
					setCursor(Qt::ArrowCursor);
					update();
				}
			}
		} else {
			// Check if mouse is over a message to show text cursor
			const auto messageIndex = findMessageAtPoint(e->pos());
			if (messageIndex >= 0) {
				setCursor(Qt::IBeamCursor);
			} else {
				setCursor(Qt::ArrowCursor);
			}
		}
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton && _mouseAction == MouseAction::Selecting) {
			// Only clear selection if it's empty and we're not in a special selection mode
			if (_textSelection.from == _textSelection.to && _mouseSelectType == TextSelectType::Letters) {
				clearTextSelection();
			}
			
			// Reset mouse action to end selection mode
			_mouseAction = MouseAction::None;
			
			// Don't reset selection type here - let it persist for continued selection modes
			// Only reset to letters if we're not in a special selection mode
			if (_mouseSelectType != TextSelectType::Paragraphs) {
				_mouseSelectType = TextSelectType::Letters;
			}
			
			// Don't reset click counter here - let it persist for potential triple-click
			
			_inMessageSelectionMode = false; // Reset message selection mode
			
			if (QApplication::clipboard()->supportsSelection()) {
				const auto text = getCopyableSelectionText();
				if (!text.isEmpty()) {
					QApplication::clipboard()->setText(text, QClipboard::Selection);
				}
			}
		}
	}

	void mouseDoubleClickEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			const auto messageIndex = findMessageAtPoint(e->pos());
			if (messageIndex >= 0) {
				const auto &message = _messages[messageIndex];
				const auto textRect = getMessageTextRect(messageIndex);
				const auto localPos = e->pos() - textRect.topLeft();
				
				// Set click counter to 2 for double-click to enable triple-click detection
				// But don't reset it if we're already in paragraph selection mode (clickCount == 3)
				if (_clickCount != 3) {
					_clickCount = 2;
				}
				
				// Clear any existing selections first
				clearMessageSelection();
				
				const auto textPos = getTextPositionAtPoint(textRect, localPos, message);
				const auto displayText = GetTextForMessage(message).text;
				_selectedMessageIndex = messageIndex;
				_mouseTextSymbol = static_cast<uint16>(textPos);
				_mouseAction = MouseAction::Selecting;
				_inMessageSelectionMode = false;
				if (_mouseSelectType == TextSelectType::Paragraphs) {
					auto selection = TextSelection(textPos, textPos);
					_textSelection = adjustSelectionToParagraphs(displayText, selection);
				} else {
					_mouseSelectType = TextSelectType::Words;
					auto selection = TextSelection(textPos, textPos);
					_textSelection = adjustSelectionToWords(displayText, selection);
				}
				
				// Ensure immediate visual feedback
				setFocus();
				setCursorForTextSelection();
				update();
				
				// Force an immediate repaint to show the selection
				repaint();
			} else {
				// Double click outside messages - reset to letter selection mode
				_mouseSelectType = TextSelectType::Letters;
			}
		}
	}

	void keyPressEvent(QKeyEvent *e) override {
		if (e == QKeySequence::Copy) {
			const auto text = getCopyableSelectionText();
			if (!text.isEmpty()) {
				QApplication::clipboard()->setText(text);
			}
			e->accept();
		} else if (e->key() == Qt::Key_Escape) {
			clearTextSelection();
			clearMessageSelection();
			// Reset selection type to letters when pressing Escape
			_mouseSelectType = TextSelectType::Letters;
			e->accept();
		} else {
			RpWidget::keyPressEvent(e);
		}
	}

	void focusOutEvent(QFocusEvent *e) override {
		clearTextSelection();
		clearMessageSelection();
		// Reset selection type to letters when losing focus
		_mouseSelectType = TextSelectType::Letters;
		RpWidget::focusOutEvent(e);
	}

	void resizeEvent(QResizeEvent *e) override {
		RpWidget::resizeEvent(e);
		updateSize();
	}

private:
	enum class MouseAction {
		None,
		Selecting,
	};

	struct MessageBubbleLayout {
		QRect bubbleRect;
		int nextY = 0;
	};

	QRect getMessagesRect() const {
		const auto r = width() > 0 ? rect() : QRect(0, 0, 400, 1000);
		return r.marginsRemoved(QMargins(kMessagesMargin, kMessagesMargin, kMessagesMargin, kMessagesMargin));
	}

	MessageBubbleLayout computeLayoutForMessage(const QRect &messagesRect, int y, const MessageData &message) const {
		const auto msgPadding = st::msgPadding;
		const auto msgMargin = st::msgMargin;
		const auto textAvailableWidth = messagesRect.width() - msgMargin.left() - msgMargin.right() - msgPadding.left() - msgPadding.right();
		const auto maxBubbleWidth = messagesRect.width() - msgMargin.left() - msgMargin.right();

		const auto textSize = getTextSizeForBubble(message, textAvailableWidth);
		const auto bubbleWidth = std::min(
			textSize.width() + msgPadding.left() + msgPadding.right(),
			maxBubbleWidth);
		const auto bubbleHeight = textSize.height() + msgPadding.top() + msgPadding.bottom();

		const int bubbleX = message.isFromUser
			? (messagesRect.right() - bubbleWidth)
			: messagesRect.left();
		const auto bubbleRect = QRect(bubbleX, y, bubbleWidth, bubbleHeight);
		return { bubbleRect, y + bubbleHeight + kMessageSpacing };
	}

	QRect getMessageBubbleRect(int messageIndex) const {
		if (messageIndex < 0 || messageIndex >= static_cast<int>(_messages.size())) {
			return QRect();
		}
		const auto messagesRect = getMessagesRect();
		auto y = messagesRect.top();
		for (int i = 0; i <= messageIndex; ++i) {
			const auto layout = computeLayoutForMessage(messagesRect, y, _messages[i]);
			if (i == messageIndex) {
				return layout.bubbleRect;
			}
			y = layout.nextY;
		}
		return QRect();
	}

	void updateSize() {
		const auto availableWidth = width() > 0 ? width() : 400;
		const QRect area(0, 0, availableWidth, 1000);
		const auto messagesRect = area.marginsRemoved(QMargins(kMessagesMargin, kMessagesMargin, kMessagesMargin, kMessagesMargin));
		auto y = messagesRect.top();
		for (const auto &message : _messages) {
			const auto layout = computeLayoutForMessage(messagesRect, y, message);
			y = layout.nextY;
		}
		const auto totalHeight = _messages.empty() ? 0 : (y - messagesRect.top() - kMessageSpacing + kBottomMargin);
		resize(availableWidth, totalHeight);
	}

	void updateMessageSelection(int fromIndex, int toIndex) {
		if (fromIndex < 0 || toIndex < 0) {
			clearMessageSelection();
			return;
		}

		// Ensure fromIndex <= toIndex
		if (fromIndex > toIndex) {
			std::swap(fromIndex, toIndex);
		}

		// Clear existing selection and add new range
		_selectedMessages.clear();
		for (int i = fromIndex; i <= toIndex; ++i) {
			if (i >= 0 && i < static_cast<int>(_messages.size())) {
				_selectedMessages.insert(i);
			}
		}
	}

	QString getSelectedMessagesText() const {
		if (_selectedMessages.empty()) {
			return QString();
		}
		QStringList texts;
		for (const auto index : _selectedMessages) {
			if (index >= 0 && index < static_cast<int>(_messages.size())) {
				texts.append(GetTextForMessage(_messages[index]).text);
			}
		}
		return texts.join(u"\n\n"_q);
	}

	int findMessageAtPoint(const QPoint &point) const {
		for (size_t i = 0; i < _messages.size(); ++i) {
			if (getMessageBubbleRect(static_cast<int>(i)).contains(point)) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int findMessageAtY(int y) const {
		const auto messagesRect = getMessagesRect();
		auto currentY = messagesRect.top();
		for (size_t i = 0; i < _messages.size(); ++i) {
			const auto layout = computeLayoutForMessage(messagesRect, currentY, _messages[i]);
			if (y >= layout.bubbleRect.top() && y < layout.bubbleRect.bottom()) {
				return static_cast<int>(i);
			}
			currentY = layout.nextY;
		}
		if (y < messagesRect.top() && !_messages.empty()) {
			return 0;
		}
		if (!_messages.empty() && y >= currentY - kMessageSpacing) {
			return static_cast<int>(_messages.size() - 1);
		}
		return -1;
	}

	QRect getMessageTextRect(int messageIndex) const {
		const auto bubbleRect = getMessageBubbleRect(messageIndex);
		return bubbleRect.isEmpty() ? QRect() : bubbleRect.marginsRemoved(st::msgPadding);
	}

	int getTextPositionAtPoint(const QRect &textRect, const QPoint &point, const MessageData &message) const {
		const auto rich = GetTextForMessage(message);
		if (rich.text.isEmpty()) {
			return 0;
		}
		Ui::Text::String textString(st::messageTextStyle, rich, kMarkupTextOptions, textRect.width());
		Ui::Text::StateRequest request;
		request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
		const auto state = textString.getState(point, textRect.width(), request);
		auto symbol = state.symbol;
		if (state.afterSymbol && symbol < rich.text.length()) {
			++symbol;
		}
		return symbol;
	}

	TextSelection adjustSelectionToWords(const QString &text, const TextSelection &selection) const {
		if (text.isEmpty()) {
			return TextSelection();
		}
		
		// If selection is empty, find the word at the position
		if (selection.empty()) {
			// Use the from position as the cursor position
			const auto position = selection.from;
			if (position > text.length()) {
				return TextSelection();
			}
			
			auto start = position;
			auto end = position;
			
			// Expand start to word boundary using Telegram's word separator logic
			while (start > 0 && !Ui::Text::IsWordSeparator(text[start - 1])) {
				--start;
			}
			
			// Expand end to word boundary using Telegram's word separator logic
			while (end < text.length() && !Ui::Text::IsWordSeparator(text[end])) {
				++end;
			}
			
			return TextSelection(start, end);
		}
		
		auto start = qBound(0, static_cast<int>(selection.from), text.length());
		auto end = qBound(0, static_cast<int>(selection.to), text.length());
		if (start > end) {
			std::swap(start, end);
		}

		while (start > 0 && !Ui::Text::IsWordSeparator(text[start - 1])) {
			--start;
		}
		while (end < text.length() && !Ui::Text::IsWordSeparator(text[end])) {
			++end;
		}
		return TextSelection(start, end);
	}

	TextSelection adjustSelectionToParagraphs(const QString &text, const TextSelection &selection) const {
		if (text.isEmpty()) {
			return TextSelection();
		}
		
		// If selection is empty, find the paragraph at the position
		if (selection.empty()) {
			// Use the from position as the cursor position
			const auto position = selection.from;
			if (position > text.length()) {
				return TextSelection();
			}
			
			auto start = position;
			auto end = position;
			
			// Expand start to paragraph boundary
			while (start > 0) {
				if (start >= 2 && text[start - 1] == QChar::LineFeed && text[start - 2] == QChar::LineFeed) {
					break; // Found double newline
				}
				--start;
			}
			
			// Expand end to paragraph boundary
			while (end < text.length()) {
				if (end + 1 < text.length() && text[end] == QChar::LineFeed && text[end + 1] == QChar::LineFeed) {
					break; // Found double newline
				}
				++end;
			}
			
			// Ensure we have a valid selection
			if (start >= end) {
				// If no paragraph boundaries found, select the entire text
				return TextSelection(0, text.length());
			}
			
			return TextSelection(start, end);
		}
		
		auto start = qBound(0, static_cast<int>(selection.from), text.length());
		auto end = qBound(0, static_cast<int>(selection.to), text.length());
		if (start > end) {
			std::swap(start, end);
		}

		while (start > 0) {
			if (start >= 2 && text[start - 1] == QChar::LineFeed && text[start - 2] == QChar::LineFeed) {
				break;
			}
			--start;
		}
		while (end < text.length()) {
			if (end + 1 < text.length() && text[end] == QChar::LineFeed && text[end + 1] == QChar::LineFeed) {
				break;
			}
			++end;
		}
		if (start >= end) {
			return TextSelection(0, text.length());
		}
		return TextSelection(start, end);
	}

	static QSize getTextSizeForBubble(const MessageData &message, int availableWidth) {
		const auto rich = GetTextForMessage(message);
		if (rich.text.isEmpty()) {
			return QSize(0, 0);
		}
		Ui::Text::String textString(st::messageTextStyle, rich, kMarkupTextOptions, availableWidth);
		const auto w = textString.countWidth(availableWidth);
		const auto h = textString.countHeight(availableWidth);
		return QSize(w, h);
	}

	void drawTextWithSelection(Painter &p, const QRect &rect, const MessageData &message, const TextSelection &selection, const Ui::MessageStyle &style) {
		const auto rich = GetTextForMessage(message);
		Ui::Text::String textString(st::messageTextStyle, rich, kMarkupTextOptions, rect.width());
		p.setPen(style.historyTextFg);
		p.setFont(st::msgFont);
		p.save();
		p.setClipRect(rect);
		textString.draw(p, {
			.position = rect.topLeft(),
			.availableWidth = rect.width(),
			.palette = &style.textPalette,
			.selection = selection,
			.useFullWidth = true,
		});
		p.restore();
	}

	QString getCopyableSelectionText() const {
		if (!_textSelection.empty()) {
			return getSelectedText();
		}
		if (!_selectedMessages.empty()) {
			return getSelectedMessagesText();
		}
		return QString();
	}

	QString getSelectedText() const {
		if (_selectedMessageIndex < 0 || _selectedMessageIndex >= static_cast<int>(_messages.size()) || _textSelection.empty()) {
			return QString();
		}
		const auto &message = _messages[_selectedMessageIndex];
		const auto displayText = GetTextForMessage(message).text;
		const auto from = qBound(0, static_cast<int>(_textSelection.from), displayText.length());
		const auto to = qBound(0, static_cast<int>(_textSelection.to), displayText.length());
		return displayText.mid(from, to - from);
	}

	void setCursorForTextSelection() {
		if (_selectedMessageIndex >= 0 && !_textSelection.empty()) {
			setCursor(Qt::IBeamCursor);
		} else {
			setCursor(Qt::ArrowCursor);
		}
	}

	not_null<Ui::ChatStyle*> _chatStyle;
	std::vector<MessageData> _messages;
	
	// Text selection state
	int _selectedMessageIndex = -1;
	TextSelection _textSelection;
	MouseAction _mouseAction = MouseAction::None;
	uint16 _mouseTextSymbol = 0;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	bool _inMessageSelectionMode = false;
	
	// Triple-click detection state
	int _clickCount = 0;
	qint64 _lastClickTime = 0;

	int _pressMessageIndex;

	std::set<int> _selectedMessages;
};

AIChatWidget::AIChatWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	// Create chat style for proper message bubble styling
	_chatStyle = std::make_unique<Ui::ChatStyle>(style::main_palette::get());

	// Create OpenAI client
	_openaiClient = std::make_unique<OpenAIClient>(this);
	_openaiClient->setSessionController(controller);

	// Create clear button
	_clearButton = std::make_unique<Ui::RoundButton>(
		this,
		rpl::single<QString>("Clear"),
		st::defaultActiveButton);
	_clearButton->setClickedCallback([=] { clearChat(); });

	// Create scroll area for messages
	_scroll = std::make_unique<Ui::ScrollArea>(
		this,
		st::historyScroll,
		false);

	// Create messages widget
	_messagesWidget = std::make_unique<MessagesWidget>(
		_scroll.get(),
		_chatStyle.get(),
		_messages);
	
	// Set the messages widget as the scroll area's content
	_scroll->setOwnedWidget(object_ptr<MessagesWidget>::fromRaw(_messagesWidget.get()));

	// Create compose controls
	setupComposeControls();

	// Set up layout
	_clearButton->show();
	_scroll->show();
	_composeControls->show();

	// Handle compose controls height changes
	_composeControls->height(
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, lifetime());

	// Handle send requests
	_composeControls->sendRequests(
	) | rpl::on_next([=](Api::SendOptions options) {
		const auto text = _composeControls->getTextWithAppliedMarkdown().text;
		if (!text.isEmpty()) {
			handleSendMessage(text);
		}
	}, lifetime());

	// Add welcome message
	addAIMessage(kWelcomeMessage);
}

void AIChatWidget::setupComposeControls() {
	// Create a simple history for the compose controls
	auto history = controller()->session().data().history(
		controller()->session().userPeerId());

	_composeControls = std::make_unique<HistoryView::ComposeControls>(
		this,
		HistoryView::ComposeControlsDescriptor{
			.show = controller()->uiShow(),
			.unavailableEmojiPasted = [=](not_null<DocumentData*> emoji) {
				// Handle emoji paste
			},
			.mode = HistoryView::ComposeControlsMode::Normal,
			.sendMenuDetails = [=] { 
				return SendMenu::Details{ .type = SendMenu::Type::SilentOnly };
			},
			.regularWindow = controller(),
			.stickerOrEmojiChosen = controller()->stickerOrEmojiChosen(),
			.features = {
				.likes = false,
				.sendAs = false,
				.ttlInfo = false,
				.botCommandSend = false,
				.silentBroadcastToggle = false,
				.attachBotsMenu = false,
				.inlineBots = false,
				.megagroupSet = false,
				.collectibleStatus = false,
				.stickersSettings = false,
				.openStickerSets = false,
				.autocompleteHashtags = false,
				.autocompleteMentions = false,
				.autocompleteCommands = false,
				.suggestStickersByEmoji = false,
				.commonTabbedPanel = false,
			},
		});

	// Set up the history for compose controls
	_composeControls->setHistory({
		.history = history.get(),
		.topicRootId = MsgId(),
		.monoforumPeerId = PeerId(),
		.showSlowmodeError = [=] { return false; },
		.sendActionFactory = [=] { 
			return Api::SendAction(history, Api::SendOptions());
		},
		.slowmodeSecondsLeft = rpl::single(0),
		.sendDisabledBySlowmode = rpl::single(false),
		.writeRestriction = rpl::single(HistoryView::Controls::WriteRestriction()),
	});

	_composeControls->setAIChatMode();
}

void AIChatWidget::handleSendMessage(const QString &text) {
	if (text.isEmpty()) {
		return;
	}

	// Add user message
	addUserMessage(text);

	// Clear the input field
	_composeControls->clear();

	// Send to OpenAI API
	sendToOpenAI();
}

void AIChatWidget::addMessage(const QString &text, bool isFromUser) {
	_messages.push_back({
		.text = text,
		.isFromUser = isFromUser,
		.timestamp = base::unixtime::now()
	});
	_messagesWidget->setMessages(_messages);
	_scroll->scrollToY(_scroll->scrollTopMax());
}

void AIChatWidget::addUserMessage(const QString &text) {
	addMessage(text, true);
}

void AIChatWidget::addAIMessage(const QString &text) {
	addMessage(text, false);
}

void AIChatWidget::replaceLastAIMessageOrAdd(const QString &text) {
	if (!_messages.empty() && !_messages.back().isFromUser && _messages.back().text == kThinkingMessage) {
		_messages.back().text = text;
		_messagesWidget->setMessages(_messages);
		_scroll->scrollToY(_scroll->scrollTopMax());
	} else {
		addAIMessage(text);
	}
}

void AIChatWidget::paintEvent(QPaintEvent *e) {
	// The main widget doesn't need to paint messages anymore
	// as they are handled by the MessagesWidget inside the scroll area
	Painter p(this);
	// Use a custom light blue background for AI chat
	p.fillRect(rect(), kAIChatBackgroundColor);
}

void AIChatWidget::mousePressEvent(QMouseEvent *e) {
	// If clicking outside the messages widget, clear any selections
	if (_messagesWidget) {
		_messagesWidget->clearTextSelection();
		_messagesWidget->clearMessageSelection();
	}
}

void AIChatWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
	
	// Update messages widget width when the main widget is resized
	_messagesWidget->resizeToWidth(_scroll->width());
}

bool AIChatWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	// This is a simple placeholder widget, so we don't handle any mementos
	return false;
}

QRect AIChatWidget::floatPlayerAvailableRect() {
	// Return the available rect for the float player
	return rect();
}

bool AIChatWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	// Don't handle wheel events for the float player in this placeholder
	return false;
}

void AIChatWidget::updateControlsGeometry() {
	const auto composeHeight = _composeControls->heightCurrent();
	const auto clearButtonHeight = _clearButton->height();
	const auto scrollHeight = height() - composeHeight - clearButtonHeight - 5; // 5px spacing

	// Position clear button at the top right
	_clearButton->move(width() - 90, 0);
	_clearButton->resizeToWidth(80);

	// Position compose controls at the bottom
	_composeControls->move(0, height() - composeHeight);
	_composeControls->resizeToWidth(width());

	// Position scroll area below clear button and above compose controls
	_scroll->setGeometry(0, clearButtonHeight + 5, width(), scrollHeight);
}

void AIChatWidget::sendToOpenAI() {
	if (_openaiClient->isWaitingForResponse()) {
		return;
	}

	addAIMessage(kThinkingMessage);

	_openaiClient->sendChatCompletion(
		_messages,
		[this](const QString &response) {
			replaceLastAIMessageOrAdd(response);
		},
		[this](const QString &error) {
			replaceLastAIMessageOrAdd(QString("❌ Error: %1").arg(error));
		}
	);
}

void AIChatWidget::clearChat() {
	// Clear all messages
	_messages.clear();
	_messagesWidget->setMessages(_messages);
	
	// Scroll to top since there are no messages
	_scroll->scrollToY(0);
	
	// Add welcome message back
	addAIMessage(kWelcomeMessage);
}
} // namespace AI
